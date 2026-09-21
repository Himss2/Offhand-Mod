// Exercise production exchange/slot reader; substitute only Android/native ABI.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <string>
#include <sys/mman.h>
#include <link.h>
#include <pl/Mod.hpp>
#include "runtime/ActionHandContext.hpp"
#define private public
#include "swap/SwapEngine.hpp"
#undef private
static std::uintptr_t base;
static int iterate(int (*cb)(dl_phdr_info*,std::size_t,void*),void* data) {
    ElfW(Phdr) ph{}; ph.p_type=PT_LOAD; ph.p_flags=PF_R|PF_X;
    ph.p_memsz=base?0x14000000:UINTPTR_MAX;
    dl_phdr_info info{}; info.dlpi_name="libminecraftpe.so";
    info.dlpi_addr=base; info.dlpi_phdr=&ph; info.dlpi_phnum=1;
    return cb(&info,sizeof(info),data);
}
#define dl_iterate_phdr iterate
#include "swap/SwapEngine.cpp"
#undef dl_iterate_phdr
using namespace levioffhand::swap;
using namespace levioffhand::runtime;
struct Stack {
    int id=0, count=0;
    std::array<unsigned char,0x88> metadata{};
    std::uint64_t networkId=0;
};
static_assert(sizeof(Stack)==0x98);
static Stack mainSlot,offSlot,emptySlot;
static std::array<std::byte,0xa08> player{};
static std::array<std::byte,0xc0> state{};
static std::array<std::uintptr_t,9> table{};
static const void* container=table.data();
static int copies=0,destructs=0,stops=0;
static bool usingItem=false, rejectWrite=false,stopChangesStack=false;
static std::vector<int> writes;
static bool isNull(const void* s) {return static_cast<const Stack*>(s)->count==0;}
static bool equal(const void* a,const void* b) {
    if(isNull(a)&&isNull(b))return true;
    const auto& x=*static_cast<const Stack*>(a);
    const auto& y=*static_cast<const Stack*>(b);
    return x.id==y.id && x.count==y.count && x.metadata==y.metadata;
}
static void require(bool condition,const char* message) {
    if(!condition){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
}
template<class T> static void put(void* dst,std::size_t off,T value) {
    std::memcpy(static_cast<std::byte*>(dst)+off,&value,sizeof(value));
}
static const void* getMain(const void* c,int index) {
    require(c==&container && index==3,"physical hotbar slot");return &mainSlot;
}
static const void* getOff(const void*) {return &offSlot;}
static void copy(void* out,const void* in) {++copies;std::memcpy(out,in,sizeof(Stack));}
static void destroy(void*) {++destructs;}
static bool isUsing(const void*) {return usingItem;}
static void stop(void*) {++stops;usingItem=false;if(stopChangesStack)mainSlot.count=31;}
static void setMain(void* c,int index,const void* s) {
    require(c==&container && index==3,"write same physical slot");
    require(currentScopedAction() && currentScopedAction()->hand==ActionHand::MainHand,
            "native notifications must read MAIN even during OFF session");
    require(!usingItem,"cancel stale use before mutation");
    require(s!=&offSlot && s!=&mainSlot,"MAIN source must be detached");
    writes.push_back(0);if(!rejectWrite)mainSlot=*static_cast<const Stack*>(s);
}
static void setOff(void*,unsigned char hand,const void* s) {
    require(hand==1,"offhand selector");
    require(s!=&offSlot && s!=&mainSlot,"OFF source must be detached");
    // Native LocalPlayer setter skips equal content, regardless of network ID.
    if(equal(s,&offSlot))return;
    writes.push_back(1);offSlot=*static_cast<const Stack*>(s);
}
int main(int argc,char**argv) {
    if(argc!=2)return 2;
    const std::string test=argv[1];
    auto& engine=SwapEngine::instance();
    if(test=="pristine" || test=="bad_body" || test=="unmapped") {
        void* region=mmap(nullptr,0x14000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        require(region!=MAP_FAILED,"fixture mmap");base=reinterpret_cast<std::uintptr_t>(region);
        const unsigned char bytes[]={0x08,0x00,0x40,0xf9,0xe3,0x03,0x1f,0x2a,0x04,0x39,0x40,0xf9,0x80,0x00,0x1f,0xd6};
        auto* target=reinterpret_cast<unsigned char*>(base+0xF9DA128);
        std::memcpy(target,bytes,sizeof(bytes));if(test=="bad_body")target[8]^=0xff;
        const auto result=resolve(test=="unmapped"?0x15000000:0xF9DA128,kSetInventorySlotFingerprint);
        require((result!=0)==(test=="pristine"),"inventory setter ABI validation");
        munmap(region,0x14000000);
    } else {
        engine.mGetOffhandSlot=getOff;engine.mStackIsNull=isNull;
        engine.mItemStackCopyCtor=copy;engine.mItemStackDtor=destroy;
        engine.mSetInventorySlot=setMain;engine.mSetItemInHandSlot=setOff;
        engine.mStacksEqual=equal;engine.mIsUsingItem=isUsing;engine.mStopUsingItem=stop;
        engine.mEmptyItem=&emptySlot;
        table[8]=reinterpret_cast<std::uintptr_t>(&getMain);
        put(player.data(),0x570,state.data());put(state.data(),0xb8,&container);
        put(state.data(),0x10,3);
        mainSlot.id=10;mainSlot.count=test=="main_empty"||test=="both_empty"?0:64;
        offSlot.id=20;offSlot.count=test=="off_empty"||test=="both_empty"?0:12;
        mainSlot.metadata.fill(0xa5);offSlot.metadata.fill(0x5a);
        mainSlot.networkId=101;offSlot.networkId=202;
        if(test=="identical"){offSlot=mainSlot;offSlot.networkId=202;}
        if(test=="repeat_same_item")offSlot.id=mainSlot.id;
        if(test=="invalid_flag")put(state.data(),0xb0,std::uint8_t{1});
        if(test=="invalid_index")put(state.data(),0x10,9);
        if(test=="missing_state")put(player.data(),0x570,static_cast<void*>(nullptr));
        if(test=="invalid_flag"||test=="invalid_index"||test=="missing_state") {
            require(!engine.selectedStack(player.data()),"unwritable slot must not masquerade as empty");
            require(!engine.swap(player.data(),&mainSlot) && writes.empty(),"reject before mutation");
        } else {
            usingItem=test=="active_use"||test=="stop_callback";
            stopChangesStack=test=="stop_callback";
            rejectWrite=test=="rejected_write";
            Stack a=mainSlot,b=offSlot;
            if(stopChangesStack)a.count=31;
            ScopedActionHand outer(ActionHand::OffHand,ActionKind::UseAir);
            const int rounds=test=="repeat_same_item"?1000:1;
            for(int i=0;i<rounds;++i) {
                writes.clear();
                bool ok=engine.swap(player.data(),&mainSlot);
                require(ok!=rejectWrite,"local verification result");
                require(currentScopedAction()->hand==ActionHand::OffHand,"restore caller hand scope");
                if(rejectWrite) {
                    require(equal(&mainSlot,&a)&&equal(&offSlot,&b),"rejected MAIN write must not overwrite OFF");
                    require(writes==std::vector<int>{0},"stop after rejected MAIN write");
                    break;
                }
                const bool noop=test=="identical"||test=="both_empty";
                require(writes==(noop?std::vector<int>{}:std::vector<int>{0,1}),
                        "one native write per changed slot, no temporary clear");
                const Stack& wantMain=noop||i%2? a:b;
                const Stack& wantOff=noop||i%2? b:a;
                require(equal(&mainSlot,&wantMain)&&equal(&offSlot,&wantOff),"contents/count/metadata preserved");
                if(!isNull(&mainSlot))require(mainSlot.networkId==wantMain.networkId,"main stack identity");
                if(!isNull(&offSlot))require(offSlot.networkId==wantOff.networkId,"offhand stack identity");
            }
            require(copies==destructs,"snapshot lifetime");
            require(stops==((test=="active_use"||test=="stop_callback")?1:0),"native use cancellation");
        }
    }
    std::cout<<"PASS: "<<test<<'\n';
}
