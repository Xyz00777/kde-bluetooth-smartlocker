#include "smartlocker/state_machine.hpp"
#include "smartlocker/device_spec.hpp"
#include "smartlocker/device_rssi_thresholds.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace std::chrono_literals;
using smartlocker::Action;
using smartlocker::DeviceConfiguration;
using smartlocker::DeviceId;
using smartlocker::DeviceObservation;
using smartlocker::autoSelectedDevice;
using smartlocker::macFromBluezPath;
using smartlocker::normalizeDeviceSpec;
using smartlocker::MachineState;
using smartlocker::StateMachine;
using smartlocker::StateMachineConfiguration;
using smartlocker::TimePoint;

namespace {

TimePoint at(const int seconds) {
    return TimePoint{} + std::chrono::seconds{seconds};
}

#define require(condition) \
    do { \
        if (!(condition)) { \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #condition); \
            std::abort(); \
        } \
    } while (false)

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

StateMachineConfiguration configuredForOneDeviceLockingWhenBluetoothOff() {
    auto configuration = configuredForOneDevice();
    configuration.lockWhenBluetoothUnavailable = true;
    return configuration;
}

void testBluetoothUnavailableDoesNotLockByDefault() {
    // Given: Bluetooth has produced a present observation.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));

    // When: the adapter is switched off, which carries no presence information at all.
    machine.setBluetoothAvailable(false, at(1));

    // Then: no lock is ever requested, however long the adapter stays off.
    require(machine.advanceTo(at(10)) == Action::None);
    require(machine.advanceTo(at(10'000)) == Action::None);
    require(machine.state() == MachineState::Error);
}

void testBluetoothUnavailableLocksWhenExplicitlyOptedIn() {
    // Given: the operator chose the fail-secure behaviour.
    StateMachine machine{configuredForOneDeviceLockingWhenBluetoothOff()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));
    machine.setBluetoothAvailable(false, at(1));

    // Then: the away duration still gates the lock, exactly as before.
    require(machine.advanceTo(at(10)) == Action::None);
    require(machine.advanceTo(at(11)) == Action::Lock);
    require(machine.state() == MachineState::Locked);
}

void testBluetoothReturnAfterIdleAdapterStartsFreshCountdown() {
    // Given: the adapter was switched off while the device was present, so no countdown was armed.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));

    // When: the adapter returns with the device now absent.
    machine.setBluetoothAvailable(false, at(1));
    require(machine.advanceTo(at(500)) == Action::None);
    machine.setBluetoothAvailable(true, at(501));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(501));

    // Then: the countdown starts from the adapter's return rather than expiring immediately, because
    // nothing was observed during the outage.
    require(machine.advanceTo(at(510)) == Action::None);
    require(machine.advanceTo(at(511)) == Action::Lock);
}

void testAwayDurationBoundaryIsInclusive() {
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(0));

    require(machine.advanceTo(at(10) - TimePoint::duration{1}) == Action::None);
    require(machine.advanceTo(at(10)) == Action::Lock);
}

void testDeviceReturnBeforeDeadlineCancelsPendingLock() {
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(1));

    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(11) - TimePoint::duration{1});

    require(machine.advanceTo(at(11)) == Action::None);
    require(machine.advanceTo(at(60)) == Action::None);
    require(machine.state() == MachineState::Monitoring);
}

void testEmptyDeviceSetNeverLocks() {
    StateMachine machine{StateMachineConfiguration{
        .awayDuration = 10s,
        .snoozeDurationCap = 30s,
        .postResumeGrace = 30s,
        .minimumPresentDevices = 1,
        .devices = {},
    }};

    machine.setBluetoothAvailable(true, at(0));
    require(machine.advanceTo(at(60)) == Action::None);
    require(machine.state() == MachineState::Starting);

    machine.setBluetoothAvailable(false, at(61));
    require(machine.advanceTo(at(120)) == Action::None);
    require(machine.state() == MachineState::Error);
}

void testRepeatedAbsenceDoesNotRestartAwayCountdown() {
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(0));

    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(3));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(6));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(9));

    require(machine.advanceTo(at(9)) == Action::None);
    require(machine.advanceTo(at(10)) == Action::Lock);
}

void testControllerFailureDuringAbsenceCountdownPreservesDeadline() {
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(1));

    machine.setBluetoothAvailable(false, at(5));

    // The countdown is intentionally preserved; Error while Bluetooth is unavailable is incidental state reporting.
    require(machine.state() == MachineState::Error);
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

