# tests/ — Custom Test Harness

## OVERVIEW
A single hand-written C++ test executable (`state_machine_test.cpp`) covering the pure policy engine. **Not QTest / QMLTest / GoogleTest** — a custom `require(...)` aborting macro with deterministic `TimePoint` helpers and manually invoked `test...()` functions.

## WHERE TO LOOK
| Task | File | Notes |
|------|------|-------|
| State-machine unit tests | `state_machine_test.cpp` | lock latch, snooze, resume grace, RSSI hysteresis, disabled, device removal |

## CONVENTIONS
- Tests link only against the `smartlocker-state-machine` static library — the Qt-free module. This is why `state_machine.cpp` must stay Qt-free.
- Registered via CTest in `CMakeLists.txt` under `if(BUILD_TESTING)`.
- Deterministic time: tests pass explicit `TimePoint` values, never wall-clock.

## COVERAGE GAPS (known)
- `daemon.cpp` (D-Bus methods, QSettings, lock retry/verify, notifications) — **no direct tests**.
- `bluez_monitor.cpp` (BlueZ service, `PropertiesChanged`, `InterfacesRemoved`, RSSI cache) — **no fake-bus tests**.
- `qml_plugin.cpp` + both QML files — **no QMLTest/UI automation**.
- CLI tests mostly invoke `--version`; they validate parsing, not runtime behavior.
- Real Plasma-session lock and rendered visual QA are **deployment checks**, not automated tests.

## COMMANDS
```bash
# Run all CTest tests (incl. state-machine-test + CLI validation)
ctest --test-dir build --output-on-failure

# Run just the harness
./build/state-machine-test
```
