#include "runtime/RightUseRouter.hpp"

#include "runtime/ActionHandContext.hpp"
#include "runtime/HandActionRouterCore.hpp"
#include "runtime/OffhandPlacementAnimation.hpp"

#include <android/log.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <memory>

#include <pl/memory/Hook.hpp>

namespace levioffhand::runtime {
namespace {

constexpr char kMinecraftLibrary[] = "libminecraftpe.so";
constexpr char kLogTag[] = "Levi Offhand";

// Minecraft Bedrock Android 1.26.51.1 (arm64-v8a)
// GNU Build ID: 712509dc14ccc233e91f267937dfb46ecdcc4b68
// SHA-256: b8a6351503d330628335a80e8131acd45291fa9a747465f0f34a31b2346847b4
//
// Native hand enum recovered from the 1.26.51.1 binary:
//   0 = main hand
//   1 = off hand
// Both GameMode::baseUseItem and the native use-on-block path carry this
// enum explicitly, so no physical inventory swap is necessary.
constexpr unsigned char kMainHand = 0;
constexpr unsigned char kOffHand = 1;

constexpr std::uintptr_t kUseItemOnBlockRva = 0xF8A1CC4;
constexpr std::uintptr_t kBaseUseItemRva = 0xF8A285C;
// Completion is distinct from release: native callback 0xFA05E30 invokes
// Item::useTimeDepleted (+0x2B8), then Player::setSelectedItem (+0x268).
constexpr std::uintptr_t kCompleteUsingItemRva = 0xF9E8094;
constexpr std::uintptr_t kSetSelectedItemRva = 0xF9F7850;
constexpr std::uintptr_t kHandTransactionRva = 0xF9E9DFC;
constexpr std::uintptr_t kReleaseCallbackRva = 0xF8A65FC;
constexpr std::uintptr_t kStopUsingItemRva = 0xF9E86C0;
constexpr std::uintptr_t kReleaseUsingItemRva = 0xF8A3204;
constexpr std::uintptr_t kSelectedItemRva = 0xF9F7824;
constexpr std::uintptr_t kOffhandSlotRva = 0xF579C2C;
constexpr std::uintptr_t kSetItemInHandSlotRva = 0xF579C50;
constexpr std::uintptr_t kStackIsNullRva = 0xFFA0F70;
constexpr std::uintptr_t kPlayerIsUsingItemRva = 0xF9E8D64;
constexpr std::uintptr_t kItemInUseStackRva = 0xF9E8D84;
constexpr std::uintptr_t kStackDiffersForUseRva = 0xFFA5B04;
constexpr std::uintptr_t kItemStackCopyCtorRva = 0xFF9D748;
constexpr std::uintptr_t kItemStackDtorRva = 0x85ADF98;

// Exact native long-use tick bridge, recovered from the uploaded 1.26.51.1
// ELF (SHA-256 b8a63515...6847b4).
//
// Player's virtual tick at 0xF9E6358 inlines getSelectedItem instead of
// calling Player::getSelectedItem.  At 0xF9E71A0 it loads the selected
// Inventory from Player selected-state +0xB8, selected slot +0x10, then calls
// Inventory vtable +0x40.  The concrete Inventory slot resolves to 0xF883B58;
// the return PC after the BLR is exactly 0xF9E71B4.
//
// Inventory's constructor 0xF881CB0 stores the owning Player at +0x158.
constexpr std::uintptr_t kInventoryGetItemRva = 0xF883B58;
constexpr std::uintptr_t kUseTickSelectedFetchRva = 0xF9E71A0;
constexpr std::uintptr_t kUseTickInventoryGetReturnRva = 0xF9E71B4;
constexpr std::size_t kInventoryOwnerOffset = 0x158;

// 1.26.51.1 Item virtual defaults used only as capability identities.
// MAINHAND ownership is based on concrete native action implementations, not
// broad data-driven ComponentItem booleans.  Relocated 1.26.51.1 primary
// vtables prove Item::use=+0x290, requiresInteract=+0x1A8 and _useOn=+0x418.
// Generic Item/ComponentItem entries do not claim the click; specialized
// overrides (FishingRod, Shears, etc.) do.
constexpr std::uintptr_t kBaseItemUseRva = 0xFF8429C;
constexpr std::uintptr_t kComponentItemUseRva = 0xFDA8274;
// WeaponItem overrides use but only returns its input: mov x0,x1; ret.
// Treating every override as a real action prevents Sword -> OFFHAND routing.
constexpr std::uintptr_t kWeaponItemNoopUseRva = 0xFD66F30;
constexpr std::uintptr_t kBaseItemRequiresInteractRva = 0xFF87F28;
constexpr std::uintptr_t kComponentItemRequiresInteractRva = 0xFDAA1FC;
constexpr std::uintptr_t kBaseItemUseOnRva = 0xFF84B84;
constexpr std::uintptr_t kComponentItemUseOnRva = 0xFDA8A20;

constexpr std::size_t kGameModePlayerOffset = sizeof(void*);
constexpr std::size_t kItemWeakPtrOffset = 0x08;
constexpr std::size_t kItemGetMaxUseDurationVtableOffset = 0x30;
constexpr std::size_t kItemGetAttackDamageVtableOffset = 0x130;
constexpr std::size_t kItemIsUseableVtableOffset = 0xB0;
constexpr std::size_t kItemRequiresInteractVtableOffset = 0x1A8;
constexpr std::size_t kItemUseVtableOffset = 0x290;
constexpr std::size_t kItemCanUseAsAttackVtableOffset = 0x298;
constexpr std::size_t kItemUseOnVtableOffset = 0x418;
constexpr std::size_t kItemStackStorageSize = 0x98;
constexpr std::size_t kItemStackCountOffset = 0x22;
constexpr std::size_t kItemMaxStackSizeOffset = 0xA8;
constexpr std::size_t kItemIdOffset = 0xAA;
constexpr std::int16_t kShearsItemId = 424;

constexpr std::array<std::uint8_t, 16> kUseItemOnBlockFingerprint{
    0xFD, 0x7B, 0xBA, 0xA9, 0xFC, 0x6F, 0x01, 0xA9,
    0xFA, 0x67, 0x02, 0xA9, 0xF8, 0x5F, 0x03, 0xA9,
};
constexpr std::array<std::uint8_t, 16> kBaseUseItemFingerprint{
    0xFF, 0x43, 0x04, 0xD1, 0xFD, 0x7B, 0x0D, 0xA9,
    0xFC, 0x5F, 0x0E, 0xA9, 0xF6, 0x57, 0x0F, 0xA9,
};
constexpr std::array<std::uint8_t, 16> kReleaseUsingItemFingerprint{
    0xFF, 0x03, 0x04, 0xD1, 0xFD, 0x7B, 0x0C, 0xA9,
    0xF7, 0x6B, 0x00, 0xF9, 0xF6, 0x57, 0x0E, 0xA9,
};
constexpr std::array<std::uint8_t, 16> kSelectedItemFingerprint{
    0x08, 0xB8, 0x42, 0xF9, 0x09, 0xC1, 0x42, 0x39,
    0x89, 0x00, 0x00, 0x34, 0x60, 0xD6, 0x01, 0xF0,
};
constexpr std::array<std::uint8_t, 16> kOffhandSlotFingerprint{
    0xFD, 0x7B, 0xBF, 0xA9, 0xFD, 0x03, 0x00, 0x91,
    0x00, 0x20, 0x00, 0x91, 0x95, 0xF8, 0x10, 0x94,
};
constexpr std::array<std::uint8_t, 16> kSetItemInHandSlotFingerprint{
    0x28, 0x1C, 0x00, 0x72, 0x00, 0x01, 0x00, 0x54,
    0x1F, 0x05, 0x00, 0x71, 0x61, 0x01, 0x00, 0x54,
};
constexpr std::array<std::uint8_t, 16> kStackIsNullFingerprint{
    0x08, 0x8C, 0x40, 0x39, 0xE8, 0x04, 0x00, 0x34,
    0xFD, 0x7B, 0xBE, 0xA9, 0xF3, 0x0B, 0x00, 0xF9,
};
constexpr std::array<std::uint8_t, 16> kPlayerIsUsingItemFingerprint{
    0xFD, 0x7B, 0xBF, 0xA9, 0xFD, 0x03, 0x00, 0x91,
    0x00, 0x60, 0x1B, 0x91, 0x80, 0xE0, 0x16, 0x94,
};
constexpr std::array<std::uint8_t, 8> kItemInUseStackFingerprint{
    0x00, 0x60, 0x1B, 0x91, 0xC0, 0x03, 0x5F, 0xD6,
};
constexpr std::array<std::uint8_t, 16> kStackDiffersForUseFingerprint{
    0x08, 0x88, 0x40, 0x39, 0x29, 0x88, 0x40, 0x39,
    0x1F, 0x01, 0x09, 0x6B, 0x01, 0x01, 0x00, 0x54,
};
constexpr std::array<std::uint8_t, 28> kItemStackCopyCtorFingerprint{
    0xFD, 0x7B, 0xBD, 0xA9, 0xF5, 0x0B, 0x00, 0xF9,
    0xF4, 0x4F, 0x02, 0xA9, 0xFD, 0x03, 0x00, 0x91,
    0xF5, 0x03, 0x01, 0xAA, 0xF3, 0x03, 0x00, 0xAA,
    0x6D, 0xFE, 0xFF, 0x97,
};
constexpr std::array<std::uint8_t, 28> kItemStackDtorFingerprint{
    0xFF, 0xC3, 0x00, 0xD1, 0xFD, 0x7B, 0x01, 0xA9,
    0xF4, 0x4F, 0x02, 0xA9, 0xFD, 0x43, 0x00, 0x91,
    0x54, 0xD0, 0x3B, 0xD5, 0xF3, 0x03, 0x00, 0xAA,
    0xE9, 0x56, 0x05, 0xD0,
};
constexpr std::array<std::uint8_t, 16> kInventoryGetItemFingerprint{
    0x81, 0x01, 0xF8, 0x37, 0x08, 0x24, 0x54, 0xA9,
    0x6A, 0x43, 0x99, 0x52, 0x6A, 0x0D, 0xA5, 0x72,
};
constexpr std::array<std::uint8_t, 24> kUseTickSelectedFetchFingerprint{
    0x00, 0x5D, 0x40, 0xF9, // ldr x0,[selected-state,#0xB8]
    0x01, 0x11, 0x40, 0xB9, // ldr w1,[selected-state,#0x10]
    0x09, 0x00, 0x40, 0xF9, // ldr x9,[x0]
    0x28, 0x21, 0x40, 0xF9, // ldr x8,[x9,#0x40]
    0x00, 0x01, 0x3F, 0xD6, // blr x8
    0xF4, 0x03, 0x00, 0xAA, // mov x20,x0
};

using BaseUseItemFn = bool (*)(void*, const void*, unsigned char);
using UseItemOnBlockFn = std::uint32_t (*)(
    void*,
    const void*,
    const void*,
    int,
    const void*,
    unsigned char,
    std::uintptr_t,
    bool
);
constexpr std::array<std::uint8_t, 16> kCompleteUsingItemFingerprint{
    0xFD,0x7B,0xBB,0xA9,0xFC,0x67,0x01,0xA9,
    0xF8,0x5F,0x02,0xA9,0xF6,0x57,0x03,0xA9,
};
constexpr auto kSetSelectedItemFingerprint = kCompleteUsingItemFingerprint;
constexpr std::array<std::uint8_t, 16> kStopUsingItemFingerprint{
    0xFD,0x7B,0xBB,0xA9,0xFC,0x0B,0x00,0xF9,
    0xF8,0x5F,0x02,0xA9,0xF6,0x57,0x03,0xA9,
};
constexpr std::array<std::uint8_t, 16> kHandTransactionFingerprint{
    0xFF,0x43,0x02,0xD1,0xFD,0x7B,0x05,0xA9,
    0xF7,0x33,0x00,0xF9,0xF6,0x57,0x07,0xA9,
};
using HandTransactionFn = void (*)(void*, unsigned char, void*, void (*)(void*), void*);
std::uintptr_t gReleaseCallback = 0;
thread_local const void* gReleasingPlayer = nullptr;
using CompleteUsingItemFn = void (*)(void*);
using SetSelectedItemFn = void (*)(void*, const void*);
CompleteUsingItemFn gStopUsingItem = nullptr;
thread_local const void* gUseWritebackPlayer = nullptr;
thread_local const void* gUseWritebackBefore = nullptr;

using ReleaseUsingItemFn = void (*)(void*);
using SelectedItemFn = const void* (*)(const void*);
using OffhandItemFn = const void* (*)(const void*);
using SetItemInHandSlotFn = void (*)(void*, unsigned char, const void*);
using StackIsNullFn = bool (*)(const void*);
using PlayerIsUsingItemFn = bool (*)(const void*);
using ItemInUseStackFn = const void* (*)(const void*);
using StackDiffersForUseFn = bool (*)(const void*, const void*);
using ItemStackCopyCtorFn = void (*)(void*, const void*);
using ItemStackDtorFn = void (*)(void*);
using InventoryGetItemFn = const void* (*)(const void*, int);
using GetMaxUseDurationFn = int (*)(const void*, const void*);
using GetAttackDamageFn = int (*)(const void*);
using ItemBoolFn = bool (*)(const void*);

OffhandItemFn gGetOffhandSlot = nullptr;
SetItemInHandSlotFn gSetItemInHandSlot = nullptr;
StackIsNullFn gStackIsNull = nullptr;
PlayerIsUsingItemFn gPlayerIsUsingItem = nullptr;
ItemInUseStackFn gItemInUseStack = nullptr;
StackDiffersForUseFn gStackDiffersForUse = nullptr;
ItemStackCopyCtorFn gItemStackCopyCtor = nullptr;
ItemStackDtorFn gItemStackDtor = nullptr;

thread_local bool gInsideBaseUse = false;
thread_local bool gInsideBlockUse = false;
thread_local const void* gScopedPlayer = nullptr;
thread_local const void* gSessionPlayer = nullptr;
thread_local void* gSessionGameMode = nullptr;
std::atomic_bool gLoggedUseTickBridge{false};

class ScopedBool final {
public:
    explicit ScopedBool(bool& value) noexcept
        : mValue(value), mPrevious(value) {
        mValue = true;
    }
    ~ScopedBool() noexcept { mValue = mPrevious; }
private:
    bool& mValue;
    bool mPrevious;
};

class ScopedPlayer final {
public:
    explicit ScopedPlayer(const void* player) noexcept
        : mPrevious(gScopedPlayer) {
        gScopedPlayer = player;
    }
    ~ScopedPlayer() noexcept { gScopedPlayer = mPrevious; }
private:
    const void* mPrevious;
};

struct ModuleSearchState {
    std::uintptr_t base{0};
};

int moduleSearchCallback(dl_phdr_info* info, std::size_t, void* rawState) noexcept {
    if (
        info == nullptr || info->dlpi_name == nullptr ||
        std::strstr(info->dlpi_name, kMinecraftLibrary) == nullptr
    ) {
        return 0;
    }
    static_cast<ModuleSearchState*>(rawState)->base =
        static_cast<std::uintptr_t>(info->dlpi_addr);
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
    const auto base = minecraftModuleBase();
    if (base == 0) {
        return 0;
    }
    const auto target = base + rva;
    if (!belongsToMinecraft(target)) {
        return 0;
    }
    return std::memcmp(
        reinterpret_cast<const void*>(target),
        fingerprint.data(),
        fingerprint.size()
    ) == 0 ? target : 0;
}

[[nodiscard]] std::uintptr_t resolveKnownBuildTarget(
    std::uintptr_t rva
) noexcept {
    const auto base = minecraftModuleBase();
    if (base == 0) {
        return 0;
    }

    const auto target = base + rva;
    return belongsToMinecraft(target) ? target : 0;
}

template <std::size_t N>
[[nodiscard]] std::uintptr_t resolveHookTarget(
    const char* name,
    std::uintptr_t rva,
    const std::array<std::uint8_t, N>& fingerprint,
    bool* usedLiveFallback = nullptr
) noexcept {
    if (usedLiveFallback != nullptr) {
        *usedLiveFallback = false;
    }

    const auto exact = resolveExactTarget(rva, fingerprint);
    if (exact != 0) {
        return exact;
    }

    // The stable helper fingerprints are validated before this function is
    // used, so a mismatch here means the known 1.26.51.1 entry-point prologue
    // has already been modified in memory (for example by another hook).
    // HookHandle is allowed to chain that live entry point instead of
    // disabling the entire right-use subsystem.
    const auto live = resolveKnownBuildTarget(rva);
    if (live != 0) {
        if (usedLiveFallback != nullptr) {
            *usedLiveFallback = true;
        }
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[RightUseRouter] %s pre-hooked target detected; chaining live 1.26.51.1 target RVA=0x%llX",
            name,
            static_cast<unsigned long long>(rva)
        );
    }
    return live;
}

[[nodiscard]] bool validObject(const void* object) noexcept {
    if (object == nullptr) {
        return false;
    }
    const void* vtable = nullptr;
    std::memcpy(&vtable, object, sizeof(vtable));
    return belongsToMinecraft(reinterpret_cast<std::uintptr_t>(vtable));
}

[[nodiscard]] const void* playerFromGameMode(void* gameMode) noexcept {
    if (gameMode == nullptr) {
        return nullptr;
    }
    const void* player = nullptr;
    const auto* bytes = static_cast<const std::byte*>(gameMode);
    std::memcpy(&player, bytes + kGameModePlayerOffset, sizeof(player));
    return validObject(player) ? player : nullptr;
}

[[nodiscard]] bool stackIsNull(const void* stack) noexcept {
    return stack == nullptr || gStackIsNull == nullptr || gStackIsNull(stack);
}

[[nodiscard]] std::uint8_t stackCount(const void* stack) noexcept {
    if (stack == nullptr) {
        return 0;
    }
    std::uint8_t count = 0;
    std::memcpy(
        &count,
        static_cast<const std::byte*>(stack) + kItemStackCountOffset,
        sizeof(count)
    );
    return count;
}

[[nodiscard]] bool stacksMatch(const void* lhs, const void* rhs) noexcept {
    return lhs != nullptr && rhs != nullptr && gStackDiffersForUse != nullptr &&
        !gStackDiffersForUse(lhs, rhs);
}

[[nodiscard]] bool useInputRepresentsSelected(
    const void* input,
    const void* selected
) noexcept {
    if (input == nullptr || selected == nullptr) {
        return false;
    }

    // The upper use dispatcher may materialize EMPTY_ITEM differently from
    // Player::getSelectedItem when MAINHAND is empty.  Native stack-difference
    // comparison is allowed to report those two empty representations as
    // different, but semantically they are the same MAINHAND context.  Treat
    // only the both-empty case as equivalent; non-empty items still require
    // the exact native use-stack match.
    if (stackIsNull(input) && stackIsNull(selected)) {
        return true;
    }

    return stacksMatch(input, selected);
}

[[nodiscard]] const void* itemFromStack(const void* stack) noexcept {
    if (stackIsNull(stack)) {
        return nullptr;
    }

    // ItemStackBase::mItem is a WeakPtr at +0x08.  The WeakPtr state begins
    // with the raw Item* on this exact build.
    const void* weakState = nullptr;
    std::memcpy(
        &weakState,
        static_cast<const std::byte*>(stack) + kItemWeakPtrOffset,
        sizeof(weakState)
    );
    if (weakState == nullptr) {
        return nullptr;
    }

    const void* item = nullptr;
    std::memcpy(&item, weakState, sizeof(item));
    return validObject(item) ? item : nullptr;
}

[[nodiscard]] bool itemIsShears(const void* item) noexcept {
    if (!validObject(item)) {
        return false;
    }

    std::uint8_t maxStackSize = 0;
    std::int16_t itemId = 0;
    std::memcpy(
        &maxStackSize,
        static_cast<const std::byte*>(item) + kItemMaxStackSizeOffset,
        sizeof(maxStackSize)
    );
    std::memcpy(
        &itemId,
        static_cast<const std::byte*>(item) + kItemIdOffset,
        sizeof(itemId)
    );

    return itemId == kShearsItemId && maxStackSize == 1;
}

template <typename Fn>
[[nodiscard]] Fn itemVirtual(
    const void* item,
    std::size_t byteOffset
) noexcept {
    if (!validObject(item)) {
        return nullptr;
    }

    const void* vtable = nullptr;
    std::memcpy(&vtable, item, sizeof(vtable));
    if (vtable == nullptr) {
        return nullptr;
    }

    Fn function = nullptr;
    std::memcpy(
        &function,
        static_cast<const std::byte*>(vtable) + byteOffset,
        sizeof(function)
    );
    if (
        function == nullptr ||
        !belongsToMinecraft(reinterpret_cast<std::uintptr_t>(function))
    ) {
        return nullptr;
    }
    return function;
}

[[nodiscard]] bool stackClaimsMainhandRightClick(
    const void* stack,
    bool* yieldedAttackOnly = nullptr,
    bool includeBlockUse = true
) noexcept {
    if (yieldedAttackOnly != nullptr) {
        *yieldedAttackOnly = false;
    }

    const void* item = itemFromStack(stack);
    if (item == nullptr) {
        return false;
    }

    // Shears keeps MAINHAND right-click ownership. On-device logs showed
    // that the generic attack-damage fallback could classify it as
    // attack-only and incorrectly invoke OFFHAND block placement.
    if (itemIsShears(item)) {
        return true;
    }

    const auto moduleBase = minecraftModuleBase();
    if (moduleBase == 0) {
        return false;
    }

    // First classify concrete native actions by virtual identity.  This must
    // happen before getMaxUseDuration: ComponentItem can carry non-zero use
    // duration data even for attack-oriented items such as Swords.
    const auto use = itemVirtual<void*>(item, kItemUseVtableOffset);
    const auto requiresInteract = itemVirtual<void*>(
        item, kItemRequiresInteractVtableOffset
    );
    const auto useOn = itemVirtual<void*>(item, kItemUseOnVtableOffset);

    const auto useAddress = reinterpret_cast<std::uintptr_t>(use);
    const auto requiresAddress =
        reinterpret_cast<std::uintptr_t>(requiresInteract);
    const auto useOnAddress = reinterpret_cast<std::uintptr_t>(useOn);

    const bool specializedUse =
        use != nullptr &&
        useAddress != moduleBase + kBaseItemUseRva &&
        useAddress != moduleBase + kComponentItemUseRva &&
        useAddress != moduleBase + kWeaponItemNoopUseRva;

    const bool specializedRequiresInteract =
        requiresInteract != nullptr &&
        requiresAddress != moduleBase + kBaseItemRequiresInteractRva &&
        requiresAddress != moduleBase + kComponentItemRequiresInteractRva;

    const bool specializedUseOn =
        useOn != nullptr &&
        useOnAddress != moduleBase + kBaseItemUseOnRva &&
        useOnAddress != moduleBase + kComponentItemUseOnRva;

    if (
        specializedUse ||
        specializedRequiresInteract ||
        (includeBlockUse && specializedUseOn)
    ) {
        return true;
    }

    // Axe/Pickaxe/Sword baseline: attack-oriented items with no specialized
    // native right-click action yield the click to OFFHAND.  This uses the
    // same virtual getAttackDamage path that DiggerItem overrides, so Sword
    // follows the already-working Axe behavior instead of ComponentItem use
    // metadata.  Trident/Shears/FishingRod are already returned above by
    // their specialized right-click virtuals.
    const auto getAttackDamage = itemVirtual<GetAttackDamageFn>(
        item, kItemGetAttackDamageVtableOffset
    );
    const int attackDamage =
        getAttackDamage != nullptr ? getAttackDamage(item) : 0;
    if (attackDamage > 0) {
        if (yieldedAttackOnly != nullptr) {
            *yieldedAttackOnly = true;
        }
        return false;
    }

    const auto getMaxUseDuration = itemVirtual<GetMaxUseDurationFn>(
        item, kItemGetMaxUseDurationVtableOffset
    );
    const int maxUseDuration =
        getMaxUseDuration != nullptr
        ? getMaxUseDuration(item, stack)
        : 0;

    if (maxUseDuration <= 0) {
        return false;
    }

    // Secondary fallback for attack-oriented items whose native attack
    // damage reports zero but whose Item ABI still marks them attack-capable.
    // Specialized right-click actions were already returned above.
    const auto canUseAsAttack = itemVirtual<ItemBoolFn>(
        item, kItemCanUseAsAttackVtableOffset
    );
    const bool attackOnly =
        canUseAsAttack != nullptr && canUseAsAttack(item);

    if (attackOnly) {
        if (yieldedAttackOnly != nullptr) {
            *yieldedAttackOnly = true;
        }
        return false;
    }

    return true;
}

class ScopedItemStackSnapshot final {
public:
    explicit ScopedItemStackSnapshot(const void* source) noexcept {
        if (
            source == nullptr ||
            gItemStackCopyCtor == nullptr ||
            gItemStackDtor == nullptr
        ) {
            return;
        }
        gItemStackCopyCtor(mStorage.data(), source);
        mConstructed = true;
    }

