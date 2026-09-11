#include "render/NativeAttachmentFix.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

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

    void testBowLocalPoseScope() {
        assert(fix::shouldFixBowLocalPose(true, 6, false));
        assert(!fix::shouldFixBowLocalPose(true, 5, false));
        assert(!fix::shouldFixBowLocalPose(true, 6, true));
        assert(!fix::shouldFixBowLocalPose(false, 6, false));
    }

    void testTridentPoseScope() {
        assert(fix::shouldRemapTridentOwnerBone(true, 6, true));
        assert(!fix::shouldRemapTridentOwnerBone(true, 5, true));
        assert(!fix::shouldRemapTridentOwnerBone(true, 6, false));
        assert(!fix::shouldRemapTridentOwnerBone(false, 6, true));

        assert(fix::shouldFixTridentLocalPose(true, 6, true));
        assert(!fix::shouldFixTridentLocalPose(true, 5, true));
        assert(!fix::shouldFixTridentLocalPose(true, 6, false));
        assert(!fix::shouldFixTridentLocalPose(false, 6, true));
    }

    void testBowLocalPoseMirrorsOnlyHorizontalPosition() {
        fix::LocalAttachmentPose pose{
            {-7.0F, -3.0F, -2.0F},
            {152.0F, -9.0F, 25.0F}
        };

        assert(fix::mirrorBowLocalPose(pose));

        const fix::LocalAttachmentPose expected{
            {7.0F, -3.0F, -2.0F},
            {152.0F, -9.0F, 25.0F}
        };
        assert(pose == expected);
    }

    void testTridentLocalPoseMirrorsPositionAndTurnsPole() {
        fix::LocalAttachmentPose pose{
            {-7.0F, -3.0F, -2.0F},
            {152.0F, -9.0F, 25.0F}
        };

        assert(fix::mirrorAndRotateTridentLocalPose(pose));

        const fix::LocalAttachmentPose expected{
            {7.0F, -3.0F, -2.0F},
            {152.0F, -9.0F, 205.0F}
        };
        assert(pose == expected);
    }

    void testInvalidLocalPoseIsNotMutated() {
        fix::LocalAttachmentPose pose{
            {std::numeric_limits<float>::infinity(), 2.0F, 3.0F},
            {4.0F, 5.0F, 6.0F}
        };
        const auto original=pose;

        assert(!fix::mirrorBowLocalPose(pose));
        assert(pose == original);
        assert(!fix::mirrorAndRotateTridentLocalPose(pose));
        assert(pose == original);
    }

    void testScopedLocalPoseOverrideRestoresBoneState() {
        std::array<
            std::byte,
            fix::kBoneLocalPoseOffset+sizeof(fix::LocalAttachmentPose)
        > boneState{};
        const fix::LocalAttachmentPose original{
            {-4.0F, 2.0F, 3.0F},
            {10.0F, 20.0F, 30.0F}
        };
        std::memcpy(
            boneState.data()+fix::kBoneLocalPoseOffset,
            &original,
            sizeof(original)
        );

        {
            fix::ScopedLocalPoseOverride override(
                boneState.data(),
                &fix::mirrorBowLocalPose
            );
            assert(override.active());

            fix::LocalAttachmentPose during{};
            std::memcpy(
                &during,
                boneState.data()+fix::kBoneLocalPoseOffset,
                sizeof(during)
            );
            assert(during.position[0] == 4.0F);
            assert(during.position[1] == 2.0F);
            assert(during.rotation[2] == 30.0F);
        }

        fix::LocalAttachmentPose restored{};
        std::memcpy(
            &restored,
            boneState.data()+fix::kBoneLocalPoseOffset,
            sizeof(restored)
        );
        assert(restored == original);
    }

    void testInactiveLocalPoseOverrideLeavesBoneStateUntouched() {
        std::array<
            std::byte,
            fix::kBoneLocalPoseOffset+sizeof(fix::LocalAttachmentPose)
        > boneState{};
        const fix::LocalAttachmentPose original{
            {1.0F, 2.0F, 3.0F},
            {4.0F, 5.0F, 6.0F}
        };
        std::memcpy(
            boneState.data()+fix::kBoneLocalPoseOffset,
            &original,
            sizeof(original)
        );

        fix::ScopedLocalPoseOverride override(boneState.data(),nullptr);
        assert(!override.active());

        fix::LocalAttachmentPose unchanged{};
        std::memcpy(
            &unchanged,
            boneState.data()+fix::kBoneLocalPoseOffset,
            sizeof(unchanged)
        );
        assert(unchanged == original);
    }
}

int main() {
    testKnownSlotHashes();
    testBowBindingScope();
    testEffectiveOffhandDrawCallsite();
    testBowLocalPoseScope();
    testTridentPoseScope();
    testBowLocalPoseMirrorsOnlyHorizontalPosition();
    testTridentLocalPoseMirrorsPositionAndTurnsPole();
    testInvalidLocalPoseIsNotMutated();
    testScopedLocalPoseOverrideRestoresBoneState();
    testInactiveLocalPoseOverrideLeavesBoneStateUntouched();
}
