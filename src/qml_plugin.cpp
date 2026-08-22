#include <QDBusInterface>
#include <QDBusReply>
#include <QQmlExtensionPlugin>
#include <qqml.h>

class SmartLockerClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)

public:
    explicit SmartLockerClient(QObject* parent = nullptr) : QObject(parent) {
        QDBusConnection::sessionBus().connect("org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", "StateChanged", this,
                                              SLOT(onDaemonStateChanged(QString)));
        refresh();
    }

    [[nodiscard]] QString state() const { return state_; }

    Q_INVOKABLE void refresh() {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        const QDBusReply<QString> reply = daemon.call("State");
        if (reply.isValid() && state_ != reply.value()) {
            state_ = reply.value();
            emit stateChanged();
        }
    }

    Q_INVOKABLE void setEnabled(const bool enabled) {
        QDBusInterface{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()}.call("SetEnabled", enabled);
        refresh();
    }

    Q_INVOKABLE void snooze(const int seconds) {
        QDBusInterface{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()}.call("Snooze", seconds);
        refresh();
    }

    Q_INVOKABLE bool deviceEnabled(const QString& path) {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        return QDBusReply<bool>{daemon.call("DeviceEnabled", path)}.value();
    }

    Q_INVOKABLE QStringList devices() {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        return QDBusReply<QStringList>{daemon.call("Devices")}.value();
    }

    Q_INVOKABLE void setDeviceEnabled(const QString& path, const bool enabled) {
        QDBusInterface{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()}
            .call("SetDeviceEnabled", path, enabled);
        refresh();
    }

    Q_INVOKABLE int deviceRssiThreshold(const QString& path) {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        return QDBusReply<int>{daemon.call("DeviceRssiThreshold", path)}.value();
    }

    Q_INVOKABLE void setDeviceRssiThreshold(const QString& path, const int thresholdDbm) {
        QDBusInterface{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()}
            .call("SetDeviceRssiThreshold", path, thresholdDbm);
        refresh();
    }

signals:
    void stateChanged();

private slots:
    void onDaemonStateChanged(const QString& state) {
        if (state_ != state) {
            state_ = state;
            emit stateChanged();
        }
    }

private:
    QString state_{"unavailable"};
};

class SmartLockerPlugin final : public QQmlExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)

public:
    void registerTypes(const char* uri) override {
        qmlRegisterType<SmartLockerClient>(uri, 1, 0, "SmartLockerClient");
    }
};

#include "qml_plugin.moc"
