#include "swap/SwapRuntime.hpp"

#include <android/log.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <elf.h>
#include <link.h>
#include <memory>
#include <sys/prctl.h>

#include <pl/memory/Hook.hpp>

namespace levioffhand::swap {
namespace {

constexpr char kMinecraftLibrary[] = "libminecraftpe.so";
constexpr char kLogTag[] = "Levi Offhand";

constexpr unsigned char kOffHand = 1;

// Minecraft Bedrock Android 1.26.51.1 arm64-v8a.
constexpr std::uintptr_t kOffhandSlotRva = 0xF579C2C;
constexpr std::uintptr_t kStackIsNullRva = 0xFFA0F70;
constexpr std::uintptr_t kItemStackCopyCtorRva = 0xFF9D748;
constexpr std::uintptr_t kItemStackDtorRva = 0x85ADF98;

// Actor::setItemInHandSlot(HandSlot, ItemStack const&).
// hand=0 dispatches virtual +0x268; hand=1 dispatches virtual +0x278.
constexpr std::uintptr_t kSetItemInHandSlotRva = 0xF579C50;

// Player::setSelectedItem(ItemStack const&) on 1.26.51.1.  This path updates
// the selected inventory slot itself (not only the Actor carried-item view),
// which is required to invalidate the first-swap ghost stack.
constexpr std::uintptr_t kSetSelectedItemRva = 0xF9F7850;

// ClientInstance::preFrameTick is the reliable native per-frame pump.
// IClientInstance vtable slot 26 => 1.26.51.1 RVA 0x9803334.
// getLocalPlayer() is vtable slot 32 => object-vptr offset +0x100.
constexpr std::uintptr_t kClientPreFrameTickRva = 0x9803334;
constexpr std::uintptr_t kSelectedItemRva = 0xF9F7824;

// ItemStack::EMPTY_ITEM() backing object returned by Player::getSelectedItem()
// when the selected container is not Inventory.  Using the game's own empty
// stack lets an occupied<->occupied swap pass through an empty intermediate
// state without fabricating an ItemStack.
constexpr std::uintptr_t kEmptyItemRva = 0x134C6780;

constexpr std::size_t kClientGetLocalPlayerVtableOffset = 0x100;

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
constexpr std::array<std::uint8_t, 48> kSetSelectedItemFingerprint{
    0xFD, 0x7B, 0xBB, 0xA9, 0xFC, 0x67, 0x01, 0xA9,
    0xF8, 0x5F, 0x02, 0xA9, 0xF6, 0x57, 0x03, 0xA9,
    0xF4, 0x4F, 0x04, 0xA9, 0xFD, 0x03, 0x00, 0x91,
    0xFF, 0xC3, 0x0E, 0xD1, 0x56, 0xD0, 0x3B, 0xD5,
    0xF3, 0x03, 0x01, 0xAA, 0xF4, 0x03, 0x00, 0xAA,
    0xC8, 0x16, 0x40, 0xF9, 0xA8, 0x83, 0x1F, 0xF8,
};
constexpr std::array<std::uint8_t, 16> kClientPreFrameTickFingerprint{
    0xFF, 0xC3, 0x00, 0xD1, 0xFD, 0x7B, 0x01, 0xA9,
    0xF4, 0x4F, 0x02, 0xA9, 0xFD, 0x43, 0x00, 0x91,
};
constexpr std::array<std::uint8_t, 16> kSelectedItemFingerprint{
    0x08, 0xB8, 0x42, 0xF9, 0x09, 0xC1, 0x42, 0x39,
    0x89, 0x00, 0x00, 0x34, 0x60, 0xD6, 0x01, 0xF0,
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

struct AddressSearchState {
    std::uintptr_t address{0};
    std::uint32_t requiredFlags{0};
    bool found{false};
};

int addressSearchCallback(
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

    auto& state=*static_cast<AddressSearchState*>(rawState);
    const auto base=static_cast<std::uintptr_t>(info->dlpi_addr);

    for(std::size_t i=0;i<info->dlpi_phnum;++i) {
        const auto& ph=info->dlpi_phdr[i];
        if(ph.p_type!=PT_LOAD) {
            continue;
        }

        const auto begin=base+static_cast<std::uintptr_t>(ph.p_vaddr);
        const auto end=begin+static_cast<std::uintptr_t>(ph.p_memsz);
        const auto flags=static_cast<std::uint32_t>(ph.p_flags);

        if(
            state.address>=begin
            &&
            state.address<end
            &&
            (flags & state.requiredFlags)==state.requiredFlags
        ) {
            state.found=true;
            break;
        }
    }

    return 1;
}

[[nodiscard]] bool addressInMinecraftLoadSegment(
    std::uintptr_t address,
    std::uint32_t requiredFlags
) noexcept {
    if(address==0) {
        return false;
    }

    AddressSearchState state{
        .address=address,
        .requiredFlags=requiredFlags,
        .found=false,
    };
    dl_iterate_phdr(&addressSearchCallback,&state);
    return state.found;
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
    if (!addressInMinecraftLoadSegment(target, PF_X)) {
        return 0;
    }

    return std::memcmp(
        reinterpret_cast<const void*>(target),
        fingerprint.data(),
        fingerprint.size()
    ) == 0 ? target : 0;
}

using ClientPreFrameTickFn = void (*)(void*);
using GetSelectedItemFn = const void* (*)(const void*);
using GetLocalPlayerFn = void* (*)(const void*);

std::unique_ptr<pl::memory::HookHandle> gClientPreFrameTickHook;
void* gClientPreFrameTickOriginal = nullptr;
GetSelectedItemFn gGetSelectedItem = nullptr;
const void* gEmptyItem = nullptr;

// The swap is only allowed to execute from our ClientInstance::preFrameTick
// detour.  Do not use the Linux thread-name string as an authorization gate:
// Android may expose visually identical names with trailing/implementation
// differences, which caused a request to be pumped and then deferred forever.
thread_local std::uint32_t gPreFrameSwapPumpDepth = 0;

class ScopedPreFrameSwapPump final {
public:
    ScopedPreFrameSwapPump() noexcept {
        ++gPreFrameSwapPumpDepth;
    }

    ~ScopedPreFrameSwapPump() noexcept {
        --gPreFrameSwapPumpDepth;
    }

    ScopedPreFrameSwapPump(const ScopedPreFrameSwapPump&) = delete;
    ScopedPreFrameSwapPump& operator=(const ScopedPreFrameSwapPump&) = delete;
};

[[nodiscard]] void* localPlayerFromClient(void* client) noexcept {
    if (client == nullptr) {
        return nullptr;
    }

    const void* vtable = nullptr;
    std::memcpy(&vtable, client, sizeof(vtable));
    if (!addressInMinecraftLoadSegment(
            reinterpret_cast<std::uintptr_t>(vtable),
            0
        )) {
        return nullptr;
    }

    GetLocalPlayerFn getter = nullptr;
    std::memcpy(
        &getter,
        static_cast<const std::byte*>(vtable) +
            kClientGetLocalPlayerVtableOffset,
        sizeof(getter)
    );
    if (
        getter == nullptr ||
        !addressInMinecraftLoadSegment(
            reinterpret_cast<std::uintptr_t>(getter),
            PF_X
        )
    ) {
        return nullptr;
    }

    return getter(client);
}

void currentThreadName(char* outName) noexcept {
    if (outName == nullptr) {
        return;
    }

    char name[16]{};
    if (prctl(PR_GET_NAME, name, 0, 0, 0) != 0) {
        std::strncpy(name, "unknown", sizeof(name) - 1);
    }

    std::strncpy(outName, name, 15);
    outName[15] = '\0';
}

void clientPreFrameTickDetour(void* client) noexcept {
    const auto original =
        reinterpret_cast<ClientPreFrameTickFn>(gClientPreFrameTickOriginal);

    // Preserve Minecraft first.  Then execute the queued hand swap at a stable
    // per-frame point on the same native client thread.
    if (original != nullptr) {
        original(client);
    }

    auto& swap = SwapRuntime::instance();
    if (!swap.hasPendingSwap()) {
        return;
    }

    void* player = localPlayerFromClient(client);
    if (player == nullptr || gGetSelectedItem == nullptr) {
        return;
    }

    ScopedPreFrameSwapPump pumpScope;

    char pumpThread[16]{};
    currentThreadName(pumpThread);
    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "[SwapRuntime] ClientInstance::preFrameTick pumping queued F swap (thread=%s)",
        pumpThread
    );

    // Read the selected stack, then process the request here.  The scoped
    // preFrameTick marker above is the execution authority; the thread name
    // is diagnostic only.
    const void* selected = gGetSelectedItem(player);
    (void)swap.processPendingSwap(player, selected);
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
        SwapRuntime::ItemStackCopyCtorFn copyCtor,
        SwapRuntime::ItemStackDtorFn dtor,
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
    SwapRuntime::ItemStackDtorFn mDtor{nullptr};
    bool mConstructed{false};
};

} // namespace

SwapRuntime& SwapRuntime::instance() noexcept {
    static SwapRuntime runtime;
    return runtime;
}

bool SwapRuntime::install(pl::mod::ModContext& context) noexcept {
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
    const auto setSelected = resolveExactTarget(
        kSetSelectedItemRva, kSetSelectedItemFingerprint
    );
    const auto clientPreFrameTick = resolveExactTarget(
        kClientPreFrameTickRva, kClientPreFrameTickFingerprint
    );
    const auto selectedItem = resolveExactTarget(
        kSelectedItemRva, kSelectedItemFingerprint
    );

    const auto moduleBase = minecraftModuleBase();
    const auto emptyItem =
        moduleBase == 0 ? 0 : moduleBase + kEmptyItemRva;

    const bool emptyMapped=
        emptyItem!=0
        &&
        addressInMinecraftLoadSegment(emptyItem,0);

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "[SwapEngine] targets off=%d null=%d copy=%d dtor=%d setOff=%d "
        "setSelected=%d preFrame=%d selected=%d emptyMapped=%d",
        offhand!=0,
        isNull!=0,
        copyCtor!=0,
        dtor!=0,
        setHand!=0,
        setSelected!=0,
        clientPreFrameTick!=0,
        selectedItem!=0,
        emptyMapped
    );

