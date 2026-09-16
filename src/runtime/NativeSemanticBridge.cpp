#include "runtime/NativeSemanticBridge.hpp"

#include "runtime/ActionHandContext.hpp"
#include "runtime/NativeCapabilityProbe.hpp"

#include <android/log.h>

#include <array>
#include <cstddef>
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

constexpr std::uintptr_t kUpperUseDispatcherRva = 0x9432794;
constexpr std::uintptr_t kAttackCallbackRva = 0xEF886A8;
constexpr std::uintptr_t kDestroyRateContextRva = 0xF08CC44;
constexpr std::uintptr_t kBaseUseItemRva = 0xEF75578;
constexpr std::uintptr_t kReleaseUsingItemRva = 0xEF76108;
constexpr std::uintptr_t kPlayerGameModeGetterRva = 0xF0CD850;

constexpr std::size_t kAttackCallbackGameModeOffset = 0x08;
constexpr std::size_t kDestroyRateStackOffset = 0x10;
constexpr std::size_t kDestroyRateContextCopySize = 0x20;
constexpr std::size_t kDestroyRateActorContextOffset = 0x00;
constexpr std::size_t kActorFromEntityContextDelta = sizeof(void*);
constexpr ActionSlotToken kOffhandSlotIdentity = 34;

constexpr std::array<std::uint8_t, 16> kUpperUseFingerprint{
    0xFF, 0x43, 0x05, 0xD1, 0xFD, 0x7B, 0x0F, 0xA9,
    0xFC, 0x6F, 0x10, 0xA9, 0xFA, 0x67, 0x11, 0xA9,
};
constexpr std::array<std::uint8_t, 16> kAttackCallbackFingerprint{
    0xFF, 0xC3, 0x01, 0xD1, 0xFD, 0x7B, 0x03, 0xA9,
    0xF7, 0x23, 0x00, 0xF9, 0xF6, 0x57, 0x05, 0xA9,
};
constexpr std::array<std::uint8_t, 16> kDestroyRateFingerprint{
    0xFF, 0x03, 0x02, 0xD1, 0xE9, 0x23, 0x02, 0x6D,
    0xFD, 0x7B, 0x03, 0xA9, 0xF9, 0x23, 0x00, 0xF9,
};
constexpr std::array<std::uint8_t, 16> kReleaseUsingItemFingerprint{
    0xFF, 0x03, 0x04, 0xD1, 0xFD, 0x7B, 0x0C, 0xA9,
    0xF7, 0x6B, 0x00, 0xF9, 0xF6, 0x57, 0x0E, 0xA9,
};
constexpr std::array<std::uint8_t, 8> kPlayerGameModeGetterFingerprint{
    0x00, 0xF4, 0x44, 0xF9, 0xC0, 0x03, 0x5F, 0xD6,
};

using UpperUseFn = bool (*)(
    void* controller,
    const void* inputFlags,
    const void* interaction,
    const void* target
);
using AttackCallbackFn = void (*)(void* callbackObject);
using DestroyRateContextFn = float (*)(const void* context);
using SelectedItemFn = const void* (*)(const void* player);
using ReleaseUsingItemFn = void (*)(void* gameMode);
using PlayerGameModeGetterFn = void* (*)(const void* player);

thread_local bool gInsideUpperUse = false;
thread_local bool gInsideAttackCallback = false;
thread_local bool gCaptureUpperPlayer = false;
thread_local const void* gCapturedUpperPlayer = nullptr;
thread_local const void* gSemanticScopePlayer = nullptr;
thread_local const void* gBridgeUsePlayer = nullptr;
thread_local void* gBridgeUseGameMode = nullptr;
PlayerGameModeGetterFn gPlayerGameModeGetter = nullptr;

class ScopedBool final {
public:
    explicit ScopedBool(bool& value) noexcept
        : mValue(value), mPrevious(value) {
        mValue = true;
    }

    ~ScopedBool() noexcept {
        mValue = mPrevious;
    }

    ScopedBool(const ScopedBool&) = delete;
    ScopedBool& operator=(const ScopedBool&) = delete;

private:
    bool& mValue;
    bool mPrevious;
};

