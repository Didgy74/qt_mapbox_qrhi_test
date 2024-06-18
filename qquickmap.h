#ifndef QQUICKMAP_H
#define QQUICKMAP_H

#include <QQuickItem>

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
public:
    explicit QQuickMap(QQuickItem* parent = nullptr);

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
    double m_viewportZoom;
    // Center of viewport X
    // Range [0, 1].
    double m_viewportX = 0.5;
    // Center of viewport Y
    // Range [0, 1].
    double m_viewportY = 0.5;

signals:
    void viewportZoomChanged();
    void viewportXChanged();
    void viewportYChanged();
};



#endif // QQUICKMAP_H
