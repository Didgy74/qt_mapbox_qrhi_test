#include "qquickmap.h"

#include <QFile>
#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGRenderNode>
#include <rhi/qrhi.h>
#include <QPainterPath>
#include <QScopeGuard>
#include <QSpan>

#include <glm/glm.hpp>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp> // glm::translate, glm::rotate, glm::scale

#include <LayerStyle.h>
#include "Evaluator.h"

#include <memory>
#include <optional>

#include <QSGTextNode>

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

static int calcMapZoomLevelForTileSizePixels(
    int vpWidth,
    int vpHeight,
    double vpZoom,
    int desiredTileWidth)
{
    // Calculate current tile size based on the largest dimension and current scale
    int currentTileSize = qMax(vpWidth, vpHeight);

    // Calculate desired scale factor
    double desiredScale = (double)desiredTileWidth / currentTileSize;

    // Figure out how the difference between the zoom levels of viewport and map
    // needed to satisfy the pixel-size requirement.
    double newMapZoomLevel = vpZoom - log2(desiredScale);

    // Round to int, and clamp output to zoom level range.
    return (int)round(newMapZoomLevel);
}

std::pair<double, double> calcViewportSizeNorm(double vpZoomLevel, double viewportAspect) {
    auto temp = 1 / pow(2, vpZoomLevel);
    return {
        temp * qMin(1.0, viewportAspect),
        temp * qMin(1.0, 1 / viewportAspect)
    };
}

