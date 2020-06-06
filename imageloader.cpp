#include "imageloader_p.h"
#include <QImageReader>
#include <QElapsedTimer>

Q_LOGGING_CATEGORY(lcImageLoad, "speedyimage.load")

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
    for (auto &jobList : d->queue) {
        for (auto &job : jobList) {
            auto jobData = job.lock();
            if (!jobData) {
                continue;
            } else if (jobData->path != path) {
                break;
            } else {
                qCDebug(lcImageLoad) << "enqueued with existing job for" << path << "with draw size" << drawSize;
                jobList.append(newJob.d);
                goto queued;
            }
        }
    }

    // Priority is primitive at the moment
    if (priority > 0) {
        d->queue.push_front(ImageLoaderPrivate::JobDataList{newJob.d});
    } else {
        d->queue.push_back(ImageLoaderPrivate::JobDataList{newJob.d});
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
    for (unsigned int i = 1; i < std::thread::hardware_concurrency() - 1; i++) {
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
            continue;
        }

        QString error;
        auto result = std::make_shared<QImage>(readImage(rd, drawSize, imageSize, error));
        for (auto &weakJob : jobData) {
            auto job = weakJob.lock();
            if (!job) {
                continue;
            }
            job->result = result;
            job->resultSize = imageSize;
            job->error = error;
            if (job->callback) {
                job->callback(ImageLoaderJob(job));
            }
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

    if (!drawSize.isEmpty() && (drawSize.width() < imageSize.width() || drawSize.height() < imageSize.height())) {
        // Downscaling; pick next factor of two size for most efficient decoding. Calculation may not be ideal.
        qreal factor = qMin(imageSize.width() / drawSize.width(), imageSize.height() / drawSize.height());

        if (factor >= 16) {
            factor = 16;
        } else if (factor >= 8) {
            factor = 8;
        } else if (factor >= 4) {
            factor = 4;
        } else if (factor >= 2) {
            factor = 2;
        } else {
            factor = 1;
        }

        // This is only really more efficient to load for JPEG, but smaller textures are a good thing long term
        if (factor > 1) {
            qCDebug(lcImageLoad) << "Using sw scaling for" << imageSize << "->" << drawSize << "at factor" << factor;
            // Be careful to not use imageSize, it may have been transformed
            rd.setScaledSize(rd.size() / factor);
        }
    }

    QImage image = rd.read();
    if (!imageSize.isValid())
        imageSize = image.size();

    if (image.isNull()) {
        error = rd.errorString();
        qCDebug(lcImageLoad) << "error loading" << rd.fileName() << error;
    } else {
        qCDebug(lcImageLoad) << "loaded" << rd.fileName() << imageSize << "at" << image.size() << "in" << tm.elapsed() << "ms for draw size" << drawSize;
    }

    return image;
}
