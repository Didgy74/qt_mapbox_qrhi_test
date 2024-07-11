#include "tileloader.h"

#include <QFile>
#include <QDir>
#include <QNetworkReply>
#include <QStandardPaths>

#include <vector_tile.pb.h>

#include "MapboxGeometryDecoding.h"

class TileLoader::TileLoaderImpl {
public:
    static void handleNetworkReply(
        TileLoader& tileLoader,
        TileCoord coord,
        QNetworkReply* reply);

    static void processTile(
        TileLoader& tileLoader,
        TileCoord coord,
        QByteArray byteArray,
        bool writeToFile);

    static void enqueueLoadingJobs(
        TileLoader& tileLoader,
        std::vector<TileCoord>&& jobs);

    struct DecodedTile {
        std::vector<TilePendingLayer> layers;
        std::vector<QVector2D> vertices;
        std::vector<qint32> indices;
    };

    static std::optional<DecodedTile> decodeTileLayers(
        TileLoader& tileLoader,
        QByteArray bytes);

    static google::protobuf::Arena* getProtobufArena(TileLoader& tileLoader) {
        auto threadId = QThread::currentThreadId();

        auto lock = std::lock_guard{ *tileLoader._protobufArenasLock };

        struct ProtobufArena : ProtobufArenaBaseType {
            google::protobuf::Arena arena;
        };

        for (auto const& pair : tileLoader.m_protobufArenas) {
            if (pair.first == threadId) {
                return &static_cast<ProtobufArena*>(pair.second.get())->arena;
            }
        }

        tileLoader.m_protobufArenas.push_back({
            threadId,
            std::make_unique<ProtobufArena>()
        });

        return &static_cast<ProtobufArena*>(tileLoader.m_protobufArenas.back().second.get())->arena;
    }
};

using TileLoaderImpl = TileLoader::TileLoaderImpl;

static QString tileCoordToFilename(TileCoord coord) {
    return QString("z%1x%2y%3.mvt").arg(coord.level).arg(coord.x).arg(coord.y);
}

bool writeNewFileHelper(const QString& path, const QByteArray &bytes)
{
    auto fileInfo = QFileInfo{ path };
    // Grab the directory part of this full filepath.
    QDir dir = fileInfo.dir();

    // QFile won't create our directories for us.
    // We gotta make them ourselves.
    if (!dir.exists() && !dir.mkpath(dir.absolutePath())) {
        return false;
    }

    // QFile won't create our directories for us.
    // We gotta make them ourselves.
    if (!dir.exists() && !dir.mkpath(dir.absolutePath())) {
        return false;
    }

    QFile file { fileInfo.absoluteFilePath() };
    if (file.exists()) {
        return false;
    }

    /* Possible improvement:
     * It's conceivable that a thread might try to read a tile-file
     * that is currently being written to by another thread.
     *
     * To solve this, we might want to introduce a .lock file
     * solution whose presence determines whether a file is currently
     * in use.
     */

    if (!file.open(QFile::WriteOnly)) {
        return false;
    }

    if (file.write(bytes) != bytes.length()) {
        return false;
    }

    return true;
}

static QString makeGetTileUrl(TileCoord coord, QString const& key) {
    QString url = "https://api.maptiler.com/tiles/v3/{z}/{x}/{y}.pbf?key=";

    url = url.replace("{x}", QString::number(coord.x));
    url = url.replace("{y}", QString::number(coord.y));
    url = url.replace("{z}", QString::number(coord.level));
    url = url.append(key);

    return url;
}

TileLoader::TileLoader(QObject *parent) : QObject{ parent }
{
#ifdef MAPTILER_KEY
    m_maptilerKey = MAPTILER_KEY;
#endif
    if (m_maptilerKey == "") {
        // Check for the environment variable for MapTiler key.
        m_maptilerKey = qEnvironmentVariable("MAPTILER_KEY");
    }

    if (m_maptilerKey == "") {
        qFatal("Failed to load MapTiler key.");
    }
}

