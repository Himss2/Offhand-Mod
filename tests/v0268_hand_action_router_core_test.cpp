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

    return 0;
}
