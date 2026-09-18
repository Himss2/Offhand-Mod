#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <pl/Mod.hpp>

namespace pl::memory {
class HookHandle;
}

namespace levioffhand::runtime {

class RightUseRouter final {
public:
    static RightUseRouter& instance() noexcept;
    ~RightUseRouter();

    bool install(pl::mod::ModContext& context) noexcept;
    void uninstall(pl::mod::ModContext& context) noexcept;
    void setFeatureEnabled(bool enabled) noexcept;

    [[nodiscard]] bool featureEnabled() const noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    RightUseRouter() = default;

    static bool baseUseItemDetour(
        void* gameMode,
        const void* itemStack,
        unsigned char hand
    ) noexcept;

    static std::uint32_t useItemOnBlockDetour(
        void* gameMode,
        const void* context,
        const void* blockPos,
        int face,
        const void* hitPos,
        unsigned char hand,
        std::uintptr_t extra,
        bool flag
    ) noexcept;

    static const void* selectedItemDetour(const void* player) noexcept;
    static void releaseUsingItemDetour(void* gameMode) noexcept;

    static RightUseRouter* sInstance;

    std::unique_ptr<pl::memory::HookHandle> mSelectedItemHook;
    std::unique_ptr<pl::memory::HookHandle> mReleaseUsingItemHook;
    std::unique_ptr<pl::memory::HookHandle> mBaseUseItemHook;
    std::unique_ptr<pl::memory::HookHandle> mUseItemOnBlockHook;

    void* mSelectedItemOriginal{nullptr};
    void* mReleaseUsingItemOriginal{nullptr};
    void* mBaseUseItemOriginal{nullptr};
    void* mUseItemOnBlockOriginal{nullptr};

    std::uintptr_t mSelectedItemTarget{0};
    std::uintptr_t mReleaseUsingItemTarget{0};
    std::uintptr_t mBaseUseItemTarget{0};
    std::uintptr_t mUseItemOnBlockTarget{0};

    std::atomic_bool mFeatureEnabled{false};
    std::atomic_bool mLoggedOffhandUse{false};
    std::atomic_bool mLoggedBlockUse{false};
    std::atomic_bool mLoggedAttackOnlyYield{false};
    std::atomic_bool mLoggedLongUse{false};
};

} // namespace levioffhand::runtime
