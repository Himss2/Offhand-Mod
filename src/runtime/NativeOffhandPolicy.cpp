#include "runtime/NativeOffhandPolicy.hpp"

#include <android/log.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <memory>

#include <pl/memory/Hook.hpp>
#include <pl/memory/Patch.hpp>
#include <pl/memory/Signature.hpp>

namespace levioffhand::runtime {
namespace {

constexpr char kMinecraftLibrary[] = "libminecraftpe.so";
constexpr char kLogTag[] = "Levi Offhand";

/*
 * Minecraft Bedrock Android 1.26.45.1
 * Build ID: 868e275cb295e9a275bb29d2258edc2f7dc48761
 *
 * Item::Item default flag initialization:
 *   RVA 0xF65A3BC  mov w9,#0x50
 *
 * In this build ItemStackBase::getAllowOffHand ultimately reads bit 7 from
 * Item+0x112, so setting 0x50 -> 0xD0 is the verified native policy.
 *
 * Minecraft Bedrock Android 1.26.51.1
 * Build ID: 712509dc14ccc233e91f267937dfb46ecdcc4b68
 *
 * ItemStackBase::getAllowOffHand:
 *   RVA 0xFFA60F0
 *   Item+0x1C8 is now a 2-bit policy enum; allow-offhand is true only when
 *   (value & 3) == 1.  Item+0x112 bit 7 is no longer the offhand capability.
 *
 * The 1.26.51.1 policy therefore patches the verified native query itself to
 * return true.  This also updates already-created Item definitions, avoiding
 * constructor-lifecycle timing problems.
 */
constexpr std::uintptr_t kItemDefaultFlagsRva126451 = 0xF65A3BC;
constexpr std::uintptr_t kAllowOffhandQueryRva126511 = 0xFFA60F0;

// Exact 1.26.51.1 manual inventory path recovered from device tracing.
// CraftingContainerManagerController::manual inventory operation @ 0xF927118
// mutates destination then source through 0xF97F9E8 before it builds the
// native request.  Promoting the moved Item at this native setter boundary
// keeps the operation fully vanilla while ensuring predictive reconciliation
// sees the real Item policy, not only our getAllowOffHand query patch.
constexpr std::uintptr_t kManualContainerSetItemRva126511 = 0xF97F9E8;
constexpr std::uintptr_t kManualDestinationSetReturnRva126511 = 0xF927828;
constexpr std::uintptr_t kManualSourceSetReturnRva126511 = 0xF927984;
constexpr std::uintptr_t kNativeSetAllowOffhandRva126511 = 0xFF82E5C;

constexpr std::size_t kItemStackItemHolderOffset = 0x08;
constexpr std::size_t kItemOffhandPolicyOffset = 0x1C8;
constexpr std::uint8_t kNativeOffhandAllowedPolicy = 1;

constexpr std::uintptr_t kLegacyPatchOffsetFromSignature = 0x0C;

constexpr char kItemConstructorFlagSignature126451[] =
    "08 DA 94 94 "
    "00 E4 00 6F "
    "F5 03 13 AA "
    "09 0A 80 52 "
    "A0 8E 8E 3C "
    "A8 56 40 79 "
    "A0 C2 00 91 "
    "A0 A2 81 3C "
    "08 19 17 12 "
    "A0 06 80 3D "
    "08 01 09 2A "
    "BF 2E 00 B9";

// Unique 24-byte prefix at RVA 0xFFA60F0 in libminecraftpe.so 1.26.51.1.
constexpr char kAllowOffhandQuerySignature126511[] =
    "08 04 40 F9 "
    "08 01 00 B4 "
    "08 01 40 F9 "
    "C8 00 00 B4 "
    "08 21 47 39 "
    "08 05 00 12";

// ContainerController manual set path.  Static RE found only two direct calls
// in the high-level manual operation: BL @ 0xF927824 (destination) and
// BL @ 0xF927980 (source).
constexpr char kManualContainerSetItemSignature126511[] =
    "FF 43 07 D1 "
    "FD 7B 17 A9 "
    "FC 6F 18 A9 "
    "FA 67 19 A9 "
    "F8 5F 1A A9 "
    "F6 57 1B A9 "
    "F4 4F 1C A9 "
    "FD C3 05 91";

// Item::setAllowOffHand(bool) @ 0xFF82E5C. It updates the low two policy
// bits at Item+0x1C8 while preserving the remaining bits.
constexpr char kNativeSetAllowOffhandSignature126511[] =
    "09 20 47 39 "
    "28 00 80 52 "
    "3F 00 00 72 "
    "08 15 88 1A "
    "29 15 1E 12 "
    "28 01 08 2A "
    "08 20 07 39 "
    "C0 03 5F D6";

constexpr char kVanillaW9Instruction[] = "09 0A 80 52";
constexpr char kPatchedW9Instruction[] = "09 1A 80 52";
constexpr char kForceAllowQueryInstruction[] = "20 00 80 52 C0 03 5F D6";
constexpr char kAllOffhandPatchName[] = "levi_offhand.item_allow_offhand";

constexpr std::array<std::uint8_t, 4> kVanillaW9Bytes{0x09, 0x0A, 0x80, 0x52};
constexpr std::array<std::uint8_t, 4> kPatchedW9Bytes{0x09, 0x1A, 0x80, 0x52};
constexpr std::array<std::uint8_t, 8> kVanillaAllowQueryPrefix{
    0x08, 0x04, 0x40, 0xF9,
    0x08, 0x01, 0x00, 0xB4,
};
constexpr std::array<std::uint8_t, 8> kPatchedAllowQueryPrefix{
    0x20, 0x00, 0x80, 0x52,
    0xC0, 0x03, 0x5F, 0xD6,
};

[[nodiscard]] bool belongsToMinecraft(std::uintptr_t address) noexcept {
    if (address == 0) {
        return false;
    }

    Dl_info info{};
    if (
        dladdr(reinterpret_cast<void*>(address), &info) == 0 ||
        info.dli_fname == nullptr
    ) {
        return false;
    }

    return std::strstr(info.dli_fname, kMinecraftLibrary) != nullptr;
}

[[nodiscard]] std::uintptr_t targetRva(std::uintptr_t address) noexcept {
    if (!belongsToMinecraft(address)) {
        return 0;
    }

    Dl_info info{};
    if (
        dladdr(reinterpret_cast<void*>(address), &info) == 0 ||
        info.dli_fbase == nullptr
    ) {
        return 0;
    }

    return address - reinterpret_cast<std::uintptr_t>(info.dli_fbase);
}

template <std::size_t N>
[[nodiscard]] bool bytesEqual(
    std::uintptr_t address,
    const std::array<std::uint8_t, N>& expected
) noexcept {
    const auto actual = pl::memory::readBytes(address, expected.size());
    return actual.size() == expected.size() &&
        std::memcmp(actual.data(), expected.data(), expected.size()) == 0;
}

[[nodiscard]] bool isCurrentQueryTarget(std::uintptr_t address) noexcept {
    return targetRva(address) == kAllowOffhandQueryRva126511;
}

[[nodiscard]] bool isLegacyConstructorTarget(std::uintptr_t address) noexcept {
    return targetRva(address) == kItemDefaultFlagsRva126451;
}

using ManualContainerSetItemFn = int (*)(
    void*,
    const void*,
    int,
    const void*,
    int,
    int
);
using NativeSetAllowOffhandFn = void (*)(void*, bool);

std::unique_ptr<pl::memory::HookHandle> gManualContainerSetHook;
void* gManualContainerSetOriginal = nullptr;
NativeSetAllowOffhandFn gNativeSetAllowOffhand = nullptr;
std::atomic_bool gLoggedManualPromotion{false};

template <typename T>
[[nodiscard]] T readObject(
    const void* base,
    std::size_t offset,
    T fallback = {}
) noexcept {
    if (base == nullptr) {
        return fallback;
    }
    T value{};
    std::memcpy(
        &value,
        static_cast<const std::byte*>(base) + offset,
        sizeof(value)
    );
    return value;
}

[[nodiscard]] bool promoteStackNativeOffhand(
    const void* stack
) noexcept {
    if (stack == nullptr || gNativeSetAllowOffhand == nullptr) {
        return false;
    }

    // Exact 1.26.51.1 ItemStack layout:
    // stack+0x08 -> ItemWeakPtr holder -> Item*.
    void* holder = readObject<void*>(
        stack,
        kItemStackItemHolderOffset,
        nullptr
    );
    if (holder == nullptr) {
        return false;
    }

    void* item = readObject<void*>(holder, 0, nullptr);
    if (item == nullptr) {
        return false;
    }

    // Item lives on the heap, so validate its vtable instead of requiring the
    // Item object address itself to belong to libminecraftpe.so.
    const void* itemVtable = readObject<const void*>(item, 0, nullptr);
    if (!belongsToMinecraft(reinterpret_cast<std::uintptr_t>(itemVtable))) {
        return false;
    }

    const std::uint8_t before = readObject<std::uint8_t>(
        item,
        kItemOffhandPolicyOffset,
        0xFF
    );
    if ((before & 0x3u) == kNativeOffhandAllowedPolicy) {
        return true;
    }

    gNativeSetAllowOffhand(item, true);

    const std::uint8_t after = readObject<std::uint8_t>(
        item,
        kItemOffhandPolicyOffset,
        0xFF
    );
    const bool allowed =
        (after & 0x3u) == kNativeOffhandAllowedPolicy;

    if (allowed) {
        bool expected = false;
        if (gLoggedManualPromotion.compare_exchange_strong(
                expected,
                true,
                std::memory_order_relaxed
            )) {
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[NativeOffhandPolicy] manual inventory promoted Item policy %u -> %u",
                static_cast<unsigned>(before & 0x3u),
                static_cast<unsigned>(after & 0x3u)
            );
        }
    }

