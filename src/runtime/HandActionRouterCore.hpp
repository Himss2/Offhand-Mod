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

} // namespace levioffhand::runtime