class ScopedSemanticPlayer final {
public:
    explicit ScopedSemanticPlayer(const void* player) noexcept
        : mPrevious(gSemanticScopePlayer) {
        gSemanticScopePlayer = player;
    }

    ~ScopedSemanticPlayer() noexcept {
        gSemanticScopePlayer = mPrevious;
    }

    ScopedSemanticPlayer(const ScopedSemanticPlayer&) = delete;
    ScopedSemanticPlayer& operator=(const ScopedSemanticPlayer&) = delete;

private:
    const void* mPrevious;
};

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

[[nodiscard]] bool belongsToMinecraft(std::uintptr_t address) noexcept {
    if (address == 0) {
        return false;
    }

    Dl_info info{};
    return dladdr(reinterpret_cast<void*>(address), &info) != 0 &&
        info.dli_fname != nullptr &&
        std::strstr(info.dli_fname, kMinecraftLibrary) != nullptr;
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

[[nodiscard]] bool objectBelongsToMinecraft(const void* object) noexcept {
    if (object == nullptr) {
        return false;
    }

    const void* vtable = nullptr;
    std::memcpy(&vtable, object, sizeof(vtable));
    return belongsToMinecraft(reinterpret_cast<std::uintptr_t>(vtable));
}

[[nodiscard]] void* gameModeForPlayer(const void* player) noexcept {
    if (player == nullptr || gPlayerGameModeGetter == nullptr) {
        return nullptr;
    }

    void* gameMode = gPlayerGameModeGetter(player);
    return objectBelongsToMinecraft(gameMode) ? gameMode : nullptr;
}

[[nodiscard]] void* gameModeFromAttackCallback(
    const void* callbackObject
) noexcept {
    if (callbackObject == nullptr) {
        return nullptr;
    }

    void* gameMode = nullptr;
    const auto* bytes = static_cast<const std::byte*>(callbackObject);
    std::memcpy(
        &gameMode,
        bytes + kAttackCallbackGameModeOffset,
        sizeof(gameMode)
    );
    return objectBelongsToMinecraft(gameMode) ? gameMode : nullptr;
}

[[nodiscard]] const void* playerFromDestroyRateContext(
    const void* context
) noexcept {
    if (context == nullptr) {
        return nullptr;
    }

    const void* entityContext = nullptr;
    const auto* bytes = static_cast<const std::byte*>(context);
    std::memcpy(
        &entityContext,
        bytes + kDestroyRateActorContextOffset,
        sizeof(entityContext)
    );
    if (entityContext == nullptr) {
        return nullptr;
    }

    const auto raw = reinterpret_cast<std::uintptr_t>(entityContext);
    if (raw < kActorFromEntityContextDelta) {
        return nullptr;
    }
    return reinterpret_cast<const void*>(raw - kActorFromEntityContextDelta);
}

[[nodiscard]] bool isOffhandMiningContext() noexcept {
    const auto scoped = currentScopedAction();
    if (
        scoped.has_value() &&
        scoped->hand == ActionHand::OffHand &&
        scoped->kind == ActionKind::MineBlock
    ) {
        return true;
    }

    auto& session = currentActionSession();
    return session.active() &&
        session.kind() == ActionSessionKind::Mining &&
        session.hand() == ActionHand::OffHand;
}

} // namespace

NativeSemanticBridge* NativeSemanticBridge::sInstance = nullptr;

NativeSemanticBridge::~NativeSemanticBridge() = default;

NativeSemanticBridge& NativeSemanticBridge::instance() noexcept {
    static NativeSemanticBridge value;
    return value;
}

