#include "runtime/OffhandValidationHook.hpp"

#include <android/log.h>

#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <memory>

#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>


namespace levioffhand::runtime {

namespace {


constexpr char
    kMinecraftLibrary[] =
        "libminecraftpe.so";


constexpr char
    kLogTag[] =
        "Levi Offhand";


/*
 * ============================================================
 * TARGET BUILD
 * ============================================================
 *
 * Minecraft Bedrock Android 1.26.45.1
 *
 * Build ID:
 *
 * 868e275cb295e9a275bb29d2258edc2f7dc48761
 */


/*
 * ============================================================
 * MANUAL PRE-VALIDATION
 * ============================================================
 *
 * RVA:
 *
 *     0xF01AD14
 *
 * Verified unique in supplied binary.
 */
constexpr char
    kManualSetPathSignature[] =

        "FF 83 01 D1 "
        "FD 7B 02 A9 "
        "F7 1B 00 F9 "
        "F6 57 04 A9 "
        "F4 4F 05 A9 "
        "FD 83 00 91 "
        "57 D0 3B D5 "
        "A5 7C 40 92 "
        "F3 03 06 2A "
        "E8 16 40 F9 "
        "F5 03 00 AA "
        "F4 03 02 2A "
        "A8 83 1F F8";


/*
 * ============================================================
 * AUTO-ADD / CRAFT DESTINATION SEARCH
 * ============================================================
 *
 * RVA:
 *
 *     0xF024024
 *
 * v0.1.8 diagnostic showed crafting entering this path.
 *
 * Known callers include:
 *
 *     0xF023868
 *     0xF02E09C
 *     0xF03B900
 *
 *
 * Function returns int in W0.
 *
 * Observed args:
 *
 *     X0
 *     X1
 *     X2
 *     W3
 *     X4
 *
 *
 * Signature below is unique in supplied binary.
 */
constexpr char
    kAutoAddPathSignature[] =

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


/*
 * ============================================================
 * ContainerScreenValidation::tryTransfer
 * ============================================================
 *
 * RVA:
 *
 *     0xF705380
 *
 *
 * This is the second validation stage which caused the
 * v0.1.9 behaviour:
 *
 *     stone appears in offhand
 *     ↓
 *     transaction validation
 *     ↓
 *     vanilla false
 *     ↓
 *     rollback
 *
 *
 * Signature unique in supplied binary.
 */
constexpr char
    kTryTransferSignature[] =

        "FF 83 06 D1 "
        "FD 7B 14 A9 "
        "FC 6F 15 A9 "
        "FA 67 16 A9 "
        "F8 5F 17 A9 "
        "F6 57 18 A9 "
        "F4 4F 19 A9 "
        "FD 03 05 91 "
        "55 D0 3B D5 "
        "F3 03 08 AA";


/*
 * ============================================================
 * ContainerScreenValidation::trySwap
 * ============================================================
 *
 * RVA:
 *
 *     0xF704CA0
 */
constexpr char
    kTrySwapSignature[] =

        "FD 7B BA A9 "
        "FC 6F 01 A9 "
        "FA 67 02 A9 "
        "F8 5F 03 A9 "
        "F6 57 04 A9 "
        "F4 4F 05 A9 "
        "FD 03 00 91 "
        "FF 43 07 D1 "
        "5C D0 3B D5 "
        "F6 03 01 AA "
        "F4 03 00 AA "
        "F3 03 08 AA";


/*
 * ============================================================
 * ItemStackBase::getAllowOffHand
 * ============================================================
 *
 * RVA:
 *
 *     0xF644930
 *
 * Same working gate used since v0.1.2.
 */
constexpr char
    kAllowOffhandSignature[] =

