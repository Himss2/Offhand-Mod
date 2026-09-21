#pragma once

#include <cstdint>

#include <pl/Mod.hpp>

namespace levioffhand::swap {

class SwapEngine final {
public:
    static SwapEngine& instance() noexcept;

    bool install(pl::mod::ModContext& context) noexcept;
    void uninstall() noexcept;

    [[nodiscard]] bool ready() const noexcept;

    // Reads the selected MAINHAND stack without calling Player::getSelectedItem.
    // That getter is already hooked by RightUseRouter, so swap must remain
    // independent from that hook chain.
    [[nodiscard]] const void* selectedStack(const void* player) const noexcept;

    // Native storage exchange only. Caller must run on the swap preFrame pump.
    [[nodiscard]] bool swap(void* player, const void* selectedStack) noexcept;

private:
    SwapEngine() = default;

    using GetOffhandSlotFn = const void* (*)(const void*);
    using StackIsNullFn = bool (*)(const void*);
    using ItemStackCopyCtorFn = void (*)(void*, const void*);
    using ItemStackDtorFn = void (*)(void*);
    using SetItemInHandSlotFn = void (*)(void*, unsigned char, const void*);
    using SetSelectedItemFn = void (*)(void*, const void*);

    GetOffhandSlotFn mGetOffhandSlot{nullptr};
    StackIsNullFn mStackIsNull{nullptr};
    ItemStackCopyCtorFn mItemStackCopyCtor{nullptr};
    ItemStackDtorFn mItemStackDtor{nullptr};
    SetItemInHandSlotFn mSetItemInHandSlot{nullptr};
    SetSelectedItemFn mSetSelectedItem{nullptr};
    const void* mEmptyItem{nullptr};
};

} // namespace levioffhand::swap