    ~ScopedItemStackSnapshot() noexcept {
        if (mConstructed && gItemStackDtor != nullptr) {
            gItemStackDtor(mStorage.data());
        }
    }

    ScopedItemStackSnapshot(const ScopedItemStackSnapshot&) = delete;
    ScopedItemStackSnapshot& operator=(const ScopedItemStackSnapshot&) = delete;

    [[nodiscard]] const void* get() const noexcept {
        return mConstructed ? mStorage.data() : nullptr;
    }

private:
    alignas(16) std::array<std::byte, kItemStackStorageSize> mStorage{};
    bool mConstructed{false};
};

[[nodiscard]] bool activeUseMatches(
    const void* player,
    const void* stack
) noexcept {
    if (
        player == nullptr || stack == nullptr ||
        gPlayerIsUsingItem == nullptr || gItemInUseStack == nullptr ||
        !gPlayerIsUsingItem(player)
    ) {
        return false;
    }

    const void* active = gItemInUseStack(player);
    return !stackIsNull(active) && stacksMatch(active, stack);
}

[[nodiscard]] const void* offhandStackForNativeUseTick(
    const void* inventory,
    std::uintptr_t returnAddress,
    bool featureEnabled
) noexcept {
    if (
        !featureEnabled ||
        inventory == nullptr ||
        gSessionPlayer == nullptr ||
        gGetOffhandSlot == nullptr
    ) {
        return nullptr;
    }

    const auto base = minecraftModuleBase();
    if (
        base == 0 ||
        returnAddress != base + kUseTickInventoryGetReturnRva
    ) {
        return nullptr;
    }

    const void* owner = nullptr;
    std::memcpy(
        &owner,
        static_cast<const std::byte*>(inventory) + kInventoryOwnerOffset,
        sizeof(owner)
    );
    if (owner == nullptr || owner != gSessionPlayer) {
        return nullptr;
    }

    const void* offStack = gGetOffhandSlot(owner);
    if (
        offStack == nullptr ||
        stackIsNull(offStack) ||
        !activeUseMatches(owner, offStack)
    ) {
        return nullptr;
    }

    return offStack;
}

void clearSession() noexcept {
    gSessionPlayer = nullptr;
    gSessionGameMode = nullptr;
}

} // namespace

