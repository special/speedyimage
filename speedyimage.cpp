#include "speedyimage_p.h"
#include <QSGSimpleTextureNode>
#include <QQuickWindow>

SpeedyImage::SpeedyImage(QQuickItem *parent)
    : QQuickItem(parent)
    , d(new SpeedyImagePrivate(this))
{
    setFlag(ItemHasContents);
}

SpeedyImagePrivate::SpeedyImagePrivate(SpeedyImage *q)
    : q(q)
    , autoTransform(true)
{
}

SpeedyImage::~SpeedyImage()
{
}

QString SpeedyImage::source() const
{
    return d->source;
}

void SpeedyImage::setSource(const QString &source)
{
    if (d->source == source)
        return;

    d->source = source;
    emit sourceChanged();

    d->reloadImage();
}

bool SpeedyImage::autoTransform() const
{
    return d->autoTransform;
}

void SpeedyImage::setAutoTransform(bool enable)
{
    if (d->autoTransform == enable)
        return;

    d->autoTransform = enable;
    emit autoTransformChanged();

    d->reloadImage();
}

QSize SpeedyImage::imageSize() const
{
    return d->image.size();
}

QSizeF SpeedyImage::paintedSize() const
{
    return d->paintRect.size();
}

void SpeedyImage::geometryChanged(const QRectF &, const QRectF &)
{
    d->calcPaintRect();
}

QSGNode *SpeedyImage::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!d->texture) {
        if (d->image.isNull()) {
            return nullptr;
        }

        d->texture.reset(window()->createTextureFromImage(d->image, {QQuickWindow::TextureCanUseAtlas, QQuickWindow::TextureIsOpaque}));
        Q_ASSERT(d->texture);
    }

    QSGSimpleTextureNode *node = static_cast<QSGSimpleTextureNode*>(oldNode);
    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setFiltering(QSGTexture::Linear);
    }
    node->setTexture(d->texture.get());
    node->setRect(d->paintRect);

    return node;
}

void SpeedyImagePrivate::reloadImage()
{
    image = QImage();
    texture.reset(); // XXX is this safe w/ multithreaded scenegraph?

    image = QImage(source);
    if (image.isNull()) {
        qWarning() << "Loading image from" << source << "failed";
    }

    calcPaintRect();
    q->update();
}

void SpeedyImagePrivate::calcPaintRect()
{
    QRectF box(0, 0, q->width(), q->height());
    QRectF img(0, 0, image.width(), image.height());
    QRectF paint;

    if (!img.isEmpty() && !box.isEmpty()) {
        if (img.width() / img.height() > box.width() / box.height()) {
            paint = QRectF(0, 0, box.width(), img.height() * (box.width() / img.width()));
        } else {
            paint = QRectF(0, 0, img.width() * (box.height() / img.height()), box.height());
        }
        paint.translate((box.width() - paint.width()) / 2, (box.height() - paint.height()) / 2);
    }

    if (paint != paintRect) {
        paintRect = paint;
        q->update();
    }
}
