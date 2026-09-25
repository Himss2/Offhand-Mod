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

constexpr std::uintptr_t kEmptyItemRva=0x134C6780;

constexpr std::size_t kItemStackStorageSize=0x98;
constexpr std::size_t kInventoryActionSize=0x1E0;
constexpr std::size_t kInventoryActionOldDescriptorOffset=0x10;
constexpr std::size_t kInventoryActionNewDescriptorOffset=0x60;
constexpr std::size_t kInventoryActionOldStackOffset=0xB0;
constexpr std::size_t kInventoryActionNewStackOffset=0x148;
constexpr std::size_t kInventoryActionSlotOffset=0x0C;
constexpr std::size_t kPlayerInventoryTransactionManagerOffset=0x9B8;
constexpr std::size_t kInventoryTransactionPendingOffset=0x08;
constexpr std::size_t kPlayerItemStackNetManagerOffset=0xA00;
// Exact ItemStackNetManagerBase screen-stack layout used inline by
// setPlayerContainer @ 0xF88A770..0xF88A7A4.
constexpr std::size_t kItemStackNetManagerScreenStackOffset=0x38;
constexpr std::size_t kScreenStackMapOffset=0x08;
constexpr std::size_t kScreenStackIndexOffset=0x20;
constexpr std::size_t kItemStackNetManagerLegacyAllowedVtableOffset=0x28;
constexpr std::size_t kItemStackNetManagerLegacyRequestIdOffset=0x50;
constexpr std::size_t kNativeLegacyScopeCallableOffset=0x20;
constexpr std::size_t kNativeLegacyScopeEngagedOffset=0x30;
constexpr std::size_t kNativeLegacyScopeSize=0x38;
constexpr std::size_t kNativeFunctionInvokeVtableOffset=0x30;
constexpr std::size_t kNativeFunctionDestroyInlineVtableOffset=0x20;
constexpr std::uint8_t kHotbarLegacyContainerId=0x00;
constexpr std::uint8_t kOffhandLegacyContainerId=0x77;

// SharedTypes::Legacy::ContainerType values.
constexpr int kInventoryContainerType=-1;
constexpr int kHandContainerType=19;
// IMPORTANT: these are different domains.
// Legacy InventoryAction container 119 addresses OFFHAND as slot 0.
// Predictive ContainerType::Hand addresses the native SimplePlayerContainer,
// where slot 0 is MAINHAND and slot 1 is OFFHAND.
constexpr int kOffhandLegacySlot=0;
constexpr int kHandOffhandSlot=1;

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

SetOffhandRawFn gSetOffhandRaw=nullptr;
StackDescriptorFromItemFn gStackDescriptorFromItem=nullptr;
InventoryActionDtorFn gInventoryActionDtor=nullptr;
InventoryTransactionAddActionFn gInventoryTransactionAddAction=nullptr;
TryBeginClientLegacyRequestFn gTryBeginClientLegacyRequest=nullptr;
TryBeginClientLegacyTransactionFn gTryBeginClientLegacyTransaction=nullptr;
RecordLegacySlotFn gRecordLegacySlot=nullptr;

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

    // Exact 1.26.51.1 RE: every return path of 0xF88A434/0xF88D960
    // stores the final-action's own address at scope+0x20. This wrapper never
    // returns a heap-owned callable. A mismatch means the aggregate was
    // relocated/corrupted; fail closed instead of heap-destroying stack memory.
    if(callable!=static_cast<void*>(scope.storage.data())) {
        __android_log_print(
            ANDROID_LOG_ERROR,kLogTag,
            "[SwapEngine][legacy-screen-slots] native scope self-pointer mismatch scope=%p callable=%p",
            scope.storage.data(),callable
        );
        return false;
    }

    const auto invoke=read<NativeScopeCallableFn>(
        vtable,kNativeFunctionInvokeVtableOffset,nullptr
    );
    const auto destroy=read<NativeScopeCallableFn>(
        vtable,kNativeFunctionDestroyInlineVtableOffset,nullptr
    );
    if(
        !invoke ||
        !mapped(reinterpret_cast<std::uintptr_t>(invoke),PF_X) ||
        !destroy ||
        !mapped(reinterpret_cast<std::uintptr_t>(destroy),PF_X)
    ) {
        return false;
    }

    // Native caller pattern:
    //   vcall +0x30 => final_action callback (clears manager+0x50)
    //   vcall +0x20 => inline std::function destroy (no heap delete)
    invoke(callable);
    destroy(callable);

    std::memset(scope.storage.data(),0,scope.storage.size());
    return true;
}

