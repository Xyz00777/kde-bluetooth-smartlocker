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

    for (const DeviceConfiguration& device : configuration_.devices) {
        if (device.rssiSampleCount == 0) {
            throw std::invalid_argument{"RSSI sample count must be positive"};
        }
        if (!deviceConfigurations_.emplace(device.id, device).second) {
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
    runtime.observed = true;
    runtime.connected = observation.connected;
    observedAtLeastOneDevice_ = true;

    if (!observation.rssiDbm.has_value() || !configuration.rssiThresholdDbm.has_value()) {
        runtime.rssiSamples.clear();
        runtime.rssiPresent.reset();
        updateState(now);
        return;
    }

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
    if (!awaySince_.has_value() || now - *awaySince_ < configuration_.awayDuration) {
        return Action::None;
    }

    state_ = MachineState::Locked;
    awaySince_.reset();
    return Action::Lock;
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

bool StateMachine::isPresent(const DeviceConfiguration& configuration, const DeviceRuntime& runtime) const {
    if (!configuration.enabled || !runtime.observed) {
        return false;
    }
    return runtime.rssiPresent.value_or(runtime.connected);
}

void StateMachine::updateRssiPresence(const DeviceConfiguration& configuration, DeviceRuntime& runtime) {
    if (!configuration.rssiThresholdDbm.has_value() || runtime.rssiSamples.empty()) {
        runtime.rssiPresent.reset();
        return;
    }
    const int total = std::accumulate(runtime.rssiSamples.begin(), runtime.rssiSamples.end(), 0);
    const int average = total / static_cast<int>(runtime.rssiSamples.size());
    const int threshold = *configuration.rssiThresholdDbm;
    const int cutoff = runtime.rssiPresent.value_or(false) ? threshold - configuration.rssiHysteresisDb : threshold;
    runtime.rssiPresent = average >= cutoff;
}

std::size_t StateMachine::presentDeviceCount() const {
    return std::count_if(deviceConfigurations_.cbegin(), deviceConfigurations_.cend(), [this](const auto& device) {
        return isPresent(device.second, devices_.at(device.first));
    });
}

void StateMachine::updateState(const TimePoint now) {
    if (!enabled_) {
        awaySince_.reset();
        snoozedUntil_.reset();
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
    if (!observedAtLeastOneDevice_) {
        state_ = bluetoothAvailable_ ? MachineState::Starting : MachineState::Error;
        return;
    }
    if (bluetoothAvailable_ && presentDeviceCount() >= configuration_.minimumPresentDevices) {
        awaySince_.reset();
        state_ = MachineState::Monitoring;
        return;
    }
    if (!awaySince_.has_value()) {
        awaySince_ = now;
    }
    state_ = bluetoothAvailable_ ? MachineState::AwaitingAbsence : MachineState::Error;
}

}
