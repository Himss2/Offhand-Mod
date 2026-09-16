#pragma once

#include "runtime/ActionHandContext.hpp"

#include <utility>

namespace levioffhand::runtime {

struct UseRouteResult {
    ActionHand hand{ActionHand::MainHand};
    bool handled{false};
    bool usedFallback{false};
};

template <typename MainAttempt, typename OffAttempt, typename Fallback>
[[nodiscard]] UseRouteResult routeUseAction(
    MainAttempt&& mainAttempt,
    OffAttempt&& offAttempt,
    Fallback&& fallback,
    ActionKind kind = ActionKind::UseAir
) {
    {
        ScopedActionHand scope(ActionHand::MainHand, kind);
        if (std::forward<MainAttempt>(mainAttempt)()) {
            return UseRouteResult{ActionHand::MainHand, true, false};
        }
    }

    {
        ScopedActionHand scope(ActionHand::OffHand, kind);
        if (std::forward<OffAttempt>(offAttempt)()) {
            return UseRouteResult{ActionHand::OffHand, true, false};
        }
    }

    std::forward<Fallback>(fallback)();
    return UseRouteResult{ActionHand::MainHand, false, true};
}

struct AttackRouteResult {
    ActionHand hand{ActionHand::MainHand};
    bool nativeResult{false};
    bool usedFallback{false};
};

template <typename MainAttempt, typename OffAttempt, typename Fallback>
[[nodiscard]] AttackRouteResult routeAttackAction(
    bool mainHasRealCombatCapability,
    bool offHasRealCombatCapability,
    MainAttempt&& mainAttempt,
    OffAttempt&& offAttempt,
    Fallback&& fallback
) {
    if (mainHasRealCombatCapability) {
        ScopedActionHand scope(ActionHand::MainHand, ActionKind::AttackEntity);
        return AttackRouteResult{
            ActionHand::MainHand,
            static_cast<bool>(std::forward<MainAttempt>(mainAttempt)()),
            false,
        };
    }

    if (offHasRealCombatCapability) {
        ScopedActionHand scope(ActionHand::OffHand, ActionKind::AttackEntity);
        return AttackRouteResult{
            ActionHand::OffHand,
            static_cast<bool>(std::forward<OffAttempt>(offAttempt)()),
            false,
        };
    }

    return AttackRouteResult{
        ActionHand::MainHand,
        static_cast<bool>(std::forward<Fallback>(fallback)()),
        true,
    };
}

struct MiningRouteResult {
    ActionHand hand{ActionHand::MainHand};
    bool nativeResult{false};
    bool usedFallback{false};
};

template <typename MainAttempt, typename OffAttempt, typename Fallback>
[[nodiscard]] MiningRouteResult routeMiningStart(
    bool mainSuitableForTarget,
    bool offSuitableForTarget,
    MainAttempt&& mainAttempt,
    OffAttempt&& offAttempt,
    Fallback&& fallback,
    bool& destroyedOut
) {
    (void)destroyedOut;

    if (mainSuitableForTarget) {
        ScopedActionHand scope(ActionHand::MainHand, ActionKind::MineBlock);
        return MiningRouteResult{
            ActionHand::MainHand,
            static_cast<bool>(std::forward<MainAttempt>(mainAttempt)()),
            false,
        };
    }

    if (offSuitableForTarget) {
        ScopedActionHand scope(ActionHand::OffHand, ActionKind::MineBlock);
        return MiningRouteResult{
            ActionHand::OffHand,
            static_cast<bool>(std::forward<OffAttempt>(offAttempt)()),
            false,
        };
    }

    return MiningRouteResult{
        ActionHand::MainHand,
        static_cast<bool>(std::forward<Fallback>(fallback)()),
        true,
    };
}

} // namespace levioffhand::runtime
