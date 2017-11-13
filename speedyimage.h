#pragma once

#include <QQuickItem>
#include <memory>

class SpeedyImagePrivate;
class SpeedyImage : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
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

    explicit SpeedyImage(QQuickItem *parent = nullptr);
    virtual ~SpeedyImage();

    QString source() const;
    void setSource(const QString &source);

    QSize imageSize() const;
    QSizeF paintedSize() const;

signals:
    void sourceChanged();
    void imageSizeChanged();
    void paintedSizeChanged();

protected:
    virtual QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *updatePaintNodeData);
    virtual void geometryChanged(const QRectF &newGeometry, const QRectF &oldGeometry);

private:
    std::shared_ptr<SpeedyImagePrivate> d;
};

