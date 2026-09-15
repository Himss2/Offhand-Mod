#include "runtime/HandActionPolicy.hpp"

#include <cassert>

using levioffhand::runtime::ActionHand;
using levioffhand::runtime::selectAttackHand;
using levioffhand::runtime::selectMiningHand;
using levioffhand::runtime::selectUseHand;

int main() {
    assert(selectAttackHand(true, true) == ActionHand::MainHand);
    assert(selectAttackHand(true, false) == ActionHand::MainHand);
    assert(selectAttackHand(false, true) == ActionHand::OffHand);
    assert(selectAttackHand(false, false) == ActionHand::MainHand);

    assert(selectMiningHand(true, true) == ActionHand::MainHand);
    assert(selectMiningHand(true, false) == ActionHand::MainHand);
    assert(selectMiningHand(false, true) == ActionHand::OffHand);
    assert(selectMiningHand(false, false) == ActionHand::MainHand);

    assert(selectUseHand(true, true) == ActionHand::MainHand);
    assert(selectUseHand(true, false) == ActionHand::MainHand);
    assert(selectUseHand(false, true) == ActionHand::OffHand);
    assert(selectUseHand(false, false) == ActionHand::MainHand);
    return 0;
}