RightUseRouter* RightUseRouter::sInstance = nullptr;

RightUseRouter::~RightUseRouter() = default;

RightUseRouter& RightUseRouter::instance() noexcept {
    static RightUseRouter value;
    return value;
}

bool RightUseRouter::install(pl::mod::ModContext& context) noexcept {
    if (installed()) {
        return true;
    }

    // Validate non-hook helper functions first.  These bytes are not supposed
    // to be patched by the right-use subsystem, so they are the exact-build
    // guard for Minecraft 1.26.51.1.
    const auto offhandTarget = resolveExactTarget(
        kOffhandSlotRva, kOffhandSlotFingerprint
    );
    const auto nullTarget = resolveExactTarget(
        kStackIsNullRva, kStackIsNullFingerprint
    );
    const auto usingTarget = resolveExactTarget(
        kPlayerIsUsingItemRva, kPlayerIsUsingItemFingerprint
    );
    const auto inUseTarget = resolveExactTarget(
        kItemInUseStackRva, kItemInUseStackFingerprint
    );
    const auto differsTarget = resolveExactTarget(
        kStackDiffersForUseRva, kStackDiffersForUseFingerprint
    );
    const auto copyCtorTarget = resolveExactTarget(
        kItemStackCopyCtorRva, kItemStackCopyCtorFingerprint
    );
    const auto dtorTarget = resolveExactTarget(
        kItemStackDtorRva, kItemStackDtorFingerprint
    );
    const auto setHandExact = resolveExactTarget(
        kSetItemInHandSlotRva, kSetItemInHandSlotFingerprint
    );
    const auto useTickFetchTarget = resolveExactTarget(
        kUseTickSelectedFetchRva, kUseTickSelectedFetchFingerprint
    );
    const auto setHandTarget =
        setHandExact != 0
        ? setHandExact
        : resolveKnownBuildTarget(kSetItemInHandSlotRva);

    if (
        offhandTarget == 0 || nullTarget == 0 ||
        usingTarget == 0 || inUseTarget == 0 || differsTarget == 0 ||
        copyCtorTarget == 0 || dtorTarget == 0 || setHandTarget == 0 ||
        useTickFetchTarget == 0
    ) {
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "[RightUseRouter] stable guard failed offhand=%d null=%d using=%d inUse=%d differs=%d copy=%d dtor=%d setHand=%d useTick=%d",
            offhandTarget != 0 ? 1 : 0,
            nullTarget != 0 ? 1 : 0,
            usingTarget != 0 ? 1 : 0,
            inUseTarget != 0 ? 1 : 0,
            differsTarget != 0 ? 1 : 0,
            copyCtorTarget != 0 ? 1 : 0,
            dtorTarget != 0 ? 1 : 0,
            setHandTarget != 0 ? 1 : 0,
            useTickFetchTarget != 0 ? 1 : 0
        );
        context.logger().warn(
            "[RightUseRouter] Minecraft 1.26.51.1 stable fingerprint validation failed; right-use disabled"
        );
        return false;
    }

    if (setHandExact == 0) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[RightUseRouter] Actor::setItemInHandSlot pre-hooked; using live 1.26.51.1 target RVA=0x%llX",
            static_cast<unsigned long long>(kSetItemInHandSlotRva)
        );
    }

    const auto stopTarget = resolveExactTarget(kStopUsingItemRva, kStopUsingItemFingerprint);
    if (stopTarget == 0) {
        context.logger().warn("[RightUseRouter] completion cancellation guard failed");
        return false;
    }
    const auto completeTarget = resolveHookTarget("Player::completeUsingItem", kCompleteUsingItemRva, kCompleteUsingItemFingerprint);
    const auto transactionTarget = resolveHookTarget("Player::handTransaction", kHandTransactionRva, kHandTransactionFingerprint);
    const auto setterTarget = resolveHookTarget("Player::setSelectedItem", kSetSelectedItemRva, kSetSelectedItemFingerprint);
    if (completeTarget == 0 || setterTarget == 0 || transactionTarget == 0) return false;

    // Only after the exact stable guard passes do we resolve hookable entry
    // points. Their prologues may already be changed in memory by a hook, so
    // use the known RVA and let Levi's HookHandle chain the live target.
    const auto selectedTarget = resolveHookTarget(
        "Player::getSelectedItem",
        kSelectedItemRva,
        kSelectedItemFingerprint
    );
    const auto inventoryGetTarget = resolveHookTarget(
        "Inventory::getItem",
        kInventoryGetItemRva,
        kInventoryGetItemFingerprint
    );
    bool blockUsePreHooked = false;
    const auto blockUseTarget = resolveHookTarget(
        "GameMode::useItemOnBlock",
        kUseItemOnBlockRva,
        kUseItemOnBlockFingerprint,
        &blockUsePreHooked
    );
    const auto useTarget = resolveHookTarget(
        "GameMode::baseUseItem",
        kBaseUseItemRva,
        kBaseUseItemFingerprint
    );
    const auto releaseTarget = resolveHookTarget(
        "GameMode::releaseUsingItem",
        kReleaseUsingItemRva,
        kReleaseUsingItemFingerprint
    );

    if (
        selectedTarget == 0 || inventoryGetTarget == 0 ||
        blockUseTarget == 0 || useTarget == 0 || releaseTarget == 0
    ) {
        context.logger().warn(
            "[RightUseRouter] Minecraft 1.26.51.1 live hook target resolution failed; right-use disabled"
        );
        return false;
    }

    gGetOffhandSlot = reinterpret_cast<OffhandItemFn>(offhandTarget);
    gSetItemInHandSlot = reinterpret_cast<SetItemInHandSlotFn>(setHandTarget);
    gStackIsNull = reinterpret_cast<StackIsNullFn>(nullTarget);
    gPlayerIsUsingItem = reinterpret_cast<PlayerIsUsingItemFn>(usingTarget);
    gItemInUseStack = reinterpret_cast<ItemInUseStackFn>(inUseTarget);
    gStackDiffersForUse = reinterpret_cast<StackDiffersForUseFn>(differsTarget);
    gItemStackCopyCtor = reinterpret_cast<ItemStackCopyCtorFn>(copyCtorTarget);
    gItemStackDtor = reinterpret_cast<ItemStackDtorFn>(dtorTarget);

    mSelectedItemTarget = selectedTarget;
    mInventoryGetItemTarget = inventoryGetTarget;
    mReleaseUsingItemTarget = releaseTarget;
    mBaseUseItemTarget = useTarget;
    mUseItemOnBlockTarget = blockUseTarget;
    mUseItemOnBlockPreHooked = blockUsePreHooked;
    sInstance = this;
    gStopUsingItem = reinterpret_cast<CompleteUsingItemFn>(stopTarget);
    gReleaseCallback = minecraftModuleBase() + kReleaseCallbackRva;

    mInventoryGetItemHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mInventoryGetItemTarget),
        reinterpret_cast<void*>(&RightUseRouter::inventoryGetItemDetour),
        &mInventoryGetItemOriginal,
        pl::memory::HookPriority::Normal
    );
    if (
        !mInventoryGetItemHook ||
        !mInventoryGetItemHook->installed() ||
        mInventoryGetItemOriginal == nullptr
    ) {
        context.logger().warn("[RightUseRouter] native long-use Inventory::getItem bridge hook failed");
        uninstall(context);
        return false;
    }

    mSelectedItemHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mSelectedItemTarget),
        reinterpret_cast<void*>(&RightUseRouter::selectedItemDetour),
        &mSelectedItemOriginal,
        pl::memory::HookPriority::Normal
    );
    if (!mSelectedItemHook || !mSelectedItemHook->installed() || mSelectedItemOriginal == nullptr) {
        context.logger().warn("[RightUseRouter] selected-item hook failed");
        uninstall(context);
        return false;
    }

    mReleaseUsingItemHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mReleaseUsingItemTarget),
        reinterpret_cast<void*>(&RightUseRouter::releaseUsingItemDetour),
        &mReleaseUsingItemOriginal,
        pl::memory::HookPriority::Normal
    );
    if (!mReleaseUsingItemHook || !mReleaseUsingItemHook->installed() || mReleaseUsingItemOriginal == nullptr) {
        context.logger().warn("[RightUseRouter] release-use hook failed");
        uninstall(context);
        return false;
    }

    mBaseUseItemHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mBaseUseItemTarget),
        reinterpret_cast<void*>(&RightUseRouter::baseUseItemDetour),
        &mBaseUseItemOriginal,
        pl::memory::HookPriority::Normal
    );
    if (!mBaseUseItemHook || !mBaseUseItemHook->installed() || mBaseUseItemOriginal == nullptr) {
        context.logger().warn("[RightUseRouter] baseUseItem hook failed");
        uninstall(context);
        return false;
    }

    mUseItemOnBlockHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mUseItemOnBlockTarget),
        reinterpret_cast<void*>(&RightUseRouter::useItemOnBlockDetour),
        &mUseItemOnBlockOriginal,
        pl::memory::HookPriority::Normal
    );
    if (
        !mUseItemOnBlockHook || !mUseItemOnBlockHook->installed() ||
        mUseItemOnBlockOriginal == nullptr
    ) {
        context.logger().warn("[RightUseRouter] use-on-block hook failed");
        uninstall(context);
        return false;
    }

    mHandTransactionHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(transactionTarget), reinterpret_cast<void*>(&RightUseRouter::handTransactionDetour),
        &mHandTransactionOriginal, pl::memory::HookPriority::Normal);
    mSetSelectedItemHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(setterTarget), reinterpret_cast<void*>(&RightUseRouter::setSelectedItemDetour),
        &mSetSelectedItemOriginal, pl::memory::HookPriority::Normal);
    mCompleteUsingItemHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(completeTarget), reinterpret_cast<void*>(&RightUseRouter::completeUsingItemDetour),
        &mCompleteUsingItemOriginal, pl::memory::HookPriority::Normal);
    if (!mHandTransactionHook->installed() || !mHandTransactionOriginal ||
        !mSetSelectedItemHook->installed() || !mCompleteUsingItemHook->installed() ||
        !mSetSelectedItemOriginal || !mCompleteUsingItemOriginal) {
        context.logger().warn("[RightUseRouter] consumption hooks failed");
        uninstall(context);
        return false;
    }

    mFeatureEnabled.store(true, std::memory_order_release);
    mLoggedOffhandUse.store(false, std::memory_order_relaxed);
    mLoggedBlockUse.store(false, std::memory_order_relaxed);
    mLoggedAttackOnlyYield.store(false, std::memory_order_relaxed);
    mLoggedLongUse.store(false, std::memory_order_relaxed);
    mLoggedOffhandWriteback.store(false, std::memory_order_relaxed);
    gLoggedUseTickBridge.store(false, std::memory_order_relaxed);
    context.logger().info(
        "[RightUseRouter] Minecraft 1.26.51.1 right-use active: MAINHAND first, OFFHAND fallback; left-click remains vanilla mainhand"
    );
    return true;
}

