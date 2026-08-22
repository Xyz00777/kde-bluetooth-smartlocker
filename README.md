# KDE Bluetooth SmartLocker

KDE Plasma 6 lock-only Bluetooth presence daemon. When every configured device is absent for the configured duration, it requests a session lock with `loginctl lock-session`. Bluetooth presence never unlocks the session.

## Safety model

- No automatic unlock path exists.
- Startup/controller failures do not immediately lock.
- Runtime absence locks only after the configured grace period.
- Global and per-device disable state is persisted with `QSettings`.
- RSSI thresholds support averaging and hysteresis.
- `InterfacesRemoved` from BlueZ is treated as device absence.

## Configure a device

Find paired BlueZ object paths:

```sh
busctl --system tree org.bluez
```

Start the daemon with one or more device paths:

```sh
kde-bluetooth-smartlocker \
  --device /org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF \
  --away-seconds 30 \
  --rssi-threshold -70
```

The daemon also accepts a semicolon-separated `SMARTLOCKER_DEVICES` environment variable. The installed user service reads `%h/.config/kde-bluetooth-smartlocker/service.conf`, for example:

```ini
SMARTLOCKER_DEVICES=/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF
```

## NixOS

```nix
{
  imports = [ inputs.kde-bluetooth-smartlocker.nixosModules.default ];
  services.kdeBluetoothSmartlocker = {
    enable = true;
    devices = [ "/org/bluez/hci0/dev_AA_BB_CC_DD_EE_FF" ];
    awaySeconds = 30;
  };
}
```

## Build and test

```sh
nix build .# --no-link
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

The project requires Qt 6 Core, DBus, and Qml. `clangd` diagnostics are optional; the compiler uses `-Wall -Wextra -Wpedantic -Werror`.

## Runtime troubleshooting

Inspect the user service and logs:

```sh
systemctl --user status kde-bluetooth-smartlocker
journalctl --user -u kde-bluetooth-smartlocker -f
```

The daemon logs lock-command failures, invalid startup paths, and duplicate devices through the `org.kde.smartlocker` logging categories.

## Status

The lock-only daemon, BlueZ monitoring, RSSI policy, D-Bus API, graphical Plasma controls, CMake build, Nix package, and NixOS module are implemented. A real Plasma-session lock test, device picker, and rendered Plasma visual QA remain deployment checks rather than automated build tests.
