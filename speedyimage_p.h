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

    ImageTextureCache *imageCache;
    ImageTextureCacheEntry cacheEntry;
    ImageLoaderJob loadJob;

    bool explicitLoadingSize;
    QSize loadingSize;
    QRectF paintRect;

    SpeedyImagePrivate(SpeedyImage *q);

    void clearImage();
    void reloadImage();
    bool calcPaintRect();
    void applyLoadingSize(const QSize &size);
    bool needsReloadForDrawSize();

public slots:
    void setWindow(QQuickWindow *window);
    void cacheEntryChanged(const QString &key);
};
