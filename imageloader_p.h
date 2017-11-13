#pragma once

#include "imageloader.h"
#include <deque>
#include <thread>
#include <QMutex>

class ImageLoaderPrivate
{
public:
    using JobDataList = QVector<std::weak_ptr<ImageLoaderJobData>>;

    ImageLoaderPrivate(ImageLoader *q);
    virtual ~ImageLoaderPrivate();

    ImageLoader *q;
    QMutex mutex;
    QWaitCondition cv;
    bool stopping;
    std::deque<JobDataList> queue;
    std::vector<std::thread> workers;

    void startWorkers();
    void worker();
    QImage readImage(const QString &path, const QSize &drawSize, QSize &imageSize);
};