    return allowed;
}

int manualContainerSetItemDetour(
    void* container,
    const void* before,
    int slot,
    const void* after,
    int arg4,
    int arg5
) noexcept {
    const auto original =
        reinterpret_cast<ManualContainerSetItemFn>(
            gManualContainerSetOriginal
        );
    if (original == nullptr) {
        return 0;
    }

    const auto caller = reinterpret_cast<std::uintptr_t>(
        __builtin_return_address(0)
    );
    const auto callerRva = targetRva(caller);
    const bool manualInventoryCall =
        callerRva == kManualDestinationSetReturnRva126511 ||
        callerRva == kManualSourceSetReturnRva126511;

    if (
        manualInventoryCall &&
        NativeOffhandPolicy::instance().featureEnabled()
    ) {
        // The first native call installs the moved stack into its destination.
        // Promote before forwarding so the later vanilla request/reconciliation
        // observes Item+0x1C8 == 1. The source-side call normally carries an
        // empty/remainder stack; promoting an actual remainder is harmless and
        // keeps the Item definition consistently native-capable.
        promoteStackNativeOffhand(after);
    }

    return original(
        container,
        before,
        slot,
        after,
        arg4,
        arg5
    );
}

[[nodiscard]] bool installManualNativePolicyBridge(
    pl::mod::ModContext& context
) noexcept {
    if (
        gManualContainerSetHook &&
        gManualContainerSetHook->installed() &&
        gManualContainerSetOriginal != nullptr &&
        gNativeSetAllowOffhand != nullptr
    ) {
        return true;
    }

    const auto manualSet = pl::memory::resolveSignature(
        kManualContainerSetItemSignature126511,
        kMinecraftLibrary
    );
    const auto nativePolicy = pl::memory::resolveSignature(
        kNativeSetAllowOffhandSignature126511,
        kMinecraftLibrary
    );

    if (
        !belongsToMinecraft(manualSet) ||
        targetRva(manualSet) != kManualContainerSetItemRva126511 ||
        !belongsToMinecraft(nativePolicy) ||
        targetRva(nativePolicy) != kNativeSetAllowOffhandRva126511
    ) {
        context.logger().error(
            "[NativeOffhandPolicy] 1.26.51.1 manual policy bridge target validation failed"
        );
        return false;
    }

    gNativeSetAllowOffhand =
        reinterpret_cast<NativeSetAllowOffhandFn>(nativePolicy);
    gManualContainerSetOriginal = nullptr;
    gManualContainerSetHook =
        std::make_unique<pl::memory::HookHandle>(
            reinterpret_cast<void*>(manualSet),
            reinterpret_cast<void*>(&manualContainerSetItemDetour),
            &gManualContainerSetOriginal,
            pl::memory::HookPriority::Normal
        );

    if (
        !gManualContainerSetHook ||
        !gManualContainerSetHook->installed() ||
        gManualContainerSetOriginal == nullptr
    ) {
        if (gManualContainerSetHook) {
            gManualContainerSetHook->reset();
            gManualContainerSetHook.reset();
        }
        gManualContainerSetOriginal = nullptr;
        gNativeSetAllowOffhand = nullptr;
        context.logger().error(
            "[NativeOffhandPolicy] manual inventory policy bridge hook failed"
        );
        return false;
    }

    gLoggedManualPromotion.store(false, std::memory_order_relaxed);
    context.logger().info(
        "[NativeOffhandPolicy] manual inventory native Item policy bridge active"
    );
    return true;
}

void uninstallManualNativePolicyBridge() noexcept {
    if (gManualContainerSetHook) {
        gManualContainerSetHook->reset();
        gManualContainerSetHook.reset();
    }
    gManualContainerSetOriginal = nullptr;
    gNativeSetAllowOffhand = nullptr;
    gLoggedManualPromotion.store(false, std::memory_order_relaxed);
}

[[nodiscard]] std::uintptr_t resolvePolicyTarget() noexcept {
    const auto current = pl::memory::resolveSignature(
        kAllowOffhandQuerySignature126511,
        kMinecraftLibrary
    );
    if (
        belongsToMinecraft(current) &&
        isCurrentQueryTarget(current) &&
        bytesEqual(current, kVanillaAllowQueryPrefix)
    ) {
        return current;
    }

    const auto legacyBase = pl::memory::resolveSignature(
        kItemConstructorFlagSignature126451,
        kMinecraftLibrary
    );
    if (belongsToMinecraft(legacyBase)) {
        const auto target = legacyBase + kLegacyPatchOffsetFromSignature;
        if (
            isLegacyConstructorTarget(target) &&
            (bytesEqual(target, kVanillaW9Bytes) || bytesEqual(target, kPatchedW9Bytes))
        ) {
            return target;
        }
    }

    return 0;
}

} // namespace