[[nodiscard]] void* currentLegacyRequestScreen(void* manager) noexcept {
    if(!manager) return nullptr;

    // setPlayerContainer @ 0xF88A770..0xF88A7A4:
    //   x8  = [manager+0x38]
    //   idx = [x8+0x20]
    //   map = [x8+0x08]
    //   block = map[((idx >> 6) & 0x3fffffffffffff8)]
    //   screen = block[idx & 0x1ff]
    void* screenStack=read<void*>(
        manager,kItemStackNetManagerScreenStackOffset,nullptr
    );
    if(!screenStack) return nullptr;

    void* map=read<void*>(screenStack,kScreenStackMapOffset,nullptr);
    if(!map) return nullptr;

    const auto index=read<std::uint64_t>(
        screenStack,kScreenStackIndexOffset,0
    );
    const auto blockOffset=
        (index>>6) & std::uint64_t{0x03FFFFFFFFFFFFF8ULL};
    const auto entryOffset=
        (index & std::uint64_t{0x1FF}) * sizeof(void*);

    void* block=read<void*>(map,static_cast<std::size_t>(blockOffset),nullptr);
    if(!block) return nullptr;

    void* screen=read<void*>(
        block,static_cast<std::size_t>(entryOffset),nullptr
    );

    __android_log_print(
        screen?ANDROID_LOG_INFO:ANDROID_LOG_ERROR,
        kLogTag,
        "[SwapEngine][legacy-screen-slots] direct screen=%p index=%llu blockOff=0x%llX entryOff=0x%llX",
        screen,
        static_cast<unsigned long long>(index),
        static_cast<unsigned long long>(blockOffset),
        static_cast<unsigned long long>(entryOffset)
    );
    return screen;
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
    LegacyScreenSlotScope(
        NativeClientLegacyScope& scope,
        void* player
    ) noexcept : mScope(scope) {
        if(!player || !gRecordLegacySlot) {
            (void)finishNativeClientLegacyScope(mScope);
            mFinished=true;
            return;
        }

        mManager=read<void*>(
            player,kPlayerItemStackNetManagerOffset,nullptr
        );
        if(!mManager) {
            (void)finishNativeClientLegacyScope(mScope);
            mFinished=true;
            return;
        }

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
            (void)finishNativeClientLegacyScope(mScope);
            mFinished=true;
            mRequestId=0;
            return;
        }

        const void* callableVtable=read<const void*>(callable,0,nullptr);
        if(
            !callableVtable ||
            !mapped(reinterpret_cast<std::uintptr_t>(callableVtable),0)
        ) {
            finishNativeClientLegacyScope(mScope);
            mRequestId=0;
            return;
        }

        const auto invoke=read<NativeScopeCallableFn>(
            callableVtable,kNativeFunctionInvokeVtableOffset,nullptr
        );
        if(!invoke || !mapped(reinterpret_cast<std::uintptr_t>(invoke),PF_X)) {
            finishNativeClientLegacyScope(mScope);
            mRequestId=0;
            return;
        }

        mScreen=currentLegacyRequestScreen(mManager);
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
    NativeClientLegacyScope& mScope;
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

class LegacyInventoryAction final {
public:
    LegacyInventoryAction(
        SwapEngine::ItemStackCopyCtorFn copyCtor,
        std::uint8_t containerId,
        int slot,
        const void* before,
        const void* after
    ) noexcept {
        if(!copyCtor || !gStackDescriptorFromItem || !gInventoryActionDtor ||
           !before || !after || slot<0) {
            return;
        }

        mStorage.fill(std::byte{0});

        // Exact 1.26.51.1 InventoryAction layout recovered from
        // 0xF9FD618 / caller 0xF8835BC.
        mStorage[4]=static_cast<std::byte>(containerId);
        std::memcpy(
            mStorage.data()+kInventoryActionSlotOffset,
            &slot,
            sizeof(slot)
        );

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

    ~LegacyInventoryAction() noexcept {
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

    // Only the proven #662 targets decide whether the F runtime is usable.
    // Screen-aware predictive bookkeeping is optional until its runtime
    // availability is proven on-device; a missing helper must never hide or
    // disable the F button/runtime again.
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
        "[SwapEngine] targets off=%d null=%d copy=%d dtor=%d setOff=%d setOffRaw=%d descriptor=%d actionDtor=%d addAction=%d tryClientLegacy=%d tryPlayerLegacy=%d recordLegacySlot=%d directScreenStack=1 setSelected=%d setSelectedLive=%d emptyMapped=%d",
        off!=0,nul!=0,copy!=0,dtor!=0,setOff!=0,setOffRaw!=0,
        descriptor!=0,actionDtor!=0,addAction!=0,tryLegacy!=0,
        tryLegacyTransaction!=0,recordLegacySlot!=0,
        setSel!=0,setSelectedChainedLive?1:0,emptyMapped
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
    gTryBeginClientLegacyRequest=
        reinterpret_cast<TryBeginClientLegacyRequestFn>(tryLegacy);
    gTryBeginClientLegacyTransaction=
        reinterpret_cast<TryBeginClientLegacyTransactionFn>(
            tryLegacyTransaction
        );
    gRecordLegacySlot=
        reinterpret_cast<RecordLegacySlotFn>(recordLegacySlot);
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
        "Swap engine ready; F exchange uses RE-balanced legacy actions + "
        "predictive touched-slot bookkeeping while preserving #662 storage writers"
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
    mEmptyItem=nullptr;
}

bool SwapEngine::ready() const noexcept {
    // Keep runtime readiness identical to the working #662 baseline.
    // Optional screen bookkeeping is probed inside swap() instead.
    return mGetOffhandSlot && mStackIsNull && mItemStackCopyCtor &&
        mItemStackDtor && mSetItemInHandSlot && mSetSelectedItem &&
        gSetOffhandRaw && gStackDescriptorFromItem && gInventoryActionDtor &&
        gInventoryTransactionAddAction && gTryBeginClientLegacyRequest &&
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

    // Never join an unrelated inventory transaction or an active modern
    // ItemStackRequest.  The native InventoryTransactionManager can accept
    // legacy InventoryAction records while modern item-stack networking is
    // enabled, but only while no modern request owns the manager.
    if(!legacyInventoryTransactionAvailable(player)) return false;

    const int selectedSlot=selectedHotbarSlot(player);
    if(selectedSlot<0) return false;

    // Exact 1.26.51.1 RE target set. Do not open a predictive request unless
    // every helper needed to close it safely is available. Runtime remains
    // installed, but this F request is rejected before mutation if validation
    // ever stops matching a future binary.
    if(
        !gTryBeginClientLegacyTransaction ||
        !gRecordLegacySlot
    ) {
        __android_log_print(
            ANDROID_LOG_ERROR,kLogTag,
            "[SwapEngine][legacy-screen-slots] exact RE helper unavailable; swap rejected before mutation tryPlayer=%d record=%d",
            gTryBeginClientLegacyTransaction?1:0,
            gRecordLegacySlot?1:0
        );
        return false;
    }

    // CRITICAL ABI RULE:
    // 0xF88A434 / 0xF88D960 writes a self-pointer at scope+0x20 when the
    // std::function callable is stored inline. The aggregate must therefore be
    // constructed directly in its final address and must never be copied or
    // assigned afterward.
    NativeClientLegacyScope nativeScope=
        gTryBeginClientLegacyTransaction(player);
    LegacyScreenSlotScope screenSlots(nativeScope,player);
    if(!screenSlots.valid()) {
        __android_log_print(
            ANDROID_LOG_ERROR,kLogTag,
            "[SwapEngine][legacy-screen-slots] native request/screen invalid; swap rejected before mutation"
        );
        return false;
    }

    void* screen=screenSlots.screen();

    // Exact selected-container RE:
    // - MAIN->EMPTY: setPlayerContainer rejects EMPTY before recording the
    //   slot, so we must record the selected HOTBAR slot ourselves.
    // - MAIN EMPTY->NONEMPTY: selected path 0xF9DA2DC succeeds through
    //   setPlayerContainer @ 0xF88A664 and records HOTBAR itself.
    // OFF always uses the proven raw #662 writer, so OFFHAND is recorded here.
    const bool mainRecorded=
        mainEmpty ||
        screenSlots.recordChangedSlot(
            screen,kInventoryContainerType,selectedSlot
        );
    const bool offRecorded=
        screenSlots.recordChangedSlot(
            screen,kHandContainerType,kHandOffhandSlot
        );
    const bool screenBookkeeping=mainRecorded && offRecorded;

    if(!screenBookkeeping) {
        (void)screenSlots.finish();
        __android_log_print(
            ANDROID_LOG_ERROR,kLogTag,
            "[SwapEngine][legacy-screen-slots] paired registration failed; swap rejected before mutation"
        );
        return false;
    }

    if(offEmpty) {
        Snapshot main(mItemStackCopyCtor,mItemStackDtor,selected);
        if(!main.get() || !mStackIsNull(mEmptyItem)) return false;

        LegacyInventoryAction offAction(
            mItemStackCopyCtor,
            kOffhandLegacyContainerId,
            kOffhandLegacySlot,
            off,
            main.get()
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

        const bool slotsClosed=screenSlots.finish();
        const bool settled=legacyTransactionSettled(player);

        __android_log_print(
            settled && slotsClosed ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
            kLogTag,
            "[SwapEngine][re-balanced] MAIN->OFF settled=%d slotsClosed=%d nativeMainClearFallback=1",
            settled?1:0,slotsClosed?1:0
        );
        return settled && slotsClosed;
    }

    if(mainEmpty) {
        Snapshot offSnap(mItemStackCopyCtor,mItemStackDtor,off);
        if(!offSnap.get() || !mStackIsNull(mEmptyItem)) return false;

        LegacyInventoryAction offAction(
            mItemStackCopyCtor,
            kOffhandLegacyContainerId,
            kOffhandLegacySlot,
            off,
            mEmptyItem
        );
        if(!offAction.valid()) return false;

        // First half: B -> EMPTY in container 119, then the exact low-level
        // OFF writer.  The transaction remains pending until selected storage
        // contributes EMPTY -> B through its normal container-0 path.
        LegacyInventoryAction hotbarFillAction(
            mItemStackCopyCtor,
            kHotbarLegacyContainerId,
            selectedSlot,
            mEmptyItem,
            offSnap.get()
        );
        if(!hotbarFillAction.valid()) return false;

        offAction.submit(player);
        gSetOffhandRaw(player,mEmptyItem);

        // RE: when MAIN destination is NONEMPTY and the Player-aware legacy
        // request is active, selected-container path 0xF9DA2DC succeeds via
        // setPlayerContainer @ 0xF88A664 and skips legacy fallback 0xF8834D4.
        // Supply exactly that missing HOTBAR EMPTY->B action ourselves.
        hotbarFillAction.submit(player);
        mSetSelectedItem(player,offSnap.get());

        const bool slotsClosed=screenSlots.finish();
        const bool settled=legacyTransactionSettled(player);

        __android_log_print(
            settled && slotsClosed ? ANDROID_LOG_INFO : ANDROID_LOG_ERROR,
            kLogTag,
            "[SwapEngine][re-balanced] OFF->MAIN settled=%d slotsClosed=%d explicitHotbarFill=1",
            settled?1:0,slotsClosed?1:0
        );
        return settled && slotsClosed;
    }

    Snapshot main(mItemStackCopyCtor,mItemStackDtor,selected);
    Snapshot offSnap(mItemStackCopyCtor,mItemStackDtor,off);
    if(!main.get() || !offSnap.get() || !mStackIsNull(mEmptyItem)) {
        return false;
    }

    LegacyInventoryAction offAction(
        mItemStackCopyCtor,
        kOffhandLegacyContainerId,
        kOffhandLegacySlot,
        off,
        main.get()
    );
    if(!offAction.valid()) return false;

    LegacyInventoryAction hotbarFillAction(
        mItemStackCopyCtor,
        kHotbarLegacyContainerId,
        selectedSlot,
        mEmptyItem,
        offSnap.get()
    );
    if(!hotbarFillAction.valid()) return false;

    // Preserve the accepted 44a local mutation order:
    //   MAIN A->EMPTY, OFF B->A, MAIN EMPTY->B.
    // RE detail:
    // - A->EMPTY makes native setPlayerContainer return false, so selected
    //   storage itself contributes the HOTBAR A->EMPTY legacy fallback action.
    // - EMPTY->B succeeds through setPlayerContainer and skips that fallback,
    //   therefore the final HOTBAR legacy action is submitted explicitly.
    mSetSelectedItem(player,mEmptyItem);
    offAction.submit(player);
    gSetOffhandRaw(player,main.get());
    hotbarFillAction.submit(player);
    mSetSelectedItem(player,offSnap.get());

    const bool slotsClosed=screenSlots.finish();
    const bool settled=legacyTransactionSettled(player);
    __android_log_print(
        settled && slotsClosed?ANDROID_LOG_INFO:ANDROID_LOG_ERROR,kLogTag,
        "[SwapEngine][re-balanced] OCCUPIED settled=%d slotsClosed=%d explicitHotbarFill=1",
        settled?1:0,slotsClosed?1:0
    );
    return settled && slotsClosed;
}

} // namespace levioffhand::swap
