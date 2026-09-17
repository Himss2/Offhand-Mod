#pragma once

#include "runtime/ActionHandContext.hpp"

#ifdef __ANDROID__
#include <android/log.h>
#include <atomic>
#endif

#include <utility>

namespace levioffhand::runtime {

#ifdef __ANDROID__
namespace detail {

inline constexpr char kActionDiagTag[] = "Levi Offhand";

inline void logUseRouteOnce() noexcept {
    static std::atomic_bool logged{false};
    bool expected = false;
    if (logged.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kActionDiagTag,
            "[ActionDiag] use route offhand-first"
        );
    }
}

inline void logAttackRouteOnce(bool mainReal, bool offReal) noexcept {
    static std::atomic_bool logged{false};
    bool expected = false;
    if (logged.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kActionDiagTag,
            "[ActionDiag] attack route mainReal=%d offReal=%d",
            mainReal ? 1 : 0,
            offReal ? 1 : 0
        );
    }
}

inline void logMiningRouteOnce(bool mainSuitable, bool offSuitable) noexcept {
    static std::atomic_bool logged{false};
    bool expected = false;
    if (logged.compare_exchange_strong(expected, true, std::memory_order_relaxed)) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kActionDiagTag,
            "[ActionDiag] mining route mainSuitable=%d offSuitable=%d",
            mainSuitable ? 1 : 0,
            offSuitable ? 1 : 0
        );
    }
}

} // namespace detail
#endif

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
#ifdef __ANDROID__
    detail::logUseRouteOnce();
#endif

    // Java-like right click: give the offhand the first chance to handle use.
    // If it cannot handle the action, preserve the vanilla/mainhand path.
    {
        ScopedActionHand scope(ActionHand::OffHand, kind);
        if (std::forward<OffAttempt>(offAttempt)()) {
            return UseRouteResult{ActionHand::OffHand, true, false};
        }
    }

    {
        ScopedActionHand scope(ActionHand::MainHand, kind);
        if (std::forward<MainAttempt>(mainAttempt)()) {
            return UseRouteResult{ActionHand::MainHand, true, false};
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
#ifdef __ANDROID__
    detail::logAttackRouteOnce(
        mainHasRealCombatCapability,
        offHasRealCombatCapability
    );
#endif

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

#ifdef __ANDROID__
    detail::logMiningRouteOnce(mainSuitableForTarget, offSuitableForTarget);
#endif

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