void testReEnableRequiresFreshObservation() {
    // Given: a watcher that observed a present device and was then disabled.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));
    machine.setEnabled(false, at(1));
    require(machine.state() == MachineState::Disabled);

    // When: the watcher is re-enabled without any new device observation.
    machine.setEnabled(true, at(2));

    // Then: the machine is in Starting state (not AwaitingAbsence) because
    // the old observation history was cleared on disable.
    require(machine.state() == MachineState::Starting);
    require(machine.advanceTo(at(60)) == Action::None);
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

void testSnoozeDoesNotPersistAcrossDisableAndReenable() {
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));

    // Snooze for 30s
    machine.snooze(30s, at(5));
    require(machine.state() == MachineState::Snoozed);

    // Disabling clears snooze
    machine.setEnabled(false, at(6));
    require(machine.state() == MachineState::Disabled);

    // Re-enabling does not restore snooze; goes to Starting
    machine.setEnabled(true, at(7));
    require(machine.state() == MachineState::Starting);
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
    machine.setDeviceConnectionRssiThreshold(DeviceId{"phone"}, -40, at(1));

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

void testDynamicDeviceRemovalReevaluatesPresence() {
    StateMachine machine{StateMachineConfiguration{
        .awayDuration = 10s,
        .snoozeDurationCap = 30s,
        .postResumeGrace = 30s,
        .minimumPresentDevices = 1,
        .devices = {
            DeviceConfiguration{.id = DeviceId{"phone1"}, .rssiThresholdDbm = -70, .rssiHysteresisDb = 5, .rssiSampleCount = 3},
            DeviceConfiguration{.id = DeviceId{"phone2"}, .rssiThresholdDbm = -70, .rssiHysteresisDb = 5, .rssiSampleCount = 3}
        },
    }};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone1"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));
    machine.observe(DeviceId{"phone2"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(0));
    require(machine.state() == MachineState::Monitoring);

    // Remove the only present device:
    machine.removeDevice(DeviceId{"phone1"}, at(1));
    // Since phone2 is absent and phone1 is gone, away timer starts:
    require(machine.state() == MachineState::AwaitingAbsence);
    require(machine.advanceTo(at(10)) == Action::None);
    require(machine.advanceTo(at(11)) == Action::Lock);
}

void testDynamicDeviceRemovalClampsMinimumPresentDevices() {
    StateMachine machine{StateMachineConfiguration{
        .awayDuration = 10s,
        .snoozeDurationCap = 30s,
        .postResumeGrace = 30s,
        .minimumPresentDevices = 2,
        .devices = {
            DeviceConfiguration{.id = DeviceId{"phone1"}, .rssiThresholdDbm = -70, .rssiHysteresisDb = 5, .rssiSampleCount = 3},
            DeviceConfiguration{.id = DeviceId{"phone2"}, .rssiThresholdDbm = -70, .rssiHysteresisDb = 5, .rssiSampleCount = 3}
        },
    }};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone1"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));
    machine.observe(DeviceId{"phone2"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));
    require(machine.state() == MachineState::Monitoring);

    // Remove phone2 so only 1 device remains:
    machine.removeDevice(DeviceId{"phone2"}, at(1));
    // Clamping minimumPresentDevices to remaining count (1) keeps state Monitoring because phone1 is present
    require(machine.state() == MachineState::Monitoring);
    require(machine.advanceTo(at(10)) == Action::None);
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

void testConnectionAndBleScanningAreIndependentPresenceSources() {
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.setDeviceConnectionEnabled(DeviceId{"phone"}, false, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = -60}, at(0));
    require(machine.state() == MachineState::Monitoring);

    machine.setDeviceBleEnabled(DeviceId{"phone"}, false, at(1));
    require(machine.state() == MachineState::AwaitingAbsence);
    require(!machine.deviceLastSignal(DeviceId{"phone"}, at(1)).has_value());
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = -60}, at(2));

    machine.setDeviceBleEnabled(DeviceId{"phone"}, true, at(3));
    require(machine.state() == MachineState::AwaitingAbsence);
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = -60}, at(4));
    require(machine.state() == MachineState::Monitoring);

    machine.setDeviceBleEnabled(DeviceId{"phone"}, false, at(5));
    machine.setDeviceConnectionEnabled(DeviceId{"phone"}, true, at(6));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -60}, at(6));
    require(machine.state() == MachineState::Monitoring);
}

