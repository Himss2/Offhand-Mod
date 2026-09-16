#pragma once

#include <cstdint>

#include <pl/Mod.hpp>

namespace levioffhand::runtime {

class NativeCapabilityProbe final {
public:
    static NativeCapabilityProbe& instance() noexcept;

    bool install(pl::mod::ModContext& context) noexcept;
    void uninstall(pl::mod::ModContext& context) noexcept;

    [[nodiscard]] bool available() const noexcept;

    [[nodiscard]] const void* playerFromGameMode(void* gameMode) const noexcept;
    [[nodiscard]] const void* mainhandStack(void* gameMode) const noexcept;
    [[nodiscard]] const void* offhandStack(void* gameMode) const noexcept;
    [[nodiscard]] const void* offhandStackForPlayer(const void* player) const noexcept;

    [[nodiscard]] bool stackIsNull(const void* stack) const noexcept;
    [[nodiscard]] bool playerIsUsingItem(const void* player) const noexcept;
    [[nodiscard]] const void* itemInUseStack(const void* player) const noexcept;
    [[nodiscard]] bool stackMatchesForUse(
        const void* lhs,
        const void* rhs
    ) const noexcept;

    [[nodiscard]] std::uintptr_t selectedItemTarget() const noexcept;

private:
    NativeCapabilityProbe() = default;

    using SelectedItemFn = const void* (*)(const void* player);
    using OffhandItemFn = const void* (*)(const void* actor);
    using StackIsNullFn = bool (*)(const void* stack);
    using PlayerIsUsingItemFn = bool (*)(const void* player);
    using ItemInUseStackFn = const void* (*)(const void* player);
    using StackDiffersForUseFn = bool (*)(const void* lhs, const void* rhs);

    [[nodiscard]] bool validatePlayerObject(const void* player) const noexcept;

    SelectedItemFn mGetSelectedItem{nullptr};
    OffhandItemFn mGetOffhandSlot{nullptr};
    StackIsNullFn mStackIsNull{nullptr};
    PlayerIsUsingItemFn mPlayerIsUsingItem{nullptr};
    ItemInUseStackFn mItemInUseStack{nullptr};
    StackDiffersForUseFn mStackDiffersForUse{nullptr};
    std::uintptr_t mSelectedItemTarget{0};
    bool mAvailable{false};
};

} // namespace levioffhand::runtime
