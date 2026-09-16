#include "runtime/HandActionRouterCore.hpp"

#include <cassert>
#include <vector>

using namespace levioffhand::runtime;

namespace {

enum class Call {
    Main,
    Off,
    Fallback,
};

} // namespace

int main() {
    {
        std::vector<Call> calls;
        const auto result = routeUseAction(
            [&]() {
                calls.push_back(Call::Main);
                const auto scope = currentScopedAction();
                assert(scope.has_value());
                assert(scope->hand == ActionHand::MainHand);
                return true;
            },
            [&]() {
                calls.push_back(Call::Off);
                return true;
            },
            [&]() {
                calls.push_back(Call::Fallback);
            }
        );
        assert(result.handled);
        assert(result.hand == ActionHand::MainHand);
        assert(!result.usedFallback);
        assert((calls == std::vector<Call>{Call::Main}));
        assert(!currentScopedAction().has_value());
    }

    {
        std::vector<Call> calls;
        const auto result = routeUseAction(
            [&]() {
                calls.push_back(Call::Main);
                return false;
            },
            [&]() {
                calls.push_back(Call::Off);
                const auto scope = currentScopedAction();
                assert(scope.has_value());
                assert(scope->hand == ActionHand::OffHand);
                assert(scope->kind == ActionKind::UseAir);
                return true;
            },
            [&]() {
                calls.push_back(Call::Fallback);
            }
        );
        assert(result.handled);
        assert(result.hand == ActionHand::OffHand);
        assert(!result.usedFallback);
        assert((calls == std::vector<Call>{Call::Main, Call::Off}));
        assert(!currentScopedAction().has_value());
    }

    {
        std::vector<Call> calls;
        const auto result = routeUseAction(
            [&]() {
                calls.push_back(Call::Main);
                return false;
            },
            [&]() {
                calls.push_back(Call::Off);
                return false;
            },
            [&]() {
                calls.push_back(Call::Fallback);
            }
        );
        assert(!result.handled);
        assert(result.hand == ActionHand::MainHand);
        assert(result.usedFallback);
        assert((calls == std::vector<Call>{Call::Main, Call::Off, Call::Fallback}));
        assert(!currentScopedAction().has_value());
    }

    // Attack: real mainhand combat capability wins even when offhand is also real.
    {
        std::vector<Call> calls;
        const auto result = routeAttackAction(
            true,
            true,
            [&]() {
                calls.push_back(Call::Main);
                const auto scope = currentScopedAction();
                assert(scope.has_value());
                assert(scope->hand == ActionHand::MainHand);
                assert(scope->kind == ActionKind::AttackEntity);
                return true;
            },
            [&]() {
                calls.push_back(Call::Off);
                return true;
            },
            [&]() {
                calls.push_back(Call::Fallback);
                return true;
            }
        );
        assert(result.nativeResult);
        assert(result.hand == ActionHand::MainHand);
        assert(!result.usedFallback);
        assert((calls == std::vector<Call>{Call::Main}));
        assert(!currentScopedAction().has_value());
    }

    // Attack: if mainhand has no real combat capability, offhand owns the attack.
    {
        std::vector<Call> calls;
        const auto result = routeAttackAction(
            false,
            true,
            [&]() {
                calls.push_back(Call::Main);
                return true;
            },
            [&]() {
                calls.push_back(Call::Off);
                const auto scope = currentScopedAction();
                assert(scope.has_value());
                assert(scope->hand == ActionHand::OffHand);
                assert(scope->kind == ActionKind::AttackEntity);
                return true;
            },
            [&]() {
                calls.push_back(Call::Fallback);
                return true;
            }
        );
        assert(result.nativeResult);
        assert(result.hand == ActionHand::OffHand);
        assert(!result.usedFallback);
        assert((calls == std::vector<Call>{Call::Off}));
        assert(!currentScopedAction().has_value());
    }

    // Attack: with no real combat capability in either hand, preserve vanilla
    // mainhand generic punch exactly once.
    {
        std::vector<Call> calls;
        const auto result = routeAttackAction(
            false,
            false,
            [&]() {
                calls.push_back(Call::Main);
                return true;
            },
            [&]() {
                calls.push_back(Call::Off);
                return true;
            },
            [&]() {
                calls.push_back(Call::Fallback);
                const auto scope = currentScopedAction();
                assert(!scope.has_value());
                return false;
            }
        );
        assert(!result.nativeResult);
        assert(result.hand == ActionHand::MainHand);
        assert(result.usedFallback);
        assert((calls == std::vector<Call>{Call::Fallback}));
        assert(!currentScopedAction().has_value());
    }

    return 0;
}
