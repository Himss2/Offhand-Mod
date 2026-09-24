#include "swap/SwapEngine.hpp"

#include <android/log.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <elf.h>
#include <link.h>

#include <pl/memory/Signature.hpp>

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
// ItemStackNetManagerClient::tryBeginClientLegacyTransactionRequest().
// RE 1.26.51.1: writes generated negative legacy request id to manager+0x50
// and returns an engaged optional std::function cleanup scope.
constexpr std::uintptr_t kTryBeginClientLegacyRequestRva=0xF88D960;

// Player-aware wrapper used by native gameplay to open the client legacy
// predictive lifecycle before legacy player-container mutations.
constexpr std::uintptr_t kTryBeginClientLegacyTransactionRva=0xF88A434;

// ItemStackNetManagerClient::_addLegacyTransactionRequestSetItemSlot.
// The second argument is ItemStackNetManagerScreen&, NOT Player*.
constexpr std::uintptr_t kRecordLegacySlotRva=0xF88CE8C;

// Resolve the manager's real active screen through the native method instead
// of decoding libc++ deque internals or casting ClientScreenData.
constexpr char kGetTopScreenSymbol[]=
    "_ZN23ItemStackNetManagerBase13_getTopScreenEv";

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

// SharedTypes::Legacy::ContainerType values.
constexpr int kInventoryContainerType=-1;
constexpr int kHandContainerType=19;
constexpr int kOffhandLocalSlot=0;

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

constexpr std::array<std::uint8_t,32> kTryBeginClientLegacyRequestFingerprint{
    0xFD,0x7B,0xBC,0xA9,0xF8,0x5F,0x01,0xA9,
    0xF6,0x57,0x02,0xA9,0xF4,0x4F,0x03,0xA9,
    0xFD,0x03,0x00,0x91,0xF4,0x03,0x00,0xAA,
    0xF3,0x03,0x08,0xAA,0x2A,0xF4,0xFF,0x97,
};

