#include "runtime/HandActionRouter.hpp"

#include "runtime/ActionHandContext.hpp"
#include "runtime/HandActionRouterCore.hpp"
#include "runtime/NativeCapabilityProbe.hpp"

#include <android/log.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <link.h>
#include <memory>

#include <pl/memory/Hook.hpp>

namespace levioffhand::runtime {
namespace {

constexpr char kMinecraftLibrary[] = "libminecraftpe.so";
constexpr char kLogTag[] = "Levi Offhand";

constexpr std::uintptr_t kAttackRva = 0xEF721E4;
constexpr std::uintptr_t kStartDestroyBlockRva = 0xEF72684;
constexpr std::uintptr_t kDestroyBlockRva = 0xEF72C18;
constexpr std::uintptr_t kContinueDestroyBlockRva = 0xEF72F9C;
constexpr std::uintptr_t kStopDestroyBlockRva = 0xEF7398C;
constexpr std::uintptr_t kBaseUseItemRva = 0xEF75578;
constexpr std::uintptr_t kReleaseUsingItemRva = 0xEF76108;
constexpr ActionSlotToken kOffhandSlotIdentity = 34;

constexpr std::array<std::uint8_t, 16> kAttackFingerprint{
    0xFF, 0x43, 0x07, 0xD1, 0xFD, 0x7B, 0x17, 0xA9,
    0xFC, 0xC3, 0x00, 0xF9, 0xFA, 0x67, 0x19, 0xA9,
};

constexpr std::array<std::uint8_t, 16> kStartDestroyBlockFingerprint{
    0xFF, 0x83, 0x01, 0xD1, 0xFD, 0x7B, 0x01, 0xA9,
    0xF9, 0x13, 0x00, 0xF9, 0xF8, 0x5F, 0x03, 0xA9,
};

constexpr std::array<std::uint8_t, 16> kDestroyBlockFingerprint{
    0xFF, 0x03, 0x02, 0xD1, 0xFD, 0x7B, 0x04, 0xA9,
    0xF8, 0x5F, 0x05, 0xA9, 0xF6, 0x57, 0x06, 0xA9,
};

constexpr std::array<std::uint8_t, 16> kContinueDestroyBlockFingerprint{
    0xFF, 0x83, 0x03, 0xD1, 0xE9, 0x23, 0x07, 0x6D,
    0xFD, 0x7B, 0x08, 0xA9, 0xFC, 0x6F, 0x09, 0xA9,
};

constexpr std::array<std::uint8_t, 16> kStopDestroyBlockFingerprint{
    0xFD, 0x7B, 0xBE, 0xA9, 0xF3, 0x0B, 0x00, 0xF9,
    0xFD, 0x03, 0x00, 0x91, 0xF3, 0x03, 0x00, 0xAA,
};

constexpr std::array<std::uint8_t, 16> kBaseUseItemFingerprint{
    0xFF, 0x43, 0x04, 0xD1, 0xFD, 0x7B, 0x0D, 0xA9,
    0xFC, 0x5F, 0x0E, 0xA9, 0xF6, 0x57, 0x0F, 0xA9,
};

constexpr std::array<std::uint8_t, 16> kReleaseUsingItemFingerprint{
    0xFF, 0x03, 0x04, 0xD1, 0xFD, 0x7B, 0x0C, 0xA9,
    0xF7, 0x6B, 0x00, 0xF9, 0xF6, 0x57, 0x0E, 0xA9,
};

using AttackFn = bool (*)(
    void* gameMode,
    void* entity,
    bool playPredictiveSound,
    const void* hitPosition
);
using StartDestroyBlockFn = bool (*)(
    void* gameMode,
    const void* blockPos,
    unsigned char face,
    bool* hasDestroyedBlock
);
using DestroyBlockFn = bool (*)(
    void* gameMode,
    const void* blockPos,
    unsigned char face
);
using ContinueDestroyBlockFn = bool (*)(
    void* gameMode,
    const void* blockPos,
    unsigned char face,
    const void* playerPos,
    bool* hasDestroyedBlock
);
using StopDestroyBlockFn = void (*)(
    void* gameMode,
    const void* blockPos
);
using BaseUseItemFn = bool (*)(void* gameMode, const void* itemStack);
using SelectedItemFn = const void* (*)(const void* player);
using ReleaseUsingItemFn = void (*)(void* gameMode);

thread_local const void* gScopedPlayer = nullptr;
thread_local const void* gSessionPlayer = nullptr;
thread_local void* gSessionGameMode = nullptr;
thread_local ActionTargetToken gSessionTargetIdentity = 0;
thread_local bool gInsideBaseUseDetour = false;
thread_local bool gInsideAttackDetour = false;
thread_local bool gInsideMiningDetour = false;

class ScopedRoutedPlayer final {
public:
    explicit ScopedRoutedPlayer(const void* player) noexcept
        : mPrevious(gScopedPlayer) {
        gScopedPlayer = player;
    }

