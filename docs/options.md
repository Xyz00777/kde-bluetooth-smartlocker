# Options and integration reference

This page describes the options currently implemented by the daemon, its packaged
user service, NixOS module, QML client, and DankMaterialShell (DMS) widget. The
daemon is lock-only: Bluetooth presence never unlocks a session.

## Command-line options

The compiled daemon accepts the following options (also shown by
`kde-bluetooth-smartlocker --help`). Numeric defaults are supplied by
`src/main.cpp`; invalid values exit with status 2.

| Option | Default | Type / constraint | Description |
| --- | --- | --- | --- |
| `--help` | `off` | flag | Show help and exit. |
| `--version` | `off` | flag | Show application version and exit. |
| `--device` `DEVICE` | `none` | repeatable string | Bluetooth address or legacy BlueZ device object path. With no configured devices, dynamically watch every BlueZ `Device1` marked paired or trusted. |
| `--away-seconds SECONDS` | `30` | integer, >= 0 | Absence duration before locking. |
| `--snooze-seconds SECONDS` | `30` | integer, > 0 | Maximum snooze duration. |
| `--resume-grace-seconds SECONDS` | `30` | integer, >= 0 | Grace duration after resume. |
| `--minimum-present COUNT` | `1` | integer, > 0 | Number of configured devices that must be present. Cannot exceed an explicitly configured device count. |
| `--rssi-threshold DBM` | `-70` | integer, -100..0 | Per-device BLE RSSI threshold. |
| `--rssi-hysteresis DB` | `5` | integer, > 0 | RSSI hysteresis in dB. |
| `--rssi-samples COUNT` | `3` | integer, > 0 | RSSI averaging window size. |
| `--prelock-notify` | `off` | flag | Send an informational notification when the away countdown starts. |
| `--lock-command COMMAND` | `loginctl` | command string | Command used to request a session lock. The normal lock action invokes the command with `lock-session`. |
| `--lock-when-bluetooth-off` | `off` | flag | Allow Bluetooth adapter unavailability to count toward locking. Without it, an unavailable adapter does not arm a new absence countdown. |

Device addresses may be colon-separated, hyphen-separated, or unseparated; the
legacy BlueZ object path form remains accepted. CLI devices and environment
devices are combined. Duplicate canonical addresses are rejected.

## Environment, settings, and user service

`SMARTLOCKER_DEVICES` is an optional semicolon-separated list of Bluetooth
addresses or legacy BlueZ object paths. Empty entries are ignored. It supplements
repeatable `--device` arguments. With neither source configured, the daemon
dynamically follows paired or trusted BlueZ devices.

Persistent daemon settings use `QSettings` beneath
`$XDG_CONFIG_HOME/kde-bluetooth-smartlocker` (Qt's normal per-user config
location). The installed user service reads optional environment assignments
from `%h/.config/kde-bluetooth-smartlocker/service.conf`, for example:

```ini
SMARTLOCKER_DEVICES=C0:1C:6A:75:9C:31;40:92:1A:53:8F:23
```

The leading `-` on systemd's `EnvironmentFile=-...` makes a missing file
non-fatal. The service sandbox only grants writes to
`%h/.config/kde-bluetooth-smartlocker`. If `XDG_CONFIG_HOME` is customized to a
different location, settings may be read-only under the sandbox and fail to
persist; keep the QSettings directory within the allowed path or adjust the
service policy locally. This service runs in the graphical user session and
needs access to the session D-Bus, host BlueZ system D-Bus, and logind lock
operation. Bluetooth presence is never an unlock signal.

The packaged user unit has these directives/settings:

| Directive | Value | Purpose |
| --- | --- | --- |
| `[Unit] Description` | `KDE Bluetooth SmartLocker` | Unit description. |
| `[Unit] After` | `graphical-session.target` | Start after the graphical session target. |
| `[Unit] PartOf` | `graphical-session.target` | Follow the graphical session lifecycle. |
| `[Unit] StartLimitIntervalSec` | `10min` | Restart limit interval. |
| `[Unit] StartLimitBurst` | `10` | Maximum starts in the interval. |
| `[Service] Type` | `simple` | Foreground daemon process. |
| `[Service] ExecStart` | installed `kde-bluetooth-smartlocker` | Start the daemon. |
| `[Service] EnvironmentFile` | `-%h/.config/kde-bluetooth-smartlocker/service.conf` | Optional user environment file. |
| `[Service] Restart` | `on-failure` | Restart after failure. |
| `[Service] RestartSec` | `2` | Delay before restart. |
| `[Service] NoNewPrivileges` | `yes` | Prevent privilege gain through exec. |
| `[Service] CapabilityBoundingSet` | empty | Grant no additional capabilities. |
| `[Service] ProtectSystem` | `strict` | Make system filesystems read-only. |
| `[Service] ProtectHome` | `read-only` | Make home read-only except declared writes. |
| `[Service] ProtectKernelTunables` | `yes` | Protect kernel tunables. |
| `[Service] ProtectControlGroups` | `yes` | Protect control groups. |
| `[Service] RestrictSUIDSGID` | `yes` | Disallow SUID/SGID execution. |
| `[Service] RestrictRealtime` | `yes` | Disallow realtime scheduling. |
| `[Service] LockPersonality` | `yes` | Restrict process personality changes. |
| `[Service] MemoryDenyWriteExecute` | `yes` | Disallow writable-and-executable memory. |
| `[Service] RestrictAddressFamilies` | `AF_UNIX AF_NETLINK` | Limit address families to local IPC and netlink. |
| `[Service] ReadWritePaths` | `%h/.config/kde-bluetooth-smartlocker` | Writable settings directory. |
| `[Install] WantedBy` | `graphical-session.target` | Enable with the graphical user session. |

