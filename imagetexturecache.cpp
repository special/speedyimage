#include "imagetexturecache_p.h"

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
    , d(std::make_shared<ImageTextureCachePrivate>())
{
    d->window = window;
}

ImageTextureCachePrivate::ImageTextureCachePrivate()
    : window(nullptr)
{
}

ImageTextureCache::~ImageTextureCache()
{
}

ImageTextureCachePrivate::~ImageTextureCachePrivate()
{
}

ImageTextureCacheEntry ImageTextureCache::get(const QString &key)
{
    auto data = d->cache.value(key);
    if (!data) {
        data = std::make_shared<ImageTextureCacheData>(d.get(), key);
        d->cache.insert(key, data);
    }
    return ImageTextureCacheEntry(data);
}

void ImageTextureCache::insert(const QString &key, const QImage &image, const QSize &imageSize)
{
    auto entry = get(key);
    entry.d->image = image;
    entry.d->imageSize = imageSize;
    // XXX texture management
    entry.d->texture = nullptr;
    emit changed(key, entry);
}

void ImageTextureCachePrivate::setFreeable(const std::shared_ptr<ImageTextureCacheData> &data, bool set)
{
    if (set) {
        freeable.append(data);
    } else {
        freeable.removeOne(data);
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

QSGTexture *ImageTextureCacheEntry::texture() const
{
    return d ? d->texture : nullptr;
}