    ~ScopedRoutedPlayer() noexcept {
        gScopedPlayer = mPrevious;
    }

    ScopedRoutedPlayer(const ScopedRoutedPlayer&) = delete;
    ScopedRoutedPlayer& operator=(const ScopedRoutedPlayer&) = delete;

private:
    const void* mPrevious;
};

class ScopedBool final {
public:
    explicit ScopedBool(bool& value) noexcept
        : mValue(value), mPrevious(value) {
        mValue = true;
    }

    ~ScopedBool() noexcept {
        mValue = mPrevious;
    }

private:
    bool& mValue;
    bool mPrevious;
};

[[nodiscard]] bool belongsToMinecraft(std::uintptr_t address) noexcept {
    if (address == 0) {
        return false;
    }

    Dl_info info{};
    return dladdr(reinterpret_cast<void*>(address), &info) != 0 &&
        info.dli_fname != nullptr &&
        std::strstr(info.dli_fname, kMinecraftLibrary) != nullptr;
}

struct ModuleSearchState {
    std::uintptr_t base{0};
};

int moduleSearchCallback(
    dl_phdr_info* info,
    std::size_t,
    void* rawState
) noexcept {
    if (
        info == nullptr ||
        info->dlpi_name == nullptr ||
        std::strstr(info->dlpi_name, kMinecraftLibrary) == nullptr
    ) {
        return 0;
    }

    auto* state = static_cast<ModuleSearchState*>(rawState);
    state->base = static_cast<std::uintptr_t>(info->dlpi_addr);
    return 1;
}

[[nodiscard]] std::uintptr_t minecraftModuleBase() noexcept {
    ModuleSearchState state{};
    dl_iterate_phdr(&moduleSearchCallback, &state);
    return state.base;
}

template <std::size_t N>
[[nodiscard]] std::uintptr_t resolveExactTarget(
    std::uintptr_t expectedRva,
    const std::array<std::uint8_t, N>& fingerprint
) noexcept {
    const std::uintptr_t base = minecraftModuleBase();
    if (base == 0) {
        return 0;
    }

    const std::uintptr_t target = base + expectedRva;
    if (!belongsToMinecraft(target)) {
        return 0;
    }

    if (
        std::memcmp(
            reinterpret_cast<const void*>(target),
            fingerprint.data(),
            fingerprint.size()
        ) != 0
    ) {
        return 0;
    }

    return target;
}

void clearSessionIdentity() noexcept {
    gSessionPlayer = nullptr;
    gSessionGameMode = nullptr;
    gSessionTargetIdentity = 0;
}

[[nodiscard]] ActionIdentityToken identityForActiveUseStack(
    const void* itemInUseStack
) noexcept {
    return static_cast<ActionIdentityToken>(
        reinterpret_cast<std::uintptr_t>(itemInUseStack)
    );
}

[[nodiscard]] ActionTargetToken targetIdentityForBlockPos(
    const void* blockPos
) noexcept {
    if (blockPos == nullptr) {
        return 0;
    }

    std::uint8_t bytes[12]{};
    std::memcpy(bytes, blockPos, sizeof(bytes));

    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return static_cast<ActionTargetToken>(hash);
}

} // namespace

HandActionRouter* HandActionRouter::sInstance = nullptr;

HandActionRouter::~HandActionRouter() = default;

HandActionRouter& HandActionRouter::instance() noexcept {
    static HandActionRouter value;
    return value;
}

bool HandActionRouter::install(pl::mod::ModContext& context) noexcept {
    if (installed()) {
        return true;
    }

    auto& probe = NativeCapabilityProbe::instance();
    if (!probe.install(context)) {
        context.logger().warn(
            "[HandActionRouter] native capability access unavailable; Java-like actions disabled"
        );
        return false;
    }

    mAttackTarget = resolveExactTarget(kAttackRva, kAttackFingerprint);
    mStartDestroyBlockTarget = resolveExactTarget(
        kStartDestroyBlockRva,
        kStartDestroyBlockFingerprint
    );
    mDestroyBlockTarget = resolveExactTarget(
        kDestroyBlockRva,
        kDestroyBlockFingerprint
    );
    mContinueDestroyBlockTarget = resolveExactTarget(
        kContinueDestroyBlockRva,
        kContinueDestroyBlockFingerprint
    );
    mStopDestroyBlockTarget = resolveExactTarget(
        kStopDestroyBlockRva,
        kStopDestroyBlockFingerprint
    );
    mTarget = resolveExactTarget(kBaseUseItemRva, kBaseUseItemFingerprint);
    mReleaseUsingItemTarget = resolveExactTarget(
        kReleaseUsingItemRva,
        kReleaseUsingItemFingerprint
    );
    mSelectedItemTarget = probe.selectedItemTarget();

    if (
        mAttackTarget == 0 ||
        mStartDestroyBlockTarget == 0 ||
        mDestroyBlockTarget == 0 ||
        mContinueDestroyBlockTarget == 0 ||
        mStopDestroyBlockTarget == 0 ||
        mTarget == 0 ||
        mReleaseUsingItemTarget == 0 ||
        !belongsToMinecraft(mSelectedItemTarget)
    ) {
        context.logger().warn(
            "[HandActionRouter] exact 1.26.45.1 action targets failed validation: "
            "attack=0x{:x} start=0x{:x} destroy=0x{:x} continue=0x{:x} "
            "stop=0x{:x} use=0x{:x} release=0x{:x} selected=0x{:x}",
            mAttackTarget,
            mStartDestroyBlockTarget,
            mDestroyBlockTarget,
            mContinueDestroyBlockTarget,
            mStopDestroyBlockTarget,
            mTarget,
            mReleaseUsingItemTarget,
            mSelectedItemTarget
        );
        probe.uninstall(context);
        mAttackTarget = 0;
        mStartDestroyBlockTarget = 0;
        mDestroyBlockTarget = 0;
        mContinueDestroyBlockTarget = 0;
        mStopDestroyBlockTarget = 0;
        mTarget = 0;
        mReleaseUsingItemTarget = 0;
        mSelectedItemTarget = 0;
        return false;
    }

    sInstance = this;
    mOriginal = nullptr;
    mAttackOriginal = nullptr;
    mStartDestroyBlockOriginal = nullptr;
    mDestroyBlockOriginal = nullptr;
    mContinueDestroyBlockOriginal = nullptr;
    mStopDestroyBlockOriginal = nullptr;
    mSelectedItemOriginal = nullptr;
    mReleaseUsingItemOriginal = nullptr;

    // The selected-item adapter is support infrastructure for every routed
    // action, so publish it before any semantic action hook.
    mSelectedItemHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mSelectedItemTarget),
        reinterpret_cast<void*>(&HandActionRouter::selectedItemDetour),
        &mSelectedItemOriginal,
        pl::memory::HookPriority::Normal
    );
    if (
        !mSelectedItemHook ||
        !mSelectedItemHook->installed() ||
        mSelectedItemOriginal == nullptr
    ) {
        context.logger().warn("[HandActionRouter] selected-item hook failed");
        uninstall(context);
        return false;
    }

    mReleaseUsingItemHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mReleaseUsingItemTarget),
        reinterpret_cast<void*>(&HandActionRouter::releaseUsingItemDetour),
        &mReleaseUsingItemOriginal,
        pl::memory::HookPriority::Normal
    );
    if (
        !mReleaseUsingItemHook ||
        !mReleaseUsingItemHook->installed() ||
        mReleaseUsingItemOriginal == nullptr
    ) {
        context.logger().warn("[HandActionRouter] releaseUsingItem hook failed");
        uninstall(context);
        return false;
    }

    mAttackHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mAttackTarget),
        reinterpret_cast<void*>(&HandActionRouter::attackDetour),
        &mAttackOriginal,
        pl::memory::HookPriority::Normal
    );
    if (!mAttackHook || !mAttackHook->installed() || mAttackOriginal == nullptr) {
        context.logger().warn("[HandActionRouter] _attack hook failed");
        uninstall(context);
        return false;
    }

    mStartDestroyBlockHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mStartDestroyBlockTarget),
        reinterpret_cast<void*>(&HandActionRouter::startDestroyBlockDetour),
        &mStartDestroyBlockOriginal,
        pl::memory::HookPriority::Normal
    );
    if (
        !mStartDestroyBlockHook ||
        !mStartDestroyBlockHook->installed() ||
        mStartDestroyBlockOriginal == nullptr
    ) {
        context.logger().warn("[HandActionRouter] startDestroyBlock hook failed");
        uninstall(context);
        return false;
    }

    mDestroyBlockHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mDestroyBlockTarget),
        reinterpret_cast<void*>(&HandActionRouter::destroyBlockDetour),
        &mDestroyBlockOriginal,
        pl::memory::HookPriority::Normal
    );
    if (
        !mDestroyBlockHook ||
        !mDestroyBlockHook->installed() ||
        mDestroyBlockOriginal == nullptr
    ) {
        context.logger().warn("[HandActionRouter] destroyBlock hook failed");
        uninstall(context);
        return false;
    }

    mContinueDestroyBlockHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mContinueDestroyBlockTarget),
        reinterpret_cast<void*>(&HandActionRouter::continueDestroyBlockDetour),
        &mContinueDestroyBlockOriginal,
        pl::memory::HookPriority::Normal
    );
    if (
        !mContinueDestroyBlockHook ||
        !mContinueDestroyBlockHook->installed() ||
        mContinueDestroyBlockOriginal == nullptr
    ) {
        context.logger().warn("[HandActionRouter] continueDestroyBlock hook failed");
        uninstall(context);
        return false;
    }

    mStopDestroyBlockHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mStopDestroyBlockTarget),
        reinterpret_cast<void*>(&HandActionRouter::stopDestroyBlockDetour),
        &mStopDestroyBlockOriginal,
        pl::memory::HookPriority::Normal
    );
    if (
        !mStopDestroyBlockHook ||
        !mStopDestroyBlockHook->installed() ||
        mStopDestroyBlockOriginal == nullptr
    ) {
        context.logger().warn("[HandActionRouter] stopDestroyBlock hook failed");
        uninstall(context);
        return false;
    }

    // baseUseItem is the broadest semantic entry and is published last.
    mBaseUseItemHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mTarget),
        reinterpret_cast<void*>(&HandActionRouter::baseUseItemDetour),
        &mOriginal,
        pl::memory::HookPriority::Normal
    );
    if (
        !mBaseUseItemHook ||
        !mBaseUseItemHook->installed() ||
        mOriginal == nullptr
    ) {
        context.logger().warn("[HandActionRouter] baseUseItem hook failed");
        uninstall(context);
        return false;
    }

    mFeatureEnabled.store(true, std::memory_order_release);
    mLoggedOffhandUse.store(false, std::memory_order_relaxed);
    mLoggedLongUse.store(false, std::memory_order_relaxed);
    mLoggedOffhandAttack.store(false, std::memory_order_relaxed);
    mLoggedOffhandMining.store(false, std::memory_order_relaxed);

    context.logger().info(
        "[HandActionRouter] Java-like item-use routing active: main -> offhand"
    );
    context.logger().info(
        "[HandActionRouter] real-combat attack routing active: main -> offhand -> vanilla punch"
    );
    context.logger().info(
        "[HandActionRouter] target-sensitive mining routing active across start/continue/destroy/stop"
    );
    context.logger().info(
        "[HandActionRouter] attack=0x{:x}, start=0x{:x}, destroy=0x{:x}, continue=0x{:x}, stop=0x{:x}",
        kAttackRva,
        kStartDestroyBlockRva,
        kDestroyBlockRva,
        kContinueDestroyBlockRva,
        kStopDestroyBlockRva
    );
    return true;
}

