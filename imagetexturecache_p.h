#pragma once

#include "imagetexturecache.h"
#include <QAtomicInteger>
#include <QImage>
#include <QMutex>

class ImageTextureCachePrivate : public QObject
{
    Q_OBJECT

public:
    static QHash<QQuickWindow*,std::weak_ptr<ImageTextureCache>> instances;
    QQuickWindow *window;

    QMutex mutex;
    QHash<QString,std::shared_ptr<ImageTextureCacheData>> cache;
    QAtomicInt cacheCost;

    QMutex freeMutex;
    QVector<std::shared_ptr<ImageTextureCacheData>> freeable;
    int freeThrottle;

    int softLimit;

    ImageTextureCachePrivate(QQuickWindow *window);
    ~ImageTextureCachePrivate();

    void setFreeable(const std::shared_ptr<ImageTextureCacheData> &data, bool freeable);

public slots:
    void renderThreadFree();
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
        , cost(1)
        , refCount(0)
    {
    }

    const QString key;
    ImageTextureCachePrivate * const cache;

    QImage image;
    QString error;
    QSize imageSize;
    QSGTexture *texture;
    int cost;

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

    int getRefCount()
    {
        return refCount.load();
    }

    void updateCost();

private:
    // Number of Entry objects referencing this data
    QAtomicInteger<int> refCount;
};