void testBlePresenceExpiresWithoutFreshAdvertisementRssi() {
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.setDeviceConnectionEnabled(DeviceId{"phone"}, false, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = -60}, at(0));
    require(machine.state() == MachineState::Monitoring);
    const auto signal = machine.deviceLastSignal(DeviceId{"phone"}, at(0));
    require(signal.has_value() && signal->rssiDbm == -60 && !signal->connected);

    require(machine.advanceTo(at(10)) == Action::None);
    require(machine.state() == MachineState::AwaitingAbsence);
    require(!machine.deviceLastSignal(DeviceId{"phone"}, at(10)).has_value());
    require(machine.advanceTo(at(19)) == Action::None);
    require(machine.advanceTo(at(20)) == Action::Lock);
}

void testConnectionAndBleSignalThresholdsAreIndependent() {
    StateMachine machine{StateMachineConfiguration{
        .awayDuration = 10s,
        .snoozeDurationCap = 30s,
        .postResumeGrace = 30s,
        .minimumPresentDevices = 1,
        .devices = {DeviceConfiguration{
            .id = DeviceId{"phone"},
            .rssiThresholdDbm = -80,
            .rssiHysteresisDb = 0,
            .rssiSampleCount = 1,
            .connectionRssiThresholdDbm = -60,
        }},
    }};
    machine.setBluetoothAvailable(true, at(0));
    require(machine.deviceConnectionRssiThreshold(DeviceId{"phone"}) == -60);
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -70}, at(0));
    require(machine.state() == MachineState::AwaitingAbsence);
    machine.setDeviceConnectionRssiThreshold(DeviceId{"phone"}, -75, at(1));
    require(machine.state() == MachineState::Monitoring);
    const auto connectedSignal = machine.deviceLastSignal(DeviceId{"phone"}, at(1));
    require(connectedSignal.has_value() && connectedSignal->rssiDbm == -70 && connectedSignal->connected);
    require(machine.deviceLastConnectionRssiDbm(DeviceId{"phone"}, at(1)) == -70);
    require(!machine.deviceLastBleRssiDbm(DeviceId{"phone"}, at(1)).has_value());
    machine.setDeviceRssiThreshold(DeviceId{"phone"}, -30, at(1));
    require(machine.state() == MachineState::Monitoring);
    machine.setDeviceRssiThreshold(DeviceId{"phone"}, -80, at(1));

    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = -70}, at(2));
    require(machine.state() == MachineState::Monitoring);
    const auto signal = machine.deviceLastSignal(DeviceId{"phone"}, at(2));
    require(signal.has_value() && signal->rssiDbm == -70 && !signal->connected);
    require(machine.deviceConnectionRssiThreshold(DeviceId{"phone"}) == -75);
    require(machine.deviceLastBleRssiDbm(DeviceId{"phone"}, at(2)) == -70);
    require(machine.deviceLastConnectionRssiDbm(DeviceId{"phone"}, at(2)) == -70);
    require(!machine.deviceLastConnectionRssiDbm(DeviceId{"phone"}, at(11)).has_value());
}

void testRssiHysteresisDoesNotCarryBetweenBleAndConnectionSources() {
    StateMachine machine{StateMachineConfiguration{
        .awayDuration = 10s,
        .snoozeDurationCap = 30s,
        .postResumeGrace = 30s,
        .minimumPresentDevices = 1,
        .devices = {DeviceConfiguration{
            .id = DeviceId{"phone"},
            .rssiThresholdDbm = -60,
            .rssiHysteresisDb = 5,
            .rssiSampleCount = 1,
            .connectionRssiThresholdDbm = -40,
        }},
    }};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -35}, at(0));
    require(machine.state() == MachineState::Monitoring);

    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = -64}, at(1));
    require(machine.state() == MachineState::AwaitingAbsence);
}

void testBleAndConnectionSignalReadingsAreRetainedAndExpireIndependently() {
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = -55}, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -82}, at(2));

    require(machine.deviceLastBleRssiDbm(DeviceId{"phone"}, at(2)) == -55);
    require(machine.deviceLastConnectionRssiDbm(DeviceId{"phone"}, at(2)) == -82);
    require(machine.deviceLastBleRssiDbm(DeviceId{"phone"}, at(9)) == -55);
    require(!machine.deviceLastBleRssiDbm(DeviceId{"phone"}, at(10)).has_value());
    require(machine.deviceLastConnectionRssiDbm(DeviceId{"phone"}, at(11)) == -82);
    require(!machine.deviceLastConnectionRssiDbm(DeviceId{"phone"}, at(12)).has_value());
}

