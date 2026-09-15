#pragma once

namespace levioffhand::runtime {

enum class ActionHand {
    MainHand,
    OffHand,
};

enum class ActionKind {
    AttackEntity,
    MineBlock,
    UseAir,
    UseBlock,
    UseEntity,
};

struct CapabilitySet {
    bool mainReal{false};
    bool offReal{false};
};

[[nodiscard]] constexpr ActionHand selectAttackHand(
    bool mainCombat,
    bool offCombat
) noexcept {
    if (mainCombat) {
        return ActionHand::MainHand;
    }
    if (offCombat) {
        return ActionHand::OffHand;
    }
    return ActionHand::MainHand;
}

[[nodiscard]] constexpr ActionHand selectMiningHand(
    bool mainSuitable,
    bool offSuitable
) noexcept {
    if (mainSuitable) {
        return ActionHand::MainHand;
    }
    if (offSuitable) {
        return ActionHand::OffHand;
    }
    return ActionHand::MainHand;
}

[[nodiscard]] constexpr ActionHand selectUseHand(
    bool mainHandled,
    bool offHandled
) noexcept {
    if (mainHandled) {
        return ActionHand::MainHand;
    }
    if (offHandled) {
        return ActionHand::OffHand;
    }
    return ActionHand::MainHand;
}

} // namespace levioffhand::runtime
