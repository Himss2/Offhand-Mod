#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <pl/Mod.hpp>

namespace pl::memory {
class HookHandle;
}

namespace levioffhand::runtime {

class HandActionRouter final {
public:
    static HandActionRouter& instance() noexcept;
    ~HandActionRouter();

    bool install(pl::mod::ModContext& context) noexcept;
    void uninstall(pl::mod::ModContext& context) noexcept;
    void setFeatureEnabled(bool enabled) noexcept;

    [[nodiscard]] bool featureEnabled() const noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    HandActionRouter() = default;

    static bool baseUseItemDetour(
        void* gameMode,
        const void* itemStack
    ) noexcept;

    static bool attackDetour(
        void* gameMode,
        void* entity,
        bool playPredictiveSound,
        const void* hitPosition
    ) noexcept;

    static const void* selectedItemDetour(
        const void* player
    ) noexcept;

    static void releaseUsingItemDetour(
        void* gameMode
    ) noexcept;

    void cancelActiveSession() noexcept;

    static HandActionRouter* sInstance;

    std::unique_ptr<pl::memory::HookHandle> mSelectedItemHook;
    std::unique_ptr<pl::memory::HookHandle> mReleaseUsingItemHook;
    std::unique_ptr<pl::memory::HookHandle> mAttackHook;
    std::unique_ptr<pl::memory::HookHandle> mBaseUseItemHook;

    // Keep the generic name mOriginal for the source contract: every routed
    // action has a transparent trampoline back into the original native path.
    void* mOriginal{nullptr};
    void* mAttackOriginal{nullptr};
    void* mSelectedItemOriginal{nullptr};
    void* mReleaseUsingItemOriginal{nullptr};

    std::uintptr_t mTarget{0};
    std::uintptr_t mAttackTarget{0};
    std::uintptr_t mSelectedItemTarget{0};
    std::uintptr_t mReleaseUsingItemTarget{0};

    std::atomic_bool mFeatureEnabled{false};
    std::atomic_bool mLoggedOffhandUse{false};
    std::atomic_bool mLoggedLongUse{false};
    std::atomic_bool mLoggedOffhandAttack{false};
};

} // namespace levioffhand::runtime
