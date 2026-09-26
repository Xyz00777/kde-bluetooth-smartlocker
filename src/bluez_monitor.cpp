#include "smartlocker/bluez_monitor.hpp"

#include "smartlocker/device_spec.hpp"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QLoggingCategory>

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
    const QStringList previouslyResolved = macToPath_.keys();
    connectionStates_.clear();
    everConnected_.clear();
    lastRssi_.clear();
    macToPath_.clear();
    for (const QString& mac : previouslyResolved) {
        emit deviceObserved(mac, false, 0, false);
    }
    emit selectedDevicesChanged(autoSelect_ ? QStringList{} : watchedMacs_.values());
    emit availabilityChanged(false);
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
    QMap<QString, QString> nextMacToPath;
    for (auto object = objects.cbegin(); object != objects.cend(); ++object) {
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
            nextMacToPath.insert(macId, object.key().path());
        }
    }
    QSet<QString> previous;
    QSet<QString> current;
    for (auto it = macToPath_.cbegin(); it != macToPath_.cend(); ++it) previous.insert(it.key());
    for (auto it = nextMacToPath.cbegin(); it != nextMacToPath.cend(); ++it) current.insert(it.key());
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
    macToPath_ = nextMacToPath;
    const QStringList selected = autoSelect_ ? current.values() : watchedMacs_.values();
    emit selectedDevicesChanged(selected);
    for (auto mapping = macToPath_.cbegin(); mapping != macToPath_.cend(); ++mapping) {
        const QVariantMap properties = objects.value(QDBusObjectPath{mapping.value()}).value("org.bluez.Device1");
        const bool connected = properties.value("Connected").toBool();
        if (connected) {
            everConnected_.insert(mapping.key(), true);
        }
        connectionStates_.insert(mapping.key(), connected);
        const bool hasRssi = properties.contains("RSSI") && !(everConnected_.value(mapping.key(), false) && !connected);
        if (hasRssi) {
            lastRssi_.insert(mapping.key(), properties.value("RSSI").toInt());
        }
        emit deviceObserved(mapping.key(), connected, hasRssi ? properties.value("RSSI").toInt() : 0, hasRssi);
    }
}

void BluezMonitor::onPropertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList& invalidated,
                                       const QDBusMessage& message) {
    if (interface != "org.bluez.Device1") {
        return;
    }
    const QString path = message.path();
    auto macIt = macToPath_.cbegin();
    while (macIt != macToPath_.cend() && macIt.value() != path) {
        ++macIt;
    }
    if (macIt == macToPath_.cend()) {
        enumerateDevices();
        return;
    }
    if (changed.contains("Address") || changed.contains("Paired") || changed.contains("Trusted")
        || invalidated.contains("Address") || invalidated.contains("Paired") || invalidated.contains("Trusted")
        || invalidated.contains("Connected") || invalidated.contains("RSSI")) {
        enumerateDevices();
        return;
    }
    const QString mac = macIt.key();
    const auto connected = changed.constFind("Connected");
    const auto rssi = changed.constFind("RSSI");
    if (connected == changed.cend() && rssi == changed.cend()) {
        return;
    }
    if (connected != changed.cend()) {
        connectionStates_.insert(mac, connected->toBool());
        if (connected->toBool()) {
            everConnected_.insert(mac, true);
        }
    }
    const bool isConnected = connectionStates_.value(mac, false);
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
    if (!interfaces.contains("org.bluez.Device1")) {
        return;
    }
    auto macIt = macToPath_.cbegin();
    while (macIt != macToPath_.cend() && macIt.value() != path.path()) {
        ++macIt;
    }
    if (macIt != macToPath_.cend()) {
        const QString mac = macIt.key();
        macToPath_.remove(mac);
        connectionStates_.remove(mac);
        everConnected_.remove(mac);
        lastRssi_.remove(mac);
        emit deviceObserved(mac, false, 0, false);
        enumerateDevices();
    }
}

}
