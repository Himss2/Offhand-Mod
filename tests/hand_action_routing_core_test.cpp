#include "runtime/HandActionRoutingCore.hpp"

#include <cassert>

using levioffhand::runtime::ActionHand;
using levioffhand::runtime::selectAttackHand;
using levioffhand::runtime::selectMiningHand;
using levioffhand::runtime::selectUseHand;

int main() {
    static_assert(selectAttackHand(true, true) == ActionHand::Main);
    static_assert(selectAttackHand(true, false) == ActionHand::Main);
    static_assert(selectAttackHand(false, true) == ActionHand::Off);
    static_assert(selectAttackHand(false, false) == ActionHand::Main);

    static_assert(selectMiningHand(true, true) == ActionHand::Main);
    static_assert(selectMiningHand(true, false) == ActionHand::Main);
    static_assert(selectMiningHand(false, true) == ActionHand::Off);
    static_assert(selectMiningHand(false, false) == ActionHand::Main);

    static_assert(selectUseHand(true, true) == ActionHand::Main);
    static_assert(selectUseHand(true, false) == ActionHand::Main);
    static_assert(selectUseHand(false, true) == ActionHand::Off);
    static_assert(selectUseHand(false, false) == ActionHand::Vanilla);

    assert(selectAttackHand(false, true) == ActionHand::Off);
    assert(selectMiningHand(false, true) == ActionHand::Off);
    assert(selectUseHand(false, true) == ActionHand::Off);
    return 0;
}
