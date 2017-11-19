#include "speedyimage_p.h"
#include "imageloader.h"
#include <QSGSimpleTextureNode>
#include <QQuickWindow>

Q_LOGGING_CATEGORY(lcItem, "speedyimage.item")

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
    , status(SpeedyImage::Null)
    , imageCache(nullptr)
{
    connect(q, &QQuickItem::windowChanged, this, &SpeedyImagePrivate::setWindow);
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

    qCDebug(lcItem) << "set source:" << source;
    d->source = source;

    d->clearImage();
    d->status = Null;

    if (!d->source.isEmpty()) {
        // reloadImage will update status and signal if it has a cache entry immediately
        d->reloadImage();
        // Otherwise, we're still loading (or waiting to be able to load)
        if (d->status == Null) {
            d->status = Loading;
            emit statusChanged();
        }
        // Handle signals where clearImage could make cacheEntryChanged not see any change or wasn't called
        if (d->status == Loading || d->paintRect.isNull())
            emit paintedSizeChanged();
        if (d->status == Loading || !imageSize().isValid())
            emit imageSizeChanged();
    }

    emit sourceChanged();
}

QSize SpeedyImage::imageSize() const
{
    return d->cacheEntry.imageSize();
}

QSizeF SpeedyImage::paintedSize() const
{
    return d->paintRect.size();
}

void SpeedyImage::geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    if (newGeometry.size() == oldGeometry.size())
        return;

    // XXX Consider this as a polish step?
    if (d->calcPaintRect()) {
        emit paintedSizeChanged();
    }

    // XXX Exclude error cases
    if (!newGeometry.isEmpty() && d->status == Loading && d->cacheEntry.isEmpty() && d->loadJob.isNull()) {
        // Trigger first load of the image once we have geometry
        qCDebug(lcItem) << "triggering load with initial item geometry" << newGeometry.size();
        d->reloadImage();
    } else if (!d->loadJob.isNull()) {
        // If there is already a load job, unconditionally try to update draw size
        // This is free, and setImage will reload if the draw size did not change in time.
        d->reloadImage();
    } else if (d->needsReloadForDrawSize()) {
        // Reload the image again if drawSize has changed and needs a larger scale
        d->reloadImage();
    }
}

QSGNode *SpeedyImage::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!d->cacheEntry.texture()) {
        return nullptr;
    }

    QSGSimpleTextureNode *node = static_cast<QSGSimpleTextureNode*>(oldNode);
    if (!node) {
        node = new QSGSimpleTextureNode;
        node->setFiltering(QSGTexture::Linear);
    }
    node->setTexture(d->cacheEntry.texture());
    node->setRect(d->paintRect);

    return node;
}

void SpeedyImagePrivate::setWindow(QQuickWindow *window)
{
    if (!window)
        return;

    Q_ASSERT(!imageCache);
    imageCache = ImageTextureCache::forWindow(window);
    connect(imageCache, &ImageTextureCache::changed, this, &SpeedyImagePrivate::cacheEntryChanged);
    connect(window, &QQuickWindow::sceneGraphInitialized, this, &SpeedyImagePrivate::reloadImage);

    // Trigger reload in case one was blocked by not having imageCache earlier
    // Should not have any side effects otherwise
    reloadImage();
}

void SpeedyImagePrivate::clearImage()
{
    cacheEntry.reset();
    loadJob.reset();
    paintRect = QRectF();
    q->update();
}

bool SpeedyImagePrivate::needsReloadForDrawSize()
{
    if (paintRect.isEmpty()) {
        // There is no paint rect when we don't know image dimensions; in this case, it's always
        // appropriate to try to reload for draw size.
        return true;
    }

    QSize loadedSize = cacheEntry.loadedSize();
    QSize imageSize = cacheEntry.imageSize();
    if (loadedSize.isEmpty()) {
        return true;
    }

    // Reload is necessary if the paint area is larger than the loaded image in either dimension,
    // and the full-scale image is larger than the loaded image in that same dimension.
    if ((paintRect.width() > loadedSize.width() && imageSize.width() > loadedSize.width()) ||
        (paintRect.height() > loadedSize.height() && imageSize.height() > loadedSize.height()))
    {
        return true;
    }

    return false;
}

void SpeedyImagePrivate::reloadImage()
{
    // Wait until the item has dimensions before starting to load
    QSize drawSize(q->width(), q->height());
    if (!imageCache || !q->window()->isSceneGraphInitialized() || source.isEmpty() || drawSize.isEmpty()) {
        qCDebug(lcItem) << "not ready to load yet;" << (bool)imageCache << source << drawSize;
        return;
    }

    if (cacheEntry.isNull()) {
        cacheEntry = imageCache->get(source);

        if (!cacheEntry.isEmpty()) {
            // Call cacheEntryChanged to handle everything
            cacheEntryChanged(source);
        }
    }

    if (!cacheEntry.isEmpty() && !needsReloadForDrawSize()) {
        // Use cache entry
        return;
    }

    if (!loadJob.isNull()) {
        // We can attempt to change the drawSize on an existing job, but there
        // is no guarantee it will take effect. That case can be handled with a
        // check in setImage that will fire off a new job at a larger drawSize
        // if the result is insufficient, and we'll still have an upscale to display
        // meanwhile.
        if (drawSize != loadJob.drawSize()) {
            qCDebug(lcItem) << "attempting to update draw size on load job to" << drawSize;
            loadJob.setDrawSize(drawSize);
        }
    } else {
        auto src = source; // Copy for lambda
        auto cache = imageCache;
        loadJob = imgLoader->enqueue(source, drawSize, 0,
             [src,cache](const ImageLoaderJob &job) {
                // Cache will signal the update to the cache entry
                if (!job.error().isEmpty())
                    cache->insert(src, job.error());
                else
                    cache->insert(src, job.result(), job.imageSize());
             });
    }
}

void SpeedyImagePrivate::cacheEntryChanged(const QString &key)
{
    if (key != source)
        return;

    qCDebug(lcItem) << "cache signal for" << key;
    loadJob.reset();
    q->update();

    auto oldStatus = status;
    if (!cacheEntry.error().isEmpty()) {
        status = SpeedyImage::Error;
    } else {
        status = SpeedyImage::Ready;
        Q_ASSERT(cacheEntry.texture());
    }

    if (calcPaintRect())
        emit q->paintedSizeChanged();
    if (status != oldStatus)
        emit q->statusChanged();
    // Can't really tell if image size changed, but assume it won't between reloads
    if (oldStatus != SpeedyImage::Ready)
        emit q->imageSizeChanged();

    // Reload the image again if drawSize has changed and needs a larger scale
    if (needsReloadForDrawSize())
    {
        qCDebug(lcItem) << "triggering immediate reload for a larger drawSize";
        reloadImage();
    }
}

bool SpeedyImagePrivate::calcPaintRect()
{
    QRectF box(0, 0, q->width(), q->height());
    QRectF img(QPointF(0, 0), cacheEntry.loadedSize());
    QRectF paint;

    if (!img.isEmpty() && !box.isEmpty()) {
        if (img.width() / img.height() > box.width() / box.height()) {
            paint = QRectF(0, 0, box.width(), img.height() * (box.width() / img.width()));
        } else {
            paint = QRectF(0, 0, img.width() * (box.height() / img.height()), box.height());
        }
        paint.translate((box.width() - paint.width()) / 2, (box.height() - paint.height()) / 2);
    }

    if (paint == paintRect)
        return false;

    paintRect = paint;
    q->update();
    return true;
}
