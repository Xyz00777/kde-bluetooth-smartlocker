#include "smartlocker/state_machine.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace smartlocker {

StateMachine::StateMachine(StateMachineConfiguration configuration)
    : configuration_(std::move(configuration)), enabled_(configuration_.enabled) {
    if (configuration_.awayDuration < std::chrono::seconds::zero()) {
        throw std::invalid_argument{"away duration must not be negative"};
    }
    if (configuration_.snoozeDurationCap <= std::chrono::seconds::zero()) {
        throw std::invalid_argument{"snooze duration cap must be positive"};
    }
    if (configuration_.postResumeGrace < std::chrono::seconds::zero()) {
        throw std::invalid_argument{"post-resume grace must not be negative"};
    }
    if (configuration_.minimumPresentDevices == 0) {
        throw std::invalid_argument{"minimum present devices must be positive"};
    }
    if (!configuration_.devices.empty() && configuration_.minimumPresentDevices > configuration_.devices.size()) {
        throw std::invalid_argument{"minimum present devices cannot exceed configured device count"};
    }

    for (const DeviceConfiguration& device : configuration_.devices) {
        if (device.rssiSampleCount == 0) {
            throw std::invalid_argument{"RSSI sample count must be positive"};
        }
        DeviceConfiguration normalized = device;
        if (!normalized.connectionRssiThresholdDbm.has_value()) {
            normalized.connectionRssiThresholdDbm = normalized.rssiThresholdDbm;
        }
        if (!deviceConfigurations_.emplace(device.id, std::move(normalized)).second) {
            throw std::invalid_argument{"device identifiers must be unique"};
        }
        devices_.emplace(device.id, DeviceRuntime{});
    }
}

void StateMachine::setBluetoothAvailable(const bool available, const TimePoint now) {
    bluetoothAvailable_ = available;
    updateState(now);
}

void StateMachine::setEnabled(const bool enabled, const TimePoint now) {
    enabled_ = enabled;
    updateState(now);
}

void StateMachine::setDeviceEnabled(const DeviceId& id, const bool enabled, const TimePoint now) {
    const auto device = deviceConfigurations_.find(id);
    if (device == deviceConfigurations_.end()) {
        throw std::invalid_argument{"unknown device"};
    }
    device->second.enabled = enabled;
    updateState(now);
}

void StateMachine::setDeviceRssiThreshold(const DeviceId& id, const int thresholdDbm, const TimePoint now) {
    const auto configuration = deviceConfigurations_.find(id);
    const auto runtime = devices_.find(id);
    if (configuration == deviceConfigurations_.end() || runtime == devices_.end()) {
        throw std::invalid_argument{"unknown device"};
    }
    configuration->second.rssiThresholdDbm = thresholdDbm;
    updateRssiPresence(configuration->second, runtime->second);
    updateState(now);
}

void StateMachine::setDeviceConnectionRssiThreshold(const DeviceId& id, const int thresholdDbm, const TimePoint now) {
    const auto configuration = deviceConfigurations_.find(id);
    const auto runtime = devices_.find(id);
    if (configuration == deviceConfigurations_.end() || runtime == devices_.end()) {
        throw std::invalid_argument{"unknown device"};
    }
    configuration->second.connectionRssiThresholdDbm = thresholdDbm;
    updateRssiPresence(configuration->second, runtime->second);
    updateState(now);
}

void StateMachine::setDeviceBleEnabled(const DeviceId& id, const bool enabled, const TimePoint now) {
    const auto configuration = deviceConfigurations_.find(id);
    if (configuration == deviceConfigurations_.end()) {
        throw std::invalid_argument{"unknown device"};
    }
    configuration->second.bleEnabled = enabled;
    const auto runtime = devices_.find(id);
    if (!enabled && !runtime->second.connected) {
        runtime->second.rssiSamples.clear();
        runtime->second.rssiPresent.reset();
        runtime->second.rssiObservedAt.reset();
        runtime->second.rssiObservedConnected = false;
    }
    updateState(now);
}

