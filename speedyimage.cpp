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
    , explicitLoadingSize(false)
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
    } else {
        emit statusChanged();
        emit paintedSizeChanged();
        emit imageSizeChanged();
    }

    emit sourceChanged();
}

QSize SpeedyImage::loadingSize() const
{
    return d->loadingSize;
}

void SpeedyImage::setLoadingSize(QSize size) {
    d->explicitLoadingSize = size.isValid();
    if (!size.isValid())
        size = QSize(width(), height());
    d->applyLoadingSize(size);
}

SpeedyImage::Status SpeedyImage::status() const
{
    return d->status;
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

    if (!d->explicitLoadingSize) {
        d->applyLoadingSize(newGeometry.size().toSize());
    }
}

QSGNode *SpeedyImage::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (!d->cacheEntry.texture()) {
        delete oldNode;
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

void SpeedyImagePrivate::applyLoadingSize(const QSize &size)
{
    if (loadingSize == size)
        return;

    loadingSize = size;
    emit q->loadingSizeChanged();

    // XXX Exclude error cases
    if (size.isValid() && status == SpeedyImage::Loading && cacheEntry.isEmpty() && loadJob.isNull()) {
        // Trigger first load of the image once we have geometry
        qCDebug(lcItem) << "triggering load with initial loading size" << size;
        reloadImage();
    } else if (!loadJob.isNull()) {
        // If there is already a load job, unconditionally try to update draw size
        // This is free, and setImage will reload if the draw size did not change in time.
        reloadImage();
    } else if (needsReloadForDrawSize()) {
        // Reload the image again if drawSize has changed and needs a larger scale
        reloadImage();
    }
}

// Returns true if the the image needs to be reloaded based on the current
// loadingSize.
bool SpeedyImagePrivate::needsReloadForDrawSize()
{
    if (status == SpeedyImage::Error || status == SpeedyImage::Null) {
        return false;
    } else if (!loadingSize.isValid()) {
        // If loadingSize is invalid, do nothing. Does not include empty loadingSize.
        return false;
    }

    QSizeF loadedSize = cacheEntry.loadedSize();
    QSizeF imageSize = cacheEntry.imageSize();
    if (imageSize.isEmpty() || loadedSize.isEmpty()) {
        // If nothing is loaded yet, always reload for draw size; see reloadImage.
        return true;
    }

    // Scale imageSize within loadingSize and reload if either dimension exceeds loadedSize
    QSizeF fitSize;
    if (!loadingSize.isEmpty()) {
        if (imageSize.width() / imageSize.height() > loadingSize.width() / loadingSize.height()) {
            fitSize = QSizeF(loadingSize.width(), imageSize.height() * (loadingSize.width() / loadingSize.width()));
        } else {
            fitSize = QSizeF(imageSize.width() * (loadingSize.height() / imageSize.height()), loadingSize.height());
        }
    } else if (loadingSize.width() == 0 && loadingSize.height() == 0) {
        // If loadingSize is exactly zero (for full size), reload only if the full size image
        // isn't loaded yet
        return imageSize != loadedSize;
    } else if (loadingSize.width() > 0) {
        // Calculate height by width
        double f = double(loadingSize.width()) / double(imageSize.width());
        fitSize = QSizeF(loadingSize.width(), qRound(imageSize.height() * f));
    } else {
        // Calculate width by height
        double f = double(loadingSize.height()) / double(imageSize.height());
        fitSize = QSizeF(qRound(imageSize.width() * f), loadingSize.height());
    }

    if ((fitSize.width() > loadedSize.width() && imageSize.width() > loadedSize.width()) ||
        (fitSize.height() > loadedSize.height() && imageSize.height() > loadedSize.height()))
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

    if ((!cacheEntry.isEmpty() && !needsReloadForDrawSize()) || !cacheEntry.error().isEmpty()) {
        // Use cache entry
        return;
    }

    if (!loadJob.isNull()) {
        // We can attempt to change the drawSize on an existing job, but there
        // is no guarantee it will take effect. That case can be handled with a
        // check in setImage that will fire off a new job at a larger drawSize
        // if the result is insufficient, and we'll still have an upscale to display
        // meanwhile.
        if (loadingSize != loadJob.drawSize()) {
            qCDebug(lcItem) << "attempting to update draw size on load job to" << loadingSize;
            loadJob.setDrawSize(loadingSize);
        }
    } else {
        auto src = source; // Copy for lambda
        auto cache = imageCache;
        loadJob = imgLoader->enqueue(source, loadingSize, 0,
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

    // XXX This code is duplicated a couple times now...
    if (img.isEmpty()) {
        // Empty paintRect
    } else if (!box.isEmpty()) {
        if (img.width() / img.height() > box.width() / box.height()) {
            paint = QRectF(0, 0, box.width(), img.height() * (box.width() / img.width()));
        } else {
            paint = QRectF(0, 0, img.width() * (box.height() / img.height()), box.height());
        }
        paint.translate((box.width() - paint.width()) / 2, (box.height() - paint.height()) / 2);
    } else if (box.width() > 0) {
        // Calculate height by width
        double f = double(img.width()) / double(img.width());
        paint = QRectF(0, 0, box.width(), qRound(img.height() * f));
        paint.translate(0, (box.height() - paint.height()) / 2);
    } else {
        // Calculate width by height
        double f = double(box.height()) / double(img.height());
        paint = QRectF(0, 0, qRound(img.width() * f), box.height());
        paint.translate((box.width() - paint.width()) / 2, 0);
    }

    if (paint == paintRect)
        return false;

    paintRect = paint;
    q->update();
    return true;
}
