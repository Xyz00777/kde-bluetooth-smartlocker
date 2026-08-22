#include "smartlocker/daemon.hpp"

#include <QDBusInterface>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(smartLockerLog, "org.kde.smartlocker")

namespace smartlocker {

namespace {

TimePoint now() {
    return std::chrono::steady_clock::now();
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

}

Daemon::Daemon(StateMachineConfiguration configuration, QSet<QString> watchedPaths, const bool prelockNotifications, QObject* parent)
    : QObject(parent), machine_(std::move(configuration)), monitor_(this), watchedPaths_(std::move(watchedPaths)),
      prelockNotifications_(prelockNotifications) {
    machine_.setEnabled(settings_.value("enabled", true).toBool(), now());
    for (const QString& path : watchedPaths_) {
        machine_.setDeviceEnabled(DeviceId{path.toStdString()}, settings_.value(QStringLiteral("devices/%1/enabled").arg(path), true).toBool(), now());
        const int threshold = settings_.value(QStringLiteral("devices/%1/rssiThreshold").arg(path), machine_.deviceRssiThreshold(DeviceId{path.toStdString()})).toInt();
        machine_.setDeviceRssiThreshold(DeviceId{path.toStdString()}, threshold, now());
    }
    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, &Daemon::advance);
    connect(&monitor_, &BluezMonitor::availabilityChanged, this, &Daemon::onAvailabilityChanged);
    connect(&monitor_, &BluezMonitor::deviceObserved, this, &Daemon::onDeviceObserved);
    connect(&lockProcess_, &QProcess::errorOccurred, this, &Daemon::onLockProcessError);
    connect(&lockProcess_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, &Daemon::onLockProcessFinished);
    QDBusConnection::systemBus().connect("org.freedesktop.login1", "/org/freedesktop/login1", "org.freedesktop.login1.Manager",
                                         "PrepareForSleep", this, SLOT(onPrepareForSleep(bool)));
}

void Daemon::start() {
    monitor_.start(watchedPaths_);
    timer_.start();
    publishState();
}

QString Daemon::State() const {
    return stateName(machine_.state());
}

QStringList Daemon::Devices() const {
    return watchedPaths_.values();
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
    return watchedPaths_.contains(path) && machine_.deviceEnabled(DeviceId{path.toStdString()});
}

int Daemon::DeviceRssiThreshold(const QString& path) const {
    return watchedPaths_.contains(path) ? machine_.deviceRssiThreshold(DeviceId{path.toStdString()}) : 0;
}

void Daemon::SetEnabled(const bool enabled) {
    machine_.setEnabled(enabled, now());
    settings_.setValue("enabled", enabled);
    settings_.sync();
    publishState();
}

bool Daemon::SetDeviceEnabled(const QString& path, const bool enabled) {
    if (!watchedPaths_.contains(path)) {
        return false;
    }
    machine_.setDeviceEnabled(DeviceId{path.toStdString()}, enabled, now());
    settings_.setValue(QStringLiteral("devices/%1/enabled").arg(path), enabled);
    settings_.sync();
    publishState();
    return true;
}

bool Daemon::SetDeviceRssiThreshold(const QString& path, const int thresholdDbm) {
    if (!watchedPaths_.contains(path)) {
        return false;
    }
    machine_.setDeviceRssiThreshold(DeviceId{path.toStdString()}, thresholdDbm, now());
    settings_.setValue(QStringLiteral("devices/%1/rssiThreshold").arg(path), thresholdDbm);
    settings_.sync();
    publishState();
    return true;
}

bool Daemon::Snooze(const int seconds) {
    if (seconds <= 0) {
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
    if (!watchedPaths_.contains(path)) {
        return;
    }
    machine_.observe(DeviceId{path.toStdString()}, DeviceObservation{connected, hasRssi ? std::optional{rssiDbm} : std::nullopt}, now());
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
}

void Daemon::onLockProcessFinished(const int exitCode, const QProcess::ExitStatus exitStatus) {
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        qCWarning(smartLockerLog) << "lock command exited unsuccessfully" << exitCode << exitStatus;
    }
}

void Daemon::advance() {
    if (machine_.advanceTo(now()) == Action::Lock && lockProcess_.state() == QProcess::NotRunning) {
        qCInfo(smartLockerLog) << "requesting session lock";
        lockProcess_.start("loginctl", {"lock-session"});
    }
    publishState();
}

void Daemon::publishState() {
    const QString currentState = State();
    if (prelockNotifications_ && currentState == "away" && previousState_ != "away") {
        QDBusInterface notification{"org.freedesktop.Notifications", "/org/freedesktop/Notifications", "org.freedesktop.Notifications",
                                    QDBusConnection::sessionBus()};
        notification.asyncCall("Notify", "Bluetooth SmartLocker", 0U, "", "Bluetooth device away",
                               "Screen will lock after the away duration.", QStringList{}, QVariantMap{}, -1);
    }
    previousState_ = currentState;
    emit StateChanged(currentState);
}

}
