#include "runtime/ActionHandContext.hpp"

namespace levioffhand::runtime {
namespace {

thread_local std::optional<ActionContextView> gScopedAction;
thread_local ActionSessionState gActionSession;

} // namespace

std::optional<ActionContextView> currentScopedAction() noexcept {
    return gScopedAction;
}

ScopedActionHand::ScopedActionHand(ActionHand hand, ActionKind kind) noexcept
    : mPrevious(gScopedAction) {
    gScopedAction = ActionContextView{hand, kind};
}

ScopedActionHand::~ScopedActionHand() noexcept {
    gScopedAction = mPrevious;
}

void ActionSessionState::begin(
    ActionSessionKind kind,
    ActionHand hand,
    ActionIdentityToken stackIdentity,
    ActionSlotToken slotIdentity,
    ActionTargetToken targetIdentity,
    std::uint64_t startTick
) noexcept {
    (void)tryBegin(
        kind,
        hand,
        stackIdentity,
        slotIdentity,
        targetIdentity,
        startTick
    );
}

bool ActionSessionState::tryBegin(
    ActionSessionKind kind,
    ActionHand hand,
    ActionIdentityToken stackIdentity,
    ActionSlotToken slotIdentity,
    ActionTargetToken targetIdentity,
    std::uint64_t startTick
) noexcept {
    if (active() || kind == ActionSessionKind::None) {
        return false;
    }

    mKind = kind;
    mHand = hand;
    mStackIdentity = stackIdentity;
    mSlotIdentity = slotIdentity;
    mTargetIdentity = targetIdentity;
    mStartTick = startTick;
    return true;
}

void ActionSessionState::finish() noexcept {
    cancel();
}

void ActionSessionState::cancel() noexcept {
    mKind = ActionSessionKind::None;
    mHand = ActionHand::MainHand;
    mStackIdentity = 0;
    mSlotIdentity = 0;
    mTargetIdentity = 0;
    mStartTick = 0;
}

bool ActionSessionState::active() const noexcept {
    return mKind != ActionSessionKind::None;
}

ActionSessionKind ActionSessionState::kind() const noexcept {
    return mKind;
}

ActionHand ActionSessionState::hand() const noexcept {
    return mHand;
}

std::uint64_t ActionSessionState::startTick() const noexcept {
    return mStartTick;
}

bool ActionSessionState::matches(
    ActionIdentityToken stackIdentity,
    ActionSlotToken slotIdentity,
    ActionTargetToken targetIdentity
) const noexcept {
    return active() &&
        mStackIdentity == stackIdentity &&
        mSlotIdentity == slotIdentity &&
        mTargetIdentity == targetIdentity;
}

ActionSessionState& currentActionSession() noexcept {
    return gActionSession;
}

} // namespace levioffhand::runtime
