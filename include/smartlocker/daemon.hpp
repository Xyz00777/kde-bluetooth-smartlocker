#pragma once

#include "smartlocker/bluez_monitor.hpp"
#include "smartlocker/state_machine.hpp"

#include <QSet>
#include <QProcess>
#include <QSettings>
#include <QTimer>

namespace smartlocker {

class Daemon final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.SmartLocker1")

public:
    Daemon(StateMachineConfiguration configuration, QSet<QString> watchedPaths, bool prelockNotifications, QObject* parent = nullptr);
    void start();

public slots:
    [[nodiscard]] QString State() const;
    [[nodiscard]] QStringList Devices() const;
    [[nodiscard]] int AwaySeconds() const;
    [[nodiscard]] int SnoozeSeconds() const;
    [[nodiscard]] int ResumeGraceSeconds() const;
    [[nodiscard]] int MinimumPresent() const;
    [[nodiscard]] bool DeviceEnabled(const QString& path) const;
    [[nodiscard]] int DeviceRssiThreshold(const QString& path) const;
    void SetEnabled(bool enabled);
    bool SetDeviceEnabled(const QString& path, bool enabled);
    bool SetDeviceRssiThreshold(const QString& path, int thresholdDbm);
    bool Snooze(int seconds);

signals:
    void StateChanged(const QString& state);

private slots:
    void onAvailabilityChanged(bool available);
    void onDeviceObserved(const QString& path, bool connected, int rssiDbm, bool hasRssi);
    void onPrepareForSleep(bool sleeping);
    void onLockProcessError(QProcess::ProcessError error);
    void onLockProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void advance();

private:
    void publishState();

    StateMachine machine_;
    BluezMonitor monitor_;
    QSet<QString> watchedPaths_;
    QTimer timer_;
    QProcess lockProcess_;
    QSettings settings_{"kde-bluetooth-smartlocker", "daemon"};
    bool prelockNotifications_;
    QString previousState_;
};

}
