#pragma once

#include <atomic>
#include <cstdint>

#include <pl/Mod.hpp>

namespace levioffhand::runtime {

class OffhandSwapRuntime final {
public:
    static OffhandSwapRuntime& instance() noexcept;

    bool install(pl::mod::ModContext& context) noexcept;
    void uninstall(pl::mod::ModContext& context) noexcept;

    void setFeatureEnabled(bool enabled) noexcept;
    [[nodiscard]] bool featureEnabled() const noexcept;
    [[nodiscard]] bool installed() const noexcept;

    // Called from Levi's Java/overlay button dispatch thread.  This method is
    // intentionally queue-only: it must never touch Minecraft ItemStack state.
    void requestSwap() noexcept;

    // The selected-item detour can execute on several Minecraft threads.
    // Only the real "MINECRAFT MAIN" thread is allowed to drain the request.
    [[nodiscard]] bool shouldProcessPendingSwap() const noexcept;

    // Called only from RightUseRouter::selectedItemDetour on MINECRAFT MAIN.
    // selectedStack is obtained from the already-hooked native getter so this
    // runtime does not recurse through Player::getSelectedItem itself.
    [[nodiscard]] bool processPendingSwap(
        void* player,
        const void* selectedStack
    ) noexcept;

    using GetOffhandSlotFn = const void* (*)(const void*);
    using StackIsNullFn = bool (*)(const void*);
    using ItemStackCopyCtorFn = void (*)(void*, const void*);
    using ItemStackDtorFn = void (*)(void*);
    using SetItemInHandSlotFn =
        void (*)(void*, unsigned char, const void*);

private:
    OffhandSwapRuntime() = default;

    GetOffhandSlotFn mGetOffhandSlot{nullptr};
    StackIsNullFn mStackIsNull{nullptr};
    ItemStackCopyCtorFn mItemStackCopyCtor{nullptr};
    ItemStackDtorFn mItemStackDtor{nullptr};
    SetItemInHandSlotFn mSetItemInHandSlot{nullptr};

    std::atomic_bool mFeatureEnabled{true};
    std::atomic_bool mInstalled{false};
    std::atomic_bool mSwapRequested{false};
    std::atomic_bool mSwapInProgress{false};
};

} // namespace levioffhand::runtime
