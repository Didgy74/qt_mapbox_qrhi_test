#include "qquickmap.h"

#include <QFile>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGRenderNode>
#include <QProtobufSerializer>
#include <rhi/qrhi.h>
#include <QPainterPath>
#include <QScopeGuard>
#include <QSpan>

#include <LayerStyle.h>
#include "Evaluator.h"
#include "MapboxGeometryDecoding.h"

#include <memory>
#include <optional>

#include "vector_tile.qpb.h"

namespace Bach {
    // Temporary, not gonna need it in the future.
    double normalizeValueToZeroOneRange(double value, double min, double max)
    {
        const double epsilon = 0.0001;
        // Return 0 if the divisor is approaching 0 (illegal mathematically).
        //
        // Note that the result will approach infinity if the divisor is
        // approaching 0. We return 0.0 here to keep the returned
        // value within the required [0, 1] space.
        if(max-min < epsilon)
            return 0.0;
        else
            return (value - min) / (max - min);
    }
}

std::map<QString, QVariant> populateFeatureMetaData(
    QSpan<unsigned int const> tags,
    QSpan<QString const> keys,
    QSpan<vector_tile::Tile_QtProtobufNested::Value const> values)
{
    //The feature's keys list is the features metadata encoded using the tile's values list.
    //The kayes list length is always expected to be even.
    //the nth element in the keys list corresponds to the (n/2)th element in the metadata map after decoding,
    // and the n+1 th element in the keys list correspond to the value for the (n/2) element in the metadata map after decoding.
    //The keys elements' values map to the tile values list's indecies.
    if (tags.size() < 2) {
        qFatal("Incorrect tag count");
        return {};
    }


    std::map<QString, QVariant> returnData;

    for(int i = 0; i <= tags.size() - 2; i += 2){
        int keyIndex = tags[i];
        int valueIndex = tags[i + 1];
        auto const& key = keys[keyIndex];

        auto const& value = values[valueIndex];

        if (value.hasStringValue()) {
            returnData.insert({key, QVariant(value.stringValue())});
        } else if (value.hasFloatValue()) {
            returnData.insert({key, QVariant(value.floatValue())});
        } else if (value.hasDoubleValue()) {
            returnData.insert({key, QVariant(value.doubleValue())});
        } else if (value.hasIntValue()) {
            returnData.insert({key, QVariant::fromValue<QtProtobuf::int64>(value.intValue())});
        } else if (value.hasUintValue()) {
            returnData.insert({key, QVariant::fromValue<QtProtobuf::uint64>(value.uintValue())});
        } else if (value.hasSintValue()) {
            returnData.insert({key, QVariant::fromValue<QtProtobuf::sint64>(value.sintValue())});
        } else if (value.hasBoolValue()) {
            returnData.insert({key, QVariant(value.boolValue())});
        }
    }

    return returnData;
}


static bool isLayerShown(const StyleSheet::AbstractLayerStyle &layerStyle, int mapZoom)
{
    return
        layerStyle.m_visibility &&
        mapZoom < layerStyle.m_maxZoom &&
        mapZoom >= layerStyle.m_minZoom;
}

static bool showFeature(
    StyleSheet::AbstractLayerStyle const& layerStyle,
    std::map<QString, QVariant> const& featureMetaData,
    int mapZoom,
    double vpZoom)
{
    if (layerStyle.m_filter.isEmpty())
        return true;
    return Evaluator::resolveExpression(
       layerStyle.m_filter,
       featureMetaData,
       mapZoom,
       vpZoom).toBool();
}

// Represents the position of a tile within the maps grid at a given zoom level.
//
// This is the C++ equivalent of the tile-position-triplet in the report.
struct TileCoord {
    /* Map zoom level of this tile's position. Range [0, 16].
     */
    int zoom = 0;

    /* X direction index-coordinate of this tile.
     *
     * Should always be in the range [0, tilecount-1]
     * where tilecount = 2^zoom
     */
    int x = 0;
    /* Y direction index-coordinate of this tile.
     *
     * Should always be in the range [0, tilecount-1]
     * where tilecount = 2^zoom
     */
    int y = 0;

    QString toString() const;