void StateMachine::setDeviceConnectionEnabled(const DeviceId& id, const bool enabled, const TimePoint now) {
    const auto configuration = deviceConfigurations_.find(id);
    if (configuration == deviceConfigurations_.end()) {
        throw std::invalid_argument{"unknown device"};
    }
    configuration->second.connectionEnabled = enabled;
    const auto runtime = devices_.find(id);
    if (!enabled && runtime->second.connected) {
        runtime->second.rssiSamples.clear();
        runtime->second.rssiPresent.reset();
        runtime->second.rssiObservedAt.reset();
        runtime->second.rssiObservedConnected = false;
    }
    updateState(now);
}

void StateMachine::addDevice(DeviceConfiguration configuration, const TimePoint now) {
    if (configuration.rssiSampleCount == 0 || deviceConfigurations_.contains(configuration.id)) {
        throw std::invalid_argument{"invalid or duplicate device configuration"};
    }
    if (!configuration.connectionRssiThresholdDbm.has_value()) {
        configuration.connectionRssiThresholdDbm = configuration.rssiThresholdDbm;
    }
    devices_.emplace(configuration.id, DeviceRuntime{});
    deviceConfigurations_.emplace(configuration.id, std::move(configuration));
    updateState(now);
}

void StateMachine::removeDevice(const DeviceId& id, const TimePoint now) {
    deviceConfigurations_.erase(id);
    devices_.erase(id);
    if (deviceConfigurations_.empty()) {
        observedAtLeastOneDevice_ = false;
    }
    updateState(now);
}

void StateMachine::snooze(const std::chrono::seconds duration, const TimePoint now) {
    if (duration <= std::chrono::seconds::zero() || duration > configuration_.snoozeDurationCap) {
        throw std::invalid_argument{"snooze duration must be within the configured cap"};
    }
    snoozedUntil_ = now + duration;
    updateState(now);
}

void StateMachine::resume(const TimePoint now) {
    resumeGraceUntil_ = now + configuration_.postResumeGrace;
    updateState(now);
}

void StateMachine::observe(const DeviceId& id, const DeviceObservation observation, const TimePoint now) {
    const auto device = devices_.find(id);
    if (device == devices_.end()) {
        throw std::invalid_argument{"observation received for an unknown device"};
    }

    DeviceRuntime& runtime = device->second;
    const DeviceConfiguration& configuration = deviceConfigurations_.at(id);
    if (observation.rssiDbm.has_value()) {
        if (observation.connected) {
            runtime.lastConnectionRssiDbm = *observation.rssiDbm;
            runtime.lastConnectionRssiObservedAt = now;
        } else {
            runtime.lastBleRssiDbm = *observation.rssiDbm;
            runtime.lastBleRssiObservedAt = now;
        }
    }
    if (runtime.rssiObservedAt.has_value() && runtime.rssiObservedConnected != observation.connected) {
        runtime.rssiSamples.clear();
        runtime.rssiPresent.reset();
    }
    runtime.observed = true;
    runtime.connected = observation.connected;
    observedAtLeastOneDevice_ = true;

    const std::optional<int>& threshold = observation.connected ? configuration.connectionRssiThresholdDbm
                                                                 : configuration.rssiThresholdDbm;
    const bool sourceEnabled = observation.connected ? configuration.connectionEnabled : configuration.bleEnabled;
    if (!sourceEnabled || !observation.rssiDbm.has_value() || !threshold.has_value()) {
        runtime.rssiSamples.clear();
        runtime.rssiPresent.reset();
        runtime.rssiObservedAt.reset();
        runtime.rssiObservedConnected = false;
        updateState(now);
        return;
    }

    runtime.rssiObservedAt = now;
    runtime.rssiObservedConnected = observation.connected;
    runtime.rssiSamples.push_back(*observation.rssiDbm);
    if (runtime.rssiSamples.size() > configuration.rssiSampleCount) {
        runtime.rssiSamples.erase(runtime.rssiSamples.begin());
    }

    updateRssiPresence(configuration, runtime);
    updateState(now);
}