## NixOS module

Import `inputs.kde-bluetooth-smartlocker.nixosModules.default` and configure
`services.kdeBluetoothSmartlocker`. The module enables a user service and adds
the package to system packages when `enable = true`.

| Option | Type | Default | Meaning |
| --- | --- | --- | --- |
| `enable` | `bool` | `false` (disabled by `mkEnableOption`) | Enable the SmartLocker user service. |
| `package` | `lib.types.package` | `mkPackage pkgs` | Package built against the consuming system's nixpkgs / Qt. |
| `installDmsPlugin` | `lib.types.bool` | `true` | Install plugin under `/etc/xdg/quickshell/dms-plugins/bluetoothSmartLocker`. Set false if installed by another DMS integration. |
| `devices` | `lib.types.listOf lib.types.str` | `[]` | Bluetooth addresses or legacy BlueZ object paths to watch. |
| `awaySeconds` | `lib.types.ints.unsigned` | `30` | Absence duration before lock request. |
| `snoozeSeconds` | `lib.types.ints.positive` | `30` | Maximum snooze duration. |
| `resumeGraceSeconds` | `lib.types.ints.unsigned` | `30` | Post-resume grace duration. |
| `minimumPresent` | `lib.types.ints.positive` | `1` | Required present-device count. |
| `rssiThreshold` | `lib.types.ints.between (-100) 0` | `-70` | Per-device RSSI threshold in dBm. |
| `rssiHysteresis` | `lib.types.ints.positive` | `5` | RSSI hysteresis in dB. |
| `rssiSamples` | `lib.types.ints.positive` | `3` | RSSI averaging window size. |
| `prelockNotify` | `lib.types.bool` | `false` | Send notification when away countdown starts. |

`devices` accepts colon/hyphen/unseparated Bluetooth addresses and legacy
`/org/bluez/hciN/dev_XX_XX_XX_XX_XX_XX` paths; module assertions reject other
values. NixOS translates policy values to daemon CLI arguments and installs the
DMS plugin by default. Its user service uses the same restart and hardening
settings listed above.

## DMS preferences

The pure-QML DMS plugin exposes only widget preferences here; daemon policy and
per-device settings are controlled by the daemon/CLI/NixOS module.

| Preference | Type | Default | Range / meaning |
| --- | --- | --- | --- |
| `showLabel` | bool | `true` | Show the short state label next to the icon in the bar. |
| `showThresholds` | bool | `true` | Show the per-device BLE range and connection strength controls in the popout. |
| `refreshIntervalSec` | integer seconds | `2` | Poll interval, minimum `2`, maximum `30` seconds. |

## Native QML `SmartLockerClient` API

Import `org.kde.smartlocker 1.0`. This native plugin is built against a specific
Qt minor release and must be loaded by a Qt-compatible host.

Properties: `state` (string), `devices` (string list), `snoozeSeconds` (integer),
and `lastError` (string). Signals: `stateChanged`, `devicesChanged`,
`settingsChanged`, and `errorOccurred`. The client methods are:

| Method | Parameters | Result / behavior |
| --- | --- | --- |
| `clearError()` | — | Clear the displayed error. |
| `refresh()` | — | Refresh daemon state and settings asynchronously. |
| `refreshDevices()` | — | Refresh the device list asynchronously. |
| `setEnabled(enabled)` | bool | Enable or disable locking. |
| `snooze(seconds)` | integer | Snooze for the requested duration. |
| `setDeviceEnabled(path, enabled)` | string, bool | Enable/disable a device. |
| `setDeviceBleEnabled(path, enabled)` | string, bool | Enable/disable BLE presence for a device. |
| `setDeviceConnectionEnabled(path, enabled)` | string, bool | Enable/disable connection presence for a device. |
| `setDeviceRssiThreshold(path, thresholdDbm)` | string, integer | Set BLE RSSI threshold. |
| `setDeviceConnectionRssiThreshold(path, thresholdDbm)` | string, integer | Set connection RSSI threshold. |
| `deviceEnabled(path)` | string | Read device enabled state; initial fallback `true`. |
| `deviceBleEnabled(path)` | string | Read BLE enabled state; initial fallback `true`. |
| `deviceConnectionEnabled(path)` | string | Read connection enabled state; initial fallback `true`. |
| `deviceRssiThreshold(path)` | string | Read BLE RSSI threshold; initial fallback `-70` dBm. |
| `deviceConnectionRssiThreshold(path)` | string | Read connection RSSI threshold; initial fallback `-70` dBm. |
| `deviceBleRssiAvailable(path)` | string | Whether a fresh BLE RSSI value is available; initial fallback `false`. |
| `deviceBleRssiDbm(path)` | string | BLE RSSI in dBm; initial fallback `0`. |
| `deviceConnectionRssiAvailable(path)` | string | Whether a connection RSSI value is available; initial fallback `false`. |
| `deviceConnectionRssiDbm(path)` | string | Connection RSSI in dBm; initial fallback `0`. |
| `deviceName(path)` | string | Cached device name; initial fallback empty string. |

