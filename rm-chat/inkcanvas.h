#pragma once

#include <QPolygonF>
#include <QQuickPaintedItem>
#include <QTimer>
#include <QVector>

/*!
 * The handwriting area. Receives pen samples in scene coordinates, keeps the
 * strokes, paints them, and can export the ink as a base64 PNG sized for a
 * vision model.
 */
class InkCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool hasInk READ hasInk NOTIFY inkChanged)

public:
    explicit InkCanvas(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    bool hasInk() const { return !m_strokes.isEmpty() || m_current.size() > 1; }

    Q_INVOKABLE void penSample(qreal sceneX, qreal sceneY, bool down);
    Q_INVOKABLE void clear();

    /*! Ink as base64 PNG (white background, black ink, cropped + scaled).
     *  Empty string if there is nothing to export. */
    Q_INVOKABLE QString exportPngBase64() const;

signals:
    void inkChanged();

private:
    void scheduleRepaint();

    QVector<QPolygonF> m_strokes;   // finished strokes, item-local coords
    QPolygonF m_current;            // stroke being written
    bool m_wasDown = false;
    QTimer m_repaint;               // e-paper friendly update throttle
};
