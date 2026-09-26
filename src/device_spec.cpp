#include "smartlocker/device_spec.hpp"

#include <algorithm>
#include <cctype>

namespace smartlocker {
namespace {

std::optional<std::string> normalizeAddress(std::string_view address) {
    const bool separated = address.size() == 17;
    if (address.size() != 12 && !separated) {
        return std::nullopt;
    }
    const char separator = separated ? address[2] : '\0';
    if (separated && separator != ':' && separator != '-') {
        return std::nullopt;
    }
    std::string hex;
    hex.reserve(12);
    for (std::size_t index = 0; index < address.size(); ++index) {
        const char character = address[index];
        if (separated && index % 3 == 2) {
            if (character != separator) {
                return std::nullopt;
            }
            continue;
        }
        if (!std::isxdigit(static_cast<unsigned char>(character))) {
            return std::nullopt;
        }
        hex.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(character))));
    }
    if (hex.size() != 12) {
        return std::nullopt;
    }
    std::string canonical;
    canonical.reserve(17);
    for (std::size_t index = 0; index < hex.size(); ++index) {
        if (index != 0 && index % 2 == 0) {
            canonical.push_back(':');
        }
        canonical.push_back(hex[index]);
    }
    return canonical;
}

}

std::optional<std::string> macFromBluezPath(const std::string_view path) {
    const std::size_t segment = path.rfind("/dev_");
    if (segment == std::string_view::npos || path.find('/', segment + 1) != std::string_view::npos) {
        return std::nullopt;
    }
    constexpr std::string_view bluezPrefix{"/org/bluez/"};
    if (!path.starts_with(bluezPrefix) || segment <= bluezPrefix.size()) {
        return std::nullopt;
    }
    const std::string_view adapter = path.substr(bluezPrefix.size(), segment - bluezPrefix.size());
    if (adapter.empty() || adapter.find('/') != std::string_view::npos) {
        return std::nullopt;
    }
    std::string addressSegment{path.substr(segment + 5)};
    std::replace(addressSegment.begin(), addressSegment.end(), '_', ':');
    const auto address = normalizeAddress(addressSegment);
    if (!address.has_value()) {
        return std::nullopt;
    }
    return address;
}

std::optional<std::string> normalizeDeviceSpec(const std::string_view spec) {
    if (spec.starts_with("/")) {
        return macFromBluezPath(spec);
    }
    return normalizeAddress(spec);
}

bool autoSelectedDevice(const bool paired, const bool trusted) {
    return paired || trusted;
}

}
