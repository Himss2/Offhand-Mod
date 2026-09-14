#include "runtime/AutoInsertRoutingCore.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

using levioffhand::runtime::routing::DestinationRecord;
using levioffhand::runtime::routing::filterDestinations;
using levioffhand::runtime::routing::kOffhandContainer;

namespace {

DestinationRecord makeRecord(std::uint8_t container, std::uint8_t marker) {
    DestinationRecord record{};
    record.bytes[0] = static_cast<std::byte>(container);
    record.bytes[1] = static_cast<std::byte>(marker);
    return record;
}

std::uint8_t byte(const DestinationRecord& record, std::size_t index) {
    return std::to_integer<std::uint8_t>(record.bytes[index]);
}

} // namespace

int main() {
    {
        const std::array source{
            makeRecord(28, 1),
            makeRecord(kOffhandContainer, 2),
            makeRecord(29, 3),
            makeRecord(kOffhandContainer, 4),
            makeRecord(60, 5),
        };
        std::array<DestinationRecord, source.size()> output{};
        const auto result = filterDestinations(source.data(), source.size(), output.data());
        assert(result.offhandCount == 2);
        assert(result.keptCount == 3);
        assert(byte(output[0], 0) == 28 && byte(output[0], 1) == 1);
        assert(byte(output[1], 0) == 29 && byte(output[1], 1) == 3);
        assert(byte(output[2], 0) == 60 && byte(output[2], 1) == 5);
    }

    {
        const std::array source{
            makeRecord(28, 9),
            makeRecord(29, 8),
        };
        std::array<DestinationRecord, source.size()> output{};
        const auto result = filterDestinations(source.data(), source.size(), output.data());
        assert(result.offhandCount == 0);
        assert(result.keptCount == 2);
    }

    {
        const std::array source{
            makeRecord(kOffhandContainer, 7),
            makeRecord(kOffhandContainer, 6),
        };
        std::array<DestinationRecord, source.size()> output{};
        const auto result = filterDestinations(source.data(), source.size(), output.data());
        assert(result.offhandCount == 2);
        assert(result.keptCount == 0);
    }

    return 0;
}
