#include "inkcanvas.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QPainter>

Q_LOGGING_CATEGORY(lcInk, "rmchat.ink")

namespace {
constexpr qreal kPenWidth = 4.0;
constexpr int kExportMaxDim = 1024;   // longest side sent to the model
constexpr int kExportPad = 30;        // white margin around the ink
}

InkCanvas::InkCanvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setOpaquePainting(true);
    setFillColor(Qt::white);
    // Antialiasing would put grey pixels along every edge, which pushes the
    // panel onto a slower greyscale waveform. Pure black on white lets it use
    // its fast monochrome update, and looks crisper on e-paper anyway.
    setAntialiasing(false);
    setRenderTarget(QQuickPaintedItem::Image);
}

void InkCanvas::ensureLayer()
{
    const QSize want(qMax(1, int(width())), qMax(1, int(height())));
    if (m_layer.size() == want)
        return;

    QImage grown(want, QImage::Format_Grayscale8);
    grown.fill(Qt::white);
    if (!m_layer.isNull()) {
        QPainter p(&grown);
        p.drawImage(0, 0, m_layer);      // keep existing ink across a resize
    }
    m_layer = grown;
}

void InkCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        ensureLayer();
}

void InkCanvas::drawSegment(const QPointF &from, const QPointF &to)
{
    ensureLayer();

    QPainter p(&m_layer);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QPen(Qt::black, kPenWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (from == to)
        p.drawPoint(to);
    else
        p.drawLine(from, to);
    p.end();

    // Repaint only what actually changed. Using update() with no argument
    // damages the whole pad, which the e-paper backend then has to push in
    // full for every single pen sample.
    const int pad = int(kPenWidth) + 2;
    QRect dirty = QRectF(from, to).normalized().toAlignedRect()
                      .adjusted(-pad, -pad, pad, pad);
    dirty &= QRect(0, 0, m_layer.width(), m_layer.height());

    m_inkBounds = m_inkBounds.isNull() ? dirty : m_inkBounds.united(dirty);
    update(dirty);
}

void InkCanvas::penSample(qreal sceneX, qreal sceneY, bool down)
{
    const QPointF local = mapFromScene(QPointF(sceneX, sceneY));
    const bool inside = boundingRect().contains(local);

    if (down && inside) {
        if (!m_drawing) {
            m_drawing = true;
            m_last = local;
            drawSegment(local, local);       // dot for a single tap
        } else {
            drawSegment(m_last, local);
            m_last = local;
        }
        if (!m_hasInk) {
            m_hasInk = true;
            emit inkChanged();
        }
    } else {
        m_drawing = false;                   // lifted, or left the pad
    }
}

void InkCanvas::clear()
{
    if (!m_hasInk)
        return;
    ensureLayer();
    m_layer.fill(Qt::white);
    m_inkBounds = QRect();
    m_drawing = false;
    m_hasInk = false;
    emit inkChanged();
    update();
}

void InkCanvas::paint(QPainter *painter)
{
    if (m_layer.isNull())
        return;
    // Cheap self-instrumentation, off unless you ask for it:
    //   QT_LOGGING_RULES="rmchat.ink.debug=true"
    static int paints = 0;
    static qint64 totalUs = 0, totalPx = 0;
    QElapsedTimer et; et.start();
    // Only the damaged region is redrawn; the clip is already set for us.
    const QRect r = painter->clipBoundingRect().toAlignedRect()
                        .intersected(QRect(0, 0, m_layer.width(), m_layer.height()));
    if (r.isEmpty())
        painter->drawImage(0, 0, m_layer);
    else
        painter->drawImage(r, m_layer, r);

    ++paints;
    totalUs += et.nsecsElapsed() / 1000;
    totalPx += r.isEmpty() ? qint64(m_layer.width()) * m_layer.height()
                           : qint64(r.width()) * r.height();
    if (paints % 25 == 0)
        qCDebug(lcInk, "paints=%d avg=%lldus avg_area=%lldpx",
               paints, totalUs / paints, totalPx / paints);
}

QString InkCanvas::exportPngBase64() const
{
    if (!m_hasInk || m_layer.isNull() || m_inkBounds.isNull())
        return QString();

    QRect box = m_inkBounds.adjusted(-kExportPad, -kExportPad, kExportPad, kExportPad)
                    .intersected(QRect(0, 0, m_layer.width(), m_layer.height()));
    if (box.isEmpty())
        return QString();

    QImage out = m_layer.copy(box);
    const int longest = qMax(out.width(), out.height());
    if (longest > kExportMaxDim)
        out = out.scaled(out.size() * (qreal(kExportMaxDim) / longest),
                         Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    out.save(&buf, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}
