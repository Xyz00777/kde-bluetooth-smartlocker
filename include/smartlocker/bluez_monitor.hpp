#pragma once

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QMap>
#include <QVariantMap>
#include <QObject>
#include <QSet>
#include <QDBusServiceWatcher>
#include <QTimer>

namespace smartlocker {

class BluezMonitor final : public QObject {
    Q_OBJECT

public:
    explicit BluezMonitor(QObject* parent = nullptr);
    void start(const QSet<QString>& watchedMacs, bool autoSelect);

signals:
    void availabilityChanged(bool available);
    void deviceObserved(const QString& mac, bool connected, int rssiDbm, bool hasRssi);
    void selectedDevicesChanged(const QStringList& macs);

private slots:
    void onPropertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList& invalidated,
                             const QDBusMessage& message);
    void onInterfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces);
    void onInterfacesAdded();
    void onServiceRegistered(const QString& service);
    void onServiceUnregistered(const QString& service);

private:
    void enumerateDevices();

    QDBusConnection bus_;
    QMap<QString, bool> connectionStates_;
    QMap<QString, bool> everConnected_;
    QMap<QString, int> lastRssi_;
    QMap<QString, QString> macToPath_;
    QSet<QString> watchedMacs_;
    QSet<QString> reportedAbsent_;
    bool autoSelect_{false};
    bool started_{false};
    QTimer refreshTimer_;
    QDBusServiceWatcher serviceWatcher_;
};

}
