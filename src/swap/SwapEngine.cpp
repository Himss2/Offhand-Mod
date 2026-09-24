#include "swap/SwapEngine.hpp"

#include <android/log.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <elf.h>
#include <link.h>

namespace levioffhand::swap {
namespace {

constexpr char kMinecraftLibrary[]="libminecraftpe.so";
constexpr char kLogTag[]="Levi Offhand";
constexpr unsigned char kOffHand=1;

// Minecraft Bedrock Android 1.26.51.1 arm64-v8a.
constexpr std::uintptr_t kOffhandSlotRva=0xF579C2C;
constexpr std::uintptr_t kStackIsNullRva=0xFFA0F70;
constexpr std::uintptr_t kItemStackCopyCtorRva=0xFF9D748;
constexpr std::uintptr_t kItemStackDtorRva=0x85ADF98;
constexpr std::uintptr_t kSetItemInHandSlotRva=0xF579C50;
constexpr std::uintptr_t kSetSelectedItemRva=0xF9F7850;
constexpr std::uintptr_t kSetOffhandRawRva=0xF579C24;
constexpr std::uintptr_t kStackDescriptorFromItemRva=0xFF73E8C;
constexpr std::uintptr_t kInventoryActionDtorRva=0x8D623A4;
constexpr std::uintptr_t kInventoryTransactionAddActionRva=0x1001EC24;
// Player-aware client legacy predictive wrapper.
// RE 1.26.51.1: 0xF88A434 accepts Player*, resolves the active
// ItemStackNetManagerClient, opens the negative-even legacy request id, and
// returns the native cleanup scope used by gameplay before hand writeback.
constexpr std::uintptr_t kTryBeginClientLegacyTransactionRva=0xF88A434;
constexpr std::uintptr_t kEmptyItemRva=0x134C6780;

constexpr std::size_t kItemStackStorageSize=0x98;
constexpr std::size_t kInventoryActionSize=0x1E0;
constexpr std::size_t kInventoryActionOldDescriptorOffset=0x10;
constexpr std::size_t kInventoryActionNewDescriptorOffset=0x60;
constexpr std::size_t kInventoryActionOldStackOffset=0xB0;
constexpr std::size_t kInventoryActionNewStackOffset=0x148;
constexpr std::size_t kPlayerInventoryTransactionManagerOffset=0x9B8;
constexpr std::size_t kInventoryTransactionPendingOffset=0x08;
constexpr std::size_t kPlayerItemStackNetManagerOffset=0xA00;
constexpr std::size_t kItemStackNetManagerLegacyAllowedVtableOffset=0x28;
constexpr std::size_t kItemStackNetManagerLegacyRequestIdOffset=0x50;
constexpr std::size_t kNativeLegacyScopeCallableOffset=0x20;
constexpr std::size_t kNativeLegacyScopeEngagedOffset=0x30;
constexpr std::size_t kNativeLegacyScopeSize=0x38;
constexpr std::size_t kNativeFunctionInvokeVtableOffset=0x30;
constexpr std::size_t kNativeFunctionDestroyVtableOffset=0x20;
constexpr std::uint8_t kOffhandLegacyContainerId=0x77;

// Player::getSelectedItem @ 0xF9F7824 was reverse engineered rather than
// called. RightUseRouter hooks that entry before swap installs, so validating
// or calling its prologue would couple swap to right-use and caused build #596
// runtime validation to fail.
//
// 0xF9F7824:
//   LDR X8,[X0,#0x570]
//   LDRB W9,[X8,#0xB0]
//   CBNZ -> ItemStack::EMPTY_ITEM
//   LDR X0,[X8,#0xB8]
//   LDR W1,[X8,#0x10]
//   vcall [vtable+0x40]
constexpr std::size_t kPlayerSelectedStateOffset=0x570;
constexpr std::size_t kSelectedStateFlagOffset=0xB0;
constexpr std::size_t kSelectedStateContainerOffset=0xB8;
constexpr std::size_t kSelectedStateIndexOffset=0x10;
constexpr std::size_t kContainerGetItemVtableOffset=0x40;

constexpr std::array<std::uint8_t,16> kOffhandSlotFingerprint{
    0xFD,0x7B,0xBF,0xA9,0xFD,0x03,0x00,0x91,
    0x00,0x20,0x00,0x91,0x95,0xF8,0x10,0x94,
};
constexpr std::array<std::uint8_t,16> kStackIsNullFingerprint{
    0x08,0x8C,0x40,0x39,0xE8,0x04,0x00,0x34,
    0xFD,0x7B,0xBE,0xA9,0xF3,0x0B,0x00,0xF9,
};
constexpr std::array<std::uint8_t,28> kItemStackCopyCtorFingerprint{
    0xFD,0x7B,0xBD,0xA9,0xF5,0x0B,0x00,0xF9,
    0xF4,0x4F,0x02,0xA9,0xFD,0x03,0x00,0x91,
    0xF5,0x03,0x01,0xAA,0xF3,0x03,0x00,0xAA,
    0x6D,0xFE,0xFF,0x97,
};
constexpr std::array<std::uint8_t,28> kItemStackDtorFingerprint{
    0xFF,0xC3,0x00,0xD1,0xFD,0x7B,0x01,0xA9,
    0xF4,0x4F,0x02,0xA9,0xFD,0x43,0x00,0x91,
    0x54,0xD0,0x3B,0xD5,0xF3,0x03,0x00,0xAA,
    0xE9,0x56,0x05,0xD0,
};
constexpr std::array<std::uint8_t,16> kSetItemInHandSlotFingerprint{
    0x28,0x1C,0x00,0x72,0x00,0x01,0x00,0x54,
    0x1F,0x05,0x00,0x71,0x61,0x01,0x00,0x54,
};
constexpr std::array<std::uint8_t,48> kSetSelectedItemFingerprint{
    0xFD,0x7B,0xBB,0xA9,0xFC,0x67,0x01,0xA9,
    0xF8,0x5F,0x02,0xA9,0xF6,0x57,0x03,0xA9,
    0xF4,0x4F,0x04,0xA9,0xFD,0x03,0x00,0x91,
    0xFF,0xC3,0x0E,0xD1,0x56,0xD0,0x3B,0xD5,
    0xF3,0x03,0x01,0xAA,0xF4,0x03,0x00,0xAA,
    0xC8,0x16,0x40,0xF9,0xA8,0x83,0x1F,0xF8,
};
constexpr std::array<std::uint8_t,8> kSetOffhandRawFingerprint{
    0x22,0x00,0x80,0x52,0x59,0xFE,0xFF,0x17,
};
constexpr std::array<std::uint8_t,32> kStackDescriptorFromItemFingerprint{
    0xFF,0xC3,0x01,0xD1,0xFD,0x7B,0x02,0xA9,
    0xFA,0x67,0x03,0xA9,0xF8,0x5F,0x04,0xA9,
    0xF6,0x57,0x05,0xA9,0xF4,0x4F,0x06,0xA9,
    0xFD,0x83,0x00,0x91,0x57,0xD0,0x3B,0xD5,
};
constexpr std::array<std::uint8_t,32> kInventoryActionDtorFingerprint{
    0xFF,0x43,0x01,0xD1,0xFD,0x7B,0x01,0xA9,
    0xF8,0x5F,0x02,0xA9,0xF6,0x57,0x03,0xA9,
    0xF4,0x4F,0x04,0xA9,0xFD,0x43,0x00,0x91,
    0x55,0xD0,0x3B,0xD5,0xF3,0x03,0x00,0xAA,
};
constexpr std::array<std::uint8_t,32> kInventoryTransactionAddActionFingerprint{
    0xFD,0x7B,0xBD,0xA9,0xF5,0x0B,0x00,0xF9,
    0xF4,0x4F,0x02,0xA9,0xFD,0x03,0x00,0x91,
    0xF3,0x03,0x00,0xAA,0x00,0x00,0x40,0xF9,
    0xF4,0x03,0x02,0x2A,0xF5,0x03,0x01,0xAA,
};

constexpr std::array<std::uint8_t,32> kTryBeginClientLegacyTransactionFingerprint{
    0xFD,0x7B,0xBE,0xA9,0xF4,0x4F,0x01,0xA9,
    0xFD,0x03,0x00,0x91,0xF3,0x03,0x08,0xAA,
    0xA0,0x01,0x00,0xB4,0xF4,0x03,0x00,0xAA,
    0xDA,0x81,0xF3,0x97,0x40,0x01,0x00,0x36,
};

struct ModuleState {
    std::uintptr_t base{0};
};

int moduleCallback(dl_phdr_info* info,std::size_t,void* raw) noexcept {
    if(!info || !info->dlpi_name ||
       std::strstr(info->dlpi_name,kMinecraftLibrary)==nullptr) {
        return 0;
    }
    static_cast<ModuleState*>(raw)->base=
        static_cast<std::uintptr_t>(info->dlpi_addr);
    return 1;
}

[[nodiscard]] std::uintptr_t moduleBase() noexcept {
    ModuleState state{};
    dl_iterate_phdr(&moduleCallback,&state);
    return state.base;
}

struct AddressState {
    std::uintptr_t address{0};
    std::uint32_t requiredFlags{0};
    bool found{false};
};

int addressCallback(dl_phdr_info* info,std::size_t,void* raw) noexcept {
    if(!info || !info->dlpi_name ||
       std::strstr(info->dlpi_name,kMinecraftLibrary)==nullptr) {
        return 0;
    }

    auto& state=*static_cast<AddressState*>(raw);
    const auto base=static_cast<std::uintptr_t>(info->dlpi_addr);
    for(std::size_t i=0;i<info->dlpi_phnum;++i) {
        const auto& ph=info->dlpi_phdr[i];
        if(ph.p_type!=PT_LOAD) continue;
        const auto begin=base+static_cast<std::uintptr_t>(ph.p_vaddr);
        const auto end=begin+static_cast<std::uintptr_t>(ph.p_memsz);
        if(state.address>=begin && state.address<end &&
           (static_cast<std::uint32_t>(ph.p_flags)&state.requiredFlags)==state.requiredFlags) {
            state.found=true;
            break;
        }
    }
    return 1;
}

[[nodiscard]] bool mapped(std::uintptr_t address,std::uint32_t flags=0) noexcept {
    if(address==0) return false;
    AddressState state{address,flags,false};
    dl_iterate_phdr(&addressCallback,&state);
    return state.found;
}

template<std::size_t N>
[[nodiscard]] std::uintptr_t resolve(
    std::uintptr_t rva,
    const std::array<std::uint8_t,N>& fp
) noexcept {
    const auto base=moduleBase();
    if(base==0) return 0;
    const auto target=base+rva;
    if(!mapped(target,PF_X)) return 0;
    return std::memcmp(reinterpret_cast<const void*>(target),fp.data(),fp.size())==0
        ? target : 0;
}


[[nodiscard]] std::uintptr_t resolveSetSelectedTarget(
    bool& chainedLiveTarget
) noexcept {
    chainedLiveTarget=false;

    const auto exact=resolve(
        kSetSelectedItemRva,
        kSetSelectedItemFingerprint
    );
    if(exact!=0) {
        return exact;
    }

    // RightUseRouter installs before SwapRuntime and hooks this exact entry.
    // All other swap ABI targets above remain exact-fingerprint validated, so
    // once those guards identify the supported 1.26.51.1 build, a changed
    // prologue here means the known setter is already hook-chained in-process.
    //
    // Calling the live entry is safe for the preFrame swap path because
    // RightUseRouter::setSelectedItemDetour is pass-through unless an explicit
    // OFFHAND consumption/release writeback scope is active.
    const auto base=moduleBase();
    if(base==0) return 0;

    const auto live=base+kSetSelectedItemRva;
    if(!mapped(live,PF_X)) {
        return 0;
    }

    chainedLiveTarget=true;
    return live;
}

template<typename T>
[[nodiscard]] T read(const void* base,std::size_t off,T fallback={}) noexcept {
    if(!base) return fallback;
    T value{};
    std::memcpy(&value,static_cast<const std::byte*>(base)+off,sizeof(value));
    return value;
}

using ContainerGetItemFn=const void* (*)(const void*,int);
using SetOffhandRawFn=void (*)(void*,const void*);
using StackDescriptorFromItemFn=void (*)(void*,const void*);
using InventoryActionDtorFn=void (*)(void*);
using InventoryTransactionAddActionFn=void (*)(void*,const void*,int);
using LegacyActionAllowedFn=bool (*)(void*);

// Native 0x38-byte predictive scope returned by the Player-aware wrapper.
struct NativeClientLegacyScope final {
    alignas(16) std::array<std::byte,kNativeLegacyScopeSize> storage{};
};
using TryBeginClientLegacyTransactionFn=
    NativeClientLegacyScope (*)(void*);
using NativeScopeCallableFn=void (*)(void*);

SetOffhandRawFn gSetOffhandRaw=nullptr;
StackDescriptorFromItemFn gStackDescriptorFromItem=nullptr;
InventoryActionDtorFn gInventoryActionDtor=nullptr;
InventoryTransactionAddActionFn gInventoryTransactionAddAction=nullptr;
TryBeginClientLegacyTransactionFn gTryBeginClientLegacyTransaction=nullptr;

class ClientLegacyPredictiveGuard final {
public:
    ClientLegacyPredictiveGuard(
        NativeClientLegacyScope& scope,
        void* player
    ) noexcept : mScope(scope) {
        if(!player) return;

        mManager=read<void*>(
            player,kPlayerItemStackNetManagerOffset,nullptr
        );
        if(!mManager) return;

        mDuringId=read<int>(
            mManager,kItemStackNetManagerLegacyRequestIdOffset,0
        );

        const auto engaged=read<std::uint8_t>(
            mScope.storage.data(),
            kNativeLegacyScopeEngagedOffset,
            0
        );
        mCallable=read<void*>(
            mScope.storage.data(),
            kNativeLegacyScopeCallableOffset,
            nullptr
        );
        if(engaged==0 || !mCallable ||
           mDuringId>-2 || (mDuringId&1)!=0) {
            return;
        }

        const void* vtable=read<const void*>(mCallable,0,nullptr);
        if(!mapped(reinterpret_cast<std::uintptr_t>(vtable),0)) return;

        mInvoke=read<NativeScopeCallableFn>(
            vtable,kNativeFunctionInvokeVtableOffset,nullptr
        );

        const bool inlineCallable=
            mCallable==static_cast<void*>(mScope.storage.data());
        mDestroy=read<NativeScopeCallableFn>(
            vtable,
            inlineCallable
                ? kNativeFunctionDestroyVtableOffset
                : kNativeFunctionDestroyVtableOffset+8,
            nullptr
        );

        mValid=
            mInvoke && mapped(reinterpret_cast<std::uintptr_t>(mInvoke),PF_X) &&
            mDestroy && mapped(reinterpret_cast<std::uintptr_t>(mDestroy),PF_X);

        __android_log_print(
            mValid?ANDROID_LOG_INFO:ANDROID_LOG_ERROR,
            kLogTag,
            "[SwapEngine][predictive-guard] active=%d reqId=%d valid=%d",
            engaged?1:0,mDuringId,mValid?1:0
        );
    }

