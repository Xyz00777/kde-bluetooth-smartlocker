#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QLoggingCategory>

#include "smartlocker/daemon.hpp"
#include "smartlocker/device_spec.hpp"

Q_LOGGING_CATEGORY(smartLockerMainLog, "org.kde.smartlocker.main")

int main(int argc, char* argv[]) {
    QCoreApplication application{argc, argv};
    application.setApplicationName("kde-bluetooth-smartlocker");
    application.setApplicationVersion("0.2.1");

    QCommandLineParser parser;
    parser.setApplicationDescription("Lock-only Bluetooth presence daemon for KDE Plasma");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"device", "Bluetooth address or legacy BlueZ device object path to watch (repeatable).", "device"});
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

    QStringList specs = parser.values("device");
    specs.append(qEnvironmentVariable("SMARTLOCKER_DEVICES").split(';', Qt::SkipEmptyParts));
    std::vector<smartlocker::DeviceConfiguration> devices;
    QSet<QString> watchedMacs;
    for (const QString& spec : specs) {
        const auto mac = smartlocker::normalizeDeviceSpec(spec.toStdString());
        if (!mac.has_value()) {
            qCCritical(smartLockerMainLog).noquote() << "invalid Bluetooth device address or BlueZ device object path:" << spec;
            return 2;
        }
        const QString id = QString::fromStdString(*mac);
        if (watchedMacs.contains(id)) {
            qCCritical(smartLockerMainLog).noquote() << "duplicate Bluetooth device address:" << id;
            return 2;
        }
        devices.push_back({smartlocker::DeviceId{*mac}, rssiThreshold, *rssiHysteresis,
                           static_cast<std::size_t>(*rssiSamples)});
        watchedMacs.insert(id);
    }
    if (!devices.empty() && static_cast<std::size_t>(*minimumPresent) > devices.size()) {
        qCCritical(smartLockerMainLog) << "minimum present devices cannot exceed configured device count";
        return 2;
    }
    smartlocker::Daemon daemon(
        {std::chrono::seconds{*awaySeconds}, std::chrono::seconds{*snoozeSeconds}, std::chrono::seconds{*resumeGraceSeconds},
         static_cast<std::size_t>(*minimumPresent), std::move(devices)},
        watchedMacs, watchedMacs.isEmpty(), rssiThreshold, *rssiHysteresis, static_cast<std::size_t>(*rssiSamples),
        parser.isSet("prelock-notify"), parser.value("lock-command"));
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService("org.kde.SmartLocker1") || !bus.registerObject("/SmartLocker", &daemon,
                                                                              QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals)) {
        return 1;
    }
    daemon.start();
    return application.exec();
}
