# src/ — C++ Implementation

## OVERVIEW
All C++ source for the daemon, the pure policy engine, and the QML plugin. Headers live 1:1 under `include/smartlocker/` (same basename). No per-module subdirs.

## WHERE TO LOOK
| Task | File | Notes |
|------|------|-------|
| CLI parse + boot | `main.cpp` | `QCoreApplication`, option validation, D-Bus service registration |
| D-Bus service object | `daemon.cpp` | `org.kde.SmartLocker1`, QSettings, lock exec/retry/verify |
| Lock policy | `state_machine.cpp` | pure C++20, no Qt; the only unit-tested module |
| BlueZ presence | `bluez_monitor.cpp` | system D-Bus, RSSI cache, `InterfacesRemoved` |
| QML bridge | `qml_plugin.cpp` | `SmartLockerClient` + `SmartLockerPlugin` (thin D-Bus client) |

## CONVENTIONS
- **`state_machine.cpp` is Qt-free** — it must stay that way for the test harness. No `Q_*` types, no Qt includes; `TimePoint = std::chrono::steady_clock::time_point`.
- **`daemon.cpp` owns the Qt boundary**: timers, `QProcess`, `QSettings`, D-Bus. It translates Qt signals into `StateMachine` calls.
- **`qml_plugin.cpp` is a thin D-Bus client only** — never a duplicate in-process runtime. Every call goes over session D-Bus to the daemon.
- D-Bus slots are `Q_SCRIPTABLE`; mutating ones (`SetEnabled`, `Snooze`, device toggles, RSSI) must reject while `sessionLocked()`.
- Logging via `Q_LOGGING_CATEGORY`: `org.kde.smartlocker` (daemon), `org.kde.smartlocker.main` (main).

## ANTI-PATTERNS
- **Never add Qt to `state_machine.cpp`** — breaks the pure-C++ testability invariant.
- **Never trust stale RSSI** after a previously-connected device disconnects (`bluez_monitor.cpp` guards this).
- **Never lock on startup/controller failure** — wait/retry.
- **Never convert unexpected errors into absence.**
- **Never suppress warnings** — `-Werror` is on every target.

## UNIQUE STYLES
- `sessionLocked()` treats logind `locking` AND `locked` as mutation-blocked.
- Lock command is verified post-run (`verifyLockApplied`); failure to actually lock re-arms.
- `InterfacesRemoved` from BlueZ = device absence.
- Persisted RSSI thresholds are range-checked and repaired on load (`daemon.cpp`).