bool NativeSemanticBridge::install(pl::mod::ModContext& context) noexcept {
    if (installed()) {
        return true;
    }

    auto& probe = NativeCapabilityProbe::instance();
    if (!probe.available()) {
        context.logger().warn(
            "[SemanticBridge] capability probe unavailable; semantic bridge disabled"
        );
        return false;
    }

    mUpperUseTarget = resolveExactTarget(
        kUpperUseDispatcherRva,
        kUpperUseFingerprint
    );
    mAttackCallbackTarget = resolveExactTarget(
        kAttackCallbackRva,
        kAttackCallbackFingerprint
    );
    mDestroyRateTarget = resolveExactTarget(
        kDestroyRateContextRva,
        kDestroyRateFingerprint
    );
    mReleaseUsingItemTarget = resolveExactTarget(
        kReleaseUsingItemRva,
        kReleaseUsingItemFingerprint
    );
    const auto gameModeGetterTarget = resolveExactTarget(
        kPlayerGameModeGetterRva,
        kPlayerGameModeGetterFingerprint
    );
    mSelectedItemTarget = probe.selectedItemTarget();

    if (
        mUpperUseTarget == 0 ||
        mAttackCallbackTarget == 0 ||
        mDestroyRateTarget == 0 ||
        mReleaseUsingItemTarget == 0 ||
        gameModeGetterTarget == 0 ||
        !belongsToMinecraft(mSelectedItemTarget)
    ) {
        context.logger().warn(
            "[SemanticBridge] exact 1.26.45.1 semantic targets failed validation"
        );
        mUpperUseTarget = 0;
        mAttackCallbackTarget = 0;
        mDestroyRateTarget = 0;
        mSelectedItemTarget = 0;
        mReleaseUsingItemTarget = 0;
        return false;
    }

    gPlayerGameModeGetter = reinterpret_cast<PlayerGameModeGetterFn>(
        gameModeGetterTarget
    );

    sInstance = this;
    mUpperUseOriginal = nullptr;
    mAttackCallbackOriginal = nullptr;
    mDestroyRateOriginal = nullptr;
    mSelectedItemOriginal = nullptr;
    mReleaseUsingItemOriginal = nullptr;

    // This selected-item adapter runs before the router's adapter. It redirects
    // only the explicitly owned semantic player; all other lookups continue
    // through the existing hook chain unchanged.
    mSelectedItemHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mSelectedItemTarget),
        reinterpret_cast<void*>(&NativeSemanticBridge::selectedItemDetour),
        &mSelectedItemOriginal,
        pl::memory::HookPriority::High
    );
    if (
        !mSelectedItemHook ||
        !mSelectedItemHook->installed() ||
        mSelectedItemOriginal == nullptr
    ) {
        context.logger().warn("[SemanticBridge] selected-item bridge hook failed");
        uninstall(context);
        return false;
    }

    mReleaseUsingItemHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mReleaseUsingItemTarget),
        reinterpret_cast<void*>(&NativeSemanticBridge::releaseUsingItemDetour),
        &mReleaseUsingItemOriginal,
        pl::memory::HookPriority::High
    );
    if (
        !mReleaseUsingItemHook ||
        !mReleaseUsingItemHook->installed() ||
        mReleaseUsingItemOriginal == nullptr
    ) {
        context.logger().warn("[SemanticBridge] release-use bridge hook failed");
        uninstall(context);
        return false;
    }

    mDestroyRateHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mDestroyRateTarget),
        reinterpret_cast<void*>(&NativeSemanticBridge::destroyRateContextDetour),
        &mDestroyRateOriginal,
        pl::memory::HookPriority::High
    );
    if (
        !mDestroyRateHook ||
        !mDestroyRateHook->installed() ||
        mDestroyRateOriginal == nullptr
    ) {
        context.logger().warn("[SemanticBridge] destroy-rate bridge hook failed");
        uninstall(context);
        return false;
    }

    mAttackCallbackHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mAttackCallbackTarget),
        reinterpret_cast<void*>(&NativeSemanticBridge::attackCallbackDetour),
        &mAttackCallbackOriginal,
        pl::memory::HookPriority::High
    );
    if (
        !mAttackCallbackHook ||
        !mAttackCallbackHook->installed() ||
        mAttackCallbackOriginal == nullptr
    ) {
        context.logger().warn("[SemanticBridge] deferred attack callback hook failed");
        uninstall(context);
        return false;
    }

    // Publish the broad upper-use entry last, after every support hook is live.
    mUpperUseHook = std::make_unique<pl::memory::HookHandle>(
        reinterpret_cast<void*>(mUpperUseTarget),
        reinterpret_cast<void*>(&NativeSemanticBridge::upperUseDetour),
        &mUpperUseOriginal,
        pl::memory::HookPriority::High
    );
    if (
        !mUpperUseHook ||
        !mUpperUseHook->installed() ||
        mUpperUseOriginal == nullptr
    ) {
        context.logger().warn("[SemanticBridge] upper-use dispatcher hook failed");
        uninstall(context);
        return false;
    }

    mFeatureEnabled.store(true, std::memory_order_release);
    mLoggedUpperUseOffhand.store(false, std::memory_order_relaxed);
    mLoggedDestroyRateOffhand.store(false, std::memory_order_relaxed);
    mLoggedAttackSelected.store(false, std::memory_order_relaxed);
    mLoggedAttackCallback.store(false, std::memory_order_relaxed);

    context.logger().info(
        "[SemanticBridge] upper-use + deferred-attack + destroy-rate bridge active"
    );
    context.logger().info(
        "[SemanticBridge] upper=0x{:x}, attackCallback=0x{:x}, destroyRate=0x{:x}, baseUse=0x{:x}",
        kUpperUseDispatcherRva,
        kAttackCallbackRva,
        kDestroyRateContextRva,
        kBaseUseItemRva
    );
    return true;
}