std::vector<TileCoord> calcVisibleTiles(
    double vpX,
    double vpY,
    double vpAspect,
    double vpZoomLevel,
    int mapZoomLevel)
{
    mapZoomLevel = qMax(0, mapZoomLevel);

    // We need to calculate the width and height of the viewport in terms of
    // world-normalized coordinates.
    auto [vpWidthNorm, vpHeightNorm] = calcViewportSizeNorm(vpZoomLevel, vpAspect);

    // Figure out the 4 edges in world-normalized coordinate space.
    auto vpMinNormX = vpX - (vpWidthNorm / 2.0);
    auto vpMaxNormX = vpX + (vpWidthNorm / 2.0);
    auto vpMinNormY = vpY - (vpHeightNorm / 2.0);
    auto vpMaxNormY = vpY + (vpHeightNorm / 2.0);

    // Amount of tiles in each direction for this map zoom level.
    auto tileCount = 1 << mapZoomLevel;

    auto clampToGrid = [&](int i) {
        return std::clamp(i, 0, tileCount-1);
    };

    // Convert edges into the index-based grid coordinates, and apply a clamp operation
    // in case the viewport goes outside the map.
    auto leftTileX = clampToGrid((int)floor(vpMinNormX * tileCount));
    auto rightTileX = clampToGrid((int)floor(vpMaxNormX * tileCount));
    auto topTileY = clampToGrid((int)floor(vpMinNormY * tileCount));
    auto botTileY = clampToGrid((int)floor(vpMaxNormY * tileCount));

    // Iterate over our two ranges to build our list.

    if (mapZoomLevel == 0 &&
        rightTileX - leftTileX == 0 &&
        botTileY - topTileY == 0)
    {
        return { { 0, 0, 0 } };
    } else {
        std::vector<TileCoord> visibleTiles;
        for (int y = topTileY; y <= botTileY; y++) {
            for (int x = leftTileX; x <= rightTileX; x++) {
                visibleTiles.push_back({ mapZoomLevel, x, y });
            }
        }
        return visibleTiles;
    }
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
    Evaluator::FeatureGeometryType featureGeomType,
    std::map<QString, QVariant> const& featureMetaData,
    int mapZoom,
    double vpZoom)
{
    if (layerStyle.m_filter.isEmpty())
        return true;
    return Evaluator::resolveExpression(
       layerStyle.m_filter,
       featureGeomType,
       featureMetaData,
       mapZoom,
       vpZoom).toBool();
}

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
        float matrix[16] = {};
        float color[4] = { 0, 0, 0, 1.0 };

        static constexpr int internalSize = 64 + 16;
        // Dynamic uniform buffers need stride to be multiple of
        // 256 bytes for now...
        char _padding2[256 - 80] = {};
    };
    static_assert(sizeof(UniformType) == 256);
    static_assert(offsetof(UniformType, color) == 64);

    std::vector<UniformType> m_uniforms = {};
    class DrawCmd {
    public:
        QRhiBuffer* vtxBuffer = {};
        // The amount to offset into the vtx buffer in bytes.
        qint64 vtxByteOffset = 0;

        QRhiBuffer* idxBuffer = {};
        // The amount to offset into the idx buffer in bytes.
        qint64 idxByteOffset = 0;
        // The amount of indices to draw.
        qint64 idxCount = 0;
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

    StyleSheet m_styleSheet;
    bool loadedStyleSheet = false;

    std::unique_ptr<TileLoaderUploadResult> tileLoaderUploadResult;
    std::unique_ptr<TileLoaderRequestResult> tileLoaderRequestResult;

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

    void loadStyleSheet();

    void prepareDrawCommands(
        TileLoader* tileLoader,
        glm::mat4 const& clipSpaceCorrection);

    virtual void prepare() override {
        QSGRenderNode::prepare();

		auto* rhi = window->rhi();

        auto* batch = rhi->nextResourceUpdateBatch();

        // If the TileLoader has any pending GPU uploads, do it here...
        // TODO: This is another race condition pretty sure...
        auto tileLoader = sourceWidget->getTileLoader();
        // This function call is thread-safe.
        auto* pendingTileUploads = tileLoader->uploadPendingTilesToRhi(rhi, batch);
        tileLoaderUploadResult.reset(pendingTileUploads);

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

        if (!loadedStyleSheet)
        {
            loadStyleSheet();
            loadedStyleSheet = true;
        }

        auto tempMat = rhi->clipSpaceCorrMatrix();
        glm::mat4 clipSpaceCorrMatrix = {};
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                clipSpaceCorrMatrix[i][j] = tempMat(i, j);
            }
        }
        // Needs to happen every frame. We prepare whatever draw calls we need.
        prepareDrawCommands(tileLoader, clipSpaceCorrMatrix);



        if (m_uniformBuffer == nullptr) {
            m_uniformBuffer = rhi->newBuffer(
                QRhiBuffer::Dynamic, // Must always be dynamic when uniform buffer.
                QRhiBuffer::UniformBuffer,
                m_uniforms.size() * 256);
            if (!m_uniformBuffer->create()) {
                qFatal() << "Failed to create uniform buffer";
            }

            if (m_resourceBindings == nullptr) {
                m_resourceBindings = rhi->newShaderResourceBindings();

                m_resourceBindings->setBindings({
                    QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                        0,
                        QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                        m_uniformBuffer,
                        UniformType::internalSize)
                });

                m_resourceBindings->create();
            }
        } else {
            // We have a uniform buffer, we need to check if it can fit the draw commands.
            if (m_uniformBuffer->size() < m_uniforms.size() * 256) {
                // TODO: There is a memory leak here.
                m_uniformBuffer = rhi->newBuffer(
                    QRhiBuffer::Dynamic, // Must always be dynamic when uniform buffer.
                    QRhiBuffer::UniformBuffer,
                    m_uniforms.size() * 256);
                if (!m_uniformBuffer->create()) {
                    qFatal() << "Failed to create uniform buffer";
                }

                // TODO: There is a memory leak here.
                m_resourceBindings = rhi->newShaderResourceBindings();
                m_resourceBindings->setBindings({
                    QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                        0,
                        QRhiShaderResourceBinding::VertexStage | QRhiShaderResourceBinding::FragmentStage,
                        m_uniformBuffer,
                        UniformType::internalSize)
                });

                m_resourceBindings->create();
            }
        }
        batch->updateDynamicBuffer(
            m_uniformBuffer,
            0,
            m_uniforms.size() * 256,
            m_uniforms.data());

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

    virtual void render(const QSGRenderNode::RenderState *state) override;
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
        node->setFlag(QSGNode::OwnedByParent);

        auto* textNode = window()->createTextNode();
        textNode->setFlag(QSGNode::OwnedByParent);
        node->appendChildNode(textNode);
        QTextLayout textLayout;
        QFont font("Arial", 24);
        textLayout.setFont(font);
        textLayout.setText("Hello, testing something out whatever");
        //textNode->setViewport(boundingRect());
        textNode->setColor(Qt::red);
        textNode->addTextLayout({10, 10}, &textLayout);
    }





    return node;
}

