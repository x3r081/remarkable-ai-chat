#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QRect>

/*!
 * The handwriting area.
 *
 * Ink is accumulated into a persistent image rather than being re-stroked
 * every frame, and each new segment repaints only its own bounding rectangle.
 * Both matter a great deal here: repainting the whole pad made the e-paper
 * backend push the entire region on every pen sample, which is what made
 * writing feel laggy compared with the stock UI.
 */
class InkCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool hasInk READ hasInk NOTIFY inkChanged)

public:
    explicit InkCanvas(QQuickItem *parent = nullptr);

    void paint(QPainter *painter) override;

    bool hasInk() const { return m_hasInk; }

    Q_INVOKABLE void penSample(qreal sceneX, qreal sceneY, bool down);
    Q_INVOKABLE void clear();

    /*! Ink as base64 PNG (white background, black ink, cropped + scaled).
     *  Empty string if there is nothing to export. */
    Q_INVOKABLE QString exportPngBase64() const;

signals:
    void inkChanged();

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    void ensureLayer();
    void drawSegment(const QPointF &from, const QPointF &to);

    QImage m_layer;          /* persistent ink, item-sized, white background */
    QPointF m_last;          /* previous point of the stroke in progress */
    bool m_drawing = false;
    bool m_hasInk = false;
    QRect m_inkBounds;       /* bounding box of everything drawn, for export */
};
