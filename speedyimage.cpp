#include "speedyimage_p.h"
#include "imageloader.h"
#include <QSGSimpleTextureNode>
#include <QQuickWindow>

static ImageLoader *imgLoader;

SpeedyImage::SpeedyImage(QQuickItem *parent)
    : QQuickItem(parent)
    , d(new SpeedyImagePrivate(this))
{
    setFlag(ItemHasContents);

    if (!imgLoader) {
        imgLoader = new ImageLoader;
    }
}

SpeedyImagePrivate::SpeedyImagePrivate(SpeedyImage *q)
    : q(q)
{
    connect(this, &SpeedyImagePrivate::imageLoaded, this, &SpeedyImagePrivate::setImage, Qt::AutoConnection);
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

    d->clearImage();
    d->reloadImage();
}

QSize SpeedyImage::imageSize() const
{
    return d->imageSize;
}

QSizeF SpeedyImage::paintedSize() const
{
    return d->paintRect.size();
}

void SpeedyImage::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    if (newGeometry.size() == oldGeometry.size())
        return;

    d->calcPaintRect();

    // XXX Exclude error cases
    if (!newGeometry.isEmpty() && !d->source.isEmpty() && d->image.isNull() && d->loadJob.isNull()) {
        // Trigger first load of the image once we have geometry
        d->reloadImage();
    } else if (!d->loadJob.isNull()) {
        // If there is already a load job, unconditionally try to update draw size
        // This is free, and setImage will reload if the draw size did not change in time.
        d->reloadImage();
    } else if ((d->paintRect.width() > d->image.width() && d->imageSize.width() > d->image.width()) ||
               (d->paintRect.height() > d->image.height() && d->imageSize.height() > d->image.height()))
    {
        // Reload the image again if drawSize has changed and needs a larger scale
        d->reloadImage();
    }
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

void SpeedyImagePrivate::clearImage()
{
    loadJob.reset();
    image = QImage();
    imageSize = QSize();
    texture.reset(); // XXX is this safe w/ multithreaded scenegraph?
    paintRect = QRectF();
    q->update();
}

void SpeedyImagePrivate::reloadImage()
{
    // Wait until the item has dimensions before starting to load
    QSize drawSize(q->width(), q->height());
    if (source.isEmpty() || drawSize.isEmpty()) {
        return;
    }

    if (!loadJob.isNull()) {
        // We can attempt to change the drawSize on an existing job, but there
        // is no guarantee it will take effect. That case can be handled with a
        // check in setImage that will fire off a new job at a larger drawSize
        // if the result is insufficient, and we'll still have an upscale to display
        // meanwhile.
        if (drawSize != loadJob.drawSize()) {
            loadJob.setDrawSize(drawSize);
        }
    } else {
        loadJob = imgLoader->enqueue(source, drawSize, 0,
             [&](const ImageLoaderJob &job) {
                emit imageLoaded(job.result(), job.imageSize());
             });
    }
}

void SpeedyImagePrivate::setImage(const QImage &img, const QSize &imgSize)
{
    clearImage();

    image = img;
    imageSize = imgSize;
    if (image.isNull()) {
        qWarning() << "Loading image from" << source << "failed";
    }

    calcPaintRect();
    q->update();

    // Reload the image again if drawSize has changed and needs a larger scale
    if ((paintRect.width() > image.width() && imageSize.width() > image.width()) ||
        (paintRect.height() > image.height() && imageSize.height() > image.height()))
    {
        reloadImage();
    }
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
