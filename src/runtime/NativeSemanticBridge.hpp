#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <pl/Mod.hpp>

namespace pl::memory {
class HookHandle;
}

namespace levioffhand::runtime {

class NativeSemanticBridge final {
public:
    static NativeSemanticBridge& instance() noexcept;
    ~NativeSemanticBridge();

    bool install(pl::mod::ModContext& context) noexcept;
    void uninstall(pl::mod::ModContext& context) noexcept;
    void setFeatureEnabled(bool enabled) noexcept;

    [[nodiscard]] bool featureEnabled() const noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    NativeSemanticBridge() = default;

    static bool upperUseDetour(
        void* controller,
        const void* inputFlags,
        const void* interaction,
        const void* target
    ) noexcept;

    static float destroyRateContextDetour(const void* context) noexcept;
    static const void* selectedItemDetour(const void* player) noexcept;
    static void releaseUsingItemDetour(void* gameMode) noexcept;

    void clearBridgeUseSession() noexcept;

    static NativeSemanticBridge* sInstance;

    std::unique_ptr<pl::memory::HookHandle> mUpperUseHook;
    std::unique_ptr<pl::memory::HookHandle> mDestroyRateHook;
    std::unique_ptr<pl::memory::HookHandle> mSelectedItemHook;
    std::unique_ptr<pl::memory::HookHandle> mReleaseUsingItemHook;

    void* mUpperUseOriginal{nullptr};
    void* mDestroyRateOriginal{nullptr};
    void* mSelectedItemOriginal{nullptr};
    void* mReleaseUsingItemOriginal{nullptr};

    std::uintptr_t mUpperUseTarget{0};
    std::uintptr_t mDestroyRateTarget{0};
    std::uintptr_t mSelectedItemTarget{0};
    std::uintptr_t mReleaseUsingItemTarget{0};

    std::atomic_bool mFeatureEnabled{false};
    std::atomic_bool mLoggedUpperUseOffhand{false};
    std::atomic_bool mLoggedDestroyRateOffhand{false};
    std::atomic_bool mLoggedAttackSelected{false};
};

} // namespace levioffhand::runtime
