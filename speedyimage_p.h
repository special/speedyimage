#pragma once

#include "speedyimage.h"
#include <memory>
#include <QSGTexture>

class SpeedyImagePrivate
{
public:
    SpeedyImage *q;

    QString source;
    bool autoTransform;

    std::unique_ptr<QSGTexture> texture;
    QImage image;
    QRectF paintRect;

    SpeedyImagePrivate(SpeedyImage *q);

    void reloadImage();
    void calcPaintRect();
};