TileLoaderUploadResult* TileLoader::uploadPendingTilesToRhi(QRhi* rhi, QRhiResourceUpdateBatch* batch)
{
    auto* returnVal = new TileLoaderUploadResult();

    auto autoLock = std::lock_guard{ *this->_tileMemoryLock };

    // Loop through our list of jobs, upload them to GPU and store them
    // our actual storage.

    // Find all tiles that are listed as ReadyToUploadToGpu
    for (auto& keyVal : tileStorage) {
        auto& tile = *keyVal.second;
        if (tile.state != TileProgressState::ReadyForGpuUpload) {
            continue;
        }

        // Create the buffers and schedule the transfer.
        auto vtxBuffer = rhi->newBuffer(
            QRhiBuffer::Immutable,
            QRhiBuffer::VertexBuffer,
            tile.verticesForUpload.size() * sizeof(tile.verticesForUpload[0]));
        if (vtxBuffer == nullptr || !vtxBuffer->create()) {
            // TODO: Handle error
            qFatal("");
        }
        tile.vertexBuffer.reset(vtxBuffer);

        // Create the buffers and schedule the transfer.
        auto idxBuffer = rhi->newBuffer(
            QRhiBuffer::Immutable,
            QRhiBuffer::IndexBuffer,
            tile.indicesForUpload.size() * sizeof(tile.indicesForUpload[0]));
        if (idxBuffer == nullptr || !idxBuffer->create()) {
            // TODO: Handle error
            qFatal("");
        }
        tile.indexBuffer.reset(idxBuffer);

        // MOVE the actual vertices and indices into our return container.
        returnVal->tilesForUpload.push_back({
            std::move(tile.verticesForUpload),
            std::move(tile.indicesForUpload)});

        // Everything went fine, we can now schedule the transfers.
        batch->uploadStaticBuffer(
            vtxBuffer,
            returnVal->tilesForUpload.back().vertices.data());
        batch->uploadStaticBuffer(
            idxBuffer,
            returnVal->tilesForUpload.back().indices.data());



        // Move the data from pending form into finished form.
        tile.layers.reserve(tile.layersForGpuUpload.size());

        for (auto& pendingLayer : tile.layersForGpuUpload) {
            TileLayer finishedLayer = {};
            finishedLayer.name = pendingLayer.name;
            finishedLayer.features.reserve(pendingLayer.features.size());

            for (auto& pendingFeature : pendingLayer.features) {
                TileFeature finishedFeature = {};
                finishedFeature.metaData = std::move(pendingFeature.metaData);
                finishedFeature.idxByteOffset = pendingFeature.idxByteOffset;
                finishedFeature.idxCount = pendingFeature.idxCount;

                finishedFeature.vtxByteOffset = pendingFeature.vtxByteOffset;

                finishedLayer.features.push_back(std::move(finishedFeature));
            }

            tile.layers.push_back(std::move(finishedLayer));
        }

        // Finally, change this tile's state to ready to render.
        tile.layersForGpuUpload = {};
        tile.state = TileProgressState::ReadyToRender;
    }

    return returnVal;
}

TileLoaderRequestResult* TileLoader::requestTiles(QSpan<TileCoord const> requestedTiles)
{
    // TODO: deduplicate input list
    std::vector<TileCoord> loadJobs;

    auto* outResult = new TileLoaderRequestResult();

    // Create scope for the mutex lock.
    {
        auto autoLock =  std::lock_guard{ *this->_tileMemoryLock };
        for (auto const& requestedCoord : requestedTiles) {

            // Check if the requested coord is already loaded.
            // A tile can be in the state of:
            // Ready to be displayed
            // Currently being loaded
            // Failed to load

            // We are not checking if our
            // tile is already in the processing stage!
            auto tileIt = tileStorage.find(requestedCoord);
            if (tileIt != tileStorage.end()) {
                auto const& tile = *tileIt->second;
                if (tile.state == TileProgressState::ReadyToRender) {
                    // Tile is ready.
                    // Return it from this function.
                    outResult->tiles.insert({ requestedCoord, &tile });

                    // Note: The user might eventually want to know
                    // about tiles that are failed also?
                }

                // If the tile exists in memory but is not ready to render,
                // it means it's otherwise being processed.
                // We can ignore this result.
            } else {
                // Not found. Queue it for loading.
                loadJobs.push_back(requestedCoord);

                // Insert a new tile with state pending.
                StoredTile newTileItem = {};
                newTileItem.state = TileProgressState::Pending;
                tileStorage.insert({
                    requestedCoord,
                    std::make_unique<StoredTile>(std::move(newTileItem)) });
            }
        }
    }

    // We have some load Jobs, fire them up.
    if (!loadJobs.empty()) {
        TileLoaderImpl::enqueueLoadingJobs(*this, std::move(loadJobs));
    }

    return outResult;
}