void testDynamicallyAddedDeviceInheritsLegacyRssiThresholdForConnection() {
    StateMachine machine{StateMachineConfiguration{
        .awayDuration = 10s,
        .snoozeDurationCap = 30s,
        .postResumeGrace = 30s,
        .minimumPresentDevices = 1,
        .devices = {},
    }};
    machine.addDevice(DeviceConfiguration{
        .id = DeviceId{"phone"},
        .rssiThresholdDbm = -47,
        .rssiHysteresisDb = 5,
        .rssiSampleCount = 3,
    }, at(0));

    require(machine.deviceRssiThreshold(DeviceId{"phone"}) == -47);
    require(machine.deviceConnectionRssiThreshold(DeviceId{"phone"}) == -47);
}

void testRssiThresholdMigrationPreservesLegacyAndRepairsOutOfRangeValues() {
    const auto inherited = smartlocker::resolveDeviceRssiThresholds(-55, std::nullopt, -70);
    require(inherited.bleDbm == -55);
    require(inherited.connectionDbm == -55);
    require(!inherited.repairBle && inherited.migrateConnection);

    const auto independent = smartlocker::resolveDeviceRssiThresholds(-55, -65, -70);
    require(independent.bleDbm == -55);
    require(independent.connectionDbm == -65);
    require(!independent.repairBle && !independent.migrateConnection);

    const auto invalidLegacy = smartlocker::resolveDeviceRssiThresholds(12, std::nullopt, -70);
    require(invalidLegacy.bleDbm == -70);
    require(invalidLegacy.connectionDbm == -70);
    require(invalidLegacy.repairBle && invalidLegacy.migrateConnection);

    const auto invalidConnection = smartlocker::resolveDeviceRssiThresholds(-50, -120, -70);
    require(invalidConnection.bleDbm == -50);
    require(invalidConnection.connectionDbm == -50);
    require(!invalidConnection.repairBle && invalidConnection.migrateConnection);
}

void testLockedStateLatchesAndDoesNotRelock() {
    // Given: a device that becomes absent.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(1));

    // When: the away duration elapses, locking once.
    require(machine.advanceTo(at(11)) == Action::Lock);
    require(machine.state() == MachineState::Locked);

    // Then: subsequent ticks while still absent do not re-request a lock.
    require(machine.advanceTo(at(12)) == Action::None);
    require(machine.advanceTo(at(60)) == Action::None);
    require(machine.state() == MachineState::Locked);
}

void testLockedStateClearsWhenDeviceReturns() {
    // Given: a device that locked after going absent.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(1));
    require(machine.advanceTo(at(11)) == Action::Lock);

    // When: the device returns.
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(12));

    // Then: the machine returns to monitoring and can lock again on a later absence.
    require(machine.state() == MachineState::Monitoring);
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(13));
    require(machine.advanceTo(at(22)) == Action::None);
    require(machine.advanceTo(at(23)) == Action::Lock);
}

void testLockFailureRetriesAfterAwayDuration() {
    // Given: a device that locked after going absent.
    StateMachine machine{configuredForOneDevice()};
    machine.setBluetoothAvailable(true, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = true, .rssiDbm = -50}, at(0));
    machine.observe(DeviceId{"phone"}, DeviceObservation{.connected = false, .rssiDbm = std::nullopt}, at(1));
    require(machine.advanceTo(at(11)) == Action::Lock);

    // When: the lock command fails and the daemon clears the latch.
    machine.clearLocked(at(11));

    // Then: the countdown restarts from the failure and retries after the away duration.
    require(machine.advanceTo(at(12)) == Action::None);
    require(machine.advanceTo(at(20)) == Action::None);
    require(machine.advanceTo(at(21)) == Action::Lock);
    require(machine.state() == MachineState::Locked);
}

