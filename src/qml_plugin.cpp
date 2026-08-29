#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QQmlExtensionPlugin>
#include <qqml.h>

class SmartLockerClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)

public:
    explicit SmartLockerClient(QObject* parent = nullptr)
        : QObject(parent),
          serviceWatcher_("org.kde.SmartLocker1", QDBusConnection::sessionBus(),
                          QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this) {
        connect(&serviceWatcher_, &QDBusServiceWatcher::serviceRegistered, this, &SmartLockerClient::onServiceRegistered);
        connect(&serviceWatcher_, &QDBusServiceWatcher::serviceUnregistered, this, &SmartLockerClient::onServiceUnregistered);
        QDBusConnection::sessionBus().connect("org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", "StateChanged", this,
                                              SLOT(onDaemonStateChanged(QString)));
        refresh();
    }

    [[nodiscard]] QString state() const { return state_; }

    Q_INVOKABLE void refresh() {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        const QDBusReply<QString> reply = daemon.call("State");
        const QString newState = reply.isValid() ? reply.value() : QStringLiteral("unavailable");
        if (state_ != newState) {
            state_ = newState;
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
        const QDBusReply<bool> reply = daemon.call("DeviceEnabled", path);
        return reply.isValid() ? reply.value() : false;
    }

    Q_INVOKABLE QStringList devices() {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        const QDBusReply<QStringList> reply = daemon.call("Devices");
        return reply.isValid() ? reply.value() : QStringList{};
    }

    Q_INVOKABLE void setDeviceEnabled(const QString& path, const bool enabled) {
        QDBusInterface{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()}
            .call("SetDeviceEnabled", path, enabled);
        refresh();
    }

    Q_INVOKABLE int deviceRssiThreshold(const QString& path) {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        const QDBusReply<int> reply = daemon.call("DeviceRssiThreshold", path);
        return reply.isValid() ? reply.value() : -70;
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

    void onServiceRegistered(const QString&) {
        refresh();
    }

    void onServiceUnregistered(const QString&) {
        if (state_ != "unavailable") {
            state_ = "unavailable";
            emit stateChanged();
        }
    }

private:
    QDBusServiceWatcher serviceWatcher_;
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
