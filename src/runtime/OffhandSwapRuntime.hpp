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

    // The selected-item gameplay hook already receives the current LocalPlayer.
    // Reuse that verified pointer instead of adding another client-instance hook.
    void observePlayer(const void* player) noexcept;

    // Java F-style swap: selected hotbar/mainhand <-> offhand.
    [[nodiscard]] bool swapNow() noexcept;

    using GetSelectedItemFn = const void* (*)(const void*);
    using GetOffhandSlotFn = const void* (*)(const void*);
    using StackIsNullFn = bool (*)(const void*);
    using ItemStackCopyCtorFn = void (*)(void*, const void*);
    using ItemStackDtorFn = void (*)(void*);
    using SetItemInHandSlotFn =
        void (*)(void*, unsigned char, const void*);

private:
    OffhandSwapRuntime() = default;

    GetSelectedItemFn mGetSelectedItem{nullptr};
    GetOffhandSlotFn mGetOffhandSlot{nullptr};
    StackIsNullFn mStackIsNull{nullptr};
    ItemStackCopyCtorFn mItemStackCopyCtor{nullptr};
    ItemStackDtorFn mItemStackDtor{nullptr};
    SetItemInHandSlotFn mSetItemInHandSlot{nullptr};

    std::atomic<const void*> mObservedPlayer{nullptr};
    std::atomic_bool mFeatureEnabled{true};
    std::atomic_bool mInstalled{false};
    std::atomic_bool mSwapInProgress{false};
};

} // namespace levioffhand::runtime
