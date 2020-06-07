#include "imagetexturecache_p.h"
#include <QLoggingCategory>
#include <QSGTexture>
#include <QElapsedTimer>

Q_LOGGING_CATEGORY(lcCache, "speedyimage.cache")
Q_LOGGING_CATEGORY(lcPerf, "speedyimage.perf", QtWarningMsg)

QHash<QQuickWindow*,std::shared_ptr<ImageTextureCache>> ImageTextureCachePrivate::instances;

// Only valid on GUI thread
std::shared_ptr<ImageTextureCache> ImageTextureCache::forWindow(QQuickWindow *window)
{
    auto p = ImageTextureCachePrivate::instances.value(window);
    if (!p) {
        p = std::shared_ptr<ImageTextureCache>(new ImageTextureCache(window));
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
        softLimit = 128;
    }
    softLimit *= 1048576;
    qCDebug(lcPerf) << "cache soft limit is" << (softLimit/1048576) << "MB";

    connect(window, &QQuickWindow::frameSwapped, this, &ImageTextureCachePrivate::renderThreadFree, Qt::DirectConnection);
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
    if (entry.d->image != image) {
        entry.d->image = image;
        entry.d->texture.reset();
    }
    entry.d->imageSize = imageSize;
    entry.d->error = QString();
    entry.d->updateCost();
    emit changed(key);
}

void ImageTextureCache::insert(const QString &key, const QString &error)
{
    auto entry = get(key);
    entry.d->image = QImage();
    entry.d->imageSize = QSize();
    entry.d->error = error;
    entry.d->texture.reset();
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

    QElapsedTimer tm;
    tm.restart();
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
    int freedCount = 0, freedCost = 0;
    while (!freeList.isEmpty()) {
        auto data = freeList.takeFirst();
        if (data->getRefCount() > 0)
            continue;

        qCDebug(lcCache) << "cache freeing" << data->cost << "from" << data->key;
        freedCount++;
        freedCost += data->cost;

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

    qCDebug(lcPerf) << tm.elapsed() << "ms - renderThreadFree freed" << freedCount << "with cost" << freedCost;
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

const QImage ImageTextureCacheEntry::image() const
{
    return d ? d->image : QImage();
}

QString ImageTextureCacheEntry::error() const
{
    return d ? d->error : QString();
}

QSize ImageTextureCacheEntry::loadedSize() const
{
    return d ? d->image.size() : QSize();
}

QSize ImageTextureCacheEntry::imageSize() const
{
    return d ? d->imageSize : QSize();
}

static void deleteSharedTexture(QSGTexture *texture)
{
    if (!texture)
        return;
    // Assuming that the object has affinity to the render thread
    qCDebug(lcCache) << "deleting texture" << texture;
    texture->deleteLater();
}

// Should only be called by the render thread
SGSharedTexture ImageTextureCacheEntry::texture()
{
    if (!d || d->image.isNull())
        return nullptr;
    if (!d->texture.isNull())
        return d->texture;

    QSGTexture *tex = d->cache->window->createTextureFromImage(d->image,
                                                               {QQuickWindow::TextureIsOpaque});
    d->texture = SGSharedTexture(tex, &deleteSharedTexture);
    return d->texture;
}

void ImageTextureCacheData::updateCost()
{
    // This isn't an accurate accounting of memory usage. It doesn't count memory used by a
    // QSGTexture (which could have larger dimensions than the image). But it is more or less
    // correct in a relative sense.
    int newCost = qMax(image.sizeInBytes(), qsizetype(1));

    if (cost != newCost) {
        int delta = newCost - cost;
        cost = newCost;
        cache->cacheCost += delta;
    }
}