void RightUseRouter::uninstall(pl::mod::ModContext& context) noexcept {
    mFeatureEnabled.store(false, std::memory_order_release);
    clearSession();

    if (mHandTransactionHook) { mHandTransactionHook->reset(); mHandTransactionHook.reset(); }
    mHandTransactionOriginal = nullptr;
    gReleaseCallback = 0;
    if (mCompleteUsingItemHook) { mCompleteUsingItemHook->reset(); mCompleteUsingItemHook.reset(); }
    if (mSetSelectedItemHook) { mSetSelectedItemHook->reset(); mSetSelectedItemHook.reset(); }
    mCompleteUsingItemOriginal = nullptr;
    mSetSelectedItemOriginal = nullptr;
    gStopUsingItem = nullptr;

    if (mUseItemOnBlockHook) {
        mUseItemOnBlockHook->reset();
        mUseItemOnBlockHook.reset();
    }
    if (mBaseUseItemHook) {
        mBaseUseItemHook->reset();
        mBaseUseItemHook.reset();
    }
    if (mReleaseUsingItemHook) {
        mReleaseUsingItemHook->reset();
        mReleaseUsingItemHook.reset();
    }
    if (mSelectedItemHook) {
        mSelectedItemHook->reset();
        mSelectedItemHook.reset();
    }
    if (mInventoryGetItemHook) {
        mInventoryGetItemHook->reset();
        mInventoryGetItemHook.reset();
    }

    mInventoryGetItemOriginal = nullptr;
    mUseItemOnBlockOriginal = nullptr;
    mBaseUseItemOriginal = nullptr;
    mReleaseUsingItemOriginal = nullptr;
    mSelectedItemOriginal = nullptr;
    mUseItemOnBlockTarget = 0;
    mUseItemOnBlockPreHooked = false;
    mBaseUseItemTarget = 0;
    mReleaseUsingItemTarget = 0;
    mSelectedItemTarget = 0;
    mInventoryGetItemTarget = 0;
    gGetOffhandSlot = nullptr;
    gSetItemInHandSlot = nullptr;
    gStackIsNull = nullptr;
    gPlayerIsUsingItem = nullptr;
    gItemInUseStack = nullptr;
    gStackDiffersForUse = nullptr;
    gItemStackCopyCtor = nullptr;
    gItemStackDtor = nullptr;
    sInstance = nullptr;

    context.logger().info("[RightUseRouter] hooks removed");
}

