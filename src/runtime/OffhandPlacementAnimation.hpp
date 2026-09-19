#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace levioffhand::runtime {

// Visual-only first-person placement impulse.
//
// This state never mutates inventory, selected-item ownership, hand routing,
// or Minecraft transactions. RightUseRouter only triggers it after a native
// OFFHAND use-on result is accepted; the renderer consumes the normalized
// 0..1 progress and applies a short transform to block items.
class OffhandPlacementAnimation final {
public:
    static OffhandPlacementAnimation& instance() noexcept {
        static OffhandPlacementAnimation value;
        return value;
    }

    void trigger() noexcept {
        mStartedNs.store(nowNs(), std::memory_order_release);
    }

    void reset() noexcept {
        mStartedNs.store(0, std::memory_order_release);
    }

    [[nodiscard]] float progress() const noexcept {
        const std::uint64_t started =
            mStartedNs.load(std::memory_order_acquire);
        if (started == 0) {
            return 0.0f;
        }

        const std::uint64_t now = nowNs();
        if (now <= started) {
            return 0.0f;
        }

        const std::uint64_t elapsed = now - started;
        if (elapsed >= kDurationNs) {
            return 0.0f;
        }

        return static_cast<float>(elapsed) /
            static_cast<float>(kDurationNs);
    }

    [[nodiscard]] bool active() const noexcept {
        return progress() > 0.0f;
    }

    static constexpr std::uint64_t durationMilliseconds() noexcept {
        return kDurationNs / 1'000'000ULL;
    }

private:
    OffhandPlacementAnimation() = default;

    [[nodiscard]] static std::uint64_t nowNs() noexcept {
        using namespace std::chrono;
        return static_cast<std::uint64_t>(
            duration_cast<nanoseconds>(
                steady_clock::now().time_since_epoch()
            ).count()
        );
    }

    static constexpr std::uint64_t kDurationNs = 220'000'000ULL;
    std::atomic<std::uint64_t> mStartedNs{0};
};

} // namespace levioffhand::runtime
