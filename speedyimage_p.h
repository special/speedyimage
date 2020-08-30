#pragma once

#include "speedyimage.h"
#include "imageloader.h"
#include "imagetexturecache.h"
#include <memory>
#include <QSGTexture>

// texture TODO:
//
// - ponder whether there are any actually useful means of async texture uploads,
//   because (very) large textures are easily >20ms
//
// - ponder performance impact of having ridiculously large texture memory usage vs.
//   keeping only the textures that are immediately useful
//
// other TODO:
//
// - would hiding the view when covered by viewer be useful? should speedyimage have
//   particular behavior when it's made !visible? potentially not a good thing: it would
//   have to upload many textures when the view becomes visible again. also, those textures
//   uploading for the view is pre-loading them for the viewer.
//
// - run renderThreadFree on a render-thread timer. start/stop based on free list size.
//
// queue TODO:
//
// - priority set in QML would be nice. it can factor in visible, currentIndex, cacheBuffer, etc
//   - how much would this actually help?
//
// - more workers?
//
//
// queue:
//   - aborts/etc do happen often with huge images, but the queue still becomes huge and load
//     is quite far behind
//
//   - that would help quite a bit when the view falls behind; although priority _might_ too.
//     the logic for the two is certainly related.

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
