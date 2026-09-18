#include "runtime/OffhandSwapRuntime.hpp"

#include <android/log.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <pthread.h>

namespace levioffhand::runtime {
namespace {

constexpr char kMinecraftLibrary[] = "libminecraftpe.so";
constexpr char kLogTag[] = "Levi Offhand";
constexpr char kMinecraftMainThreadName[] = "MINECRAFT MAIN";

constexpr unsigned char kMainHand = 0;
constexpr unsigned char kOffHand = 1;

// Minecraft Bedrock Android 1.26.51.1 arm64-v8a.
constexpr std::uintptr_t kOffhandSlotRva = 0xF579C2C;
constexpr std::uintptr_t kStackIsNullRva = 0xFFA0F70;
constexpr std::uintptr_t kItemStackCopyCtorRva = 0xFF9D748;
constexpr std::uintptr_t kItemStackDtorRva = 0x85ADF98;

// Actor::setItemInHandSlot(HandSlot, ItemStack const&).
// hand=0 dispatches virtual +0x268; hand=1 dispatches virtual +0x278.
constexpr std::uintptr_t kSetItemInHandSlotRva = 0xF579C50;

constexpr std::size_t kItemStackStorageSize = 0x98;

constexpr std::array<std::uint8_t, 16> kOffhandSlotFingerprint{
    0xFD, 0x7B, 0xBF, 0xA9, 0xFD, 0x03, 0x00, 0x91,
    0x00, 0x20, 0x00, 0x91, 0x95, 0xF8, 0x10, 0x94,
};
constexpr std::array<std::uint8_t, 16> kStackIsNullFingerprint{
    0x08, 0x8C, 0x40, 0x39, 0xE8, 0x04, 0x00, 0x34,
    0xFD, 0x7B, 0xBE, 0xA9, 0xF3, 0x0B, 0x00, 0xF9,
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
constexpr std::array<std::uint8_t, 16> kSetItemInHandSlotFingerprint{
    0x28, 0x1C, 0x00, 0x72, 0x00, 0x01, 0x00, 0x54,
    0x1F, 0x05, 0x00, 0x71, 0x61, 0x01, 0x00, 0x54,
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

[[nodiscard]] bool validObject(const void* object) noexcept {
    if (object == nullptr) {
        return false;
    }

    const void* vtable = nullptr;
    std::memcpy(&vtable, object, sizeof(vtable));
    return belongsToMinecraft(reinterpret_cast<std::uintptr_t>(vtable));
}

[[nodiscard]] bool isMinecraftMainThread() noexcept {
    char name[16]{};
    if (pthread_getname_np(pthread_self(), name, sizeof(name)) != 0) {
        return false;
    }
    return std::strcmp(name, kMinecraftMainThreadName) == 0;
}

class ScopedSwapFlag final {
public:
    explicit ScopedSwapFlag(std::atomic_bool& flag) noexcept
        : mFlag(flag) {}

    ~ScopedSwapFlag() noexcept {
        mFlag.store(false, std::memory_order_release);
    }

private:
    std::atomic_bool& mFlag;
};

class ItemStackSnapshot final {
public:
    ItemStackSnapshot(
        OffhandSwapRuntime::ItemStackCopyCtorFn copyCtor,
        OffhandSwapRuntime::ItemStackDtorFn dtor,
        const void* source
    ) noexcept
        : mDtor(dtor) {
        if (copyCtor == nullptr || dtor == nullptr || source == nullptr) {
            return;
        }

        copyCtor(mStorage.data(), source);
        mConstructed = true;
    }

    ~ItemStackSnapshot() noexcept {
        if (mConstructed && mDtor != nullptr) {
            mDtor(mStorage.data());
        }
    }

    ItemStackSnapshot(const ItemStackSnapshot&) = delete;
    ItemStackSnapshot& operator=(const ItemStackSnapshot&) = delete;

    [[nodiscard]] const void* get() const noexcept {
        return mConstructed ? mStorage.data() : nullptr;
    }

private:
    alignas(16) std::array<std::byte, kItemStackStorageSize> mStorage{};
    OffhandSwapRuntime::ItemStackDtorFn mDtor{nullptr};
    bool mConstructed{false};
};

} // namespace

OffhandSwapRuntime& OffhandSwapRuntime::instance() noexcept {
    static OffhandSwapRuntime runtime;
    return runtime;
}

bool OffhandSwapRuntime::install(pl::mod::ModContext& context) noexcept {
    uninstall(context);

    const auto offhand = resolveExactTarget(
        kOffhandSlotRva, kOffhandSlotFingerprint
    );
    const auto isNull = resolveExactTarget(
        kStackIsNullRva, kStackIsNullFingerprint
    );
    const auto copyCtor = resolveExactTarget(
        kItemStackCopyCtorRva, kItemStackCopyCtorFingerprint
    );
    const auto dtor = resolveExactTarget(
        kItemStackDtorRva, kItemStackDtorFingerprint
    );
    const auto setHand = resolveExactTarget(
        kSetItemInHandSlotRva, kSetItemInHandSlotFingerprint
    );

    if (
        offhand == 0 || isNull == 0 ||
        copyCtor == 0 || dtor == 0 || setHand == 0
    ) {
        context.logger().error(
            "Swap runtime: Minecraft 1.26.51.1 native target validation failed"
        );
        return false;
    }

    mGetOffhandSlot = reinterpret_cast<GetOffhandSlotFn>(offhand);
    mStackIsNull = reinterpret_cast<StackIsNullFn>(isNull);
    mItemStackCopyCtor =
        reinterpret_cast<ItemStackCopyCtorFn>(copyCtor);
    mItemStackDtor = reinterpret_cast<ItemStackDtorFn>(dtor);
    mSetItemInHandSlot =
        reinterpret_cast<SetItemInHandSlotFn>(setHand);

    mSwapRequested.store(false, std::memory_order_release);
    mSwapInProgress.store(false, std::memory_order_release);
    mFeatureEnabled.store(true, std::memory_order_release);
    mInstalled.store(true, std::memory_order_release);

    context.logger().info(
        "Swap runtime active: F requests are drained on MINECRAFT MAIN"
    );
    return true;
}

void OffhandSwapRuntime::uninstall(pl::mod::ModContext&) noexcept {
    mInstalled.store(false, std::memory_order_release);
    mFeatureEnabled.store(false, std::memory_order_release);
    mSwapRequested.store(false, std::memory_order_release);
    mSwapInProgress.store(false, std::memory_order_release);

    mGetOffhandSlot = nullptr;
    mStackIsNull = nullptr;
    mItemStackCopyCtor = nullptr;
    mItemStackDtor = nullptr;
    mSetItemInHandSlot = nullptr;
}

void OffhandSwapRuntime::setFeatureEnabled(bool enabled) noexcept {
    mFeatureEnabled.store(enabled, std::memory_order_release);
    if (!enabled) {
        mSwapRequested.store(false, std::memory_order_release);
    }
}

bool OffhandSwapRuntime::featureEnabled() const noexcept {
    return mFeatureEnabled.load(std::memory_order_acquire);
}

bool OffhandSwapRuntime::installed() const noexcept {
    return mInstalled.load(std::memory_order_acquire) &&
        mGetOffhandSlot != nullptr &&
        mStackIsNull != nullptr &&
        mItemStackCopyCtor != nullptr &&
        mItemStackDtor != nullptr &&
        mSetItemInHandSlot != nullptr;
}

void OffhandSwapRuntime::requestSwap() noexcept {
    if (!installed() || !featureEnabled()) {
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "[SwapRuntime] F request ignored: runtime unavailable or module disabled"
        );
        return;
    }

    // The external HUD button is dispatched from Android/Java's UI thread.
    // Never invoke Minecraft ItemStack constructors/setters here: the 1.26.51.1
    // ItemRegistry TLS/context is not valid on this thread.
    mSwapRequested.store(true, std::memory_order_release);

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "[SwapRuntime] F swap queued for MINECRAFT MAIN"
    );
}

bool OffhandSwapRuntime::shouldProcessPendingSwap() const noexcept {
    return installed() &&
        featureEnabled() &&
        mSwapRequested.load(std::memory_order_acquire) &&
        isMinecraftMainThread();
}

bool OffhandSwapRuntime::processPendingSwap(
    void* player,
    const void* selectedStack
) noexcept {
    if (!shouldProcessPendingSwap()) {
        return false;
    }

    if (!validObject(player) || selectedStack == nullptr) {
        // Keep the request queued; a later verified main-thread getter can
        // provide a usable LocalPlayer/selected stack.
        return false;
    }

    const void* offStack = mGetOffhandSlot(player);
    if (offStack == nullptr) {
        return false;
    }

    bool requested = true;
    if (!mSwapRequested.compare_exchange_strong(
            requested,
            false,
            std::memory_order_acq_rel
        )) {
        return false;
    }

    bool expected = false;
    if (!mSwapInProgress.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel
        )) {
        mSwapRequested.store(true, std::memory_order_release);
        return false;
    }
    ScopedSwapFlag swapGuard(mSwapInProgress);

