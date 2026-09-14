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

    // Binary-proven ModelPart state layout for Minecraft Bedrock 1.26.45.1.
    // The composed matrix/cache constants are intentionally retained for
    // contracts proving the new local-pose override does not mutate them.
    inline constexpr std::size_t kBoneComposedMatrixOffset=0x30;
    inline constexpr std::size_t kBoneComposedMatrixSize=64;
    inline constexpr std::size_t kBoneLocalPoseOffset=0x70;
    inline constexpr std::size_t kBoneMatrixCachedOffset=0xDE;

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
    inline bool validLocalPose(const LocalAttachmentPose& pose) noexcept {
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

    // Mirror the *current animated* Trident pole pose across local X.  Bedrock
    // Euler rotations are applied X -> Y -> Z, so the reflected orientation
    // keeps RotX and negates RotY/RotZ.  This works for wield, raise, shake and
    // riptide poses without hard-coding any one animation frame.
    [[nodiscard]]
    inline bool mirrorTridentOffhandLocalPose(
        LocalAttachmentPose& pose
    ) noexcept {
        if(!validLocalPose(pose)) {
            return false;
        }

        pose.position[0]=-pose.position[0];
        pose.rotation[1]=-pose.rotation[1];
        pose.rotation[2]=-pose.rotation[2];
        return true;
    }

    // Temporary local-pose mutation around Minecraft's native F147ED0 compose.
    // Crucially this does NOT invalidate +0xDE and does NOT overwrite the
    // composed owner matrix at +0x30; Minecraft's native hand anchor stays live.
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

            std::memcpy(mTarget,&corrected,sizeof(corrected));
            mActive=true;
        }

        ~ScopedLocalPoseOverride() {
            if(mActive) {
                std::memcpy(mTarget,&mOriginal,sizeof(mOriginal));
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
        std::byte* mTarget=nullptr;
        LocalAttachmentPose mOriginal{};
        bool mActive=false;
    };

    template<std::size_t Size>
    [[nodiscard]]
    constexpr std::uint64_t fnv1(const char (&text)[Size]) noexcept {
        std::uint64_t hash=0xCBF29CE484222325ULL;
        for(std::size_t index=0;index+1<Size;++index) {
            hash*=0x100000001B3ULL;
            hash^=static_cast<unsigned char>(text[index]);
        }
        return hash;
    }

    inline constexpr std::uint64_t kRightItemLowerHash=fnv1("rightitem");
    inline constexpr std::uint64_t kLeftItemLowerHash=fnv1("leftitem");
    inline constexpr std::uint64_t kRightItemCamelHash=fnv1("rightItem");
    inline constexpr std::uint64_t kLeftItemCamelHash=fnv1("leftItem");
    inline constexpr std::uint64_t kPoleBoneHash=fnv1("pole");

    [[nodiscard]]
    constexpr bool shouldRemapBowOwnerBone(
        bool isBow,
        std::uint32_t slot,
        bool hooksReady
    ) noexcept {
        return isBow && slot==kOffhandSlot && hooksReady;
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

    // Reentrant reader admission for published prepare/resolver trampolines.
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
        inline static thread_local std::atomic_uint32_t* sActiveReaders=nullptr;
        inline static thread_local std::uint32_t sDepth=0;
        bool mEntered=false;
    };
}