void HandActionRouter::uninstall(pl::mod::ModContext& context) noexcept {
    cancelActiveSession();
    mFeatureEnabled.store(false, std::memory_order_release);

    // Stop new semantic entries first, then remove their support hooks.
    if (mBaseUseItemHook) {
        mBaseUseItemHook->reset();
        mBaseUseItemHook.reset();
    }
    if (mStopDestroyBlockHook) {
        mStopDestroyBlockHook->reset();
        mStopDestroyBlockHook.reset();
    }
    if (mContinueDestroyBlockHook) {
        mContinueDestroyBlockHook->reset();
        mContinueDestroyBlockHook.reset();
    }
    if (mDestroyBlockHook) {
        mDestroyBlockHook->reset();
        mDestroyBlockHook.reset();
    }
    if (mStartDestroyBlockHook) {
        mStartDestroyBlockHook->reset();
        mStartDestroyBlockHook.reset();
    }
    if (mAttackHook) {
        mAttackHook->reset();
        mAttackHook.reset();
    }
    if (mReleaseUsingItemHook) {
        mReleaseUsingItemHook->reset();
        mReleaseUsingItemHook.reset();
    }
    if (mSelectedItemHook) {
        mSelectedItemHook->reset();
        mSelectedItemHook.reset();
    }

    mOriginal = nullptr;
    mAttackOriginal = nullptr;
    mStartDestroyBlockOriginal = nullptr;
    mDestroyBlockOriginal = nullptr;
    mContinueDestroyBlockOriginal = nullptr;
    mStopDestroyBlockOriginal = nullptr;
    mSelectedItemOriginal = nullptr;
    mReleaseUsingItemOriginal = nullptr;

    mAttackTarget = 0;
    mStartDestroyBlockTarget = 0;
    mDestroyBlockTarget = 0;
    mContinueDestroyBlockTarget = 0;
    mStopDestroyBlockTarget = 0;
    mTarget = 0;
    mSelectedItemTarget = 0;
    mReleaseUsingItemTarget = 0;

    sInstance = nullptr;
    clearSessionIdentity();
    currentActionSession().cancel();

    NativeCapabilityProbe::instance().uninstall(context);
    context.logger().info("[HandActionRouter] action hooks removed");
}