    // Define less-than operator, equality operator, and inequality
    // operator in order to allow using this type as a key in QMap.
    bool operator<(const TileCoord &other) const;
    bool operator==(const TileCoord &other) const;
    bool operator!=(const TileCoord &other) const;
};

/*!
 * \internal
 *
 * \brief The TileScreenPlacement struct describes a tile's
 * position and size within the viewport.
 */
struct TileScreenPlacement {
    double pixelPosX;
    double pixelPosY;
    double pixelWidth;
};

/*!
 * \internal
 *
 * \brief The TilePosCalculator class
 * is a helper class for positioning tiles within the viewport.
 */
class TilePosCalculator {
    double vpWidth;
    double vpHeight;
    double vpX;
    double vpY;
    double vpZoom;
    int mapZoom;

private:
    // Largest dimension between viewport height and width, expressed in pixels.
    double VpMaxDim() const { return qMax(vpWidth, vpHeight); }

    // Aspect ratio of the viewport, as a scalar.
    double VpAspect() const { return (double)vpWidth / (double)vpHeight; }

    // The scale of the world map as a scalar fraction of the viewport.
    // Example: A value of 2 means the world map can fit 2 viewports in X and Y directions.
    double WorldmapScale() const { return pow(2, vpZoom); }

    // Size of an individual tile as a fraction of the world map.
    //
    // Exmaple: A value of 0.5 means the tile takes up half the length of the world map
    // in X and Y directions.
    double TileSizeNorm() const { return WorldmapScale() / (1 << mapZoom); }

public:
    /*!
     * \brief calcTileSizeData
     * Calculates the on-screen position information of a specific tile.
     *
     * \param coord The cooardinates of the tile wanted.
     * \return The TileScreenPlacement with the correct data.
     */
    TileScreenPlacement calcTileSizeData(TileCoord coord) const {
        // Calculate where the top-left origin of the world map is relative to the viewport.
        double worldOriginX = vpX * WorldmapScale() - 0.5;
        double worldOriginY = vpY * WorldmapScale() - 0.5;

        // Adjust the world such that our worldmap is still centered around our center-coordinate
        // when the aspect ratio changes.
        if (VpAspect() < 1.0) {
            worldOriginX += -0.5 * VpAspect() + 0.5;
        } else if (VpAspect() > 1.0) {
            worldOriginY += -0.5 / VpAspect() + 0.5;
        }

        // The position of this tile expressed in world-normalized coordinates.
        double posNormX = (coord.x * TileSizeNorm()) - worldOriginX;
        double posNormY = (coord.y * TileSizeNorm()) - worldOriginY;

        TileScreenPlacement out;
        out.pixelPosX = posNormX * VpMaxDim();
        out.pixelPosY = posNormY * VpMaxDim();

        // Calculate the width of a tile as it's displayed on-screen.
        // (Height is same as width, perfectly square)
        out.pixelWidth = TileSizeNorm() * VpMaxDim();

        return out;
    }

    /*!
     * \internal
     * \brief createTilePosCalculator constructs a TilePosCalculator
     * object based on the current state of the viewport.
     *
     * \param vpWidth The width of the viewport in pixels on screen.
     * \param vpHeight The height of the viewport in pixels on screen.
     * \param vpX The center X coordinate of the viewport in
     * world-normalized coordinates.
     * \param vpY The center Y coordinate of the viewport in
     * world-normalized coordinates.
     * \param vpZoom The current zoom-level of the viewport.
     * \param mapZoom The current zoom-level of the map.
     * \return The TilePosCalculator object that can be used to
     * position tiles correctly on-screen.
     */
    static TilePosCalculator create(
        int vpWidth,
        int vpHeight,
        double vpX,
        double vpY,
        double vpZoom,
        int mapZoom)
    {
        TilePosCalculator out;
        out.vpWidth = vpWidth;
        out.vpHeight = vpHeight;
        out.vpX = vpX;
        out.vpY = vpY;
        out.vpZoom = vpZoom;
        out.mapZoom = mapZoom;
        return out;
    }

};


// Empty default destructor
QQuickMap::RhiStuffPimplT::~RhiStuffPimplT() {}

class QQuickMap::RhiStuffImpl : public QQuickMap::RhiStuffPimplT {
public:
};

