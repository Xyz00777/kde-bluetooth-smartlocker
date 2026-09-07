# KDE Bluetooth SmartLocker

KDE Plasma 6 lock-only Bluetooth presence daemon. When every configured device is absent for the configured duration, it requests a session lock with `loginctl lock-session`. Bluetooth presence never unlocks the session.

## Safety model

- No automatic unlock path exists.
- Startup/controller failures do not immediately lock.
- Runtime absence locks only after the configured grace period.
- A failed lock command is retried after the away duration; a background verifier re-arms locking if the session never actually locks.
- Mutating D-Bus calls (`SetEnabled`, `Snooze`, device toggles, RSSI thresholds) are rejected while the session is locking or locked.
- Global and per-device disable state is persisted with `QSettings`.
- RSSI thresholds support averaging and hysteresis; stale RSSI never keeps a disconnected device "present".
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

Available options:

| Option | Default | Meaning |
| --- | --- | --- |
| `--device PATH` | – | BlueZ device object path to watch (repeatable) |
| `--away-seconds N` | `30` | Absence duration before locking |
| `--snooze-seconds N` | `30` | Maximum snooze duration |
| `--resume-grace-seconds N` | `30` | Post-resume lock grace duration |
| `--minimum-present N` | `1` | Required present devices for the policy |
| `--rssi-threshold DBM` | `-70` | Per-device RSSI threshold (must be within -100..0) |
| `--rssi-hysteresis DB` | `5` | RSSI hysteresis in dB |
| `--rssi-samples N` | `3` | RSSI averaging window size |
| `--lock-command CMD` | `loginctl` | Command used to lock the session |
| `--prelock-notify` | off | Notify when the away countdown starts |

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

The NixOS module passes the absolute `systemd` `loginctl` path via `--lock-command`, validates the RSSI threshold range and device paths at build time, and installs the daemon as a hardened user unit (`NoNewPrivileges`, bounded restarts).

## Build and test

```sh
nix build .# --no-link
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

The project requires Qt 6 Core, DBus, and Qml. `clangd` diagnostics are optional; the compiler uses `-Wall -Wextra -Wpedantic -Werror`. The test suite covers the state machine (lock latches, snooze, resume grace, RSSI hysteresis), the D-Bus API surface, and the CLI validation.

## Runtime troubleshooting

Inspect the user service and logs:

```sh
systemctl --user status kde-bluetooth-smartlocker
journalctl --user -u kde-bluetooth-smartlocker -f
```

The daemon logs lock-command failures, invalid startup paths, and duplicate devices through the `org.kde.smartlocker` logging categories.

## Development

### Version consistency hook

The application version is defined in four places that must stay in sync:

- `CMakeLists.txt` — `project(... VERSION ...)`
- `flake.nix` — `version = "..."`
- `src/main.cpp` — `setApplicationVersion("...")`
- `plasmoid/metadata.json` — `"Version": "..."`

A pre-commit hook (`scripts/check-version.sh`, wired through `.githooks/pre-commit`) aborts any commit where these disagree. The repository sets `core.hooksPath` to `.githooks`, which overrides any global hooks path. When bumping the version, update all four sources in the same commit.

Run the checker manually with:

```sh
./scripts/check-version.sh
```

## Status

The lock-only daemon, BlueZ monitoring, RSSI policy, D-Bus API (with a session-locked mutation gate), lock retry and verification, graphical Plasma controls, CMake build, Nix package, and NixOS module are implemented and covered by automated tests. A real Plasma-session lock test and rendered Plasma visual QA remain deployment checks rather than automated build tests.
