#include "runtime/NativeOffhandPolicy.hpp"

#include <android/log.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>

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
 * Item::getAllowOffHand reads bit 7 (0x80) from Item + 0x112.
 * 0x50 -> 0xD0 preserves the existing low flags and adds mAllowOffHand.
 */
constexpr std::uintptr_t kItemDefaultFlagsRva = 0xF65A3BC;
constexpr std::uintptr_t kPatchOffsetFromSignature = 0x0C;

constexpr char kItemConstructorFlagSignature[] =
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

constexpr char kVanillaItemFlagsInstruction[] = "09 0A 80 52";
constexpr char kAllOffhandItemFlagsInstruction[] = "09 1A 80 52";
constexpr char kAllOffhandPatchName[] = "levi_offhand.item_allow_offhand";

constexpr std::array<std::uint8_t, 4> kVanillaBytes{0x09, 0x0A, 0x80, 0x52};
constexpr std::array<std::uint8_t, 4> kPatchedBytes{0x09, 0x1A, 0x80, 0x52};

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

[[nodiscard]] bool bytesEqual(
    std::uintptr_t address,
    const std::array<std::uint8_t, 4>& expected
) noexcept {
    const auto actual = pl::memory::readBytes(address, expected.size());
    return actual.size() == expected.size() &&
        std::memcmp(actual.data(), expected.data(), expected.size()) == 0;
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

    if (bytesEqual(mInstruction, kPatchedBytes)) {
        mPatchApplied.store(true, std::memory_order_release);
        return true;
    }
    if (!bytesEqual(mInstruction, kVanillaBytes)) {
        __android_log_print(
            ANDROID_LOG_ERROR,
            kLogTag,
            "[NativeOffhandPolicy] refusing patch: unexpected Item flags instruction"
        );
        return false;
    }

    const bool ok = pl::memory::writeBytes(
        mInstruction,
        kAllOffhandItemFlagsInstruction,
        kAllOffhandPatchName
    );
    mPatchApplied.store(ok, std::memory_order_release);

    if (ok) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[NativeOffhandPolicy] Item default flags 0x50 -> 0xD0"
        );
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

    mInstruction = 0;
    const auto signatureBase = pl::memory::resolveSignature(
        kItemConstructorFlagSignature,
        kMinecraftLibrary
    );

    if (!belongsToMinecraft(signatureBase)) {
        context.logger().error(
            "Levi Offhand v0.2.62: Item constructor flag signature resolution failed"
        );
        return false;
    }

    mInstruction = signatureBase + kPatchOffsetFromSignature;
    if (
        !bytesEqual(mInstruction, kVanillaBytes) &&
        !bytesEqual(mInstruction, kPatchedBytes)
    ) {
        context.logger().error(
            "Levi Offhand v0.2.62: Item flag instruction fingerprint mismatch"
        );
        mInstruction = 0;
        return false;
    }

    mFeatureEnabled.store(true, std::memory_order_release);
    if (!applyPatch()) {
        mInstruction = 0;
        return false;
    }

    context.logger().info(
        "[NativeOffhandPolicy] active at 0x{:x} (target RVA 0x{:x})",
        mInstruction,
        kItemDefaultFlagsRva
    );
    return true;
}

void NativeOffhandPolicy::uninstall(pl::mod::ModContext& context) noexcept {
    revertPatch();
    mInstruction = 0;
    context.logger().info(
        "[NativeOffhandPolicy] constructor patch removed; existing Item singletons retain flags until process restart"
    );
}

void NativeOffhandPolicy::setFeatureEnabled(bool enabled) noexcept {
    mFeatureEnabled.store(enabled, std::memory_order_release);

    if (enabled) {
        if (!applyPatch()) {
            __android_log_print(
                ANDROID_LOG_ERROR,
                kLogTag,
                "[NativeOffhandPolicy] failed to re-enable constructor patch"
            );
        }
        return;
    }

    revertPatch();
    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "[NativeOffhandPolicy] constructor patch disabled; restart required for complete rollback of existing Item singletons"
    );
}

bool NativeOffhandPolicy::featureEnabled() const noexcept {
    return mFeatureEnabled.load(std::memory_order_acquire);
}

bool NativeOffhandPolicy::installed() const noexcept {
    return mInstruction != 0 && mPatchApplied.load(std::memory_order_acquire);
}

} // namespace levioffhand::runtime