QQuickMap::RhiStuffImpl* QQuickMap::getRhiStuff() {
    return static_cast<RhiStuffImpl*>(rhiStuff.get());
}

class MyCustomRenderNode : public QSGRenderNode {
public:
    QQuickMap* sourceWidget = nullptr;
    QQuickWindow* window = nullptr;

    QRhiBuffer* m_uniformBuffer = nullptr;
    struct UniformType {
        float tilePos[2] = { 0, 0 };
        float _padding[2] = { 0, 0 };
        float color[4] = { 0, 0, 0, 1.0 };
        // Dynamic uniform buffers need stride to be multiple of
        // 256 bytes for now...
        char _padding2[256 - 32] = {};
    };
    std::vector<UniformType> m_uniforms = {};
    class DrawCmd {
    public:
        QRhiBuffer* vtxBuffer;
        QRhiBuffer* idxBuffer;
        int idxCount;
    };
    std::vector<DrawCmd> m_drawCmds = {};

    QRhiShaderResourceBindings* m_resourceBindings = nullptr;
    QRhiShaderResourceBindings* m_resourceBindingsLayout = nullptr;
    QRhiGraphicsPipeline* m_pipeline = nullptr;

    bool shaderInitialized = false;

    class BackgroundRhiResources {
    public:
        QRhiShaderResourceBindings* resourceBindings = nullptr;
        QRhiGraphicsPipeline* pipeline = nullptr;
        QRhiBuffer* uniformBuffer = {};
        float color[4] = {};
    };
    BackgroundRhiResources backgroundRhi = {};

    bool loadedTile = false;

    class Feature {
    public:
        // Eventually we do not need to store the
        // actual vertices or the indices. Just the
        // index count.
        // To do: Make each feature act as a span into
        // one big buffer for the entire layer.
        //
        // For now it's useful to store them long-term
        // Because they need to live until they get batch-uploaded
        // to the GPU.

        QRhiBuffer* vertexBuffer = {};
        std::vector<QVector2D> vertices;
        QRhiBuffer* indexBuffer = {};
        std::vector<qint32> indices;

        // Nils: I think we should just store all key-values in the layer-level
        // and only store tags for the features, and index into the relevant key-values.
        std::map<QString, QVariant> metaData;
    };

    class Layer {
    public:
        QString name;
        std::vector<Feature> features;
    };

    std::vector<Layer> m_layers;

    StyleSheet m_styleSheet;

    explicit MyCustomRenderNode(QQuickWindow *window, QQuickMap* quickItem) :
        window{ window },
        sourceWidget{ quickItem }
    {}

	QSGRenderNode::RenderingFlags flags() const override
	{
		// We are rendering 2D content directly into the scene graph using QRhi, no
		// direct usage of a 3D API. Hence NoExternalRendering. This is a minor
		// optimization.

		// Additionally, the node takes the item transform into account by relying
		// on projectionMatrix() and matrix() (see prepare()) and never rendering at
		// other Z coordinates. Hence DepthAwareRendering. This is a potentially
		// bigger optimization.

        return
            QSGRenderNode::NoExternalRendering |
            QSGRenderNode::DepthAwareRendering |
            QSGRenderNode::BoundedRectRendering |
            QSGRenderNode::OpaqueRendering;
	}

	QSGRenderNode::StateFlags changedStates() const override
	{
		// In Qt 6 only ViewportState and ScissorState matter, the rest is ignored.
        return QSGRenderNode::StateFlag::ViewportState | QSGRenderNode::StateFlag::ScissorState;
	}

    void loadBackgroundShader(QRhi* rhi);

    void loadFillShaderResourceBindingsLayout(QRhi* rhi);
    void loadFillShader(QRhi* rhi);

    void loadTile(QRhi* rhi, QRhiResourceUpdateBatch* batch);

    void prepareDrawCommands();

