#include "smartlocker/bluez_monitor.hpp"

#include "smartlocker/device_spec.hpp"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QLoggingCategory>

#include <iterator>

using BluezInterfaces = QMap<QString, QVariantMap>;
using BluezObjects = QMap<QDBusObjectPath, BluezInterfaces>;
Q_DECLARE_METATYPE(BluezObjects)

namespace {

Q_LOGGING_CATEGORY(bluezMonitorLog, "org.kde.smartlocker.bluez")

}

namespace smartlocker {

BluezMonitor::BluezMonitor(QObject* parent)
    : QObject(parent), bus_(QDBusConnection::systemBus()),
      serviceWatcher_("org.bluez", bus_, QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this) {
    qDBusRegisterMetaType<BluezObjects>();
    refreshTimer_.setInterval(5000);
    connect(&refreshTimer_, &QTimer::timeout, this, [this] { enumerateDevices(); });
    connect(&serviceWatcher_, &QDBusServiceWatcher::serviceRegistered, this, &BluezMonitor::onServiceRegistered);
    connect(&serviceWatcher_, &QDBusServiceWatcher::serviceUnregistered, this, &BluezMonitor::onServiceUnregistered);
}

void BluezMonitor::onServiceRegistered(const QString&) {
    emit availabilityChanged(true);
    enumerateDevices();
}

void BluezMonitor::onServiceUnregistered(const QString&) {
    const QStringList previouslyResolved = macToPaths_.keys();
    connectionStates_.clear();
    pathConnectionStates_.clear();
    everConnected_.clear();
    lastRssi_.clear();
    macToPaths_.clear();
    pathToMac_.clear();
    deviceNames_.clear();
    for (const QString& mac : previouslyResolved) {
        emit deviceObserved(mac, false, 0, false);
    }
    emit selectedDevicesChanged(autoSelect_ ? QStringList{} : watchedMacs_.values());
    emit availabilityChanged(false);
}

QString BluezMonitor::deviceName(const QString& mac) const {
    return deviceNames_.value(mac);
}

void BluezMonitor::start(const QSet<QString>& watchedMacs, const bool autoSelect) {
    if (started_) {
        return;
    }
    started_ = true;
    watchedMacs_ = watchedMacs;
    autoSelect_ = autoSelect;
    QDBusConnectionInterface* interface = bus_.interface();
    if (interface != nullptr) {
        interface->setTimeout(2000);
    }
    const bool connected = interface != nullptr && interface->isServiceRegistered("org.bluez");
    emit availabilityChanged(connected);
    bus_.connect("org.bluez", QString{}, "org.freedesktop.DBus.Properties", "PropertiesChanged", this,
                 SLOT(onPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
    bus_.connect("org.bluez", QString{}, "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", this,
                 SLOT(onInterfacesRemoved(QDBusObjectPath,QStringList)));
    bus_.connect("org.bluez", QString{}, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", this, SLOT(onInterfacesAdded()));
    refreshTimer_.start();
    if (connected) {
        enumerateDevices();
    }
}

void BluezMonitor::enumerateDevices() {
    QDBusInterface manager{"org.bluez", "/", "org.freedesktop.DBus.ObjectManager", bus_};
    manager.setTimeout(2000);
    const QDBusMessage reply = manager.call("GetManagedObjects");
    if (reply.type() == QDBusMessage::ErrorMessage || reply.arguments().isEmpty()) {
        qCWarning(bluezMonitorLog) << "failed to enumerate BlueZ objects:" << reply.errorMessage();
        return;
    }
    const BluezObjects objects = qdbus_cast<BluezObjects>(reply.arguments().constFirst());
    bool hasAdapter = false;
    QMap<QString, QStringList> nextMacToPaths;
    for (auto object = objects.cbegin(); object != objects.cend(); ++object) {
        const auto adapter = object.value().constFind("org.bluez.Adapter1");
        if (adapter != object.value().cend() && adapter.value().value("Powered", true).toBool()) {
            hasAdapter = true;
        }
        const auto device = object.value().constFind("org.bluez.Device1");
        if (device == object.value().cend()) {
            continue;
        }
        const QVariantMap& properties = device.value();
        const QString rawAddress = properties.value("Address").toString();
        const auto mac = normalizeDeviceSpec(rawAddress.toStdString());
        if (!mac.has_value()) {
            continue;
        }
        const QString macId = QString::fromStdString(*mac);
        if (autoSelect_ ? autoSelectedDevice(properties.value("Paired").toBool(), properties.value("Trusted").toBool())
                        : watchedMacs_.contains(macId)) {
            nextMacToPaths[macId].append(object.key().path());
        }
    }
    emit availabilityChanged(hasAdapter);
    QSet<QString> previous;
    QSet<QString> current;
    for (auto it = macToPaths_.cbegin(); it != macToPaths_.cend(); ++it) previous.insert(it.key());
    for (auto it = nextMacToPaths.cbegin(); it != nextMacToPaths.cend(); ++it) current.insert(it.key());
    for (const QString& mac : previous - current) {
        emit deviceObserved(mac, false, 0, false);
    }
    if (!autoSelect_) {
        // Edge-triggered: never-present devices must not spam the journal per tick.
        QSet<QString> stillAbsent;
        for (const QString& mac : watchedMacs_ - current) {
            stillAbsent.insert(mac);
            if (!reportedAbsent_.contains(mac)) {
                qCDebug(bluezMonitorLog) << "configured Bluetooth device is currently absent:" << mac;
            }
        }
        reportedAbsent_ = stillAbsent;
    } else {
        reportedAbsent_.clear();
    }
    macToPaths_ = nextMacToPaths;
    pathToMac_.clear();
    for (auto it = macToPaths_.cbegin(); it != macToPaths_.cend(); ++it) {
        for (const QString& path : it.value()) {
            pathToMac_.insert(path, it.key());
        }
    }
    const QStringList selected = autoSelect_ ? macToPaths_.keys() : watchedMacs_.values();
    emit selectedDevicesChanged(selected);
    for (auto mapping = macToPaths_.cbegin(); mapping != macToPaths_.cend(); ++mapping) {
        const QString mac = mapping.key();
        // One address can be reachable through several Device1 objects (e.g. paired on
        // more than one adapter). Presence is the union over those objects, otherwise a
        // disconnected duplicate would mask a connected one and fabricate an absence.
        bool connected = false;
        QString name;
        bool hasRssi = false;
        int rssiDbm = 0;
        for (const QString& path : mapping.value()) {
            const QVariantMap properties = objects.value(QDBusObjectPath{path}).value("org.bluez.Device1");
            if (name.isEmpty()) {
                name = properties.value("Alias", properties.value("Name")).toString();
            }
            const bool pathConnected = properties.value("Connected").toBool();
            pathConnectionStates_.insert(path, pathConnected);
            if (pathConnected && !connected) {
                connected = true;
                if (properties.contains("RSSI")) {
                    hasRssi = true;
                    rssiDbm = properties.value("RSSI").toInt();
                }
            }
        }
        const QString prev = deviceNames_.value(mac);
        if (name.isEmpty()) {
            deviceNames_.remove(mac);
        } else {
            deviceNames_.insert(mac, name);
        }
        if (name != prev) {
            emit deviceNameChanged(mac, name);
        }
        if (connected) {
            everConnected_.insert(mac, true);
        }
        connectionStates_.insert(mac, connected);
        if (hasRssi) {
            lastRssi_.insert(mac, rssiDbm);
        }
        emit deviceObserved(mac, connected, rssiDbm, hasRssi);
    }
    const QSet<QString> livePaths(pathToMac_.cbegin(), pathToMac_.cend());
    for (auto it = pathConnectionStates_.begin(); it != pathConnectionStates_.end();) {
        it = livePaths.contains(it.key()) ? std::next(it) : pathConnectionStates_.erase(it);
    }
}

bool BluezMonitor::macConnected(const QString& mac) const {
    const auto it = macToPaths_.constFind(mac);
    if (it == macToPaths_.cend()) {
        return false;
    }
    for (const QString& path : it.value()) {
        if (pathConnectionStates_.value(path, false)) {
            return true;
        }
    }
    return false;
}

void BluezMonitor::onPropertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList& invalidated,
                                       const QDBusMessage& message) {
    if (interface == "org.bluez.Adapter1") {
        if (changed.contains("Powered") || invalidated.contains("Powered")) {
            enumerateDevices();
        }
        return;
    }
    if (interface != "org.bluez.Device1") {
        return;
    }
    const QString path = message.path();
    const auto macIt = pathToMac_.constFind(path);
    if (macIt == pathToMac_.cend()) {
        enumerateDevices();
        return;
    }
    if (changed.contains("Address") || changed.contains("Paired") || changed.contains("Trusted")
        || changed.contains("Name") || changed.contains("Alias")
        || invalidated.contains("Address") || invalidated.contains("Paired") || invalidated.contains("Trusted")
        || invalidated.contains("Name") || invalidated.contains("Alias")
        || invalidated.contains("Connected") || invalidated.contains("RSSI")) {
        enumerateDevices();
        return;
    }
    const QString mac = macIt.value();
    const auto connected = changed.constFind("Connected");
    const auto rssi = changed.constFind("RSSI");
    if (connected == changed.cend() && rssi == changed.cend()) {
        return;
    }
    if (connected != changed.cend()) {
        pathConnectionStates_.insert(path, connected->toBool());
        if (connected->toBool()) {
            everConnected_.insert(mac, true);
        }
    }
    const bool isConnected = macConnected(mac);
    connectionStates_.insert(mac, isConnected);
    if (rssi != changed.cend() && !(everConnected_.value(mac, false) && !isConnected)) {
        lastRssi_.insert(mac, rssi->toInt());
        emit deviceObserved(mac, isConnected, rssi->toInt(), true);
    } else {
        if (everConnected_.value(mac, false) && !isConnected) {
            lastRssi_.remove(mac);
        }
        emit deviceObserved(mac, isConnected, 0, false);
    }
}

void BluezMonitor::onInterfacesAdded() {
    enumerateDevices();
}

void BluezMonitor::onInterfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces) {
    if (interfaces.contains("org.bluez.Adapter1")) {
        enumerateDevices();
        return;
    }
    if (!interfaces.contains("org.bluez.Device1")) {
        return;
    }
    const auto macIt = pathToMac_.constFind(path.path());
    if (macIt != pathToMac_.cend()) {
        const QString mac = macIt.value();
        pathToMac_.erase(macIt);
        pathConnectionStates_.remove(path.path());
        macToPaths_.remove(mac);
        deviceNames_.remove(mac);
        connectionStates_.remove(mac);
        everConnected_.remove(mac);
        lastRssi_.remove(mac);
        enumerateDevices();
    }
}

}