void RightUseRouter::setFeatureEnabled(bool enabled) noexcept {
    if (!enabled) {
        clearSession();
    }
    mFeatureEnabled.store(enabled && installed(), std::memory_order_release);
}

bool RightUseRouter::featureEnabled() const noexcept {
    return mFeatureEnabled.load(std::memory_order_acquire);
}

bool RightUseRouter::installed() const noexcept {
    return mHandTransactionHook != nullptr && mHandTransactionHook->installed() &&
        mHandTransactionOriginal != nullptr && mCompleteUsingItemHook != nullptr && mCompleteUsingItemHook->installed() &&
        mSetSelectedItemHook != nullptr && mSetSelectedItemHook->installed() &&
        mCompleteUsingItemOriginal != nullptr && mSetSelectedItemOriginal != nullptr &&
        mSelectedItemHook != nullptr && mSelectedItemHook->installed() &&
        mInventoryGetItemHook != nullptr && mInventoryGetItemHook->installed() &&
        mInventoryGetItemOriginal != nullptr &&
        mReleaseUsingItemHook != nullptr && mReleaseUsingItemHook->installed() &&
        mBaseUseItemHook != nullptr && mBaseUseItemHook->installed() &&
        mUseItemOnBlockHook != nullptr && mUseItemOnBlockHook->installed() &&
        mSelectedItemOriginal != nullptr &&
        mReleaseUsingItemOriginal != nullptr &&
        mBaseUseItemOriginal != nullptr &&
        mUseItemOnBlockOriginal != nullptr;
}

