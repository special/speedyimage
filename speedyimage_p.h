#pragma once

#include "speedyimage.h"
#include "imageloader.h"
#include "imagetexturecache.h"
#include <memory>
#include <QSGTexture>

class SpeedyImagePrivate : public QObject
{
    Q_OBJECT

public:
    SpeedyImage *q;

    QString source;
    SpeedyImage::Status status;
    bool componentComplete;

    std::shared_ptr<ImageTextureCache> imageCache;
    ImageTextureCacheEntry cacheEntry;
    ImageLoaderJob loadJob;
    SGSharedTexture texture;

    QSize targetSize;
    QSize explicitTargetSize;
    QRectF paintRect;

    Qt::Alignment alignment;
    SpeedyImage::SizeMode sizeMode;

    SpeedyImagePrivate(SpeedyImage *q);

    void clearImage();
    void reloadImage();
    bool calcPaintRect();
    bool updateTargetSize();
    bool needsReloadForDrawSize();

    QSize targetLoadSize() const;

public slots:
    void setWindow(QQuickWindow *window);
    void cacheEntryChanged(const QString &key);
};
