#include "runtime/NativeActionCapability.hpp"

#include <cstddef>
#include <cstring>
#include <dlfcn.h>

#include <pl/memory/Signature.hpp>
#include <pl/memory/Patch.hpp>

namespace levioffhand::runtime {
namespace {
constexpr char kMinecraftLibrary[] = "libminecraftpe.so";
constexpr std::size_t kItemWeakPtrOffset = 0x08;

constexpr std::uintptr_t kHandContainerRva = 0xF0A9644;
constexpr std::uintptr_t kBaseItemGetAttackDamageRva = 0xF667504;
constexpr std::uintptr_t kBaseItemCanUseAsAttackRva = 0xF664D6C;
constexpr std::uintptr_t kBaseItemGetDestroySpeedRva = 0xF665908;
constexpr std::uintptr_t kBaseItemCanDestroySpecialRva = 0xF665914;

constexpr std::size_t kGetMaxUseDurationSlot = 6;
constexpr std::size_t kCanDestroySpecialSlot = 34;
constexpr std::size_t kGetAttackDamageSlot = 38;
constexpr std::size_t kCanUseAsAttackSlot = 83;
constexpr std::size_t kGetDestroySpeedSlot = 89;

constexpr char kAnchorSignature[] =
    "08 DA 94 94 00 E4 00 6F F5 03 13 AA 09 0A 80 52 "
    "A0 8E 8E 3C A8 56 40 79 A0 C2 00 91 A0 A2 81 3C";

[[nodiscard]] std::uintptr_t moduleBaseOf(std::uintptr_t address) noexcept {
    Dl_info info{};
    if (address == 0 || dladdr(reinterpret_cast<void*>(address), &info) == 0 ||
        info.dli_fbase == nullptr || info.dli_fname == nullptr ||
        std::strstr(info.dli_fname, kMinecraftLibrary) == nullptr) {
        return 0;
    }
    return reinterpret_cast<std::uintptr_t>(info.dli_fbase);
}

[[nodiscard]] bool bytesEqual(
    std::uintptr_t address,
    const unsigned char* bytes,
    std::size_t size
) noexcept {
    const auto actual = pl::memory::readBytes(address, size);
    return actual.size() == size && std::memcmp(actual.data(), bytes, size) == 0;
}

template <typename Fn>
[[nodiscard]] Fn virtualFn(void* object, std::size_t slot) noexcept {
    if (object == nullptr) return nullptr;
    auto*** p = reinterpret_cast<void***>(object);
    if (*p == nullptr) return nullptr;
    return reinterpret_cast<Fn>((*p)[slot]);
}
} // namespace

NativeActionCapability& NativeActionCapability::instance() noexcept {
    static NativeActionCapability value;
    return value;
}

bool NativeActionCapability::install(pl::mod::ModContext& context) noexcept {
    if (installed()) return true;
    const auto anchor = pl::memory::resolveSignature(kAnchorSignature, kMinecraftLibrary);
    mModuleBase = moduleBaseOf(anchor);
    if (mModuleBase == 0) {
        context.logger().error("[NativeActionCapability] target module resolution failed");
        return false;
    }

    static constexpr unsigned char attackDefault[]{0xE0,0x03,0x1F,0x2A,0xC0,0x03,0x5F,0xD6};
    static constexpr unsigned char canAttackDefault[]{0xE0,0x03,0x1F,0x2A,0xC0,0x03,0x5F,0xD6};
    static constexpr unsigned char destroyDefault[]{0x28,0x00,0x80,0x52,0x00,0x01,0x22,0x1E,0xC0,0x03,0x5F,0xD6};
    static constexpr unsigned char canDestroyDefault[]{0xE0,0x03,0x1F,0x2A,0xC0,0x03,0x5F,0xD6};
    static constexpr unsigned char handContainer[]{0xFD,0x7B,0xBF,0xA9,0xFD,0x03,0x00,0x91};

    const bool fingerprints =
        bytesEqual(mModuleBase + kBaseItemGetAttackDamageRva, attackDefault, sizeof(attackDefault)) &&
        bytesEqual(mModuleBase + kBaseItemCanUseAsAttackRva, canAttackDefault, sizeof(canAttackDefault)) &&
        bytesEqual(mModuleBase + kBaseItemGetDestroySpeedRva, destroyDefault, sizeof(destroyDefault)) &&
        bytesEqual(mModuleBase + kBaseItemCanDestroySpecialRva, canDestroyDefault, sizeof(canDestroyDefault)) &&
        bytesEqual(mModuleBase + kHandContainerRva, handContainer, sizeof(handContainer));
    if (!fingerprints) {
        context.logger().error("[NativeActionCapability] exact-build fingerprint mismatch");
        mModuleBase = 0;
        return false;
    }

    context.logger().info("[NativeActionCapability] native Item capability probes ready");
    return true;
}

void NativeActionCapability::uninstall(pl::mod::ModContext& context) noexcept {
    mModuleBase = 0;
    context.logger().info("[NativeActionCapability] probes removed");
}

bool NativeActionCapability::installed() const noexcept { return mModuleBase != 0; }
std::uintptr_t NativeActionCapability::moduleBase() const noexcept { return mModuleBase; }

void* NativeActionCapability::itemFromStack(const void* stack) const noexcept {
    if (stack == nullptr) return nullptr;
    void* weakStorage = nullptr;
    std::memcpy(&weakStorage, static_cast<const std::byte*>(stack) + kItemWeakPtrOffset, sizeof(weakStorage));
    if (weakStorage == nullptr) return nullptr;
    void* item = nullptr;
    std::memcpy(&item, weakStorage, sizeof(item));
    return item;
}

void* NativeActionCapability::offhandStack(void* player) const noexcept {
    if (!installed() || player == nullptr) return nullptr;
    using HandContainerFn = void* (*)(void* entityContext);
    const auto getHandContainer = reinterpret_cast<HandContainerFn>(mModuleBase + kHandContainerRva);
    void* container = getHandContainer(static_cast<std::byte*>(player) + 8);
    if (container == nullptr) return nullptr;
    using GetItemFn = const void* (*)(void*, int);
    const auto getItem = virtualFn<GetItemFn>(container, 8);
    if (getItem == nullptr) return nullptr;
    return const_cast<void*>(getItem(container, 1));
}

bool NativeActionCapability::hasCombatCapability(const void* stack) const noexcept {
    void* item = itemFromStack(stack);
    if (item == nullptr) return false;
    using AttackDamageFn = int (*)(const void*);
    using CanUseAsAttackFn = bool (*)(const void*);
    const auto damage = virtualFn<AttackDamageFn>(item, kGetAttackDamageSlot);
    const auto canAttack = virtualFn<CanUseAsAttackFn>(item, kCanUseAsAttackSlot);
    return (damage != nullptr && damage(item) > 0) || (canAttack != nullptr && canAttack(item));
}

float NativeActionCapability::destroySpeed(const void* stack, const void* block) const noexcept {
    void* item = itemFromStack(stack);
    if (item == nullptr || block == nullptr) return 1.0F;
    using DestroySpeedFn = float (*)(const void*, const void*, const void*);
    const auto fn = virtualFn<DestroySpeedFn>(item, kGetDestroySpeedSlot);
    return fn != nullptr ? fn(item, stack, block) : 1.0F;
}

bool NativeActionCapability::isSuitableForBlock(const void* stack, const void* block) const noexcept {
    void* item = itemFromStack(stack);
    if (item == nullptr || block == nullptr) return false;
    using CanDestroyFn = bool (*)(const void*, const void*);
    const auto canDestroy = virtualFn<CanDestroyFn>(item, kCanDestroySpecialSlot);
    const bool special = canDestroy != nullptr && canDestroy(item, block);
    return special || destroySpeed(stack, block) > 1.0001F;
}

int NativeActionCapability::getMaxUseDuration(const void* stack) const noexcept {
    void* item = itemFromStack(stack);
    if (item == nullptr) return 0;
    using Fn = int (*)(const void*, const void*);
    const auto fn = virtualFn<Fn>(item, kGetMaxUseDurationSlot);
    return fn != nullptr ? fn(item, stack) : 0;
}

} // namespace levioffhand::runtime