    ~ClientLegacyPredictiveGuard() noexcept {
        finish();
    }

    [[nodiscard]] bool valid() const noexcept {
        return mValid;
    }

    [[nodiscard]] bool finish() noexcept {
        if(mFinished) return mClosedCleanly;
        mFinished=true;

        if(mValid && mInvoke && mCallable) {
            mInvoke(mCallable);
        }
        if(mValid && mDestroy && mCallable) {
            mDestroy(mCallable);
        }

        const int afterId=mManager
            ? read<int>(
                mManager,kItemStackNetManagerLegacyRequestIdOffset,0
            )
            : mDuringId;
        mClosedCleanly=mValid && afterId==0;

        __android_log_print(
            mClosedCleanly?ANDROID_LOG_INFO:ANDROID_LOG_ERROR,
            kLogTag,
            "[SwapEngine][predictive-guard] close reqId=%d->%d clean=%d",
            mDuringId,afterId,mClosedCleanly?1:0
        );

        mCallable=nullptr;
        mInvoke=nullptr;
        mDestroy=nullptr;
        return mClosedCleanly;
    }

private:
    NativeClientLegacyScope& mScope;
    void* mManager{nullptr};
    void* mCallable{nullptr};
    NativeScopeCallableFn mInvoke{nullptr};
    NativeScopeCallableFn mDestroy{nullptr};
    int mDuringId{0};
    bool mValid{false};
    bool mFinished{false};
    bool mClosedCleanly{false};
};

[[nodiscard]] bool legacyInventoryTransactionAvailable(void* player) noexcept {
    if(!player) return false;

    auto* txManager=
        static_cast<std::byte*>(player)+kPlayerInventoryTransactionManagerOffset;
    if(read<void*>(txManager,kInventoryTransactionPendingOffset,nullptr)!=nullptr) {
        __android_log_print(
            ANDROID_LOG_WARN,kLogTag,
            "[SwapEngine][legacy-txn] existing pending InventoryTransaction; F swap rejected"
        );
        return false;
    }

    void* netManager=read<void*>(
        player,kPlayerItemStackNetManagerOffset,nullptr
    );
    if(!netManager) return true;

    const void* vtable=read<const void*>(netManager,0,nullptr);
    if(!mapped(reinterpret_cast<std::uintptr_t>(vtable),0)) return false;

    const auto allowed=read<LegacyActionAllowedFn>(
        vtable,kItemStackNetManagerLegacyAllowedVtableOffset,nullptr
    );
    if(!allowed || !mapped(reinterpret_cast<std::uintptr_t>(allowed),PF_X)) {
        return false;
    }

    const bool ok=allowed(netManager);
    if(!ok) {
        __android_log_print(
            ANDROID_LOG_WARN,kLogTag,
            "[SwapEngine][legacy-txn] ItemStackNetManager has active modern request; F swap rejected"
        );
    }
    return ok;
}

class OffhandInventoryAction final {
public:
    OffhandInventoryAction(
        SwapEngine::ItemStackCopyCtorFn copyCtor,
        const void* before,
        const void* after
    ) noexcept {
        if(!copyCtor || !gStackDescriptorFromItem || !gInventoryActionDtor ||
           !before || !after) {
            return;
        }

        mStorage.fill(std::byte{0});
        mStorage[4]=static_cast<std::byte>(kOffhandLegacyContainerId);

        gStackDescriptorFromItem(
            mStorage.data()+kInventoryActionOldDescriptorOffset,before
        );
        gStackDescriptorFromItem(
            mStorage.data()+kInventoryActionNewDescriptorOffset,after
        );
        copyCtor(
            mStorage.data()+kInventoryActionOldStackOffset,before
        );
        copyCtor(
            mStorage.data()+kInventoryActionNewStackOffset,after
        );
        mConstructed=true;
    }