void HandActionRouter::setFeatureEnabled(bool enabled) noexcept {
    if (!enabled) {
        cancelActiveSession();
    }
    mFeatureEnabled.store(enabled && installed(), std::memory_order_release);
}

bool HandActionRouter::featureEnabled() const noexcept {
    return mFeatureEnabled.load(std::memory_order_acquire);
}

bool HandActionRouter::installed() const noexcept {
    return
        mBaseUseItemHook != nullptr &&
        mBaseUseItemHook->installed() &&
        mAttackHook != nullptr &&
        mAttackHook->installed() &&
        mStartDestroyBlockHook != nullptr &&
        mStartDestroyBlockHook->installed() &&
        mDestroyBlockHook != nullptr &&
        mDestroyBlockHook->installed() &&
        mContinueDestroyBlockHook != nullptr &&
        mContinueDestroyBlockHook->installed() &&
        mStopDestroyBlockHook != nullptr &&
        mStopDestroyBlockHook->installed() &&
        mReleaseUsingItemHook != nullptr &&
        mReleaseUsingItemHook->installed() &&
        mSelectedItemHook != nullptr &&
        mSelectedItemHook->installed() &&
        mOriginal != nullptr &&
        mAttackOriginal != nullptr &&
        mStartDestroyBlockOriginal != nullptr &&
        mDestroyBlockOriginal != nullptr &&
        mContinueDestroyBlockOriginal != nullptr &&
        mStopDestroyBlockOriginal != nullptr &&
        mReleaseUsingItemOriginal != nullptr &&
        mSelectedItemOriginal != nullptr &&
        NativeCapabilityProbe::instance().available();
}

