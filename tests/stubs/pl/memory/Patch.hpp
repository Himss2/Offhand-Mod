#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace pl::memory {

inline bool writeBytes(
    std::uintptr_t,
    std::span<const std::uint8_t>,
    std::string_view
) {
    return true;
}

inline bool revertPatch(std::string_view) {
    return true;
}

} // namespace pl::memory