void TileLoaderImpl::enqueueLoadingJobs(TileLoader& tileLoader, std::vector<TileCoord>&& jobs) {
    // All the following code runs on a separate thread. This function returns immediately.
    tileLoader.m_threadPool.start([jobs = std::move(jobs), &tileLoader]() {

        // For each tile we want to load, check if they're in
        // disk cache. Every tile that is in disk cache can start
        // being loaded on a thread immediately.
        // Any tile that needs to be networked, needs to be queued up
        // for download on the NetworkAccessManager's thread.

        std::vector<TileCoord> tilesToDownload;

        for (auto const& jobCoord : jobs) {

            QString basePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

            auto dir = QDir::cleanPath(
                basePath + QDir::separator() +
                "tiles" + QDir::separator() +
                "maptiler_planet" + QDir::separator() +
                tileCoordToFilename(jobCoord));

            QFile file{ dir };
            if (file.exists()) {
                // Found the file. Move the processing onto a separate thread.
                // One async task per tile.
                //
                // TODO: Move the following code into it's own function.
                tileLoader.m_threadPool.start([=, &tileLoader]() {
                    QFile file { dir };
                    bool openSuccess = file.open(QFile::ReadOnly);
                    if (!openSuccess) {
                        // Found the file but unable to read it. Bug?

                    }
                    QByteArray tileBytes = file.readAll();
                    TileLoaderImpl::processTile(
                        tileLoader,
                        jobCoord,
                        tileBytes,
                        false);

                });

            } else {
                // File doesn't exist. Queue it for downloading.
                tilesToDownload.push_back(jobCoord);
            }
        }

        if (!tilesToDownload.empty()) {

            // We are now on a thread inside the thread-pool.
            // Downloads need to be invoked on the same thread as the NetworkAccessManager.
            // Queue it up for execution.
            QMetaObject::invokeMethod(
                &tileLoader.m_networkAccessMgr,
                [=, &tileLoader]() { // Capture by value to not get lifetime issues.

                    // Create one request for every tile
                    for (auto const& loadJobCoord : tilesToDownload) {

                        QNetworkRequest req = { };
                        auto url = makeGetTileUrl(loadJobCoord, tileLoader.m_maptilerKey);
                        req.setUrl(QUrl{ url });

                        // Handle the reply
                        auto reply = tileLoader.m_networkAccessMgr.get(req);
                        QObject::connect(
                            reply,
                            &QNetworkReply::finished,
                            &tileLoader,
                            [=, &tileLoader]() {
                                // TODO error handling.
                                TileLoaderImpl::handleNetworkReply(
                                    tileLoader,
                                    loadJobCoord,
                                    reply);
                            });
                    }
                });
        }
    });
}

void TileLoaderImpl::handleNetworkReply(
    TileLoader& tileLoader,
    TileCoord tileCoord,
    QNetworkReply* reply)
{
    // This will be called on the thread belonging to the QNetworkAccessManager.
    //
    // We just want to do error checking, then extract the information we want
    // and then offload the rest of the processing to another thread.
    reply->deleteLater();

    if (!reply->isFinished()) {
        qFatal("Developer error");
    }

    if (reply->error() != QNetworkReply::NoError) {

        QVariant contentType = reply->header(QNetworkRequest::ContentTypeHeader);
        if (contentType == "text/plain;charset=UTF-8") {
            QByteArray byteArray = reply->readAll();
            auto errorString = QString::fromUtf8(byteArray);
        }

        qFatal("Unimplemented");
    }

    QVariant contentType = reply->header(QNetworkRequest::ContentTypeHeader);
    if (contentType != "application/x-protobuf") {
        // Error handling, we got back an unexpected reply.
        qFatal("Unimplemented");
    }

    QByteArray byteArray = reply->readAll();

    tileLoader.m_threadPool.start([=, &tileLoader]() {
        // QByteArray has COW semantics, so we can just capture by value here...
        processTile(
            tileLoader,
            tileCoord,
            byteArray,
            true);
    });
}

void TileLoaderImpl::processTile(
    TileLoader& tileLoader,
    TileCoord tileCoord,
    QByteArray tileBytes,
    bool writeToFile)
{
    auto decodedTileOpt = TileLoaderImpl::decodeTileLayers(tileLoader, tileBytes);
    if (!decodedTileOpt.has_value()) {
        qFatal("");
    }
    auto& decodedTile = decodedTileOpt.value();

    // We've decoded the tile but we can't upload it to the GPU until later
    // when we have access to QRhi.
    // Push onto a list of pending

    {
        // Append this job to our list of pending GPU uploads.
        auto autoLock = std::lock_guard{ *tileLoader._tileMemoryLock };
        // Find the tile-element we're processing
        // We assume this element already exists, and is in the pending state.
        // If it's not, we fucked up big time.
        auto tileIt = tileLoader.tileStorage.find(tileCoord);
        if (tileIt == tileLoader.tileStorage.end()) {
            qFatal(
                "Tried to set a tile for being ready for GPU transfers, "
                "but couldn't find existing tile-node.");
        }

        auto& tile = *tileIt->second;
        if (tile.state != TileProgressState::Pending) {
            qFatal(
                "Tried to set a tile for being ready for GPU transfers, "
                "but existing tile-node was not in state 'Pending'.");
        }

        tile.state = TileProgressState::ReadyForGpuUpload;

        tile.verticesForUpload = std::move(decodedTile.vertices);
        tile.indicesForUpload = std::move(decodedTile.indices);
        tile.layersForGpuUpload = std::move(decodedTile.layers);

        // Now we can signal that this tile is ready
        emit tileLoader.tileLoaded(true, tileCoord);
    }

    if (writeToFile) {
        // Then we write to the disk cache.
        QString basePath = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);

        auto dir = QDir::cleanPath(
            basePath + QDir::separator() +
            "tiles" + QDir::separator() +
            "maptiler_planet" + QDir::separator() +
            tileCoordToFilename(tileCoord));
        bool fileWriteSuccess = writeNewFileHelper(dir, tileBytes);
        if (!fileWriteSuccess) {
            qFatal("Unable to write to file.");
        }
    }
}