    virtual void prepare() override {
		auto* rhi = window->rhi();

        auto* batch = rhi->nextResourceUpdateBatch();

        if (backgroundRhi.pipeline == nullptr) {
            loadBackgroundShader(rhi);
        }

        if (m_resourceBindingsLayout == nullptr) {
            loadFillShaderResourceBindingsLayout(rhi);
        }

        if (!shaderInitialized) {
            loadFillShader(rhi);
            shaderInitialized = true;
        }

        if (!loadedTile)
        {
            loadTile(rhi, batch);
            loadedTile = true;
        }

        if (m_uniformBuffer == nullptr) {
            prepareDrawCommands();

            m_uniformBuffer = rhi->newBuffer(
                QRhiBuffer::Dynamic, // Must always be dynamic when uniform buffer.
                QRhiBuffer::UniformBuffer,
                m_uniforms.size() * sizeof(m_uniforms[0]));
            if (!m_uniformBuffer->create()) {
                qFatal() << "Failed to create uniform buffer";
            }
            batch->updateDynamicBuffer(
                m_uniformBuffer,
                0,
                m_uniforms.size() * sizeof(m_uniforms[0]),
                m_uniforms.data());

            if (m_resourceBindings == nullptr) {
                m_resourceBindings = rhi->newShaderResourceBindings();

                m_resourceBindings->setBindings({
                    QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                        0,
                        QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                        m_uniformBuffer,
                        sizeof(m_uniforms[0]))
                });

                m_resourceBindings->create();
            }

        }

        // Find a background layer
        {
            BackgroundLayerStyle const* backLayer = nullptr;
            for (auto const& layerStyle : m_styleSheet.m_layerStyles) {
                if (layerStyle->type() == StyleSheet::LayerType::Background) {
                    backLayer = static_cast<BackgroundLayerStyle const*>(layerStyle.get());
                    break;
                }
            }
            if (backLayer == nullptr) {
                qFatal("Unimplemented");
            } else {
                auto color = backLayer->getColor(0);

                backgroundRhi.color[0] = color.redF();
                backgroundRhi.color[1] = color.greenF();
                backgroundRhi.color[2] = color.blueF();
                backgroundRhi.color[3] = color.alphaF();
            }

            batch->updateDynamicBuffer(
                backgroundRhi.uniformBuffer,
                0,
                sizeof(backgroundRhi.color),
                backgroundRhi.color);
        }

        commandBuffer()->resourceUpdate(batch);
    }

    virtual void render(const QSGRenderNode::RenderState *state) override {
        auto* cb = commandBuffer();

        // The QQuickItem canvas has Y pointing down.
        // The QRhi canvas is Y pointing up.
        // We need to fix.

        auto pixelRatio = renderTarget()->devicePixelRatio();
        auto renderTargetSize = renderTarget()->pixelSize().toSizeF();

        auto x = sourceWidget->x() * pixelRatio;
        auto y = sourceWidget->y() * pixelRatio;
        auto width = sourceWidget->width() * pixelRatio;
        auto height = sourceWidget->height() * pixelRatio;
        // Adjust Y coordinate to account for the inverted Y-axis
        float invertedY = renderTargetSize.height() - (y + height);

        // Use the adjusted Y coordinate for rendering
        cb->setViewport({
            (float)x,
            (float)invertedY,
            (float)width,
            (float)height
        });

        {
            // Render background
            cb->setGraphicsPipeline(backgroundRhi.pipeline);
            cb->setShaderResources();
            cb->draw(4);
        }

		cb->setGraphicsPipeline(m_pipeline);

        // TODO: WARNING! Pretty sure this is a race condition
        // Should be uploaded during QQuickMap::updatePaintingNode instead
        // I think
        auto vpZoom = sourceWidget->getViewportZoom();
        auto vpX = sourceWidget->getViewportX();
        auto vpY = sourceWidget->getViewportY();

        auto tilePosCalc = TilePosCalculator::create(
            width,
            height,
            vpX,
            1 - vpY,
            vpZoom,
            0);
        auto tilePos = tilePosCalc.calcTileSizeData({0, 0, 0});

        // Use the adjusted Y coordinate for rendering
        cb->setViewport({
            (float)(x + tilePos.pixelPosX),
            (float)(invertedY + tilePos.pixelPosY),
            (float)tilePos.pixelWidth,
            (float)tilePos.pixelWidth
        });
        cb->setScissor(QRhiScissor{
            (int)x,
            (int)invertedY,
            (int)width,
            (int)height
        });

        for (int i = 0; i < m_drawCmds.size(); i++) {
            auto const& drawCmd = m_drawCmds[i];

            auto dynOffset = QRhiCommandBuffer::DynamicOffset(0, sizeof(UniformType) * i );
            cb->setShaderResources(m_resourceBindings, 1, &dynOffset);

            QRhiCommandBuffer::VertexInput vertexInputs[] = { { drawCmd.vtxBuffer, 0 } };

            cb->setVertexInput(
                0,
                1,
                vertexInputs,
                drawCmd.idxBuffer,
                0,
                QRhiCommandBuffer::IndexUInt32);

            cb->drawIndexed(drawCmd.idxCount, 1, 0, 0, 0);
        }
    }
};

