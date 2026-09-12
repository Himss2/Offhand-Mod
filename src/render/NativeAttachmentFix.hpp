#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace levioffhand::render::native_attachment_fix {
    inline constexpr std::uint32_t kMainhandSlot=5;
    inline constexpr std::uint32_t kOffhandSlot=6;
    inline constexpr std::uintptr_t kEffectiveOffhandDrawCallsiteRva=
        0x9B36370;
    inline constexpr std::uintptr_t kV2AttachmentDrawCallsiteRva=
        0xA2C87BC;
    inline constexpr std::size_t kBoneComposedMatrixOffset=0x30;
    inline constexpr std::size_t kBoneComposedMatrixSize=64;
    inline constexpr std::size_t kBoneLocalPoseOffset=0x70;
    inline constexpr std::size_t kBoneBindingModeOffset=0xDC;
    inline constexpr std::size_t kBoneMatrixCachedOffset=0xDE;
    inline constexpr std::uintptr_t kBindingModeFirstReadCallsiteRva=
        0x9B37780;
    inline constexpr std::uintptr_t kBindingModeSecondReadCallsiteRva=
        0x9B377D8;
    inline constexpr float kBowTppExtraLeftOffset=0.10F;

    struct LocalAttachmentPose {
        std::array<float,3> position{};
        std::array<float,3> rotation{};

        friend constexpr bool operator==(
            const LocalAttachmentPose&,
            const LocalAttachmentPose&
        ) noexcept =default;
    };

    static_assert(sizeof(LocalAttachmentPose)==24);
    static_assert(std::is_trivially_copyable_v<LocalAttachmentPose>);
    static_assert(offsetof(LocalAttachmentPose,position)==0);
    static_assert(offsetof(LocalAttachmentPose,rotation)==12);

    using LocalPoseMutator=bool(*)(LocalAttachmentPose&) noexcept;

    [[nodiscard]]
    inline bool validLocalPose(
        const LocalAttachmentPose& pose
    ) noexcept {
        for(const float value:pose.position) {
            if(!std::isfinite(value)) {
                return false;
            }
        }

        for(const float value:pose.rotation) {
            if(!std::isfinite(value)) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]]
    inline bool mirrorAndOffsetBowLocalPose(
        LocalAttachmentPose& pose
    ) noexcept {
        if(!validLocalPose(pose)) {
            return false;
        }

        const float mirroredX=-pose.position[0];
        pose.position[0]=
            mirroredX
            + std::copysign(kBowTppExtraLeftOffset,mirroredX);
        return true;
    }

    class ScopedLocalPoseOverride final {
    public:
        ScopedLocalPoseOverride(
            void* boneState,
            LocalPoseMutator mutator
        ) noexcept {
            if(!boneState || !mutator) {
                return;
            }

            mTarget=
                static_cast<std::byte*>(boneState)
                + kBoneLocalPoseOffset;
            std::memcpy(&mOriginal,mTarget,sizeof(mOriginal));

            LocalAttachmentPose corrected=mOriginal;
            if(!mutator(corrected)) {
                mTarget=nullptr;
                return;
            }

            mBoneState=static_cast<std::byte*>(boneState);
            std::memcpy(
                mOriginalComposedMatrix.data(),
                mBoneState+kBoneComposedMatrixOffset,
                mOriginalComposedMatrix.size()
            );
            std::memcpy(
                &mOriginalCacheFlag,
                mBoneState+kBoneMatrixCachedOffset,
                sizeof(mOriginalCacheFlag)
            );
            std::memcpy(mTarget,&corrected,sizeof(corrected));
            const std::uint8_t cacheInvalid=0;
            std::memcpy(
                mBoneState+kBoneMatrixCachedOffset,
                &cacheInvalid,
                sizeof(cacheInvalid)
            );
            mActive=true;
        }

        ~ScopedLocalPoseOverride() {
            if(mActive) {
                std::memcpy(mTarget,&mOriginal,sizeof(mOriginal));
                std::memcpy(
                    mBoneState+kBoneComposedMatrixOffset,
                    mOriginalComposedMatrix.data(),
                    mOriginalComposedMatrix.size()
                );
                std::memcpy(
                    mBoneState+kBoneMatrixCachedOffset,
                    &mOriginalCacheFlag,
                    sizeof(mOriginalCacheFlag)
                );
            }
        }

        ScopedLocalPoseOverride(const ScopedLocalPoseOverride&)=delete;
        ScopedLocalPoseOverride& operator=(
            const ScopedLocalPoseOverride&
        )=delete;
        ScopedLocalPoseOverride(ScopedLocalPoseOverride&&)=delete;
        ScopedLocalPoseOverride& operator=(ScopedLocalPoseOverride&&)=delete;

        [[nodiscard]]
        bool active() const noexcept {
            return mActive;
        }

    private:
        std::byte* mBoneState=nullptr;
        std::byte* mTarget=nullptr;
        LocalAttachmentPose mOriginal{};
        std::array<std::byte,kBoneComposedMatrixSize>
            mOriginalComposedMatrix{};
        std::uint8_t mOriginalCacheFlag=0;
        bool mActive=false;
    };

    template<std::size_t Size>
    [[nodiscard]]
    constexpr std::uint64_t fnv1(
        const char (&text)[Size]
    ) noexcept {
        std::uint64_t hash=0xCBF29CE484222325ULL;

        for(std::size_t index=0;index+1<Size;++index) {
            hash*=0x100000001B3ULL;
            hash^=static_cast<unsigned char>(text[index]);
        }

        return hash;
    }

    inline constexpr std::uint64_t kRightItemLowerHash=
        fnv1("rightitem");
    inline constexpr std::uint64_t kLeftItemLowerHash=
        fnv1("leftitem");
    inline constexpr std::uint64_t kRightItemCamelHash=
        fnv1("rightItem");
    inline constexpr std::uint64_t kLeftItemCamelHash=
        fnv1("leftItem");
    inline constexpr std::uint64_t kPoleBoneHash=
        fnv1("pole");

    [[nodiscard]]
    constexpr bool isRightOwnerBoneHash(
        std::uint64_t hash
    ) noexcept {
        return
            hash==kRightItemLowerHash
            || hash==kRightItemCamelHash;
    }

    [[nodiscard]]
    constexpr bool shouldForceTridentBindingResolve(
        bool isTrident,
        std::uint32_t slot,
        bool isFirstPerson,
        std::uintptr_t callsiteRva,
        std::uint64_t sourceHash,
        std::uint8_t nativeMode,
        bool alreadyResolved
    ) noexcept {
        return
            isTrident
            && slot==kOffhandSlot
            && isFirstPerson
            && callsiteRva==kBindingModeFirstReadCallsiteRva
            && isRightOwnerBoneHash(sourceHash)
            && nativeMode!=0
            && !alreadyResolved;
    }

    template<std::size_t Capacity>
    class ResolvedBindingCache final {
        static_assert(Capacity>0);

    public:
        void clear() noexcept {
            mEntries.fill(nullptr);
            mNextReplacement=0;
        }

        [[nodiscard]]
        bool synchronize(std::uint64_t generation) noexcept {
            if(mGeneration==generation) {
                return false;
            }

            clear();
            mGeneration=generation;
            return true;
        }

        [[nodiscard]]
        bool contains(const void* bindingState) const noexcept {
            if(!bindingState) {
                return false;
            }

            for(const void* resolved:mEntries) {
                if(resolved==bindingState) {
                    return true;
                }
            }

            return false;
        }

        [[nodiscard]]
        bool recordResolution(
            const void* bindingState,
            bool resolved
        ) noexcept {
            if(!resolved || !bindingState) {
                return false;
            }

            if(contains(bindingState)) {
                return true;
            }

            for(const void*& entry:mEntries) {
                if(!entry) {
                    entry=bindingState;
                    return true;
                }
            }

            mEntries[mNextReplacement]=bindingState;
            mNextReplacement=(mNextReplacement+1)%Capacity;
            return true;
        }

    private:
        std::array<const void*,Capacity> mEntries{};
        std::size_t mNextReplacement=0;
        std::uint64_t mGeneration=0;
    };

    // Reentrant reader admission for a published trampoline set. Closing the
    // gate rejects new threads, while nested calls on an already-admitted
    // thread remain valid until the outermost guard releases its reader.
    class ScopedHookRead final {
    public:
        ScopedHookRead(
            std::atomic_bool& ready,
            std::atomic_uint32_t& activeReaders
        ) noexcept {
            if(sDepth!=0) {
                if(sActiveReaders!=&activeReaders) {
                    return;
                }

                ++sDepth;
                mEntered=true;
                return;
            }

            if(!ready.load(std::memory_order_seq_cst)) {
                return;
            }

            activeReaders.fetch_add(1,std::memory_order_seq_cst);
            if(!ready.load(std::memory_order_seq_cst)) {
                activeReaders.fetch_sub(1,std::memory_order_seq_cst);
                return;
            }

            sActiveReaders=&activeReaders;
            sDepth=1;
            mEntered=true;
        }

        ~ScopedHookRead() {
            if(!mEntered) {
                return;
            }

            --sDepth;
            if(sDepth==0) {
                auto* activeReaders=sActiveReaders;
                sActiveReaders=nullptr;
                activeReaders->fetch_sub(1,std::memory_order_seq_cst);
            }
        }

        ScopedHookRead(const ScopedHookRead&)=delete;
        ScopedHookRead& operator=(const ScopedHookRead&)=delete;
        ScopedHookRead(ScopedHookRead&&)=delete;
        ScopedHookRead& operator=(ScopedHookRead&&)=delete;

        [[nodiscard]]
        bool entered() const noexcept {
            return mEntered;
        }

    private:
        inline static thread_local std::atomic_uint32_t*
            sActiveReaders=nullptr;
        inline static thread_local std::uint32_t sDepth=0;
        bool mEntered=false;
    };

    enum class OwnerBoneHashKind:std::uint8_t {
        RightItemLower,
        RightItemCamel,
        LeftItemLower,
        LeftItemCamel,
        Other
    };

    [[nodiscard]]
    constexpr OwnerBoneHashKind classifyOwnerBoneHash(
        std::uint64_t hash
    ) noexcept {
        if(hash==kRightItemLowerHash) {
            return OwnerBoneHashKind::RightItemLower;
        }

        if(hash==kRightItemCamelHash) {
            return OwnerBoneHashKind::RightItemCamel;
        }

        if(hash==kLeftItemLowerHash) {
            return OwnerBoneHashKind::LeftItemLower;
        }

        if(hash==kLeftItemCamelHash) {
            return OwnerBoneHashKind::LeftItemCamel;
        }

        return OwnerBoneHashKind::Other;
    }

    [[nodiscard]]
    constexpr bool consumeProbeBudget(
        std::uint32_t& emitted,
        std::uint32_t limit
    ) noexcept {
        if(emitted>=limit) {
            return false;
        }

        ++emitted;
        return true;
    }

    [[nodiscard]]
    constexpr bool shouldRemapBowOwnerBone(
        bool isBow,
        std::uint32_t slot,
        bool isFirstPerson,
        bool hooksReady
    ) noexcept {
        return
            isBow
            && slot==kOffhandSlot
            && !isFirstPerson
            && hooksReady;
    }

    [[nodiscard]]
    constexpr bool shouldRemapTridentOwnerBone(
        bool isTrident,
        std::uint32_t slot,
        bool isFirstPerson,
        bool hooksReady
    ) noexcept {
        return
            isTrident
            && slot==kOffhandSlot
            && isFirstPerson
            && hooksReady;
    }

    [[nodiscard]]
    constexpr bool isEffectiveOffhandDrawCallsite(
        std::uintptr_t callsiteRva
    ) noexcept {
        return
            callsiteRva==kEffectiveOffhandDrawCallsiteRva
            || callsiteRva==kV2AttachmentDrawCallsiteRva;
    }

    [[nodiscard]]
    constexpr bool shouldFixBowLocalPose(
        bool isBow,
        std::uint32_t slot,
        bool isFirstPerson
    ) noexcept {
        return
            isBow
            && slot==kOffhandSlot
            && !isFirstPerson;
    }

    [[nodiscard]]
    constexpr bool mapRightOwnerBoneToLeft(
        std::uint64_t sourceHash,
        std::uint64_t& mappedHash
    ) noexcept {
        if(sourceHash==kRightItemLowerHash) {
            mappedHash=kLeftItemLowerHash;
            return true;
        }

        if(sourceHash==kRightItemCamelHash) {
            mappedHash=kLeftItemCamelHash;
            return true;
        }

        return false;
    }

}