bool HandActionRouter::baseUseItemDetour(
    void* gameMode,
    const void* itemStack
) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mOriginal == nullptr) {
        return false;
    }

    const auto original = reinterpret_cast<BaseUseItemFn>(instance->mOriginal);
    if (
        !instance->featureEnabled() ||
        gInsideBaseUseDetour ||
        currentActionSession().active()
    ) {
        return original(gameMode, itemStack);
    }

    auto& probe = NativeCapabilityProbe::instance();
    const void* player = probe.playerFromGameMode(gameMode);
    const void* mainStack = probe.mainhandStack(gameMode);
    if (
        player == nullptr ||
        mainStack == nullptr ||
        itemStack != mainStack
    ) {
        return original(gameMode, itemStack);
    }

    const void* offStack = probe.offhandStackForPlayer(player);
    if (offStack == nullptr || probe.stackIsNull(offStack)) {
        return original(gameMode, itemStack);
    }

    ScopedBool reentryGuard(gInsideBaseUseDetour);
    ScopedRoutedPlayer routedPlayer(player);

    const UseRouteResult result = routeUseAction(
        [&]() noexcept {
            return original(gameMode, mainStack);
        },
        [&]() noexcept {
            return original(gameMode, offStack);
        },
        []() noexcept {},
        ActionKind::UseAir
    );

    if (result.handled && result.hand == ActionHand::OffHand) {
        bool expected = false;
        if (instance->mLoggedOffhandUse.compare_exchange_strong(
                expected,
                true,
                std::memory_order_relaxed
            )) {
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[HandActionRouter] right-use selected OFFHAND after MAIN passed"
            );
        }

        if (probe.playerIsUsingItem(player)) {
            const void* itemInUseStack = probe.itemInUseStack(player);
            if (
                itemInUseStack != nullptr &&
                !probe.stackIsNull(itemInUseStack) &&
                probe.stackMatchesForUse(itemInUseStack, offStack)
            ) {
                auto& session = currentActionSession();
                session.cancel();
                const ActionIdentityToken identity =
                    identityForActiveUseStack(itemInUseStack);
                if (
                    session.tryBegin(
                        ActionSessionKind::UsingItem,
                        ActionHand::OffHand,
                        identity,
                        kOffhandSlotIdentity,
                        0,
                        0
                    )
                ) {
                    gSessionPlayer = player;
                    gSessionGameMode = gameMode;
                    gSessionTargetIdentity = 0;
                    expected = false;
                    if (instance->mLoggedLongUse.compare_exchange_strong(
                            expected,
                            true,
                            std::memory_order_relaxed
                        )) {
                        __android_log_print(
                            ANDROID_LOG_INFO,
                            kLogTag,
                            "[HandActionRouter] OFFHAND long-use session locked until release"
                        );
                    }
                }
            }
        }
    }

    return result.handled;
}

