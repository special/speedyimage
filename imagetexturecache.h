#pragma once

#include <QObject>
#include <QQuickWindow>
#include <memory>

class QSGTexture;

class ImageTextureCacheData;
class ImageTextureCacheEntry;
class ImageTextureCachePrivate;

using SGSharedTexture = QSharedPointer<QSGTexture>;

// ImageTextureCacheEntry represents an entry in the image texture cache,
// holds a reference to that entry to ensure its lifetime.
class ImageTextureCacheEntry
{
    friend class ImageTextureCache;

public:
    ImageTextureCacheEntry();
    ImageTextureCacheEntry(const ImageTextureCacheEntry &o);
    ~ImageTextureCacheEntry();

    ImageTextureCacheEntry &operator=(const ImageTextureCacheEntry &o);

    bool isNull() const { return !d; }
    bool isEmpty() const { return image().isNull() && error().isEmpty(); }
    void reset();

    const QImage image() const;
    QString error() const;
    QSize loadedSize() const;
    QSize imageSize() const;
    SGSharedTexture texture();

private:
    std::shared_ptr<ImageTextureCacheData> d;

    ImageTextureCacheEntry(const std::shared_ptr<ImageTextureCacheData> &dp);
};

class ImageTextureCache : public QObject
{
    Q_OBJECT

public:
    static std::shared_ptr<ImageTextureCache> forWindow(QQuickWindow *window);
    virtual ~ImageTextureCache();

    // Query the cache with the given key, and return a CacheEntry with the
    // result and a strong reference to this entry in the cache.
    //
    // Even if the key does not exist or has no result, an entry will be added
    // to the cache. If the key is later inserted, the entry will be updated.
    ImageTextureCacheEntry get(const QString &key);

    void insert(const QString &key, const QImage &image, const QSize &imageSize);
    void insert(const QString &key, const QString &error);

signals:
    void changed(const QString &key);

private:
    std::shared_ptr<ImageTextureCachePrivate> d;

    explicit ImageTextureCache(QQuickWindow *window);
};