NativeOffhandPolicy& NativeOffhandPolicy::instance() noexcept {
    static NativeOffhandPolicy value;
    return value;
}

bool NativeOffhandPolicy::applyPatch() noexcept {
    if (mInstruction == 0) {
        return false;
    }
    if (mPatchApplied.load(std::memory_order_acquire)) {
        return true;
    }

    const auto rva = targetRva(mInstruction);
    const char* replacement = nullptr;

    if (rva == kAllowOffhandQueryRva126511) {
        if (bytesEqual(mInstruction, kPatchedAllowQueryPrefix)) {
            mPatchApplied.store(true, std::memory_order_release);
            return true;
        }
        if (!bytesEqual(mInstruction, kVanillaAllowQueryPrefix)) {
            __android_log_print(
                ANDROID_LOG_ERROR,
                kLogTag,
                "[NativeOffhandPolicy] refusing 1.26.51.1 query patch: fingerprint mismatch"
            );
            return false;
        }
        replacement = kForceAllowQueryInstruction;
    } else if (rva == kItemDefaultFlagsRva126451) {
        if (bytesEqual(mInstruction, kPatchedW9Bytes)) {
            mPatchApplied.store(true, std::memory_order_release);
            return true;
        }
        if (!bytesEqual(mInstruction, kVanillaW9Bytes)) {
            __android_log_print(
                ANDROID_LOG_ERROR,
                kLogTag,
                "[NativeOffhandPolicy] refusing 1.26.45.1 constructor patch: fingerprint mismatch"
            );
            return false;
        }
        replacement = kPatchedW9Instruction;
    } else {
        __android_log_print(
            ANDROID_LOG_ERROR,
            kLogTag,
            "[NativeOffhandPolicy] refusing patch: unsupported policy target"
        );
        return false;
    }

    const bool ok = pl::memory::writeBytes(
        mInstruction,
        replacement,
        kAllOffhandPatchName
    );
    mPatchApplied.store(ok, std::memory_order_release);

    if (ok) {
        if (rva == kAllowOffhandQueryRva126511) {
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[NativeOffhandPolicy] 1.26.51.1 getAllowOffHand forced true at RVA 0x%llX",
                static_cast<unsigned long long>(rva)
            );
        } else {
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[NativeOffhandPolicy] 1.26.45.1 Item default flags 0x50 -> 0xD0"
            );
        }
    }

    return ok;
}

