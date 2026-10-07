#include "smartlocker/bluez_monitor.hpp"

#include "smartlocker/bluez_types.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QDBusServer>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QThread>

#include <cstdio>
#include <cstdlib>

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

QVariantMap deviceProperties(const bool connected, const int rssi, const QString& alias, const QString& name) {
    QVariantMap properties;
    properties.insert(QStringLiteral("Address"), QStringLiteral("AA:BB:CC:DD:EE:FF"));
    properties.insert(QStringLiteral("Paired"), true);
    properties.insert(QStringLiteral("Trusted"), true);
    properties.insert(QStringLiteral("Connected"), connected);
    if (rssi != 0) {
        properties.insert(QStringLiteral("RSSI"), rssi);
    }
    if (!alias.isNull()) {
        properties.insert(QStringLiteral("Alias"), alias);
    }
    if (!name.isNull()) {
        properties.insert(QStringLiteral("Name"), name);
    }
    return properties;
}

BluezInterfaces deviceInterface(const QVariantMap& properties) {
    return BluezInterfaces{{QStringLiteral("org.bluez.Device1"), properties}};
}

BluezObjects singleDevice(const QVariantMap& properties) {
    return BluezObjects{
        {QDBusObjectPath(QStringLiteral("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF")), deviceInterface(properties)}};
}

BluezObjects twoPaths(const bool firstConnected, const bool secondConnected) {
    const QString mac = QStringLiteral("AA:BB:CC:DD:EE:FF");
    return BluezObjects{
        {QDBusObjectPath(QStringLiteral("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF")),
         deviceInterface({{QStringLiteral("Address"), mac}, {QStringLiteral("Paired"), true},
                          {QStringLiteral("Trusted"), true}, {QStringLiteral("Connected"), firstConnected},
                          {QStringLiteral("Alias"), QStringLiteral("DualAdapter")}})},
        {QDBusObjectPath(QStringLiteral("/org/bluez/hci1/dev_AA_BB_CC_DD_EE_FF")),
         deviceInterface({{QStringLiteral("Address"), mac}, {QStringLiteral("Paired"), true},
                          {QStringLiteral("Trusted"), true}, {QStringLiteral("Connected"), secondConnected}})},
    };
}

const QString kMac = QStringLiteral("AA:BB:CC:DD:EE:FF");

// org.bluez stand-in.
//
// It owns both the QDBusServer and the exported object and lives entirely on a
// worker thread: BluezMonitor issues a BLOCKING GetManagedObjects, so a fake sharing
// the caller's event loop would deadlock, and Qt requires an exported object and the
// connection that dispatches to it to share a thread.
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

private:
    BluezObjects objects_;
    bool failWithError_{false};
};

class FakeBusWorker final : public QThread {
    Q_OBJECT

public:
    QString address() const { return address_; }
    bool ready() const { return ready_; }
    FakeBluez* fake() const { return fake_; }

protected:
    void run() override {
        auto* server = new QDBusServer(QStringLiteral("unix:tmpdir=/tmp"));
        if (!server->isConnected()) {
            return;
        }
        auto* fake = new FakeBluez;
        connect(server, &QDBusServer::newConnection, fake, [fake](QDBusConnection incoming) {
            incoming.registerObject(QStringLiteral("/"), fake, QDBusConnection::ExportScriptableSlots);
        });
        {
            QMutexLocker locker(&mutex_);
            fake_ = fake;
            address_ = server->address();
            ready_ = true;
        }
        exec();
    }

private:
    QMutex mutex_;
    FakeBluez* fake_{nullptr};
    QString address_;
    bool ready_{false};
};

void publish(FakeBluez* fake, const BluezObjects& objects) {
    QMetaObject::invokeMethod(fake, "publish", Qt::BlockingQueuedConnection, Q_ARG(BluezObjects, objects));
}

void setFail(FakeBluez* fake, const bool value) {
    QMetaObject::invokeMethod(fake, "setFailWithError", Qt::BlockingQueuedConnection, Q_ARG(bool, value));
}

} // namespace

#include "bluez_monitor_test.moc"

