#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace smartlocker {

[[nodiscard]] std::optional<std::string> normalizeDeviceSpec(std::string_view spec);
[[nodiscard]] std::optional<std::string> macFromBluezPath(std::string_view path);
[[nodiscard]] bool autoSelectedDevice(bool paired, bool trusted);

}
