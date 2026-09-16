#include "runtime/NativeCapabilityProbe.hpp"

#include <android/log.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <link.h>

namespace levioffhand::runtime {
namespace {

constexpr char kMinecraftLibrary[] = "libminecraftpe.so";
constexpr char kLogTag[] = "Levi Offhand";

constexpr std::uintptr_t kSelectedItemRva = 0xF0B900C;
constexpr std::uintptr_t kOffhandSlotRva = 0xEC9D62C;
constexpr std::uintptr_t kStackIsNullRva = 0xF63E760;
constexpr std::uintptr_t kPlayerIsUsingItemRva = 0xF0B8094;
constexpr std::uintptr_t kItemInUseStackRva = 0xF0B80B4;
constexpr std::uintptr_t kStackDiffersForUseRva = 0xF6443F4;
constexpr std::uintptr_t kActorBlockSourceRva = 0xEC844CC;

constexpr std::size_t kGameModePlayerOffset = sizeof(void*);

// Exact ItemStackBase ABI: vptr at +0x0, WeakPtr<Item> at +0x8. WeakPtr holds
// SharedCounter<Item>* and the SharedCounter's first field is Item*.
constexpr std::size_t kItemStackItemOffset = sizeof(void*);

// Exact native virtual slots for 1.26.45.1. These are also guarded by the
// binary relocation contract so a game update cannot silently reuse them.
constexpr std::size_t kBlockSourceGetBlockSlot = 2;
constexpr std::size_t kItemGetAttackDamageSlot = 38;
constexpr std::size_t kItemGetDestroySpeedSlot = 89;

constexpr std::array<std::uint8_t, 16> kSelectedItemFingerprint{
    0x08, 0xB8, 0x42, 0xF9, 0x09, 0xC1, 0x42, 0x39,
    0x89, 0x00, 0x00, 0x34, 0xE0, 0xB1, 0x01, 0xF0,
};
constexpr std::array<std::uint8_t, 16> kOffhandSlotFingerprint{
    0xFD, 0x7B, 0xBF, 0xA9, 0xFD, 0x03, 0x00, 0x91,
    0x00, 0x20, 0x00, 0x91, 0x03, 0x30, 0x10, 0x94,
};
constexpr std::array<std::uint8_t, 16> kStackIsNullFingerprint{
    0x08, 0x8C, 0x40, 0x39, 0x08, 0x05, 0x00, 0x34,
    0xFD, 0x7B, 0xBE, 0xA9, 0xF3, 0x0B, 0x00, 0xF9,
};
constexpr std::array<std::uint8_t, 16> kPlayerIsUsingItemFingerprint{
    0xFD, 0x7B, 0xBF, 0xA9, 0xFD, 0x03, 0x00, 0x91,
    0x00, 0x60, 0x1B, 0x91, 0xB0, 0x19, 0x16, 0x94,
};
constexpr std::array<std::uint8_t, 8> kItemInUseStackFingerprint{
    0x00, 0x60, 0x1B, 0x91, 0xC0, 0x03, 0x5F, 0xD6,
};
constexpr std::array<std::uint8_t, 16> kStackDiffersForUseFingerprint{
    0x08, 0x88, 0x40, 0x39, 0x29, 0x88, 0x40, 0x39,
    0x1F, 0x01, 0x09, 0x6B, 0x01, 0x01, 0x00, 0x54,
};
constexpr std::array<std::uint8_t, 16> kActorBlockSourceFingerprint{
    0xFD, 0x7B, 0xBE, 0xA9, 0xF4, 0x4F, 0x01, 0xA9,
    0xFD, 0x03, 0x00, 0x91, 0xF3, 0x03, 0x00, 0xAA,
};

struct ModuleSearchState {
    std::uintptr_t base{0};
};

int moduleSearchCallback(
    dl_phdr_info* info,
    std::size_t,
    void* rawState
) noexcept {
    if (
        info == nullptr ||
        info->dlpi_name == nullptr ||
        std::strstr(info->dlpi_name, kMinecraftLibrary) == nullptr
    ) {
        return 0;
    }

    auto* state = static_cast<ModuleSearchState*>(rawState);
    state->base = static_cast<std::uintptr_t>(info->dlpi_addr);
    return 1;
}

[[nodiscard]] std::uintptr_t minecraftModuleBase() noexcept {
    ModuleSearchState state{};
    dl_iterate_phdr(&moduleSearchCallback, &state);
    return state.base;
}

[[nodiscard]] bool belongsToMinecraft(std::uintptr_t address) noexcept {
    if (address == 0) {
        return false;
    }

    Dl_info info{};
    return dladdr(reinterpret_cast<void*>(address), &info) != 0 &&
        info.dli_fname != nullptr &&
        std::strstr(info.dli_fname, kMinecraftLibrary) != nullptr;
}

template <std::size_t N>
[[nodiscard]] std::uintptr_t resolveExactTarget(
    std::uintptr_t rva,
    const std::array<std::uint8_t, N>& fingerprint
) noexcept {
    const std::uintptr_t base = minecraftModuleBase();
    if (base == 0) {
        return 0;
    }

    const std::uintptr_t target = base + rva;
    if (!belongsToMinecraft(target)) {
        return 0;
    }

    if (
        std::memcmp(
            reinterpret_cast<const void*>(target),
            fingerprint.data(),
            fingerprint.size()
        ) != 0
    ) {
        return 0;
    }

    return target;
}

} // namespace

NativeCapabilityProbe& NativeCapabilityProbe::instance() noexcept {
    static NativeCapabilityProbe value;
    return value;
}

bool NativeCapabilityProbe::install(pl::mod::ModContext& context) noexcept {
    if (available()) {
        return true;
    }

    const auto selectedItemTarget = resolveExactTarget(
        kSelectedItemRva,
        kSelectedItemFingerprint
    );
    const auto offhandSlotTarget = resolveExactTarget(
        kOffhandSlotRva,
        kOffhandSlotFingerprint
    );
    const auto stackIsNullTarget = resolveExactTarget(
        kStackIsNullRva,
        kStackIsNullFingerprint
    );
    const auto playerIsUsingTarget = resolveExactTarget(
        kPlayerIsUsingItemRva,
        kPlayerIsUsingItemFingerprint
    );
    const auto itemInUseStackTarget = resolveExactTarget(
        kItemInUseStackRva,
        kItemInUseStackFingerprint
    );
    const auto stackDiffersTarget = resolveExactTarget(
        kStackDiffersForUseRva,
        kStackDiffersForUseFingerprint
    );
    const auto actorBlockSourceTarget = resolveExactTarget(
        kActorBlockSourceRva,
        kActorBlockSourceFingerprint
    );

    mGetSelectedItem = reinterpret_cast<SelectedItemFn>(selectedItemTarget);
    mGetOffhandSlot = reinterpret_cast<OffhandItemFn>(offhandSlotTarget);
    mStackIsNull = reinterpret_cast<StackIsNullFn>(stackIsNullTarget);
    mPlayerIsUsingItem = reinterpret_cast<PlayerIsUsingItemFn>(
        playerIsUsingTarget
    );
    mItemInUseStack = reinterpret_cast<ItemInUseStackFn>(
        itemInUseStackTarget
    );
    mStackDiffersForUse = reinterpret_cast<StackDiffersForUseFn>(
        stackDiffersTarget
    );
    mGetBlockSource = reinterpret_cast<ActorBlockSourceFn>(
        actorBlockSourceTarget
    );
    mSelectedItemTarget = selectedItemTarget;

    mAvailable =
        mGetSelectedItem != nullptr &&
        mGetOffhandSlot != nullptr &&
        mStackIsNull != nullptr &&
        mPlayerIsUsingItem != nullptr &&
        mItemInUseStack != nullptr &&
        mStackDiffersForUse != nullptr &&
        mGetBlockSource != nullptr;

    if (!mAvailable) {
        context.logger().warn(
            "[NativeCapabilityProbe] fail-closed: exact 1.26.45.1 capability fingerprint mismatch"
        );
        uninstall(context);
        return false;
    }

    context.logger().info(
        "[NativeCapabilityProbe] exact-RVA hand/use/block accessors and Item capability ABI resolved"
    );
    return true;
}

void NativeCapabilityProbe::uninstall(pl::mod::ModContext& context) noexcept {
    mGetSelectedItem = nullptr;
    mGetOffhandSlot = nullptr;
    mStackIsNull = nullptr;
    mPlayerIsUsingItem = nullptr;
    mItemInUseStack = nullptr;
    mStackDiffersForUse = nullptr;
    mGetBlockSource = nullptr;
    mSelectedItemTarget = 0;
    mAvailable = false;
    context.logger().info("[NativeCapabilityProbe] accessors released");
}

bool NativeCapabilityProbe::available() const noexcept {
    return mAvailable;
}

bool NativeCapabilityProbe::validatePlayerObject(const void* player) const noexcept {
    if (player == nullptr) {
        return false;
    }

    const auto raw = reinterpret_cast<std::uintptr_t>(player);
    if ((raw & (alignof(void*) - 1U)) != 0U) {
        return false;
    }

    const void* vtable = nullptr;
    std::memcpy(&vtable, player, sizeof(vtable));
    return belongsToMinecraft(reinterpret_cast<std::uintptr_t>(vtable));
}

const void* NativeCapabilityProbe::itemFromStack(const void* stack) const noexcept {
    if (stack == nullptr || stackIsNull(stack)) {
        return nullptr;
    }

    const void* counter = nullptr;
    const auto* stackBytes = static_cast<const std::byte*>(stack);
    std::memcpy(&counter, stackBytes + kItemStackItemOffset, sizeof(counter));
    if (counter == nullptr) {
        return nullptr;
    }

    const void* item = nullptr;
    std::memcpy(&item, counter, sizeof(item));
    if (item == nullptr) {
        return nullptr;
    }

    const void* vtable = nullptr;
    std::memcpy(&vtable, item, sizeof(vtable));
    if (!belongsToMinecraft(reinterpret_cast<std::uintptr_t>(vtable))) {
        return nullptr;
    }

    return item;
}

const void* NativeCapabilityProbe::playerFromGameMode(void* gameMode) const noexcept {
    if (!available() || gameMode == nullptr) {
        return nullptr;
    }

    const void* player = nullptr;
    const auto* bytes = static_cast<const std::byte*>(gameMode);
    std::memcpy(&player, bytes + kGameModePlayerOffset, sizeof(player));
    if (!validatePlayerObject(player)) {
        return nullptr;
    }
    return player;
}

const void* NativeCapabilityProbe::mainhandStack(void* gameMode) const noexcept {
    const void* player = playerFromGameMode(gameMode);
    if (player == nullptr || mGetSelectedItem == nullptr) {
        return nullptr;
    }
    return mGetSelectedItem(player);
}

const void* NativeCapabilityProbe::offhandStack(void* gameMode) const noexcept {
    const void* player = playerFromGameMode(gameMode);
    return offhandStackForPlayer(player);
}

const void* NativeCapabilityProbe::offhandStackForPlayer(
    const void* player
) const noexcept {
    if (!available() || player == nullptr || mGetOffhandSlot == nullptr) {
        return nullptr;
    }
    return mGetOffhandSlot(player);
}

bool NativeCapabilityProbe::stackIsNull(const void* stack) const noexcept {
    return stack == nullptr || mStackIsNull == nullptr || mStackIsNull(stack);
}

bool NativeCapabilityProbe::playerIsUsingItem(const void* player) const noexcept {
    return player != nullptr &&
        mPlayerIsUsingItem != nullptr &&
        mPlayerIsUsingItem(player);
}

const void* NativeCapabilityProbe::itemInUseStack(const void* player) const noexcept {
    if (!available() || player == nullptr || mItemInUseStack == nullptr) {
        return nullptr;
    }
    return mItemInUseStack(player);
}

bool NativeCapabilityProbe::stackMatchesForUse(
    const void* lhs,
    const void* rhs
) const noexcept {
    if (
        lhs == nullptr ||
        rhs == nullptr ||
        mStackDiffersForUse == nullptr
    ) {
        return false;
    }
    return !mStackDiffersForUse(lhs, rhs);
}

bool NativeCapabilityProbe::realCombatCapability(const void* stack) const noexcept {
    const void* item = itemFromStack(stack);
    if (item == nullptr) {
        return false;
    }

    const void* vtable = nullptr;
    std::memcpy(&vtable, item, sizeof(vtable));
    if (!belongsToMinecraft(reinterpret_cast<std::uintptr_t>(vtable))) {
        return false;
    }

    const void* function = nullptr;
    const auto* vtableBytes = static_cast<const std::byte*>(vtable);
    std::memcpy(
        &function,
        vtableBytes + kItemGetAttackDamageSlot * sizeof(void*),
        sizeof(function)
    );
    if (!belongsToMinecraft(reinterpret_cast<std::uintptr_t>(function))) {
        return false;
    }

    const auto getAttackDamage = reinterpret_cast<GetAttackDamageFn>(
        const_cast<void*>(function)
    );
    return getAttackDamage(item) > 0;
}

const void* NativeCapabilityProbe::blockAt(
    const void* player,
    const void* blockPos
) const noexcept {
    if (
        !available() ||
        player == nullptr ||
        blockPos == nullptr ||
        mGetBlockSource == nullptr
    ) {
        return nullptr;
    }

    void* blockSource = mGetBlockSource(player);
    if (blockSource == nullptr) {
        return nullptr;
    }

    const void* vtable = nullptr;
    std::memcpy(&vtable, blockSource, sizeof(vtable));
    if (!belongsToMinecraft(reinterpret_cast<std::uintptr_t>(vtable))) {
        return nullptr;
    }

    const void* function = nullptr;
    const auto* vtableBytes = static_cast<const std::byte*>(vtable);
    std::memcpy(
        &function,
        vtableBytes + kBlockSourceGetBlockSlot * sizeof(void*),
        sizeof(function)
    );
    if (!belongsToMinecraft(reinterpret_cast<std::uintptr_t>(function))) {
        return nullptr;
    }

    const auto getBlock = reinterpret_cast<BlockSourceGetBlockFn>(
        const_cast<void*>(function)
    );
    return getBlock(blockSource, blockPos);
}

bool NativeCapabilityProbe::realMiningCapability(
    const void* stack,
    const void* block
) const noexcept {
    if (block == nullptr) {
        return false;
    }

    const void* item = itemFromStack(stack);
    if (item == nullptr) {
        return false;
    }

    const void* vtable = nullptr;
    std::memcpy(&vtable, item, sizeof(vtable));
    if (!belongsToMinecraft(reinterpret_cast<std::uintptr_t>(vtable))) {
        return false;
    }

    const void* function = nullptr;
    const auto* vtableBytes = static_cast<const std::byte*>(vtable);
    std::memcpy(
        &function,
        vtableBytes + kItemGetDestroySpeedSlot * sizeof(void*),
        sizeof(function)
    );
    if (!belongsToMinecraft(reinterpret_cast<std::uintptr_t>(function))) {
        return false;
    }

    const auto getDestroySpeed = reinterpret_cast<GetDestroySpeedFn>(
        const_cast<void*>(function)
    );
    return getDestroySpeed(item, stack, block) > 1.0F;
}

std::uintptr_t NativeCapabilityProbe::stackItemIdentity(
    const void* stack
) const noexcept {
    return reinterpret_cast<std::uintptr_t>(itemFromStack(stack));
}

std::uintptr_t NativeCapabilityProbe::selectedItemTarget() const noexcept {
    return mSelectedItemTarget;
}

} // namespace levioffhand::runtime
