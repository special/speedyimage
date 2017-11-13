#pragma once

#include <QObject>
#include <memory>

// ImageLoaderJob is a strong reference to a pending or completed job for an ImageLoader.
// Jobs are reference counted, and will be aborted if no references remain when the job
// reaches the front of the queue.
class ImageLoaderJobData;
class ImageLoaderJob
{
    friend class ImageLoader;

public:
    using CallbackFunc = std::function<void(const ImageLoaderJob &)>;

    ImageLoaderJob(const ImageLoaderJob &o);
    ~ImageLoaderJob();

    QString path() const { return d->path; }
    QSize drawSize() const { return d->drawSize; }
    int priority() const { return d->priority; }
    CallbackFunc callback() const { return d->callback; }

    bool finished() const { return d->result; }
    QImage result() const { return d->result ? *d->result : QImage(); }

private:
    std::shared_ptr<ImageLoaderJobData> d;

    ImageLoaderJob(const QString &path, const QSize &drawSize, int priority, CallbackFunc callback);
};

class ImageLoaderJobData
{
    QString path;
    QSize drawSize;
    int priority;
    ImageLoaderJob::CallbackFunc callback;

    std::shared_ptr<QImage> result;
};

class ImageLoaderPrivate
class ImageLoader : public QObject
{
    Q_OBJECT

public:
    explicit ImageLoader(QObject *parent = nullptr);
    virtual ~ImageLoader();

    ImageLoaderJob enqueue(const QString &path, const QSize &drawSize, int priority, ImageLoaderJob::CallbackFunc callback);

private:
    std::shared_ptr<ImageLoaderPrivate> d;
};

