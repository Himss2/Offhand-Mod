#pragma once

#include <array>
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
    inline constexpr std::size_t kBoneLocalPoseOffset=0x70;

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
    inline bool mirrorBowLocalPose(
        LocalAttachmentPose& pose
    ) noexcept {
        if(!validLocalPose(pose)) {
            return false;
        }

        pose.position[0]=-pose.position[0];
        return true;
    }

    [[nodiscard]]
    inline bool mirrorAndRotateTridentLocalPose(
        LocalAttachmentPose& pose
    ) noexcept {
        if(!validLocalPose(pose)) {
            return false;
        }

        pose.position[0]=-pose.position[0];
        pose.rotation[2]+=180.0F;
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
    constexpr bool shouldRemapBowOwnerBone(
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
    constexpr bool shouldRemapTridentOwnerBone(
        bool isTrident,
        std::uint32_t slot,
        bool isFirstPerson
    ) noexcept {
        return
            isTrident
            && slot==kOffhandSlot
            && isFirstPerson;
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

    [[nodiscard]]
    constexpr bool shouldFixTridentLocalPose(
        bool isTrident,
        std::uint32_t slot,
        bool isFirstPerson
    ) noexcept {
        return
            isTrident
            && slot==kOffhandSlot
            && isFirstPerson;
    }

}
