# tests/ — Custom Test Harness

## OVERVIEW
Two hand-written C++ test executables: `state_machine_test.cpp` covering the pure policy engine, and `bluez_monitor_test.cpp` covering BlueZ presence over a private fake D-Bus bus. **Not QTest / QMLTest / GoogleTest** — a custom `require(...)` aborting macro with deterministic `TimePoint` helpers and manually invoked `test...()` functions; the BlueZ harness uses `QSignalSpy` and `check(...)` assertions that print every failure and exit non-zero.

## WHERE TO LOOK
| Task | File | Notes |
|------|------|-------|
| State-machine unit tests | `state_machine_test.cpp` | lock latch, snooze, resume grace, RSSI hysteresis, disabled, device removal, Bluetooth-off policy (default stop vs `--lock-when-bluetooth-off` opt-in, and countdown preservation) |
| BlueZ monitor tests | `bluez_monitor_test.cpp` | `PropertiesChanged`, `InterfacesAdded`/`InterfacesRemoved`, duplicate-path union, stale RSSI, name pruning, empty-Alias fallback, failed replies, configured-but-absent devices |
| Lock verification | `lock_verification_test.cpp` | the pure fail-open rule in `lockIsVerified()` |
| Lock execution | `lock_path_test.sh` | runs the real daemon to the lock path with a recording lock command |

## CONVENTIONS
- Tests link only against the `smartlocker-state-machine` static library — the Qt-free module. This is why `state_machine.cpp` must stay Qt-free.
- Registered via CTest in `CMakeLists.txt` under `if(BUILD_TESTING)`.
- Deterministic time: tests pass explicit `TimePoint` values, never wall-clock.

## COVERAGE GAPS (known)
- `daemon.cpp` D-Bus methods, QSettings persistence, and notifications — **no direct tests**. The fail-open lock verification rule is covered via the pure `lockIsVerified()`, but the Qt code that calls it is not.
- `qml_plugin.cpp` + both QML files — **no QMLTest/UI automation**.
- CLI tests mostly invoke `--version`; they validate parsing, not runtime behavior.
- `lock_path_test.sh` needs a real `org.bluez` on the **system** bus with a powered adapter, so it skips where BlueZ is absent.
- Real Plasma-session lock and rendered visual QA are **deployment checks**, not automated tests.

## COMMANDS
```bash
# Run all CTest tests (incl. state-machine-test + CLI validation)
ctest --test-dir build --output-on-failure

# Run just the harnesses
./build/state-machine-test
./build/bluez-monitor-test
```