bool RightUseRouter::baseUseItemDetour(
    void* gameMode,
    const void* itemStack,
    unsigned char hand
) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mBaseUseItemOriginal == nullptr) {
        return false;
    }

    const auto original = reinterpret_cast<BaseUseItemFn>(instance->mBaseUseItemOriginal);
    if (
        !instance->featureEnabled() || gInsideBaseUse || hand == kOffHand
    ) {
        return original(gameMode, itemStack, hand);
    }

    const void* player = playerFromGameMode(gameMode);
    if (player == nullptr || instance->mSelectedItemOriginal == nullptr) {
        return original(gameMode, itemStack, hand);
    }

    const auto selectedOriginal = reinterpret_cast<SelectedItemFn>(
        instance->mSelectedItemOriginal
    );
    const void* mainStack = selectedOriginal(player);
    const void* offStack =
        gGetOffhandSlot != nullptr ? gGetOffhandSlot(player) : nullptr;

    // 1.26.51.1's upper dispatcher passes a local ItemStack copy (sp+0x60),
    // so pointer identity with Player::getSelectedItem is invalid.
    if (
        itemStack == nullptr || mainStack == nullptr ||
        !useInputRepresentsSelected(itemStack, mainStack) ||
        offStack == nullptr || stackIsNull(offStack)
    ) {
        return original(gameMode, itemStack, hand);
    }

    ScopedBool reentry(gInsideBaseUse);
    ScopedPlayer routedPlayer(player);

    const auto finishOffhandUse = [&](bool handled) noexcept {
        if (!handled) {
            return false;
        }

        bool expected = false;
        if (instance->mLoggedOffhandUse.compare_exchange_strong(
                expected, true, std::memory_order_relaxed
            )) {
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[RightUseRouter] air/self-use handled by OFFHAND (native hand=1)"
            );
        }

        if (activeUseMatches(player, gGetOffhandSlot(player))) {
            gSessionPlayer = player;
            gSessionGameMode = gameMode;
            expected = false;
            if (instance->mLoggedLongUse.compare_exchange_strong(
                    expected, true, std::memory_order_relaxed
                )) {
                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "[RightUseRouter] OFFHAND long-use session pinned until release"
                );
            }
        }
        return true;
    };

    const auto attemptOffhandUse = [&]() noexcept {
        // Resolve AFTER MAIN's attempt: a native callback may have changed OFF.
        const void* currentOff = gGetOffhandSlot(player);
        if (stackIsNull(currentOff)) {
            return false;
        }
        ScopedItemStackSnapshot offSnapshot(currentOff);
        if (offSnapshot.get() == nullptr) {
            return false;
        }
        ScopedActionHand offScope(ActionHand::OffHand, ActionKind::UseAir);
        const bool nativeHandled = original(gameMode, offSnapshot.get(), kOffHand);
        const void* resultingOff = gGetOffhandSlot(player);
        return finishOffhandUse(nativeHandled || activeUseMatches(player, resultingOff));
    };

    // Critical Java-style rule for Sword/Axe/Pickaxe/empty-like MAINHAND:
    // do not call the generic MAINHAND baseUseItem first.  ComponentItem can
    // return a success-like result even when it has no real right-click
    // action, which previously swallowed food/potion/other self-use in
    // OFFHAND.  The same capability classifier used by block placement is
    // authoritative here.
    if (!stackClaimsMainhandRightClick(mainStack)) {
        if (attemptOffhandUse()) {
            return true;
        }

        // OFFHAND passed: preserve untouched vanilla MAINHAND fallback once.
        ScopedActionHand mainScope(ActionHand::MainHand, ActionKind::UseAir);
        return original(gameMode, itemStack, hand);
    }

    // MAINHAND has a real native right-click owner. Preserve Java priority:
    // MAIN first, OFF only when MAIN genuinely passes.
    const auto result = routeUseAction(
        [&]() noexcept {
            const bool nativeHandled = original(gameMode, itemStack, hand);
            return nativeHandled || activeUseMatches(player, mainStack);
        },
        attemptOffhandUse,
        []() noexcept {},
        ActionKind::UseAir
    );

    if (result.handled && result.hand == ActionHand::MainHand) {
        if (gSessionPlayer == player) {
            clearSession();
        }
        return true;
    }

    if (result.handled && result.hand == ActionHand::OffHand) {
        return true;
    }

    return false;
}