    if (
        offhand == 0 || isNull == 0 ||
        copyCtor == 0 || dtor == 0 || setHand == 0 ||
        setSelected == 0 ||
        clientPreFrameTick == 0 || selectedItem == 0 ||
        !emptyMapped
    ) {
        context.logger().error(
            "Swap engine: Minecraft 1.26.51.1 native target validation failed"
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
    mSetSelectedItem =
        reinterpret_cast<SetSelectedItemFn>(setSelected);
    gGetSelectedItem =
        reinterpret_cast<GetSelectedItemFn>(selectedItem);
    gEmptyItem = reinterpret_cast<const void*>(emptyItem);

    // Fail closed if the recovered global is not actually Minecraft's empty
    // ItemStack.  A bad EMPTY_ITEM pointer must never reach an inventory
    // setter.
    if (!mStackIsNull(gEmptyItem)) {
        context.logger().error(
            "Swap runtime: ItemStack::EMPTY_ITEM validation failed"
        );
        gGetSelectedItem = nullptr;
        gEmptyItem = nullptr;
        mGetOffhandSlot = nullptr;
        mStackIsNull = nullptr;
        mItemStackCopyCtor = nullptr;
        mItemStackDtor = nullptr;
        mSetItemInHandSlot = nullptr;
        mSetSelectedItem = nullptr;
        return false;
    }

    gClientPreFrameTickOriginal = nullptr;
    gClientPreFrameTickHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(clientPreFrameTick),
        reinterpret_cast<void*>(&clientPreFrameTickDetour),
        &gClientPreFrameTickOriginal,
        pl::memory::HookPriority::Normal
    );
    if (
        !gClientPreFrameTickHook ||
        !gClientPreFrameTickHook->installed() ||
        gClientPreFrameTickOriginal == nullptr
    ) {
        context.logger().error(
            "Swap runtime: ClientInstance::preFrameTick hook failed"
        );
        gClientPreFrameTickHook.reset();
        gClientPreFrameTickOriginal = nullptr;
        gGetSelectedItem = nullptr;
        gEmptyItem = nullptr;
        mGetOffhandSlot = nullptr;
        mStackIsNull = nullptr;
        mItemStackCopyCtor = nullptr;
        mItemStackDtor = nullptr;
        mSetItemInHandSlot = nullptr;
        mSetSelectedItem = nullptr;
        return false;
    }

    mSwapRequested.store(false, std::memory_order_release);
    mSwapInProgress.store(false, std::memory_order_release);
    mFeatureEnabled.store(true, std::memory_order_release);
    mInstalled.store(true, std::memory_order_release);

    context.logger().info(
        "Swap engine active: F requests pumped by ClientInstance::preFrameTick"
    );
    return true;
}

void SwapRuntime::uninstall(pl::mod::ModContext&) noexcept {
    mInstalled.store(false, std::memory_order_release);
    mFeatureEnabled.store(false, std::memory_order_release);
    mSwapRequested.store(false, std::memory_order_release);
    mSwapInProgress.store(false, std::memory_order_release);

    if (gClientPreFrameTickHook) {
        gClientPreFrameTickHook->reset();
        gClientPreFrameTickHook.reset();
    }
    gClientPreFrameTickOriginal = nullptr;
    gGetSelectedItem = nullptr;
    gEmptyItem = nullptr;
    gPreFrameSwapPumpDepth = 0;

    mGetOffhandSlot = nullptr;
    mStackIsNull = nullptr;
    mItemStackCopyCtor = nullptr;
    mItemStackDtor = nullptr;
    mSetItemInHandSlot = nullptr;
    mSetSelectedItem = nullptr;
}

void SwapRuntime::setFeatureEnabled(bool enabled) noexcept {
    mFeatureEnabled.store(enabled, std::memory_order_release);
    if (!enabled) {
        mSwapRequested.store(false, std::memory_order_release);
    }
}

bool SwapRuntime::featureEnabled() const noexcept {
    return mFeatureEnabled.load(std::memory_order_acquire);
}

bool SwapRuntime::installed() const noexcept {
    return mInstalled.load(std::memory_order_acquire) &&
        mGetOffhandSlot != nullptr &&
        mStackIsNull != nullptr &&
        mItemStackCopyCtor != nullptr &&
        mItemStackDtor != nullptr &&
        mSetItemInHandSlot != nullptr &&
        mSetSelectedItem != nullptr &&
        gEmptyItem != nullptr;
}

void SwapRuntime::requestSwap() noexcept {
    if (!installed() || !featureEnabled()) {
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "[SwapRuntime] F request ignored: runtime unavailable or module disabled"
        );
        return;
    }

