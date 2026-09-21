#include "swap/SwapEngine.hpp"
#include "runtime/ActionHandContext.hpp"

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
constexpr std::uintptr_t kSetInventorySlotRva=0xF9DA128;
constexpr std::uintptr_t kStacksEqualRva=0xFFA5B3C;
constexpr std::uintptr_t kIsUsingItemRva=0xF9E8D64;
constexpr std::uintptr_t kStopUsingItemRva=0xF9E86C0;
constexpr std::uintptr_t kEmptyItemRva=0x134C6780;

constexpr std::size_t kItemStackStorageSize=0x98;

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
constexpr std::array<std::uint8_t,16> kSetInventorySlotFingerprint{
    0x08,0x00,0x40,0xF9,0xE3,0x03,0x1F,0x2A,0x04,0x39,0x40,0xF9,0x80,0x00,0x1F,0xD6
};
constexpr std::array<std::uint8_t,16> kStacksEqualFingerprint{
    0x08,0x88,0x40,0x39,0x29,0x88,0x40,0x39,0x1F,0x01,0x09,0x6B,0x41,0x00,0x00,0x54
};
constexpr std::array<std::uint8_t,16> kIsUsingItemFingerprint{
    0xFD,0x7B,0xBF,0xA9,0xFD,0x03,0x00,0x91,0x00,0x60,0x1B,0x91,0x80,0xE0,0x16,0x94
};
constexpr std::array<std::uint8_t,16> kStopUsingItemFingerprint{
    0xFD,0x7B,0xBB,0xA9,0xFC,0x0B,0x00,0xF9,0xF8,0x5F,0x02,0xA9,0xF6,0x57,0x03,0xA9
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
    if(!mapped(target,PF_R|PF_X) || !mapped(target+N-1,PF_R|PF_X)) return 0;
    return std::memcmp(reinterpret_cast<const void*>(target),fp.data(),fp.size())==0
        ? target : 0;
}

template<typename T>
[[nodiscard]] T read(const void* base,std::size_t off,T fallback={}) noexcept {
    if(!base) return fallback;
    T value{};
    std::memcpy(&value,static_cast<const std::byte*>(base)+off,sizeof(value));
    return value;
}

using ContainerGetItemFn=const void* (*)(const void*,int);

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
    const auto setSlot=resolve(kSetInventorySlotRva,kSetInventorySlotFingerprint);
    const auto equal=resolve(kStacksEqualRva,kStacksEqualFingerprint);
    const auto usingItem=resolve(kIsUsingItemRva,kIsUsingItemFingerprint);
    const auto stop=resolve(kStopUsingItemRva,kStopUsingItemFingerprint);

    const auto base=moduleBase();
    const auto empty=base?base+kEmptyItemRva:0;
    const bool emptyMapped=mapped(empty,0);

    __android_log_print(
        ANDROID_LOG_INFO,kLogTag,
        "[SwapEngine] targets off=%d null=%d copy=%d dtor=%d setOff=%d inventorySetter=%d equal=%d using=%d stop=%d emptyMapped=%d",
        off!=0,nul!=0,copy!=0,dtor!=0,setOff!=0,setSlot!=0,equal!=0,usingItem!=0,stop!=0,emptyMapped
    );

    if(!off||!nul||!copy||!dtor||!setOff||!setSlot||!equal||!usingItem||!stop||!emptyMapped) {
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
    mSetInventorySlot=reinterpret_cast<SetInventorySlotFn>(setSlot);
    mStacksEqual=reinterpret_cast<StacksEqualFn>(equal);
    mIsUsingItem=reinterpret_cast<IsUsingItemFn>(usingItem);
    mStopUsingItem=reinterpret_cast<StopUsingItemFn>(stop);
    mEmptyItem=reinterpret_cast<const void*>(empty);

    if(!mStackIsNull(mEmptyItem)) {
        context.logger().error("Swap engine: native EMPTY_ITEM validation failed");
        uninstall();
        return false;
    }

    context.logger().info(
        "Swap engine ready: native inventory slot exchange, detached sources, local verification"
    );
    return true;
}

void SwapEngine::uninstall() noexcept {
    mGetOffhandSlot=nullptr;
    mStackIsNull=nullptr;
    mItemStackCopyCtor=nullptr;
    mItemStackDtor=nullptr;
    mSetItemInHandSlot=nullptr;
    mSetInventorySlot=nullptr;
    mStacksEqual=nullptr;
    mIsUsingItem=nullptr;
    mStopUsingItem=nullptr;
    mEmptyItem=nullptr;
}

