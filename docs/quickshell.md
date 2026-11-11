# Quickshell Integration

`kde-bluetooth-smartlocker` provides first-class support for [Quickshell](https://outfoxxed.me/quickshell/).

Because the daemon communicates over session D-Bus and the native QML extension plugin `org.kde.smartlocker` only depends on Qt 6 Core, DBus, and Qml (without any KDE Frameworks or Plasma library dependencies), it can be used directly in Quickshell widgets, status bars, and floating panels.

This guide covers the **generic Quickshell** integration. `org.kde.smartlocker` is a native QML plugin, so the copy you import must be built with the same Qt minor version as the Quickshell build; mixing Qt minor versions can fail to load the module. If you use **DankMaterialShell (DMS)**, use the dedicated [DMS plugin](dms.md) instead: it is pure QML, talks to the daemon over D-Bus, and does not depend on the native module.

## AppImage Plasma integration and host boundary

The x86_64 AppImage's daemon runs with its bundled Qt 6.12 runtime. This is
separate from the native Plasma plugin, which supports **host Plasma/Qt 6.11.1
only**. Do not import the host plugin into a different Qt minor runtime. The
AppImage requires host BlueZ and a Bluetooth controller, session and system
D-Bus, a systemd user manager, logind with `loginctl lock-session`, and a
working graphical session/lock screen. It does not bundle those host services
or the Quickshell/DMS/Plasma shell.

For packaged Plasma use, run the AppImage installer with `--install
[--desktop auto|dms|plasma]`; remove it with `--uninstall`. It places the
AppImage under `$XDG_DATA_HOME/kde-bluetooth-smartlocker/`, creates the user
service at `$XDG_CONFIG_HOME/systemd/user/kde-bluetooth-smartlocker.service`,
and reads optional service environment assignments from
`%h/.config/kde-bluetooth-smartlocker/service.conf`. Plasma files and its Qt
6.11.1 module are installed under the user's XDG data paths. The installer
configures a Plasma QML import environment script; restart Plasma or log out and
back in before expecting that import path to be active. For DMS, use its pure-
QML plugin rather than loading a native Qt plugin; see [DMS Integration](dms.md).

## 1. Running the Example Configuration

An example standalone floating window configuration is provided at [`examples/quickshell/shell.qml`](../examples/quickshell/shell.qml).

Run it with:

```sh
quickshell -p examples/quickshell
```

Make sure `QML_IMPORT_PATH` includes the directory containing `org/kde/smartlocker` (for instance, when built with CMake, `<build-dir>/plasmoid/qml` or `<installed-prefix>/lib/qt-6/qml`).

## 2. Using `SmartLockerClient` in Custom Quickshell Widgets

In your Quickshell bar, tray, or menu QML files:

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import org.kde.smartlocker 1.0

SmartLockerClient {
    id: smartLocker
}

// Read properties:
// - smartLocker.state, one of: "starting", "monitoring", "away", "snoozed",
//   "locked", "disabled", "error", or "unavailable".
//   "unavailable" is client-side only: the daemon is not running or unreachable.
//   Treat any other value as unknown rather than assuming a healthy state.
// - smartLocker.devices (list of watched Bluetooth addresses)
// - smartLocker.snoozeSeconds (configured maximum snooze duration in seconds)
// - smartLocker.lastError (empty when there is no outstanding error; otherwise a
//   human-readable reason the daemon refused the last change)
//   Listen for the errorOccurred signal, and call clearError() to dismiss it.

// Call methods:
// - smartLocker.setEnabled(bool)
// - smartLocker.snooze(seconds)
// - smartLocker.deviceEnabled(address)
// - smartLocker.setDeviceEnabled(address, bool)
// - smartLocker.deviceBleEnabled(address)
// - smartLocker.setDeviceBleEnabled(address, bool)
// - smartLocker.deviceConnectionEnabled(address)
// - smartLocker.setDeviceConnectionEnabled(address, bool)
// - smartLocker.deviceRssiThreshold(address)
// - smartLocker.setDeviceRssiThreshold(address, dbm)
// - smartLocker.deviceConnectionRssiThreshold(address)
// - smartLocker.setDeviceConnectionRssiThreshold(address, dbm)
// - smartLocker.deviceBleRssiAvailable(address)
// - smartLocker.deviceBleRssiDbm(address)
// - smartLocker.deviceConnectionRssiAvailable(address)
// - smartLocker.deviceConnectionRssiDbm(address)
// - smartLocker.deviceName(address)
// - smartLocker.refresh()
// - smartLocker.refreshDevices()
// - smartLocker.clearError()
```

## Notes on device getters

`deviceEnabled`, `deviceBleEnabled`, `deviceConnectionEnabled`, both RSSI threshold getters, the BLE and connection RSSI availability/value getters, and `deviceName` read an in-memory cache and never
perform blocking D-Bus calls, so they are safe to call from a property binding. The cache is
filled asynchronously by `refresh()`, which also emits `settingsChanged`; re-read the getters
from a handler on `settingsChanged` if you need to react to daemon-side changes such as live
device-name updates. Before the first reply arrives, the getters return the daemon's defaults
(`true`, `true`, `true`, `-70`, `-70`, no fresh BLE or connection RSSI, and an empty name) rather than blocking.
