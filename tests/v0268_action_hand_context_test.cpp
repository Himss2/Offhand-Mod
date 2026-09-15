#include "runtime/ActionHandContext.hpp"

#include <cassert>

using namespace levioffhand::runtime;

int main() {
    assert(!currentScopedAction().has_value());

    {
        ScopedActionHand outer(ActionHand::OffHand, ActionKind::UseAir);
        const auto first = currentScopedAction();
        assert(first.has_value());
        assert(first->hand == ActionHand::OffHand);
        assert(first->kind == ActionKind::UseAir);

        {
            ScopedActionHand inner(ActionHand::MainHand, ActionKind::AttackEntity);
            const auto nested = currentScopedAction();
            assert(nested.has_value());
            assert(nested->hand == ActionHand::MainHand);
            assert(nested->kind == ActionKind::AttackEntity);
        }

        const auto restored = currentScopedAction();
        assert(restored.has_value());
        assert(restored->hand == ActionHand::OffHand);
        assert(restored->kind == ActionKind::UseAir);
    }
    assert(!currentScopedAction().has_value());

    auto& session = currentActionSession();
    session.cancel();
    assert(!session.active());

    session.begin(
        ActionSessionKind::Mining,
        ActionHand::OffHand,
        0x1111,
        34,
        0xAABBCCDD,
        120
    );
    assert(session.active());
    assert(session.kind() == ActionSessionKind::Mining);
    assert(session.hand() == ActionHand::OffHand);
    assert(session.startTick() == 120);
    assert(session.matches(0x1111, 34, 0xAABBCCDD));
    assert(!session.matches(0x2222, 34, 0xAABBCCDD));
    assert(!session.matches(0x1111, 0, 0xAABBCCDD));
    assert(!session.matches(0x1111, 34, 0xDDCCBBAA));

    // A session never changes hand implicitly: a new action must cancel/end first.
    const bool replacedWhileActive = session.tryBegin(
        ActionSessionKind::UsingItem,
        ActionHand::MainHand,
        0x3333,
        0,
        0,
        121
    );
    assert(!replacedWhileActive);
    assert(session.hand() == ActionHand::OffHand);
    assert(session.kind() == ActionSessionKind::Mining);

    session.cancel();
    assert(!session.active());
    assert(session.kind() == ActionSessionKind::None);

    assert(session.tryBegin(
        ActionSessionKind::UsingItem,
        ActionHand::MainHand,
        0x3333,
        0,
        0,
        121
    ));
    assert(session.hand() == ActionHand::MainHand);
    session.finish();
    assert(!session.active());

    return 0;
}
