#include "runtime/RightUseRouter.hpp"

#include "runtime/ActionHandContext.hpp"
#include "runtime/HandActionRouterCore.hpp"

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
constexpr std::uintptr_t kReleaseUsingItemRva = 0xF8A3204;
constexpr std::uintptr_t kSelectedItemRva = 0xF9F7824;
constexpr std::uintptr_t kOffhandSlotRva = 0xF579C2C;
constexpr std::uintptr_t kStackIsNullRva = 0xFFA0F70;
constexpr std::uintptr_t kPlayerIsUsingItemRva = 0xF9E8D64;
constexpr std::uintptr_t kItemInUseStackRva = 0xF9E8D84;
constexpr std::uintptr_t kStackDiffersForUseRva = 0xFFA5B04;
constexpr std::uintptr_t kItemStackCopyCtorRva = 0xFF9D748;
constexpr std::uintptr_t kItemStackDtorRva = 0x85ADF98;

// 1.26.51.1 Item virtual defaults used only as capability identities.
// MAINHAND ownership is based on concrete native action implementations, not
// broad data-driven ComponentItem booleans.  Relocated 1.26.51.1 primary
// vtables prove Item::use=+0x290, requiresInteract=+0x1A8 and _useOn=+0x410.
// Generic Item/ComponentItem entries do not claim the click; specialized
// overrides (FishingRod, Shears, etc.) do.
constexpr std::uintptr_t kBaseItemUseRva = 0xFF8429C;
constexpr std::uintptr_t kComponentItemUseRva = 0xFDA8274;
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
using ReleaseUsingItemFn = void (*)(void*);
using SelectedItemFn = const void* (*)(const void*);
using OffhandItemFn = const void* (*)(const void*);
using StackIsNullFn = bool (*)(const void*);
using PlayerIsUsingItemFn = bool (*)(const void*);
using ItemInUseStackFn = const void* (*)(const void*);
using StackDiffersForUseFn = bool (*)(const void*, const void*);
using ItemStackCopyCtorFn = void (*)(void*, const void*);
using ItemStackDtorFn = void (*)(void*);
using GetMaxUseDurationFn = int (*)(const void*, const void*);
using GetAttackDamageFn = int (*)(const void*);
using ItemBoolFn = bool (*)(const void*);