int main(int argc, char* argv[]) {
    // Unbuffered: an abort must not lose the progress already reported.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);
    qDBusRegisterMetaType<BluezObjects>();

    FakeBusWorker worker;
    worker.start();
    for (int i = 0; i < 400 && !worker.ready(); ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(5);
    }
    if (!worker.ready()) {
        std::printf("FAIL: fake bus never became ready\n");
        return 1;
    }

    QDBusConnection bus = QDBusConnection::connectToPeer(worker.address(), QStringLiteral("org.bluez"));
    if (!bus.isConnected()) {
        std::printf("FAIL: peer connection not established\n");
        return 1;
    }
    FakeBluez* fake = worker.fake();
    // Wait for the server side to accept and export before issuing blocking calls.
    spinFor(300);

    {
        QDBusInterface probe(QStringLiteral("org.bluez"), QStringLiteral("/"),
                             QStringLiteral("org.freedesktop.DBus.ObjectManager"), bus);
        probe.setTimeout(3000);
        publish(fake, singleDevice(deviceProperties(true, -55, QStringLiteral("Probe"), QString())));
        const QDBusReply<BluezObjects> reply = probe.call(QStringLiteral("GetManagedObjects"));
        if (!reply.isValid()) {
            std::printf("FAIL: fake bus does not answer GetManagedObjects: %s\n",
                        reply.error().message().toStdString().c_str());
            return 1;
        }
    }

    smartlocker::BluezMonitor monitor(nullptr, bus);
    monitor.setRefreshInterval(30);
    QSignalSpy observed(&monitor, &smartlocker::BluezMonitor::deviceObserved);
    QSignalSpy availability(&monitor, &smartlocker::BluezMonitor::availabilityChanged);

    // A single connected device is present, and an empty Alias falls back to Name.
    publish(fake, singleDevice(deviceProperties(true, -55, QStringLiteral(""), QStringLiteral("FallbackName"))));
    monitor.start({}, true);
    check(waitForObservation(observed, kMac, true), "single connected device reported present");
    check(monitor.deviceName(kMac) == QStringLiteral("FallbackName"), "empty Alias falls back to Name");
    check(availability.count() >= 1, "availability published at least once");

    // An unchanged re-enumeration must not report the same observation again.
    observed.clear();
    spinFor(250);
    check(observed.count() == 0, "unchanged re-enumeration emits no duplicate observation");

    // Both duplicate paths disconnected: absent.
    observed.clear();
    publish(fake, twoPaths(false, false));
    check(waitForObservation(observed, kMac, false), "both duplicates disconnected reports absence");

    // Only the FIRST path connected. A last-path-wins implementation would read the
    // second (disconnected) object here and wrongly report absence.
    observed.clear();
    publish(fake, twoPaths(true, false));
    check(waitForObservation(observed, kMac, true), "presence is the union across duplicate paths");

    // Now only the second path is connected: still present, so nothing changed.
    observed.clear();
    publish(fake, twoPaths(false, true));
    spinFor(250);
    check(observed.count() == 0, "union stays stable when the connected path changes");

    // Back to both disconnected.
    observed.clear();
    publish(fake, twoPaths(false, false));
    check(waitForObservation(observed, kMac, false), "losing the last connected path reports absence");

    // Every object gone: the MAC vanishes and its cached name is pruned.
    observed.clear();
    publish(fake, BluezObjects{});
    check(waitForObservation(observed, kMac, false), "device removed entirely reports absence");
    spinFor(150);
    check(monitor.deviceName(kMac).isEmpty(), "vanished device name is pruned");
    observed.clear();
    spinFor(250);
    check(observed.count() == 0, "steady empty enumeration stays silent");

    // A failing reply must not fabricate observations or wipe the known device set.
    observed.clear();
    publish(fake, singleDevice(deviceProperties(true, -50, QStringLiteral("KeptName"), QString())));
    check(waitForObservation(observed, kMac, true), "device present before the failure");
    check(monitor.deviceName(kMac) == QStringLiteral("KeptName"), "name cached before the failure");
    observed.clear();
    setFail(fake, true);
    spinFor(300);
    check(observed.count() == 0, "failed reply does not fabricate observations");
    check(monitor.deviceName(kMac) == QStringLiteral("KeptName"), "failed reply keeps the previous device set");
    setFail(fake, false);

    worker.quit();
    worker.wait(3000);

    if (failures == 0) {
        std::printf("all bluez monitor checks passed\n");
        return 0;
    }
    std::printf("%d bluez monitor check(s) failed\n", failures);
    return 1;
}
