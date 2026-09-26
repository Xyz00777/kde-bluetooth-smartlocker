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
    Daemon(StateMachineConfiguration configuration, QSet<QString> watchedMacs, bool autoSelect, int rssiThreshold, int rssiHysteresis,
           std::size_t rssiSamples, bool prelockNotifications,
           QString lockCommand = QStringLiteral("loginctl"), QObject* parent = nullptr);
    void start();

public slots:
    Q_SCRIPTABLE [[nodiscard]] QString State() const;
    Q_SCRIPTABLE [[nodiscard]] QStringList Devices() const;
    Q_SCRIPTABLE [[nodiscard]] int AwaySeconds() const;
    Q_SCRIPTABLE [[nodiscard]] int SnoozeSeconds() const;
    Q_SCRIPTABLE [[nodiscard]] int ResumeGraceSeconds() const;
    Q_SCRIPTABLE [[nodiscard]] int MinimumPresent() const;
    Q_SCRIPTABLE [[nodiscard]] bool DeviceEnabled(const QString& path) const;
    Q_SCRIPTABLE [[nodiscard]] int DeviceRssiThreshold(const QString& path) const;
    Q_SCRIPTABLE bool SetEnabled(bool enabled);
    Q_SCRIPTABLE bool SetDeviceEnabled(const QString& path, bool enabled);
    Q_SCRIPTABLE bool SetDeviceRssiThreshold(const QString& path, int thresholdDbm);
    Q_SCRIPTABLE bool Snooze(int seconds);

signals:
    Q_SCRIPTABLE void StateChanged(const QString& state);
    Q_SCRIPTABLE void SettingsChanged();

private slots:
    void onAvailabilityChanged(bool available);
    void onDeviceObserved(const QString& path, bool connected, int rssiDbm, bool hasRssi);
    void onSelectedDevicesChanged(const QStringList& macs);
    void onPrepareForSleep(bool sleeping);
    void onLockProcessError(QProcess::ProcessError error);
    void onLockProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void verifyLockApplied();
    void advance();

private:
    void publishState();

    StateMachine machine_;
    BluezMonitor monitor_;
    QSet<QString> watchedMacs_;
    bool autoSelect_;
    int rssiThreshold_{-70};
    int rssiHysteresis_{5};
    std::size_t rssiSamples_{3};
    QTimer timer_;
    QTimer verifyTimer_;
    QProcess lockProcess_;
    QSettings settings_{"kde-bluetooth-smartlocker", "daemon"};
    bool prelockNotifications_;
    bool started_{false};
    QString lockCommand_;
    QString previousState_;
};

}
