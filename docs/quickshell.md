# Quickshell Integration

`kde-bluetooth-smartlocker` provides first-class support for [Quickshell](https://outfoxxed.me/quickshell/).

Because the daemon communicates over session D-Bus and the native QML extension plugin `org.kde.smartlocker` only depends on Qt 6 Core, DBus, and Qml (without any KDE Frameworks or Plasma library dependencies), it can be used directly in Quickshell widgets, status bars, and floating panels.

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
// - smartLocker.deviceRssiThreshold(address)
// - smartLocker.setDeviceRssiThreshold(address, dbm)
// - smartLocker.deviceName(address)
// - smartLocker.refresh()
// - smartLocker.refreshDevices()
// - smartLocker.clearError()
```

## Notes on device getters

`deviceEnabled`, `deviceRssiThreshold` and `deviceName` read an in-memory cache and never
perform blocking D-Bus calls, so they are safe to call from a property binding. The cache is
filled asynchronously by `refresh()`, which also emits `settingsChanged`; re-read the getters
from a handler on `settingsChanged` if you need to react to daemon-side changes such as live
device-name updates. Before the first reply arrives, the getters return the daemon's defaults
(`true`, `-70`, and an empty name) rather than blocking.
