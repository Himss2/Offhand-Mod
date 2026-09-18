#include "runtime/AutoInsertRoutingCore.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

using levioffhand::runtime::routing::DestinationRecord;
using levioffhand::runtime::routing::destinationName;
using levioffhand::runtime::routing::filterDestinations;
using levioffhand::runtime::routing::isOffhandRecord;

namespace {

DestinationRecord makeShortRecord(
    std::string_view name,
    std::uint8_t marker
) {
    assert(name.size() <= 23);
    DestinationRecord record{};
    record.bytes[0] = static_cast<std::byte>(
        static_cast<std::uint8_t>(name.size() << 1U)
    );
    std::memcpy(record.bytes.data() + 1, name.data(), name.size());
    record.bytes[0x18] = static_cast<std::byte>(marker);
    return record;
}

DestinationRecord makeLongRecord(
    std::string_view name,
    std::uint8_t marker
) {
    DestinationRecord record{};
    record.bytes[0] = static_cast<std::byte>(1U);
    const std::size_t size = name.size();
    const char* data = name.data();
    std::memcpy(record.bytes.data() + 0x08, &size, sizeof(size));
    std::memcpy(record.bytes.data() + 0x10, &data, sizeof(data));
    record.bytes[0x18] = static_cast<std::byte>(marker);
    return record;
}

std::uint8_t byte(const DestinationRecord& record, std::size_t index) {
    return std::to_integer<std::uint8_t>(record.bytes[index]);
}

} // namespace

int main() {
    {
        const std::array source{
            makeShortRecord("inventory", 1),
            makeShortRecord("minecraft:offhand", 2),
            makeShortRecord("hotbar", 3),
            makeShortRecord("OffHandContainer", 4),
            makeShortRecord("cursor", 5),
        };
        std::array<DestinationRecord, source.size()> output{};
        const auto result = filterDestinations(
            source.data(), source.size(), output.data()
        );
        assert(result.offhandCount == 2);
        assert(result.keptCount == 3);
        assert(byte(output[0], 0x18) == 1);
        assert(byte(output[1], 0x18) == 3);
        assert(byte(output[2], 0x18) == 5);
    }

    {
        static constexpr std::string_view longOffhand =
            "minecraft:automatic_destination/offhand";
        static constexpr std::string_view longInventory =
            "minecraft:automatic_destination/inventory";

        const auto off = makeLongRecord(longOffhand, 7);
        const auto inventory = makeLongRecord(longInventory, 8);
        assert(isOffhandRecord(off));
        assert(!isOffhandRecord(inventory));

        const auto view = destinationName(off);
        assert(view.valid);
        assert(view.size == longOffhand.size());
    }

    {
        // Regression: the old implementation compared bytes[0] with enum 34.
        // A non-offhand 17-character short string also has SSO tag 34 and must
        // no longer be filtered accidentally.
        const auto sameLength = makeShortRecord("12345678901234567", 9);
        assert(byte(sameLength, 0) == 34);
        assert(!isOffhandRecord(sameLength));
    }

    return 0;
}
