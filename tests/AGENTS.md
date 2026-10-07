# tests/ — Custom Test Harness

## OVERVIEW
Two hand-written C++ test executables: `state_machine_test.cpp` covering the pure policy engine, and `bluez_monitor_test.cpp` covering BlueZ presence over a private fake D-Bus bus. **Not QTest / QMLTest / GoogleTest** — a custom `require(...)` aborting macro with deterministic `TimePoint` helpers and manually invoked `test...()` functions; the BlueZ harness uses `QSignalSpy` and `check(...)` assertions that print every failure and exit non-zero.

## WHERE TO LOOK
| Task | File | Notes |
|------|------|-------|
| State-machine unit tests | `state_machine_test.cpp` | lock latch, snooze, resume grace, RSSI hysteresis, disabled, device removal |
| BlueZ monitor tests | `bluez_monitor_test.cpp` | duplicate-path presence union, duplicate-observation suppression, vanished-device pruning, empty-Alias fallback, failed-reply handling |

## CONVENTIONS
- Tests link only against the `smartlocker-state-machine` static library — the Qt-free module. This is why `state_machine.cpp` must stay Qt-free.
- Registered via CTest in `CMakeLists.txt` under `if(BUILD_TESTING)`.
- Deterministic time: tests pass explicit `TimePoint` values, never wall-clock.

## COVERAGE GAPS (known)
- `daemon.cpp` (D-Bus methods, QSettings, lock retry/verify, notifications) — **no direct tests**.
- `bluez_monitor.cpp` `PropertiesChanged` and `InterfacesRemoved` signal handling — only the periodic enumeration path is covered; the signal-driven paths are not.
- `qml_plugin.cpp` + both QML files — **no QMLTest/UI automation**.
- CLI tests mostly invoke `--version`; they validate parsing, not runtime behavior.
- Real Plasma-session lock and rendered visual QA are **deployment checks**, not automated tests.

## COMMANDS
```bash
# Run all CTest tests (incl. state-machine-test + CLI validation)
ctest --test-dir build --output-on-failure

# Run just the harnesses
./build/state-machine-test
./build/bluez-monitor-test
```