    // The external HUD button is dispatched from Android/Java's UI thread.
    // Never invoke Minecraft ItemStack constructors/setters here.  The native
    // ClientInstance::preFrameTick hook is the reliable game-thread pump.
    mSwapRequested.store(true, std::memory_order_release);

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "[SwapRuntime] F swap queued for MINECRAFT MAIN"
    );
}

bool SwapRuntime::hasPendingSwap() const noexcept {
    return installed() &&
        featureEnabled() &&
        mSwapRequested.load(std::memory_order_acquire);
}

bool SwapRuntime::processPendingSwap(
    void* player,
    const void* selectedStack
) noexcept {
    if (!hasPendingSwap()) {
        return false;
    }

    if (player == nullptr || selectedStack == nullptr) {
        // Keep the request queued until a native client frame exposes both.
        return false;
    }

    if (gPreFrameSwapPumpDepth == 0) {
        char executionThread[16]{};
        currentThreadName(executionThread);
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "[SwapRuntime] pending F swap rejected outside preFrameTick pump (thread=%s)",
            executionThread
        );
        return false;
    }

    const void* offStack = mGetOffhandSlot(player);
    if (offStack == nullptr) {
        return false;
    }

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "[SwapRuntime] draining queued F swap on MINECRAFT MAIN"
    );

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

    // Snapshot every non-empty source before the first mutation.  After this
    // point each storage has exactly one owner/setter:
    //   selected hotbar -> Player::setSelectedItem
    //   offhand         -> Actor/LocalPlayer offhand path (hand=1)
    // Never call setItemInHandSlot(hand=0): it represents the carried-item
    // view and writing it in addition to setSelectedItem is what produced
    // first-swap ghosts, duplicate-looking stacks and later item loss.
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
                "[SwapRuntime] MAINHAND snapshot failed"
            );
            return false;
        }

        // offStack is the native EMPTY_ITEM currently stored in offhand.
        mSetSelectedItem(player, offStack);
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
                "[SwapRuntime] OFFHAND snapshot failed"
            );
            return false;
        }

        // selectedStack is the native empty selected-slot stack.
        mSetItemInHandSlot(player, kOffHand, selectedStack);
        mSetSelectedItem(player, offSnapshot.get());
    } else {
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
                "[SwapRuntime] hand snapshot creation failed"
            );
            return false;
        }

        if (gEmptyItem == nullptr || !mStackIsNull(gEmptyItem)) {
            __android_log_print(
                ANDROID_LOG_ERROR,
                kLogTag,
                "[SwapRuntime] native EMPTY_ITEM unavailable during occupied swap"
            );
            return false;
        }

        // Do not replace A with B while B still exists in offhand (or vice
        // versa).  That transient duplicate is observable by Bedrock's
        // inventory transaction/reconciliation layer and is what makes the
        // second swap produce ghosts, duplicates or item loss.
        //
        // Route through the game's native EMPTY_ITEM instead:
        //   MAIN=A, OFF=B
        //   MAIN=empty, OFF=B
        //   MAIN=empty, OFF=A
        //   MAIN=B, OFF=A
        //
        // Reusing the same slot twice also lets the native transaction layer
        // coalesce MAIN A->empty->B while no frame ever contains two copies
        // of A or B.
        mSetSelectedItem(player, gEmptyItem);
        mSetItemInHandSlot(player, kOffHand, mainSnapshot.get());
        mSetSelectedItem(player, offSnapshot.get());
    }

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "[SwapRuntime] swapped selected hotbar <-> OFFHAND without transient duplicates"
    );
    return true;
}

} // namespace levioffhand::runtime
