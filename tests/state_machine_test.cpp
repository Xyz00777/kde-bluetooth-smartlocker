#include "smartlocker/state_machine.hpp"

#include <chrono>
#include <cstdlib>

using namespace std::chrono_literals;
using smartlocker::Action;
using smartlocker::DeviceConfiguration;
using smartlocker::DeviceId;
using smartlocker::DeviceObservation;
using smartlocker::MachineState;
using smartlocker::StateMachine;
using smartlocker::StateMachineConfiguration;
using smartlocker::TimePoint;

namespace {

TimePoint at(const int seconds) {
    return TimePoint{} + std::chrono::seconds{seconds};
}

void require(const bool condition) {
    if (!condition) {
        std::abort();
    }
}

StateMachineConfiguration configuredForOneDevice() {
    return StateMachineConfiguration{
        .awayDuration = 10s,
        .snoozeDurationCap = 30s,
        .postResumeGrace = 30s,
        .minimumPresentDevices = 1,
        .devices = {DeviceConfiguration{
            .id = DeviceId{"phone"},
            .rssiThresholdDbm = -70,
            .rssiHysteresisDb = 5,
            .rssiSampleCount = 3,
        }},
    };
}

void testStartupBluetoothFailureDoesNotLock() {
    // Given: no monitor has completed its first device observation.
    StateMachine machine{configuredForOneDevice()};

    // When: Bluetooth is unavailable throughout startup.
    machine.setBluetoothAvailable(false, at(0));
    const Action action = machine.advanceTo(at(60));

    // Then: startup remains an error/retry state without a lock request.
    require(action == Action::None);
    require(machine.state() == MachineState::Error);
}

void testRuntimeBluetoothFailureLocksAfterAwayDuration() {
    // Given: Bluetooth has produced a present observation.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));

    // When: the monitor becomes unavailable while locking remains enabled.
    machine.setBluetoothAvailable(false, at(1));

    // Then: no lock occurs before the full away duration, but one follows it.
    require(machine.advanceTo(at(10)) == Action::None);
    require(machine.advanceTo(at(11)) == Action::Lock);
    require(machine.state() == MachineState::Locked);
}

void testDisabledWatcherNeverLocks() {
    // Given: an enabled watcher that has observed its device.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));

    // When: the watcher is disabled before the device becomes unavailable.
    machine.setEnabled(false, at(1));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(2));

    // Then: advancing past the away duration never requests a lock.
    require(machine.advanceTo(at(60)) == Action::None);
    require(machine.state() == MachineState::Disabled);
}

void testBelowThresholdRssiInitiatesLockingWhileConnected() {
    // Given: a connected device whose RSSI is initially above its threshold.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));

    // When: RSSI drops below the threshold while the device remains connected.
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -80}, at(1));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -80}, at(2));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -80}, at(3));

    // Then: dominant RSSI evidence requests a lock after the away duration.
    require(machine.advanceTo(at(12)) == Action::None);
    require(machine.advanceTo(at(13)) == Action::Lock);
}

void testInitialRssiBelowThresholdInitiatesLocking() {
    // Given: a connected device with no prior RSSI presence classification.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));

    // When: its initial RSSI sample is below the configured threshold.
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -72}, at(0));

    // Then: the initial sample does not receive the present-state hysteresis allowance.
    require(machine.advanceTo(at(10)) == Action::Lock);
}

void testSnoozeDefersButDoesNotResetAnActiveAwayTimer() {
    // Given: a device already in an absence countdown.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(0));

    // When: the user snoozes locking for a bounded duration.
    machine.snooze(30s, at(5));

    // Then: locking resumes when snooze ends without restarting the away timer.
    require(machine.advanceTo(at(34)) == Action::None);
    require(machine.advanceTo(at(35)) == Action::Lock);
}

void testResumeGraceDefersAnActiveAwayTimer() {
    // Given: a previously observed device is absent.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(0));

    // When: monitoring resumes after system sleep.
    machine.resume(at(5));

    // Then: locking remains paused through the configured grace period.
    require(machine.advanceTo(at(34)) == Action::None);
    require(machine.advanceTo(at(35)) == Action::Lock);
}

void testDisablingPresentDeviceStartsAwayCountdown() {
    // Given: a present configured device.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));

    // When: the configured device is runtime-disabled.
    machine.setDeviceEnabled(DeviceId{"phone"}, false, at(1));

    // Then: absence is evaluated using the normal away duration.
    require(machine.advanceTo(at(10)) == Action::None);
    require(machine.advanceTo(at(11)) == Action::Lock);
}

void testChangingThresholdReevaluatesObservedRssi() {
    // Given: a connected device is present under its initial RSSI threshold.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));

    // When: its threshold becomes stricter than the observed RSSI.
    machine.setDeviceRssiThreshold(DeviceId{"phone"}, -40, at(1));

    // Then: the normal away duration applies from the policy change.
    require(machine.advanceTo(at(10)) == Action::None);
    require(machine.advanceTo(at(11)) == Action::Lock);
}

void testRemovedDeviceStartsAwayCountdown() {
    // Given: a connected device is present.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));

    // When: BlueZ reports that the device object was removed.
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(1));

    // Then: the removal starts the normal away countdown.
    require(machine.advanceTo(at(10)) == Action::None);
    require(machine.advanceTo(at(11)) == Action::Lock);
}

void testUnconfiguredRssiThresholdUsesDefaultAndFallsBackToConnected() {
    StateMachine machine{StateMachineConfiguration{
        .awayDuration = 10s,
        .snoozeDurationCap = 30s,
        .postResumeGrace = 30s,
        .minimumPresentDevices = 1,
        .devices = {DeviceConfiguration{
            .id = DeviceId{"phone"},
            .rssiThresholdDbm = std::nullopt,
            .rssiHysteresisDb = 5,
            .rssiSampleCount = 3,
        }},
    }};

    require(machine.deviceRssiThreshold(DeviceId{"phone"}) == -70);
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = std::nullopt}, at(0));
    require(machine.state() == MachineState::Monitoring);
    require(machine.advanceTo(at(0)) == Action::None);

    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(1));
    require(machine.advanceTo(at(10)) == Action::None);
    require(machine.advanceTo(at(11)) == Action::Lock);
}

void testMinimumPresentExceedingDevicesThrows() {
    bool threw = false;
    try {
        StateMachine machine{StateMachineConfiguration{
            .awayDuration = 10s,
            .snoozeDurationCap = 30s,
            .postResumeGrace = 30s,
            .minimumPresentDevices = 2,
            .devices = {DeviceConfiguration{
                .id = DeviceId{"phone"},
                .rssiThresholdDbm = -70,
                .rssiHysteresisDb = 5,
                .rssiSampleCount = 3,
            }},
        }};
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw);
}

} // namespace

int main() {
    testStartupBluetoothFailureDoesNotLock();
    testRuntimeBluetoothFailureLocksAfterAwayDuration();
    testDisabledWatcherNeverLocks();
    testBelowThresholdRssiInitiatesLockingWhileConnected();
    testInitialRssiBelowThresholdInitiatesLocking();
    testSnoozeDefersButDoesNotResetAnActiveAwayTimer();
    testResumeGraceDefersAnActiveAwayTimer();
    testDisablingPresentDeviceStartsAwayCountdown();
    testChangingThresholdReevaluatesObservedRssi();
    testRemovedDeviceStartsAwayCountdown();
    testUnconfiguredRssiThresholdUsesDefaultAndFallsBackToConnected();
    testMinimumPresentExceedingDevicesThrows();
}
