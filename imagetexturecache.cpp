#include "imagetexturecache_p.h"
#include <QLoggingCategory>
#include <QSGTexture>

Q_LOGGING_CATEGORY(lcCache, "speedyimage.cache")

QHash<QQuickWindow*,ImageTextureCache*> ImageTextureCachePrivate::instances;

// Only valid on GUI thread
ImageTextureCache *ImageTextureCache::forWindow(QQuickWindow *window)
{
    auto p = ImageTextureCachePrivate::instances.value(window);
    if (!p) {
        p = new ImageTextureCache(window);
        ImageTextureCachePrivate::instances.insert(window, p);
    }
    return p;
}

ImageTextureCache::ImageTextureCache(QQuickWindow *window)
    : QObject(window)
    , d(std::make_shared<ImageTextureCachePrivate>(window))
{
}

ImageTextureCachePrivate::ImageTextureCachePrivate(QQuickWindow *window)
    : window(window)
    , freeThrottle(0)
    , softLimit(qgetenv("SPEEDYIMAGE_CACHE_SIZE").toInt())
{
    if (softLimit < 1) {
        softLimit = 512 * 1048576;
    }

    connect(window, &QQuickWindow::beforeSynchronizing, this, &ImageTextureCachePrivate::renderThreadFree, Qt::DirectConnection);
}

ImageTextureCache::~ImageTextureCache()
{
}

ImageTextureCachePrivate::~ImageTextureCachePrivate()
{
}

ImageTextureCacheEntry ImageTextureCache::get(const QString &key)
{
    QMutexLocker l(&d->mutex);
    auto data = d->cache.value(key);
    if (!data) {
        data = std::make_shared<ImageTextureCacheData>(d.get(), key);
        d->cache.insert(key, data);
        data->updateCost();
    }
    return ImageTextureCacheEntry(data);
}

void ImageTextureCache::insert(const QString &key, const QImage &image, const QSize &imageSize)
{
    auto entry = get(key);
    entry.d->image = image;
    entry.d->imageSize = imageSize;
    // XXX smarter texture management
    // XXX Atlas won't be used because this isn't done from render thread
    entry.d->texture = d->window->createTextureFromImage(image, {QQuickWindow::TextureCanUseAtlas, QQuickWindow::TextureIsOpaque});
    Q_ASSERT(entry.d->texture);
    entry.d->updateCost();

    emit changed(key);
}

void ImageTextureCachePrivate::setFreeable(const std::shared_ptr<ImageTextureCacheData> &data, bool set)
{
    QMutexLocker l(&freeMutex);
    if (set) {
        freeable.append(data);
    } else {
        freeable.removeOne(data);
    }
}

void ImageTextureCachePrivate::renderThreadFree()
{
    // Only check cache every 100 frames
    // XXX Would a timer with affinity to the render thread do this without being as reliant on render timing?
    if (++freeThrottle < 100)
        return;
    freeThrottle = 0;

    qCDebug(lcCache) << "cache using" << cacheCost << "of" << softLimit;
    if (cacheCost <= softLimit)
        return;

    // Copy freeable and clear so we can release the mutex while working on cache, to avoid deadlocks
    QMutexLocker freeLock(&freeMutex);
    if (freeable.isEmpty())
        return;
    auto freeList = freeable;
    freeable.clear();
    freeLock.unlock();

    // There is no path for a data to go from 0 to 1 ref without holding the cache mutex,
    // so holding it guarantees that data with 0 ref can be freed safely.
    QMutexLocker cacheLock(&mutex);
    while (!freeList.isEmpty()) {
        auto data = freeList.takeFirst();
        if (data->getRefCount() > 0)
            continue;

        qCDebug(lcCache) << "cache freeing" << data->cost << "from" << data->key;

        delete data->texture;
        data->texture = nullptr;

        Q_ASSERT(cache.value(data->key) == data);
        cache.remove(data->key);

        cacheCost -= data->cost;
        if (cacheCost <= softLimit)
            break;
    }

    qCDebug(lcCache) << "cache using" << cacheCost << "of" << softLimit << "after free;" << freeList.size() << "items still freeable";
    cacheLock.unlock();

    // Move anything we didn't free back into freeable
    if (!freeList.isEmpty()) {
        freeLock.relock();
        freeList += freeable;
        freeable = freeList;
        freeLock.unlock();
    }
}

ImageTextureCacheEntry::ImageTextureCacheEntry()
{
}

ImageTextureCacheEntry::ImageTextureCacheEntry(const std::shared_ptr<ImageTextureCacheData> &dp)
    : d(dp)
{
    if (d)
        d->ref();
}

ImageTextureCacheEntry::ImageTextureCacheEntry(const ImageTextureCacheEntry &o)
    : d(o.d)
{
    if (d)
        d->ref();
}

ImageTextureCacheEntry::~ImageTextureCacheEntry()
{
    if (d)
        d->deref();
}

ImageTextureCacheEntry &ImageTextureCacheEntry::operator=(const ImageTextureCacheEntry &o)
{
    if (d != o.d) {
        if (d)
            d->deref();
        d = o.d;
        if (d)
            d->ref();
    }
    return *this;
}

void ImageTextureCacheEntry::reset()
{
    if (d)
        d->deref();
    d.reset();
}

QImage ImageTextureCacheEntry::image() const
{
    return d ? d->image : QImage();
}

QSize ImageTextureCacheEntry::loadedSize() const
{
    return d ? d->image.size() : QSize();
}

QSize ImageTextureCacheEntry::imageSize() const
{
    return d ? d->imageSize : QSize();
}

QSGTexture *ImageTextureCacheEntry::texture() const
{
    return d ? d->texture : nullptr;
}

void ImageTextureCacheData::updateCost()
{
    int newCost = 1;
    if (texture) {
        QSize sz = texture->textureSize();
        newCost = qMax(sz.width() * sz.height() * 3, 1);
    }

    if (cost != newCost) {
        int delta = newCost - cost;
        cost = newCost;
        cache->cacheCost += delta;
    }
}