Action StateMachine::advanceTo(const TimePoint now) {
    updateState(now);
    if (state_ == MachineState::Snoozed || state_ == MachineState::Starting) {
        return Action::None;
    }
    if (locked_) {
        return Action::None;
    }
    if (!awaySince_.has_value() || now - *awaySince_ < configuration_.awayDuration) {
        return Action::None;
    }

    locked_ = true;
    state_ = MachineState::Locked;
    awaySince_.reset();
    return Action::Lock;
}

void StateMachine::clearLocked(const TimePoint now) {
    locked_ = false;
    updateState(now);
}

MachineState StateMachine::state() const {
    return state_;
}

std::chrono::seconds StateMachine::awayDuration() const {
    return configuration_.awayDuration;
}

std::chrono::seconds StateMachine::snoozeDurationCap() const {
    return configuration_.snoozeDurationCap;
}

std::chrono::seconds StateMachine::postResumeGrace() const {
    return configuration_.postResumeGrace;
}

std::size_t StateMachine::minimumPresentDevices() const {
    return configuration_.minimumPresentDevices;
}

bool StateMachine::deviceEnabled(const DeviceId& id) const {
    const auto device = deviceConfigurations_.find(id);
    if (device == deviceConfigurations_.end()) {
        throw std::invalid_argument{"unknown device"};
    }
    return device->second.enabled;
}

int StateMachine::deviceRssiThreshold(const DeviceId& id) const {
    const auto device = deviceConfigurations_.find(id);
    if (device == deviceConfigurations_.end()) {
        throw std::invalid_argument{"unknown device"};
    }
    return device->second.rssiThresholdDbm.value_or(-70);
}

int StateMachine::deviceConnectionRssiThreshold(const DeviceId& id) const {
    const auto device = deviceConfigurations_.find(id);
    if (device == deviceConfigurations_.end()) {
        throw std::invalid_argument{"unknown device"};
    }
    return device->second.connectionRssiThresholdDbm.value_or(-70);
}

std::optional<DeviceSignal> StateMachine::deviceLastSignal(const DeviceId& id, const TimePoint now) const {
    const auto configuration = deviceConfigurations_.find(id);
    const auto runtime = devices_.find(id);
    if (configuration == deviceConfigurations_.end() || runtime == devices_.end()) {
        throw std::invalid_argument{"unknown device"};
    }
    const bool sourceEnabled = runtime->second.rssiObservedConnected ? configuration->second.connectionEnabled : configuration->second.bleEnabled;
    if (!sourceEnabled || runtime->second.rssiSamples.empty() || !runtime->second.rssiObservedAt.has_value()
        || now - *runtime->second.rssiObservedAt >= kRssiFreshness) {
        return std::nullopt;
    }
    return DeviceSignal{runtime->second.rssiSamples.back(), runtime->second.rssiObservedConnected};
}

std::optional<int> StateMachine::deviceLastBleRssiDbm(const DeviceId& id, const TimePoint now) const {
    const auto runtime = devices_.find(id);
    if (!deviceConfigurations_.contains(id) || runtime == devices_.end()) {
        throw std::invalid_argument{"unknown device"};
    }
    if (!runtime->second.lastBleRssiDbm.has_value() || !runtime->second.lastBleRssiObservedAt.has_value()
        || now - *runtime->second.lastBleRssiObservedAt >= kRssiFreshness) {
        return std::nullopt;
    }
    return runtime->second.lastBleRssiDbm;
}

std::optional<int> StateMachine::deviceLastConnectionRssiDbm(const DeviceId& id, const TimePoint now) const {
    const auto runtime = devices_.find(id);
    if (!deviceConfigurations_.contains(id) || runtime == devices_.end()) {
        throw std::invalid_argument{"unknown device"};
    }
    if (!runtime->second.lastConnectionRssiDbm.has_value() || !runtime->second.lastConnectionRssiObservedAt.has_value()
        || now - *runtime->second.lastConnectionRssiObservedAt >= kRssiFreshness) {
        return std::nullopt;
    }
    return runtime->second.lastConnectionRssiDbm;
}

