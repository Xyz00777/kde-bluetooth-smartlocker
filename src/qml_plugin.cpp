#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusServiceWatcher>
#include <QQmlExtensionPlugin>
#include <qqml.h>

#include <utility>

namespace {

constexpr int kCallTimeoutMs = 2000;
constexpr const char* kService = "org.kde.SmartLocker1";
constexpr const char* kPath = "/SmartLocker";

// QDBusInterface is neither copyable nor movable, so it cannot be returned by value;
// every call site either builds one in place or goes through this async helper.
template <typename... Args>
QDBusPendingCall asyncDaemonCall(const QString& method, Args&&... args) {
    QDBusInterface daemon{QLatin1StringView{kService}, QLatin1StringView{kPath}, QLatin1StringView{kService}, QDBusConnection::sessionBus()};
    daemon.setTimeout(kCallTimeoutMs);
    return daemon.asyncCall(method, std::forward<Args>(args)...);
}

} // namespace

class SmartLockerClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QStringList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(int snoozeSeconds READ snoozeSeconds NOTIFY settingsChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)

public:
    explicit SmartLockerClient(QObject* parent = nullptr)
        : QObject(parent),
          serviceWatcher_(QString::fromLatin1(kService), QDBusConnection::sessionBus(),
                          QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this) {
        connect(&serviceWatcher_, &QDBusServiceWatcher::serviceRegistered, this, &SmartLockerClient::onServiceRegistered);
        connect(&serviceWatcher_, &QDBusServiceWatcher::serviceUnregistered, this, &SmartLockerClient::onServiceUnregistered);
        QDBusConnection::sessionBus().connect(kService, kPath, kService, "StateChanged", this, SLOT(onDaemonStateChanged(QString)));
        QDBusConnection::sessionBus().connect(kService, kPath, kService, "SettingsChanged", this, SLOT(onDaemonSettingsChanged()));
        refresh();
    }

    [[nodiscard]] QString state() const { return state_; }
    [[nodiscard]] QStringList devices() const { return devices_; }
    [[nodiscard]] int snoozeSeconds() const { return snoozeSeconds_; }
    [[nodiscard]] QString lastError() const { return lastError_; }

    Q_INVOKABLE void clearError() { setLastError(QString{}); }

    Q_INVOKABLE void refresh() {
        const quint64 generation = ++refreshGeneration_;

        watchReply<QString>(asyncDaemonCall(QStringLiteral("State")), [this, generation](const QDBusPendingReply<QString>& reply) {
            if (generation != refreshGeneration_) {
                return;
            }
            const QString next = reply.isValid() ? reply.value() : QStringLiteral("unavailable");
            if (state_ != next) {
                state_ = next;
                emit stateChanged();
            }
        });

        watchReply<int>(asyncDaemonCall(QStringLiteral("SnoozeSeconds")), [this, generation](const QDBusPendingReply<int>& reply) {
            if (generation != refreshGeneration_ || !reply.isValid()) {
                return;
            }
            if (snoozeSeconds_ != reply.value()) {
                snoozeSeconds_ = reply.value();
                emit settingsChanged();
            }
        });

        refreshDevices(generation);
    }

    Q_INVOKABLE void refreshDevices() { refreshDevices(++refreshGeneration_); }

    Q_INVOKABLE void setEnabled(const bool enabled) { watchMutation(asyncDaemonCall(QStringLiteral("SetEnabled"), enabled)); }

    Q_INVOKABLE void snooze(const int seconds) { watchMutation(asyncDaemonCall(QStringLiteral("Snooze"), seconds)); }

    Q_INVOKABLE void setDeviceEnabled(const QString& path, const bool enabled) {
        enabledCache_.insert(path, enabled);
        watchMutation(asyncDaemonCall(QStringLiteral("SetDeviceEnabled"), path, enabled), [this, path] { enabledCache_.remove(path); });
    }

    Q_INVOKABLE void setDeviceRssiThreshold(const QString& path, const int thresholdDbm) {
        thresholdCache_.insert(path, thresholdDbm);
        watchMutation(asyncDaemonCall(QStringLiteral("SetDeviceRssiThreshold"), path, thresholdDbm),
                      [this, path] { thresholdCache_.remove(path); });
    }

    Q_INVOKABLE bool deviceEnabled(const QString& path) {
        const auto it = enabledCache_.constFind(path);
        if (it != enabledCache_.cend()) {
            return it.value();
        }
        QDBusInterface daemon{QLatin1StringView{kService}, QLatin1StringView{kPath}, QLatin1StringView{kService}, QDBusConnection::sessionBus()};
        daemon.setTimeout(kCallTimeoutMs);
        const QDBusReply<bool> reply = daemon.call(QStringLiteral("DeviceEnabled"), path);
        if (reply.isValid()) {
            enabledCache_.insert(path, reply.value());
            return reply.value();
        }
        return false;
    }

    Q_INVOKABLE int deviceRssiThreshold(const QString& path) {
        const auto it = thresholdCache_.constFind(path);
        if (it != thresholdCache_.cend()) {
            return it.value();
        }
        QDBusInterface daemon{QLatin1StringView{kService}, QLatin1StringView{kPath}, QLatin1StringView{kService}, QDBusConnection::sessionBus()};
        daemon.setTimeout(kCallTimeoutMs);
        const QDBusReply<int> reply = daemon.call(QStringLiteral("DeviceRssiThreshold"), path);
        if (reply.isValid()) {
            thresholdCache_.insert(path, reply.value());
            return reply.value();
        }
        return -70;
    }

    Q_INVOKABLE QString deviceName(const QString& path) {
        const auto it = nameCache_.constFind(path);
        if (it != nameCache_.cend()) {
            return it.value();
        }
        QDBusInterface daemon{QLatin1StringView{kService}, QLatin1StringView{kPath}, QLatin1StringView{kService}, QDBusConnection::sessionBus()};
        daemon.setTimeout(kCallTimeoutMs);
        const QDBusReply<QString> reply = daemon.call(QStringLiteral("DeviceName"), path);
        if (reply.isValid() && !reply.value().isEmpty()) {
            nameCache_.insert(path, reply.value());
            return reply.value();
        }
        return QString{};
    }

