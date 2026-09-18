#include "runtime/OffhandSwapRuntime.hpp"

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

constexpr unsigned char kMainHand = 0;
constexpr unsigned char kOffHand = 1;

// Minecraft Bedrock Android 1.26.51.1 arm64-v8a.
constexpr std::uintptr_t kSelectedItemRva = 0xF9F7824;
constexpr std::uintptr_t kOffhandSlotRva = 0xF579C2C;
constexpr std::uintptr_t kStackIsNullRva = 0xFFA0F70;
constexpr std::uintptr_t kItemStackCopyCtorRva = 0xFF9D748;
constexpr std::uintptr_t kItemStackDtorRva = 0x85ADF98;

// Actor::setItemInHandSlot(HandSlot, ItemStack const&).
// hand=0 dispatches virtual +0x268; hand=1 dispatches virtual +0x278.
// On LocalPlayer this therefore uses Minecraft's own hand-slot setters instead
// of directly writing ItemStack memory.
constexpr std::uintptr_t kSetItemInHandSlotRva = 0xF579C50;

constexpr std::size_t kItemStackStorageSize = 0x98;

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

    const auto selected = resolveExactTarget(
        kSelectedItemRva, kSelectedItemFingerprint
    );
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
        selected == 0 || offhand == 0 || isNull == 0 ||
        copyCtor == 0 || dtor == 0 || setHand == 0
    ) {
        context.logger().error(
            "Swap runtime: Minecraft 1.26.51.1 native target validation failed"
        );
        return false;
    }

    mGetSelectedItem = reinterpret_cast<GetSelectedItemFn>(selected);
    mGetOffhandSlot = reinterpret_cast<GetOffhandSlotFn>(offhand);
    mStackIsNull = reinterpret_cast<StackIsNullFn>(isNull);
    mItemStackCopyCtor =
        reinterpret_cast<ItemStackCopyCtorFn>(copyCtor);
    mItemStackDtor = reinterpret_cast<ItemStackDtorFn>(dtor);
    mSetItemInHandSlot =
        reinterpret_cast<SetItemInHandSlotFn>(setHand);

    mObservedPlayer.store(nullptr, std::memory_order_release);
    mSwapInProgress.store(false, std::memory_order_release);
    mFeatureEnabled.store(true, std::memory_order_release);
    mInstalled.store(true, std::memory_order_release);

    context.logger().info(
        "Swap runtime active: selected hotbar <-> offhand native hand setter"
    );
    return true;
}

void OffhandSwapRuntime::uninstall(pl::mod::ModContext&) noexcept {
    mInstalled.store(false, std::memory_order_release);
    mFeatureEnabled.store(false, std::memory_order_release);
    mObservedPlayer.store(nullptr, std::memory_order_release);
    mSwapInProgress.store(false, std::memory_order_release);

    mGetSelectedItem = nullptr;
    mGetOffhandSlot = nullptr;
    mStackIsNull = nullptr;
    mItemStackCopyCtor = nullptr;
    mItemStackDtor = nullptr;
    mSetItemInHandSlot = nullptr;
}

void OffhandSwapRuntime::setFeatureEnabled(bool enabled) noexcept {
    mFeatureEnabled.store(enabled, std::memory_order_release);
}

bool OffhandSwapRuntime::featureEnabled() const noexcept {
    return mFeatureEnabled.load(std::memory_order_acquire);
}

bool OffhandSwapRuntime::installed() const noexcept {
    return mInstalled.load(std::memory_order_acquire) &&
        mGetSelectedItem != nullptr &&
        mGetOffhandSlot != nullptr &&
        mStackIsNull != nullptr &&
        mItemStackCopyCtor != nullptr &&
        mItemStackDtor != nullptr &&
        mSetItemInHandSlot != nullptr;
}

void OffhandSwapRuntime::observePlayer(const void* player) noexcept {
    if (!installed() || !validObject(player)) {
        return;
    }
    mObservedPlayer.store(player, std::memory_order_release);
}

bool OffhandSwapRuntime::swapNow() noexcept {
    if (!installed() || !featureEnabled()) {
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "[SwapRuntime] ignored: runtime unavailable or module disabled"
        );
        return false;
    }

    bool expected = false;
    if (!mSwapInProgress.compare_exchange_strong(
            expected,
            true,
            std::memory_order_acq_rel
        )) {
        return false;
    }
    ScopedSwapFlag swapGuard(mSwapInProgress);

    const void* observed =
        mObservedPlayer.load(std::memory_order_acquire);
    if (!validObject(observed)) {
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "[SwapRuntime] no valid LocalPlayer observed yet"
        );
        return false;
    }

    void* player = const_cast<void*>(observed);
    const void* mainStack = mGetSelectedItem(player);
    const void* offStack = mGetOffhandSlot(player);
    if (mainStack == nullptr || offStack == nullptr) {
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "[SwapRuntime] hand stack lookup failed"
        );
        return false;
    }

    if (mStackIsNull(mainStack) && mStackIsNull(offStack)) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[SwapRuntime] both hands empty; nothing to swap"
        );
        return true;
    }

    // Snapshot both sources before any setter runs.  This is important:
    // setItemInHandSlot mutates a live slot, so reading the second source after
    // the first write would lose one side of the exchange.
    ItemStackSnapshot mainSnapshot(
        mItemStackCopyCtor,
        mItemStackDtor,
        mainStack
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
            "[SwapRuntime] ItemStack snapshot creation failed"
        );
        return false;
    }

    // Match Java F semantics: selected mainhand receives the former offhand
    // stack, and offhand receives the former selected-mainhand stack.
    mSetItemInHandSlot(player, kMainHand, offSnapshot.get());
    mSetItemInHandSlot(player, kOffHand, mainSnapshot.get());

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "[SwapRuntime] swapped MAINHAND <-> OFFHAND"
    );
    return true;
}

} // namespace levioffhand::runtime
