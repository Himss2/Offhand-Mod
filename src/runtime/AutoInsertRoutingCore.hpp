#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace levioffhand::runtime::routing {

// Minecraft ContainerEnumName::OffhandContainer remains 34.  Do not treat
// this enum value as a raw byte inside DestinationRecord: the first 0x18 bytes
// are a libc++ std::string destination name.
inline constexpr std::uint8_t kOffhandContainer = 34;
inline constexpr std::size_t kDestinationRecordSize = 0x20;
inline constexpr std::size_t kDestinationNameStorageSize = 0x18;
inline constexpr std::size_t kMaxDestinationNameSize = 128;

struct alignas(8) DestinationRecord {
    std::array<std::byte, kDestinationRecordSize> bytes{};
};
static_assert(sizeof(DestinationRecord) == kDestinationRecordSize);
static_assert(alignof(DestinationRecord) >= alignof(std::uint64_t));

struct FilterResult {
    std::size_t keptCount{};
    std::size_t offhandCount{};
};

struct DestinationNameView {
    const char* data{};
    std::size_t size{};
    bool valid{};
};

[[nodiscard]] inline DestinationNameView destinationName(
    const DestinationRecord& record
) noexcept {
    const auto tag = std::to_integer<std::uint8_t>(record.bytes[0]);

    // Android libc++ basic_string<char> layout used by both validated builds:
    // short: bit0=0, size=tag>>1, bytes begin at +1
    // long : bit0=1, size at +0x08, pointer at +0x10
    if ((tag & 1U) == 0U) {
        const std::size_t size = tag >> 1U;
        if (size > (kDestinationNameStorageSize - 1U)) {
            return {};
        }
        return {
            reinterpret_cast<const char*>(record.bytes.data() + 1U),
            size,
            true,
        };
    }

    std::size_t size = 0;
    const char* data = nullptr;
    std::memcpy(&size, record.bytes.data() + 0x08, sizeof(size));
    std::memcpy(&data, record.bytes.data() + 0x10, sizeof(data));
    if (
        data == nullptr || size == 0 ||
        size > kMaxDestinationNameSize
    ) {
        return {};
    }
    return {data, size, true};
}

[[nodiscard]] inline char asciiLower(char value) noexcept {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A'))
        : value;
}

[[nodiscard]] inline bool containsAsciiCaseInsensitive(
    const char* data,
    std::size_t size,
    std::string_view needle
) noexcept {
    if (
        data == nullptr || needle.empty() ||
        size < needle.size()
    ) {
        return false;
    }

    for (std::size_t start = 0; start + needle.size() <= size; ++start) {
        bool match = true;
        for (std::size_t index = 0; index < needle.size(); ++index) {
            if (
                asciiLower(data[start + index]) !=
                asciiLower(needle[index])
            ) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] inline bool isOffhandRecord(
    const DestinationRecord& record
) noexcept {
    const auto name = destinationName(record);
    return name.valid &&
        containsAsciiCaseInsensitive(name.data, name.size, "offhand");
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
