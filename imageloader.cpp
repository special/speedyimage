#include "imageloader_p.h"
#include <QImageReader>

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

        // jobData is a vector of weak pointers to ImageLoaderJobData representing the same file
        QString path;
        QSize drawSize;
        for (auto &weakJob : jobData) {
            auto job = weakJob.lock();
            if (!job) {
                continue;
            }
            path = job->path;
            drawSize = QSize(qMax(drawSize.width(), job->drawSize.width()), qMax(drawSize.height(), job->drawSize.height()));
        }

        if (path.isEmpty()) {
            // Job aborted
            continue;
        }

        QSize imageSize;
        auto result = std::make_shared<QImage>(readImage(path, drawSize, imageSize));
        for (auto &weakJob : jobData) {
            auto job = weakJob.lock();
            if (!job) {
                continue;
            }
            job->result = result;
            job->resultSize = imageSize;
            if (job->callback) {
                job->callback(ImageLoaderJob(job));
            }
        }
    }
}

QImage ImageLoaderPrivate::readImage(const QString &path, const QSize &drawSize, QSize &imageSize)
{
    imageSize = QSize();

    QImageReader rd(path);
    rd.setAutoTransform(true);

    QByteArray format = rd.format();
    if (format == "jpeg") {
        imageSize = rd.size();
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

            if (factor > 1) {
                qCDebug(lcImageLoad) << "Using JPEG scaling for" << imageSize << "->" << drawSize << "at factor" << factor;
                rd.setScaledSize(imageSize / factor);
            }
        }
    }

    QImage image = rd.read();
    if (!imageSize.isValid()) {
        imageSize = image.size();
    }

    qCDebug(lcImageLoad) << "loaded" << path << imageSize << "at" << image.size() << "with draw size" << drawSize;
    return image;
}
