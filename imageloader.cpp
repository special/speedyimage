#include "imageloader_p.h"
#include <QImageReader>
#include <QElapsedTimer>
#include <QtMath>
#include <qglobal.h>

Q_LOGGING_CATEGORY(lcImageLoad, "speedyimage.load")
Q_DECLARE_LOGGING_CATEGORY(lcPerf)

ImageLoader::ImageLoader(QObject *parent)
    : QObject(parent)
    , d(new ImageLoaderPrivate(this))
{
}

ImageLoaderPrivate::ImageLoaderPrivate(ImageLoader *q)
    : q(q)
    , stopping(false)
{
}

ImageLoader::~ImageLoader()
{
}

ImageLoaderPrivate::~ImageLoaderPrivate()
{
}

ImageLoaderJob ImageLoader::enqueue(const QString &path, const QSize &drawSize, int priority, ImageLoaderCallback callback)
{
    ImageLoaderJob newJob(path, drawSize, priority, callback);

    // This algorithm is ..very far from ideal
    QMutexLocker l(&d->mutex);
    int i = 0;
    for (auto &jobList : d->queue) {
        for (auto &job : jobList) {
            auto jobData = job.lock();
            if (!jobData) {
                continue;
            } else if (jobData->path != path) {
                break;
            } else {
                qCDebug(lcImageLoad) << "enqueued with existing job for" << path << "with draw size" << drawSize;
                newJob.d->stats.queuePosition = i;
                jobList.append(newJob.d);
                goto queued;
            }
        }
        i++;
    }

    // TODO: Priority is primitive at the moment, and it doesn't move existing jobs (above)
    if (priority > 0) {
        d->queue.push_front(ImageLoaderPrivate::JobDataList{newJob.d});
        newJob.d->stats.queuePosition = 0;
    } else {
        d->queue.push_back(ImageLoaderPrivate::JobDataList{newJob.d});
        newJob.d->stats.queuePosition = d->queue.size();
    }
    qCDebug(lcImageLoad) << "enqueued new job for" << path << "with draw size" << drawSize;

queued:
    if (d->workers.empty()) {
        d->startWorkers();
    }

    l.unlock();
    d->cv.wakeOne();

    return newJob;
}

void ImageLoaderPrivate::startWorkers()
{
    workers.clear();
    workers.emplace_back(&ImageLoaderPrivate::worker, this);

    int count = qEnvironmentVariableIntValue("SPEEDYIMAGE_WORKERS");
    if (count < 1)
        count = std::thread::hardware_concurrency();

    for (int i = 0; i < count; i++) {
        workers.emplace_back(&ImageLoaderPrivate::worker, this);
    }
    qCDebug(lcImageLoad) << workers.size() << "workers started";
}

void ImageLoaderPrivate::worker()
{
    for (;;) {
        QMutexLocker l(&mutex);
        while (!stopping && queue.empty()) {
            cv.wait(&mutex);
        }
        if (stopping) {
            break;
        }
        JobDataList jobData = queue.front();
        queue.pop_front();
        l.unlock();

        QImageReader rd;
        rd.setAutoTransform(true);
        QSize drawSize, imageSize;

        // jobData is a vector of weak pointers to ImageLoaderJobData representing the same file
        for (auto &weakJob : jobData) {
            auto job = weakJob.lock();
            if (!job) {
                // aborted
                continue;
            }
            job->stats.tmStarted.restart();

            if (rd.fileName().isEmpty()) {
                rd.setFileName(job->path);
            }

            // If only one dimension of drawSize is set, read image size to calculate the other by aspect
            QSize jobDrawSize = job->drawSize;
            if (jobDrawSize.isEmpty() && (jobDrawSize.width() > 0 || jobDrawSize.height() > 0)) {
                if (!imageSize.isValid()) {
                    imageSize = rd.size();
                }
                if (imageSize.isEmpty()) {
                    // Indicates that the plugin can't read size ahead of decoding, which should only
                    // be third party plugins. In this case we can't be smart about scaling anyway, so
                    // just make drawSize infinite.
                    jobDrawSize = QSize(0, 0);
                } else if (jobDrawSize.width() > 0) {
                    // Calculate height by width
                    double f = double(jobDrawSize.width()) / double(imageSize.width());
                    jobDrawSize = QSize(jobDrawSize.width(), qRound(imageSize.height() * f));
                } else {
                    // Width by height
                    double f = double(jobDrawSize.height()) / double(imageSize.height());
                    jobDrawSize = QSize(qRound(imageSize.width() * f), jobDrawSize.height());
                }
            }

            // Now max the potentially-modified jobDrawSize with drawSize
            if (jobDrawSize.isEmpty()) {
                // Full size
                drawSize = QSize(0, 0); // Valid, but empty; unset is invalid
            } else if (!drawSize.isValid() || !drawSize.isEmpty()) {
                // All other cases, except when drawSize is already set to empty for full size
                drawSize = QSize(qMax(drawSize.width(), jobDrawSize.width()), qMax(drawSize.height(), jobDrawSize.height()));
            }
        }

        if (rd.fileName().isEmpty()) {
            // Job aborted
            qCDebug(lcImageLoad) << "job was aborted";
            continue;
        }

        QString error;
        auto result = std::make_shared<QImage>(readImage(rd, drawSize, imageSize, error));
        int count = 0;
        for (auto &weakJob : jobData) {
            auto job = weakJob.lock();
            if (!job) {
                continue;
            }
            count++;
            job->stats.tmFinished.restart();
            job->result = result;
            job->resultSize = imageSize;
            job->error = error;
            if (job->callback) {
                job->callback(ImageLoaderJob(job));
            }
        }

        if (!count) {
            qCDebug(lcImageLoad) << "job finished but nothing is interested anymore";
        }
    }
}

