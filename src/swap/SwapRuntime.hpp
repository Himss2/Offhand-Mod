#pragma once

#include <atomic>

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

private:
    SwapRuntime() = default;
    [[nodiscard]] bool drain(void* player,const void* selectedStack) noexcept;

    std::atomic_bool mFeatureEnabled{true};
    std::atomic_bool mInstalled{false};
    std::atomic_bool mSwapRequested{false};
    std::atomic_bool mSwapInProgress{false};
};

} // namespace levioffhand::swap