bool HandActionRouter::attackDetour(
    void* gameMode,
    void* entity,
    bool playPredictiveSound,
    const void* hitPosition
) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mAttackOriginal == nullptr) {
        return false;
    }

    const auto original = reinterpret_cast<AttackFn>(instance->mAttackOriginal);
    if (
        !instance->featureEnabled() ||
        gInsideAttackDetour ||
        currentActionSession().active()
    ) {
        return original(gameMode, entity, playPredictiveSound, hitPosition);
    }

    auto& probe = NativeCapabilityProbe::instance();
    const void* player = probe.playerFromGameMode(gameMode);
    const void* mainStack = probe.mainhandStack(gameMode);
    if (player == nullptr || mainStack == nullptr) {
        return original(gameMode, entity, playPredictiveSound, hitPosition);
    }

    const void* offStack = probe.offhandStackForPlayer(player);
    const bool mainReal = probe.realCombatCapability(mainStack);
    const bool offReal =
        offStack != nullptr &&
        !probe.stackIsNull(offStack) &&
        probe.realCombatCapability(offStack);

    ScopedBool reentryGuard(gInsideAttackDetour);
    ScopedRoutedPlayer routedPlayer(player);

    const AttackRouteResult result = routeAttackAction(
        mainReal,
        offReal,
        [&]() noexcept {
            return original(gameMode, entity, playPredictiveSound, hitPosition);
        },
        [&]() noexcept {
            return original(gameMode, entity, playPredictiveSound, hitPosition);
        },
        [&]() noexcept {
            return original(gameMode, entity, playPredictiveSound, hitPosition);
        }
    );

    if (!result.usedFallback && result.hand == ActionHand::OffHand) {
        bool expected = false;
        if (instance->mLoggedOffhandAttack.compare_exchange_strong(
                expected,
                true,
                std::memory_order_relaxed
            )) {
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[HandActionRouter] attack selected OFFHAND real combat capability"
            );
        }
    }

    return result.nativeResult;
}

bool HandActionRouter::startDestroyBlockDetour(
    void* gameMode,
    const void* blockPos,
    unsigned char face,
    bool* hasDestroyedBlock
) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mStartDestroyBlockOriginal == nullptr) {
        return false;
    }

    const auto original = reinterpret_cast<StartDestroyBlockFn>(
        instance->mStartDestroyBlockOriginal
    );
    if (!instance->featureEnabled() || gInsideMiningDetour) {
        return original(gameMode, blockPos, face, hasDestroyedBlock);
    }

    auto& session = currentActionSession();
    if (session.active()) {
        if (session.kind() != ActionSessionKind::Mining) {
            return original(gameMode, blockPos, face, hasDestroyedBlock);
        }
        session.cancel();
        clearSessionIdentity();
    }

    auto& probe = NativeCapabilityProbe::instance();
    const void* player = probe.playerFromGameMode(gameMode);
    const void* mainStack = probe.mainhandStack(gameMode);
    if (
        player == nullptr ||
        mainStack == nullptr ||
        blockPos == nullptr ||
        hasDestroyedBlock == nullptr
    ) {
        return original(gameMode, blockPos, face, hasDestroyedBlock);
    }

    const void* targetBlock = probe.blockAt(player, blockPos);
    if (targetBlock == nullptr) {
        return original(gameMode, blockPos, face, hasDestroyedBlock);
    }

    const void* offStack = probe.offhandStackForPlayer(player);
    const bool mainSuitable = probe.realMiningCapability(mainStack, targetBlock);
    const bool offSuitable =
        offStack != nullptr &&
        !probe.stackIsNull(offStack) &&
        probe.realMiningCapability(offStack, targetBlock);

    ScopedBool reentryGuard(gInsideMiningDetour);
    ScopedRoutedPlayer routedPlayer(player);

    const MiningRouteResult result = routeMiningStart(
        mainSuitable,
        offSuitable,
        [&]() noexcept {
            return original(gameMode, blockPos, face, hasDestroyedBlock);
        },
        [&]() noexcept {
            return original(gameMode, blockPos, face, hasDestroyedBlock);
        },
        [&]() noexcept {
            return original(gameMode, blockPos, face, hasDestroyedBlock);
        },
        *hasDestroyedBlock
    );

    if (
        !result.usedFallback &&
        result.nativeResult &&
        result.hand == ActionHand::OffHand &&
        !*hasDestroyedBlock
    ) {
        const ActionIdentityToken stackIdentity = static_cast<ActionIdentityToken>(
            probe.stackItemIdentity(offStack)
        );
        const ActionTargetToken targetIdentity = targetIdentityForBlockPos(blockPos);
        if (
            stackIdentity != 0 &&
            session.tryBegin(
                ActionSessionKind::Mining,
                ActionHand::OffHand,
                stackIdentity,
                kOffhandSlotIdentity,
                targetIdentity,
                0
            )
        ) {
            gSessionPlayer = player;
            gSessionGameMode = gameMode;
            gSessionTargetIdentity = targetIdentity;

            bool expected = false;
            if (instance->mLoggedOffhandMining.compare_exchange_strong(
                    expected,
                    true,
                    std::memory_order_relaxed
                )) {
                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "[HandActionRouter] mining selected OFFHAND native destroy-speed capability"
                );
            }
        }
    }

    return result.nativeResult;
}

