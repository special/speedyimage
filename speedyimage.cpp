#include "speedyimage_p.h"
#include "imageloader.h"
#include <QSGImageNode>
#include <QQuickWindow>

Q_LOGGING_CATEGORY(lcItem, "speedyimage.item")
Q_DECLARE_LOGGING_CATEGORY(lcPerf)

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
        } else {
            fit = QRectF(0, 0, content.width() * (box.height() / content.height()), box.height()); // fit to y
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

// XXX the case where that weird lagging is happening shows normal image load times and such, but the
// callback times can reach hundreds of ms. main thread busy..?

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
        d->updateTargetSize();
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

// targetSize is the size that the image is scaled to fit within. If targetSize is not set or is set
// with both dimensions <= 0, the targetSize is implicitly the item's size. If a targetSize is set
// with either dimension <= 0, the target is scaled based on imageSize to fit the other dimension.
//
// In all cases targetSize returns an effective size, which may not be the same as in setTargetSize.
//
// TODO: Add a bool property to disable scaled loading entirely
QSize SpeedyImage::targetSize() const
{
    return d->targetSize;
}

void SpeedyImage::setTargetSize(QSize size)
{
    d->explicitTargetSize = size;
    d->updateTargetSize();
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
    d->updateTargetSize();
}

QSGNode *SpeedyImage::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
    if (d->texture && !oldNode) {
        qCWarning(lcItem) << "updatePaintNode called with a saved texture but no oldNode."
            << "This probably means that the texture could've been freed earlier.";
    }

    d->texture = d->cacheEntry.texture();
    if (!d->texture) {
        delete oldNode;
        return nullptr;
    }

    QSGImageNode *node = static_cast<QSGImageNode*>(oldNode);
    if (!node) {
        node = window()->createImageNode();
        node->setFiltering(QSGTexture::Linear);
        //node->setMipmapFiltering(QSGTexture::Linear);
    }
    node->setTexture(d->texture.data());
    node->setRect(d->paintRect.toRect());

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
    targetSize = QSize();
    status = SpeedyImage::Null;
    q->update();
}

bool SpeedyImagePrivate::updateTargetSize()
{
    QSize before = targetSize;
    if (!explicitTargetSize.isEmpty()) {
        targetSize = explicitTargetSize;
    } else if (explicitTargetSize.width() <= 0 && explicitTargetSize.height() <= 0) {
        targetSize = q->size().toSize();
    } else {
        targetSize = fitContentRect(explicitTargetSize, q->imageSize(), alignment, sizeMode).size().toSize();
    }

    if (before == targetSize)
        return false;

    emit q->targetSizeChanged();
    reloadImage();
    return true;
}

QSize SpeedyImagePrivate::targetLoadSize() const
{
    if (!q->window())
        return targetSize;
    return targetSize * q->window()->devicePixelRatio();
}

// Returns true if the the image needs to be reloaded based on the current loadingSize.
bool SpeedyImagePrivate::needsReloadForDrawSize()
{
    if (status == SpeedyImage::Error || status == SpeedyImage::Null) {
        return false;
    }

    // Don't load if targetLoadSize is null; this is an item without dimensions yet
    QSize targetLoad = targetLoadSize();
    if (targetLoad.isNull())
        return false;

    QSizeF loadedSize = cacheEntry.loadedSize();
    QSizeF imageSize = cacheEntry.imageSize();
    if (imageSize.isEmpty() || loadedSize.isEmpty()) {
        // If nothing is loaded yet, always reload for draw size; see reloadImage.
        return true;
    }

    // Scale imageSize within targetLoadSize and reload if either dimension exceeds loadedSize
    QSizeF fit = fitContentRect(targetLoad, imageSize, alignment, sizeMode).size();
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
        || source.isEmpty() || targetLoadSize().isNull())
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

    QSize loadingSize = targetLoadSize();

    if (!loadJob.isNull()) {
        // We can attempt to change the drawSize on an existing job, but there
        // is no guarantee it will take effect. That case can be handled with a
        // check in setImage that will fire off a new job at a larger drawSize
        // if the result is insufficient, and we'll still have an upscale to display
        // meanwhile.
        if (loadingSize != loadJob.drawSize()) {
            qCDebug(lcItem) << this << "updating load size on existing job to" << loadingSize << "for target size" << q->targetSize();
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

    if (!loadJob.isNull()) {
        auto stats = loadJob.stats();
        qCInfo(lcPerf) << stats.tmCreated.elapsed() << "ms - loaded" << cacheEntry.imageSize() << "image at"
                       << cacheEntry.loadedSize() << "- waited" << stats.tmCreated.msecsTo(stats.tmStarted) << "ms for"
                       << "queue position" << stats.queuePosition << "- read in" << stats.tmStarted.msecsTo(stats.tmFinished) << "ms"
                       << "- callback after" << stats.tmFinished.elapsed() << "ms";
    }
    loadJob.reset();
    q->update();

    auto oldStatus = status;
    if (!cacheEntry.error().isEmpty()) {
        status = SpeedyImage::Error;
    } else {
        status = SpeedyImage::Ready;
        Q_ASSERT(!cacheEntry.image().isNull());
    }

    if (calcPaintRect())
        emit q->paintedSizeChanged();
    if (status != oldStatus)
        emit q->statusChanged();

    // Can't really tell if image size changed, but assume it won't between reloads
    if (oldStatus != SpeedyImage::Ready) {
        emit q->imageSizeChanged();
        updateTargetSize();
    }

    // Reload the image again if drawSize has changed and needs a larger scale
    if (needsReloadForDrawSize())
    {
        qCWarning(lcPerf) << "reloading image after cache loaded at" << cacheEntry.loadedSize()
                          << "but wanted" << targetLoadSize();
        reloadImage();
    } else {
        qCDebug(lcItem) << this << "loaded image of" << cacheEntry.imageSize() << "as" << cacheEntry.loadedSize()
                        << "and want" << targetLoadSize() << "for display target" << q->targetSize();
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
