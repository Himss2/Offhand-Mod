#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

#include <pl/Mod.hpp>


namespace pl::memory {

class HookHandle;

} // namespace pl::memory


namespace levioffhand::runtime {


/*
 * Minecraft 1.26.45.1
 *
 * ContainerValidationResult:
 *
 * vector<ContainerValidationOperation> = 24 bytes
 * ContainerValidationOutcome          = 4 bytes
 * request action type                 = 1 byte
 * padding
 *
 * total = 32 bytes
 *
 * AArch64 therefore returns it through hidden x8 storage.
 */
struct alignas(8) ContainerValidationResultAbi {

    std::array<
        std::byte,
        32
    > storage{};
};


static_assert(
    sizeof(ContainerValidationResultAbi)
    ==
    32
);


static_assert(
    alignof(ContainerValidationResultAbi)
    ==
    8
);


class OffhandValidationHook final {

public:

    static
    OffhandValidationHook&
    instance() noexcept;


    ~OffhandValidationHook();


    bool install(
        pl::mod::ModContext& context
    ) noexcept;


    void uninstall(
        pl::mod::ModContext& context
    ) noexcept;


    void setFeatureEnabled(
        bool enabled
    ) noexcept;


    [[nodiscard]]
    bool featureEnabled()
        const noexcept;


    [[nodiscard]]
    bool installed()
        const noexcept;


private:

    OffhandValidationHook() =
        default;


    /*
     * UI/manual pre-validation path.
     *
     * Minecraft 1.26.45.1:
     *
     * RVA 0xF01AD14
     */
    static int
    manualSetPathDetour(

        void* arg0,

        const void* arg1,

        std::uint32_t arg2,

        const void* arg3,

        std::uint64_t arg4,

        std::uint64_t arg5,

        std::uint32_t arg6

    ) noexcept;


    /*
     * Automatic inventory destination/add path.
     *
     * Minecraft 1.26.45.1:
     *
     * RVA 0xF024024
     *
     * Crafting uses this path.
     *
     * While this function runs, arbitrary offhand permission
     * is explicitly disabled.
     */
    static int
    autoAddPathDetour(

        void* arg0,

        const void* arg1,

        const void* arg2,

        int amount,

        const void* arg4

    ) noexcept;


    /*
     * ContainerScreenValidation::tryTransfer
     *
     * RVA 0xF705380
     */
    static
    ContainerValidationResultAbi
    tryTransferDetour(

        void* self,

        const void* srcSlot,

        const void* dstSlot,

        int transferAmount,

        bool allowPartial

    ) noexcept;


    /*
     * ContainerScreenValidation::trySwap
     *
     * RVA 0xF704CA0
     */
    static
    ContainerValidationResultAbi
    trySwapDetour(

        void* self,

        const void* slotA,

        const void* slotB

    ) noexcept;


    /*
     * ItemStackBase::getAllowOffHand
     *
     * RVA 0xF644930
     */
    static bool
    allowOffhandDetour(

        const void* itemStackBase

    ) noexcept;


private:

    static
    OffhandValidationHook*
        sInstance;


    std::unique_ptr<
        pl::memory::HookHandle
    >
        mManualSetHook;


    std::unique_ptr<
        pl::memory::HookHandle
    >
        mAutoAddHook;


    std::unique_ptr<
        pl::memory::HookHandle
    >
        mTryTransferHook;


    std::unique_ptr<
        pl::memory::HookHandle
    >
        mTrySwapHook;


    std::unique_ptr<
        pl::memory::HookHandle
    >
        mAllowOffhandHook;


    void*
        mManualSetOriginal{
            nullptr
        };


    void*
        mAutoAddOriginal{
            nullptr
        };


    void*
        mTryTransferOriginal{
            nullptr
        };


    void*
        mTrySwapOriginal{
            nullptr
        };


    void*
        mAllowOffhandOriginal{
            nullptr
        };


    std::uintptr_t
        mManualSetTarget{
            0
        };


    std::uintptr_t
        mAutoAddTarget{
            0
        };


    std::uintptr_t
        mTryTransferTarget{
            0
        };


    std::uintptr_t
        mTrySwapTarget{
            0
        };


    std::uintptr_t
        mAllowOffhandTarget{
            0
        };


    std::atomic_bool
        mFeatureEnabled{
            true
        };


    /*
     * Diagnostics.
     */

    std::atomic_uint64_t
        mManualSetCalls{
            0
        };


    std::atomic_uint64_t
        mAutoAddCalls{
            0
        };


    std::atomic_uint64_t
        mTransferCalls{
            0
        };


    std::atomic_uint64_t
        mSwapCalls{
            0
        };


    std::atomic_uint64_t
        mScopedAllowCalls{
            0
        };


    std::atomic_uint64_t
        mVanillaAllowCalls{
            0
        };


    std::atomic_bool
        mManualLogged{
            false
        };


    std::atomic_bool
        mAutoAddLogged{
            false
        };


    std::atomic_bool
        mTransferLogged{
            false
        };


    std::atomic_bool
        mScopedAllowLogged{
            false
        };


    std::atomic_bool
        mVanillaAllowLogged{
            false
        };
};


} // namespace levioffhand::runtime