bool HandActionRouter::continueDestroyBlockDetour(
    void* gameMode,
    const void* blockPos,
    unsigned char face,
    const void* playerPos,
    bool* hasDestroyedBlock
) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mContinueDestroyBlockOriginal == nullptr) {
        return false;
    }

    const auto original = reinterpret_cast<ContinueDestroyBlockFn>(
        instance->mContinueDestroyBlockOriginal
    );
    if (!instance->featureEnabled() || gInsideMiningDetour) {
        return original(gameMode, blockPos, face, playerPos, hasDestroyedBlock);
    }

    auto& session = currentActionSession();
    if (
        !session.active() ||
        session.kind() != ActionSessionKind::Mining ||
        session.hand() != ActionHand::OffHand
    ) {
        return original(gameMode, blockPos, face, playerPos, hasDestroyedBlock);
    }

    auto& probe = NativeCapabilityProbe::instance();
    const void* player = probe.playerFromGameMode(gameMode);
    const void* offStack = probe.offhandStackForPlayer(player);
    const ActionTargetToken targetIdentity = targetIdentityForBlockPos(blockPos);
    const ActionIdentityToken stackIdentity = static_cast<ActionIdentityToken>(
        probe.stackItemIdentity(offStack)
    );
    if (
        gameMode != gSessionGameMode ||
        player == nullptr ||
        player != gSessionPlayer ||
        offStack == nullptr ||
        probe.stackIsNull(offStack) ||
        targetIdentity != gSessionTargetIdentity ||
        !session.matches(
            stackIdentity,
            kOffhandSlotIdentity,
            targetIdentity
        )
    ) {
        session.cancel();
        clearSessionIdentity();
        return original(gameMode, blockPos, face, playerPos, hasDestroyedBlock);
    }

    ScopedBool reentryGuard(gInsideMiningDetour);
    ScopedActionHand actionScope(ActionHand::OffHand, ActionKind::MineBlock);
    ScopedRoutedPlayer routedPlayer(player);
    const bool nativeResult = original(
        gameMode,
        blockPos,
        face,
        playerPos,
        hasDestroyedBlock
    );

    if (hasDestroyedBlock != nullptr && *hasDestroyedBlock) {
        session.finish();
        clearSessionIdentity();
    }
    return nativeResult;
}

bool HandActionRouter::destroyBlockDetour(
    void* gameMode,
    const void* blockPos,
    unsigned char face
) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mDestroyBlockOriginal == nullptr) {
        return false;
    }

    const auto original = reinterpret_cast<DestroyBlockFn>(
        instance->mDestroyBlockOriginal
    );
    if (!instance->featureEnabled() || gInsideMiningDetour) {
        return original(gameMode, blockPos, face);
    }

    auto& session = currentActionSession();
    if (
        !session.active() ||
        session.kind() != ActionSessionKind::Mining ||
        session.hand() != ActionHand::OffHand
    ) {
        return original(gameMode, blockPos, face);
    }

    auto& probe = NativeCapabilityProbe::instance();
    const void* player = probe.playerFromGameMode(gameMode);
    const void* offStack = probe.offhandStackForPlayer(player);
    const ActionTargetToken targetIdentity = targetIdentityForBlockPos(blockPos);
    const ActionIdentityToken stackIdentity = static_cast<ActionIdentityToken>(
        probe.stackItemIdentity(offStack)
    );
    if (
        gameMode != gSessionGameMode ||
        player == nullptr ||
        player != gSessionPlayer ||
        offStack == nullptr ||
        probe.stackIsNull(offStack) ||
        targetIdentity != gSessionTargetIdentity ||
        !session.matches(
            stackIdentity,
            kOffhandSlotIdentity,
            targetIdentity
        )
    ) {
        session.cancel();
        clearSessionIdentity();
        return original(gameMode, blockPos, face);
    }

    ScopedBool reentryGuard(gInsideMiningDetour);
    ScopedActionHand actionScope(ActionHand::OffHand, ActionKind::MineBlock);
    ScopedRoutedPlayer routedPlayer(player);
    const bool nativeResult = original(gameMode, blockPos, face);

    session.finish();
    clearSessionIdentity();
    return nativeResult;
}

void HandActionRouter::stopDestroyBlockDetour(
    void* gameMode,
    const void* blockPos
) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mStopDestroyBlockOriginal == nullptr) {
        return;
    }

    const auto original = reinterpret_cast<StopDestroyBlockFn>(
        instance->mStopDestroyBlockOriginal
    );
    if (!instance->featureEnabled() || gInsideMiningDetour) {
        original(gameMode, blockPos);
        return;
    }

    auto& session = currentActionSession();
    if (
        !session.active() ||
        session.kind() != ActionSessionKind::Mining ||
        session.hand() != ActionHand::OffHand
    ) {
        original(gameMode, blockPos);
        return;
    }

    auto& probe = NativeCapabilityProbe::instance();
    const void* player = probe.playerFromGameMode(gameMode);
    const void* offStack = probe.offhandStackForPlayer(player);
    const ActionTargetToken targetIdentity = targetIdentityForBlockPos(blockPos);
    const ActionIdentityToken stackIdentity = static_cast<ActionIdentityToken>(
        probe.stackItemIdentity(offStack)
    );
    if (
        gameMode != gSessionGameMode ||
        player == nullptr ||
        player != gSessionPlayer ||
        offStack == nullptr ||
        probe.stackIsNull(offStack) ||
        targetIdentity != gSessionTargetIdentity ||
        !session.matches(
            stackIdentity,
            kOffhandSlotIdentity,
            targetIdentity
        )
    ) {
        session.cancel();
        clearSessionIdentity();
        original(gameMode, blockPos);
        return;
    }

    {
        ScopedBool reentryGuard(gInsideMiningDetour);
        ScopedActionHand actionScope(ActionHand::OffHand, ActionKind::MineBlock);
        ScopedRoutedPlayer routedPlayer(player);
        original(gameMode, blockPos);
    }

    session.cancel();
    clearSessionIdentity();
}

