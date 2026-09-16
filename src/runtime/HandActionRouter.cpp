#include "runtime/HandActionRouter.hpp"

#include "runtime/ActionHandContext.hpp"
#include "runtime/HandActionRouterCore.hpp"
#include "runtime/NativeCapabilityProbe.hpp"

#include <android/log.h>

#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <memory>

#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

namespace levioffhand::runtime {
namespace {

constexpr char kMinecraftLibrary[] = "libminecraftpe.so";
constexpr char kLogTag[] = "Levi Offhand";

constexpr std::uintptr_t kBaseUseItemRva = 0xEF75578;
constexpr std::uintptr_t kReleaseUsingItemRva = 0xEF76108;
constexpr ActionSlotToken kOffhandSlotIdentity = 34;

constexpr char kBaseUseItemSignature[] =
    "FF 43 04 D1 "
    "FD 7B 0D A9 "
    "FC 5F 0E A9 "
    "F6 57 0F A9";

constexpr char kReleaseUsingItemSignature[] =
    "FF 03 04 D1 "
    "FD 7B 0C A9 "
    "F7 6B 00 F9 "
    "F6 57 0E A9";

using BaseUseItemFn = bool (*)(void* gameMode, const void* itemStack);
using SelectedItemFn = const void* (*)(const void* player);
using ReleaseUsingItemFn = void (*)(void* gameMode);

thread_local const void* gScopedPlayer = nullptr;
thread_local const void* gSessionPlayer = nullptr;
thread_local void* gSessionGameMode = nullptr;
thread_local bool gInsideBaseUseDetour = false;

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

[[nodiscard]] std::uintptr_t resolveExactTarget(
    const char* signature,
    std::uintptr_t expectedRva
) noexcept {
    const std::uintptr_t target = pl::memory::resolveSignature(
        signature,
        kMinecraftLibrary
    );
    if (!belongsToMinecraft(target)) {
        return 0;
    }

    const std::uintptr_t base = moduleBaseOf(target);
    if (base == 0 || target < base || target - base != expectedRva) {
        return 0;
    }
    return target;
}

void clearSessionIdentity() noexcept {
    gSessionPlayer = nullptr;
    gSessionGameMode = nullptr;
}

[[nodiscard]] ActionIdentityToken identityForActiveUseStack(
    const void* itemInUseStack
) noexcept {
    return static_cast<ActionIdentityToken>(
        reinterpret_cast<std::uintptr_t>(itemInUseStack)
    );
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
            "[HandActionRouter] native hand access unavailable; Java-like actions disabled"
        );
        return false;
    }

    mTarget = resolveExactTarget(kBaseUseItemSignature, kBaseUseItemRva);
    mReleaseUsingItemTarget = resolveExactTarget(
        kReleaseUsingItemSignature,
        kReleaseUsingItemRva
    );
    mSelectedItemTarget = probe.selectedItemTarget();

    if (
        mTarget == 0 ||
        mReleaseUsingItemTarget == 0 ||
        !belongsToMinecraft(mSelectedItemTarget)
    ) {
        context.logger().warn(
            "[HandActionRouter] exact 1.26.45.1 action targets failed validation"
        );
        probe.uninstall(context);
        mTarget = 0;
        mReleaseUsingItemTarget = 0;
        mSelectedItemTarget = 0;
        return false;
    }

    sInstance = this;
    mOriginal = nullptr;
    mSelectedItemOriginal = nullptr;
    mReleaseUsingItemOriginal = nullptr;

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

    context.logger().info(
        "[HandActionRouter] Java-like item-use routing active: main -> offhand"
    );
    context.logger().info(
        "[HandActionRouter] baseUseItem RVA=0x{:x}, release RVA=0x{:x}",
        kBaseUseItemRva,
        kReleaseUsingItemRva
    );
    return true;
}

void HandActionRouter::uninstall(pl::mod::ModContext& context) noexcept {
    cancelActiveSession();
    mFeatureEnabled.store(false, std::memory_order_release);

    if (mBaseUseItemHook) {
        mBaseUseItemHook->reset();
        mBaseUseItemHook.reset();
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
    mSelectedItemOriginal = nullptr;
    mReleaseUsingItemOriginal = nullptr;
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
        mReleaseUsingItemHook != nullptr &&
        mReleaseUsingItemHook->installed() &&
        mSelectedItemHook != nullptr &&
        mSelectedItemHook->installed() &&
        mOriginal != nullptr &&
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