        "08 04 40 F9 "
        "88 00 00 B4 "
        "00 01 40 F9 "
        "40 00 00 B4 "
        "D6 8B 00 14 "
        "E0 03 1F 2A "
        "C0 03 5F D6";


/*
 * ============================================================
 * Container names
 * ============================================================
 */
enum class ContainerName :
    std::uint8_t {

    AnvilResultPreview =
        2,

    SmithingResultPreview =
        5,

    CraftingOutputPreview =
        14,

    FurnaceResult =
        26,

    Hotbar =
        28,

    Inventory =
        29,

    TradeResultPreview =
        33,

    Offhand =
        34,

    CompoundCreatorOutputPreview =
        36,

    ElementConstructorOutputPreview =
        37,

    MaterialReducerOutput =
        39,

    LoomResultPreview =
        44,

    Trade2ResultPreview =
        49,

    GrindstoneResultPreview =
        52,

    StonecutterResultPreview =
        54,

    CartographyResultPreview =
        57,

    Cursor =
        59,

    CreatedOutput =
        60,

    Invalid =
        0xFF
};


/*
 * ============================================================
 * Scoped state
 * ============================================================
 *
 * Priority:
 *
 * AUTO-ADD / CRAFT
 *         ↓
 *      VANILLA
 *
 * otherwise:
 *
 * MANUAL PRECHECK or EXPLICIT TRANSACTION
 *         ↓
 *       ALLOW
 *
 *
 * All scopes are thread-local so unrelated Minecraft threads
 * cannot inherit permission.
 */
thread_local std::uint32_t
    gManualSetDepth =
        0;


thread_local std::uint32_t
    gAutoAddDepth =
        0;


thread_local std::uint32_t
    gTransferDepth =
        0;


/*
 * Diagnostic v2:
 *
 * Unique getAllowOffHand callers observed after an offhand
 * transfer/swap.
 *
 * Fixed storage means no allocations are performed inside this
 * hot hook.
 */
constexpr std::size_t
    kAllowProbeMaxCallers =
        96;


thread_local std::uintptr_t
    gAllowProbeCallers[
        kAllowProbeMaxCallers
    ]{};


thread_local std::size_t
    gAllowProbeCallerCount =
        0;


void
resetAllowProbe()
noexcept {

    gAllowProbeCallerCount =
        0;
}


[[nodiscard]]
bool
rememberAllowProbeCaller(
    std::uintptr_t callerRva
) noexcept {

    if (
        callerRva ==
        0
    ) {

        return false;
    }


    for (
        std::size_t i = 0;
        i < gAllowProbeCallerCount;
        ++i
    ) {

        if (
            gAllowProbeCallers[i]
            ==
            callerRva
        ) {

            return false;
        }
    }


    if (
        gAllowProbeCallerCount
        >=
        kAllowProbeMaxCallers
    ) {

        return false;
    }


    gAllowProbeCallers[
        gAllowProbeCallerCount++
    ] =
        callerRva;


    return true;
}


class ScopedDepth final {

public:

    explicit
    ScopedDepth(
        std::uint32_t& depth,
        bool active = true
    ) noexcept
        : mDepth(depth),
          mActive(active) {

        if (
            mActive
        ) {

            ++mDepth;
        }
    }


    ~ScopedDepth() {

        if (
            mActive
            &&
            mDepth != 0
        ) {

            --mDepth;
        }
    }


    ScopedDepth(
        const ScopedDepth&
    ) = delete;


    ScopedDepth&
    operator=(
        const ScopedDepth&
    ) = delete;


private:

    std::uint32_t&
        mDepth;


