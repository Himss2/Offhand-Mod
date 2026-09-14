#include "runtime/AutoInsertRouting.hpp"
#include "runtime/AutoInsertRoutingCore.hpp"

#include <android/log.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <new>

#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

namespace levioffhand::runtime {
namespace {

constexpr char kMinecraftLibrary[] = "libminecraftpe.so";
constexpr char kLogTag[] = "Levi Offhand";

constexpr std::uintptr_t kPlannerRva = 0xF024024;
constexpr std::size_t kMaxDestinationRecords = 256;

using routing::DestinationRecord;
using routing::filterDestinations;
using routing::kDestinationRecordSize;

constexpr char kAutoInsertPlannerSignature[] =
    "FF 83 05 D1 "
    "FD 7B 10 A9 "
    "FC 6F 11 A9 "
    "FA 67 12 A9 "
    "F8 5F 13 A9 "
    "F6 57 14 A9 "
    "F4 4F 15 A9 "
    "FD 03 04 91 "
    "E2 2F 00 F9 "
    "49 D0 3B D5 "
    "28 15 40 F9 "
    "7F 04 00 71 "
    "A8 03 1F F8 "
    "0B 55 00 54 "
    "F6 03 00 AA";

struct DestinationVectorAbi {
    const DestinationRecord* begin;
    const DestinationRecord* end;
    const DestinationRecord* capacityEnd;
};
static_assert(sizeof(DestinationVectorAbi) == 0x18);

using PlannerFn = int (*)(
    void* arg0,
    const void* arg1,
    const void* arg2,
    int amount,
    const void* destinationVector
);

[[nodiscard]] bool belongsToMinecraft(std::uintptr_t address) noexcept {
    if (address == 0) {
        return false;
    }

    Dl_info info{};
    if (
        dladdr(reinterpret_cast<void*>(address), &info) == 0 ||
        info.dli_fname == nullptr
    ) {
        return false;
    }
    return std::strstr(info.dli_fname, kMinecraftLibrary) != nullptr;
}

[[nodiscard]] std::uintptr_t moduleBaseOf(std::uintptr_t address) noexcept {
    if (address == 0) {
        return 0;
    }
    Dl_info info{};
    if (
        dladdr(reinterpret_cast<void*>(address), &info) == 0 ||
        info.dli_fbase == nullptr ||
        info.dli_fname == nullptr ||
        std::strstr(info.dli_fname, kMinecraftLibrary) == nullptr
    ) {
        return 0;
    }
    return reinterpret_cast<std::uintptr_t>(info.dli_fbase);
}

[[nodiscard]] bool decodeVector(
    const void* raw,
    DestinationVectorAbi& out,
    std::size_t& recordCount
) noexcept {
    recordCount = 0;
    if (raw == nullptr) {
        return false;
    }

    std::memcpy(&out, raw, sizeof(out));

    const auto begin = reinterpret_cast<std::uintptr_t>(out.begin);
    const auto end = reinterpret_cast<std::uintptr_t>(out.end);
    const auto capacityEnd = reinterpret_cast<std::uintptr_t>(out.capacityEnd);

    if (begin == 0 && end == 0 && capacityEnd == 0) {
        return true;
    }
    if (begin == 0 || end == 0 || capacityEnd == 0) {
        return false;
    }
    if (end < begin || capacityEnd < end) {
        return false;
    }

    const auto usedBytes = end - begin;
    if ((usedBytes % kDestinationRecordSize) != 0) {
        return false;
    }

    recordCount = usedBytes / kDestinationRecordSize;
    return recordCount <= kMaxDestinationRecords;
}

} // namespace

AutoInsertRouting* AutoInsertRouting::sInstance = nullptr;

AutoInsertRouting::~AutoInsertRouting() = default;

AutoInsertRouting& AutoInsertRouting::instance() noexcept {
    static AutoInsertRouting value;
    return value;
}

bool AutoInsertRouting::install(pl::mod::ModContext& context) noexcept {
    if (installed()) {
        return true;
    }

    mOriginal = nullptr;
    mTarget = pl::memory::resolveSignature(
        kAutoInsertPlannerSignature,
        kMinecraftLibrary
    );

    if (!belongsToMinecraft(mTarget)) {
        context.logger().error(
            "Levi Offhand v0.2.62: automatic insertion planner resolution failed"
        );
        mTarget = 0;
        return false;
    }

    // Publish the instance before the hook becomes reachable so the detour
    // cannot observe a transient null singleton during installation.
    sInstance = this;

    mHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mTarget),
        reinterpret_cast<void*>(&AutoInsertRouting::plannerDetour),
        &mOriginal,
        pl::memory::HookPriority::Normal
    );

    if (!mHook || !mHook->installed() || mOriginal == nullptr) {
        context.logger().error(
            "Levi Offhand v0.2.62: automatic insertion planner hook failed"
        );
        if (mHook) {
            mHook->uninstall();
            mHook.reset();
        }
        mOriginal = nullptr;
        mTarget = 0;
        sInstance = nullptr;
        return false;
    }

    mFeatureEnabled.store(true, std::memory_order_release);
    mLoggedFilter.store(false, std::memory_order_relaxed);
    mLoggedRetainedGuard.store(false, std::memory_order_relaxed);

    context.logger().info(
        "[AutoInsertRouting] planner active RVA=0x{:x}",
        kPlannerRva
    );
    context.logger().info(
        "[AutoInsertRouting] ContainerValidation hooks = 0"
    );
    return true;
}

