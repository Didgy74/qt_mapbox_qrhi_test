#ifndef QQUICKMAP_H
#define QQUICKMAP_H

#include <QQuickItem>

#include "tileloader.h"

inline double normalizeValueToZeroOneRange(double value, double min, double max)
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


inline std::pair<double, double> lonLatToWorldNormCoord(double lon, double lat)
{
    constexpr double webMercatorPhiCutoff = 1.4844222297;

    // Convert longitude and latitude to radians
    auto lambda = lon;
    auto phi = lat;

    // Convert to Web Mercator
    auto x = lambda;
    auto y = std::log(std::tan(M_PI / 4.0 + phi / 2.0));

    // Normalize x and y to [0, 1]
    // Assuming the Web Mercator x range is [-π, π] and y range is calculated from latitude range
    auto xNormalized = normalizeValueToZeroOneRange(x, -M_PI, M_PI);
    // We have to flip the sign of Y, because Mercator has positive Y moving up,
    // while the world-normalized coordinate space has Y moving down.
    auto yNormalized = normalizeValueToZeroOneRange(
        -y,
        std::log(std::tan(M_PI / 4.0 + -webMercatorPhiCutoff / 2.0)),
        std::log(std::tan(M_PI / 4.0 + webMercatorPhiCutoff / 2.0)));


    return { xNormalized, yNormalized };
}

inline std::pair<double, double> lonLatToWorldNormCoordDegrees(double lon, double lat)
{
    auto degToRad = [](double deg) {
        return deg * M_PI / 180.0;
    };
    return lonLatToWorldNormCoord(degToRad(lon), degToRad(lat));
}


class QQuickMap : public QQuickItem
{
    Q_OBJECT
    QML_NAMED_ELEMENT(QQuickMap)
    Q_PROPERTY(
        double viewportZoom
        READ getViewportZoom
        WRITE setViewportZoom
        NOTIFY viewportZoomChanged
        STORED true)

    Q_PROPERTY(
        double viewportX
        READ getViewportX
        WRITE setViewportX
        NOTIFY viewportXChanged
        STORED true)

    Q_PROPERTY(
        double viewportY
        READ getViewportY
        WRITE setViewportY
        NOTIFY viewportYChanged
        STORED true)

    Q_PROPERTY(
        double viewportRotation
        READ getViewportRotation
        WRITE setViewportRotation
        NOTIFY viewportRotationChanged
        STORED true)

    Q_PROPERTY(
        TileLoader* tileLoader
        READ getTileLoader
        WRITE setTileLoader
        NOTIFY tileLoaderChanged
        STORED true)

public:
    explicit QQuickMap(QQuickItem* parent = nullptr);

    Q_INVOKABLE void setViewportCoordDegrees(double lon, double lat) {
        auto [x, y] = lonLatToWorldNormCoordDegrees(lon, lat);
        setViewportX(x);
        setViewportY(y);
    }

    double getViewportZoom() const { return m_viewportZoom; }
    void setViewportZoom(double newValue) {
        bool changed = newValue != m_viewportZoom;
        m_viewportZoom = newValue;
        if (changed) {
            update();
            emit viewportZoomChanged();
        }
    }

    void zoomIn() { setViewportZoom(getViewportZoom() + 0.1); }
    void zoomOut() { setViewportZoom(getViewportZoom() - 0.1); }

    double getViewportX() const { return m_viewportX; }
    void setViewportX(double newValue) {
        bool changed = newValue != m_viewportX;
        m_viewportX = newValue;
        if (changed) {
            update();
            emit viewportXChanged();
        }
    }

    double getViewportY() const { return m_viewportY; }
    void setViewportY(double newValue) {
        bool changed = newValue != m_viewportY;
        m_viewportY = newValue;
        if (changed) {
            update();
            emit viewportYChanged();
        }
    }

    double getViewportRotation() const { return m_viewportRotation; }
    void setViewportRotation(double newValue) {
        bool changed = newValue != m_viewportRotation;
        m_viewportRotation = newValue;
        if (changed) {
            update();
            emit viewportRotationChanged();
        }
    }

    TileLoader* getTileLoader() const { return m_tileLoader; }
    void setTileLoader(TileLoader* newLoader) {
        bool changed = newLoader != m_tileLoader;
        m_tileLoader = newLoader;

        QObject::connect(
            m_tileLoader,
            &TileLoader::tileLoaded,
            this,
            [=](bool success, TileCoord tile) {
                this->update();
            });
        if (changed) {
            update();
            emit tileLoaderChanged();
        }
    }


protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

    QPoint mouseStartPosition = {-1, -1};
    QPoint mouseCurrentPosition = {-1, -1};

    // Maybe not necessary anymore, idk.
    class RhiStuffPimplT {
    public:
        virtual ~RhiStuffPimplT() = 0;
    };
    class RhiStuffImpl;

    std::unique_ptr<RhiStuffPimplT> rhiStuff;
    RhiStuffImpl* getRhiStuff();

private:
    double m_viewportZoom = 0;
    // Center of viewport X
    // Range [0, 1].
    double m_viewportX = 0.5;
    // Center of viewport Y
    // Range [0, 1].
    double m_viewportY = 0.5;

    // Rotation of the viewport
    // Range [0, 360]
    double m_viewportRotation = 0;

    // The RenderNode needs to store a TileLoader request result
    // during the 'prepare' stage AND the 'render' stage. And then
    // destroy that result when the QtQuick Scene Graph command buffer
    // has been submitted. Likely we need to use
    // some event that happens after rendering...
    //
    //
    // This is to signal
    // to the TileLoader that the tiles are now ready to be cleaned up
    // by the tile eviction policy (Not yet implemented.
    TileLoader* m_tileLoader = nullptr;

signals:
    void viewportZoomChanged();
    void viewportXChanged();
    void viewportYChanged();
    void viewportRotationChanged();
    void tileLoaderChanged();
};



#endif // QQUICKMAP_H