std::uint32_t RightUseRouter::useItemOnBlockDetour(
    void* gameMode,
    const void* interaction,
    const void* blockPos,
    int face,
    const void* hitPos,
    unsigned char hand,
    std::uintptr_t extra,
    bool flag
) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mUseItemOnBlockOriginal == nullptr) {
        return 0;
    }

    const auto original = reinterpret_cast<UseItemOnBlockFn>(
        instance->mUseItemOnBlockOriginal
    );
    if (
        !instance->featureEnabled() || gInsideBlockUse || hand == kOffHand
    ) {
        return original(
            gameMode, interaction, blockPos, face, hitPos, hand, extra, flag
        );
    }

    ScopedBool reentry(gInsideBlockUse);

    const void* player = playerFromGameMode(gameMode);
    if (player == nullptr || instance->mSelectedItemOriginal == nullptr) {
        return original(
            gameMode, interaction, blockPos, face, hitPos, hand, extra, flag
        );
    }

    const auto selectedOriginal = reinterpret_cast<SelectedItemFn>(
        instance->mSelectedItemOriginal
    );
    const void* mainStack = selectedOriginal(player);

    // Decide whether MAINHAND genuinely owns right-click *before* executing
    // GameMode::useItemOn.  Calling the generic MAINHAND use-on wrapper first
    // can mutate/prime the client transaction even when a Sword/Pickaxe has no
    // right-click action; a later OFFHAND placement can then report handled
    // locally but fail to commit.  Items with a real native right-click
    // capability keep strict MAINHAND priority.
    bool yieldedAttackOnly = false;
    bool mainAttempted = false;
    std::uint32_t mainResult = 0;
    if (stackClaimsMainhandRightClick(mainStack, &yieldedAttackOnly)) {
        mainAttempted = true;
        ScopedActionHand mainScope(ActionHand::MainHand, ActionKind::UseBlock);
        mainResult = original(
            gameMode, interaction, blockPos, face, hitPos, hand, extra, flag
        );
        // Only a neutral native result may fall through. Preserve all bits.
        // Bow/food/etc. still need the upper dispatcher to attempt MAIN air-use
        // before OFF block-use: do not steal their click at this lower boundary.
        if (mainResult != 0u ||
            stackClaimsMainhandRightClick(mainStack, nullptr, false)) {
            return mainResult;
        }
    }

    const auto mainFallback = [&]() noexcept -> std::uint32_t {
        if (mainAttempted) {
            return mainResult;
        }
        ScopedActionHand mainScope(ActionHand::MainHand, ActionKind::UseBlock);
        return original(
            gameMode, interaction, blockPos, face, hitPos, hand, extra, flag
        );
    };

    if (yieldedAttackOnly) {
        bool expected = false;
        if (instance->mLoggedAttackOnlyYield.compare_exchange_strong(
                expected, true, std::memory_order_relaxed
            )) {
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[RightUseRouter] attack-only MAINHAND yielded right-click to OFFHAND before use-on"
            );
        }
    }

    const void* offStack =
        gGetOffhandSlot != nullptr ? gGetOffhandSlot(player) : nullptr;
    if (stackIsNull(offStack)) {
        return mainFallback();
    }

    // Preserve the transaction-safe detached before-state that fixed the
    // post-placement offhand-slot lock.  hand=1 still makes Minecraft mutate
    // the real offhand slot internally.
    ScopedItemStackSnapshot offSnapshot(offStack);
    if (offSnapshot.get() == nullptr) {
        return mainFallback();
    }

    // MAINHAND has no native right-click ownership, so OFFHAND gets the first
    // and only use-on transaction attempt.  If OFFHAND passes, run the untouched
    // MAINHAND wrapper once as vanilla fallback; never run MAINHAND before
    // OFFHAND in this branch.
    std::uint32_t offResult = 0;
    if (instance->mUseItemOnBlockPreHooked) {
        // A hook already owns this entry point before Levi Offhand.  Some
        // wrappers re-query Player::getSelectedItem instead of trusting x1 +
        // hand=1.  Scope ONLY this chained call so those nested lookups see
        // the OFFHAND stack.  The clean native path below remains the proven
        // snapshot-only implementation and is not spoofed.
        ScopedActionHand offScope(ActionHand::OffHand, ActionKind::UseBlock);
        ScopedPlayer routedPlayer(player);
        offResult = original(
            gameMode,
            offSnapshot.get(),
            blockPos,
            face,
            hitPos,
            kOffHand,
            extra,
            flag
        );
    } else {
        offResult = original(
            gameMode,
            offSnapshot.get(),
            blockPos,
            face,
            hitPos,
            kOffHand,
            extra,
            flag
        );
    }

    if ((offResult & 1u) != 0u) {
        const std::uint8_t liveCount = stackCount(offStack);
        const std::uint8_t placedCount = stackCount(offSnapshot.get());
        if (gSetItemInHandSlot != nullptr && liveCount != placedCount) {
            gSetItemInHandSlot(
                const_cast<void*>(player),
                kOffHand,
                offSnapshot.get()
            );

            bool writebackExpected = false;
            if (instance->mLoggedOffhandWriteback.compare_exchange_strong(
                    writebackExpected,
                    true,
                    std::memory_order_relaxed
                )) {
                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "[RightUseRouter] OFFHAND placement count reconciled %u -> %u via native hand setter",
                    static_cast<unsigned int>(liveCount),
                    static_cast<unsigned int>(placedCount)
                );
            }
        }

        // Visual-only: animate only after the accepted native placement and
        // any required OFFHAND count reconciliation.
        OffhandPlacementAnimation::instance().trigger();

        bool expected = false;
        if (instance->mLoggedBlockUse.compare_exchange_strong(
                expected, true, std::memory_order_relaxed
            )) {
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[RightUseRouter] MAINHAND had no right-click owner; block-use/place handled by OFFHAND first (native hand=1)"
            );
        }
        return offResult;
    }

    // A nonzero OFF result is definitive even when it does not swing.
    return offResult != 0u ? offResult : mainFallback();
}

const void* RightUseRouter::inventoryGetItemDetour(
    const void* inventory,
    int slot
) noexcept {
    auto* instance = sInstance;
    if (
        instance == nullptr ||
        instance->mInventoryGetItemOriginal == nullptr
    ) {
        return nullptr;
    }

    const auto original = reinterpret_cast<InventoryGetItemFn>(
        instance->mInventoryGetItemOriginal
    );

    const auto returnAddress = reinterpret_cast<std::uintptr_t>(
        __builtin_return_address(0)
    );
    const void* offStack = offhandStackForNativeUseTick(
        inventory,
        returnAddress,
        instance->featureEnabled()
    );
    if (offStack == nullptr) {
        return original(inventory, slot);
    }

    bool expected = false;
    if (gLoggedUseTickBridge.compare_exchange_strong(
            expected, true, std::memory_order_relaxed
        )) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[RightUseRouter] native long-use tick reads live OFFHAND instead of selected MAIN"
        );
    }
    return offStack;
}