    bool
        mActive;
};


[[nodiscard]]
bool manualSetActive()
    noexcept {

    return
        gManualSetDepth != 0;
}


[[nodiscard]]
bool autoAddActive()
    noexcept {

    return
        gAutoAddDepth != 0;
}


[[nodiscard]]
bool transferScopeActive()
    noexcept {

    return
        gTransferDepth != 0;
}


/*
 * ============================================================
 * ContainerValidationSlotData
 * ============================================================
 *
 * FullContainerName is located first.
 *
 * Container enum is its first byte.
 *
 * This layout was already validated at runtime in v0.1.7:
 *
 * shield:
 *
 *     src=28
 *     dst=34
 */
[[nodiscard]]
ContainerName containerOf(
    const void* slotData
) noexcept {

    if (
        slotData ==
        nullptr
    ) {

        return
            ContainerName::Invalid;
    }


    return
        static_cast<
            ContainerName
        >(
            *static_cast<
                const std::uint8_t*
            >(
                slotData
            )
        );
}


/*
 * ============================================================
 * Result / automatically-generated containers
 * ============================================================
 */
[[nodiscard]]
bool isResultContainer(
    ContainerName name
) noexcept {

    switch (
        name
    ) {

    case
        ContainerName::
        AnvilResultPreview:

    case
        ContainerName::
        SmithingResultPreview:

    case
        ContainerName::
        CraftingOutputPreview:

    case
        ContainerName::
        FurnaceResult:

    case
        ContainerName::
        TradeResultPreview:

    case
        ContainerName::
        CompoundCreatorOutputPreview:

    case
        ContainerName::
        ElementConstructorOutputPreview:

    case
        ContainerName::
        MaterialReducerOutput:

    case
        ContainerName::
        LoomResultPreview:

    case
        ContainerName::
        Trade2ResultPreview:

    case
        ContainerName::
        GrindstoneResultPreview:

    case
        ContainerName::
        StonecutterResultPreview:

    case
        ContainerName::
        CartographyResultPreview:

    case
        ContainerName::
        CreatedOutput:

        return true;


    default:

        return false;
    }
}


/*
 * ============================================================
 * Explicit transfer permission
 * ============================================================
 */
[[nodiscard]]
bool shouldUnlockTransfer(
    ContainerName src,
    ContainerName dst
) noexcept {

    /*
     * Arbitrary item must always be removable from offhand.
     */
    if (
        src ==
        ContainerName::Offhand
    ) {

        return true;
    }


    /*
     * Not moving into offhand.
     */
    if (
        dst !=
        ContainerName::Offhand
    ) {

        return false;
    }


    /*
     * Never give arbitrary-offhand permission to an invalid /
     * pseudo automatic source.
     */
    if (
        src ==
        ContainerName::Invalid
    ) {

        return false;
    }


    /*
     * Most important crafting protection.
     */
    if (
        isResultContainer(
            src
        )
    ) {

        return false;
    }


    return true;
}


/*
 * ============================================================
 * Explicit swap permission
 * ============================================================
 */
[[nodiscard]]
bool shouldUnlockSwap(
    ContainerName slotA,
    ContainerName slotB
) noexcept {

    if (
        slotA ==
        ContainerName::Offhand
    ) {

        if (
            slotB ==
            ContainerName::Invalid
            ||
            isResultContainer(
                slotB
            )
        ) {

            return false;
        }


        return true;
    }


    if (
        slotB ==
        ContainerName::Offhand
    ) {

        if (
            slotA ==
            ContainerName::Invalid
            ||
            isResultContainer(
                slotA
            )
        ) {

            return false;
        }


        return true;
    }


    return false;
}


/*
 * ============================================================
 * Runtime safety
 * ============================================================
 */
[[nodiscard]]
bool belongsToMinecraft(
    std::uintptr_t address
) noexcept {

    if (
        address ==
        0
    ) {

        return false;
    }


    Dl_info
        info{};


    if (
        dladdr(
            reinterpret_cast<void*>(
                address
            ),
            &info
        )
        ==
        0
        ||
        info.dli_fname ==
        nullptr
    ) {

        return false;
    }


    return
        std::strstr(
            info.dli_fname,
            kMinecraftLibrary
        )
        != nullptr;
}


[[nodiscard]]
std::uintptr_t
moduleBaseOf(
    std::uintptr_t address
) noexcept {

    if (
        address ==
        0
    ) {

        return 0;
    }


    Dl_info
        info{};


    if (
        dladdr(
            reinterpret_cast<void*>(
                address
            ),
            &info
        )
        ==
        0
        ||
        info.dli_fbase ==
        nullptr
    ) {

        return 0;
    }


    return
        reinterpret_cast<
            std::uintptr_t
        >(
            info.dli_fbase
        );
}


/*
 * ============================================================
 * Original function ABIs
 * ============================================================
 */

using ManualSetPathFn =

    int (*)(
        void* arg0,
        const void* arg1,
        std::uint32_t arg2,
        const void* arg3,
        std::uint64_t arg4,
        std::uint64_t arg5,
        std::uint32_t arg6
    );


using AutoAddPathFn =

    int (*)(
        void* arg0,
        const void* arg1,
        const void* arg2,
        int amount,
        const void* arg4
    );


using TryTransferFn =

    ContainerValidationResultAbi (*)(
        void* self,
        const void* srcSlot,
        const void* dstSlot,
        int transferAmount,
        bool allowPartial
    );


using TrySwapFn =

