#pragma once

#include <atomic>
#include <cstdint>

#include <pl/Mod.hpp>

namespace levioffhand::swap {

class SwapRuntime final {
public:
    static SwapRuntime& instance() noexcept;

    bool install(pl::mod::ModContext& context) noexcept;
    void uninstall(pl::mod::ModContext& context) noexcept;

    void setFeatureEnabled(bool enabled) noexcept;
    [[nodiscard]] bool featureEnabled() const noexcept;
    [[nodiscard]] bool installed() const noexcept;

    // UI thread: queue only.
    void requestSwap() noexcept;

    [[nodiscard]] bool hasPendingSwap() const noexcept;

    // Internal entry used only by this translation unit's preFrame detour.
    // Kept public to avoid making the UI or any gameplay router a friend.
    [[nodiscard]] bool drain(void* player,const void* selectedStack) noexcept;

private:
    SwapRuntime() = default;

    std::atomic_bool mFeatureEnabled{true};
    std::atomic_bool mInstalled{false};
    std::atomic_bool mSwapRequested{false};
    std::atomic_bool mSwapInProgress{false};
    std::atomic<std::uint64_t> mLastSwapStartNs{0};
};

} // namespace levioffhand::swap