QQuickMap::QQuickMap(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    auto buttons = Qt::MouseButtons{};
    buttons.setFlag(Qt::LeftButton);
    setAcceptedMouseButtons(buttons);
}

QSGNode* QQuickMap::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    auto node = oldNode;
    if (oldNode == nullptr) {
        node = new MyCustomRenderNode(window(), this);
    }

    return node;
}

void MyCustomRenderNode::loadBackgroundShader(QRhi* rhi) {
    backgroundRhi.uniformBuffer = rhi->newBuffer(
        QRhiBuffer::Dynamic,
        QRhiBuffer::UniformBuffer,
        sizeof(backgroundRhi.color));
    backgroundRhi.uniformBuffer->create();

    backgroundRhi.resourceBindings = rhi->newShaderResourceBindings();
    backgroundRhi.resourceBindings->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0,
            QRhiShaderResourceBinding::FragmentStage,
            backgroundRhi.uniformBuffer)
    });
    backgroundRhi.resourceBindings->create();

    QFile file;
    file.setFileName(":/shaders/background.vert.qsb");
    if (!file.open(QFile::ReadOnly))
        qFatal("Failed to load vertex shader");
    auto vtxShaderStage = QRhiShaderStage(
        QRhiShaderStage::Vertex,
        QShader::fromSerialized(file.readAll()));

    file.close();
    file.setFileName(":/shaders/background.frag.qsb");
    if (!file.open(QFile::ReadOnly))
        qFatal("Failed to load fragment shader");
    auto fragShaderStage = QRhiShaderStage(
        QRhiShaderStage::Fragment,
        QShader::fromSerialized(file.readAll()));


    backgroundRhi.pipeline = rhi->newGraphicsPipeline();
    backgroundRhi.pipeline->setFrontFace(QRhiGraphicsPipeline::CCW);
    backgroundRhi.pipeline->setCullMode(QRhiGraphicsPipeline::None);
    backgroundRhi.pipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    backgroundRhi.pipeline->setPolygonMode(QRhiGraphicsPipeline::PolygonMode::Fill);
    QRhiGraphicsPipeline::TargetBlend blend = {};
    blend.enable = true;
    backgroundRhi.pipeline->setTargetBlends({ blend });
    backgroundRhi.pipeline->setShaderResourceBindings(backgroundRhi.resourceBindings);
    backgroundRhi.pipeline->setShaderStages({vtxShaderStage, fragShaderStage});
    backgroundRhi.pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());
    backgroundRhi.pipeline->create();
}

void MyCustomRenderNode::loadFillShaderResourceBindingsLayout(QRhi* rhi)
{
    m_resourceBindingsLayout = rhi->newShaderResourceBindings();
    m_resourceBindingsLayout->setBindings({
        QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
            0,
            QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
            nullptr,
            sizeof(UniformType))
    });

    m_resourceBindingsLayout->create();
}