void AutoInsertRouting::uninstall(pl::mod::ModContext& context) noexcept {
    if (mHook) {
        mHook->uninstall();
        mHook.reset();
    }

    // Clear the singleton only after the detour is no longer reachable.
    sInstance = nullptr;
    mOriginal = nullptr;
    mTarget = 0;
    mFeatureEnabled.store(false, std::memory_order_release);
    context.logger().info("[AutoInsertRouting] planner guard removed");
}

void AutoInsertRouting::setFeatureEnabled(bool enabled) noexcept {
    if (enabled) {
        mFeatureEnabled.store(true, std::memory_order_release);
        return;
    }

    // Existing Item singletons may retain mAllowOffHand=true after the
    // constructor patch is reverted. Keep automatic routing protected until
    // process restart instead of exposing the v0.2.60 crafting regression.
    mFeatureEnabled.store(true, std::memory_order_release);

    bool expected = false;
    if (mLoggedRetainedGuard.compare_exchange_strong(
            expected,
            true,
            std::memory_order_relaxed
        )) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[AutoInsertRouting] routing guard retained until process restart"
        );
    }
}

bool AutoInsertRouting::featureEnabled() const noexcept {
    return mFeatureEnabled.load(std::memory_order_acquire);
}

bool AutoInsertRouting::installed() const noexcept {
    return mHook != nullptr && mHook->installed() && mOriginal != nullptr;
}

int AutoInsertRouting::plannerDetour(
    void* arg0,
    const void* arg1,
    const void* arg2,
    int amount,
    const void* destinationVector
) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mOriginal == nullptr) {
        return 0;
    }

    const auto original = reinterpret_cast<PlannerFn>(instance->mOriginal);
    if (!instance->featureEnabled()) {
        return original(arg0, arg1, arg2, amount, destinationVector);
    }

    DestinationVectorAbi source{};
    std::size_t recordCount = 0;
    if (!decodeVector(destinationVector, source, recordCount) || recordCount == 0) {
        return original(arg0, arg1, arg2, amount, destinationVector);
    }

    std::size_t offhandCount = 0;
    for (std::size_t index = 0; index < recordCount; ++index) {
        if (routing::isOffhandRecord(source.begin[index])) {
            ++offhandCount;
        }
    }

    if (offhandCount == 0) {
        return original(arg0, arg1, arg2, amount, destinationVector);
    }

    auto filtered = std::unique_ptr<DestinationRecord[]>(
        new (std::nothrow) DestinationRecord[recordCount]
    );
    if (!filtered) {
        return original(arg0, arg1, arg2, amount, destinationVector);
    }

    const auto filterResult = filterDestinations(
        source.begin,
        recordCount,
        filtered.get()
    );
    const auto keptCount = filterResult.keptCount;

    DestinationVectorAbi filteredVector{
        filtered.get(),
        filtered.get() + keptCount,
        filtered.get() + keptCount,
    };

    bool expected = false;
    if (instance->mLoggedFilter.compare_exchange_strong(
            expected,
            true,
            std::memory_order_relaxed
        )) {
        const auto caller = reinterpret_cast<std::uintptr_t>(
            __builtin_return_address(0)
        );
        const auto base = moduleBaseOf(caller);
        const auto callerRva = base != 0 && caller >= base ? caller - base : 0;
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[AutoInsertRouting] callerRva=0x%llX candidates=%zu offhandFiltered=1 kept=%zu",
            static_cast<unsigned long long>(callerRva),
            recordCount,
            keptCount
        );
    }

    return original(arg0, arg1, arg2, amount, &filteredVector);
}

} // namespace levioffhand::runtime
