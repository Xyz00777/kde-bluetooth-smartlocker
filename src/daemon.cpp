#include "smartlocker/daemon.hpp"

#include <QCoreApplication>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(smartLockerLog, "org.kde.smartlocker")

namespace smartlocker {

namespace {

TimePoint now() {
    return std::chrono::steady_clock::now();
}

bool sessionLocked() {
    QDBusInterface login1{"org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                          QDBusConnection::systemBus()};
    login1.setTimeout(2000);
    const QDBusReply<QDBusObjectPath> reply = login1.call("GetSessionByPID", static_cast<quint32>(QCoreApplication::applicationPid()));
    if (!reply.isValid()) {
        return false;
    }
    QDBusInterface session{"org.freedesktop.login1", reply.value().path(), "org.freedesktop.login1.Session",
                           QDBusConnection::systemBus()};
    session.setTimeout(2000);
    const QString state = session.property("State").toString();
    // logind reports "locking" during the transition and "locked" while the screen
    // is locked; mutating calls must be rejected in both, since the plasmoid is
    // unreachable behind the lock screen anyway.
    return state == "locking" || state == "locked";
}

QString stateName(const MachineState state) {
    switch (state) {
    case MachineState::Starting:
        return "starting";
    case MachineState::Monitoring:
        return "monitoring";
    case MachineState::AwaitingAbsence:
        return "away";
    case MachineState::Snoozed:
        return "snoozed";
    case MachineState::Disabled:
        return "disabled";
    case MachineState::Error:
        return "error";
    case MachineState::Locked:
        return "locked";
    }
    return "error";
}

QString deviceSettingsKey(const QString& mac, const char* name) {
    return QStringLiteral("devices/%1/%2").arg(mac, QString::fromLatin1(name));
}

}

Daemon::Daemon(StateMachineConfiguration configuration, QSet<QString> watchedMacs, const bool autoSelect, const int rssiThreshold,
               const int rssiHysteresis, const std::size_t rssiSamples, const bool prelockNotifications,
               QString lockCommand, QObject* parent)
    : QObject(parent), machine_(std::move(configuration)), monitor_(this), watchedMacs_(std::move(watchedMacs)), autoSelect_(autoSelect),
      rssiThreshold_(rssiThreshold), rssiHysteresis_(rssiHysteresis), rssiSamples_(rssiSamples), prelockNotifications_(prelockNotifications),
      lockCommand_(std::move(lockCommand)) {
    machine_.setEnabled(settings_.value("enabled", true).toBool(), now());
    for (const QString& path : watchedMacs_) {
        const DeviceId id{path.toStdString()};
        machine_.setDeviceEnabled(id, settings_.value(deviceSettingsKey(path, "enabled"), true).toBool(), now());
        int threshold = settings_.value(deviceSettingsKey(path, "rssiThreshold"), machine_.deviceRssiThreshold(id)).toInt();
        if (threshold < -100 || threshold > 0) {
            qCWarning(smartLockerLog) << "ignoring out-of-range persisted RSSI threshold for" << path << ":" << threshold;
            threshold = machine_.deviceRssiThreshold(id);
            settings_.setValue(deviceSettingsKey(path, "rssiThreshold"), threshold);
        }
        machine_.setDeviceRssiThreshold(id, threshold, now());
    }
    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, &Daemon::advance);
    verifyTimer_.setInterval(3000);
    connect(&verifyTimer_, &QTimer::timeout, this, &Daemon::verifyLockApplied);
    connect(&monitor_, &BluezMonitor::availabilityChanged, this, &Daemon::onAvailabilityChanged);
    connect(&monitor_, &BluezMonitor::deviceObserved, this, &Daemon::onDeviceObserved);
    connect(&monitor_, &BluezMonitor::selectedDevicesChanged, this, &Daemon::onSelectedDevicesChanged);
    connect(&lockProcess_, &QProcess::errorOccurred, this, &Daemon::onLockProcessError);
    connect(&lockProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &Daemon::onLockProcessFinished);
    const bool connected = QDBusConnection::systemBus().connect("org.freedesktop.login1", "/org/freedesktop/login1",
                                                                "org.freedesktop.login1.Manager", "PrepareForSleep", this,
                                                                SLOT(onPrepareForSleep(bool)));
    if (!connected) {
        qCWarning(smartLockerLog) << "failed to subscribe to PrepareForSleep; resume grace will not apply";
    }
}

void Daemon::start() {
    if (started_) {
        return;
    }
    started_ = true;
    monitor_.start(watchedMacs_, autoSelect_);
    timer_.start();
    verifyTimer_.start();
    publishState();
}

QString Daemon::State() const {
    return stateName(machine_.state());
}

QStringList Daemon::Devices() const {
    QStringList devices = watchedMacs_.values();
    devices.sort();
    return devices;
}

int Daemon::AwaySeconds() const {
    return static_cast<int>(machine_.awayDuration().count());
}

int Daemon::SnoozeSeconds() const {
    return static_cast<int>(machine_.snoozeDurationCap().count());
}

int Daemon::ResumeGraceSeconds() const {
    return static_cast<int>(machine_.postResumeGrace().count());
}

int Daemon::MinimumPresent() const {
    return static_cast<int>(machine_.minimumPresentDevices());
}

bool Daemon::DeviceEnabled(const QString& path) const {
    return watchedMacs_.contains(path) && machine_.deviceEnabled(DeviceId{path.toStdString()});
}

int Daemon::DeviceRssiThreshold(const QString& path) const {
    return watchedMacs_.contains(path) ? machine_.deviceRssiThreshold(DeviceId{path.toStdString()}) : -70;
}

bool Daemon::SetEnabled(const bool enabled) {
    if (sessionLocked()) {
        return false;
    }
    machine_.setEnabled(enabled, now());
    settings_.setValue("enabled", enabled);
    settings_.sync();
    publishState();
    emit SettingsChanged();
    return true;
}

bool Daemon::SetDeviceEnabled(const QString& path, const bool enabled) {
    if (sessionLocked()) {
        return false;
    }
    if (!watchedMacs_.contains(path)) {
        return false;
    }
    machine_.setDeviceEnabled(DeviceId{path.toStdString()}, enabled, now());
    settings_.setValue(deviceSettingsKey(path, "enabled"), enabled);
    settings_.sync();
    publishState();
    emit SettingsChanged();
    return true;
}

bool Daemon::SetDeviceRssiThreshold(const QString& path, const int thresholdDbm) {
    if (sessionLocked()) {
        return false;
    }
    if (!watchedMacs_.contains(path)) {
        return false;
    }
    if (thresholdDbm < -100 || thresholdDbm > 0) {
        return false;
    }
    machine_.setDeviceRssiThreshold(DeviceId{path.toStdString()}, thresholdDbm, now());
    settings_.setValue(deviceSettingsKey(path, "rssiThreshold"), thresholdDbm);
    settings_.sync();
    publishState();
    emit SettingsChanged();
    return true;
}

bool Daemon::Snooze(const int seconds) {
    if (sessionLocked()) {
        return false;
    }
    if (seconds <= 0 || seconds > machine_.snoozeDurationCap().count()) {
        return false;
    }
    machine_.snooze(std::chrono::seconds{seconds}, now());
    publishState();
    return true;
}

void Daemon::onAvailabilityChanged(const bool available) {
    machine_.setBluetoothAvailable(available, now());
    publishState();
}

void Daemon::onDeviceObserved(const QString& path, const bool connected, const int rssiDbm, const bool hasRssi) {
    if (!watchedMacs_.contains(path)) {
        return;
    }
    machine_.observe(DeviceId{path.toStdString()}, DeviceObservation{connected, hasRssi ? std::optional{rssiDbm} : std::nullopt}, now());
    publishState();
}

void Daemon::onSelectedDevicesChanged(const QStringList& macs) {
    const QSet<QString> selected{macs.cbegin(), macs.cend()};
    for (const QString& mac : selected - watchedMacs_) {
        watchedMacs_.insert(mac);
        const int savedThreshold = settings_.value(deviceSettingsKey(mac, "rssiThreshold"), rssiThreshold_).toInt();
        const int threshold = savedThreshold >= -100 && savedThreshold <= 0 ? savedThreshold : rssiThreshold_;
        machine_.addDevice({DeviceId{mac.toStdString()}, threshold, rssiHysteresis_, rssiSamples_,
                            settings_.value(deviceSettingsKey(mac, "enabled"), true).toBool()}, now());
    }
    const QSet<QString> removed = watchedMacs_ - selected;
    for (const QString& mac : removed) {
        if (autoSelect_) {
            machine_.removeDevice(DeviceId{mac.toStdString()}, now());
            watchedMacs_.remove(mac);
        }
    }
    emit SettingsChanged();
    publishState();
}

void Daemon::onPrepareForSleep(const bool sleeping) {
    if (!sleeping) {
        machine_.resume(now());
        publishState();
    }
}

void Daemon::onLockProcessError(const QProcess::ProcessError error) {
    qCWarning(smartLockerLog) << "lock command failed to start" << error << lockProcess_.errorString();
    machine_.clearLocked(now());
    publishState();
}

void Daemon::onLockProcessFinished(const int exitCode, const QProcess::ExitStatus exitStatus) {
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        qCWarning(smartLockerLog) << "lock command exited unsuccessfully" << exitCode << exitStatus;
        machine_.clearLocked(now());
        publishState();
    }
}

void Daemon::advance() {
    if (machine_.advanceTo(now()) == Action::Lock && lockProcess_.state() == QProcess::NotRunning) {
        qCInfo(smartLockerLog) << "requesting session lock";
        lockProcess_.start(lockCommand_, {"lock-session"});
    }
    publishState();
}

void Daemon::verifyLockApplied() {
    if (machine_.state() != MachineState::Locked) {
        return;
    }
    if (sessionLocked()) {
        return;
    }
    if (lockProcess_.state() == QProcess::NotRunning) {
        qCWarning(smartLockerLog) << "lock command succeeded but session is not locked; retrying";
        machine_.clearLocked(now());
        publishState();
    }
}

void Daemon::publishState() {
    const QString currentState = State();
    if (prelockNotifications_ && currentState == "away" && previousState_ != "away") {
        QDBusInterface notification{"org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
                                    QDBusConnection::sessionBus()};
        notification.asyncCall("Notify", "Bluetooth SmartLocker", 0U, "", "Bluetooth device away",
                               "Screen will lock after the away duration.", QStringList{}, QVariantMap{}, -1);
    }
    if (currentState != previousState_) {
        previousState_ = currentState;
        emit StateChanged(currentState);
    }
}

}
