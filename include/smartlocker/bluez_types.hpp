#pragma once

#include <QDBusObjectPath>
#include <QMap>
#include <QVariantMap>

// Shape of an org.freedesktop.DBus.ObjectManager.GetManagedObjects reply:
// object path -> interface name -> property name -> value.
using BluezInterfaces = QMap<QString, QVariantMap>;
using BluezObjects = QMap<QDBusObjectPath, BluezInterfaces>;

Q_DECLARE_METATYPE(BluezObjects)