void NativeOffhandPolicy::revertPatch() noexcept {
    if (!mPatchApplied.exchange(false, std::memory_order_acq_rel)) {
        return;
    }

    if (!pl::memory::revertPatch(kAllOffhandPatchName)) {
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "[NativeOffhandPolicy] patch revert reported failure"
        );
    }
}

bool NativeOffhandPolicy::install(pl::mod::ModContext& context) noexcept {
    if (installed()) {
        return true;
    }

    mInstruction = resolvePolicyTarget();
    if (mInstruction == 0) {
        context.logger().error(
            "Levi Offhand: native offhand policy target resolution failed"
        );
        return false;
    }

    const auto rva = targetRva(mInstruction);
    if (
        rva == kAllowOffhandQueryRva126511 &&
        !installManualNativePolicyBridge(context)
    ) {
        mInstruction = 0;
        return false;
    }

    mFeatureEnabled.store(true, std::memory_order_release);
    if (!applyPatch()) {
        if (rva == kAllowOffhandQueryRva126511) {
            uninstallManualNativePolicyBridge();
        }
        mInstruction = 0;
        return false;
    }
    if (rva == kAllowOffhandQueryRva126511) {
        context.logger().info(
            "[NativeOffhandPolicy] 1.26.51.1 active: ItemStackBase::getAllowOffHand RVA 0x{:x} forced true",
            rva
        );
    } else {
        context.logger().info(
            "[NativeOffhandPolicy] 1.26.45.1 active: Item constructor RVA 0x{:x} flag bit7 enabled",
            rva
        );
    }
    return true;
}

