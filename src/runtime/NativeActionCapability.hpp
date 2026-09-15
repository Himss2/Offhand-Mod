#pragma once

#include <cstdint>
#include <pl/Mod.hpp>

namespace levioffhand::runtime {

class NativeActionCapability final {
public:
    static NativeActionCapability& instance() noexcept;

    bool install(pl::mod::ModContext& context) noexcept;
    void uninstall(pl::mod::ModContext& context) noexcept;

    [[nodiscard]] bool installed() const noexcept;
    [[nodiscard]] std::uintptr_t moduleBase() const noexcept;

    [[nodiscard]] void* itemFromStack(const void* stack) const noexcept;
    [[nodiscard]] void* offhandStack(void* player) const noexcept;
    [[nodiscard]] bool hasCombatCapability(const void* stack) const noexcept;
    [[nodiscard]] bool isSuitableForBlock(const void* stack, const void* block) const noexcept;
    [[nodiscard]] float destroySpeed(const void* stack, const void* block) const noexcept;
    [[nodiscard]] int getMaxUseDuration(const void* stack) const noexcept;

private:
    NativeActionCapability() = default;
    std::uintptr_t mModuleBase{0};
};

} // namespace levioffhand::runtime