bool StateMachine::isPresent(const DeviceConfiguration& configuration, const DeviceRuntime& runtime, const TimePoint now) const {
    if (!configuration.enabled || !runtime.observed) {
        return false;
    }
    if (runtime.connected) {
        if (!configuration.connectionEnabled) {
            return false;
        }
        return !runtime.rssiObservedConnected || runtime.rssiPresent.value_or(false);
    }
    const bool freshBleRssi = !runtime.rssiObservedConnected && runtime.rssiObservedAt.has_value()
        && now - *runtime.rssiObservedAt < kRssiFreshness;
    return configuration.bleEnabled && freshBleRssi && runtime.rssiPresent.value_or(false);
}

void StateMachine::updateRssiPresence(const DeviceConfiguration& configuration, DeviceRuntime& runtime) {
    const std::optional<int>& threshold = runtime.rssiObservedConnected ? configuration.connectionRssiThresholdDbm
                                                                         : configuration.rssiThresholdDbm;
    if (!threshold.has_value() || runtime.rssiSamples.empty()) {
        runtime.rssiPresent.reset();
        return;
    }
    const long long total = std::accumulate(runtime.rssiSamples.begin(), runtime.rssiSamples.end(), 0LL);
    const int average = static_cast<int>(total / static_cast<long long>(runtime.rssiSamples.size()));
    const int thresholdDbm = *threshold;
    const int cutoff = runtime.rssiPresent.value_or(false) ? thresholdDbm - configuration.rssiHysteresisDb : thresholdDbm;
    runtime.rssiPresent = average >= cutoff;
}

std::size_t StateMachine::presentDeviceCount(const TimePoint now) const {
    return std::count_if(deviceConfigurations_.cbegin(), deviceConfigurations_.cend(), [this, now](const auto& device) {
        return isPresent(device.second, devices_.at(device.first), now);
    });
}

void StateMachine::updateState(const TimePoint now) {
    if (!enabled_) {
        awaySince_.reset();
        snoozedUntil_.reset();
        observedAtLeastOneDevice_ = false;
        locked_ = false;
        state_ = MachineState::Disabled;
        return;
    }
    if (snoozedUntil_.has_value() && now < *snoozedUntil_) {
        state_ = MachineState::Snoozed;
        return;
    }
    snoozedUntil_.reset();
    if (resumeGraceUntil_.has_value() && now < *resumeGraceUntil_) {
        state_ = MachineState::Starting;
        return;
    }
    resumeGraceUntil_.reset();
    if (deviceConfigurations_.empty()) {
        awaySince_.reset();
        state_ = bluetoothAvailable_ ? MachineState::Starting : MachineState::Error;
        return;
    }
    if (!observedAtLeastOneDevice_) {
        state_ = bluetoothAvailable_ ? MachineState::Starting : MachineState::Error;
        return;
    }
    const std::size_t effectiveMinimum = std::min(configuration_.minimumPresentDevices, deviceConfigurations_.size());
    if (bluetoothAvailable_ && presentDeviceCount(now) >= effectiveMinimum) {
        awaySince_.reset();
        locked_ = false;
        state_ = MachineState::Monitoring;
        return;
    }
    if (locked_) {
        state_ = MachineState::Locked;
        return;
    }
    // An unpowered adapter reports no presence, so by default it must never *arm* a new absence
    // countdown: switching Bluetooth off at the desk is not evidence of leaving. A countdown that
    // was already running is deliberately left alone -- the device was observed absent before the
    // radio dropped, so that evidence still holds (testControllerFailureDuringAbsenceCountdownPreservesDeadline).
    if (!bluetoothAvailable_ && !configuration_.lockWhenBluetoothUnavailable && !awaySince_.has_value()) {
        state_ = MachineState::Error;
        return;
    }
    if (!awaySince_.has_value()) {
        awaySince_ = now;
    }
    state_ = bluetoothAvailable_ ? MachineState::AwaitingAbsence : MachineState::Error;
}

}