constexpr std::array<std::uint8_t,32> kTryBeginClientLegacyTransactionFingerprint{
    0xFD,0x7B,0xBE,0xA9,0xF4,0x4F,0x01,0xA9,
    0xFD,0x03,0x00,0x91,0xF3,0x03,0x08,0xAA,
    0xA0,0x01,0x00,0xB4,0xF4,0x03,0x00,0xAA,
    0xDA,0x81,0xF3,0x97,0x40,0x01,0x00,0x36,
};
constexpr std::array<std::uint8_t,32> kRecordLegacySlotFingerprint{
    0xFF,0xC3,0x00,0xD1,0xFD,0x7B,0x01,0xA9,
    0xF4,0x4F,0x02,0xA9,0xFD,0x43,0x00,0x91,
    0x53,0xD0,0x3B,0xD5,0x49,0x1C,0x00,0x12,
    0xE8,0x03,0x01,0xAA,0x6A,0x16,0x40,0xF9,
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

// Opaque 0x38-byte return object. On AArch64 an aggregate this size is
// returned indirectly through x8, exactly matching the native function ABI.
// We intentionally keep it trivial and invoke the native cleanup callable
// ourselves instead of depending on the game's libc++ std::function layout
// at compile time.
struct NativeClientLegacyScope final {
    alignas(16) std::array<std::byte,kNativeLegacyScopeSize> storage{};
};
using TryBeginClientLegacyRequestFn=
    NativeClientLegacyScope (*)(void*);
using TryBeginClientLegacyTransactionFn=
    NativeClientLegacyScope (*)(void*);
using NativeScopeCallableFn=void (*)(void*);
using RecordLegacySlotFn=void (*)(void*,void*,int,int);
using GetTopScreenFn=void* (*)(void*);

SetOffhandRawFn gSetOffhandRaw=nullptr;
StackDescriptorFromItemFn gStackDescriptorFromItem=nullptr;
InventoryActionDtorFn gInventoryActionDtor=nullptr;
InventoryTransactionAddActionFn gInventoryTransactionAddAction=nullptr;
TryBeginClientLegacyRequestFn gTryBeginClientLegacyRequest=nullptr;
TryBeginClientLegacyTransactionFn gTryBeginClientLegacyTransaction=nullptr;
RecordLegacySlotFn gRecordLegacySlot=nullptr;
GetTopScreenFn gGetTopScreen=nullptr;

[[nodiscard]] bool finishNativeClientLegacyScope(
    NativeClientLegacyScope& scope
) noexcept {
    const auto engaged=read<std::uint8_t>(
        scope.storage.data(),
        kNativeLegacyScopeEngagedOffset,
        0
    );
    void* callable=read<void*>(
        scope.storage.data(),
        kNativeLegacyScopeCallableOffset,
        nullptr
    );
    if(engaged==0 || !callable) return false;

    const void* vtable=read<const void*>(callable,0,nullptr);
    if(!mapped(reinterpret_cast<std::uintptr_t>(vtable),0)) return false;

    const auto invoke=read<NativeScopeCallableFn>(
        vtable,kNativeFunctionInvokeVtableOffset,nullptr
    );
    const auto destroy=read<NativeScopeCallableFn>(
        vtable,kNativeFunctionDestroyVtableOffset,nullptr
    );
    if(!invoke || !mapped(reinterpret_cast<std::uintptr_t>(invoke),PF_X)) {
        return false;
    }

    // Native lambda operator() ends the legacy request and clears manager+0x50.
    invoke(callable);

    // Both 1.26.51.1 inline lambdas have trivial inline destructors, but call
    // the native destroy slot when available to preserve std::function rules.
    if(destroy && mapped(reinterpret_cast<std::uintptr_t>(destroy),PF_X)) {
        destroy(callable);
    }

    std::memset(scope.storage.data(),0,scope.storage.size());
    return true;
}

[[nodiscard]] bool normalizeDestinationHandWithNativeLegacyRequest(
    void* player,
    unsigned char hand,
    const void* stack,
    SwapEngine::SetItemInHandSlotFn setHand
) noexcept {
    if(!player || !stack || !setHand || !gTryBeginClientLegacyRequest) {
        return false;
    }

    void* manager=read<void*>(
        player,kPlayerItemStackNetManagerOffset,nullptr
    );
    if(!manager) return false;

    const int beforeId=read<int>(
        manager,kItemStackNetManagerLegacyRequestIdOffset,0
    );
    if(beforeId!=0) {
        __android_log_print(
            ANDROID_LOG_WARN,kLogTag,
            "[SwapEngine][native-client-legacy] busy before normalize hand=%u req=%d",
            static_cast<unsigned>(hand),beforeId
        );
        return false;
    }

    // Direct initialization is intentional: the native function writes an
    // inline std::function whose self pointer targets this exact return object.
    NativeClientLegacyScope scope=
        gTryBeginClientLegacyRequest(manager);

    const int duringId=read<int>(
        manager,kItemStackNetManagerLegacyRequestIdOffset,0
    );

    const auto engaged=read<std::uint8_t>(
        scope.storage.data(),
        kNativeLegacyScopeEngagedOffset,
        0
    );
    void* callable=read<void*>(
        scope.storage.data(),
        kNativeLegacyScopeCallableOffset,
        nullptr
    );

    const bool ownsRequest=
        engaged!=0 && callable!=nullptr && duringId<0;

    if(ownsRequest) {
        setHand(player,hand,stack);
    }

    const bool cleanupOk=finishNativeClientLegacyScope(scope);
    const int afterId=read<int>(
        manager,kItemStackNetManagerLegacyRequestIdOffset,0
    );

    __android_log_print(
        ownsRequest && cleanupOk && afterId==0
            ? ANDROID_LOG_INFO
            : ANDROID_LOG_ERROR,
        kLogTag,
        "[SwapEngine][native-client-legacy] hand=%u owned=%d cleanup=%d req=%d->%d->%d",
        static_cast<unsigned>(hand),
        ownsRequest?1:0,
        cleanupOk?1:0,
        beforeId,duringId,afterId
    );

    return ownsRequest && cleanupOk && afterId==0;
}

[[nodiscard]] int selectedHotbarSlot(const void* player) noexcept {
    if(!player) return -1;
    const void* state=read<const void*>(
        player,kPlayerSelectedStateOffset,nullptr
    );
    if(!state) return -1;
    if(read<std::uint8_t>(state,kSelectedStateFlagOffset,0xFF)!=0) return -1;
    const int slot=read<int>(state,kSelectedStateIndexOffset,-1);
    return slot>=0 && slot<=8 ? slot : -1;
}

class LegacyScreenSlotScope final {
public:
    explicit LegacyScreenSlotScope(void* player) noexcept {
        if(!player || !gTryBeginClientLegacyTransaction ||
           !gRecordLegacySlot || !gGetTopScreen) {
            return;
        }

        mManager=read<void*>(
            player,kPlayerItemStackNetManagerOffset,nullptr
        );
        if(!mManager) return;

        const int beforeId=read<int>(
            mManager,kItemStackNetManagerLegacyRequestIdOffset,0
        );
        if(beforeId!=0) {
            __android_log_print(
                ANDROID_LOG_WARN,kLogTag,
                "[SwapEngine][legacy-screen-slots] busy before open req=%d",
                beforeId
            );
            return;
        }

        mScope=gTryBeginClientLegacyTransaction(player);
        mRequestId=read<int>(
            mManager,kItemStackNetManagerLegacyRequestIdOffset,0
        );

        const auto engaged=read<std::uint8_t>(
            mScope.storage.data(),kNativeLegacyScopeEngagedOffset,0
        );
        void* callable=read<void*>(
            mScope.storage.data(),kNativeLegacyScopeCallableOffset,nullptr
        );
        if(engaged==0 || !callable ||
           mRequestId>-2 || (mRequestId&1)!=0) {
            finishNativeClientLegacyScope(mScope);
            mRequestId=0;
            return;
        }

        const void* callableVtable=read<const void*>(callable,0,nullptr);
        const auto invoke=read<NativeScopeCallableFn>(
            callableVtable,kNativeFunctionInvokeVtableOffset,nullptr
        );
        if(!callableVtable ||
           !mapped(reinterpret_cast<std::uintptr_t>(callableVtable),0) ||
           !invoke || !mapped(reinterpret_cast<std::uintptr_t>(invoke),PF_X)) {
            finishNativeClientLegacyScope(mScope);
            mRequestId=0;
            return;
        }

        mScreen=gGetTopScreen(mManager);
        if(!mScreen) {
            finishNativeClientLegacyScope(mScope);
            mRequestId=0;
            return;
        }
        const void* screenVtable=read<const void*>(mScreen,0,nullptr);
        if(!screenVtable ||
           !mapped(reinterpret_cast<std::uintptr_t>(screenVtable),0)) {
            mScreen=nullptr;
            finishNativeClientLegacyScope(mScope);
            mRequestId=0;
            return;
        }

        mValid=true;
        __android_log_print(
            ANDROID_LOG_INFO,kLogTag,
            "[SwapEngine][legacy-screen-slots] open req=%d screen=%p",
            mRequestId,mScreen
        );
    }

    ~LegacyScreenSlotScope() noexcept {
        finish();
    }

    [[nodiscard]] bool valid() const noexcept {
        return mValid && mManager && mScreen && mRequestId<=-2;
    }

    [[nodiscard]] void* screen() const noexcept {
        return valid()?mScreen:nullptr;
    }

    [[nodiscard]] bool recordChangedSlot(
        void* screen,
        int containerType,
        int slot
    ) const noexcept {
        if(!valid() || screen!=mScreen || slot<0 || !gRecordLegacySlot) {
            return false;
        }
        const int activeId=read<int>(
            mManager,kItemStackNetManagerLegacyRequestIdOffset,0
        );
        if(activeId!=mRequestId) return false;

        gRecordLegacySlot(
            mManager,screen,containerType,slot
        );
        __android_log_print(
            ANDROID_LOG_INFO,kLogTag,
            "[SwapEngine][legacy-screen-slots] req=%d type=%d slot=%d",
            mRequestId,containerType,slot
        );
        return true;
    }

    [[nodiscard]] bool finish() noexcept {
        if(mFinished) return mClosed;
        mFinished=true;
        if(!mValid) return false;

        const bool cleanupOk=finishNativeClientLegacyScope(mScope);
        const int afterId=read<int>(
            mManager,kItemStackNetManagerLegacyRequestIdOffset,0
        );
        mClosed=cleanupOk && afterId==0;

        __android_log_print(
            mClosed?ANDROID_LOG_INFO:ANDROID_LOG_ERROR,kLogTag,
            "[SwapEngine][legacy-screen-slots] close req=%d->%d cleanup=%d",
            mRequestId,afterId,cleanupOk?1:0
        );
        mValid=false;
        return mClosed;
    }

private:
    NativeClientLegacyScope mScope{};
    void* mManager{nullptr};
    void* mScreen{nullptr};
    int mRequestId{0};
    bool mValid{false};
    bool mFinished{false};
    bool mClosed{false};
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
        kTryBeginClientLegacyRequestRva,
        kTryBeginClientLegacyRequestFingerprint
    );
    const auto tryLegacyTransaction=resolve(
        kTryBeginClientLegacyTransactionRva,
        kTryBeginClientLegacyTransactionFingerprint
    );
    const auto recordLegacySlot=resolve(
        kRecordLegacySlotRva,kRecordLegacySlotFingerprint
    );
    const auto getTopScreen=pl::memory::resolveSignature(
        kGetTopScreenSymbol,kMinecraftLibrary
    );
    const bool getTopScreenValid=
        getTopScreen!=0 && mapped(getTopScreen,PF_X);

    const bool stableBuild=
        off!=0 && nul!=0 && copy!=0 && dtor!=0 && setOff!=0 &&
        setOffRaw!=0 && descriptor!=0 && actionDtor!=0 && addAction!=0 &&
        tryLegacy!=0 && tryLegacyTransaction!=0 && recordLegacySlot!=0 &&
        getTopScreenValid;

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
        "[SwapEngine] targets off=%d null=%d copy=%d dtor=%d setOff=%d setOffRaw=%d descriptor=%d actionDtor=%d addAction=%d tryClientLegacy=%d tryPlayerLegacy=%d recordLegacySlot=%d topScreen=%d setSelected=%d setSelectedLive=%d emptyMapped=%d",
        off!=0,nul!=0,copy!=0,dtor!=0,setOff!=0,setOffRaw!=0,
        descriptor!=0,actionDtor!=0,addAction!=0,tryLegacy!=0,
        tryLegacyTransaction!=0,recordLegacySlot!=0,getTopScreenValid?1:0,
        setSel!=0,setSelectedChainedLive?1:0,emptyMapped
    );

    if(!off||!nul||!copy||!dtor||!setOff||!setOffRaw||!descriptor||
       !actionDtor||!addAction||!tryLegacy||!tryLegacyTransaction||
       !recordLegacySlot||!getTopScreenValid||!setSel||!emptyMapped) {
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
    gTryBeginClientLegacyRequest=
        reinterpret_cast<TryBeginClientLegacyRequestFn>(tryLegacy);
    gTryBeginClientLegacyTransaction=
        reinterpret_cast<TryBeginClientLegacyTransactionFn>(
            tryLegacyTransaction
        );
    gRecordLegacySlot=
        reinterpret_cast<RecordLegacySlotFn>(recordLegacySlot);
    gGetTopScreen=
        reinterpret_cast<GetTopScreenFn>(getTopScreen);
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
        "Swap engine ready; F exchange uses paired legacy InventoryAction "
        "hotbar(0) + offhand(119), with native selected setter and raw OFF write"
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
    gTryBeginClientLegacyRequest=nullptr;
    gTryBeginClientLegacyTransaction=nullptr;
    gRecordLegacySlot=nullptr;
    gGetTopScreen=nullptr;
    mEmptyItem=nullptr;
}

