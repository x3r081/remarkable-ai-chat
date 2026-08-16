#include "inkcanvas.h"

#include <QBuffer>
#include <QImage>
#include <QPainter>

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

    m_repaint.setSingleShot(true);
    m_repaint.setInterval(40);        // ~25 fps cap; kind to the e-paper
    connect(&m_repaint, &QTimer::timeout, this, [this] { update(); });
}

void InkCanvas::penSample(qreal sceneX, qreal sceneY, bool down)
{
    const QPointF local = mapFromScene(QPointF(sceneX, sceneY));
    const bool inside = boundingRect().contains(local);

    if (down && inside) {
        const bool hadInk = hasInk();
        m_current.append(local);
        if (!hadInk)
            emit inkChanged();
        scheduleRepaint();
    } else if (m_wasDown && m_current.size() > 1) {
        // lifted, or wandered off the canvas: finish the stroke
        m_strokes.append(m_current);
        m_current.clear();
        scheduleRepaint();
    } else {
        m_current.clear();
    }
    m_wasDown = down && inside;
}

void InkCanvas::clear()
{
    if (!hasInk())
        return;
    m_strokes.clear();
    m_current.clear();
    m_wasDown = false;
    emit inkChanged();
    update();
}

void InkCanvas::scheduleRepaint()
{
    if (!m_repaint.isActive())
        m_repaint.start();
}

static void drawStrokes(QPainter *p, const QVector<QPolygonF> &strokes,
                        const QPolygonF &current, qreal width)
{
    QPen pen(Qt::black, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p->setPen(pen);
    p->setRenderHint(QPainter::Antialiasing, true);
    for (const auto &s : strokes) {
        if (s.size() == 1)
            p->drawPoint(s.first());
        else
            p->drawPolyline(s);
    }
    if (current.size() > 1)
        p->drawPolyline(current);
}

void InkCanvas::paint(QPainter *painter)
{
    drawStrokes(painter, m_strokes, m_current, kPenWidth);
}

QString InkCanvas::exportPngBase64() const
{
    QVector<QPolygonF> all = m_strokes;
    if (m_current.size() > 1)
        all.append(m_current);
    if (all.isEmpty())
        return QString();

    QRectF box;
    for (const auto &s : all)
        box = box.united(s.boundingRect());
    box.adjust(-kExportPad, -kExportPad, kExportPad, kExportPad);

    qreal scale = 1.0;
    const qreal longest = qMax(box.width(), box.height());
    if (longest > kExportMaxDim)
        scale = kExportMaxDim / longest;

    QImage img(qMax(1, int(box.width() * scale)),
               qMax(1, int(box.height() * scale)),
               QImage::Format_Grayscale8);
    img.fill(Qt::white);

    QPainter p(&img);
    p.scale(scale, scale);
    p.translate(-box.topLeft());
    drawStrokes(&p, m_strokes, m_current, kPenWidth / scale);
    p.end();

    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return QString::fromLatin1(bytes.toBase64());
}