bool SwapEngine::ready() const noexcept {
    return mGetOffhandSlot && mStackIsNull && mItemStackCopyCtor &&
        mItemStackDtor && mSetItemInHandSlot && mSetInventorySlot &&
        mStacksEqual && mIsUsingItem && mStopUsingItem && mEmptyItem;
}

const void* SwapEngine::selectedStack(const void* player) const noexcept {
    if(!ready() || !player) return nullptr;

    const void* state=read<const void*>(player,kPlayerSelectedStateOffset,nullptr);
    if(!state) return nullptr;

    const auto flag=read<std::uint8_t>(state,kSelectedStateFlagOffset,0);
    if(flag!=0) return nullptr;

    const void* container=read<const void*>(state,kSelectedStateContainerOffset,nullptr);
    const int index=read<int>(state,kSelectedStateIndexOffset,-1);
    if(!container || index<0 || index>8) return nullptr;

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
    // A queued request must still refer to the physical hotbar slot. The
    // selected-view flag is not an empty inventory slot and cannot be written.
    if(selectedStack(player)!=selected) return false;

    // Native inventory notifications can call the hooked selected getter.
    // Give them an explicit MAIN scope, restored automatically on return.
    const runtime::ScopedActionHand storageScope(
        runtime::ActionHand::MainHand,runtime::ActionKind::UseAir);
    if(mIsUsingItem(player)) {
        // Cancel; do not release/fire/consume the item being moved.
        mStopUsingItem(player);
        if(mIsUsingItem(player)) return false;
    }
    selected=selectedStack(player); // stop-use can run native callbacks
    if(!selected) return false;
    const void* state=read<const void*>(player,kPlayerSelectedStateOffset,nullptr);
    void* container=read<void*>(state,kSelectedStateContainerOffset,nullptr);
    const int index=read<int>(state,kSelectedStateIndexOffset,-1);
    const void* off=mGetOffhandSlot(player);
    if(!container || index<0 || index>8 || !off) return false;

    // Native InventoryTransactionManager lives at Player+0x9B8; +8 is its
    // pending legacy transaction. Diagnostics only: never flush or edit it.
    const bool pendingBefore=read<const void*>(player,0x9C0,nullptr)!=nullptr;
    const bool mainEmpty=mStackIsNull(selected);
    const bool offEmpty=mStackIsNull(off);
    // LocalPlayer's offhand setter also skips content-equal stacks. Treat the
    // whole exchange as a no-op so MAIN cannot take OFF's network identity
    // while OFF retains it. Equality includes count and native item metadata.
    if((mainEmpty && offEmpty) || mStacksEqual(selected,off)) return true;

    // Always detach BOTH sources, including empty stacks. Neither setter may
    // retain a reference into a slot changed by the other setter/callback.
    Snapshot main(mItemStackCopyCtor,mItemStackDtor,selected);
    Snapshot offSnap(mItemStackCopyCtor,mItemStackDtor,off);
    if(!main.get() || !offSnap.get()) return false;

    // Inventory::setItem dispatches the native slot setter, which records MAIN
    // before/after and sends its normal notifications. Player::setSelectedItem
    // additionally emits a gameplay event using the hooked selected view.
    // Do not introduce a temporary EMPTY transition for occupied exchanges.
    mSetInventorySlot(container,index,offSnap.get());
    const void* writtenMain=selectedStack(player);
    const void* unchangedOff=mGetOffhandSlot(player);
    if(!writtenMain || !mStacksEqual(writtenMain,offSnap.get()) ||
       !unchangedOff || !mStacksEqual(unchangedOff,offSnap.get())) {
        __android_log_print(ANDROID_LOG_ERROR,kLogTag,
            "[SwapEngine] MAIN write rejected or native callback changed OFF; OFF not overwritten");
        return false;
    }
    mSetItemInHandSlot(player,kOffHand,main.get());

    const void* actualMain=selectedStack(player);
    const void* actualOff=mGetOffhandSlot(player);
    const bool matches=actualMain && actualOff &&
        mStacksEqual(actualMain,offSnap.get()) && mStacksEqual(actualOff,main.get());
    __android_log_print(ANDROID_LOG_INFO,kLogTag,
        "[SwapEngine] slot=%d localVerified=%d legacyPendingBefore=%d legacyPendingAfter=%d",
        index,matches,pendingBefore,read<const void*>(player,0x9C0,nullptr)!=nullptr);
    if(!matches) {
        __android_log_print(ANDROID_LOG_ERROR,kLogTag,
            "[SwapEngine] local slot verification failed after exchange; no blind retry");
    }
    return matches;
}

} // namespace levioffhand::swap
