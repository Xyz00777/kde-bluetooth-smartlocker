# Quickshell Integration

`kde-bluetooth-smartlocker` provides first-class support for [Quickshell](https://outfoxxed.me/quickshell/).

Because the daemon communicates over session D-Bus and the native QML extension plugin `org.kde.smartlocker` only depends on Qt 6 Core, DBus, and Qml (without any KDE Frameworks or Plasma library dependencies), it can be used directly in Quickshell widgets, status bars, and floating panels.

## 1. Running the Example Configuration

An example standalone floating window configuration is provided at [`examples/quickshell/shell.qml`](./examples/quickshell/shell.qml).

Run it with:

```sh
quickshell -p examples/quickshell/shell.qml
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
// - smartLocker.state ("monitoring", "awaiting_absence", "locked", "disabled", "unavailable", etc.)
// - smartLocker.devices (list of watched Bluetooth addresses)

// Call methods:
// - smartLocker.setEnabled(bool)
// - smartLocker.snooze(seconds)
// - smartLocker.deviceEnabled(address)
// - smartLocker.setDeviceEnabled(address, bool)
// - smartLocker.deviceRssiThreshold(address)
// - smartLocker.setDeviceRssiThreshold(address, dbm)
// - smartLocker.refresh()
```