void NativeSemanticBridge::uninstall(pl::mod::ModContext& context) noexcept {
    mFeatureEnabled.store(false, std::memory_order_release);
    clearBridgeUseSession();

    if (mUpperUseHook) {
        mUpperUseHook->reset();
        mUpperUseHook.reset();
    }
    if (mAttackCallbackHook) {
        mAttackCallbackHook->reset();
        mAttackCallbackHook.reset();
    }
    if (mDestroyRateHook) {
        mDestroyRateHook->reset();
        mDestroyRateHook.reset();
    }
    if (mReleaseUsingItemHook) {
        mReleaseUsingItemHook->reset();
        mReleaseUsingItemHook.reset();
    }
    if (mSelectedItemHook) {
        mSelectedItemHook->reset();
        mSelectedItemHook.reset();
    }

    mUpperUseOriginal = nullptr;
    mAttackCallbackOriginal = nullptr;
    mDestroyRateOriginal = nullptr;
    mSelectedItemOriginal = nullptr;
    mReleaseUsingItemOriginal = nullptr;
    mUpperUseTarget = 0;
    mAttackCallbackTarget = 0;
    mDestroyRateTarget = 0;
    mSelectedItemTarget = 0;
    mReleaseUsingItemTarget = 0;
    sInstance = nullptr;
    gPlayerGameModeGetter = nullptr;

    gInsideUpperUse = false;
    gInsideAttackCallback = false;
    gCaptureUpperPlayer = false;
    gCapturedUpperPlayer = nullptr;
    gSemanticScopePlayer = nullptr;
    gBridgeUsePlayer = nullptr;
    gBridgeUseGameMode = nullptr;

    context.logger().info("[SemanticBridge] semantic hooks removed");
}

void NativeSemanticBridge::setFeatureEnabled(bool enabled) noexcept {
    if (!enabled) {
        clearBridgeUseSession();
    }
    mFeatureEnabled.store(enabled && installed(), std::memory_order_release);
}

bool NativeSemanticBridge::featureEnabled() const noexcept {
    return mFeatureEnabled.load(std::memory_order_acquire);
}

bool NativeSemanticBridge::installed() const noexcept {
    return
        mUpperUseHook != nullptr &&
        mUpperUseHook->installed() &&
        mAttackCallbackHook != nullptr &&
        mAttackCallbackHook->installed() &&
        mDestroyRateHook != nullptr &&
        mDestroyRateHook->installed() &&
        mSelectedItemHook != nullptr &&
        mSelectedItemHook->installed() &&
        mReleaseUsingItemHook != nullptr &&
        mReleaseUsingItemHook->installed() &&
        mUpperUseOriginal != nullptr &&
        mAttackCallbackOriginal != nullptr &&
        mDestroyRateOriginal != nullptr &&
        mSelectedItemOriginal != nullptr &&
        mReleaseUsingItemOriginal != nullptr &&
        gPlayerGameModeGetter != nullptr;
}

