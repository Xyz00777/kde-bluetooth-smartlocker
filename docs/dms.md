# DankMaterialShell Integration

`kde-bluetooth-smartlocker` ships a [DankMaterialShell](https://danklinux.com/) (DMS) plugin that shows the daemon state and exposes its controls in a DankBar widget.

The plugin is pure QML. It talks to the daemon over session D-Bus using `busctl`, and it does **not** import or load the native `org.kde.smartlocker` QML module. That means it works even when DMS is built against a different Qt minor version than Plasma.

## Requirements

- DankMaterialShell **1.5 or newer** (tested against 1.6.2 plugin discovery).
- The `kde-bluetooth-smartlocker` daemon running in the same user session, normally `systemctl --user status kde-bluetooth-smartlocker`.
- `busctl` (provided by systemd) on `PATH`.
- The plugin files installed under a DMS plugin directory. The package ships them at `share/kde-bluetooth-smartlocker/dms-plugin`.

## NixOS install

Import the module and leave `installDmsPlugin` at its default (`true`):

```nix
{
  imports = [ inputs.kde-bluetooth-smartlocker.nixosModules.default ];
  services.kdeBluetoothSmartlocker = {
    enable = true;
    devices = [ "AA:BB:CC:DD:EE:FF" ];
    # Default true: exposes the plugin at
    # /etc/xdg/quickshell/dms-plugins/bluetoothSmartLocker, where DMS 1.6.2
    # discovers system-wide plugins.
    installDmsPlugin = true;
  };
}
```

DMS hardcodes `/etc/xdg/quickshell/dms-plugins` and does not search `XDG_CONFIG_DIRS`, so the module uses `environment.etc` to place the plugin there. If you install the same plugin through DMS's own home-manager or NixOS plugin option, set `installDmsPlugin = false` to avoid an `/etc` collision.

With a manual (non-Nix) install, copy `dms-plugin/` to `~/.config/DankMaterialShell/plugins/bluetoothSmartLocker` (the per-user plugin directory) or to the system-wide directory above.

## Enable the plugin

1. Open **DMS Settings → Plugins**.
2. Enable **"Bluetooth SmartLocker"**.
3. Add the widget to a DankBar from the bar's widget picker.

DMS 1.6.2 has no manifest flag that auto-enables bar widgets; the enabled state is stored per user in `plugin_settings.json`. Enabling from a terminal is equivalent:

```sh
dms ipc call plugins enable bluetoothSmartLocker
```

## Behavior and states

The widget mirrors the daemon state:

| State | Meaning |
| --- | --- |
| `starting` | The daemon is initializing. |
| `monitoring` | At least one configured device is present; the session stays unlocked. |
| `away` | Every configured device is absent and the away countdown is running. |
| `snoozed` | The user temporarily suppressed locking. |
| `locked` | The daemon has requested and verified a session lock. |
| `disabled` | Locking is disabled globally. |
| `error` | The daemon reported an error; check the logs. |
| `unavailable` | The widget cannot reach the daemon (not running or not on the bus). |

Presence never unlocks the session. Locking is one-way, and a failed lock command is retried until the session actually locks.

Each watched device has independent **BLE** and **Connection** switches and RSSI thresholds. BLE advertisements use the BLE range slider; a connected device's signal uses the separate connection strength slider. Each slider retains that source's latest RSSI marker, while its handle remains the configured threshold. A device counts as present when either enabled source meets its own threshold. Advertisement RSSI older than 10 seconds expires.

## Settings

The plugin exposes a small set of widget settings in DMS:

| Setting | Type | Default | Meaning |
| --- | --- | --- | --- |
| `showLabel` | bool | `true` | Show the short state label next to the Bluetooth icon in the bar. |
| `showThresholds` | bool | `true` | Show the per-device BLE range and connection strength sliders in the popout. |
| `refreshIntervalSec` | int | `2` | How often the widget polls the daemon over `busctl`, in seconds (2–30), including the latest RSSI marker. |

These are widget preferences only. Per-device BLE/connection switches and both RSSI thresholds are saved by the daemon and can be changed in each device row. The watched devices, away duration, and default RSSI thresholds remain daemon settings, for example through the NixOS module or the daemon CLI.

## Lock-only safety

The DMS plugin is a status and control surface, not an alternative lock runtime. It never adds an unlock path and never adds a manual lock button. All locking decisions stay in the daemon, which uses `loginctl lock-session` and verifies the result.

## Troubleshooting

Query the daemon state directly:

```sh
busctl --user call org.kde.SmartLocker1 /SmartLocker org.kde.SmartLocker1 State
```

If that fails, confirm the daemon is running and its service is registered:

```sh
systemctl --user status kde-bluetooth-smartlocker
systemctl --user status org.kde.SmartLocker1
journalctl --user -u kde-bluetooth-smartlocker -f
```

After changing plugin files on disk, ask DMS to reload the plugin:

```sh
dms ipc call plugins reload bluetoothSmartLocker
```

If the plugin does not appear in DMS Settings, verify the installation path (`/etc/xdg/quickshell/dms-plugins/bluetoothSmartLocker` for the NixOS module) and check the DMS logs:

```sh
journalctl --user -u dankmaterialshell -f
```

Log names vary by launch method; if the unit is not named `dankmaterialshell`, inspect the process output or your compositor's log.
