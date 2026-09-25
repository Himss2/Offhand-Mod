#include "swap/SwapRuntime.hpp"
#include "swap/SwapEngine.hpp"

#include <android/log.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <chrono>
#include <cstring>
#include <elf.h>
#include <link.h>
#include <memory>

#include <pl/memory/Hook.hpp>

namespace levioffhand::swap {
namespace {

constexpr char kMinecraftLibrary[]="libminecraftpe.so";
constexpr char kLogTag[]="Levi Offhand";

constexpr std::uintptr_t kClientPreFrameTickRva=0x9803334;
constexpr std::size_t kClientGetLocalPlayerVtableOffset=0x100;
constexpr std::uint64_t kMinSwapIntervalNs=50'000'000ULL;

constexpr std::array<std::uint8_t,16> kClientPreFrameTickFingerprint{
    0xFF,0xC3,0x00,0xD1,0xFD,0x7B,0x01,0xA9,
    0xF4,0x4F,0x02,0xA9,0xFD,0x43,0x00,0x91,
};

struct ModuleState { std::uintptr_t base{0}; };

int moduleCallback(dl_phdr_info* info,std::size_t,void* raw) noexcept {
    if(!info || !info->dlpi_name ||
       std::strstr(info->dlpi_name,kMinecraftLibrary)==nullptr) return 0;
    static_cast<ModuleState*>(raw)->base=
        static_cast<std::uintptr_t>(info->dlpi_addr);
    return 1;
}

[[nodiscard]] std::uintptr_t moduleBase() noexcept {
    ModuleState s{};
    dl_iterate_phdr(&moduleCallback,&s);
    return s.base;
}

struct AddressState {
    std::uintptr_t address{0};
    std::uint32_t flags{0};
    bool found{false};
};

int addressCallback(dl_phdr_info* info,std::size_t,void* raw) noexcept {
    if(!info || !info->dlpi_name ||
       std::strstr(info->dlpi_name,kMinecraftLibrary)==nullptr) return 0;
    auto& s=*static_cast<AddressState*>(raw);
    const auto base=static_cast<std::uintptr_t>(info->dlpi_addr);
    for(std::size_t i=0;i<info->dlpi_phnum;++i) {
        const auto& ph=info->dlpi_phdr[i];
        if(ph.p_type!=PT_LOAD) continue;
        const auto begin=base+static_cast<std::uintptr_t>(ph.p_vaddr);
        const auto end=begin+static_cast<std::uintptr_t>(ph.p_memsz);
        if(s.address>=begin && s.address<end &&
           (static_cast<std::uint32_t>(ph.p_flags)&s.flags)==s.flags) {
            s.found=true;
            break;
        }
    }
    return 1;
}

[[nodiscard]] bool mapped(std::uintptr_t a,std::uint32_t f=0) noexcept {
    if(!a) return false;
    AddressState s{a,f,false};
    dl_iterate_phdr(&addressCallback,&s);
    return s.found;
}

using PreFrameFn=void (*)(void*);
using GetLocalPlayerFn=void* (*)(const void*);

std::unique_ptr<pl::memory::HookHandle> gPreFrameHook;
void* gPreFrameOriginal=nullptr;

[[nodiscard]] void* localPlayerFromClient(void* client) noexcept {
    if(!client) return nullptr;
    const void* vtable=nullptr;
    std::memcpy(&vtable,client,sizeof(vtable));
    if(!mapped(reinterpret_cast<std::uintptr_t>(vtable),0)) return nullptr;

    GetLocalPlayerFn getter=nullptr;
    std::memcpy(
        &getter,
        static_cast<const std::byte*>(vtable)+kClientGetLocalPlayerVtableOffset,
        sizeof(getter)
    );
    if(!getter || !mapped(reinterpret_cast<std::uintptr_t>(getter),PF_X)) {
        return nullptr;
    }
    return getter(client);
}

[[nodiscard]] std::uint64_t steadyNowNs() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count()
    );
}

class ScopedProgress final {
public:
    explicit ScopedProgress(std::atomic_bool& f) noexcept : mFlag(f) {}
    ~ScopedProgress() noexcept { mFlag.store(false,std::memory_order_release); }
private:
    std::atomic_bool& mFlag;
};

void preFrameDetour(void* client) noexcept {
    const auto original=reinterpret_cast<PreFrameFn>(gPreFrameOriginal);
    if(original) original(client);

    auto& runtime=SwapRuntime::instance();
    if(!runtime.hasPendingSwap()) return;

    void* player=localPlayerFromClient(client);
    if(!player) return;

    const void* selected=SwapEngine::instance().selectedStack(player);
    if(!selected) {
        __android_log_print(
            ANDROID_LOG_WARN,kLogTag,
            "[SwapRuntime] selected stack unavailable; request kept queued"
        );
        return;
    }

    // process the request on this exact native frame only.
    // SwapRuntime owns request state; SwapEngine owns storage mutation.
    (void)runtime.drain(player,selected);
}

} // namespace

SwapRuntime& SwapRuntime::instance() noexcept {
    static SwapRuntime runtime;
    return runtime;
}

