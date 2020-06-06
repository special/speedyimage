#include "speedyimage_p.h"
#include "imageloader.h"
#include <QSGSimpleTextureNode>
#include <QQuickWindow>

Q_LOGGING_CATEGORY(lcItem, "speedyimage.item")

static ImageLoader *imgLoader;

// Return a rectangle fitting content within box while preserving
// aspect ratio. If either dimension of box is zero, scale based
// on the other dimension.
// XXX ^ not quite
QRectF fitContentRect(const QSizeF &box, const QSizeF &content, Qt::Alignment align, SpeedyImage::SizeMode mode)
{
    QRectF fit;

    if (content.isEmpty() || (box.width() == 0 && box.height() == 0)) {
        return fit;
    }

    // XXX limits correctly when y is larger, but not when x is larger

    if (!box.isEmpty()) {
        double fContent = content.width() / content.height();
        double fBox = box.width() / box.height();
        bool fitLongEdge = (mode == SpeedyImage::SizeMode::Fit);
        if (fitLongEdge == (fContent > fBox)) {
            fit = QRectF(0, 0, box.width(), content.height() * (box.width() / content.width())); // fit to x
            qCDebug(lcItem) << "fit to box width:" << fContent << fBox << fit;
        } else {
            fit = QRectF(0, 0, content.width() * (box.height() / content.height()), box.height()); // fit to y
            qCDebug(lcItem) << "fit to box height:" << fContent << fBox << fit;
        }
    } else if (box.width() > 0 || mode == SpeedyImage::SizeMode::Crop) {
        // Calculate height by width
        double f = double(content.width()) / double(content.width());
        fit = QRectF(0, 0, box.width(), content.height() * f);
    } else {
        // Calculate width by height
        double f = double(box.height()) / double(content.height());
        fit = QRectF(0, 0, qRound(content.width() * f), box.height());
    }

    // xxx need top for crop
    if (align & Qt::AlignHCenter)
        fit.moveLeft((box.width() - fit.width()) / 2);
    else if (align & Qt::AlignRight)
        fit.moveRight(box.width());

    if (align & Qt::AlignVCenter)
        fit.moveTop((box.height() - fit.height()) / 2);
    else if (align & Qt::AlignBottom)
        fit.moveBottom(box.height());

    return fit;
}

