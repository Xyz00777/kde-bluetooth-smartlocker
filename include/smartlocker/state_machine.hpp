#pragma once

#include <chrono>
#include <compare>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace smartlocker {

using TimePoint = std::chrono::steady_clock::time_point;

enum class Action {
    None,
    Lock,
};

enum class MachineState {
    Starting,
    Monitoring,
    AwaitingAbsence,
    Snoozed,
    Disabled,
    Error,
    Locked,
};

struct DeviceId {
    std::string value;

    auto operator<=>(const DeviceId&) const = default;
};

struct DeviceObservation {
    bool connected;
    std::optional<int> rssiDbm;
};

struct DeviceConfiguration {
    DeviceId id;
    std::optional<int> rssiThresholdDbm;
    int rssiHysteresisDb;
    std::size_t rssiSampleCount;
    bool enabled{true};
};

struct StateMachineConfiguration {
    std::chrono::seconds awayDuration;
    std::chrono::seconds snoozeDurationCap;
    std::chrono::seconds postResumeGrace;
    std::size_t minimumPresentDevices;
    std::vector<DeviceConfiguration> devices;
    bool enabled{true};
};

class StateMachine {
public:
    explicit StateMachine(StateMachineConfiguration configuration);

    void setBluetoothAvailable(bool available, TimePoint now);
    void setEnabled(bool enabled, TimePoint now);
    void setDeviceEnabled(const DeviceId& id, bool enabled, TimePoint now);
    void setDeviceRssiThreshold(const DeviceId& id, int thresholdDbm, TimePoint now);
    void snooze(std::chrono::seconds duration, TimePoint now);
    void resume(TimePoint now);
    void observe(const DeviceId& id, DeviceObservation observation, TimePoint now);
    [[nodiscard]] Action advanceTo(TimePoint now);
    [[nodiscard]] MachineState state() const;
    [[nodiscard]] std::chrono::seconds awayDuration() const;
    [[nodiscard]] std::chrono::seconds snoozeDurationCap() const;
    [[nodiscard]] std::chrono::seconds postResumeGrace() const;
    [[nodiscard]] std::size_t minimumPresentDevices() const;
    [[nodiscard]] bool deviceEnabled(const DeviceId& id) const;
    [[nodiscard]] int deviceRssiThreshold(const DeviceId& id) const;

private:
    struct DeviceRuntime {
        bool observed{false};
        bool connected{false};
        std::optional<bool> rssiPresent;
        std::vector<int> rssiSamples;
    };

    [[nodiscard]] bool isPresent(const DeviceConfiguration& configuration, const DeviceRuntime& runtime) const;
    void updateRssiPresence(const DeviceConfiguration& configuration, DeviceRuntime& runtime);
    [[nodiscard]] std::size_t presentDeviceCount() const;
    void updateState(TimePoint now);

    StateMachineConfiguration configuration_;
    std::map<DeviceId, DeviceConfiguration> deviceConfigurations_;
    std::map<DeviceId, DeviceRuntime> devices_;
    bool bluetoothAvailable_{false};
    bool enabled_{true};
    bool observedAtLeastOneDevice_{false};
    std::optional<TimePoint> awaySince_;
    std::optional<TimePoint> snoozedUntil_;
    std::optional<TimePoint> resumeGraceUntil_;
    MachineState state_{MachineState::Starting};
};

}
