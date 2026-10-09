#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QLoggingCategory>

#include <iostream>

#include "smartlocker/daemon.hpp"
#include "smartlocker/device_spec.hpp"

Q_LOGGING_CATEGORY(smartLockerMainLog, "org.kde.smartlocker.main")

int main(int argc, char* argv[]) {
    QCoreApplication application{argc, argv};
    application.setApplicationName("kde-bluetooth-smartlocker");
    application.setApplicationVersion("0.15.0");

    QCommandLineParser parser;
    parser.setApplicationDescription("Lock-only Bluetooth presence daemon for desktop environments");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"device", "Bluetooth address or legacy BlueZ device object path to watch (repeatable, or set SMARTLOCKER_DEVICES).", "device"});
    parser.addOption({"away-seconds", "Absence duration before locking.", "seconds", "30"});
    parser.addOption({"snooze-seconds", "Maximum snooze duration.", "seconds", "30"});
    parser.addOption({"resume-grace-seconds", "Post-resume lock grace duration.", "seconds", "30"});
    parser.addOption({"minimum-present", "Required present devices for the policy.", "count", "1"});
    parser.addOption({"rssi-threshold", "Per-device RSSI threshold in dBm.", "dBm", "-70"});
    parser.addOption({"rssi-hysteresis", "RSSI hysteresis in dB.", "dB", "5"});
    parser.addOption({"rssi-samples", "RSSI averaging window size.", "count", "3"});
    parser.addOption({"prelock-notify", "Notify when the away countdown starts."});
    parser.addOption({"lock-command", "Command used to lock the session.", "command", "loginctl"});
    parser.addOption({"lock-when-bluetooth-off", "Lock once the away duration elapses even when the Bluetooth adapter is unavailable."});
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
    // Usage and startup errors must reach the terminal unconditionally, so they bypass the
    // optional Qt logging pipeline and go straight to stderr.
    const auto fail = [](const QString& message) {
        std::cerr << message.toStdString() << '\n';
    };
    const auto requirePositive = [&parser, &fail](std::initializer_list<const char*> names) -> bool {
        for (const char* name : names) {
            bool valid = false;
            const int value = parser.value(name).toInt(&valid);
            if (!valid || value <= 0) {
                fail(QStringLiteral("--%1 must be an integer greater than zero; got %2")
                         .arg(QString::fromLatin1(name), parser.value(name)));
                return false;
            }
        }
        return true;
    };
    const auto requireNonNegative = [&parser, &fail](std::initializer_list<const char*> names) -> bool {
        for (const char* name : names) {
            bool valid = false;
            const int value = parser.value(name).toInt(&valid);
            if (!valid || value < 0) {
                fail(QStringLiteral("--%1 must be a non-negative integer; got %2")
                         .arg(QString::fromLatin1(name), parser.value(name)));
                return false;
            }
        }
        return true;
    };
    if (!requireNonNegative({"away-seconds", "resume-grace-seconds"})
        || !requirePositive({"snooze-seconds", "minimum-present", "rssi-hysteresis", "rssi-samples"})) {
        return 2;
    }
    const auto awaySeconds = nonNegative("away-seconds");
    const auto snoozeSeconds = positive("snooze-seconds");
    const auto resumeGraceSeconds = nonNegative("resume-grace-seconds");
    const auto minimumPresent = positive("minimum-present");
    const auto rssiHysteresis = positive("rssi-hysteresis");
    const auto rssiSamples = positive("rssi-samples");
    bool rssiValid = false;
    const int rssiThreshold = parser.value("rssi-threshold").toInt(&rssiValid);
    if (!rssiValid) {
        fail(QStringLiteral("--rssi-threshold must be an integer; got %1").arg(parser.value("rssi-threshold")));
        return 2;
    }
    if (rssiThreshold < -100 || rssiThreshold > 0) {
        fail(QStringLiteral("--rssi-threshold must be within -100..0 dBm; got %1").arg(rssiThreshold));
        return 2;
    }

    QStringList specs = parser.values("device");
    specs.append(qEnvironmentVariable("SMARTLOCKER_DEVICES").split(';', Qt::SkipEmptyParts));
    std::vector<smartlocker::DeviceConfiguration> devices;
    QSet<QString> watchedMacs;
    for (const QString& rawSpec : specs) {
        const QString spec = rawSpec.trimmed();
        if (spec.isEmpty()) {
            continue;
        }
        const auto mac = smartlocker::normalizeDeviceSpec(spec.toStdString());
        if (!mac.has_value()) {
            fail(QStringLiteral("invalid Bluetooth device address or BlueZ device object path: %1").arg(spec));
            return 2;
        }
        const QString id = QString::fromStdString(*mac);
        if (watchedMacs.contains(id)) {
            fail(QStringLiteral("duplicate Bluetooth device address: %1").arg(id));
            return 2;
        }
        devices.push_back({smartlocker::DeviceId{*mac}, rssiThreshold, *rssiHysteresis,
                           static_cast<std::size_t>(*rssiSamples)});
        watchedMacs.insert(id);
    }
    if (!devices.empty() && static_cast<std::size_t>(*minimumPresent) > devices.size()) {
        fail(QStringLiteral("--minimum-present (%1) cannot exceed the configured device count (%2)")
                 .arg(*minimumPresent)
                 .arg(static_cast<int>(devices.size())));
        return 2;
    }
    smartlocker::Daemon daemon(
        {.awayDuration = std::chrono::seconds{*awaySeconds},
         .snoozeDurationCap = std::chrono::seconds{*snoozeSeconds},
         .postResumeGrace = std::chrono::seconds{*resumeGraceSeconds},
         .minimumPresentDevices = static_cast<std::size_t>(*minimumPresent),
         .devices = std::move(devices),
         .lockWhenBluetoothUnavailable = parser.isSet("lock-when-bluetooth-off")},
        watchedMacs, watchedMacs.isEmpty(), rssiThreshold, *rssiHysteresis, static_cast<std::size_t>(*rssiSamples),
        parser.isSet("prelock-notify"), parser.value("lock-command"));
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        fail(QStringLiteral("cannot connect to the session bus; is DBUS_SESSION_BUS_ADDRESS set?"));
        return 1;
    }
    if (!bus.registerObject("/SmartLocker", &daemon,
                            QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportScriptableSignals)) {
        fail(QStringLiteral("failed to export /SmartLocker: %1").arg(bus.lastError().message()));
        return 1;
    }
    daemon.start();
    if (!bus.registerService("org.kde.SmartLocker1")) {
        const QString reason = bus.lastError().message();
        fail(reason.isEmpty() ? QStringLiteral("failed to own org.kde.SmartLocker1; another instance is already running")
                              : QStringLiteral("failed to own org.kde.SmartLocker1: %1").arg(reason));
        bus.unregisterObject("/SmartLocker");
        return 1;
    }
    QObject::connect(&application, &QCoreApplication::aboutToQuit, [&bus] {
        bus.unregisterObject("/SmartLocker");
        bus.unregisterService("org.kde.SmartLocker1");
    });
    return application.exec();
}