QImage ImageLoaderPrivate::readImage(QImageReader &rd, const QSize &drawSize, QSize &imageSize, QString &error)
{
    QElapsedTimer tm;
    tm.restart();

    imageSize = rd.size();
    auto transform = rd.transformation();
    if (transform & QImageIOHandler::TransformationRotate90)
        imageSize = QSize(imageSize.height(), imageSize.width());

    double factor = 1;
    if (!drawSize.isEmpty() && (drawSize.width() < imageSize.width() || drawSize.height() < imageSize.height())) {
        // libjpeg can improve performance with n/8 scaling during decode. Below mirrors Qt's logic in
        // calculating that value. However, Qt's API takes a size explicitly and will scale in software
        // after decode. Because of rounding, it's sometimes impossible to scale during decode without
        // Qt scaling as well. So the factor is reduced until no rounding is necessary, which
        // successfully avoids calling QImage::scaled at the cost of loading some images too large.
        //
        // Performance impact is less clear. It may be faster to decode at a smaller size and allow Qt
        // to do a bit of scaling than to decode the larger size that this code will pick. That would
        // certainly save memory. Benchmarking would be needed, if not fixing this properly.
        //
        // A real fix will require changing Qt to perform decoder scaling without Qt's scaling.
        factor = qMin(double(imageSize.width()) / drawSize.width(),
                      double(imageSize.height()) / drawSize.height());
        factor = qBound(1, qCeil(8/factor), 8);

        // TODO: a few things worth trying:
        //
        //   1. patch Qt to allow for exact decoder-only scaling
        //   2. consider when software scaling is worthwhile to reduce gfx memory usage

#if 0
        //  XXX for huge images this gets really ugly.
        double idealFactor = factor;
        QSize scaleSize;
        for (; factor < 8; factor++) {
            QSizeF scaleSizeF = QSizeF(imageSize)*(factor/8);
            scaleSize = QSize(qCeil(scaleSizeF.width()), qCeil(scaleSizeF.height()));
            if (scaleSize == scaleSizeF)
                break;
        }

        if (factor != idealFactor) {
            qCInfo(lcImageLoad) << "Image of" << imageSize << "would ideally scale by"
                                << (idealFactor/8) << "but would have to use" << (factor/8)
                                << "to avoid Qt scaling";
        }

        // Use the "ideal" number regardless, because it can otherwise load things at ridiculous
        // sizes
        factor = idealFactor;
#else
        QSizeF scaleSizeF = QSizeF(imageSize)*(factor/8);
        QSize scaleSize = QSize(qCeil(scaleSizeF.width()), qCeil(scaleSizeF.height()));
#endif

        if (factor < 8) {
            qint64 waste = (scaleSize.width() * scaleSize.height() * 4) - (drawSize.width() * drawSize.height() * 4);
            if (scaleSize == scaleSizeF) {
                qCDebug(lcImageLoad) << "Using accurate decoder scaling from" << imageSize << "->" << scaleSize << "for draw size" << drawSize << "oversized by" << waste/1024 << "KB";
            } else {
                qCDebug(lcImageLoad) << "Using bad decoder scaling from" << imageSize << "->" << scaleSize << "for draw size" << drawSize << "oversized by" << waste/1024 << "KB";
            }
            if (rd.size() != imageSize) {
                // scaledSize is applied before transform
                qCDebug(lcImageLoad) << "Swapping dimensions when scaling on a transformed image";
                rd.setScaledSize(QSize(scaleSize.height(), scaleSize.width()));
            } else {
                rd.setScaledSize(scaleSize);
            }
        }
    }

    QImage image = rd.read();
    if (!imageSize.isValid())
        imageSize = image.size();

    if (image.isNull()) {
        error = rd.errorString();
        qCDebug(lcImageLoad) << "error loading" << rd.fileName() << error;
    } else {
        qCDebug(lcImageLoad) << "loaded" << rd.fileName() << imageSize << "at" << image.size() << "for draw size" << drawSize << "with format" << image.format() << image.hasAlphaChannel();
    }

    return image;
}
