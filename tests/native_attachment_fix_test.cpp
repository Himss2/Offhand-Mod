#include "render/NativeAttachmentFix.hpp"

#include <algorithm>
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

    void testBowLocalPoseMirrorsAndAddsExtraLeftDistance() {
        fix::LocalAttachmentPose pose{
            {-0.50F, -3.0F, -2.0F},
            {152.0F, -9.0F, 25.0F}
        };

        assert(fix::mirrorAndOffsetBowLocalPose(pose));

        const fix::LocalAttachmentPose expected{
            {0.70F, -3.0F, -2.0F},
            {152.0F, -9.0F, 25.0F}
        };
        assert(std::fabs(pose.position[0]-expected.position[0])<1.0e-6F);
        assert(pose.position[1] == expected.position[1]);
        assert(pose.position[2] == expected.position[2]);
        assert(pose.rotation == expected.rotation);

        fix::LocalAttachmentPose oppositeSide{
            {0.50F, -3.0F, -2.0F},
            {152.0F, -9.0F, 25.0F}
        };

        assert(fix::mirrorAndOffsetBowLocalPose(oppositeSide));
        assert(std::fabs(oppositeSide.position[0]+0.70F)<1.0e-6F);
    }

    void testOwnerBoneHashClassificationForNativeProbe() {
        assert(
            fix::classifyOwnerBoneHash(fix::fnv1("rightitem"))
            == fix::OwnerBoneHashKind::RightItemLower
        );
        assert(
            fix::classifyOwnerBoneHash(fix::fnv1("rightItem"))
            == fix::OwnerBoneHashKind::RightItemCamel
        );
        assert(
            fix::classifyOwnerBoneHash(fix::fnv1("leftitem"))
            == fix::OwnerBoneHashKind::LeftItemLower
        );
        assert(
            fix::classifyOwnerBoneHash(fix::fnv1("leftItem"))
            == fix::OwnerBoneHashKind::LeftItemCamel
        );
        assert(
            fix::classifyOwnerBoneHash(fix::fnv1("pole"))
            == fix::OwnerBoneHashKind::Other
        );
    }

    void testProbeBudgetStopsLogSpamAtLimit() {
        std::uint32_t emitted=0;

        assert(fix::consumeProbeBudget(emitted,2));
        assert(emitted==1);
        assert(fix::consumeProbeBudget(emitted,2));
        assert(emitted==2);
        assert(!fix::consumeProbeBudget(emitted,2));
        assert(emitted==2);
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

        assert(!fix::mirrorAndOffsetBowLocalPose(pose));
        assert(pose == original);
        assert(!fix::mirrorAndRotateTridentLocalPose(pose));
        assert(pose == original);
    }

    void testScopedLocalPoseOverrideRestoresBoneState() {
        std::array<
            std::byte,
            fix::kBoneMatrixCachedOffset+1
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
                &fix::mirrorAndOffsetBowLocalPose
            );
            assert(override.active());

            fix::LocalAttachmentPose during{};
            std::memcpy(
                &during,
                boneState.data()+fix::kBoneLocalPoseOffset,
                sizeof(during)
            );
            assert(std::fabs(during.position[0]-4.20F)<1.0e-6F);
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

    void testScopedOverrideForcesRecomposeAndRestoresNativeCache() {
        std::array<
            std::byte,
            fix::kBoneMatrixCachedOffset+1
        > boneState{};
        const fix::LocalAttachmentPose originalPose{
            {-2.0F, 4.0F, 6.0F},
            {10.0F, 20.0F, 30.0F}
        };
        std::array<std::byte,fix::kBoneComposedMatrixSize> originalCache{};
        std::fill(originalCache.begin(),originalCache.end(),std::byte{0x2A});

        std::memcpy(
            boneState.data()+fix::kBoneLocalPoseOffset,
            &originalPose,
            sizeof(originalPose)
        );
        std::memcpy(
            boneState.data()+fix::kBoneComposedMatrixOffset,
            originalCache.data(),
            originalCache.size()
        );
        boneState[fix::kBoneMatrixCachedOffset]=std::byte{1};

        {
            fix::ScopedLocalPoseOverride override(
                boneState.data(),
                &fix::mirrorAndRotateTridentLocalPose
            );
            assert(override.active());
            assert(
                boneState[fix::kBoneMatrixCachedOffset]
                == std::byte{0}
            );

            std::fill(
                boneState.begin()+fix::kBoneComposedMatrixOffset,
                boneState.begin()+fix::kBoneComposedMatrixOffset
                    + fix::kBoneComposedMatrixSize,
                std::byte{0x5A}
            );
            boneState[fix::kBoneMatrixCachedOffset]=std::byte{1};
        }

        fix::LocalAttachmentPose restoredPose{};
        std::memcpy(
            &restoredPose,
            boneState.data()+fix::kBoneLocalPoseOffset,
            sizeof(restoredPose)
        );
        assert(restoredPose==originalPose);
        assert(
            std::equal(
                originalCache.begin(),
                originalCache.end(),
                boneState.begin()+fix::kBoneComposedMatrixOffset
            )
        );
        assert(
            boneState[fix::kBoneMatrixCachedOffset]
            == std::byte{1}
        );
    }

    void testInactiveLocalPoseOverrideLeavesBoneStateUntouched() {
        std::array<
            std::byte,
            fix::kBoneMatrixCachedOffset+1
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
    testBowLocalPoseMirrorsAndAddsExtraLeftDistance();
    testOwnerBoneHashClassificationForNativeProbe();
    testProbeBudgetStopsLogSpamAtLimit();
    testTridentLocalPoseMirrorsPositionAndTurnsPole();
    testInvalidLocalPoseIsNotMutated();
    testScopedLocalPoseOverrideRestoresBoneState();
    testScopedOverrideForcesRecomposeAndRestoresNativeCache();
    testInactiveLocalPoseOverrideLeavesBoneStateUntouched();
}
