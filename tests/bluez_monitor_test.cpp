#include "smartlocker/bluez_monitor.hpp"

#include "smartlocker/bluez_types.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QTemporaryFile>
#include <QSignalSpy>
#include <QThread>

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace {

int failures = 0;

void check(const bool condition, const char* what) {
    if (!condition) {
        std::printf("FAIL: %s\n", what);
        ++failures;
    }
}

void spinFor(const int milliseconds) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(2);
    }
}

bool waitForObservation(QSignalSpy& spy, const QString& mac, const bool connected) {
    for (int i = 0; i < 400; ++i) {
        for (const QList<QVariant>& call : spy) {
            if (call.at(0).toString() == mac && call.at(1).toBool() == connected) {
                return true;
            }
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return false;
}

const QString kMac = QStringLiteral("AA:BB:CC:DD:EE:FF");
const QString kDevicePath = QStringLiteral("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF");

QVariantMap deviceProperties(const bool connected, const int rssi, const QString& alias) {
    QVariantMap properties;
    properties.insert(QStringLiteral("Address"), kMac);
    properties.insert(QStringLiteral("Paired"), true);
    properties.insert(QStringLiteral("Trusted"), true);
    properties.insert(QStringLiteral("Connected"), connected);
    if (rssi != 0) {
        properties.insert(QStringLiteral("RSSI"), rssi);
    }
    if (!alias.isNull()) {
        properties.insert(QStringLiteral("Alias"), alias);
    }
    return properties;
}

BluezInterfaces deviceInterface(const QVariantMap& properties) {
    return BluezInterfaces{{QStringLiteral("org.bluez.Device1"), properties}};
}

BluezObjects singleDevice(const QVariantMap& properties) {
    return BluezObjects{{QDBusObjectPath(kDevicePath), deviceInterface(properties)}};
}

BluezObjects twoPaths(const bool firstConnected, const bool secondConnected) {
    return BluezObjects{
        {QDBusObjectPath(kDevicePath),
         deviceInterface({{QStringLiteral("Address"), kMac}, {QStringLiteral("Paired"), true},
                          {QStringLiteral("Trusted"), true}, {QStringLiteral("Connected"), firstConnected},
                          {QStringLiteral("Alias"), QStringLiteral("DualAdapter")}})},
        {QDBusObjectPath(QStringLiteral("/org/bluez/hci1/dev_AA_BB_CC_DD_EE_FF")),
         deviceInterface({{QStringLiteral("Address"), kMac}, {QStringLiteral("Paired"), true},
                          {QStringLiteral("Trusted"), true}, {QStringLiteral("Connected"), secondConnected},
                          {QStringLiteral("Alias"), QStringLiteral("DualAdapter")}})},
    };
}

BluezObjects adapterOnly(const bool powered) {
    const QVariantMap adapter{{QStringLiteral("Powered"), QVariant(powered)}};
    return BluezObjects{
        {QDBusObjectPath(QStringLiteral("/org/bluez/hci0")),
         BluezInterfaces{{QStringLiteral("org.bluez.Adapter1"), adapter}}},
    };
}

// org.bluez stand-in, split across two objects because BlueZ exposes the object manager and
// the properties interface as separate D-Bus interfaces.
class FakeBluez final : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")

public:
    using QObject::QObject;

public slots:
    Q_SCRIPTABLE BluezObjects GetManagedObjects() {
        if (failWithError_) {
            sendErrorReply(QStringLiteral("org.freedesktop.DBus.Error.ServiceUnknown"),
                           QStringLiteral("simulated BlueZ failure"));
        }
        return objects_;
    }

    void publish(const BluezObjects& objects) { objects_ = objects; }
    void setFailWithError(const bool value) { failWithError_ = value; }
    void emitInterfacesAdded(const QDBusObjectPath& path, const BluezInterfaces& interfaces) {
        Q_EMIT InterfacesAdded(path, interfaces);
    }
    void emitInterfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces) {
        Q_EMIT InterfacesRemoved(path, interfaces);
    }

signals:
    void InterfacesAdded(const QDBusObjectPath& path, const BluezInterfaces& interfaces);
    void InterfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces);

