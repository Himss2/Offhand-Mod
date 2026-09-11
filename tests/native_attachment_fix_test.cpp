#include "render/NativeAttachmentFix.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>

namespace fix = levioffhand::render::native_attachment_fix;

namespace {
    void testKnownSlotHashes() {
        static_assert(
            fix::fnv1("main_hand") == 0xEF6FF81E3179CD3EULL
        );
        static_assert(
            fix::fnv1("off_hand") == 0x5D4C22812BA3AF8CULL
        );
        static_assert(
            fix::fnv1("rightitem") == 0x35397D324D54C8C4ULL
        );
        static_assert(
            fix::fnv1("leftitem") == 0x1CF3FDCBB0AB92F7ULL
        );
        static_assert(
            fix::fnv1("pole") == 0xACCBE87EB1D18FCBULL
        );
    }

    void testBowBindingScope() {
        assert(fix::shouldRemapBowOwnerBone(true, 6, false));
        assert(!fix::shouldRemapBowOwnerBone(true, 5, false));
        assert(!fix::shouldRemapBowOwnerBone(true, 6, true));
        assert(!fix::shouldRemapBowOwnerBone(false, 6, false));

        std::uint64_t mapped = 0;
        assert(fix::mapRightOwnerBoneToLeft(
            fix::fnv1("rightitem"), mapped
        ));
        assert(mapped == fix::fnv1("leftitem"));

        assert(fix::mapRightOwnerBoneToLeft(
            fix::fnv1("rightItem"), mapped
        ));
        assert(mapped == fix::fnv1("leftItem"));

        mapped = 0x1234;
        assert(!fix::mapRightOwnerBoneToLeft(
            fix::fnv1("pole"), mapped
        ));
        assert(mapped == 0x1234);
    }

    void testEffectiveOffhandDrawCallsite() {
        static_assert(
            fix::kEffectiveOffhandDrawCallsiteRva==0x9B36370
        );
        static_assert(
            fix::kV2AttachmentDrawCallsiteRva==0xA2C87BC
        );
        static_assert(
            fix::isEffectiveOffhandDrawCallsite(0x9B36370)
        );
        static_assert(
            !fix::isEffectiveOffhandDrawCallsite(0x9B36A18)
        );
        static_assert(
            fix::isEffectiveOffhandDrawCallsite(0xA2C87BC)
        );
        static_assert(
            !fix::isEffectiveOffhandDrawCallsite(0xA2C837C)
        );
    }

    void testBowPoseOffsetScope() {
        static_assert(fix::kBowTppRightOffset==0.20F);
        assert(fix::shouldOffsetBowPose(true, 6, false));
        assert(!fix::shouldOffsetBowPose(true, 5, false));
        assert(!fix::shouldOffsetBowPose(true, 6, true));
        assert(!fix::shouldOffsetBowPose(false, 6, false));
    }

    void testBowPoseOffset() {
        // The renderer's semantic horizontal axis is matrix column 1.  Point
        // that local axis along world +X so the expected screen-right shift
        // is unambiguous and cannot accidentally regress to column 0/depth.
        std::array<float, 16> matrix{
             0.0F, -2.0F,  0.0F,  0.0F,
             3.0F,  0.0F,  0.0F,  0.0F,
             0.0F,  0.0F,  4.0F,  0.0F,
             5.0F,  6.0F,  7.0F,  1.0F
        };

        fix::offsetBowRight(
            matrix,
            fix::kBowTppRightOffset
        );

        const std::array<float, 16> expected{
             0.0F, -2.0F,  0.0F,  0.0F,
             3.0F,  0.0F,  0.0F,  0.0F,
             0.0F,  0.0F,  4.0F,  0.0F,
             5.2F,  6.0F,  7.0F,  1.0F
        };

        for(std::size_t index=0;index<matrix.size();++index) {
            assert(std::fabs(matrix[index]-expected[index])<1.0e-6F);
        }

        std::array<float, 16> degenerate{};
        degenerate[12]=1.0F;
        fix::offsetBowRight(degenerate,0.20F);
        assert(degenerate[12]==1.0F);
    }

    void testTridentPoseScope() {
        assert(fix::shouldRemapTridentOwnerBone(true, 6, true));
        assert(!fix::shouldRemapTridentOwnerBone(true, 5, true));
        assert(!fix::shouldRemapTridentOwnerBone(true, 6, false));
        assert(!fix::shouldRemapTridentOwnerBone(false, 6, true));

        assert(fix::shouldFixTridentPose(true, 6, true));
        assert(!fix::shouldFixTridentPose(true, 5, true));
        assert(!fix::shouldFixTridentPose(true, 6, false));
        assert(!fix::shouldFixTridentPose(false, 6, true));
    }

    void testTridentMatrixCorrection() {
        std::array<float, 16> matrix{
             1.0F,  2.0F,  3.0F,  4.0F,
             5.0F,  6.0F,  7.0F,  8.0F,
             9.0F, 10.0F, 11.0F, 12.0F,
            13.0F, 14.0F, 15.0F, 16.0F
        };

        fix::rotateTridentPoleHeadUp(matrix);

        const std::array<float, 16> expected{
            -1.0F, -2.0F, -3.0F, -4.0F,
             -5.0F, -6.0F, -7.0F, -8.0F,
              9.0F, 10.0F, 11.0F, 12.0F,
             13.0F, 14.0F, 15.0F, 16.0F
        };

        assert(matrix == expected);
    }
}

int main() {
    testKnownSlotHashes();
    testBowBindingScope();
    testEffectiveOffhandDrawCallsite();
    testBowPoseOffsetScope();
    testBowPoseOffset();
    testTridentPoseScope();
    testTridentMatrixCorrection();
}
