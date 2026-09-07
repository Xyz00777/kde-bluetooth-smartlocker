#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QLoggingCategory>
#include <QRegularExpression>

#include "smartlocker/daemon.hpp"

Q_LOGGING_CATEGORY(smartLockerMainLog, "org.kde.smartlocker.main")

int main(int argc, char* argv[]) {
    QCoreApplication application{argc, argv};
    application.setApplicationName("kde-bluetooth-smartlocker");
    application.setApplicationVersion("0.1.1");

    QCommandLineParser parser;
    parser.setApplicationDescription("Lock-only Bluetooth presence daemon for KDE Plasma");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"device", "BlueZ device object path to watch.", "path"});
    parser.addOption({"away-seconds", "Absence duration before locking.", "seconds", "30"});
    parser.addOption({"snooze-seconds", "Maximum snooze duration.", "seconds", "30"});
    parser.addOption({"resume-grace-seconds", "Post-resume lock grace duration.", "seconds", "30"});
    parser.addOption({"minimum-present", "Required present devices for the policy.", "count", "1"});
    parser.addOption({"rssi-threshold", "Per-device RSSI threshold in dBm.", "dBm", "-70"});
    parser.addOption({"rssi-hysteresis", "RSSI hysteresis in dB.", "dB", "5"});
    parser.addOption({"rssi-samples", "RSSI averaging window size.", "count", "3"});
    parser.addOption({"prelock-notify", "Notify when the away countdown starts."});
    parser.addOption({"lock-command", "Command used to lock the session.", "command", "loginctl"});
    parser.process(application);

    const auto positive = [&parser](const QString& name) -> std::optional<int> {
        bool valid = false;
        const int value = parser.value(name).toInt(&valid);
        return valid && value > 0 ? std::optional{value} : std::nullopt;
    };
    const auto nonNegative = [&parser](const QString& name) -> std::optional<int> {
        bool valid = false;
        const int value = parser.value(name).toInt(&valid);
        return valid && value >= 0 ? std::optional{value} : std::nullopt;
    };
    const auto awaySeconds = nonNegative("away-seconds");
    const auto snoozeSeconds = positive("snooze-seconds");
    const auto resumeGraceSeconds = nonNegative("resume-grace-seconds");
    const auto minimumPresent = positive("minimum-present");
    const auto rssiHysteresis = positive("rssi-hysteresis");
    const auto rssiSamples = positive("rssi-samples");
    bool rssiValid = false;
    const int rssiThreshold = parser.value("rssi-threshold").toInt(&rssiValid);
    if (!awaySeconds.has_value() || !snoozeSeconds.has_value() || !resumeGraceSeconds.has_value() || !minimumPresent.has_value()
        || !rssiHysteresis.has_value() || !rssiSamples.has_value() || !rssiValid || rssiThreshold < -100 || rssiThreshold > 0) {
        return 2;
    }

    QStringList paths = parser.values("device");
    paths.append(qEnvironmentVariable("SMARTLOCKER_DEVICES").split(';', Qt::SkipEmptyParts));
    std::vector<smartlocker::DeviceConfiguration> devices;
    QSet<QString> watchedPaths;
    const auto validDevicePath = [](const QString& path) {
        return path.startsWith("/org/bluez/") && path.contains("/dev_") && !path.contains(QRegularExpression{"[\\s;]"});
    };
    for (const QString& path : paths) {
        if (!validDevicePath(path)) {
            qCCritical(smartLockerMainLog).noquote() << "invalid BlueZ device object path:" << path;
            return 2;
        }
        if (watchedPaths.contains(path)) {
            qCCritical(smartLockerMainLog).noquote() << "duplicate BlueZ device object path:" << path;
            return 2;
        }
        devices.push_back({smartlocker::DeviceId{path.toStdString()}, rssiThreshold, *rssiHysteresis,
                           static_cast<std::size_t>(*rssiSamples)});
        watchedPaths.insert(path);
    }
    if (!devices.empty() && static_cast<std::size_t>(*minimumPresent) > devices.size()) {
        qCCritical(smartLockerMainLog) << "minimum present devices cannot exceed configured device count";
        return 2;
    }
    smartlocker::Daemon daemon(
        {std::chrono::seconds{*awaySeconds}, std::chrono::seconds{*snoozeSeconds}, std::chrono::seconds{*resumeGraceSeconds},
         static_cast<std::size_t>(*minimumPresent), std::move(devices)},
        watchedPaths, parser.isSet("prelock-notify"), parser.value("lock-command"));
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService("org.kde.SmartLocker1") || !bus.registerObject("/SmartLocker", &daemon,
                                                                              QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals)) {
        return 1;
    }
    daemon.start();
    return application.exec();
}
