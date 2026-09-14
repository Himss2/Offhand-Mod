#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace levioffhand::runtime::routing {

inline constexpr std::uint8_t kOffhandContainer = 34;
inline constexpr std::size_t kDestinationRecordSize = 0x20;

struct alignas(8) DestinationRecord {
    std::array<std::byte, kDestinationRecordSize> bytes{};
};
static_assert(sizeof(DestinationRecord) == kDestinationRecordSize);
static_assert(alignof(DestinationRecord) >= alignof(std::uint64_t));

struct FilterResult {
    std::size_t keptCount{};
    std::size_t offhandCount{};
};

[[nodiscard]] inline bool isOffhandRecord(
    const DestinationRecord& record
) noexcept {
    return std::to_integer<std::uint8_t>(record.bytes[0]) == kOffhandContainer;
}

[[nodiscard]] inline FilterResult filterDestinations(
    const DestinationRecord* source,
    std::size_t count,
    DestinationRecord* destination
) noexcept {
    FilterResult result{};
    if (source == nullptr || destination == nullptr) {
        return result;
    }

    for (std::size_t index = 0; index < count; ++index) {
        if (isOffhandRecord(source[index])) {
            ++result.offhandCount;
            continue;
        }

        std::memcpy(
            &destination[result.keptCount],
            &source[index],
            sizeof(DestinationRecord)
        );
        ++result.keptCount;
    }

    return result;
}

} // namespace levioffhand::runtime::routing
