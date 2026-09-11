#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace levioffhand::render::native_attachment_fix {
    inline constexpr std::uint32_t kMainhandSlot=5;
    inline constexpr std::uint32_t kOffhandSlot=6;
    inline constexpr std::uintptr_t kEffectiveOffhandDrawCallsiteRva=
        0x9B36370;
    inline constexpr std::uintptr_t kV2AttachmentDrawCallsiteRva=
        0xA2C87BC;
    inline constexpr float kBowTppRightOffset=0.20F;

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
    constexpr bool shouldOffsetBowPose(
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
    constexpr bool shouldFixTridentPose(
        bool isTrident,
        std::uint32_t slot,
        bool isFirstPerson
    ) noexcept {
        return
            isTrident
            && slot==kOffhandSlot
            && isFirstPerson;
    }

    template<typename MatrixValues>
    void offsetBowRight(
        MatrixValues& matrix,
        float distance=kBowTppRightOffset
    ) noexcept {
        // Device calibration established that the held-item semantic
        // horizontal axis is matrix column 1 (not column 0, which primarily
        // changed depth in v0.2.48).  Normalize it so attachment scale cannot
        // amplify the requested visual-right adjustment.
        const float x=matrix[4];
        const float y=matrix[5];
        const float z=matrix[6];
        const float lengthSquared=x*x+y*y+z*z;

        if(
            !std::isfinite(distance)
            || !std::isfinite(lengthSquared)
            || lengthSquared<=1.0e-12F
        ) {
            return;
        }

        const float scale=distance/std::sqrt(lengthSquared);

        matrix[12]+=x*scale;
        matrix[13]+=y*scale;
        matrix[14]+=z*scale;
    }

    template<typename MatrixValues>
    void rotateTridentPoleHeadUp(
        MatrixValues& matrix
    ) noexcept {
        // Local M * Rz(180 deg): flip the pole orientation while preserving
        // its absolute translation.  Hand placement is handled separately by
        // resolving the attachment owner from rightitem to leftitem.  Never
        // reflect matrix[12] here: that moved v0.2.48 outside the FPP frustum.
        for(std::size_t index=0;index<8;++index) {
            matrix[index]=-matrix[index];
        }
    }
}