    ~OffhandInventoryAction() noexcept {
        if(mConstructed && gInventoryActionDtor) {
            gInventoryActionDtor(mStorage.data());
        }
    }

    [[nodiscard]] bool valid() const noexcept { return mConstructed; }

    void submit(void* player) const noexcept {
        if(!mConstructed || !player || !gInventoryTransactionAddAction) return;
        auto* manager=
            static_cast<std::byte*>(player)+
            kPlayerInventoryTransactionManagerOffset;
        gInventoryTransactionAddAction(manager,mStorage.data(),0);
    }

private:
    alignas(16) std::array<std::byte,kInventoryActionSize> mStorage{};
    bool mConstructed{false};
};

[[nodiscard]] bool legacyTransactionSettled(const void* player) noexcept {
    if(!player) return false;
    const auto* manager=
        static_cast<const std::byte*>(player)+
        kPlayerInventoryTransactionManagerOffset;
    return read<const void*>(
        manager,kInventoryTransactionPendingOffset,nullptr
    )==nullptr;
}

class Snapshot final {
public:
    Snapshot(
        SwapEngine::ItemStackCopyCtorFn copyCtor,
        SwapEngine::ItemStackDtorFn dtor,
        const void* source
    ) noexcept : mDtor(dtor) {
        if(copyCtor && dtor && source) {
            copyCtor(mStorage.data(),source);
            mConstructed=true;
        }
    }

