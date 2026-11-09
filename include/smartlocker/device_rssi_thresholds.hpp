#pragma once

#include <optional>

namespace smartlocker {

struct DeviceRssiThresholds {
    int bleDbm;
    int connectionDbm;
    bool repairBle;
    bool migrateConnection;
};

[[nodiscard]] constexpr DeviceRssiThresholds resolveDeviceRssiThresholds(const std::optional<int> savedBle,
                                                                         const std::optional<int> savedConnection,
                                                                         const int defaultDbm) {
    const bool validBle = savedBle.has_value() && *savedBle >= -100 && *savedBle <= 0;
    const int bleDbm = validBle ? *savedBle : defaultDbm;
    const bool validConnection = savedConnection.has_value() && *savedConnection >= -100 && *savedConnection <= 0;
    const int connectionDbm = validConnection ? *savedConnection : bleDbm;
    return DeviceRssiThresholds{
        .bleDbm = bleDbm,
        .connectionDbm = connectionDbm,
        .repairBle = savedBle.has_value() && !validBle,
        .migrateConnection = !validConnection,
    };
}

}
