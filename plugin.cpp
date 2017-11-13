#include <QQmlExtensionPlugin>
#include "speedyimage.h"

class SpeedyImagePlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)

public:
    void registerTypes(const char *uri)
    {
        Q_ASSERT(uri == QLatin1Literal("SpeedyImage"));
        qmlRegisterType<SpeedyImage>(uri, 1, 0, "SpeedyImage");
    }
};

#include "plugin.moc"