std::optional<TileLoaderImpl::DecodedTile> TileLoaderImpl::decodeTileLayers(
    TileLoader& tileLoader,
    QByteArray bytes)
{
    auto protobufArena = TileLoaderImpl::getProtobufArena(tileLoader);
    auto arenaCleanup = QScopeGuard{ [&]() {
        protobufArena->Reset();
    }};
    auto tile = google::protobuf::Arena::CreateMessage<vector_tile::Tile>(protobufArena);

    if (!tile->ParseFromArray(bytes.data(), bytes.size())) {
        qFatal("Tile parsing error.");
    }

    // Decode the layers into our own internal data-type

    DecodedTile decodedTile;
    for (auto const& inLayer : tile->layers()) {

        TilePendingLayer outLayer = {};
        outLayer.name = QString::fromStdString(inLayer.name());

        auto const& layerKeys = inLayer.keys();
        auto const& layerValues = inLayer.values();

        for (auto const& inFeature : inLayer.features()) {
            if (inFeature.type() != vector_tile::Tile::GeomType::Tile_GeomType_POLYGON) {
                continue;
            }

            TilePendingFeature outFeature = {};

            auto const& inTags = inFeature.tags();

            // Populate the meta-data for this feature.
            if (inTags.size() % 2 != 0) {
                qFatal("Incorrect tag count");
            }

            for(int i = 0; i <= inTags.size() - 2; i += 2){
                int keyIndex = inTags[i];
                int valueIndex = inTags[i + 1];
                auto const& key = layerKeys[keyIndex];
                auto const& value = layerValues[valueIndex];

                if (value.has_bool_value()) {
                    outFeature.metaData.insert({ QString::fromStdString(key), value.bool_value() });
                } else if (value.has_double_value()) {
                    outFeature.metaData.insert({ QString::fromStdString(key), value.double_value() });
                } else if (value.has_float_value()) {
                    outFeature.metaData.insert({ QString::fromStdString(key), value.float_value() });
                } else if (value.has_int_value()) {
                    outFeature.metaData.insert({ QString::fromStdString(key), QVariant::fromValue(value.int_value()) });
                } else if (value.has_sint_value()) {
                    outFeature.metaData.insert({ QString::fromStdString(key), value.has_sint_value() });
                } else if (value.has_string_value()) {
                    outFeature.metaData.insert({
                        QString::fromStdString(key),
                        QString::fromStdString(value.string_value()) });
                } else if (value.has_uint_value()) {
                    outFeature.metaData.insert({ QString::fromStdString(key), value.has_uint_value() });
                } else {
                    qFatal("");
                }
            }

            outFeature.vtxByteOffset = decodedTile.vertices.size() * sizeof(decodedTile.vertices[0]);
            outFeature.idxByteOffset = decodedTile.indices.size() * sizeof(decodedTile.indices[0]);
            auto const& encodedGeometry = inFeature.geometry();
            try {
                auto decodedGeometry = ProtobufFeatureToPolygon(encodedGeometry);
                for (auto const& item : decodedGeometry.first) {
                    decodedTile.vertices.push_back({ (float)item.x, (float)item.y });
                }
                outFeature.idxCount = decodedGeometry.second.size();
                for (auto const& item : decodedGeometry.second) {
                    decodedTile.indices.push_back(item);
                }

            } catch (std::exception& e) {
                // If we couldn't triangulate this one, pretend it doesn't exist
                //qFatal("Error!!");
                //return std::nullopt;
                continue;
            }

            outLayer.features.push_back(std::move(outFeature));
        }

        decodedTile.layers.push_back(std::move(outLayer));
    }

    return decodedTile;
}
