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
    QMutexLocker l(&d->mutex);
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
    // XXX smarter texture management
    // XXX Atlas won't be used because this isn't done from render thread
    // XXX needs lifetime management; supposed to free from the render thread
    // XXX Use the window's post-render signal to schedule a call to check free list from render thread?
    entry.d->texture = d->window->createTextureFromImage(image, {QQuickWindow::TextureCanUseAtlas, QQuickWindow::TextureIsOpaque});
    Q_ASSERT(entry.d->texture);
    emit changed(key);
}

void ImageTextureCachePrivate::setFreeable(const std::shared_ptr<ImageTextureCacheData> &data, bool set)
{
    // XXX Currently unused, and also not threadsafe; can't just reuse mutex because this can be called under mutex
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