    const bool mainEmpty = mStackIsNull(selectedStack);
    const bool offEmpty = mStackIsNull(offStack);

    if (mainEmpty && offEmpty) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[SwapRuntime] both hands empty; request consumed"
        );
        return true;
    }

    // Avoid copy-constructing an EMPTY_ITEM.  Besides doing less work, this
    // keeps ItemRegistry access limited to stacks that actually own an Item.
    if (offEmpty) {
        ItemStackSnapshot mainSnapshot(
            mItemStackCopyCtor,
            mItemStackDtor,
            selectedStack
        );
        if (mainSnapshot.get() == nullptr) {
            __android_log_print(
                ANDROID_LOG_ERROR,
                kLogTag,
                "[SwapRuntime] MAINHAND snapshot failed on MINECRAFT MAIN"
            );
            return false;
        }

        mSetItemInHandSlot(player, kMainHand, offStack);
        mSetItemInHandSlot(player, kOffHand, mainSnapshot.get());
    } else if (mainEmpty) {
        ItemStackSnapshot offSnapshot(
            mItemStackCopyCtor,
            mItemStackDtor,
            offStack
        );
        if (offSnapshot.get() == nullptr) {
            __android_log_print(
                ANDROID_LOG_ERROR,
                kLogTag,
                "[SwapRuntime] OFFHAND snapshot failed on MINECRAFT MAIN"
            );
            return false;
        }

        mSetItemInHandSlot(player, kOffHand, selectedStack);
        mSetItemInHandSlot(player, kMainHand, offSnapshot.get());
    } else {
        // Both sources must be detached before the first live hand setter runs.
        ItemStackSnapshot mainSnapshot(
            mItemStackCopyCtor,
            mItemStackDtor,
            selectedStack
        );
        ItemStackSnapshot offSnapshot(
            mItemStackCopyCtor,
            mItemStackDtor,
            offStack
        );

        if (
            mainSnapshot.get() == nullptr ||
            offSnapshot.get() == nullptr
        ) {
            __android_log_print(
                ANDROID_LOG_ERROR,
                kLogTag,
                "[SwapRuntime] hand snapshot creation failed on MINECRAFT MAIN"
            );
            return false;
        }

        mSetItemInHandSlot(player, kMainHand, offSnapshot.get());
        mSetItemInHandSlot(player, kOffHand, mainSnapshot.get());
    }

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "[SwapRuntime] swapped MAINHAND <-> OFFHAND on MINECRAFT MAIN"
    );
    return true;
}

} // namespace levioffhand::runtime
