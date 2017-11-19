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

    ImageTextureCache *imageCache;
    ImageTextureCacheEntry cacheEntry;
    ImageLoaderJob loadJob;

    QRectF paintRect;

    SpeedyImagePrivate(SpeedyImage *q);

    void clearImage();
    void reloadImage();
    bool calcPaintRect();
    bool needsReloadForDrawSize();

public slots:
    void setWindow(QQuickWindow *window);
    void cacheEntryChanged(const QString &key);
};