    ContainerValidationResultAbi (*)(
        void* self,
        const void* slotA,
        const void* slotB
    );


using AllowOffhandFn =

    bool (*)(
        const void* itemStackBase
    );


} // namespace


OffhandValidationHook*
OffhandValidationHook::sInstance =
    nullptr;


OffhandValidationHook::
~OffhandValidationHook() =
    default;


OffhandValidationHook&
OffhandValidationHook::
instance() noexcept {

    static
        OffhandValidationHook
        instance;


    return instance;
}


bool
OffhandValidationHook::
install(
    pl::mod::ModContext& context
) noexcept {

    if (
        installed()
    ) {

        return true;
    }


    uninstall(
        context
    );


    resetAllowProbe();


    auto&
        logger =
            context.logger();


    /*
     * ========================================================
     * Reset diagnostics
     * ========================================================
     */

    mManualSetCalls.store(
        0,
        std::memory_order_relaxed
    );


    mAutoAddCalls.store(
        0,
        std::memory_order_relaxed
    );


    mTransferCalls.store(
        0,
        std::memory_order_relaxed
    );


    mSwapCalls.store(
        0,
        std::memory_order_relaxed
    );


    mScopedAllowCalls.store(
        0,
        std::memory_order_relaxed
    );


    mVanillaAllowCalls.store(
        0,
        std::memory_order_relaxed
    );


    mManualLogged.store(
        false,
        std::memory_order_relaxed
    );


    mAutoAddLogged.store(
        false,
        std::memory_order_relaxed
    );


    mTransferLogged.store(
        false,
        std::memory_order_relaxed
    );


    mScopedAllowLogged.store(
        false,
        std::memory_order_relaxed
    );


    mVanillaAllowLogged.store(
        false,
        std::memory_order_relaxed
    );


    /*
     * ========================================================
     * Resolve all exact functions
     * ========================================================
     */

    mManualSetTarget =
        pl::memory::
        resolveSignature(
            kManualSetPathSignature,
            kMinecraftLibrary
        );


    mAutoAddTarget =
        pl::memory::
        resolveSignature(
            kAutoAddPathSignature,
            kMinecraftLibrary
        );


    mTryTransferTarget =
        pl::memory::
        resolveSignature(
            kTryTransferSignature,
            kMinecraftLibrary
        );


    mTrySwapTarget =
        pl::memory::
        resolveSignature(
            kTrySwapSignature,
            kMinecraftLibrary
        );


    mAllowOffhandTarget =
        pl::memory::
        resolveSignature(
            kAllowOffhandSignature,
            kMinecraftLibrary
        );


    if (
        !belongsToMinecraft(
            mManualSetTarget
        )
        ||
        !belongsToMinecraft(
            mAutoAddTarget
        )
        ||
        !belongsToMinecraft(
            mTryTransferTarget
        )
        ||
        !belongsToMinecraft(
            mTrySwapTarget
        )
        ||
        !belongsToMinecraft(
            mAllowOffhandTarget
        )
    ) {

        logger.error(
            "Levi Offhand v0.1.10: "
            "Minecraft 1.26.45.1 target resolution failed"
        );


        uninstall(
            context
        );


        return false;
    }


    sInstance =
        this;


    /*
     * ========================================================
     * Hook manual pre-validation
     * ========================================================
     */

    mManualSetHook =
        std::make_unique<
            pl::memory::HookHandle
        >(
            reinterpret_cast<void*>(
                mManualSetTarget
            ),

            reinterpret_cast<void*>(
                &OffhandValidationHook::
                    manualSetPathDetour
            ),

            &mManualSetOriginal,

            pl::memory::
                HookPriority::Normal
        );


    if (
        !mManualSetHook
        ||
        !mManualSetHook->
            installed()
        ||
        mManualSetOriginal ==
            nullptr
    ) {

        logger.error(
            "v0.1.10: manual-set hook failed"
        );


        uninstall(
            context
        );


        return false;
    }


    /*
     * ========================================================
     * Hook auto-add/crafting path.
     * ========================================================
     */

    mAutoAddHook =
        std::make_unique<
            pl::memory::HookHandle
        >(
            reinterpret_cast<void*>(
                mAutoAddTarget
            ),

            reinterpret_cast<void*>(
                &OffhandValidationHook::
                    autoAddPathDetour
            ),

            &mAutoAddOriginal,

            pl::memory::
                HookPriority::Normal
        );


    if (
        !mAutoAddHook
        ||
        !mAutoAddHook->
            installed()
        ||
        mAutoAddOriginal ==
            nullptr
    ) {

        logger.error(
            "v0.1.10: auto-add hook failed"
        );


        uninstall(
            context
        );


        return false;
    }


    /*
     * ========================================================
     * Hook actual transfer validation.
     * ========================================================
     */

    mTryTransferHook =
        std::make_unique<
            pl::memory::HookHandle
        >(
            reinterpret_cast<void*>(
                mTryTransferTarget
            ),

            reinterpret_cast<void*>(
                &OffhandValidationHook::
                    tryTransferDetour
            ),

            &mTryTransferOriginal,

            pl::memory::
                HookPriority::Normal
        );


    if (
        !mTryTransferHook
        ||
        !mTryTransferHook->
            installed()
        ||
        mTryTransferOriginal ==
            nullptr
    ) {

        logger.error(
            "v0.1.10: tryTransfer hook failed"
        );


        uninstall(
            context
        );


        return false;
    }


    /*
     * ========================================================
     * Hook swap.
     * ========================================================
     */

    mTrySwapHook =
        std::make_unique<
            pl::memory::HookHandle
        >(
            reinterpret_cast<void*>(
                mTrySwapTarget
            ),

            reinterpret_cast<void*>(
                &OffhandValidationHook::
                    trySwapDetour
            ),

            &mTrySwapOriginal,

            pl::memory::
                HookPriority::Normal
        );


    if (
        !mTrySwapHook
        ||
        !mTrySwapHook->
            installed()
        ||
        mTrySwapOriginal ==
            nullptr
    ) {

        logger.error(
            "v0.1.10: trySwap hook failed"
        );


        uninstall(
            context
        );


        return false;
    }


    /*
     * ========================================================
     * Hook final item policy.
     * ========================================================
     */

    mAllowOffhandHook =
        std::make_unique<
            pl::memory::HookHandle
        >(
            reinterpret_cast<void*>(
                mAllowOffhandTarget
            ),

            reinterpret_cast<void*>(
                &OffhandValidationHook::
                    allowOffhandDetour
            ),

            &mAllowOffhandOriginal,

            pl::memory::
                HookPriority::Normal
        );


    if (
        !mAllowOffhandHook
        ||
        !mAllowOffhandHook->
            installed()
        ||
        mAllowOffhandOriginal ==
            nullptr
    ) {

        logger.error(
            "v0.1.10: getAllowOffHand hook failed"
        );


        uninstall(
            context
        );


        return false;
    }


    mFeatureEnabled.store(
        true,
        std::memory_order_release
    );


    logger.info(
        "Levi Offhand v0.1.10 storage active"
    );


    logger.info(
        "Stage 1 manual precheck = scoped ALLOW"
    );


    logger.info(
        "Stage 2 offhand transaction = scoped ALLOW"
    );


    logger.info(
        "Auto-add/crafting destination search = VANILLA"
    );


    logger.info(
        "Targets manual=0x{:x}, "
        "autoAdd=0x{:x}, "
        "transfer=0x{:x}, "
        "swap=0x{:x}, "
        "allow=0x{:x}",
        mManualSetTarget,
        mAutoAddTarget,
        mTryTransferTarget,
        mTrySwapTarget,
        mAllowOffhandTarget
    );


    return true;
}


void
OffhandValidationHook::
uninstall(
    pl::mod::ModContext& context
) noexcept {

    /*
     * Reverse installation order.
     */

    if (
        mAllowOffhandHook
    ) {

        mAllowOffhandHook->
            reset();

        mAllowOffhandHook.
            reset();
    }


    if (
        mTrySwapHook
    ) {

        mTrySwapHook->
            reset();

        mTrySwapHook.
            reset();
    }


    if (
        mTryTransferHook
    ) {

        mTryTransferHook->
            reset();

        mTryTransferHook.
            reset();
    }


    if (
        mAutoAddHook
    ) {

        mAutoAddHook->
            reset();

        mAutoAddHook.
            reset();
    }


    if (
        mManualSetHook
    ) {

        mManualSetHook->
            reset();

        mManualSetHook.
            reset();
    }


    if (
        sInstance ==
        this
    ) {

        sInstance =
            nullptr;
    }


    gManualSetDepth =
        0;


    gAutoAddDepth =
        0;


    gTransferDepth =
        0;


    resetAllowProbe();


    mManualSetOriginal =
        nullptr;


    mAutoAddOriginal =
        nullptr;


    mTryTransferOriginal =
        nullptr;


    mTrySwapOriginal =
        nullptr;


    mAllowOffhandOriginal =
        nullptr;


    mManualSetTarget =
        0;


    mAutoAddTarget =
        0;


    mTryTransferTarget =
        0;


    mTrySwapTarget =
        0;


    mAllowOffhandTarget =
        0;


    context.logger().info(
        "Levi Offhand v0.1.10 storage hooks removed"
    );
}


void
OffhandValidationHook::
setFeatureEnabled(
    bool enabled
) noexcept {

    mFeatureEnabled.store(
        enabled,
        std::memory_order_release
    );


    resetAllowProbe();


    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "Storage Unlock %s",
        enabled
            ? "ON"
            : "OFF"
    );
}