void NativeOffhandPolicy::uninstall(pl::mod::ModContext& context) noexcept {
    const bool currentQuery = isCurrentQueryTarget(mInstruction);
    if (currentQuery) {
        uninstallManualNativePolicyBridge();
    }
    revertPatch();
    mInstruction = 0;

    if (currentQuery) {
        context.logger().info(
            "[NativeOffhandPolicy] 1.26.51.1 getAllowOffHand query restored"
        );
    } else {
        context.logger().info(
            "[NativeOffhandPolicy] 1.26.45.1 constructor patch removed; existing Item singletons retain flags until process restart"
        );
    }
}

void NativeOffhandPolicy::setFeatureEnabled(bool enabled) noexcept {
    mFeatureEnabled.store(enabled, std::memory_order_release);

    if (enabled) {
        if (!applyPatch()) {
            __android_log_print(
                ANDROID_LOG_ERROR,
                kLogTag,
                "[NativeOffhandPolicy] failed to re-enable native policy"
            );
        }
        return;
    }

    const bool currentQuery = isCurrentQueryTarget(mInstruction);
    revertPatch();
    if (currentQuery) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[NativeOffhandPolicy] 1.26.51.1 query patch disabled immediately"
        );
    } else {
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[NativeOffhandPolicy] 1.26.45.1 constructor patch disabled; restart required for complete rollback of existing Item singletons"
        );
    }
}

bool NativeOffhandPolicy::featureEnabled() const noexcept {
    return mFeatureEnabled.load(std::memory_order_acquire);
}

bool NativeOffhandPolicy::installed() const noexcept {
    return mInstruction != 0 && mPatchApplied.load(std::memory_order_acquire);
}

} // namespace levioffhand::runtime
