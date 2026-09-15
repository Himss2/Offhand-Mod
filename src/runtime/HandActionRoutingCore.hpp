#pragma once

namespace levioffhand::runtime {

enum class ActionHand {
    Vanilla,
    Main,
    Off,
};

[[nodiscard]] constexpr ActionHand selectAttackHand(
    bool mainCombat,
    bool offCombat
) noexcept {
    if (mainCombat) {
        return ActionHand::Main;
    }
    if (offCombat) {
        return ActionHand::Off;
    }
    return ActionHand::Main;
}

[[nodiscard]] constexpr ActionHand selectMiningHand(
    bool mainSuitable,
    bool offSuitable
) noexcept {
    if (mainSuitable) {
        return ActionHand::Main;
    }
    if (offSuitable) {
        return ActionHand::Off;
    }
    return ActionHand::Main;
}

[[nodiscard]] constexpr ActionHand selectUseHand(
    bool mainHandled,
    bool offHandled
) noexcept {
    if (mainHandled) {
        return ActionHand::Main;
    }
    if (offHandled) {
        return ActionHand::Off;
    }
    return ActionHand::Vanilla;
}

} // namespace levioffhand::runtime