bool
OffhandValidationHook::
featureEnabled()
const noexcept {

    return
        mFeatureEnabled.load(
            std::memory_order_acquire
        );
}


bool
OffhandValidationHook::
installed()
const noexcept {

    return
        mManualSetHook !=
            nullptr
        &&
        mManualSetHook->
            installed()

        &&

        mAutoAddHook !=
            nullptr
        &&
        mAutoAddHook->
            installed()

        &&

        mTryTransferHook !=
            nullptr
        &&
        mTryTransferHook->
            installed()

        &&

        mTrySwapHook !=
            nullptr
        &&
        mTrySwapHook->
            installed()

        &&

        mAllowOffhandHook !=
            nullptr
        &&
        mAllowOffhandHook->
            installed();
}


/*
 * ============================================================
 * Stage 1 — manual pre-validation
 * ============================================================
 */

int
OffhandValidationHook::
manualSetPathDetour(
    void* arg0,
    const void* arg1,
    std::uint32_t arg2,
    const void* arg3,
    std::uint64_t arg4,
    std::uint64_t arg5,
    std::uint32_t arg6
) noexcept {

    auto*
        instance =
            sInstance;


    if (
        instance ==
            nullptr
        ||
        instance->
            mManualSetOriginal ==
            nullptr
    ) {

        return 3;
    }


    instance->
        mManualSetCalls.
        fetch_add(
            1,
            std::memory_order_relaxed
        );


    const auto
        original =

        reinterpret_cast<
            ManualSetPathFn
        >(
            instance->
                mManualSetOriginal
        );


    /*
     * Feature OFF:
     *
     * complete vanilla.
     */
    if (
        !instance->
            featureEnabled()
    ) {

        return
            original(
                arg0,
                arg1,
                arg2,
                arg3,
                arg4,
                arg5,
                arg6
            );
    }


    bool
        expected =
            false;


    if (
        instance->
        mManualLogged.
        compare_exchange_strong(
            expected,
            true,
            std::memory_order_relaxed
        )
    ) {

        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "Stage1 manual-set scope reached"
        );
    }


    ScopedDepth
        scope(
            gManualSetDepth
        );


    return
        original(
            arg0,
            arg1,
            arg2,
            arg3,
            arg4,
            arg5,
            arg6
        );
}


