#pragma once

#include <QObject>
#include <QLoggingCategory>
#include <QImage>
#include <QWaitCondition>
#include <QElapsedTimer>
#include <memory>
#include <functional>

class ImageLoaderJob;
using ImageLoaderCallback = std::function<void(const ImageLoaderJob &)>;

struct ImageLoaderJobStats {
    QElapsedTimer tmCreated, tmStarted, tmFinished;
    int queuePosition = -1;
};

// ImageLoaderJob is a strong reference to a pending or completed job for an ImageLoader.
// Jobs are reference counted, and will be aborted if no references remain when the job
// reaches the front of the queue.
struct ImageLoaderJobData
{
    QString path;
    QSize drawSize;
    int priority;
    ImageLoaderCallback callback;

    std::shared_ptr<QImage> result;
    QSize resultSize;
    QString error;
    ImageLoaderJobStats stats;
};

class ImageLoaderJob
{
    friend class ImageLoader;
    friend class ImageLoaderPrivate;

public:
    ImageLoaderJob() { }
    ImageLoaderJob(const ImageLoaderJob &o) : d(o.d) { }
    ~ImageLoaderJob() { }

    bool isNull() const { return !d; }
    void reset() { d.reset(); }

    QString path() const { return d ? d->path : QString(); }
    QSize drawSize() const { return d ? d->drawSize : QSize(); }
    int priority() const { return d ? d->priority : 0; }
    ImageLoaderCallback callback() const { return d ? d->callback : ImageLoaderCallback(); }

    void setDrawSize(const QSize &size)
    {
        if (d) d->drawSize = size;
    }

    bool finished() const { return d ? (d->result || !d->error.isEmpty()) : false; }
    QImage result() const { return d && d->result ? *d->result : QImage(); }
    QSize imageSize() const { return d ? d->resultSize : QSize(); }
    QString error() const { return d ? d->error : QString(); }

    ImageLoaderJobStats stats() const { return d ? d->stats : ImageLoaderJobStats(); }

private:
    std::shared_ptr<ImageLoaderJobData> d;

    ImageLoaderJob(const std::shared_ptr<ImageLoaderJobData> &d)
        : d(d)
    {
    }

    ImageLoaderJob(const QString &path, const QSize &drawSize, int priority, ImageLoaderCallback callback)
        : d(std::make_shared<ImageLoaderJobData>())
    {
        d->path = path;
        d->drawSize = drawSize;
        d->priority = priority;
        d->callback = callback;
        d->stats.tmCreated.restart();
    }
};

class ImageLoaderPrivate;
class ImageLoader : public QObject
{
    Q_OBJECT

public:
    explicit ImageLoader(QObject *parent = nullptr);
    virtual ~ImageLoader();

    ImageLoaderJob enqueue(const QString &path, const QSize &drawSize, int priority, ImageLoaderCallback callback);

private:
    std::shared_ptr<ImageLoaderPrivate> d;
};

Q_DECLARE_LOGGING_CATEGORY(lcImageLoad)
