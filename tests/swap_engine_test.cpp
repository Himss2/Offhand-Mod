// Production resolver/exchange with a fake ELF map and native slot functions.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>
#include <string>
#include <sys/mman.h>
#include <link.h>
#include <pl/Mod.hpp>
#define private public
#include "swap/SwapEngine.hpp"
#undef private
static std::uintptr_t base;
static int iterate(int (*cb)(dl_phdr_info*,std::size_t,void*),void* data) {
 ElfW(Phdr) ph{};ph.p_type=PT_LOAD;ph.p_flags=PF_R|PF_X;ph.p_memsz=0x14000000;
 dl_phdr_info info{};info.dlpi_name="libminecraftpe.so";info.dlpi_addr=base;info.dlpi_phdr=&ph;info.dlpi_phnum=1;
 return cb(&info,sizeof(info),data);
}
#define dl_iterate_phdr iterate
#include "swap/SwapEngine.cpp"
#undef dl_iterate_phdr
using namespace levioffhand::swap;
struct Stack { int id=0; int count=0; std::array<unsigned char,0x90> metadata{}; };
static_assert(sizeof(Stack)==0x98);
static Stack mainSlot,offSlot,emptySlot;
static int copies=0,destructs=0;
static std::vector<int> writes;
static const void* getOff(const void*) {return &offSlot;}
static bool isNull(const void* s) {return static_cast<const Stack*>(s)->count==0;}
static void copy(void* out,const void* in) {++copies;std::memcpy(out,in,sizeof(Stack));}
static void destroy(void*) {++destructs;}
static void setMain(void*,const void* s) {writes.push_back(0);mainSlot=*static_cast<const Stack*>(s);}
static void setOff(void*,unsigned char hand,const void* s) {if(hand!=1)std::abort();writes.push_back(1);offSlot=*static_cast<const Stack*>(s);}
int main(int argc,char**argv) {
 if(argc!=2)return 2;
 const std::string test=argv[1];
 auto& engine=SwapEngine::instance();
 if(test=="pristine" || test=="hooked" || test=="bad_body" || test=="unmapped") {
  void* region=mmap(nullptr,0x14000000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
  if(region==MAP_FAILED)return 2;
  base=reinterpret_cast<std::uintptr_t>(region);
  // Exact supplied binary bytes; first 16 bytes may be replaced by an inline hook.
  const unsigned char bytes[]={0xfd,0x7b,0xbb,0xa9,0xfc,0x67,0x01,0xa9,0xf8,0x5f,0x02,0xa9,0xf6,0x57,0x03,0xa9,0xf4,0x4f,0x04,0xa9,0xfd,0x03,0x00,0x91,0xff,0xc3,0x0e,0xd1,0x56,0xd0,0x3b,0xd5,0xf3,0x03,0x01,0xaa,0xf4,0x03,0x00,0xaa,0xc8,0x16,0x40,0xf9,0xa8,0x83,0x1f,0xf8,0x04,0xa0,0xed,0x97,0x08,0x00,0x40,0xf9,0x08,0x11,0x43,0xf9,0x00,0x01,0x3f,0xd6,0xf5,0x03,0x00,0xaa,0xe8,0x23,0x00,0x91,0x80,0x22,0x00,0x91,0xac,0xc8,0xb6,0x94};
  auto* target=reinterpret_cast<unsigned char*>(base+kSetSelectedItemRva);
  std::memcpy(target,bytes,sizeof(bytes));
  if(test=="hooked" || test=="bad_body") std::memset(target,0,16);
  if(test=="bad_body") target[40]^=0xff;
  const auto rva=test=="unmapped"?0x15000000:kSetSelectedItemRva;
  const auto resolved=resolveSelectedSetter(rva);
  const bool expected=test=="pristine"||test=="hooked";
  const bool ok=(resolved!=0)==expected;
  munmap(region,0x14000000);
  if(!ok){std::cerr<<"FAIL: "<<test<<" callable setter resolution\n";return 1;}
 } else {
  engine.mGetOffhandSlot=getOff;engine.mStackIsNull=isNull;
  engine.mItemStackCopyCtor=copy;engine.mItemStackDtor=destroy;
  engine.mSetSelectedItem=setMain;engine.mSetItemInHandSlot=setOff;engine.mEmptyItem=&emptySlot;
  mainSlot.id=10;mainSlot.count=test=="main_empty"||test=="both_empty"?0:64;
  offSlot.id=20;offSlot.count=test=="off_empty"||test=="both_empty"?0:12;
  mainSlot.metadata.fill(0xa5);offSlot.metadata.fill(0x5a);
  const Stack a=mainSlot,b=offSlot;
  int player=1;
  if(!engine.swap(&player,&mainSlot))return 1;
  if(!isNull(&a) && (offSlot.id!=a.id||offSlot.count!=a.count||offSlot.metadata!=a.metadata))return 1;
  if(!isNull(&b) && (mainSlot.id!=b.id||mainSlot.count!=b.count||mainSlot.metadata!=b.metadata))return 1;
  if(isNull(&a)&&!isNull(&offSlot))return 1;
  if(isNull(&b)&&!isNull(&mainSlot))return 1;
  const std::vector<int> expected=test=="both_empty"?std::vector<int>{}:test=="main_empty"?std::vector<int>{1,0}:test=="off_empty"?std::vector<int>{0,1}:std::vector<int>{0,1,0};
  if(writes!=expected||copies!=destructs)return 1;
 }
 std::cout<<"PASS: "<<test<<'\n';
}