/*
 * ============================================================
 * Craft / auto-add path
 * ============================================================
 */

int
OffhandValidationHook::
autoAddPathDetour(
    void* arg0,
    const void* arg1,
    const void* arg2,
    int amount,
    const void* arg4
) noexcept {

    auto*
        instance =
            sInstance;


    if (
        instance ==
            nullptr
        ||
        instance->
            mAutoAddOriginal ==
            nullptr
    ) {

        return 0;
    }


    instance->
        mAutoAddCalls.
        fetch_add(
            1,
            std::memory_order_relaxed
        );


    const auto
        original =

        reinterpret_cast<
            AutoAddPathFn
        >(
            instance->
                mAutoAddOriginal
        );


    if (
        !instance->
            featureEnabled()
    ) {

        return
            original(
                arg0,
                arg1,
                arg2,
                amount,
                arg4
            );
    }


    bool
        expected =
            false;


    if (
        instance->
        mAutoAddLogged.
        compare_exchange_strong(
            expected,
            true,
            std::memory_order_relaxed
        )
    ) {

        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "Auto-add/crafting scope reached: "
            "forcing VANILLA offhand policy"
        );
    }


    /*
     * Highest-priority scope.
     *
     * Even if auto-add internally reaches a function that also
     * opens manualSetDepth, allowOffhandDetour checks this scope
     * FIRST and will use vanilla.
     */
    ScopedDepth
        scope(
            gAutoAddDepth
        );


    return
        original(
            arg0,
            arg1,
            arg2,
            amount,
            arg4
        );
}


