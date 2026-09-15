#pragma once

#include "runtime/HandActionPolicy.hpp"

#include <cstdint>
#include <optional>

namespace levioffhand::runtime {

struct ActionContextView {
    ActionHand hand{ActionHand::MainHand};
    ActionKind kind{ActionKind::UseAir};
};

[[nodiscard]] std::optional<ActionContextView> currentScopedAction() noexcept;

class ScopedActionHand final {
public:
    ScopedActionHand(ActionHand hand, ActionKind kind) noexcept;
    ~ScopedActionHand() noexcept;

    ScopedActionHand(const ScopedActionHand&) = delete;
    ScopedActionHand& operator=(const ScopedActionHand&) = delete;
    ScopedActionHand(ScopedActionHand&&) = delete;
    ScopedActionHand& operator=(ScopedActionHand&&) = delete;

private:
    std::optional<ActionContextView> mPrevious;
};

enum class ActionSessionKind {
    None,
    Mining,
    UsingItem,
    Charging,
    Blocking,
};

using ActionIdentityToken = std::uint64_t;
using ActionSlotToken = std::int32_t;
using ActionTargetToken = std::uint64_t;

class ActionSessionState final {
public:
    void begin(
        ActionSessionKind kind,
        ActionHand hand,
        ActionIdentityToken stackIdentity,
        ActionSlotToken slotIdentity,
        ActionTargetToken targetIdentity,
        std::uint64_t startTick
    ) noexcept;

    [[nodiscard]] bool tryBegin(
        ActionSessionKind kind,
        ActionHand hand,
        ActionIdentityToken stackIdentity,
        ActionSlotToken slotIdentity,
        ActionTargetToken targetIdentity,
        std::uint64_t startTick
    ) noexcept;

    void finish() noexcept;
    void cancel() noexcept;

    [[nodiscard]] bool active() const noexcept;
    [[nodiscard]] ActionSessionKind kind() const noexcept;
    [[nodiscard]] ActionHand hand() const noexcept;
    [[nodiscard]] std::uint64_t startTick() const noexcept;

    [[nodiscard]] bool matches(
        ActionIdentityToken stackIdentity,
        ActionSlotToken slotIdentity,
        ActionTargetToken targetIdentity
    ) const noexcept;

private:
    ActionSessionKind mKind{ActionSessionKind::None};
    ActionHand mHand{ActionHand::MainHand};
    ActionIdentityToken mStackIdentity{0};
    ActionSlotToken mSlotIdentity{0};
    ActionTargetToken mTargetIdentity{0};
    std::uint64_t mStartTick{0};
};

[[nodiscard]] ActionSessionState& currentActionSession() noexcept;

} // namespace levioffhand::runtime
