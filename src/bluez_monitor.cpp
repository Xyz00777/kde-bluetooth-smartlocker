#include "smartlocker/bluez_monitor.hpp"

#include "smartlocker/bluez_types.hpp"
#include "smartlocker/device_spec.hpp"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QLoggingCategory>

#include <iterator>
#include <utility>

namespace {

Q_LOGGING_CATEGORY(bluezMonitorLog, "org.kde.smartlocker.bluez")

// Every real state change (connect, disconnect, add, remove, RSSI, adapter power) already
// arrives as a D-Bus signal, so the periodic enumeration is only a watchdog for a missed
// signal. Poll quickly while something is absent or not yet known, and slowly once every
// device is stably present, where there is nothing to detect.
constexpr int kFastRefreshMs = 5000;
constexpr int kSlowRefreshMs = 60000;

}

namespace smartlocker {

BluezMonitor::BluezMonitor(QObject* parent, QDBusConnection bus)
    : QObject(parent), bus_(std::move(bus)),
      serviceWatcher_("org.bluez", bus_, QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this) {
    qDBusRegisterMetaType<BluezObjects>();
    refreshTimer_.setInterval(kFastRefreshMs);
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

void BluezMonitor::setRefreshInterval(const int milliseconds) {
    // A positive value pins the watchdog, disabling the adaptive backoff, so tests are not
    // subject to the slow interval once the observed set settles.
    pinnedRefreshIntervalMs_ = milliseconds;
    refreshTimer_.setInterval(milliseconds);
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
    const QDBusReply<BluezObjects> reply = manager.call("GetManagedObjects");
    // A typed reply covers both failure modes: an error reply is invalid, and so is a
    // payload that cannot be demarshalled into the object-manager shape. Either way the
    // previous device set is kept, rather than being replaced by a partial read.
    if (!reply.isValid()) {
        qCWarning(bluezMonitorLog) << "failed to enumerate BlueZ objects:" << reply.error().message();
        return;
    }
    const BluezObjects objects = reply.value();
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
        // Drop every per-MAC cache entry, or names and connection state accumulate for
        // devices that are no longer present.
        deviceNames_.remove(mac);
        connectionStates_.remove(mac);
        everConnected_.remove(mac);
        lastRssi_.remove(mac);
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
    // "Settled" must mean the observed set stopped changing. Comparing against the
    // configured set would wrongly report settled in auto-select mode, where that set is
    // empty, even while devices are appearing and disappearing.
    const bool settled = !current.isEmpty() && previous == current;
    if (pinnedRefreshIntervalMs_ > 0) {
        refreshTimer_.start(pinnedRefreshIntervalMs_);
    } else if (refreshTimer_.isActive()) {
        refreshTimer_.start(settled ? kSlowRefreshMs : kFastRefreshMs);
    }
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
                const QString alias = properties.value("Alias").toString();
                name = alias.isEmpty() ? properties.value("Name").toString() : alias;
            }
            const bool pathConnected = properties.value("Connected").toBool();
            pathConnectionStates_.insert(path, pathConnected);
            if (pathConnected) {
                connected = true;
                if (!hasRssi && properties.contains("RSSI")) {
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
        // The periodic re-enumeration re-reads the same cached properties every tick. Only
        // report a genuine change: re-emitting unchanged values would count one RSSI
        // snapshot as a fresh sample each time, collapsing the averaging window.
        const bool isNew = !connectionStates_.contains(mac);
        const bool presenceChanged = isNew || connectionStates_.value(mac) != connected;
        const bool rssiChanged = isNew || lastRssi_.contains(mac) != hasRssi || (hasRssi && lastRssi_.value(mac) != rssiDbm);
        connectionStates_.insert(mac, connected);
        if (hasRssi) {
            lastRssi_.insert(mac, rssiDbm);
        } else {
            // A device that stops reporting RSSI must not leave a cached reading behind,
            // both for change detection and because stale RSSI must never imply presence.
            lastRssi_.remove(mac);
        }
        if (presenceChanged || rssiChanged) {
            emit deviceObserved(mac, connected, rssiDbm, hasRssi);
        }
    }
    // QMap iterators dereference to the mapped value, so a range-constructed QSet
    // would hold MACs rather than object paths and prune every live entry. Insert keys.
    QSet<QString> livePaths;
    livePaths.reserve(pathToMac_.size());
    for (auto it = pathToMac_.cbegin(); it != pathToMac_.cend(); ++it) {
        livePaths.insert(it.key());
    }
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
    // The reporting path's own state must authorise the reading: a disconnected duplicate
    // must never refresh the MAC-level RSSI while another path keeps the MAC present.
    const bool pathConnected = pathConnectionStates_.value(path, false);
    if (rssi != changed.cend() && pathConnected && !(everConnected_.value(mac, false) && !isConnected)) {
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
