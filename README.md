# Bluetooth SmartLocker

Lock-only Bluetooth presence daemon for KDE Plasma 6, Quickshell, and desktop environments. When every configured device is absent for the configured duration, it requests a session lock with `loginctl lock-session`. Bluetooth presence never unlocks the session.

## Safety model

- No automatic unlock path exists.
- Startup/controller failures do not immediately lock.
- Runtime absence locks only after the configured grace period.
- An unavailable Bluetooth adapter never *arms* a new absence countdown: an unpowered radio reports no presence, so switching Bluetooth off at the desk cannot lock the session. Pass `--lock-when-bluetooth-off` to lock on that evidence anyway. An absence countdown that was already running when the adapter dropped still completes, because the device was observed absent before the radio went away.
- A failed lock command is retried after the away duration; a background verifier re-arms locking if the session never actually locks. An unverifiable session counts as "not proven locked" rather than as success, so a lock that silently failed cannot leave the screen unlocked forever.
- Mutating D-Bus calls (`SetEnabled`, `Snooze`, device toggles, RSSI thresholds) are rejected while the session is locking or locked.
- Global and per-device disable state is persisted with `QSettings`.
- RSSI thresholds support averaging and hysteresis; stale RSSI never keeps a disconnected device "present".
- `InterfacesRemoved` from BlueZ is treated as device absence.

## Configure a device

Configure stable Bluetooth addresses; the daemon selects the adapter and resolves each address to its current BlueZ object path. Accepted formats include colon-separated, hyphen-separated, and unseparated addresses. Existing BlueZ object paths remain supported for compatibility.

```sh
busctl --system tree org.bluez  # optional diagnostic only
```

Start the daemon with one or more device paths:

```sh
kde-bluetooth-smartlocker \
  --device AA:BB:CC:DD:EE:FF \
  --away-seconds 30 \
  --rssi-threshold -70
```

Available options:

| Option | Default | Meaning |
| --- | --- | --- |
| `--device DEVICE` | – | Bluetooth address or legacy BlueZ object path to watch (repeatable). With no specs, automatically watches all paired or trusted devices. |
| `--away-seconds N` | `30` | Absence duration before locking |
| `--snooze-seconds N` | `30` | Maximum snooze duration |
| `--resume-grace-seconds N` | `30` | Post-resume lock grace duration |
| `--minimum-present N` | `1` | Required present devices for the policy |
| `--rssi-threshold DBM` | `-70` | Per-device RSSI threshold (must be within -100..0) |
| `--rssi-hysteresis DB` | `5` | RSSI hysteresis in dB |
| `--rssi-samples N` | `3` | RSSI averaging window size |
| `--lock-command CMD` | `loginctl` | Command used to lock the session |
| `--prelock-notify` | off | Notify when the away countdown starts |
| `--lock-when-bluetooth-off` | off | Lock after the away duration even when the Bluetooth adapter is unavailable |

The daemon also accepts a semicolon-separated `SMARTLOCKER_DEVICES` environment variable. The installed user service reads `%h/.config/kde-bluetooth-smartlocker/service.conf`, for example:

```ini
SMARTLOCKER_DEVICES=C0:1C:6A:75:9C:31;40:92:1A:53:8F:23
```

With no `--device` options and an empty or unset `SMARTLOCKER_DEVICES`, the daemon dynamically watches every BlueZ device marked `Paired` or `Trusted`. An address not currently present in BlueZ is retained as configured but treated as absent; it does not cause startup failure. `Devices()` and per-device settings use canonical uppercase colon-separated MAC addresses, which remain stable if BlueZ renumbers adapters.

Settings are stored by `QSettings` under `$XDG_CONFIG_HOME/kde-bluetooth-smartlocker`. The packaged user service only grants write access to `%h/.config/kde-bluetooth-smartlocker`, so a customised `XDG_CONFIG_HOME` leaves the config directory read-only inside the service sandbox and settings will not persist; the daemon logs a warning when a write fails.

## NixOS

```nix
{
  imports = [ inputs.kde-bluetooth-smartlocker.nixosModules.default ];
  services.kdeBluetoothSmartlocker = {
    enable = true;
    devices = [ "AA:BB:CC:DD:EE:FF" ];
    awaySeconds = 30;
  };
}
```

## Quickshell

The native QML extension plugin `org.kde.smartlocker` only depends on Qt 6 Core, DBus, and Qml, making it directly usable in [Quickshell](https://outfoxxed.me/quickshell/) without any KDE Frameworks or Plasma dependencies.

An example standalone floating window configuration is provided at `examples/quickshell/shell.qml`:

```sh
quickshell -p examples/quickshell
```

See [Quickshell Integration](docs/quickshell.md) for details on importing and using `SmartLockerClient` in custom widgets.

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

A pre-commit hook (`.githooks/pre-commit`, which invokes `scripts/check-version.sh`) aborts any commit where these disagree. The hook only runs if git's `core.hooksPath` points at `.githooks`, and that setting lives in the local `.git/config` of each clone, where git cannot track it. Enable the hook per clone with:

```sh
git config core.hooksPath .githooks
```

That local setting overrides any global hooks path. Regardless of the hook, CI runs `./scripts/check-version.sh` on every push and pull request, so a version mismatch fails the build either way. When bumping the version, update all four sources in the same commit.

Run the checker manually with:

```sh
./scripts/check-version.sh
```

## Status

The lock-only daemon, BlueZ monitoring, RSSI policy, D-Bus API (with a session-locked mutation gate), lock retry and verification, graphical Plasma controls, CMake build, Nix package, and NixOS module are implemented and covered by automated tests. The lock path is exercised end to end by a test that runs the real daemon against a recording lock command, so no session is ever locked. A real Plasma-session lock test and rendered Plasma visual QA remain deployment checks rather than automated build tests.
