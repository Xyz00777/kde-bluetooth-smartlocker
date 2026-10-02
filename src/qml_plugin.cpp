#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QQmlExtensionPlugin>
#include <qqml.h>

class SmartLockerClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QStringList devices READ devices NOTIFY devicesChanged)

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
    [[nodiscard]] QStringList devices() const { return devices_; }

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
        if (reply.isValid() && devices_ != reply.value()) {
            devices_ = reply.value();
            emit devicesChanged();
        }
    }

    Q_INVOKABLE void setEnabled(const bool enabled) {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        const QDBusPendingCall pending = daemon.asyncCall("SetEnabled", enabled);
        auto* watcher = new QDBusPendingCallWatcher{pending, this};
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* callWatcher) {
            callWatcher->deleteLater();
            refresh();
        });
    }

    Q_INVOKABLE void snooze(const int seconds) {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        const QDBusPendingCall pending = daemon.asyncCall("Snooze", seconds);
        auto* watcher = new QDBusPendingCallWatcher{pending, this};
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* callWatcher) {
            callWatcher->deleteLater();
            refresh();
        });
    }

    Q_INVOKABLE int snoozeSeconds() {
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        daemon.setTimeout(2000);
        const QDBusReply<int> reply = daemon.call("SnoozeSeconds");
        return reply.isValid() ? reply.value() : 30;
    }

    Q_INVOKABLE bool deviceEnabled(const QString& path) {
        const auto it = enabledCache_.constFind(path);
        if (it != enabledCache_.cend()) {
            return it.value();
        }
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        daemon.setTimeout(2000);
        const QDBusReply<bool> reply = daemon.call("DeviceEnabled", path);
        if (reply.isValid()) {
            enabledCache_.insert(path, reply.value());
            return reply.value();
        }
        return false;
    }

    Q_INVOKABLE void setDeviceEnabled(const QString& path, const bool enabled) {
        enabledCache_.insert(path, enabled);
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        const QDBusPendingCall pending = daemon.asyncCall("SetDeviceEnabled", path, enabled);
        auto* watcher = new QDBusPendingCallWatcher{pending, this};
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* callWatcher) {
            callWatcher->deleteLater();
            refresh();
        });
    }

    Q_INVOKABLE int deviceRssiThreshold(const QString& path) {
        const auto it = thresholdCache_.constFind(path);
        if (it != thresholdCache_.cend()) {
            return it.value();
        }
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        daemon.setTimeout(2000);
        const QDBusReply<int> reply = daemon.call("DeviceRssiThreshold", path);
        if (reply.isValid()) {
            thresholdCache_.insert(path, reply.value());
            return reply.value();
        }
        return -70;
    }

    Q_INVOKABLE void setDeviceRssiThreshold(const QString& path, const int thresholdDbm) {
        thresholdCache_.insert(path, thresholdDbm);
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        const QDBusPendingCall pending = daemon.asyncCall("SetDeviceRssiThreshold", path, thresholdDbm);
        auto* watcher = new QDBusPendingCallWatcher{pending, this};
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher* callWatcher) {
            callWatcher->deleteLater();
            refresh();
        });
    }

    Q_INVOKABLE QString deviceName(const QString& path) {
        const auto it = nameCache_.constFind(path);
        if (it != nameCache_.cend()) {
            return it.value();
        }
        QDBusInterface daemon{"org.kde.SmartLocker1", "/SmartLocker", "org.kde.SmartLocker1", QDBusConnection::sessionBus()};
        daemon.setTimeout(2000);
        const QDBusReply<QString> reply = daemon.call("DeviceName", path);
        if (reply.isValid()) {
            nameCache_.insert(path, reply.value());
            return reply.value();
        }
        return QString{};
    }

signals:
    void stateChanged();
    void devicesChanged();
    void settingsChanged();

private slots:
    void onDaemonStateChanged(const QString& state) {
        if (state_ != state) {
            state_ = state;
            emit stateChanged();
        }
    }

    void onDaemonSettingsChanged() {
        enabledCache_.clear();
        thresholdCache_.clear();
        nameCache_.clear();
        refreshDevices();
        emit settingsChanged();
    }

    void onServiceRegistered(const QString&) {
        enabledCache_.clear();
        thresholdCache_.clear();
        nameCache_.clear();
        refresh();
    }

    void onServiceUnregistered(const QString&) {
        enabledCache_.clear();
        thresholdCache_.clear();
        nameCache_.clear();
        if (state_ != "unavailable") {
            state_ = "unavailable";
            emit stateChanged();
        }
        if (!devices_.isEmpty()) {
            devices_.clear();
            emit devicesChanged();
        }
    }

private:
    QDBusServiceWatcher serviceWatcher_;
    QString state_{"unavailable"};
    QStringList devices_{};
    QMap<QString, bool> enabledCache_{};
    QMap<QString, int> thresholdCache_{};
    QMap<QString, QString> nameCache_{};
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