private:
    BluezObjects objects_;
    bool failWithError_{false};
};

class FakeProperties final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.Properties")

public:
    using QObject::QObject;

public slots:
    void emitChanged(const QString& interfaceName, const QVariantMap& changed) {
        Q_EMIT PropertiesChanged(interfaceName, changed, QStringList{});
    }

signals:
    void PropertiesChanged(const QString& interfaceName, const QVariantMap& changed, const QStringList& invalidated);
};

// Owns the org.bluez name and exports the fakes.
//
// It runs on its own thread because BluezMonitor issues a BLOCKING GetManagedObjects: a fake
// sharing the caller's event loop would deadlock. It also needs a real bus rather than a
// peer-to-peer QDBusServer, because a peer endpoint has no message bus to route signals, so
// PropertiesChanged/InterfacesAdded/InterfacesRemoved would silently never arrive.
class FakeBluezService final : public QThread {
    Q_OBJECT

public:
    explicit FakeBluezService(QString busAddress) : busAddress_(std::move(busAddress)) {}

    bool ready() const { return ready_; }
    bool failed() const { return failed_; }
    FakeBluez* manager() const { return manager_; }
    FakeProperties* properties() const { return properties_; }

protected:
    void run() override {
        QDBusConnection bus = QDBusConnection::connectToBus(busAddress_, QStringLiteral("fake-bluez"));
        if (!bus.isConnected()) {
            failed_ = true;
            return;
        }
        auto* manager = new FakeBluez;
        auto* properties = new FakeProperties;
        // The properties object must sit at the device object path: BluezMonitor resolves
        // the device from message.path() and re-enumerates for anything else.
        if (!bus.registerObject(QStringLiteral("/"), manager,
                                QDBusConnection::ExportScriptableSlots | QDBusConnection::ExportAllSignals)
            || !bus.registerObject(kDevicePath, properties, QDBusConnection::ExportAllSignals)
            || !bus.registerService(QStringLiteral("org.bluez"))) {
            failed_ = true;
            return;
        }
        manager_ = manager;
        properties_ = properties;
        ready_ = true;
        exec();
    }

private:
    QString busAddress_;
    FakeBluez* manager_{nullptr};
    FakeProperties* properties_{nullptr};
    bool ready_{false};
    bool failed_{false};
};

void publish(FakeBluez* fake, const BluezObjects& objects) {
    QMetaObject::invokeMethod(fake, "publish", Qt::BlockingQueuedConnection, Q_ARG(BluezObjects, objects));
}

void setFail(FakeBluez* fake, const bool value) {
    QMetaObject::invokeMethod(fake, "setFailWithError", Qt::BlockingQueuedConnection, Q_ARG(bool, value));
}

void emitChanged(FakeProperties* properties, const QVariantMap& changed) {
    QMetaObject::invokeMethod(properties, "emitChanged", Qt::BlockingQueuedConnection,
                              Q_ARG(QString, QStringLiteral("org.bluez.Device1")), Q_ARG(QVariantMap, changed));
}

void emitAdded(FakeBluez* fake, const QDBusObjectPath& path, const BluezInterfaces& interfaces) {
    QMetaObject::invokeMethod(fake, "emitInterfacesAdded", Qt::BlockingQueuedConnection,
                              Q_ARG(QDBusObjectPath, path), Q_ARG(BluezInterfaces, interfaces));
}

} // namespace

#include "bluez_monitor_test.moc"

