#include "runtime/ActionHandContext.hpp"

namespace levioffhand::runtime {
namespace {
thread_local ActionHandContext::Frame gFrame{};
struct SessionState {
    bool active{false};
    ActionKind kind{ActionKind::None};
    ActionHand hand{ActionHand::Vanilla};
    SessionKey key{};
};
thread_local SessionState gSession{};
} // namespace

bool ActionHandContext::active() noexcept { return gFrame.active; }
ActionHand ActionHandContext::hand() noexcept { return gFrame.hand; }
ActionKind ActionHandContext::kind() noexcept { return gFrame.kind; }
void* ActionHandContext::owner() noexcept { return gFrame.owner; }

bool ActionHandContext::sessionActive() noexcept { return gSession.active; }
ActionHand ActionHandContext::sessionHand() noexcept { return gSession.hand; }
ActionKind ActionHandContext::sessionKind() noexcept { return gSession.kind; }
SessionKey ActionHandContext::sessionKey() noexcept { return gSession.key; }
bool ActionHandContext::matchesSession(const SessionKey& key) noexcept {
    return gSession.active && gSession.key == key;
}

void ActionHandContext::beginSession(
    ActionKind kind,
    ActionHand hand,
    SessionKey key
) noexcept {
    gSession.active = true;
    gSession.kind = kind;
    gSession.hand = hand;
    gSession.key = key;
}

void ActionHandContext::cancelSession() noexcept { gSession = {}; }
void ActionHandContext::completeSession() noexcept { gSession = {}; }

ActionHandContext::Frame ActionHandContext::push(
    ActionKind kind,
    ActionHand hand,
    void* owner
) noexcept {
    const Frame previous = gFrame;
    gFrame = Frame{true, hand, kind, owner};
    return previous;
}

void ActionHandContext::restore(Frame frame) noexcept { gFrame = frame; }

ScopedActionHand::ScopedActionHand(
    ActionKind kind,
    ActionHand hand,
    void* owner
) noexcept : mPrevious(ActionHandContext::push(kind, hand, owner)) {}

ScopedActionHand::~ScopedActionHand() { ActionHandContext::restore(mPrevious); }

} // namespace levioffhand::runtime
