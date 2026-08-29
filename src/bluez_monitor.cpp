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
    emit availabilityChanged(false);
}

void BluezMonitor::start(const QSet<QString>& watchedPaths) {
    watchedPaths_ = watchedPaths;
    const bool connected = bus_.interface() != nullptr && bus_.interface()->isServiceRegistered("org.bluez");
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
        const QDBusReply<QVariantMap> reply = properties.call("GetAll", "org.bluez.Device1");
        if (!reply.isValid()) {
            connectionStates_.insert(path, false);
            emit deviceObserved(path, false, 0, false);
            continue;
        }
        const QVariantMap values = reply.value();
        const bool connected = values.value("Connected").toBool();
        const bool hasRssi = values.contains("RSSI");
        connectionStates_.insert(path, connected);
        emit deviceObserved(path, connected, values.value("RSSI").toInt(), hasRssi);
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
        connectionStates_.insert(path, connected->toBool());
    }
    if (!connectionStates_.contains(path)) {
        return;
    }
    const bool isConnected = connectionStates_.value(path);
    if (rssi != changed.cend()) {
        emit deviceObserved(path, isConnected, rssi->toInt(), true);
    } else if (connected != changed.cend()) {
        emit deviceObserved(path, isConnected, 0, false);
    }
}

void BluezMonitor::onInterfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces) {
    const QString devicePath = path.path();
    if (!watchedPaths_.contains(devicePath) || !interfaces.contains("org.bluez.Device1")) {
        return;
    }
    connectionStates_.remove(devicePath);
    emit deviceObserved(devicePath, false, 0, false);
}

}
