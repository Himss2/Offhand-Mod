#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace levioffhand::render::native_attachment_fix {
    inline constexpr std::uint32_t kMainhandSlot=5;
    inline constexpr std::uint32_t kOffhandSlot=6;

    // Binary-proven ModelPart state layout for Minecraft Bedrock 1.26.45.1.
    inline constexpr std::size_t kBoneComposedMatrixOffset=0x30;
    inline constexpr std::size_t kBoneComposedMatrixSize=64;
    inline constexpr std::size_t kBoneLocalPoseOffset=0x70;
    inline constexpr std::size_t kBoneMatrixCachedOffset=0xDE;
    inline constexpr std::uintptr_t kNativeAttachmentHandEquipCallsiteRva=
        0x9B369C8;

    using Matrix4=std::array<float,16>;

    [[nodiscard]]
    constexpr Matrix4 identityMatrix() noexcept {
        return {
            1.0F,0.0F,0.0F,0.0F,
            0.0F,1.0F,0.0F,0.0F,
            0.0F,0.0F,1.0F,0.0F,
            0.0F,0.0F,0.0F,1.0F
        };
    }

    [[nodiscard]]
    constexpr Matrix4 mirrorXMatrix() noexcept {
        return {
            -1.0F,0.0F,0.0F,0.0F,
             0.0F,1.0F,0.0F,0.0F,
             0.0F,0.0F,1.0F,0.0F,
             0.0F,0.0F,0.0F,1.0F
        };
    }

    [[nodiscard]]
    inline bool validMatrix(const Matrix4& matrix) noexcept {
        for(const float value:matrix) {
            if(!std::isfinite(value)) {
                return false;
            }
        }
        return true;
    }

    // Bedrock Matrix objects in this renderer are column-major: translation is
    // stored in indices 12/13/14 and multiplication composes A * B.
    [[nodiscard]]
    constexpr Matrix4 multiplyMatrix(
        const Matrix4& a,
        const Matrix4& b
    ) noexcept {
        Matrix4 out{};
        for(std::size_t column=0;column<4;++column) {
            for(std::size_t row=0;row<4;++row) {
                float value=0.0F;
                for(std::size_t k=0;k<4;++k) {
                    value+=a[k*4+row]*b[column*4+k];
                }
                out[column*4+row]=value;
            }
        }
        return out;
    }

    [[nodiscard]]
    inline bool invertAffineMatrix(
        const Matrix4& matrix,
        Matrix4& inverse
    ) noexcept {
        if(!validMatrix(matrix)) {
            return false;
        }

        const float a00=matrix[0];
        const float a01=matrix[4];
        const float a02=matrix[8];
        const float a10=matrix[1];
        const float a11=matrix[5];
        const float a12=matrix[9];
        const float a20=matrix[2];
        const float a21=matrix[6];
        const float a22=matrix[10];

        const float determinant=
            a00*(a11*a22-a12*a21)
            -a01*(a10*a22-a12*a20)
            +a02*(a10*a21-a11*a20);
        if(!std::isfinite(determinant) || std::fabs(determinant)<1.0e-6F) {
            return false;
        }

        const float invDet=1.0F/determinant;
        const float r00=(a11*a22-a12*a21)*invDet;
        const float r01=(a02*a21-a01*a22)*invDet;
        const float r02=(a01*a12-a02*a11)*invDet;
        const float r10=(a12*a20-a10*a22)*invDet;
        const float r11=(a00*a22-a02*a20)*invDet;
        const float r12=(a02*a10-a00*a12)*invDet;
        const float r20=(a10*a21-a11*a20)*invDet;
        const float r21=(a01*a20-a00*a21)*invDet;
        const float r22=(a00*a11-a01*a10)*invDet;

        inverse={
            r00,r10,r20,0.0F,
            r01,r11,r21,0.0F,
            r02,r12,r22,0.0F,
            0.0F,0.0F,0.0F,1.0F
        };

        const float tx=matrix[12];
        const float ty=matrix[13];
        const float tz=matrix[14];
        inverse[12]=-(r00*tx+r01*ty+r02*tz);
        inverse[13]=-(r10*tx+r11*ty+r12*tz);
        inverse[14]=-(r20*tx+r21*ty+r22*tz);
        return validMatrix(inverse);
    }

    // F147ED0 has already produced M = Owner * Local.  Reflecting the complete
    // local result rather than its Euler input is:
    //
    //   M' = Owner * S * Owner^-1 * M * S
    //      = Owner * (S * Local * S)
    //
    // where S mirrors local X.  The two reflections retain a proper transform
    // determinant while moving the complete animated/pivoted attachment to the
    // opposite local-hand side.  The owner matrix itself is never modified.
    [[nodiscard]]
    inline bool mirrorComposedTransformInOwnerX(
        const Matrix4& owner,
        Matrix4& composed
    ) noexcept {
        if(!validMatrix(owner) || !validMatrix(composed)) {
            return false;
        }

        Matrix4 inverseOwner{};
        if(!invertAffineMatrix(owner,inverseOwner)) {
            return false;
        }

        const Matrix4 original=composed;
        const Matrix4 mirror=mirrorXMatrix();
        Matrix4 result=multiplyMatrix(owner,mirror);
        result=multiplyMatrix(result,inverseOwner);
        result=multiplyMatrix(result,original);
        result=multiplyMatrix(result,mirror);
        if(!validMatrix(result)) {
            return false;
        }
        composed=result;
        return true;
    }


    // Convert the player's fully animated right-item owner frame into the
    // bilateral left-side carrier while leaving the attachment-local matrix
    // untouched.  Spear/Trident local animation is composed *after* this
    // carrier is seeded, so their native 3D geometry and authored pole/spear
    // animation are not reflected a second time.
    [[nodiscard]]
    inline bool mirrorOwnerCarrierAcrossX(
        Matrix4& matrix
    ) noexcept {
        if(!validMatrix(matrix)) {
            return false;
        }
        const Matrix4 original=matrix;
        const Matrix4 mirror=mirrorXMatrix();
        Matrix4 result=multiplyMatrix(mirror,original);
        result=multiplyMatrix(result,mirror);
        if(!validMatrix(result)) {
            return false;
        }
        matrix=result;
        return true;
    }

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
    inline constexpr std::uint64_t kSpearBoneHash=fnv1("spear");

    [[nodiscard]]
    constexpr bool shouldRemapBowOwnerBone(
        bool isBow,
        std::uint32_t slot,
        bool hooksReady
    ) noexcept {
        return isBow && slot==kOffhandSlot && hooksReady;
    }

    // Device evidence from v0.2.64: Spear becomes a 2D inventory-style item
    // when sent through generic renderOffhandItem.  Only Bow is allowed to use
    // this generic FIRSTPERSON_LEFT route; Spear and Trident stay native 3D.
    [[nodiscard]]
    constexpr bool shouldRouteGenericLeftFirstPerson(
        bool featureEnabled,
        bool isBow,
        std::uint32_t slot,
        bool isFirstPerson
    ) noexcept {
        return
            featureEnabled
            && isBow
            && slot==kOffhandSlot
            && isFirstPerson;
    }

    [[nodiscard]]
    constexpr bool shouldAdmitNativeSpearFirstPerson(
        bool featureEnabled,
        bool isSpear,
        std::uint32_t slot,
        bool isFirstPerson,
        std::uintptr_t callsiteRva
    ) noexcept {
        return
            featureEnabled
            && isSpear
            && slot==kOffhandSlot
            && isFirstPerson
            && callsiteRva==kNativeAttachmentHandEquipCallsiteRva;
    }

    [[nodiscard]]
    constexpr bool isRightOwnerBoneHash(
        std::uint64_t hash
    ) noexcept {
        return hash==kRightItemLowerHash || hash==kRightItemCamelHash;
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