/*
 * ============================================================
 * Stage 2 — ContainerScreenValidation::tryTransfer
 * ============================================================
 */

ContainerValidationResultAbi
OffhandValidationHook::
tryTransferDetour(
    void* self,
    const void* srcSlot,
    const void* dstSlot,
    int transferAmount,
    bool allowPartial
) noexcept {

    auto*
        instance =
            sInstance;


    if (
        instance ==
            nullptr
        ||
        instance->
            mTryTransferOriginal ==
            nullptr
    ) {

        return {};
    }


    instance->
        mTransferCalls.
        fetch_add(
            1,
            std::memory_order_relaxed
        );


    const auto
        original =

        reinterpret_cast<
            TryTransferFn
        >(
            instance->
                mTryTransferOriginal
        );


    const ContainerName
        src =
            containerOf(
                srcSlot
            );


    const ContainerName
        dst =
            containerOf(
                dstSlot
            );


    const bool
        touchesOffhand =

            src ==
                ContainerName::Offhand

            ||

            dst ==
                ContainerName::Offhand;


    const bool
        unlock =

            instance->
                featureEnabled()

            &&

            shouldUnlockTransfer(
                src,
                dst
            );


    if (
        touchesOffhand
    ) {

        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "tryTransfer: "
            "src=%u dst=%u amount=%d partial=%d policy=%s",
            static_cast<unsigned>(
                src
            ),
            static_cast<unsigned>(
                dst
            ),
            transferAmount,
            allowPartial
                ? 1
                : 0,
            unlock
                ? "ALLOW"
                : "VANILLA"
        );
    }


    if (
        unlock
    ) {

        bool
            expected =
                false;


        if (
            instance->
            mTransferLogged.
            compare_exchange_strong(
                expected,
                true,
                std::memory_order_relaxed
            )
        ) {

            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "Stage2 offhand transaction scope reached"
            );
        }
    }


    ScopedDepth
        scope(
            gTransferDepth,
            unlock
        );


    const auto result =
        original(
            self,
            srcSlot,
            dstSlot,
            transferAmount,
            allowPartial
        );


    if (
        touchesOffhand
    ) {

        resetAllowProbe();


        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[AllowProbeReset] transfer touched offhand"
        );
    }


    return result;
}


/*
 * ============================================================
 * Swap transaction
 * ============================================================
 */

ContainerValidationResultAbi
OffhandValidationHook::
trySwapDetour(
    void* self,
    const void* slotA,
    const void* slotB
) noexcept {

    auto*
        instance =
            sInstance;


    if (
        instance ==
            nullptr
        ||
        instance->
            mTrySwapOriginal ==
            nullptr
    ) {

        return {};
    }


    instance->
        mSwapCalls.
        fetch_add(
            1,
            std::memory_order_relaxed
        );


    const auto
        original =

        reinterpret_cast<
            TrySwapFn
        >(
            instance->
                mTrySwapOriginal
        );


    const ContainerName
        a =
            containerOf(
                slotA
            );


    const ContainerName
        b =
            containerOf(
                slotB
            );


    const bool
        touchesOffhand =

            a ==
                ContainerName::Offhand

            ||

            b ==
                ContainerName::Offhand;


    const bool
        unlock =

            instance->
                featureEnabled()

            &&

            shouldUnlockSwap(
                a,
                b
            );


    if (
        touchesOffhand
    ) {

        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "trySwap: "
            "A=%u B=%u policy=%s",
            static_cast<unsigned>(
                a
            ),
            static_cast<unsigned>(
                b
            ),
            unlock
                ? "ALLOW"
                : "VANILLA"
        );
    }


    ScopedDepth
        scope(
            gTransferDepth,
            unlock
        );


    const auto result =
        original(
            self,
            slotA,
            slotB
        );


    if (
        touchesOffhand
    ) {

        resetAllowProbe();


        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[AllowProbeReset] swap touched offhand"
        );
    }


    return result;
}