signals:
    void stateChanged();
    void devicesChanged();
    void settingsChanged();
    void errorOccurred();

private:
    template <typename Reply, typename Handler>
    void watchReply(const QDBusPendingCall& pending, Handler&& handler) {
        auto* watcher = new QDBusPendingCallWatcher{pending, this};
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, handler = std::forward<Handler>(handler)]() mutable {
            const QDBusPendingReply<Reply> reply = *watcher;
            watcher->deleteLater();
            handler(reply);
        });
    }

    template <typename OnRejected>
    void watchMutation(const QDBusPendingCall& pending, OnRejected&& onRejected) {
        auto* watcher = new QDBusPendingCallWatcher{pending, this};
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, onRejected = std::forward<OnRejected>(onRejected)]() {
            // Every mutator returns bool, and false is a normal policy rejection (locked
            // session, unwatched device, out-of-range value). That is not a D-Bus error,
            // so the reply value has to be inspected as well.
            const QDBusPendingReply<bool> reply = *watcher;
            const bool failed = reply.isError() || !reply.value();
            const QString reason =
                reply.isError() ? reply.error().message() : tr("the change was refused (the session may be locked)");
            watcher->deleteLater();
            if (failed) {
                // Drop the optimistic entry so the next read re-fetches the value the daemon
                // actually holds, instead of showing a setting that was never applied.
                onRejected();
                setLastError(tr("The daemon rejected the change: %1").arg(reason));
            } else {
                setLastError(QString{});
            }
            refresh();
        });
    }

    void watchMutation(const QDBusPendingCall& pending) { watchMutation(pending, [] {}); }

    void refreshDevices(const quint64 generation) {
        watchReply<QStringList>(asyncDaemonCall(QStringLiteral("Devices")), [this, generation](const QDBusPendingReply<QStringList>& reply) {
            if (generation != refreshGeneration_ || !reply.isValid()) {
                return;
            }
            if (devices_ != reply.value()) {
                devices_ = reply.value();
                emit devicesChanged();
            }
        });

        watchReply<QVariantMap>(asyncDaemonCall(QStringLiteral("DeviceSettings")), [this, generation](const QDBusPendingReply<QVariantMap>& reply) {
            if (generation != refreshGeneration_ || !reply.isValid()) {
                return;
            }
            // Priming the per-device caches here keeps the QML getters off the
            // synchronous D-Bus path, which would otherwise block the UI thread.
            enabledCache_.clear();
            thresholdCache_.clear();
            nameCache_.clear();
            for (auto it = reply.value().cbegin(); it != reply.value().cend(); ++it) {
                const QVariantMap entry = it.value().toMap();
                enabledCache_.insert(it.key(), entry.value(QStringLiteral("enabled")).toBool());
                thresholdCache_.insert(it.key(), entry.value(QStringLiteral("rssiThreshold")).toInt());
                const QString name = entry.value(QStringLiteral("name")).toString();
                if (!name.isEmpty()) {
                    nameCache_.insert(it.key(), name);
                }
            }
            // The cache contents just changed, so delegates holding device names, toggles
            // and thresholds must re-read them or they would display stale values.
            emit settingsChanged();
        });
    }

    void setLastError(const QString& message) {
        if (lastError_ == message) {
            return;
        }
        lastError_ = message;
        emit errorOccurred();
    }

    void clearCaches() {
        enabledCache_.clear();
        thresholdCache_.clear();
        nameCache_.clear();
    }

private slots:
    void onDaemonStateChanged(const QString& state) {
        if (state_ != state) {
            state_ = state;
            emit stateChanged();
        }
    }

    void onDaemonSettingsChanged() {
        // Clearing first keeps a delegate from displaying a value the daemon has just
        // changed; refresh() re-primes every cache in one asynchronous round trip.
        clearCaches();
        refresh();
    }

    void onServiceRegistered(const QString&) {
        clearCaches();
        refresh();
    }

    void onServiceUnregistered(const QString&) {
        clearCaches();
        setLastError(tr("The daemon is not running."));
        if (state_ != QStringLiteral("unavailable")) {
            state_ = QStringLiteral("unavailable");
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
    int snoozeSeconds_{30};
    QString lastError_{};
    quint64 refreshGeneration_{0};
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