void MyCustomRenderNode::loadFillShader(QRhi* rhi) {
    QFile file;
    file.setFileName(":/shaders/shader.vert.qsb");
    if (!file.open(QFile::ReadOnly))
        qFatal("Failed to load vertex shader");
    auto vtxShaderStage = QRhiShaderStage(
        QRhiShaderStage::Vertex,
        QShader::fromSerialized(file.readAll()));

    file.close();
    file.setFileName(":/shaders/shader.frag.qsb");
    if (!file.open(QFile::ReadOnly))
        qFatal("Failed to load fragment shader");
    auto fragShaderStage = QRhiShaderStage(
        QRhiShaderStage::Fragment,
        QShader::fromSerialized(file.readAll()));

    m_pipeline = rhi->newGraphicsPipeline();
    m_pipeline->setFrontFace(QRhiGraphicsPipeline::CCW);
    m_pipeline->setCullMode(QRhiGraphicsPipeline::None);
    m_pipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    m_pipeline->setPolygonMode(QRhiGraphicsPipeline::PolygonMode::Fill);

    QRhiGraphicsPipeline::TargetBlend blend = {};
    blend.enable = true;
    m_pipeline->setTargetBlends({ blend });

    m_pipeline->setShaderResourceBindings(m_resourceBindingsLayout);
    m_pipeline->setShaderStages({ vtxShaderStage, fragShaderStage });

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({ { 2 * sizeof(float) } });
    inputLayout.setAttributes({ { 0, 0, QRhiVertexInputAttribute::Float2, 0 } });
    m_pipeline->setVertexInputLayout(inputLayout);

    m_pipeline->setRenderPassDescriptor(renderTarget()->renderPassDescriptor());

    m_pipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);

    m_pipeline->create();
}

void MyCustomRenderNode::loadTile(
    QRhi* rhi,
    QRhiResourceUpdateBatch* batch)
{
    QProtobufSerializer serializer;

    QFile tileFile = { ":z0x0y0.mvt" };
    tileFile.open(QFile::ReadOnly);
    auto tileBytes = tileFile.readAll();

    vector_tile::Tile tile;
    tile.deserialize(&serializer, tileBytes);

    if (serializer.deserializationError()!= QAbstractProtobufSerializer::NoError) {
        qFatal("Couldn't deserialize");
    }

    for (auto const& inLayer : tile.layers()) {
        Layer outLayer = {};

        outLayer.name = inLayer.name();

        for (auto const& inFeature : inLayer.features()) {
            if (inFeature.type() != vector_tile::Tile::GeomType::POLYGON) {
                continue;
            }

            Feature outFeature = {};

            outFeature.metaData = populateFeatureMetaData(
                inFeature.tags(),
                inLayer.keys(),
                inLayer.values());

            auto decodedGeometry = ProtobufFeatureToPolygon(inFeature.geometry());

            if (decodedGeometry.first.empty() || decodedGeometry.second.empty()) {
                continue;
            }

            for (auto const& item : decodedGeometry.first) {
                outFeature.vertices.push_back({(float)item.x, (float)item.y});
            }
            outFeature.indices = decodedGeometry.second;

            auto vtxBuffer = rhi->newBuffer(
                QRhiBuffer::Immutable,
                QRhiBuffer::VertexBuffer,
                outFeature.vertices.size() * sizeof(outFeature.vertices[0]));
            if (!vtxBuffer->create()) {
                qFatal("Failed to create tile vertex buffer");
            }
            batch->uploadStaticBuffer(
                vtxBuffer,
                outFeature.vertices.data());
            outFeature.vertexBuffer = vtxBuffer;

            auto idxBuffer = rhi->newBuffer(
                QRhiBuffer::Immutable,
                QRhiBuffer::IndexBuffer,
                outFeature.indices.size() * sizeof(outFeature.indices[0]));
            if (!idxBuffer->create()) {
                qFatal("Failed to create tile vertex buffer");
            }
            batch->uploadStaticBuffer(
                idxBuffer,
                outFeature.indices.data());
            outFeature.indexBuffer = idxBuffer;

            outLayer.features.push_back(std::move(outFeature));
        }

        m_layers.push_back(std::move(outLayer));
    }

    auto stylesheet = StyleSheet::fromJsonFile(":/styleSheet.json").value();
    m_styleSheet = std::move(stylesheet);
}