void MyCustomRenderNode::render(const QSGRenderNode::RenderState *state)
{
    // We can destroy the uploaded tiles
    tileLoaderUploadResult.reset();

    auto* cb = commandBuffer();

    // The QQuickItem canvas has Y pointing down.
    // The QRhi canvas is Y pointing up.
    // We need to fix.

    auto pixelRatio = renderTarget()->devicePixelRatio();
    auto renderTargetSize = renderTarget()->pixelSize().toSizeF();

    // Pretty sure accessing the widget is a race condition.
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

    // For some reason, Vulkan requires
    // that the set pipeline uses scissor
    // in order for us to set it.
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

        QRhiCommandBuffer::VertexInput vertexInputs[] = { {
            drawCmd.vtxBuffer,
            drawCmd.vtxByteOffset } };

        cb->setVertexInput(
            0,
            1,
            vertexInputs,
            drawCmd.idxBuffer,
            drawCmd.idxByteOffset,
            QRhiCommandBuffer::IndexUInt32);

        cb->drawIndexed(drawCmd.idxCount, 1, 0, 0, 0);
    }
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
    blend.opColor = QRhiGraphicsPipeline::Add;
    blend.opAlpha = QRhiGraphicsPipeline::Add;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::One;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;

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

void MyCustomRenderNode::loadStyleSheet()
{
    auto stylesheet = StyleSheet::fromJsonFile(":/styleSheet-basic.json").value();
    m_styleSheet = std::move(stylesheet);
}