const void* HandActionRouter::selectedItemDetour(
    const void* player
) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mSelectedItemOriginal == nullptr) {
        return nullptr;
    }

    const auto original = reinterpret_cast<SelectedItemFn>(
        instance->mSelectedItemOriginal
    );
    if (!instance->featureEnabled() || player == nullptr) {
        return original(player);
    }

    bool offhandOwned = false;
    const auto scoped = currentScopedAction();
    if (
        scoped.has_value() &&
        scoped->hand == ActionHand::OffHand &&
        player == gScopedPlayer
    ) {
        offhandOwned = true;
    }

    auto& session = currentActionSession();
    if (
        session.active() &&
        session.hand() == ActionHand::OffHand &&
        player == gSessionPlayer
    ) {
        offhandOwned = true;
    }

    if (!offhandOwned) {
        return original(player);
    }

    auto& probe = NativeCapabilityProbe::instance();
    const void* offStack = probe.offhandStackForPlayer(player);
    if (offStack == nullptr || probe.stackIsNull(offStack)) {
        if (session.active() && player == gSessionPlayer) {
            session.cancel();
            clearSessionIdentity();
        }
        return original(player);
    }

    if (session.active() && player == gSessionPlayer) {
        if (session.kind() == ActionSessionKind::UsingItem) {
            const void* itemInUseStack = probe.itemInUseStack(player);
            const ActionIdentityToken currentIdentity =
                identityForActiveUseStack(itemInUseStack);
            if (
                itemInUseStack == nullptr ||
                probe.stackIsNull(itemInUseStack) ||
                !probe.stackMatchesForUse(itemInUseStack, offStack) ||
                !session.matches(currentIdentity, kOffhandSlotIdentity, 0)
            ) {
                session.cancel();
                clearSessionIdentity();
                return original(player);
            }
        } else if (session.kind() == ActionSessionKind::Mining) {
            const ActionIdentityToken currentIdentity =
                static_cast<ActionIdentityToken>(probe.stackItemIdentity(offStack));
            if (
                !session.matches(
                    currentIdentity,
                    kOffhandSlotIdentity,
                    gSessionTargetIdentity
                )
            ) {
                session.cancel();
                clearSessionIdentity();
                return original(player);
            }
        } else {
            session.cancel();
            clearSessionIdentity();
            return original(player);
        }
    }

    return offStack;
}

void HandActionRouter::releaseUsingItemDetour(void* gameMode) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mReleaseUsingItemOriginal == nullptr) {
        return;
    }

    const auto original = reinterpret_cast<ReleaseUsingItemFn>(
        instance->mReleaseUsingItemOriginal
    );
    auto& session = currentActionSession();
    if (
        !instance->featureEnabled() ||
        !session.active() ||
        session.kind() != ActionSessionKind::UsingItem ||
        session.hand() != ActionHand::OffHand ||
        gameMode != gSessionGameMode ||
        gSessionPlayer == nullptr
    ) {
        original(gameMode);
        return;
    }

    {
        ScopedActionHand actionScope(ActionHand::OffHand, ActionKind::UseAir);
        ScopedRoutedPlayer routedPlayer(gSessionPlayer);
        original(gameMode);
    }

    session.finish();
    clearSessionIdentity();
}

void HandActionRouter::cancelActiveSession() noexcept {
    auto& session = currentActionSession();
    if (!session.active()) {
        clearSessionIdentity();
        return;
    }

    if (
        session.kind() == ActionSessionKind::UsingItem &&
        session.hand() == ActionHand::OffHand &&
        gSessionGameMode != nullptr &&
        gSessionPlayer != nullptr &&
        mReleaseUsingItemOriginal != nullptr
    ) {
        const auto release = reinterpret_cast<ReleaseUsingItemFn>(
            mReleaseUsingItemOriginal
        );
        ScopedActionHand actionScope(ActionHand::OffHand, ActionKind::UseAir);
        ScopedRoutedPlayer routedPlayer(gSessionPlayer);
        release(gSessionGameMode);
    }

    session.cancel();
    clearSessionIdentity();
}

} // namespace levioffhand::runtime