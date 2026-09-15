#pragma once

#include "runtime/HandActionRoutingCore.hpp"

#include <cstdint>

namespace levioffhand::runtime {

enum class ActionKind {
    None,
    AttackEntity,
    MineBlock,
    UseAir,
    UseBlock,
    UseEntity,
};

struct SessionKey {
    void* owner{nullptr};
    void* stack{nullptr};
    std::uintptr_t target{0};
    int x{0};
    int y{0};
    int z{0};

    friend constexpr bool operator==(const SessionKey&, const SessionKey&) = default;
};

class ActionHandContext final {
public:
    [[nodiscard]] static bool active() noexcept;
    [[nodiscard]] static ActionHand hand() noexcept;
    [[nodiscard]] static ActionKind kind() noexcept;
    [[nodiscard]] static void* owner() noexcept;

    [[nodiscard]] static bool sessionActive() noexcept;
    [[nodiscard]] static ActionHand sessionHand() noexcept;
    [[nodiscard]] static ActionKind sessionKind() noexcept;
    [[nodiscard]] static SessionKey sessionKey() noexcept;
    [[nodiscard]] static bool matchesSession(const SessionKey& key) noexcept;

    static void beginSession(ActionKind kind, ActionHand hand, SessionKey key) noexcept;
    static void cancelSession() noexcept;
    static void completeSession() noexcept;

public:
    struct Frame {
        bool active{false};
        ActionHand hand{ActionHand::Vanilla};
        ActionKind kind{ActionKind::None};
        void* owner{nullptr};
    };

private:
    friend class ScopedActionHand;
    [[nodiscard]] static Frame push(ActionKind kind, ActionHand hand, void* owner) noexcept;
    static void restore(Frame frame) noexcept;
};

class ScopedActionHand final {
public:
    ScopedActionHand(ActionKind kind, ActionHand hand, void* owner) noexcept;
    ~ScopedActionHand();

    ScopedActionHand(const ScopedActionHand&) = delete;
    ScopedActionHand& operator=(const ScopedActionHand&) = delete;

private:
    ActionHandContext::Frame mPrevious{};
};

} // namespace levioffhand::runtime
