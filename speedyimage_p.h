#pragma once

#include "speedyimage.h"
#include "imageloader.h"
#include <memory>
#include <QSGTexture>

class SpeedyImagePrivate : public QObject
{
    Q_OBJECT

public:
    SpeedyImage *q;

    QString source;

    ImageLoaderJob loadJob;

    std::unique_ptr<QSGTexture> texture;
    QImage image;
    QSize imageSize;
    QRectF paintRect;

    SpeedyImagePrivate(SpeedyImage *q);

    void clearImage();
    void reloadImage();
    void calcPaintRect();

public slots:
    void setImage(const QImage &img, const QSize &imageSize);

signals:
    void imageLoaded(const QImage &img, const QSize &imageSize);
};