int main(int argc, char* argv[]) {
    // Unbuffered: an abort must not lose the progress already reported.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);
    qDBusRegisterMetaType<BluezObjects>();

    // A self-contained config keeps this independent of the host: Nix ships an empty
    // session.conf and a build sandbox has no /etc/dbus-1 at all, so --session alone either
    // finds nothing or, with the store copy, has no <listen> element.
    QTemporaryFile configFile;
    if (!configFile.open()) {
        std::printf("FAIL: cannot create a temporary dbus config\n");
        return 1;
    }
    const QByteArray config = QByteArrayLiteral(
        "<!DOCTYPE busconfig PUBLIC \"-//freedesktop//DTD D-Bus Bus Configuration 1.0//EN\"\n"
        " \"http://www.freedesktop.org/standards/dbus/1.0/busconfig.dtd\">\n"
        "<busconfig>\n"
        "  <type>session</type>\n"
        "  <listen>unix:tmpdir=/tmp</listen>\n"
        "  <auth>EXTERNAL</auth>\n"
        "  <allow_anonymous/>\n"
        "  <policy context=\"default\">\n"
        "    <allow send_destination=\"*\" eavesdrop=\"true\"/>\n"
        "    <allow eavesdrop=\"true\"/>\n"
        "    <allow own=\"*\"/>\n"
        "  </policy>\n"
        "</busconfig>\n");
    configFile.write(config);
    configFile.flush();

    QProcess daemon;
    daemon.start(QStringLiteral("dbus-daemon"),
                 {QStringLiteral("--config-file=") + configFile.fileName(), QStringLiteral("--nofork"),
                  QStringLiteral("--print-address")});
    if (!daemon.waitForStarted(5000)) {
        std::printf("FAIL: could not start a private dbus-daemon: %s\n",
                    qUtf8Printable(daemon.errorString()));
        return 1;
    }
    QString busAddress;
    for (int i = 0; i < 100 && busAddress.isEmpty(); ++i) {
        if (daemon.waitForReadyRead(500)) {
            busAddress = QString::fromLocal8Bit(daemon.readAllStandardOutput()).trimmed();
        }
    }
    if (busAddress.isEmpty()) {
        std::printf("FAIL: dbus-daemon printed no address; stderr: %s\n",
                    qUtf8Printable(QString::fromLocal8Bit(daemon.readAllStandardError()).trimmed()));
        return 1;
    }
    if (!busAddress.startsWith(QLatin1String("unix:"))) {
        std::printf("FAIL: unexpected bus address '%s'\n", qUtf8Printable(busAddress));
        return 1;
    }

    FakeBluezService service(busAddress);
    service.start();
    for (int i = 0; i < 400 && !service.ready() && !service.failed(); ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    if (!service.ready()) {
        std::printf("FAIL: fake org.bluez service never registered\n");
        daemon.kill();
        return 1;
    }
    FakeBluez* fake = service.manager();
    FakeProperties* fakeProperties = service.properties();

    QDBusConnection bus = QDBusConnection::connectToBus(busAddress, QStringLiteral("bluez-monitor-test"));
    if (!bus.isConnected()) {
        std::printf("FAIL: client could not connect to the private bus\n");
        return 1;
    }
    spinFor(200);

    smartlocker::BluezMonitor monitor(nullptr, bus);
    monitor.setRefreshInterval(30);
    QSignalSpy observed(&monitor, &smartlocker::BluezMonitor::deviceObserved);
    QSignalSpy availability(&monitor, &smartlocker::BluezMonitor::availabilityChanged);

    // A single connected device is present, and an empty Alias falls back to Name.
    QVariantMap withEmptyAlias = deviceProperties(true, -55, QStringLiteral(""));
    withEmptyAlias.insert(QStringLiteral("Name"), QStringLiteral("FallbackName"));
    publish(fake, singleDevice(withEmptyAlias));
    monitor.start({}, true);
    check(waitForObservation(observed, kMac, true), "single connected device reported present");
    check(monitor.deviceName(kMac) == QStringLiteral("FallbackName"), "empty Alias falls back to Name");
    check(availability.count() >= 1, "availability published at least once");

    // An unchanged re-enumeration must not report the same observation again.
    observed.clear();
    spinFor(250);
    check(observed.count() == 0, "unchanged re-enumeration emits no duplicate observation");

    // Presence is the union across duplicate paths, not "last path wins".
    observed.clear();
    publish(fake, twoPaths(false, false));
    check(waitForObservation(observed, kMac, false), "both duplicates disconnected reports absence");
    observed.clear();
    publish(fake, twoPaths(true, false));
    check(waitForObservation(observed, kMac, true), "presence is the union across duplicate paths");
    observed.clear();
    publish(fake, twoPaths(false, true));
    spinFor(250);
    check(observed.count() == 0, "union stays stable when the connected path changes");
    observed.clear();
    publish(fake, twoPaths(false, false));
    check(waitForObservation(observed, kMac, false), "losing the last connected path reports absence");

    // Every object gone: the MAC vanishes and its cached name is pruned.
    observed.clear();
    publish(fake, BluezObjects{});
    check(waitForObservation(observed, kMac, false), "device removed entirely reports absence");
    spinFor(200);
    check(monitor.deviceName(kMac).isEmpty(), "vanished device name is pruned");
    observed.clear();
    spinFor(250);
    check(observed.count() == 0, "steady empty enumeration stays silent");

    // PropertiesChanged on the connected path applies both Connected and RSSI.
    observed.clear();
    publish(fake, singleDevice(deviceProperties(false, 0, QStringLiteral("Sig"))));
    check(waitForObservation(observed, kMac, false), "disconnected single path before the signal test");
    observed.clear();
    emitChanged(fakeProperties, {{QStringLiteral("Connected"), true}, {QStringLiteral("RSSI"), -61}});
    check(waitForObservation(observed, kMac, true), "PropertiesChanged applies Connected");
    bool sawRssi = false;
    for (const QList<QVariant>& call : observed) {
        if (call.at(3).toBool()) {
            sawRssi = true;
            check(call.at(2).toInt() == -61, "the reported RSSI value is the one that arrived");
        }
    }
    check(sawRssi, "PropertiesChanged delivers the RSSI reading for a connected path");

    // Once disconnected, a later RSSI update must not be reported as usable.
    observed.clear();
    emitChanged(fakeProperties, {{QStringLiteral("Connected"), false}});
    check(waitForObservation(observed, kMac, false), "PropertiesChanged applies disconnect");
    observed.clear();
    emitChanged(fakeProperties, {{QStringLiteral("RSSI"), -42}});
    spinFor(250);
    for (const QList<QVariant>& call : observed) {
        check(!call.at(3).toBool(), "RSSI is not reported as usable while the path is disconnected");
    }

    // InterfacesRemoved reports absence and prunes the cached name. The signal and the
    // object store must agree, exactly as BlueZ behaves, otherwise the re-enumeration that
    // follows the signal would immediately re-report the device. This step needs a generous
    // budget: emitting the signal blocks the calling thread, so the in-flight enumeration
    // reply is only handled once the blocking invocation returns.
    observed.clear();
    publish(fake, singleDevice(deviceProperties(true, 0, QStringLiteral("Gone"))));
    check(waitForObservation(observed, kMac, true), "device back before InterfacesRemoved");
    check(monitor.deviceName(kMac) == QStringLiteral("Gone"), "name cached before InterfacesRemoved");
    observed.clear();
    publish(fake, BluezObjects{});
    QMetaObject::invokeMethod(fake, "emitInterfacesRemoved", Qt::BlockingQueuedConnection,
                              Q_ARG(QDBusObjectPath, QDBusObjectPath(kDevicePath)),
                              Q_ARG(QStringList, QStringList{QStringLiteral("org.bluez.Device1")}));
    spinFor(600);
    check(waitForObservation(observed, kMac, false), "InterfacesRemoved reports absence");
    check(monitor.deviceName(kMac).isEmpty(), "InterfacesRemoved prunes the cached name");

    // Removing ONE of two paths for the same address must not report absence: presence is the
    // union, so the surviving object still holds the device present.
    observed.clear();
    publish(fake, twoPaths(true, true));
    check(waitForObservation(observed, kMac, true), "both duplicate paths present before removal");
    spinFor(150);
    publish(fake, twoPaths(false, true));
    QMetaObject::invokeMethod(fake, "emitInterfacesRemoved", Qt::BlockingQueuedConnection,
                              Q_ARG(QDBusObjectPath, QDBusObjectPath(kDevicePath)),
                              Q_ARG(QStringList, QStringList{QStringLiteral("org.bluez.Device1")}));
    spinFor(600);
    for (const QList<QVariant>& call : observed) {
        check(call.at(1).toBool(), "removing one of several paths never reports absence");
    }
    check(monitor.deviceName(kMac) == QStringLiteral("DualAdapter"), "name survives removal of one path");

    // Removing the last remaining path does report absence. Drive to absent first so the
    // following appearance is a real transition rather than a no-op.
    observed.clear();
    publish(fake, BluezObjects{});
    check(waitForObservation(observed, kMac, false), "device absent before the final removal");
    observed.clear();
    publish(fake, singleDevice(deviceProperties(true, 0, QStringLiteral("Last"))));
    check(waitForObservation(observed, kMac, true), "device present before the final removal");
    spinFor(150);
    publish(fake, BluezObjects{});
    QMetaObject::invokeMethod(fake, "emitInterfacesRemoved", Qt::BlockingQueuedConnection,
                              Q_ARG(QDBusObjectPath, QDBusObjectPath(kDevicePath)),
                              Q_ARG(QStringList, QStringList{QStringLiteral("org.bluez.Device1")}));
    check(waitForObservation(observed, kMac, false), "removing the last path reports absence");

    // A reconnect that reports Connected without RSSI must not leave a stale cached reading
    // behind, otherwise an enumeration restoring the SAME value would look unchanged and the
    // daemon would never learn RSSI is available again.
    observed.clear();
    publish(fake, singleDevice(deviceProperties(true, -55, QStringLiteral("Cycle"))));
    check(waitForObservation(observed, kMac, true), "device present with RSSI before the cycle");
    observed.clear();
    emitChanged(fakeProperties, {{QStringLiteral("Connected"), false}});
    check(waitForObservation(observed, kMac, false), "disconnect observed");
    observed.clear();
    emitChanged(fakeProperties, {{QStringLiteral("Connected"), true}});
    check(waitForObservation(observed, kMac, true), "reconnect observed");
    observed.clear();
    publish(fake, singleDevice(deviceProperties(true, -55, QStringLiteral("Cycle"))));
    check(waitForObservation(observed, kMac, true), "restored RSSI is re-reported after a reconnect");
    bool rssiRestored = false;
    for (const QList<QVariant>& call : observed) {
        if (call.at(3).toBool()) {
            rssiRestored = true;
            check(call.at(2).toInt() == -55, "the restored RSSI value is correct");
        }
    }
    check(rssiRestored, "an unchanged RSSI value is still reported after a disconnect cycle");

    // InterfacesAdded discovers a device that appears.
    observed.clear();
    publish(fake, singleDevice(deviceProperties(true, 0, QStringLiteral("Added"))));
    emitAdded(fake, QDBusObjectPath(kDevicePath), deviceInterface(deviceProperties(true, 0, QStringLiteral("Added"))));
    check(waitForObservation(observed, kMac, true), "InterfacesAdded discovers an appearing device");
    spinFor(150);
    check(monitor.deviceName(kMac) == QStringLiteral("Added"), "InterfacesAdded picks the name back up");

    // A failing reply must not fabricate observations or wipe the known device set.
    observed.clear();
    spinFor(200);
    setFail(fake, true);
    spinFor(300);
    check(observed.count() == 0, "failed reply does not fabricate observations");
    check(monitor.deviceName(kMac) == QStringLiteral("Added"), "failed reply keeps the previous device set");
    setFail(fake, false);

    // A configured device that BlueZ does not expose at all must still be reported absent.
    // Without that observation the state machine never clears its "seen a device" gate, so it
    // stays in Starting forever and the session never locks.
    smartlocker::BluezMonitor configured(nullptr, bus);
    configured.setRefreshInterval(30);
    QSignalSpy configuredObserved(&configured, &smartlocker::BluezMonitor::deviceObserved);

    publish(fake, adapterOnly(true));
    configured.start({kMac}, false);
    check(waitForObservation(configuredObserved, kMac, false),
          "a configured device BlueZ does not expose is reported absent");

    // With no powered adapter BlueZ cannot see anything, so absence must NOT be concluded and a
    // controller failure still cannot provoke a lock.
    configuredObserved.clear();
    publish(fake, adapterOnly(false));
    spinFor(250);
    check(configuredObserved.count() == 0, "no absence is reported while no adapter is powered");

    service.quit();
    service.wait(3000);
    daemon.kill();
    daemon.waitForFinished(2000);

    if (failures == 0) {
        std::printf("all bluez monitor checks passed\n");
        return 0;
    }
    std::printf("%d bluez monitor check(s) failed\n", failures);
    return 1;
}
