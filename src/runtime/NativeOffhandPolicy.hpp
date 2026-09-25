#pragma once

#include <atomic>
#include <cstdint>

#include <pl/Mod.hpp>

namespace levioffhand::runtime {

class NativeOffhandPolicy final {
public:
    static NativeOffhandPolicy& instance() noexcept;
    ~NativeOffhandPolicy() = default;

    bool install(pl::mod::ModContext& context) noexcept;
    void uninstall(pl::mod::ModContext& context) noexcept;
    void setFeatureEnabled(bool enabled) noexcept;

    [[nodiscard]] bool featureEnabled() const noexcept;
    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] bool installed() const noexcept;

private:
    NativeOffhandPolicy() = default;

    bool applyPatch() noexcept;
    void revertPatch() noexcept;

    std::uintptr_t mInstruction{0};
    std::atomic_bool mFeatureEnabled{true};
    std::atomic_bool mPatchApplied{false};
};

} // namespace levioffhand::runtime
