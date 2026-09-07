# PROJECT KNOWLEDGE BASE

**Generated:** 2026-09-07T08:12:45Z
**Commit:** 5459d75
**Branch:** main

## OVERVIEW
Lock-only Bluetooth presence daemon for KDE Plasma 6. Watches BlueZ devices over system D-Bus; when every configured device is absent for the away duration, it requests a session lock via `loginctl lock-session`. **Bluetooth presence never unlocks.** Stack: C++20, Qt6 (Core/DBus/Qml), CMake, Nix.

## STRUCTURE
```
kde-bluetooth-smartlocker/
├── src/            # C++ impl: main, daemon, bluez_monitor, state_machine, qml_plugin
├── include/smartlocker/  # public headers, 1:1 with src/
├── plasmoid/       # Plasma 6 package: QML UI + native QML plugin module
├── tests/          # single custom-harness test executable
├── systemd/        # user-service template (CMake-configured)
├── scripts/        # version-consistency checker
├── flake.nix       # Nix package + dev shell + inline NixOS module
└── CMakeLists.txt  # sole build definition (no per-dir CMake)
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Daemon entry / CLI | `src/main.cpp` | `QCoreApplication`, arg parsing, D-Bus registration |
| Lock policy logic | `src/state_machine.cpp` | pure C++20, no Qt; unit-tested |
| D-Bus service + lock exec | `src/daemon.cpp` | `org.kde.SmartLocker1`, QSettings, lock retry/verify |
| BlueZ monitoring | `src/bluez_monitor.cpp` | system D-Bus, RSSI caching, InterfacesRemoved |
| Plasma UI | `plasmoid/contents/ui/` | `main.qml`, `DevicePolicyRow.qml` |
| QML↔daemon bridge | `src/qml_plugin.cpp` | `SmartLockerClient` D-Bus client |
| Tests | `tests/state_machine_test.cpp` | custom harness, not QTest |
| Packaging / NixOS module | `flake.nix` | inline module, hardened user unit |

## CODE MAP
| Symbol | Type | Location | Refs | Role |
|--------|------|----------|------|------|
| `StateMachine` | class | `include/smartlocker/state_machine.hpp` | 16 | pure policy engine; `advanceTo()` returns `Action::Lock` |
| `Daemon` | class | `include/smartlocker/daemon.hpp` | — | D-Bus service object; owns machine, monitor, timers, lock process |
| `BluezMonitor` | class | `include/smartlocker/bluez_monitor.hpp` | — | BlueZ presence/RSSI observer |
| `SmartLockerClient` | class | `src/qml_plugin.cpp` | — | QML-facing D-Bus client |
| `SmartLockerPlugin` | class | `src/qml_plugin.cpp` | — | registers `SmartLockerClient` QML type |
| `main()` | func | `src/main.cpp` | — | boot: parse → Daemon → register D-Bus → exec |

## CONVENTIONS
- **C++20**, `-Wall -Wextra -Wpedantic -Werror` on every target. No `.clang-format`/`.editorconfig`/`.clang-tidy` — warnings-as-errors is the only style gate.
- **No KDE Frameworks / ECM / KConfig / i18n.** Plain Qt6 Core/DBus/Qml. Persistence via `QSettings` (`kde-bluetooth-smartlocker/daemon`).
- **Flat `src/`** — no per-module subdirs; headers under `include/smartlocker/` mirror `src/` 1:1.
- **Two runtime entry paths**, not one GUI app: headless daemon (`main.cpp`, systemd) + Plasma QML frontend (`main.qml` via `SmartLockerClient`).
- **Version defined in 4 places** that must stay in sync: `CMakeLists.txt`, `flake.nix`, `src/main.cpp`, `plasmoid/metadata.json`. Pre-commit hook (`scripts/check-version.sh`) enforces it.
- **Every change bumps the version** — no change lands without a version increase. Features/behavior changes → minor bump (`0.1.0` → `0.2.0`); bugfixes → patch bump (`0.1.0` → `0.1.1`). Bump all 4 places in the same commit.
- `Q_LOGGING_CATEGORY` under `org.kde.smartlocker` for daemon, `org.kde.smartlocker.main` for main.

## ANTI-PATTERNS (THIS PROJECT)
- **Never implement automatic unlock.** Lock-only is a hard invariant.
- **Never trust stale RSSI** after a previously-connected device disconnects (BlueZ retains last RSSI).
- **Never lock on startup/controller failure** — wait/retry, don't lock.
- **Never use Plasma-private APIs to lock** — always `loginctl lock-session`.
- **Never delete declarative devices through the UI** — device lists only extend.
- **Never convert unexpected errors into absence.**
- **Never ship a duplicate in-process runtime** — QML uses a thin D-Bus client only.
- **Never add a manual lock-screen button** to the UI.

## UNIQUE STYLES
- State machine is **pure C++20** (no Qt) for testability; `TimePoint = steady_clock`.
- `sessionLocked()` treats both logind `locking` and `locked` as mutation-blocked.
- Lock command is verified post-run; failure to actually lock triggers retry.
- `InterfacesRemoved` from BlueZ = device absence.

## COMMANDS
```bash
# Build + test (CMake)
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure

# Nix (CI runs this)
nix build .# --no-link

# Dev shell (sets QML_IMPORT_PATH, QT_PLUGIN_PATH)
nix develop

# Version consistency check
./scripts/check-version.sh
```

## NOTES
- `qmllint` cannot auto-discover QML import paths (nixpkgs #31725); pass `-I` explicitly. Known diagnostics (`SmartLockerClient was not found`, `Unused import`, `org.kde.smartlocker`) are filtered; genuine `Failed to import` fails the build.
- `qmlimportscanner` moved to `libexec/` in qtdeclarative 6.11.1; add it to `PATH` in the dev shell.
- If `PrepareForSleep` subscription fails, resume grace does not apply (logged warning).
- Persisted RSSI thresholds are range-checked and repaired on load.
- Real Plasma-session lock test and rendered Plasma visual QA are deployment checks, not automated tests.
- `result` is a dangling Nix symlink; `.codegraph/`, `.omo/`, `ses_*` are agent artifacts — ignore for structure.