bool NativeSemanticBridge::upperUseDetour(
    void* controller,
    const void* inputFlags,
    const void* interaction,
    const void* target
) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mUpperUseOriginal == nullptr) {
        return false;
    }

    const auto original = reinterpret_cast<UpperUseFn>(instance->mUpperUseOriginal);
    if (
        !instance->featureEnabled() ||
        gInsideUpperUse ||
        currentActionSession().active()
    ) {
        return original(controller, inputFlags, interaction, target);
    }

    ScopedBool reentryGuard(gInsideUpperUse);

    const bool previousCapture = gCaptureUpperPlayer;
    const void* previousCaptured = gCapturedUpperPlayer;
    gCaptureUpperPlayer = true;
    gCapturedUpperPlayer = nullptr;

    const bool mainHandled = original(controller, inputFlags, interaction, target);
    const void* player = gCapturedUpperPlayer;

    gCaptureUpperPlayer = previousCapture;
    gCapturedUpperPlayer = previousCaptured;

    if (mainHandled || player == nullptr) {
        return mainHandled;
    }

    auto& probe = NativeCapabilityProbe::instance();
    const void* offStack = probe.offhandStackForPlayer(player);
    if (offStack == nullptr || probe.stackIsNull(offStack)) {
        return false;
    }

    bool offHandled = false;
    {
        ScopedSemanticPlayer semanticPlayer(player);
        ScopedActionHand actionScope(ActionHand::OffHand, ActionKind::UseAir);
        offHandled = original(controller, inputFlags, interaction, target);
    }

    if (!offHandled) {
        return false;
    }

    bool expected = false;
    if (instance->mLoggedUpperUseOffhand.compare_exchange_strong(
            expected,
            true,
            std::memory_order_relaxed
        )) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[SemanticBridge] upper-use OFFHAND retry handled"
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
            if (!session.active()) {
                void* gameMode = gameModeForPlayer(player);
                const ActionIdentityToken identity = static_cast<ActionIdentityToken>(
                    reinterpret_cast<std::uintptr_t>(itemInUseStack)
                );
                if (
                    gameMode != nullptr &&
                    identity != 0 &&
                    session.tryBegin(
                        ActionSessionKind::UsingItem,
                        ActionHand::OffHand,
                        identity,
                        kOffhandSlotIdentity,
                        0,
                        0
                    )
                ) {
                    gBridgeUsePlayer = player;
                    gBridgeUseGameMode = gameMode;
                }
            }
        }
    }

    return true;
}

void NativeSemanticBridge::attackCallbackDetour(
    void* callbackObject
) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mAttackCallbackOriginal == nullptr) {
        return;
    }

    const auto original = reinterpret_cast<AttackCallbackFn>(
        instance->mAttackCallbackOriginal
    );
    if (
        !instance->featureEnabled() ||
        gInsideAttackCallback ||
        callbackObject == nullptr ||
        currentActionSession().active()
    ) {
        original(callbackObject);
        return;
    }

    void* gameMode = gameModeFromAttackCallback(callbackObject);
    auto& probe = NativeCapabilityProbe::instance();
    const void* player = probe.playerFromGameMode(gameMode);
    const void* mainStack = probe.mainhandStack(gameMode);
    const void* offStack = probe.offhandStackForPlayer(player);

    const bool mainReal =
        mainStack != nullptr &&
        !probe.stackIsNull(mainStack) &&
        probe.realCombatCapability(mainStack);
    const bool offReal =
        offStack != nullptr &&
        !probe.stackIsNull(offStack) &&
        probe.realCombatCapability(offStack);

    if (player == nullptr || mainReal || !offReal) {
        original(callbackObject);
        return;
    }

    {
        ScopedBool reentryGuard(gInsideAttackCallback);
        ScopedSemanticPlayer semanticPlayer(player);
        ScopedActionHand actionScope(ActionHand::OffHand, ActionKind::AttackEntity);
        original(callbackObject);
    }

    bool expected = false;
    if (instance->mLoggedAttackCallback.compare_exchange_strong(
            expected,
            true,
            std::memory_order_relaxed
        )) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[SemanticBridge] attack callback scoped to OFFHAND"
        );
    }
}

