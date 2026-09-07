#include "smartlocker/bluez_monitor.hpp"

#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>

namespace smartlocker {

BluezMonitor::BluezMonitor(QObject* parent)
    : QObject(parent), bus_(QDBusConnection::systemBus()),
      serviceWatcher_("org.bluez", bus_, QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this) {
    refreshTimer_.setInterval(5000);
    connect(&refreshTimer_, &QTimer::timeout, this, [this] { enumerateDevices(watchedPaths_); });
    connect(&serviceWatcher_, &QDBusServiceWatcher::serviceRegistered, this, &BluezMonitor::onServiceRegistered);
    connect(&serviceWatcher_, &QDBusServiceWatcher::serviceUnregistered, this, &BluezMonitor::onServiceUnregistered);
}

void BluezMonitor::onServiceRegistered(const QString&) {
    emit availabilityChanged(true);
    enumerateDevices(watchedPaths_);
}

void BluezMonitor::onServiceUnregistered(const QString&) {
    connectionStates_.clear();
    everConnected_.clear();
    lastRssi_.clear();
    emit availabilityChanged(false);
}

void BluezMonitor::start(const QSet<QString>& watchedPaths) {
    if (started_) {
        return;
    }
    started_ = true;
    watchedPaths_ = watchedPaths;
    QDBusConnectionInterface* interface = bus_.interface();
    if (interface != nullptr) {
        interface->setTimeout(2000);
    }
    const bool connected = interface != nullptr && interface->isServiceRegistered("org.bluez");
    emit availabilityChanged(connected);
    if (connected) {
        enumerateDevices(watchedPaths);
    }
    bus_.connect("org.bluez", QString{}, "org.freedesktop.DBus.Properties", "PropertiesChanged", this,
                 SLOT(onPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)));
    bus_.connect("org.bluez", QString{}, "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", this,
                 SLOT(onInterfacesRemoved(QDBusObjectPath,QStringList)));
    refreshTimer_.start();
}

void BluezMonitor::enumerateDevices(const QSet<QString>& watchedPaths) {
    for (const QString& path : watchedPaths) {
        QDBusInterface properties{"org.bluez", path, "org.freedesktop.DBus.Properties", bus_};
        properties.setTimeout(2000);
        const QDBusReply<QVariantMap> reply = properties.call("GetAll", "org.bluez.Device1");
        if (!reply.isValid()) {
            connectionStates_.insert(path, false);
            emit deviceObserved(path, false, 0, false);
            continue;
        }
        const QVariantMap values = reply.value();
        const bool connected = values.value("Connected").toBool();
        const bool hasRssi = values.contains("RSSI");
        if (connected) {
            everConnected_.insert(path, true);
        }
        connectionStates_.insert(path, connected);
        // A device that was previously connected but is now disconnected must not
        // keep stale RSSI presence: BlueZ retains the last RSSI value, so trusting
        // it here would keep a walked-away device "present" forever.
        if (hasRssi && !(everConnected_.value(path, false) && !connected)) {
            lastRssi_.insert(path, values.value("RSSI").toInt());
            emit deviceObserved(path, connected, values.value("RSSI").toInt(), true);
        } else {
            emit deviceObserved(path, connected, 0, false);
        }
    }
}

void BluezMonitor::onPropertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList&,
                                       const QDBusMessage& message) {
    if (interface != "org.bluez.Device1") {
        return;
    }
    const QString path = message.path();
    if (!watchedPaths_.contains(path)) {
        return;
    }
    const auto connected = changed.constFind("Connected");
    const auto rssi = changed.constFind("RSSI");
    if (connected == changed.cend() && rssi == changed.cend()) {
        return;
    }
    if (connected != changed.cend()) {
        if (connected->toBool()) {
            everConnected_.insert(path, true);
        }
        connectionStates_.insert(path, connected->toBool());
    }
    if (!connectionStates_.contains(path)) {
        return;
    }
    const bool isConnected = connectionStates_.value(path);
    if (rssi != changed.cend()) {
        lastRssi_.insert(path, rssi->toInt());
        emit deviceObserved(path, isConnected, rssi->toInt(), true);
    } else if (connected != changed.cend()) {
        if (everConnected_.value(path, false) && !isConnected) {
            emit deviceObserved(path, false, 0, false);
        } else {
            const auto cached = lastRssi_.constFind(path);
            if (cached != lastRssi_.cend()) {
                emit deviceObserved(path, isConnected, cached.value(), true);
            } else {
                emit deviceObserved(path, isConnected, 0, false);
            }
        }
    }
}

void BluezMonitor::onInterfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces) {
    const QString devicePath = path.path();
    if (!watchedPaths_.contains(devicePath) || !interfaces.contains("org.bluez.Device1")) {
        return;
    }
    connectionStates_.remove(devicePath);
    everConnected_.remove(devicePath);
    lastRssi_.remove(devicePath);
    emit deviceObserved(devicePath, false, 0, false);
}

}
