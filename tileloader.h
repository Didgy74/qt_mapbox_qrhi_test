#ifndef TILELOADER_H
#define TILELOADER_H

#include <QObject>
#include <QMutex>
#include <QThreadPool>
#include <QNetworkAccessManager>
#include <rhi/qrhi.h>
#include <mutex>


#include <map>
#include <memory>

struct TileCoord {
    int level = 0;
    int x = 0;
    int y = 0;

    // Allows us to use this type as the key in a map.
    [[nodiscard]] bool operator<(TileCoord const& other) const {
        if (level < other.level) { return true; }
        else if (level > other.level) { return false; }

        if (x < other.x) { return true; }
        else if (x > other.x) { return false; }
        if (y < other.y) { return true; }
        else { return false; }
    }
    [[nodiscard]] bool operator==(TileCoord const& other) const {
        return level == other.level && x == other.x && y == other.y;
    }
    [[nodiscard]] bool operator!=(const TileCoord &other) const {
        return !(*this == other);
    }
};

class TileLoaderRequestResult;
class TileLoaderUploadResult;

class TileLoader : public QObject
{
    Q_OBJECT
public:
    explicit TileLoader(QObject *parent = nullptr);
    TileLoader& operator=(const TileLoader&) = delete;
    TileLoader& operator=(TileLoader&&) = delete;

    // Thread-safe
    // This stores the temporary data that can only be cleared
    // when the resource-update-batch has been properly submitted.
    TileLoaderUploadResult* uploadPendingTilesToRhi(QRhi* rhi, QRhiResourceUpdateBatch* batch);

    // Thread-safe
    //
    // The return type needs some way to signal the
    // TileLoader that the tiles are no longer in use.
    [[nodiscard]] TileLoaderRequestResult* requestTiles(QSpan<TileCoord const> tiles);

    class TileLoaderImpl;

    enum class TileProgressState {
        ReadyToRender,
        Pending,
        ReadyForGpuUpload,
        Failed, // Currently unused
    };

    class TileFeature {
    public:
        // The amount of bytes to offset into this Tile's
        // vertex buffer object to get to the first
        // vertex of this feature.
        qint64 vtxByteOffset = 0;

        // The amount of bytes to offset into this Tile's
        // index buffer object to get to the first
        // index of this feature.
        qint64 idxByteOffset = 0;
        // The number of vertices to draw to
        // to draw this feature.
        qint64 idxCount = 0;

        // Nils: I think we should just store all key-values in the layer-level
        // and only store tags for the features, and index into the relevant key-values.
        // This avoids data duplication.
        std::map<QString, QVariant> metaData;
    };

    class TileLayer {
    public:
        QString name;
        std::vector<TileFeature> features;
    };

    class TilePendingFeature {
    public:
        // The amount of bytes to offset into this Tile's
        // vertex buffer object to get to the first
        // vertex of this feature.
        qint64 vtxByteOffset = 0;

        // The amount of bytes to offset into this Tile's
        // index buffer object to get to the first
        // index of this feature.
        qint64 idxByteOffset = 0;
        // The number of vertices to draw to
        // to draw this feature.
        qint64 idxCount = 0;
        std::map<QString, QVariant> metaData;
    };

    class TilePendingLayer {
    public:
        QString name;
        std::vector<TilePendingFeature> features;
    };

    class StoredTile {
    public:
        // Contains the data we want to upload to the GPU and load into the
        // 'layers' member.
        //
        // THIS WILL BE EMPTY ONCE UPLOAD IS SCHEDULED
        std::vector<TilePendingLayer> layersForGpuUpload;
        // THIS WILL BE EMPTY ONCE UPLOAD IS SCHEDULED
        std::vector<QVector2D> verticesForUpload;
        // THIS WILL BE EMPTY ONCE UPLOAD IS SCHEDULED
        std::vector<qint32> indicesForUpload;

        TileProgressState state = {};

        std::vector<TileLayer> layers;
        // Contains all vertices for this tile. This includes all
        // layers and features.
        std::unique_ptr<QRhiBuffer> vertexBuffer;
        // Contains all indices for this tile. This includes all
        // layers and features.
        std::unique_ptr<QRhiBuffer> indexBuffer;
    };

private:
    // We use unique-ptr here to let use the lock in const methods.
    std::unique_ptr<std::mutex> _tileMemoryLock = std::make_unique<std::mutex>();

    // IMPORTANT: This variable is ONLY available when tileMemoryLock is locked.
    std::map<TileCoord, std::unique_ptr<StoredTile>> tileStorage;

    QThreadPool m_threadPool;
    struct ProtobufArenaBaseType {
        virtual ~ProtobufArenaBaseType() {}
    };
    std::vector<std::pair<Qt::HANDLE, std::unique_ptr<ProtobufArenaBaseType>>> m_protobufArenas;
    std::unique_ptr<std::mutex> _protobufArenasLock = std::make_unique<std::mutex>();

    QNetworkAccessManager m_networkAccessMgr;
    QString m_maptilerKey = {};

    friend TileLoaderImpl;

signals:
    void tileLoaded(bool success, TileCoord tile);
};

class TileLoaderRequestResult : public QObject{
    Q_OBJECT

public:
    // I think this should ideally be created as a child
    // of the TileLoader object, but for now we implement it as a
    // standalone object.
    TileLoaderRequestResult() : QObject(nullptr) {

    }
    virtual ~TileLoaderRequestResult() {}
    std::map<TileCoord, TileLoader::StoredTile const*> tiles;
};

class TileLoaderUploadResult : public QObject {
    Q_OBJECT
public:
    // I think this should ideally be created as a child
    // of the TileLoader object, but for now we implement it as a
    // standalone object.
    TileLoaderUploadResult() : QObject(nullptr) {}
    virtual ~TileLoaderUploadResult() {}

    struct TileUploadItem {
        TileUploadItem(
            std::vector<QVector2D>&& vertices,
            std::vector<qint32>&& indices) :
            vertices { std::move(vertices) },
            indices { std::move(indices) }
        {}

        std::vector<QVector2D> vertices;
        std::vector<qint32> indices;
    };
    std::vector<TileUploadItem> tilesForUpload;
};

#endif // TILELOADER_H