float NativeSemanticBridge::destroyRateContextDetour(
    const void* context
) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mDestroyRateOriginal == nullptr) {
        return 0.0F;
    }

    const auto original = reinterpret_cast<DestroyRateContextFn>(
        instance->mDestroyRateOriginal
    );
    if (
        !instance->featureEnabled() ||
        context == nullptr ||
        !isOffhandMiningContext()
    ) {
        return original(context);
    }

    const void* player = playerFromDestroyRateContext(context);
    auto& probe = NativeCapabilityProbe::instance();
    const void* offStack = probe.offhandStackForPlayer(player);
    if (offStack == nullptr || probe.stackIsNull(offStack)) {
        return original(context);
    }

    std::array<std::byte, kDestroyRateContextCopySize> localContext{};
    std::memcpy(localContext.data(), context, localContext.size());
    std::memcpy(
        localContext.data() + kDestroyRateStackOffset,
        &offStack,
        sizeof(offStack)
    );

    const float result = original(localContext.data());

    bool expected = false;
    if (instance->mLoggedDestroyRateOffhand.compare_exchange_strong(
            expected,
            true,
            std::memory_order_relaxed
        )) {
        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[SemanticBridge] destroy-rate context redirected to OFFHAND"
        );
    }

    return result;
}

const void* NativeSemanticBridge::selectedItemDetour(
    const void* player
) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mSelectedItemOriginal == nullptr) {
        return nullptr;
    }

    const auto original = reinterpret_cast<SelectedItemFn>(
        instance->mSelectedItemOriginal
    );

    if (gCaptureUpperPlayer && player != nullptr) {
        gCapturedUpperPlayer = player;
    }

    if (!instance->featureEnabled() || player == nullptr) {
        return original(player);
    }

    bool offhandOwned = false;
    const auto scoped = currentScopedAction();
    if (
        scoped.has_value() &&
        scoped->hand == ActionHand::OffHand &&
        player == gSemanticScopePlayer
    ) {
        offhandOwned = true;
    }

    auto& session = currentActionSession();
    if (
        session.active() &&
        session.kind() == ActionSessionKind::UsingItem &&
        session.hand() == ActionHand::OffHand &&
        player == gBridgeUsePlayer
    ) {
        offhandOwned = true;
    }

    if (!offhandOwned) {
        return original(player);
    }

    auto& probe = NativeCapabilityProbe::instance();
    const void* offStack = probe.offhandStackForPlayer(player);
    if (offStack == nullptr || probe.stackIsNull(offStack)) {
        return original(player);
    }

    if (
        scoped.has_value() &&
        scoped->hand == ActionHand::OffHand &&
        scoped->kind == ActionKind::AttackEntity &&
        player == gSemanticScopePlayer
    ) {
        bool expected = false;
        if (instance->mLoggedAttackSelected.compare_exchange_strong(
                expected,
                true,
                std::memory_order_relaxed
            )) {
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[ActionDiag] attack selected-item bridge=OFFHAND"
            );
        }
    }

    return offStack;
}

void NativeSemanticBridge::releaseUsingItemDetour(void* gameMode) noexcept {
    auto* instance = sInstance;
    if (instance == nullptr || instance->mReleaseUsingItemOriginal == nullptr) {
        return;
    }

    const auto original = reinterpret_cast<ReleaseUsingItemFn>(
        instance->mReleaseUsingItemOriginal
    );
    auto& session = currentActionSession();

    const bool bridgeOwnsSession =
        instance->featureEnabled() &&
        gBridgeUsePlayer != nullptr &&
        gBridgeUseGameMode != nullptr &&
        gameMode == gBridgeUseGameMode &&
        session.active() &&
        session.kind() == ActionSessionKind::UsingItem &&
        session.hand() == ActionHand::OffHand;

    if (!bridgeOwnsSession) {
        original(gameMode);
        return;
    }

    {
        ScopedSemanticPlayer semanticPlayer(gBridgeUsePlayer);
        ScopedActionHand actionScope(ActionHand::OffHand, ActionKind::UseAir);
        original(gameMode);
    }

    session.finish();
    gBridgeUsePlayer = nullptr;
    gBridgeUseGameMode = nullptr;
}

void NativeSemanticBridge::clearBridgeUseSession() noexcept {
    if (gBridgeUsePlayer == nullptr && gBridgeUseGameMode == nullptr) {
        return;
    }

    auto& session = currentActionSession();
    if (
        session.active() &&
        session.kind() == ActionSessionKind::UsingItem &&
        session.hand() == ActionHand::OffHand
    ) {
        session.cancel();
    }

    gBridgeUsePlayer = nullptr;
    gBridgeUseGameMode = nullptr;
}

} // namespace levioffhand::runtime
