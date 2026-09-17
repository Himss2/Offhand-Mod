#include "runtime/HandActionRouterCore.hpp"

#include <cassert>
#include <vector>

using namespace levioffhand::runtime;

namespace {
enum class Call { Main, Off, Fallback };
}

int main() {
    // Right-use: OFFHAND gets the first opportunity.
    {
        std::vector<Call> calls;
        const auto result = routeUseAction(
            [&]() {
                calls.push_back(Call::Main);
                return true;
            },
            [&]() {
                calls.push_back(Call::Off);
                const auto scope = currentScopedAction();
                assert(scope.has_value());
                assert(scope->hand == ActionHand::OffHand);
                assert(scope->kind == ActionKind::UseAir);
                return true;
            },
            [&]() { calls.push_back(Call::Fallback); }
        );
        assert(result.handled);
        assert(result.hand == ActionHand::OffHand);
        assert(!result.usedFallback);
        assert((calls == std::vector<Call>{Call::Off}));
        assert(!currentScopedAction().has_value());
    }

    // If OFFHAND passes, MAINHAND gets the unchanged vanilla attempt.
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
                return false;
            },
            [&]() { calls.push_back(Call::Fallback); }
        );
        assert(result.handled);
        assert(result.hand == ActionHand::MainHand);
        assert(!result.usedFallback);
        assert((calls == std::vector<Call>{Call::Off, Call::Main}));
        assert(!currentScopedAction().has_value());
    }

    // If neither hand handles use, preserve the fallback exactly once.
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
            [&]() { calls.push_back(Call::Fallback); }
        );
        assert(!result.handled);
        assert(result.usedFallback);
        assert((calls == std::vector<Call>{Call::Off, Call::Main, Call::Fallback}));
        assert(!currentScopedAction().has_value());
    }

    // Legacy attack selector remains deterministic for future RE work, but the
    // 26.50.1 compatibility path does not install an attack hook.
    {
        std::vector<Call> calls;
        const auto result = routeAttackAction(
            true,
            true,
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
                return false;
            }
        );
        assert(result.nativeResult);
        assert(result.hand == ActionHand::MainHand);
        assert((calls == std::vector<Call>{Call::Main}));
    }

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
                return false;
            }
        );
        assert(result.usedFallback);
        assert((calls == std::vector<Call>{Call::Fallback}));
    }

    // Legacy mining selector also remains unit-tested but uninstalled on
    // 26.50.1 until its complete ABI is revalidated.
    {
        std::vector<Call> calls;
        bool destroyed = false;
        const auto result = routeMiningStart(
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
                assert(scope->kind == ActionKind::MineBlock);
                return true;
            },
            [&]() {
                calls.push_back(Call::Fallback);
                return false;
            },
            destroyed
        );
        assert(result.nativeResult);
        assert(result.hand == ActionHand::OffHand);
        assert((calls == std::vector<Call>{Call::Off}));
    }

    return 0;
}
