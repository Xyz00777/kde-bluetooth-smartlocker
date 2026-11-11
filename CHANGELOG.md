# Changelog

## 0.19.0

Extend the lock-only Bluetooth presence daemon with a portable x86_64 AppImage and user-scoped desktop integration. The AppImage includes the daemon, Qt/QML runtime, DMS plugin, and Qt-matched Plasma integrations, with a per-user installer for the application and systemd user service.

The host remains responsible for BlueZ, D-Bus, logind/`loginctl`, and the desktop shell. Plasma native QML plugins must match the host's Qt minor version; the DMS plugin remains pure QML. Bluetooth presence continues to request locking only; it never unlocks a session.
