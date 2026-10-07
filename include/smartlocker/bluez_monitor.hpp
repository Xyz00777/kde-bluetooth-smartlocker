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
    [[nodiscard]] QString deviceName(const QString& mac) const;

signals:
    void availabilityChanged(bool available);
    void deviceObserved(const QString& mac, bool connected, int rssiDbm, bool hasRssi);
    void selectedDevicesChanged(const QStringList& macs);
    void deviceNameChanged(const QString& mac, const QString& name);

private slots:
    void onPropertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList& invalidated,
                             const QDBusMessage& message);
    void onInterfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces);
    void onInterfacesAdded();
    void onServiceRegistered(const QString& service);
    void onServiceUnregistered(const QString& service);

private:
    void enumerateDevices();
    [[nodiscard]] bool macConnected(const QString& mac) const;

    QDBusConnection bus_;
    QMap<QString, bool> connectionStates_;
    QMap<QString, bool> pathConnectionStates_;
    QMap<QString, bool> everConnected_;
    QMap<QString, int> lastRssi_;
    QMap<QString, QStringList> macToPaths_;
    QMap<QString, QString> pathToMac_;
    QMap<QString, QString> deviceNames_;
    QSet<QString> watchedMacs_;
    QSet<QString> reportedAbsent_;
    bool autoSelect_{false};
    bool started_{false};
    QTimer refreshTimer_;
    QDBusServiceWatcher serviceWatcher_;
};

}
