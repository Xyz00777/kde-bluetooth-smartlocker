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
        QDBusConnection::sessionBus().connect("org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", "SettingsChanged", this,
                                              SLOT(onDaemonSettingsChanged()));
        refresh();
    }

    [[nodiscard]] QString state() const { return state_; }

    Q_INVOKABLE void refresh() {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        daemon.setTimeout(2000);
        const QDBusReply<QString> reply = daemon.call("State");
        const QString newState = reply.isValid() ? reply.value() : QStringLiteral("unavailable");
        if (state_ != newState) {
            state_ = newState;
            emit stateChanged();
        }
        refreshDevices();
        emit settingsChanged();
    }

    Q_INVOKABLE void refreshDevices() {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        daemon.setTimeout(2000);
        const QDBusReply<QStringList> reply = daemon.call("Devices");
        if (reply.isValid()) {
            devices_ = reply.value();
        }
    }

    Q_INVOKABLE bool setEnabled(const bool enabled) {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        daemon.setTimeout(2000);
        const QDBusReply<bool> reply = daemon.call("SetEnabled", enabled);
        refresh();
        return reply.isValid() ? reply.value() : false;
    }

    Q_INVOKABLE void snooze(const int seconds) {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        daemon.setTimeout(2000);
        daemon.call("Snooze", seconds);
        refresh();
    }

    Q_INVOKABLE int snoozeSeconds() {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        daemon.setTimeout(2000);
        const QDBusReply<int> reply = daemon.call("SnoozeSeconds");
        return reply.isValid() ? reply.value() : 30;
    }

    Q_INVOKABLE bool deviceEnabled(const QString& path) {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        daemon.setTimeout(2000);
        const QDBusReply<bool> reply = daemon.call("DeviceEnabled", path);
        return reply.isValid() ? reply.value() : false;
    }

    Q_INVOKABLE QStringList devices() {
        return devices_;
    }

    Q_INVOKABLE void setDeviceEnabled(const QString& path, const bool enabled) {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        daemon.setTimeout(2000);
        daemon.call("SetDeviceEnabled", path, enabled);
        refresh();
    }

    Q_INVOKABLE int deviceRssiThreshold(const QString& path) {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        daemon.setTimeout(2000);
        const QDBusReply<int> reply = daemon.call("DeviceRssiThreshold", path);
        return reply.isValid() ? reply.value() : -70;
    }

    Q_INVOKABLE void setDeviceRssiThreshold(const QString& path, const int thresholdDbm) {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        daemon.setTimeout(2000);
        daemon.call("SetDeviceRssiThreshold", path, thresholdDbm);
        refresh();
    }

signals:
    void stateChanged();
    void settingsChanged();

private slots:
    void onDaemonStateChanged(const QString& state) {
        if (state_ != state) {
            state_ = state;
            emit stateChanged();
        }
    }

    void onDaemonSettingsChanged() {
        refreshDevices();
        emit settingsChanged();
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
    QStringList devices_{};
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