void testSnoozeExceedingCapThrows() {
    StateMachine machine{configuredForOneDevice()};
    bool threw = false;
    try {
        machine.snooze(31s, at(0));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw);
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

void testDeviceSpecNormalization() {
    require(normalizeDeviceSpec("C0:1C:6A:75:9C:31") == "C0:1C:6A:75:9C:31");
    require(normalizeDeviceSpec("c0-1c-6a-75-9c-31") == "C0:1C:6A:75:9C:31");
    require(normalizeDeviceSpec("c0:1c:6a:75:9c:31") == "C0:1C:6A:75:9C:31");
    require(normalizeDeviceSpec("C01C6A759C31") == "C0:1C:6A:75:9C:31");
    require(normalizeDeviceSpec("c01c6a759c31") == "C0:1C:6A:75:9C:31");
}

void testLegacyPathExtractsCanonicalMac() {
    require(normalizeDeviceSpec("/org/bluez/hci0/dev_c0_1c_6a_75_9c_31") == "C0:1C:6A:75:9C:31");
    require(macFromBluezPath("/org/bluez/hci7/dev_C0_1C_6A_75_9C_31") == "C0:1C:6A:75:9C:31");
}

void testMalformedDeviceSpecIsRejected() {
    require(!normalizeDeviceSpec("C0:1C:6A:75:9C"));
    require(!normalizeDeviceSpec("/org/bluez/hci0/adapter"));
    require(!normalizeDeviceSpec("C0:1C:6A:75:9C:3Z"));
    require(!normalizeDeviceSpec("C0::1C:6A:75:9C:31"));
    require(!normalizeDeviceSpec("/org/bluez/hci0/extra/dev_C0_1C_6A_75_9C_31"));
}

void testCanonicalDeviceIdsCompareEqual() {
    const auto first = normalizeDeviceSpec("c0-1c-6a-75-9c-31");
    const auto second = normalizeDeviceSpec("/org/bluez/hci9/dev_C0_1C_6A_75_9C_31");
    require(first.has_value() && second.has_value());
    require(DeviceId{*first} == DeviceId{*second});
}

void testAutoSelectionRequiresPairedOrTrusted() {
    require(autoSelectedDevice(true, false));
    require(autoSelectedDevice(false, true));
    require(autoSelectedDevice(true, true));
    require(!autoSelectedDevice(false, false));
}

} // namespace

int main() {
    testStartupBluetoothFailureDoesNotLock();
    testBluetoothUnavailableDoesNotLockByDefault();
    testBluetoothUnavailableLocksWhenExplicitlyOptedIn();
    testBluetoothReturnAfterIdleAdapterStartsFreshCountdown();
    testAwayDurationBoundaryIsInclusive();
    testDeviceReturnBeforeDeadlineCancelsPendingLock();
    testEmptyDeviceSetNeverLocks();
    testRepeatedAbsenceDoesNotRestartAwayCountdown();
    testControllerFailureDuringAbsenceCountdownPreservesDeadline();
    testDisabledWatcherNeverLocks();
    testReEnableRequiresFreshObservation();
    testBelowThresholdRssiInitiatesLockingWhileConnected();
    testInitialRssiBelowThresholdInitiatesLocking();
    testSnoozeDefersButDoesNotResetAnActiveAwayTimer();
    testSnoozeDoesNotPersistAcrossDisableAndReenable();
    testResumeGraceDefersAnActiveAwayTimer();
    testDisablingPresentDeviceStartsAwayCountdown();
    testChangingThresholdReevaluatesObservedRssi();
    testRemovedDeviceStartsAwayCountdown();
    testDynamicDeviceRemovalReevaluatesPresence();
    testDynamicDeviceRemovalClampsMinimumPresentDevices();
    testUnconfiguredRssiThresholdUsesDefaultAndFallsBackToConnected();
    testConnectionAndBleScanningAreIndependentPresenceSources();
    testBlePresenceExpiresWithoutFreshAdvertisementRssi();
    testConnectionAndBleSignalThresholdsAreIndependent();
    testRssiHysteresisDoesNotCarryBetweenBleAndConnectionSources();
    testBleAndConnectionSignalReadingsAreRetainedAndExpireIndependently();
    testDynamicallyAddedDeviceInheritsLegacyRssiThresholdForConnection();
    testRssiThresholdMigrationPreservesLegacyAndRepairsOutOfRangeValues();
    testLockedStateLatchesAndDoesNotRelock();
    testLockedStateClearsWhenDeviceReturns();
    testLockFailureRetriesAfterAwayDuration();
    testSnoozeExceedingCapThrows();
    testMinimumPresentExceedingDevicesThrows();
    testDeviceSpecNormalization();
    testLegacyPathExtractsCanonicalMac();
    testMalformedDeviceSpecIsRejected();
    testCanonicalDeviceIdsCompareEqual();
    testAutoSelectionRequiresPairedOrTrusted();
}
