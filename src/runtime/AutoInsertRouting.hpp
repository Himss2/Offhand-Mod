#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <pl/Mod.hpp>

namespace pl::memory {
class HookHandle;
}

namespace levioffhand::runtime {

class AutoInsertRouting final {
public:
    static AutoInsertRouting& instance() noexcept;
    ~AutoInsertRouting();

    bool install(pl::mod::ModContext& context) noexcept;
    void uninstall(pl::mod::ModContext& context) noexcept;
    void setFeatureEnabled(bool enabled) noexcept;

    [[nodiscard]] bool featureEnabled() const noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    AutoInsertRouting() = default;

    static int plannerDetour(
        void* arg0,
        const void* arg1,
        const void* arg2,
        int amount,
        const void* destinationVector
    ) noexcept;

    static AutoInsertRouting* sInstance;

    std::unique_ptr<pl::memory::HookHandle> mHook;
    void* mOriginal{nullptr};
    std::uintptr_t mTarget{0};
    std::atomic_bool mFeatureEnabled{true};
    std::atomic_bool mLoggedFilter{false};
    std::atomic_bool mLoggedRetainedGuard{false};
};

} // namespace levioffhand::runtime