const void* RightUseRouter::selectedItemDetour(const void* player) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mSelectedItemOriginal == nullptr) {
        return nullptr;
    }
    const auto original = reinterpret_cast<SelectedItemFn>(instance->mSelectedItemOriginal);


    if (!instance->featureEnabled() || player == nullptr) {
        return original(player);
    }

    const auto scoped = currentScopedAction();
    // An active OFF session must not override an explicit MAIN attempt.
    if (scoped.has_value() && scoped->hand == ActionHand::MainHand) {
        return original(player);
    }
    const bool scopedOffhand =
        scoped.has_value() && scoped->hand == ActionHand::OffHand &&
        player == gScopedPlayer;
    const bool sessionOffhand = gSessionPlayer == player;
    if ((!scopedOffhand && !sessionOffhand) || gGetOffhandSlot == nullptr) {
        return original(player);
    }

    const void* offStack = gGetOffhandSlot(player);
    // During native completion/release an empty OFF stack is a valid result,
    // not permission for later callback reads to switch to MAIN.
    if (scopedOffhand && gUseWritebackPlayer == player && offStack != nullptr) {
        return offStack;
    }
    if (stackIsNull(offStack)) {
        if (sessionOffhand) {
            clearSession();
        }
        return original(player);
    }

    if (scopedOffhand) {
        return offStack;
    }

    if (sessionOffhand) {
        if (
            gPlayerIsUsingItem == nullptr || !gPlayerIsUsingItem(player) ||
            gItemInUseStack == nullptr
        ) {
            clearSession();
            return original(player);
        }
        const void* active = gItemInUseStack(player);
        if (stackIsNull(active) || !stacksMatch(active, offStack)) {
            clearSession();
            return original(player);
        }
    }

    return offStack;
}

void RightUseRouter::completeUsingItemDetour(void* player) noexcept {
    auto* instance = sInstance;
    if (!instance || !instance->mCompleteUsingItemOriginal) return;
    const auto original = reinterpret_cast<CompleteUsingItemFn>(instance->mCompleteUsingItemOriginal);
    if (!instance->featureEnabled() || player == nullptr) {
        original(player);
        return;
    }
    const void* off = gGetOffhandSlot ? gGetOffhandSlot(player) : nullptr;
    const auto selected = reinterpret_cast<SelectedItemFn>(instance->mSelectedItemOriginal);
    const void* main = selected ? selected(player) : nullptr;
    // The integrated server uses a different Player object/thread. A native
    // active stack uniquely matching OFF also establishes ownership there.
    // When both slots match, retain native MAIN unless this player has an
    // explicitly tracked OFF session.
    const bool ownsSession = gSessionPlayer == player;
    const bool uniqueNativeOff = activeUseMatches(player, off) && !activeUseMatches(player, main);
    if (!ownsSession && !uniqueNativeOff) {
        ScopedActionHand mainScope(ActionHand::MainHand, ActionKind::UseAir);
        original(player);
        return;
    }
    if (!activeUseMatches(player, off) || stackIsNull(off) || !gSetItemInHandSlot) {
        // The held stack changed during use: do not consume the replacement
        // or let a stale OFF session consume MAIN. Stop without releasing.
        if (gStopUsingItem) gStopUsingItem(player);
        if (ownsSession) clearSession();
        return;
    }
    ScopedItemStackSnapshot before(off);
    if (!before.get()) {
        if (gStopUsingItem) gStopUsingItem(player);
        if (ownsSession) clearSession();
        return;
    }
    const void* previousPlayer = gUseWritebackPlayer;
    const void* previousBefore = gUseWritebackBefore;
    gUseWritebackPlayer = player;
    gUseWritebackBefore = before.get();
    {
        ScopedActionHand handScope(ActionHand::OffHand, ActionKind::UseAir);
        ScopedPlayer playerScope(player);
        // Keep the original consumption effect, container conversion, callback
        // envelope, and clear-use lifecycle. Only its final slot write changes.
        original(player);
    }
    gUseWritebackPlayer = previousPlayer;
    gUseWritebackBefore = previousBefore;
    if (ownsSession) clearSession();
}

void RightUseRouter::setSelectedItemDetour(void* player, const void* stack) noexcept {
    auto* instance = sInstance;
    if (!instance || !instance->mSetSelectedItemOriginal) return;
    const auto original = reinterpret_cast<SetSelectedItemFn>(instance->mSetSelectedItemOriginal);
    const auto action = currentScopedAction();
    if (!instance->featureEnabled() || !player || player != gUseWritebackPlayer ||
        !action || action->hand != ActionHand::OffHand || !gUseWritebackBefore) {
        original(player, stack);
        return;
    }
    const void* live = gGetOffhandSlot ? gGetOffhandSlot(player) : nullptr;
    if (!live || !stacksMatch(live, gUseWritebackBefore) ||
        stackCount(live) != stackCount(gUseWritebackBefore)) {
        // A nested callback already replaced the slot. Never overwrite it or
        // fall back to writing the consumed OFF stack into MAIN.
        return;
    }
    if (gSetItemInHandSlot && stack) {
        // Virtual hand setter dispatches LocalPlayer's OFF override, including
        // native container-119 InventoryAction recording. Preserve empty stacks
        // and bottle/bowl replacements supplied by the original callback.
        gSetItemInHandSlot(player, kOffHand, stack);
    }
}

void RightUseRouter::releaseUsingItemDetour(void* gameMode) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mReleaseUsingItemOriginal == nullptr) {
        return;
    }
    const auto original = reinterpret_cast<ReleaseUsingItemFn>(
        instance->mReleaseUsingItemOriginal
    );

    const void* player = playerFromGameMode(gameMode);
    if (!instance->featureEnabled() || !player) {
        original(gameMode);
        return;
    }
    const void* off = gGetOffhandSlot ? gGetOffhandSlot(player) : nullptr;
    const auto selected = reinterpret_cast<SelectedItemFn>(instance->mSelectedItemOriginal);
    const void* main = selected ? selected(player) : nullptr;
    const bool ownsSession = gSessionPlayer == player && gSessionGameMode == gameMode;
    const bool uniqueNativeOff = activeUseMatches(player, off) && !activeUseMatches(player, main);
    if (!ownsSession && !uniqueNativeOff) {
        ScopedActionHand mainScope(ActionHand::MainHand, ActionKind::UseAir);
        original(gameMode);
        return;
    }
    if (!activeUseMatches(player, off) || stackIsNull(off) || !gSetItemInHandSlot) {
        if (gStopUsingItem) gStopUsingItem(const_cast<void*>(player));
        if (ownsSession) clearSession();
        return;
    }
    ScopedItemStackSnapshot before(off);
    if (!before.get()) {
        if (gStopUsingItem) gStopUsingItem(const_cast<void*>(player));
        if (ownsSession) clearSession();
        return;
    }
    const void* previousPlayer = gUseWritebackPlayer;
    const void* previousBefore = gUseWritebackBefore;
    const void* previousRelease = gReleasingPlayer;
    gUseWritebackPlayer = player;
    gUseWritebackBefore = before.get();
    gReleasingPlayer = player;
    {
        ScopedActionHand actionScope(ActionHand::OffHand, ActionKind::UseAir);
        ScopedPlayer routedPlayer(player);
        original(gameMode);
    }
    gUseWritebackPlayer = previousPlayer;
    gUseWritebackBefore = previousBefore;
    gReleasingPlayer = previousRelease;
    if (ownsSession) clearSession();
}

void RightUseRouter::handTransactionDetour(void* player, unsigned char hand, void* envelope,
                                         void (*callback)(void*), void* context) noexcept {
    auto* instance = sInstance;
    if (!instance || !instance->mHandTransactionOriginal) return;
    const auto original = reinterpret_cast<HandTransactionFn>(instance->mHandTransactionOriginal);
    const auto action = currentScopedAction();
    if (instance->featureEnabled() && player && player == gReleasingPlayer &&
        action && action->hand == ActionHand::OffHand && hand == kMainHand &&
        reinterpret_cast<std::uintptr_t>(callback) == gReleaseCallback) {
        hand = kOffHand;
    }
    // Forward the same native envelope/callback/context exactly once. The
    // wrapper owns callback lifetime; never copy its C++ closure manually.
    original(player, hand, envelope, callback, context);
}

} // namespace levioffhand::runtime