SpeedyImage::SpeedyImage(QQuickItem *parent)
    : QQuickItem(parent)
    , d(new SpeedyImagePrivate(this))
{
    setFlag(ItemHasContents);

    if (!imgLoader) {
        imgLoader = new ImageLoader;
    }
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

    d->clearImage();
    d->source = source;

    if (!d->source.isEmpty()) {
        // reloadImage will start loading the image (if possible) or immediately set it
        // from the cache entry. If the image is set immediately, status and other signals
        // will have been sent via cacheEntryChanged.
        d->reloadImage();
        // If status is still null, there was no instant cache entry
        if (d->status == Null) {
            d->status = Loading;
            emit statusChanged();
        }

        // These signals are necessary if we're still loading (because of clearImage), or
        // if the cacheEntry was loaded and the value still matches what clearImage set.
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

Qt::Alignment SpeedyImage::alignment() const
{
    return d->alignment;
}

void SpeedyImage::setAlignment(Qt::Alignment align)
{
    if (d->alignment == align)
        return;
    d->alignment = align;
    d->calcPaintRect();
    emit alignmentChanged();
}

SpeedyImage::SizeMode SpeedyImage::sizeMode() const
{
    return d->sizeMode;
}

void SpeedyImage::setSizeMode(SizeMode mode)
{
    if (d->sizeMode == mode)
        return;
    d->sizeMode = mode;
    d->calcPaintRect();
    emit sizeModeChanged();
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
    QQuickItem::geometryChanged(newGeometry, oldGeometry);

    QSize size = newGeometry.size().toSize();
    if (size == oldGeometry.size().toSize())
        return;

    if (d->calcPaintRect())
        emit paintedSizeChanged();
    if (!d->explicitLoadingSize)
        d->applyLoadingSize(newGeometry.size().toSize());
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

void SpeedyImage::componentComplete()
{
    QQuickItem::componentComplete();
    d->componentComplete = true;
    d->reloadImage();
}

SpeedyImagePrivate::SpeedyImagePrivate(SpeedyImage *q)
    : q(q)
    , status(SpeedyImage::Null)
    , componentComplete(false)
    , explicitLoadingSize(false)
    , alignment(Qt::AlignCenter)
    , sizeMode(SpeedyImage::SizeMode::Fit)
{
    connect(q, &QQuickItem::windowChanged, this, &SpeedyImagePrivate::setWindow);
}

void SpeedyImagePrivate::setWindow(QQuickWindow *window)
{
    if (imageCache) {
        disconnect(imageCache.get(), nullptr, this, nullptr);
        imageCache.reset();
        // Must be cleared because cacheEntry's texture is specific to a window.
        // If possible, reloadImage below will fill it in from the new imageCache.
        clearImage();
    }

    if (window) {
        imageCache = ImageTextureCache::forWindow(window);
        connect(imageCache.get(), &ImageTextureCache::changed, this, &SpeedyImagePrivate::cacheEntryChanged);
        connect(window, &QQuickWindow::sceneGraphInitialized, this, &SpeedyImagePrivate::reloadImage);

        // Trigger reload in case one was blocked by not having imageCache earlier. Has no effect
        // if this is not necessary.
        reloadImage();
    }
}

void SpeedyImagePrivate::clearImage()
{
    cacheEntry.reset();
    loadJob.reset();
    paintRect = QRectF();
    status = SpeedyImage::Null;
    q->update();
}

void SpeedyImagePrivate::applyLoadingSize(QSize size)
{
    // If size has a zero dimension, set based on imageSize
    QSize imageSize = q->imageSize();
    if (size.isEmpty() && !imageSize.isEmpty()) {
        if (size.width() == 0 && size.height() == 0)
            size = imageSize;
        else
            size = fitContentRect(size, imageSize, alignment, sizeMode).size().toSize();
    }

    if (loadingSize == size)
        return;

    loadingSize = size;
    emit q->loadingSizeChanged();

    // Does nothing if a reload is not necessary
    reloadImage();
}

// Returns true if the the image needs to be reloaded based on the current loadingSize.
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

    if (loadingSize.width() == 0 && loadingSize.height() == 0) {
        // If loadingSize is exactly zero, reload only if full imageSize isn't loaded yet
        return imageSize != loadedSize;
    }

    // Scale imageSize within loadingSize and reload if either dimension exceeds loadedSize
    QSizeF fit = fitContentRect(loadingSize, imageSize, alignment, sizeMode).size();
    if ((fit.width() > loadedSize.width() && imageSize.width() > loadedSize.width()) ||
        (fit.height() > loadedSize.height() && imageSize.height() > loadedSize.height()))
    {
        return true;
    }

    return false;
}

void SpeedyImagePrivate::reloadImage()
{
    if (!componentComplete || !imageCache || !q->window() || !q->window()->isSceneGraphInitialized()
        || source.isEmpty() || !loadingSize.isValid())
    {
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
            qCDebug(lcItem) << this << "updating load size on existing job to" << loadingSize;
            loadJob.setDrawSize(loadingSize);
        }
    } else if (imageCache) {
        // Copy for lambda
        auto src = source;
        std::shared_ptr<ImageTextureCache> cache = imageCache;

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

    qCDebug(lcItem) << this << "cache updated for" << key << "job duration" << loadJob.elapsed();
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
    if (oldStatus != SpeedyImage::Ready) {
        emit q->imageSizeChanged();
        // If loadingSize has a zero dimension, update it based on the imageSize
        if (loadingSize.isEmpty())
            applyLoadingSize(loadingSize);
    }

    // Reload the image again if drawSize has changed and needs a larger scale
    if (needsReloadForDrawSize())
    {
        qCDebug(lcItem) << this << "draw size increased while loading, reloading at larger size";
        reloadImage();
    }
}

bool SpeedyImagePrivate::calcPaintRect()
{
    QSizeF box(q->width(), q->height());
    QSizeF img(cacheEntry.loadedSize());
    QRectF paint = fitContentRect(box, img, alignment, sizeMode);

    // XXX Should paint be rounded? Might lead to bad results if it's not...
    if (paint == paintRect)
        return false;

    paintRect = paint;
    q->update();
    return true;
}