OffhandItemFn gGetOffhandSlot = nullptr;
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
    const std::array<std::uint8_t, N>& fingerprint
) noexcept {
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
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "[RightUseRouter] %s prologue already modified; chaining live 1.26.51.1 target RVA=0x%llX",
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

[[nodiscard]] bool stacksMatch(const void* lhs, const void* rhs) noexcept {
    return lhs != nullptr && rhs != nullptr && gStackDiffersForUse != nullptr &&
        !gStackDiffersForUse(lhs, rhs);
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
    bool* yieldedAttackOnly = nullptr
) noexcept {
    if (yieldedAttackOnly != nullptr) {
        *yieldedAttackOnly = false;
    }

    const void* item = itemFromStack(stack);
    if (item == nullptr) {
        return false;
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
        useAddress != moduleBase + kComponentItemUseRva;

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
        specializedUseOn
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

    if (
        offhandTarget == 0 || nullTarget == 0 ||
        usingTarget == 0 || inUseTarget == 0 || differsTarget == 0 ||
        copyCtorTarget == 0 || dtorTarget == 0
    ) {
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "[RightUseRouter] stable guard failed offhand=%d null=%d using=%d inUse=%d differs=%d copy=%d dtor=%d",
            offhandTarget != 0 ? 1 : 0,
            nullTarget != 0 ? 1 : 0,
            usingTarget != 0 ? 1 : 0,
            inUseTarget != 0 ? 1 : 0,
            differsTarget != 0 ? 1 : 0,
            copyCtorTarget != 0 ? 1 : 0,
            dtorTarget != 0 ? 1 : 0
        );
        context.logger().warn(
            "[RightUseRouter] Minecraft 1.26.51.1 stable fingerprint validation failed; right-use disabled"
        );
        return false;
    }

    // Only after the exact stable guard passes do we resolve hookable entry
    // points. Their prologues may already be changed in memory by a hook, so
    // use the known RVA and let Levi's HookHandle chain the live target.
    const auto selectedTarget = resolveHookTarget(
        "Player::getSelectedItem",
        kSelectedItemRva,
        kSelectedItemFingerprint
    );
    const auto blockUseTarget = resolveHookTarget(
        "GameMode::useItemOnBlock",
        kUseItemOnBlockRva,
        kUseItemOnBlockFingerprint
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
        selectedTarget == 0 || blockUseTarget == 0 ||
        useTarget == 0 || releaseTarget == 0
    ) {
        context.logger().warn(
            "[RightUseRouter] Minecraft 1.26.51.1 live hook target resolution failed; right-use disabled"
        );
        return false;
    }

    gGetOffhandSlot = reinterpret_cast<OffhandItemFn>(offhandTarget);
    gStackIsNull = reinterpret_cast<StackIsNullFn>(nullTarget);
    gPlayerIsUsingItem = reinterpret_cast<PlayerIsUsingItemFn>(usingTarget);
    gItemInUseStack = reinterpret_cast<ItemInUseStackFn>(inUseTarget);
    gStackDiffersForUse = reinterpret_cast<StackDiffersForUseFn>(differsTarget);
    gItemStackCopyCtor = reinterpret_cast<ItemStackCopyCtorFn>(copyCtorTarget);
    gItemStackDtor = reinterpret_cast<ItemStackDtorFn>(dtorTarget);

    mSelectedItemTarget = selectedTarget;
    mReleaseUsingItemTarget = releaseTarget;
    mBaseUseItemTarget = useTarget;
    mUseItemOnBlockTarget = blockUseTarget;
    sInstance = this;

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

    mFeatureEnabled.store(true, std::memory_order_release);
    mLoggedOffhandUse.store(false, std::memory_order_relaxed);
    mLoggedBlockUse.store(false, std::memory_order_relaxed);
    mLoggedAttackOnlyYield.store(false, std::memory_order_relaxed);
    mLoggedLongUse.store(false, std::memory_order_relaxed);
    context.logger().info(
        "[RightUseRouter] Minecraft 1.26.51.1 right-use active: MAINHAND first, OFFHAND fallback; left-click remains vanilla mainhand"
    );
    return true;
}

void RightUseRouter::uninstall(pl::mod::ModContext& context) noexcept {
    mFeatureEnabled.store(false, std::memory_order_release);
    clearSession();

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

    mUseItemOnBlockOriginal = nullptr;
    mBaseUseItemOriginal = nullptr;
    mReleaseUsingItemOriginal = nullptr;
    mSelectedItemOriginal = nullptr;
    mUseItemOnBlockTarget = 0;
    mBaseUseItemTarget = 0;
    mReleaseUsingItemTarget = 0;
    mSelectedItemTarget = 0;
    gGetOffhandSlot = nullptr;
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
    return mSelectedItemHook != nullptr && mSelectedItemHook->installed() &&
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
    const void* offStack = gGetOffhandSlot != nullptr ? gGetOffhandSlot(player) : nullptr;

    // 1.26.51.1's upper dispatcher passes a local ItemStack copy (sp+0x60),
    // so pointer identity with Player::getSelectedItem is invalid. Match using
    // Minecraft's own stack comparator. The third ABI argument is the native
    // hand enum: 0=main, 1=offhand.
    if (
        itemStack == nullptr || mainStack == nullptr ||
        !stacksMatch(itemStack, mainStack) ||
        offStack == nullptr || stackIsNull(offStack)
    ) {
        return original(gameMode, itemStack, hand);
    }

    const auto scoped = currentScopedAction();
    if (
        scoped.has_value() &&
        scoped->hand == ActionHand::OffHand &&
        player == gScopedPlayer
    ) {
        ScopedBool reentry(gInsideBaseUse);
        const bool handled = original(gameMode, offStack, kOffHand);
        if (
            handled && gPlayerIsUsingItem != nullptr &&
            gPlayerIsUsingItem(player) && gItemInUseStack != nullptr
        ) {
            const void* active = gItemInUseStack(player);
            if (!stackIsNull(active) && stacksMatch(active, offStack)) {
                gSessionPlayer = player;
                gSessionGameMode = gameMode;
            }
        }
        return handled;
    }

    ScopedBool reentry(gInsideBaseUse);
    ScopedPlayer routedPlayer(player);

    const auto result = routeUseAction(
        [&]() noexcept {
            const bool nativeHandled = original(gameMode, itemStack, hand);
            // Bow/spear/charge-style items may enter Player::isUsingItem before
            // this bool reports a consumed action.  Native active-use ownership
            // is authoritative: once MAINHAND owns the use state, OFFHAND must
            // not be attempted for the same input.
            return nativeHandled || activeUseMatches(player, mainStack);
        },
        [&]() noexcept {
            const bool nativeHandled = original(gameMode, offStack, kOffHand);
            return nativeHandled || activeUseMatches(player, offStack);
        },
        []() noexcept {},
        ActionKind::UseAir
    );

    if (result.handled && result.hand == ActionHand::MainHand) {
        // A new MAINHAND use supersedes any stale offhand session state.
        if (gSessionPlayer == player) {
            clearSession();
        }
    }

    if (result.handled && result.hand == ActionHand::OffHand) {
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

        if (activeUseMatches(player, offStack)) {
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
    }

    return result.handled;
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
    if (stackClaimsMainhandRightClick(mainStack, &yieldedAttackOnly)) {
        return original(
            gameMode,
            interaction,
            blockPos,
            face,
            hitPos,
            hand,
            extra,
            flag
        );
    }

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
        return original(
            gameMode, interaction, blockPos, face, hitPos, hand, extra, flag
        );
    }

    // Preserve the transaction-safe detached before-state that fixed the
    // post-placement offhand-slot lock.  hand=1 still makes Minecraft mutate
    // the real offhand slot internally.
    ScopedItemStackSnapshot offSnapshot(offStack);
    if (offSnapshot.get() == nullptr) {
        return original(
            gameMode, interaction, blockPos, face, hitPos, hand, extra, flag
        );
    }

    // MAINHAND has no native right-click ownership, so OFFHAND gets the first
    // and only use-on transaction attempt.  If OFFHAND passes, run the untouched
    // MAINHAND wrapper once as vanilla fallback; never run MAINHAND before
    // OFFHAND in this branch.
    const std::uint32_t offResult = original(
        gameMode,
        offSnapshot.get(),
        blockPos,
        face,
        hitPos,
        kOffHand,
        extra,
        flag
    );

    if ((offResult & 1u) != 0u) {
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

    return original(
        gameMode,
        interaction,
        blockPos,
        face,
        hitPos,
        hand,
        extra,
        flag
    );
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
    const bool scopedOffhand =
        scoped.has_value() && scoped->hand == ActionHand::OffHand &&
        player == gScopedPlayer;
    const bool sessionOffhand = gSessionPlayer == player;
    if ((!scopedOffhand && !sessionOffhand) || gGetOffhandSlot == nullptr) {
        return original(player);
    }

    const void* offStack = gGetOffhandSlot(player);
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

void RightUseRouter::releaseUsingItemDetour(void* gameMode) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mReleaseUsingItemOriginal == nullptr) {
        return;
    }
    const auto original = reinterpret_cast<ReleaseUsingItemFn>(
        instance->mReleaseUsingItemOriginal
    );

    if (
        !instance->featureEnabled() || gSessionPlayer == nullptr ||
        gSessionGameMode != gameMode
    ) {
        original(gameMode);
        return;
    }

    {
        ScopedActionHand actionScope(ActionHand::OffHand, ActionKind::UseAir);
        ScopedPlayer routedPlayer(gSessionPlayer);
        original(gameMode);
    }
    clearSession();
}

} // namespace levioffhand::runtime