bool SwapRuntime::drain(void* player,const void* selected) noexcept {
    if(!hasPendingSwap()) return false;

    // Inventory exchange is a logical game action, not a render-frame action.
    // Keep one pending intent queued but cap actual mutation to ~20/s so a
    // held/spammed F button cannot execute dozens of native transactions per
    // rendered frame burst.
    const auto nowNs=steadyNowNs();
    const auto lastNs=mLastSwapStartNs.load(std::memory_order_acquire);
    if(lastNs!=0 && nowNs-lastNs<kMinSwapIntervalNs) {
        return false;
    }

    bool requested=true;
    if(!mSwapRequested.compare_exchange_strong(
            requested,false,std::memory_order_acq_rel)) {
        return false;
    }

    bool busy=false;
    if(!mSwapInProgress.compare_exchange_strong(
            busy,true,std::memory_order_acq_rel)) {
        mSwapRequested.store(true,std::memory_order_release);
        return false;
    }
    ScopedProgress guard(mSwapInProgress);
    mLastSwapStartNs.store(nowNs,std::memory_order_release);

    const auto res=SwapEngine::instance().swap(player,selected);
    if(res==SwapResult::RetryLater) {
        // The previous native inventory/request owner is still settling.
        // Preserve exactly one coalesced user intent; do not retry rejected
        // ABI/state failures because they may be non-transient.
        mSwapRequested.store(true,std::memory_order_release);
        return false;
    }
    return res==SwapResult::Success;
}

bool SwapRuntime::install(pl::mod::ModContext& context) noexcept {
    uninstall(context);

    if(!SwapEngine::instance().install(context)) {
        return false;
    }

    const auto base=moduleBase();
    const auto target=base?base+kClientPreFrameTickRva:0;
    const bool valid=
        target &&
        mapped(target,PF_X) &&
        std::memcmp(
            reinterpret_cast<const void*>(target),
            kClientPreFrameTickFingerprint.data(),
            kClientPreFrameTickFingerprint.size()
        )==0;

    __android_log_print(
        ANDROID_LOG_INFO,kLogTag,
        "[SwapRuntime] preFrame target valid=%d",
        valid
    );

    if(!valid) {
        context.logger().error("Swap runtime: preFrame target validation failed");
        SwapEngine::instance().uninstall();
        return false;
    }

    gPreFrameHook=std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(target),
        reinterpret_cast<void*>(&preFrameDetour),
        &gPreFrameOriginal,
        pl::memory::HookPriority::Normal
    );

    if(!gPreFrameHook || !gPreFrameHook->installed() || !gPreFrameOriginal) {
        context.logger().error("Swap runtime: ClientInstance::preFrameTick hook failed");
        if(gPreFrameHook) {
            gPreFrameHook->reset();
            gPreFrameHook.reset();
        }
        gPreFrameOriginal=nullptr;
        SwapEngine::instance().uninstall();
        return false;
    }

    mSwapRequested.store(false,std::memory_order_release);
    mSwapInProgress.store(false,std::memory_order_release);
    mLastSwapStartNs.store(0,std::memory_order_release);
    mFeatureEnabled.store(true,std::memory_order_release);
    mInstalled.store(true,std::memory_order_release);

    context.logger().info(
        "Swap runtime active: UI queue -> preFrame -> isolated SwapEngine"
    );
    return true;
}

void SwapRuntime::uninstall(pl::mod::ModContext&) noexcept {
    mInstalled.store(false,std::memory_order_release);
    mFeatureEnabled.store(false,std::memory_order_release);
    mSwapRequested.store(false,std::memory_order_release);
    mSwapInProgress.store(false,std::memory_order_release);
    mLastSwapStartNs.store(0,std::memory_order_release);

    if(gPreFrameHook) {
        gPreFrameHook->reset();
        gPreFrameHook.reset();
    }
    gPreFrameOriginal=nullptr;
    SwapEngine::instance().uninstall();
}

void SwapRuntime::setFeatureEnabled(bool enabled) noexcept {
    mFeatureEnabled.store(enabled,std::memory_order_release);
    if(!enabled) mSwapRequested.store(false,std::memory_order_release);
}

bool SwapRuntime::featureEnabled() const noexcept {
    return mFeatureEnabled.load(std::memory_order_acquire);
}

bool SwapRuntime::installed() const noexcept {
    return mInstalled.load(std::memory_order_acquire) &&
        gPreFrameHook && gPreFrameHook->installed() &&
        gPreFrameOriginal && SwapEngine::instance().ready();
}

void SwapRuntime::requestSwap() noexcept {
    if(!installed() || !featureEnabled()) {
        __android_log_print(
            ANDROID_LOG_WARN,kLogTag,
            "[SwapRuntime] F request ignored: runtime unavailable or module disabled"
        );
        return;
    }
    // Coalesce repeated UI taps while one request is already waiting.
    // This preserves the latest intent without growing work on the main thread.
    (void)mSwapRequested.exchange(true,std::memory_order_acq_rel);
}

bool SwapRuntime::hasPendingSwap() const noexcept {
    return installed() && featureEnabled() &&
        mSwapRequested.load(std::memory_order_acquire);
}

} // namespace levioffhand::swap
