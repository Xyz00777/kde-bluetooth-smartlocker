#pragma once

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QDBusServiceWatcher>
#include <QTimer>

namespace smartlocker {

class BluezMonitor final : public QObject {
    Q_OBJECT

public:
    explicit BluezMonitor(QObject* parent = nullptr);
    void start(const QSet<QString>& watchedPaths);

signals:
    void availabilityChanged(bool available);
    void deviceObserved(const QString& path, bool connected, int rssiDbm, bool hasRssi);

private slots:
    void onPropertiesChanged(const QString& interface, const QVariantMap& changed, const QStringList& invalidated,
                             const QDBusMessage& message);
    void onInterfacesRemoved(const QDBusObjectPath& path, const QStringList& interfaces);
    void onServiceRegistered(const QString& service);
    void onServiceUnregistered(const QString& service);

private:
    void enumerateDevices(const QSet<QString>& watchedPaths);

    QDBusConnection bus_;
    QMap<QString, bool> connectionStates_;
    QSet<QString> watchedPaths_;
    QTimer refreshTimer_;
    QDBusServiceWatcher serviceWatcher_;
};

}