void MyCustomRenderNode::prepareDrawCommands() {
    for (auto const& abstractLayerStylePtr : m_styleSheet.m_layerStyles) {
        if (abstractLayerStylePtr->type() != StyleSheet::LayerType::fill) {
            continue;
        }
        if (!isLayerShown(*abstractLayerStylePtr, 0)) {
            continue;
        }

        auto const& fillLayerStyle = static_cast<FillLayerStyle const&>(*abstractLayerStylePtr);

        // Find the tile-layer with this name.
        std::optional<int> tileLayerIndex;
        for (int i = 0; i < m_layers.size(); i++) {
            if (abstractLayerStylePtr->m_sourceLayer == m_layers[i].name) {
                tileLayerIndex = i;
                break;
            }
        }
        if (!tileLayerIndex.has_value()) {
            continue;
        }
        auto const& tileLayer = m_layers[tileLayerIndex.value()];

        for (auto const& feature : tileLayer.features) {
            if (!showFeature(fillLayerStyle, feature.metaData, 0, 0)) {
                continue;
            }

            auto color = fillLayerStyle.getFillColor(feature.metaData, 0, 0);

            UniformType test = {};
            test.color[0] = color.redF();
            test.color[1] = color.greenF();
            test.color[2] = color.blueF();
            // TODO! There is a bug here!! Things are not being blended correctly!!
            //test.color[3] = color.alphaF();
            test.color[3] = 1;
            m_uniforms.push_back(test);

            DrawCmd cmd = {};
            cmd.vtxBuffer = feature.vertexBuffer;
            cmd.idxBuffer = feature.indexBuffer;
            cmd.idxCount = feature.indices.size();
            m_drawCmds.push_back(cmd);
        }
    }
}

void QQuickMap::mousePressEvent(QMouseEvent *event)
{
    if (event->buttons() == Qt::MouseButton::LeftButton)
        mouseStartPosition = event->pos();
}

void QQuickMap::mouseReleaseEvent(QMouseEvent *event)
{
    // mouseStartPosition = {-1,-1};
}

void QQuickMap::mouseMoveEvent(QMouseEvent *event)
{
    // Check if the left mouse button is pressed
    if (event->buttons() & Qt::LeftButton) {
        mouseCurrentPosition = event->pos();

        // Calculate the difference between the current and original mouse position.
        QPointF diff = mouseCurrentPosition - mouseStartPosition;

        // Scaling factor used when zooming.
        auto scalar = 1/(std::pow(2, getViewportZoom()));

        // Calculate window aspect ratio, used to scale x coordinate
        // correctly. This was added in after talking to ChatGPT about
        // what could be the cause of the problem.
        double windowAspectRatio = (double)width() / height();

        // Scale the difference variable based on zoom level.
        diff *= scalar;

        // Scale the difference variable based on aspect ratio.
        // This makes the mouse cursor stay hovered over the exact area
        // where it was when the left mouse button was clicked.
        if (width() < height())
            diff.rx() *= windowAspectRatio;
        else if (width() > height())
            diff.ry() /= windowAspectRatio;

        // Translate normalized coordinates to world coordinate space.
        auto world_x = m_viewportX * width();
        auto world_y = m_viewportY * height();

        // Find where to move the position to in the world coordinate space.
        auto new_x = world_x - diff.rx();
        auto new_y = world_y - diff.ry();

        // Normalise the new coordinates so they can be put back in the norm space.
        auto new_x_norm = Bach::normalizeValueToZeroOneRange(new_x, 0, width());
        auto new_y_norm = Bach::normalizeValueToZeroOneRange(new_y, 0, height());

        // Move to the map to the new position.
        setViewportX(new_x_norm);
        setViewportY(new_y_norm);

        // Store the current mouse position before re-rendering.
        mouseStartPosition = mouseCurrentPosition;

        // Call update to render the window.
        update();
    }
}

void QQuickMap::wheelEvent(QWheelEvent *event)
{
    // Calculations are provided by Qt's own source example, here:
    // https://doc.qt.io/qt-6/qwheelevent.html#angleDelta
    QPoint numPixels = event->pixelDelta();
    QPoint numDegrees = event->angleDelta() / 8;

    // Check if degrees or pixels were used to record/measure scrolling.
    // A positive y value means the wheel was moved vertically away from the user.
    // A negative y value means the wheel was moved vertically towards the user.
    if (!numPixels.isNull()) {
        if (numPixels.y() > 0)
            zoomIn();
        else if (numPixels.y() < 0)
            zoomOut();
    } else if (!numDegrees.isNull()) {
        if (numDegrees.y() > 0)
            zoomIn();
        else if (numDegrees.y() < 0)
            zoomOut();
    }
    // accept() is called to indicate that the receiver wants
    // the mouse wheel event. Check the following for more info:
    // https://doc.qt.io/qt-6/qwheelevent.html#QWheelEvent-2
    event->accept();
}
