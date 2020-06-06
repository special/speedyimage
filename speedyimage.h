#pragma once

#include <QQuickItem>
#include <memory>

class SpeedyImagePrivate;
class SpeedyImage : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QSize targetSize READ targetSize WRITE setTargetSize NOTIFY targetSizeChanged)
    Q_PROPERTY(Qt::Alignment alignment READ alignment WRITE setAlignment NOTIFY alignmentChanged)
    Q_PROPERTY(SizeMode sizeMode READ sizeMode WRITE setSizeMode NOTIFY sizeModeChanged)

    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(QSize imageSize READ imageSize NOTIFY imageSizeChanged)
    Q_PROPERTY(QSizeF paintedSize READ paintedSize NOTIFY paintedSizeChanged)

public:
    enum Status {
        Null,
        Ready,
        Loading,
        Error
    };
    Q_ENUM(Status)

    enum class SizeMode {
        Fit,
        Crop
    };
    Q_ENUM(SizeMode)

    explicit SpeedyImage(QQuickItem *parent = nullptr);
    virtual ~SpeedyImage();

    QString source() const;
    void setSource(const QString &source);

    QSize targetSize() const;
    void setTargetSize(QSize size);

    Qt::Alignment alignment() const;
    void setAlignment(Qt::Alignment align);

    SizeMode sizeMode() const;
    void setSizeMode(SizeMode mode);

    Status status() const;

    QSize imageSize() const;
    QSizeF paintedSize() const;

signals:
    void sourceChanged();
    void targetSizeChanged();
    void alignmentChanged();
    void sizeModeChanged();
    void statusChanged();
    void imageSizeChanged();
    void paintedSizeChanged();

protected:
    virtual QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *updatePaintNodeData);
    virtual void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry);
    virtual void componentComplete();

private:
    std::shared_ptr<SpeedyImagePrivate> d;
};