    ~Snapshot() noexcept {
        if(mConstructed && mDtor) mDtor(mStorage.data());
    }

    [[nodiscard]] const void* get() const noexcept {
        return mConstructed?mStorage.data():nullptr;
    }

private:
    alignas(16) std::array<std::byte,kItemStackStorageSize> mStorage{};
    SwapEngine::ItemStackDtorFn mDtor{nullptr};
    bool mConstructed{false};
};

} // namespace

SwapEngine& SwapEngine::instance() noexcept {
    static SwapEngine engine;
    return engine;
}

bool SwapEngine::install(pl::mod::ModContext& context) noexcept {
    uninstall();

    const auto off=resolve(kOffhandSlotRva,kOffhandSlotFingerprint);
    const auto nul=resolve(kStackIsNullRva,kStackIsNullFingerprint);
    const auto copy=resolve(kItemStackCopyCtorRva,kItemStackCopyCtorFingerprint);
    const auto dtor=resolve(kItemStackDtorRva,kItemStackDtorFingerprint);
    const auto setOff=resolve(kSetItemInHandSlotRva,kSetItemInHandSlotFingerprint);
    const auto setOffRaw=resolve(kSetOffhandRawRva,kSetOffhandRawFingerprint);
    const auto descriptor=resolve(
        kStackDescriptorFromItemRva,kStackDescriptorFromItemFingerprint
    );
    const auto actionDtor=resolve(
        kInventoryActionDtorRva,kInventoryActionDtorFingerprint
    );
    const auto addAction=resolve(
        kInventoryTransactionAddActionRva,
        kInventoryTransactionAddActionFingerprint
    );
    const auto tryLegacy=resolve(
        kTryBeginClientLegacyTransactionRva,
        kTryBeginClientLegacyTransactionFingerprint
    );

    const bool stableBuild=
        off!=0 && nul!=0 && copy!=0 && dtor!=0 && setOff!=0 &&
        setOffRaw!=0 && descriptor!=0 && actionDtor!=0 && addAction!=0 &&
        tryLegacy!=0;

    bool setSelectedChainedLive=false;
    const auto setSel=
        stableBuild
        ? resolveSetSelectedTarget(setSelectedChainedLive)
        : 0;

    const auto base=moduleBase();
    const auto empty=base?base+kEmptyItemRva:0;
    const bool emptyMapped=mapped(empty,0);

    __android_log_print(
        ANDROID_LOG_INFO,kLogTag,
        "[SwapEngine] targets off=%d null=%d copy=%d dtor=%d setOff=%d setOffRaw=%d descriptor=%d actionDtor=%d addAction=%d tryClientLegacy=%d setSelected=%d setSelectedLive=%d emptyMapped=%d",
        off!=0,nul!=0,copy!=0,dtor!=0,setOff!=0,setOffRaw!=0,
        descriptor!=0,actionDtor!=0,addAction!=0,tryLegacy!=0,setSel!=0,
        setSelectedChainedLive?1:0,emptyMapped
    );

    if(!off||!nul||!copy||!dtor||!setOff||!setOffRaw||!descriptor||
       !actionDtor||!addAction||!tryLegacy||!setSel||!emptyMapped) {
        context.logger().error(
            "Swap engine: native storage target validation failed"
        );
        return false;
    }

    mGetOffhandSlot=reinterpret_cast<GetOffhandSlotFn>(off);
    mStackIsNull=reinterpret_cast<StackIsNullFn>(nul);
    mItemStackCopyCtor=reinterpret_cast<ItemStackCopyCtorFn>(copy);
    mItemStackDtor=reinterpret_cast<ItemStackDtorFn>(dtor);
    mSetItemInHandSlot=reinterpret_cast<SetItemInHandSlotFn>(setOff);
    mSetSelectedItem=reinterpret_cast<SetSelectedItemFn>(setSel);
    gSetOffhandRaw=reinterpret_cast<SetOffhandRawFn>(setOffRaw);
    gStackDescriptorFromItem=
        reinterpret_cast<StackDescriptorFromItemFn>(descriptor);
    gInventoryActionDtor=
        reinterpret_cast<InventoryActionDtorFn>(actionDtor);
    gInventoryTransactionAddAction=
        reinterpret_cast<InventoryTransactionAddActionFn>(addAction);
    gTryBeginClientLegacyTransaction=
        reinterpret_cast<TryBeginClientLegacyTransactionFn>(tryLegacy);
    mEmptyItem=reinterpret_cast<const void*>(empty);

    if(setSelectedChainedLive) {
        context.logger().info(
            "[SwapEngine] Player::setSelectedItem pre-hooked; chaining live "
            "1.26.51.1 target RVA=0xF9F7850"
        );
    }

    if(!mStackIsNull(mEmptyItem)) {
        context.logger().error("Swap engine: native EMPTY_ITEM validation failed");
        uninstall();
        return false;
    }

    context.logger().info(
        "Swap engine ready; F exchange uses native client predictive guard + "
        "native selected/OFF hand setters"
    );
    return true;
}

void SwapEngine::uninstall() noexcept {
    mGetOffhandSlot=nullptr;
    mStackIsNull=nullptr;
    mItemStackCopyCtor=nullptr;
    mItemStackDtor=nullptr;
    mSetItemInHandSlot=nullptr;
    mSetSelectedItem=nullptr;
    gSetOffhandRaw=nullptr;
    gStackDescriptorFromItem=nullptr;
    gInventoryActionDtor=nullptr;
    gInventoryTransactionAddAction=nullptr;
    gTryBeginClientLegacyTransaction=nullptr;
    mEmptyItem=nullptr;
}

bool SwapEngine::ready() const noexcept {
    return mGetOffhandSlot && mStackIsNull && mItemStackCopyCtor &&
        mItemStackDtor && mSetItemInHandSlot && mSetSelectedItem &&
        gSetOffhandRaw && gStackDescriptorFromItem && gInventoryActionDtor &&
        gInventoryTransactionAddAction && gTryBeginClientLegacyTransaction &&
        mEmptyItem;
}

const void* SwapEngine::selectedStack(const void* player) const noexcept {
    if(!ready() || !player) return nullptr;

    const void* state=read<const void*>(player,kPlayerSelectedStateOffset,nullptr);
    if(!state) return mEmptyItem;

    const auto flag=read<std::uint8_t>(state,kSelectedStateFlagOffset,0);
    if(flag!=0) return mEmptyItem;

    const void* container=read<const void*>(state,kSelectedStateContainerOffset,nullptr);
    const int index=read<int>(state,kSelectedStateIndexOffset,-1);
    if(!container || index<0) return nullptr;

    const void* vtable=read<const void*>(container,0,nullptr);
    if(!mapped(reinterpret_cast<std::uintptr_t>(vtable),0)) return nullptr;

    const auto getter=read<ContainerGetItemFn>(
        vtable,kContainerGetItemVtableOffset,nullptr
    );
    if(!getter || !mapped(reinterpret_cast<std::uintptr_t>(getter),PF_X)) {
        return nullptr;
    }

    return getter(container,index);
}

bool SwapEngine::swap(void* player,const void* selected) noexcept {
    if(!ready() || !player || !selected) return false;

    const void* off=mGetOffhandSlot(player);
    if(!off) return false;

    const bool mainEmpty=mStackIsNull(selected);
    const bool offEmpty=mStackIsNull(off);

    if(mainEmpty && offEmpty) return true;

    if(!legacyInventoryTransactionAvailable(player) ||
       !gTryBeginClientLegacyTransaction) {
        return false;
    }

    // The old #630/#662 path moved correct ItemStacks but performed the OFF
    // mutation through a synthetic container-119 action + raw writer, then
    // tried to heal ownership after settlement. Manual inventory and the
    // right-click path that unlocks a stuck MAIN slot both perform their hand
    // write while a native negative-even client legacy request is alive.
    //
    // Keep the proven 44a mutation order, but execute the actual OFF write
    // through Actor::setItemInHandSlot inside that same predictive scope.
    // This lets LocalPlayer own container-119 bookkeeping and predictive
    // reconciliation at the moment the destination slot changes.
    NativeClientLegacyScope nativeScope=
        gTryBeginClientLegacyTransaction(player);
    ClientLegacyPredictiveGuard guard(nativeScope,player);
    if(!guard.valid()) {
        __android_log_print(
            ANDROID_LOG_ERROR,kLogTag,
            "[SwapEngine] predictive guard unavailable; swap rejected before mutation"
        );
        return false;
    }

    bool mutated=false;
    const char* direction="?";

    if(offEmpty) {
        Snapshot main(mItemStackCopyCtor,mItemStackDtor,selected);
        if(!main.get() || !mStackIsNull(mEmptyItem)) return false;

        direction="MAIN->OFF";
        mSetSelectedItem(player,mEmptyItem);
        mSetItemInHandSlot(player,kOffHand,main.get());
        mutated=true;
    } else if(mainEmpty) {
        Snapshot offSnap(mItemStackCopyCtor,mItemStackDtor,off);
        if(!offSnap.get() || !mStackIsNull(mEmptyItem)) return false;

        direction="OFF->MAIN";
        mSetItemInHandSlot(player,kOffHand,mEmptyItem);
        mSetSelectedItem(player,offSnap.get());
        mutated=true;
    } else {
        Snapshot main(mItemStackCopyCtor,mItemStackDtor,selected);
        Snapshot offSnap(mItemStackCopyCtor,mItemStackDtor,off);
        if(!main.get() || !offSnap.get() || !mStackIsNull(mEmptyItem)) {
            return false;
        }

        direction="OCCUPIED";
        // Preserve the accepted 44a order exactly.
        mSetSelectedItem(player,mEmptyItem);
        mSetItemInHandSlot(player,kOffHand,main.get());
        mSetSelectedItem(player,offSnap.get());
        mutated=true;
    }

    const bool requestClosed=guard.finish();
    const bool settled=legacyTransactionSettled(player);

    __android_log_print(
        mutated && requestClosed && settled
            ? ANDROID_LOG_INFO
            : ANDROID_LOG_ERROR,
        kLogTag,
        "[SwapEngine][native-predictive-swap] %s mutated=%d requestClosed=%d settled=%d",
        direction,mutated?1:0,requestClosed?1:0,settled?1:0
    );

    return mutated && requestClosed && settled;
}

} // namespace levioffhand::swap