void MyCustomRenderNode::prepareDrawCommands(
    TileLoader* tileLoader,
    glm::mat4 const& clipSpaceCorrection)
{
    m_uniforms.clear();
    m_drawCmds.clear();

    // TODO: Pretty sure this is a race condition.
    double vpZoom = sourceWidget->getViewportZoom();
    double width = sourceWidget->width();
    double height = sourceWidget->height();
    double aspect = width / height;
    double vpX = sourceWidget->getViewportX();
    double vpY = sourceWidget->getViewportY();
    float vpRotation = (float)sourceWidget->getViewportRotation();

    // This needs to be clamped based on the StyleSheeet min-max zoom?
    auto mapZoom = (int)std::round(vpZoom);
    mapZoom = std::clamp(mapZoom, 0, 15);

    if (tileLoader == nullptr) {
        return;
    }

    auto visibleCoords = calcVisibleTiles(
        vpX,
        vpY,
        aspect,
        vpZoom,
        mapZoom);

    // This is a memory leak.
    auto* tileRequestResult = tileLoader->requestTiles(visibleCoords);
    tileLoaderRequestResult.reset(tileRequestResult);

    for (auto const tileCoord : visibleCoords) {
        // Check if this tile-coord is loaded.
        auto tileIt = tileRequestResult->tiles.find(tileCoord);
        if (tileIt == tileRequestResult->tiles.end()) {
            continue;
        }
        auto const& tile = *tileIt->second;

        for (auto const& abstractLayerStylePtr : m_styleSheet.m_layerStyles) {
            if (abstractLayerStylePtr->type() != StyleSheet::LayerType::fill) {
                continue;
            }
            if (!isLayerShown(*abstractLayerStylePtr, mapZoom)) {
                continue;
            }

            auto const& fillLayerStyle = static_cast<FillLayerStyle const&>(*abstractLayerStylePtr);

            // Find source layer in tile.
            std::optional<int> tileLayerIndex;
            for (int i = 0; i < tile.layers.size(); i++) {
                if (abstractLayerStylePtr->m_sourceLayer == tile.layers[i].name) {
                    tileLayerIndex = i;
                    break;
                }
            }
            if (!tileLayerIndex.has_value()) {
                continue;
            }
            auto const& tileLayer = tile.layers[tileLayerIndex.value()];

            for (auto const& feature : tileLayer.features) {
                bool shouldShowFeature = showFeature(
                    fillLayerStyle,
                    Evaluator::FeatureGeometryType::Polygon,
                    feature.metaData,
                    mapZoom,
                    vpZoom);
                if (!shouldShowFeature) {
                    continue;
                }

                auto color = fillLayerStyle.getFillColor(
                    Evaluator::FeatureGeometryType::Polygon,
                    feature.metaData,
                    mapZoom,
                    vpZoom);
                auto translate = fillLayerStyle.getTranslation(
                    feature.metaData,
                    mapZoom,
                    vpZoom);

                UniformType test = {};
                test.color[0] = color.redF();
                //test.color[0] = 0;
                test.color[1] = color.greenF();
                //test.color[0] = 1;
                test.color[2] = color.blueF();
                //test.color[2] = 0;
                // TODO! There is a bug here!! Things are not being blended correctly!!
                test.color[3] = color.alphaF();

                {
                    auto quadScale = 1 / std::powf(2, mapZoom);

                    auto mat = glm::mat4{ 1.f };

                    // First we emplace the tile inside the quad that holds the worldmap
                    mat = glm::scale(glm::mat4{1.f}, { quadScale, quadScale, 1 }) * mat;
                    // Move origin to top left
                    mat = glm::translate(glm::mat4{ 1.f }, glm::vec3{
                        -(std::pow(2, mapZoom)-1) / 2,
                        (std::pow(2, mapZoom)-1) / 2,
                        0 } * quadScale) *
                        mat;

                    // Offset into the correct grid-cell for this tile.
                    mat = glm::translate(glm::mat4{1.f},
                        glm::vec3{ tileCoord.x, -tileCoord.y, 0 } * quadScale) *
                        mat;

                    // Position the world map relative to the viewport
                    mat = glm::translate(glm::mat4{1.f}, { 0.5, -0.5, 0}) * mat;
                    mat = glm::translate(glm::mat4{1.f}, {
                        -vpX,
                        vpY,
                        0 }) *
                        mat;

                    mat = glm::rotate(glm::mat4{1.f},
                        glm::radians(vpRotation),
                        {0, 0, 1}) *
                        mat;

                    // Scale the quad according to viewport.
                    mat = glm::scale(glm::mat4{1.f}, {
                        std::pow(2, vpZoom),
                        std::pow(2, vpZoom),
                        1}) *
                        mat;

                    // So far, a tile has had the length of 1 and normalized coordinates of
                    // [-0.5, 0.5]. NDC is range [-1, 1]. Adjust it to fill the range.
                    mat = glm::scale(glm::mat4{1.f}, {2, 2, 1}) * mat;

                    // Adjust for viewport aspect
                    if (aspect < 1) {
                        mat = glm::scale(glm::mat4{1.f}, { 1 / aspect, 1, 1}) * mat;
                    } else {
                        mat = glm::scale(glm::mat4{1.f}, { 1, aspect, 1}) * mat;
                    }

                    mat = clipSpaceCorrection * mat;

                    memcpy(&test.matrix, &mat, sizeof(mat));
                }

                m_uniforms.push_back(test);

                DrawCmd cmd = {};
                cmd.vtxBuffer = tile.vertexBuffer.get();
                cmd.vtxByteOffset = feature.vtxByteOffset;
                cmd.idxBuffer = tile.indexBuffer.get();
                cmd.idxByteOffset = feature.idxByteOffset;
                cmd.idxCount = feature.idxCount;
                m_drawCmds.push_back(cmd);
            }
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

        // Rotate the new coordinates so that they're moving in our up-direction.
        float angleRad = -glm::radians((float)getViewportRotation()); // Convert angle to radians if it's in degrees
        glm::mat2 rotationMatrix = glm::mat2(
            glm::vec2(cos(angleRad), -sin(angleRad)),
            glm::vec2(sin(angleRad), cos(angleRad))
            );
        auto diffVec = rotationMatrix * glm::vec2{ diff.x(), diff.y() };
        diff.setX(diffVec.x);
        diff.setY(diffVec.y);



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