Getters are cache-only and safe in QML bindings; `refresh()` populates them
asynchronously and emits `settingsChanged`. Client-side `unavailable` indicates
the daemon is missing/unreachable. It never provides an unlock operation.

## AppImage and host requirements

The x86_64 AppImage bundles the daemon's **Qt 6.12 runtime**. This is separate
from the only supported host Plasma integration: its native QML plugin is built
for **host Plasma/Qt 6.11.1 only**. Do not load the Qt 6.11.1 native plugin into
the AppImage's Qt 6.12 runtime or into a different host Qt version. The DMS
plugin is pure QML and uses host `busctl` over D-Bus, so it is not coupled to
the native plugin's Qt minor version.

### Host requirements

The AppImage is not a replacement for host services or a desktop session. The
host must provide:

- BlueZ on the system D-Bus and the host's Bluetooth controller.
- A working session D-Bus and systemd user manager (`systemctl --user`).
- logind support and a usable `loginctl lock-session` operation for the active
  session.
- A supported graphical session/lock screen. The host must provide a desktop
  shell; DMS installs require the DMS runtime, while the Plasma session must
  use Qt 6.11.1.
- `busctl` for the DMS pure-QML widget.

The AppImage does not bundle BlueZ, the controller, the D-Bus daemon, logind,
`loginctl`, the Plasma session, or the DMS runtime. CI tests cannot establish that real hardware, a desktop shell,
or a real session lock works; verifying those behaviors is deployment-only.

### Install and uninstall

Run the downloaded AppImage explicitly; `auto` detects DMS or Plasma:

```sh
./kde-bluetooth-smartlocker-*.AppImage --install [--desktop auto|dms|plasma]
./kde-bluetooth-smartlocker-*.AppImage --uninstall
```

Installation is per-user and uses stable paths:

| Item | Path (XDG default) | Purpose |
| --- | --- | --- |
| AppImage | `$XDG_DATA_HOME/kde-bluetooth-smartlocker/kde-bluetooth-smartlocker.AppImage` (`~/.local/share`) | Installed executable used by the service. |
| systemd user unit | `$XDG_CONFIG_HOME/systemd/user/kde-bluetooth-smartlocker.service` (`~/.config/systemd/user`) | Starts the daemon with the graphical session. |
| service environment | `%h/.config/kde-bluetooth-smartlocker/service.conf` | Optional environment file; set `SMARTLOCKER_DEVICES` here. |
| DMS plugin | `$XDG_DATA_HOME/quickshell/dms-plugins/bluetoothSmartLocker` | Pure-QML widget files. |
| Plasma plasmoid | `$XDG_DATA_HOME/plasma/plasmoids/org.kde.smartlocker` | Plasma widget files. |
| Plasma QML module | `$XDG_DATA_HOME/qt6/qml/org/kde/smartlocker` | Qt 6.11.1 native plugin and `qmldir`. |

The installer enables and starts `kde-bluetooth-smartlocker.service` in the
user manager. Service environment assignments belong in `service.conf`, for
example `SMARTLOCKER_DEVICES=AA:BB:CC:DD:EE:FF`. The installer records owned
paths; uninstall removes those paths and its unit/plugin enablement but retains
`service.conf` and daemon settings in `~/.config/kde-bluetooth-smartlocker/`.
Plasma's QML import environment is supplied by
`~/.config/plasma-workspace/env/smartlocker-qml.sh`; restart Plasma or log out
and back in after installation for the import path to take effect.

## Versioning, changelog, and required checks

Changes to software or package inputs require a strictly higher SemVer version
and a corresponding `CHANGELOG.md` entry headed `## <new-version>` (for
example, `## 0.19.0`). The release entry follows the heading and summarizes the
software change. Documentation-only and CI-only changes (as well as test-only
and checker-script-only changes) are exempt from the application
version/changelog requirement; a mixed
software plus documentation/CI change still requires both the higher version
and changelog entry. Keep the five application version declarations in sync as
described in the README.

For repository branch protection, configure **`CI / required-checks`** as a
required status check on the protected branch. GitHub Actions workflows cannot
enable branch protection or make a check required; a repository administrator
must set this in the branch protection/ruleset settings. The CI job does not
replace real deployment verification: hardware presence and actual desktop
session locking must be checked on a supported host.
