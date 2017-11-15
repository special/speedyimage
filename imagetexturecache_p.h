#pragma once

#include "imagetexturecache.h"
#include <QAtomicInteger>
#include <QImage>

class ImageTextureCachePrivate
{
public:
    static QHash<QQuickWindow*,ImageTextureCache*> instances;
    QQuickWindow *window;

    QHash<QString,std::shared_ptr<ImageTextureCacheData>> cache;
    QVector<std::shared_ptr<ImageTextureCacheData>> freeable;

    ImageTextureCachePrivate();
    ~ImageTextureCachePrivate();

    void setFreeable(const std::shared_ptr<ImageTextureCacheData> &data, bool freeable);
};

// Internal representation of data in the cache, referenced by
// ImageTextureCacheEntry.
struct ImageTextureCacheData : public std::enable_shared_from_this<ImageTextureCacheData>
{
public:
    ImageTextureCacheData(ImageTextureCachePrivate *cache, const QString &key)
        : key(key)
        , cache(cache)
        , texture(nullptr)
        , refCount(0)
    {
    }

    const QString key;
    ImageTextureCachePrivate * const cache;

    QImage image;
    QSize imageSize;
    QSGTexture *texture;

    void ref() {
        if (!refCount.fetchAndAddOrdered(1)) {
            cache->setFreeable(shared_from_this(), false);
        }
    }

    void deref()
    {
        if (!refCount.deref()) {
            cache->setFreeable(shared_from_this(), true);
        }
    }

private:
    // Number of Entry objects referencing this data
    QAtomicInteger<int> refCount;
};