/*
 * ============================================================
 * Final policy
 * ============================================================
 */

bool
OffhandValidationHook::
allowOffhandDetour(
    const void* itemStackBase
) noexcept {

    auto*
        instance =
            sInstance;


    if (
        instance ==
            nullptr
        ||
        instance->
            mAllowOffhandOriginal ==
            nullptr
    ) {

        return false;
    }


    const auto
        original =

        reinterpret_cast<
            AllowOffhandFn
        >(
            instance->
                mAllowOffhandOriginal
        );


    /*
     * ========================================================
     * Feature disabled:
     *
     * completely vanilla.
     * ========================================================
     */
    if (
        !instance->
            featureEnabled()
    ) {

        return
            original(
                itemStackBase
            );
    }


    /*
     * ========================================================
     * HIGHEST PRIORITY:
     *
     * Crafting / auto-add.
     *
     * Never inherit arbitrary-offhand permission.
     * ========================================================
     */
    if (
        autoAddActive()
    ) {

        instance->
            mVanillaAllowCalls.
            fetch_add(
                1,
                std::memory_order_relaxed
            );


        return
            original(
                itemStackBase
            );
    }


    /*
     * ========================================================
     * Approved manual precheck OR approved transaction.
     * ========================================================
     */
    if (
        manualSetActive()
        ||
        transferScopeActive()
    ) {

        instance->
            mScopedAllowCalls.
            fetch_add(
                1,
                std::memory_order_relaxed
            );


        bool
            expected =
                false;


        if (
            instance->
            mScopedAllowLogged.
            compare_exchange_strong(
                expected,
                true,
                std::memory_order_relaxed
            )
        ) {

            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "Scoped getAllowOffHand: ALLOW"
            );
        }


        return true;
    }


    /*
     * ========================================================
     * Everything else:
     *
     * vanilla.
     * ========================================================
     */

    instance->
        mVanillaAllowCalls.
        fetch_add(
            1,
            std::memory_order_relaxed
        );


    bool
        expected =
            false;


    if (
        instance->
        mVanillaAllowLogged.
        compare_exchange_strong(
            expected,
            true,
            std::memory_order_relaxed
        )
    ) {

        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "getAllowOffHand outside scoped operation: VANILLA"
        );
    }


    const bool vanilla =
        original(
            itemStackBase
        );


    /*
     * Diagnostic v2 only.
     *
     * IMPORTANT:
     *
     * This does NOT modify vanilla's result.
     *
     * We only record Minecraft callers that receive false after
     * an offhand transaction. One of these may be the eligibility
     * gate that prevents unsupported items from entering
     * renderOffhandItem.
     */
    if (
        !vanilla
    ) {

        const auto caller =
            reinterpret_cast<
                std::uintptr_t
            >(
                __builtin_return_address(0)
            );


        if (
            belongsToMinecraft(
                caller
            )
        ) {

            const auto base =
                moduleBaseOf(
                    caller
                );


            const auto callerRva =

                base != 0
                &&
                caller >= base

                ?

                caller - base

                :

                0;


            if (
                rememberAllowProbeCaller(
                    callerRva
                )
            ) {

                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "[AllowProbe] "
                    "callerRva=0x%llX "
                    "stack=%p vanilla=0 "
                    "manual=%u transfer=%u autoAdd=%u",
                    static_cast<
                        unsigned long long
                    >(
                        callerRva
                    ),
                    itemStackBase,
                    manualSetActive()
                        ? 1u
                        : 0u,
                    transferScopeActive()
                        ? 1u
                        : 0u,
                    autoAddActive()
                        ? 1u
                        : 0u
                );
            }
        }
    }


    return vanilla;
}


} // namespace levioffhand::runtime
