#include "smartlocker/bluez_types.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusObjectPath>

class LoginManager final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.login1.Manager")

public slots:
    Q_SCRIPTABLE QDBusObjectPath GetSessionByPID(quint32) {
        ++requests_;
        return QDBusObjectPath{QStringLiteral("/org/freedesktop/login1/session/test")};
    }
    Q_SCRIPTABLE QDBusObjectPath GetSession(const QString&) {
        ++requests_;
        return QDBusObjectPath{QStringLiteral("/org/freedesktop/login1/session/test")};
    }
    Q_SCRIPTABLE quint32 requests() const { return requests_; }

private:
    quint32 requests_{0};
};

class Session final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.login1.Session")
    Q_PROPERTY(QString Type READ type)
    Q_PROPERTY(QString State READ state)
    Q_PROPERTY(bool LockedHint READ lockedHint)
    Q_PROPERTY(bool Active READ active)

public:
    QString type() const { return QStringLiteral("wayland"); }
    QString state() const { return state_; }
    bool lockedHint() const { return lockedHint_; }
    bool active() const { return active_; }
    void setState(QString state, bool active) { state_ = std::move(state); active_ = active; }
    void setLockedHint(bool lockedHint) { lockedHint_ = lockedHint; }

private:
    QString state_{QStringLiteral("active")};
    bool lockedHint_{false};
    bool active_{true};
};

class FakeBluez final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.DBus.ObjectManager")

public:
    explicit FakeBluez(quint32* requests) : requests_(requests) {}

public slots:
    Q_SCRIPTABLE BluezObjects GetManagedObjects() const {
        ++*requests_;
        return {
            {QDBusObjectPath{QStringLiteral("/org/bluez/hci0")},
             {{QStringLiteral("org.bluez.Adapter1"), {{QStringLiteral("Powered"), true}}}}},
            {QDBusObjectPath{QStringLiteral("/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF")},
             {{QStringLiteral("org.bluez.Device1"), {{QStringLiteral("Address"), QStringLiteral("AA:BB:CC:DD:EE:FF")},
                                                       {QStringLiteral("Paired"), true}, {QStringLiteral("Trusted"), true},
                                                       {QStringLiteral("Connected"), false}}}}},
        };
    }

private:
    quint32* requests_;
};

class TestControl final : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.SmartLockerTest")

public:
    TestControl(Session* session, LoginManager* login, quint32* requests)
        : session_(session), login_(login), requests_(requests) {}

public slots:
    Q_SCRIPTABLE void SetSessionState(const QString& state, bool active) { session_->setState(state, active); }
    Q_SCRIPTABLE void SetLockedHint(bool locked) { session_->setLockedHint(locked); }
    Q_SCRIPTABLE quint32 Requests() const { return login_->requests() + *requests_; }
private:
    Session* session_;
    LoginManager* login_;
    quint32* requests_;
};

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    qDBusRegisterMetaType<BluezObjects>();
    if (argc != 2) return 2;
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) return 3;
    quint32 requests = 0;
    LoginManager login;
    Session session;
    FakeBluez bluez(&requests);
    TestControl control(&session, &login, &requests);
    if (!bus.registerObject(QStringLiteral("/org/freedesktop/login1"), &login,
                            QDBusConnection::ExportScriptableSlots)
        || !bus.registerObject(QStringLiteral("/org/freedesktop/login1/session/test"), &session,
                               QDBusConnection::ExportAllProperties)
        || !bus.registerObject(QStringLiteral("/"), &bluez, QDBusConnection::ExportScriptableSlots)
        || !bus.registerObject(QStringLiteral("/org/kde/SmartLockerTest"), &control,
                               QDBusConnection::ExportScriptableSlots)
        || !bus.registerService(QStringLiteral("org.freedesktop.login1"))
        || !bus.registerService(QStringLiteral("org.bluez"))) return 4;
    return app.exec();
}

#include "device_settings_fake_services.moc"