bool SwapEngine::ready() const noexcept {
    return mGetOffhandSlot && mStackIsNull && mItemStackCopyCtor &&
        mItemStackDtor && mSetItemInHandSlot && mSetSelectedItem &&
        gSetOffhandRaw && gStackDescriptorFromItem && gInventoryActionDtor &&
        gInventoryTransactionAddAction && gTryBeginClientLegacyRequest &&
        gTryBeginClientLegacyTransaction && gRecordLegacySlot &&
        gGetTopScreen && mEmptyItem;
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

    // Never join an unrelated inventory transaction or an active modern
    // ItemStackRequest.  The native InventoryTransactionManager can accept
    // legacy InventoryAction records while modern item-stack networking is
    // enabled, but only while no modern request owns the manager.
    if(!legacyInventoryTransactionAvailable(player)) return false;

    const int selectedSlot=selectedHotbarSlot(player);
    if(selectedSlot<0) return false;

    LegacyScreenSlotScope screenSlots(player);
    if(!screenSlots.valid()) {
        __android_log_print(
            ANDROID_LOG_ERROR,kLogTag,
            "[SwapEngine][legacy-screen-slots] no native screen/request; F swap rejected before mutation"
        );
        return false;
    }
    void* screen=screenSlots.screen();
    if(
        !screenSlots.recordChangedSlot(
            screen,kInventoryContainerType,selectedSlot
        ) ||
        !screenSlots.recordChangedSlot(
            screen,kHandContainerType,kOffhandLocalSlot
        )
    ) {
        __android_log_print(
            ANDROID_LOG_ERROR,kLogTag,
            "[SwapEngine][legacy-screen-slots] paired slot registration failed before mutation"
        );
        return false;
    }

    if(offEmpty) {
        Snapshot main(mItemStackCopyCtor,mItemStackDtor,selected);
        if(!main.get() || !mStackIsNull(mEmptyItem)) return false;

        OffhandInventoryAction offAction(
            mItemStackCopyCtor,off,main.get()
        );
        if(!offAction.valid()) return false;

        // Selected container +0x68 is transaction-aware on LocalPlayer:
        // A -> EMPTY records legacy container 0 and performs the local clear.
        mSetSelectedItem(player,mEmptyItem);

        // LocalPlayer's public OFF setter suppresses container-119 actions when
        // modern ItemStackNetManager mode is active. Supply exactly that
        // missing action, then call the same raw OFF writer used by vanilla.
        offAction.submit(player);
        gSetOffhandRaw(player,main.get());

        const bool settled=legacyTransactionSettled(player);
        const bool slotsClosed=screenSlots.finish();
        const bool normalized=
            settled && slotsClosed &&
            normalizeDestinationHandWithNativeLegacyRequest(
                player,kOffHand,main.get(),mSetItemInHandSlot
            );

        __android_log_print(
            settled && normalized ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
            kLogTag,
            "[SwapEngine][native-client-normalize] MAIN->OFF settled=%d slotsClosed=%d normalized=%d hand=1",
            settled?1:0,slotsClosed?1:0,normalized?1:0
        );
        return settled && slotsClosed;
    }

    if(mainEmpty) {
        Snapshot offSnap(mItemStackCopyCtor,mItemStackDtor,off);
        if(!offSnap.get() || !mStackIsNull(mEmptyItem)) return false;

        OffhandInventoryAction offAction(
            mItemStackCopyCtor,off,mEmptyItem
        );
        if(!offAction.valid()) return false;

        // First half: B -> EMPTY in container 119, then the exact low-level
        // OFF writer.  The transaction remains pending until selected storage
        // contributes EMPTY -> B through its normal container-0 path.
        offAction.submit(player);
        gSetOffhandRaw(player,mEmptyItem);
        mSetSelectedItem(player,offSnap.get());

        const bool settled=legacyTransactionSettled(player);
        const bool slotsClosed=screenSlots.finish();
        const bool normalized=
            settled && slotsClosed &&
            normalizeDestinationHandWithNativeLegacyRequest(
                player,0,offSnap.get(),mSetItemInHandSlot
            );

        __android_log_print(
            settled && normalized ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
            kLogTag,
            "[SwapEngine][native-client-normalize] OFF->MAIN settled=%d slotsClosed=%d normalized=%d hand=0",
            settled?1:0,slotsClosed?1:0,normalized?1:0
        );
        return settled && slotsClosed;
    }

    Snapshot main(mItemStackCopyCtor,mItemStackDtor,selected);
    Snapshot offSnap(mItemStackCopyCtor,mItemStackDtor,off);
    if(!main.get() || !offSnap.get() || !mStackIsNull(mEmptyItem)) {
        return false;
    }

    OffhandInventoryAction offAction(
        mItemStackCopyCtor,off,main.get()
    );
    if(!offAction.valid()) return false;

    // Preserve the accepted 44a local mutation order while making the
    // InventoryTransaction balanced:
    //   hotbar A->EMPTY, OFF B->A, hotbar EMPTY->B.
    // InventoryTransactionManager retains the first two unbalanced records
    // and sends only after the third record balances the transaction.
    mSetSelectedItem(player,mEmptyItem);
    offAction.submit(player);
    gSetOffhandRaw(player,main.get());
    mSetSelectedItem(player,offSnap.get());

    const bool settled=legacyTransactionSettled(player);
    const bool slotsClosed=screenSlots.finish();
    __android_log_print(
        settled && slotsClosed?ANDROID_LOG_INFO:ANDROID_LOG_ERROR,kLogTag,
        "[SwapEngine][legacy-txn] occupied settled=%d slotsClosed=%d hotbar=0 offhand=119",
        settled?1:0,slotsClosed?1:0
    );
    return settled && slotsClosed;
}

} // namespace levioffhand::swap
