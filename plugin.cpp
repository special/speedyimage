#include <QQmlExtensionPlugin>

class SpeedyImagePlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)

public:
    void registerTypes(const char *uri)
    {
        Q_ASSERT(uri == QLatin1Literal("SpeedyImage"));
    }
};

#include "plugin.moc"
