#include "render/NativeAttachmentFix.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <thread>

namespace fix = levioffhand::render::native_attachment_fix;

namespace {
    bool applyDefaultBowTppPose(
        fix::LocalAttachmentPose& pose
    ) noexcept {
        return fix::mirrorAndOffsetBowLocalPose(
            pose,
            fix::kBowTppHorizontalDefault
        );
    }

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
        assert(fix::shouldRemapBowOwnerBone(true, 6, false, true));
        assert(!fix::shouldRemapBowOwnerBone(true, 5, false, true));
        assert(!fix::shouldRemapBowOwnerBone(true, 6, true, true));
        assert(!fix::shouldRemapBowOwnerBone(false, 6, false, true));
        assert(!fix::shouldRemapBowOwnerBone(true, 6, false, false));

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

    void testTridentLocalPoseScope() {
        assert(fix::shouldFixTridentLocalPose(true, 6, true));
        assert(!fix::shouldFixTridentLocalPose(true, 5, true));
        assert(!fix::shouldFixTridentLocalPose(true, 6, false));
        assert(!fix::shouldFixTridentLocalPose(false, 6, true));
    }

    void testTridentBindingScope() {
        assert(fix::shouldRemapTridentOwnerBone(true, 6, true, true));
        assert(!fix::shouldRemapTridentOwnerBone(true, 5, true, true));
        assert(!fix::shouldRemapTridentOwnerBone(true, 6, false, true));
        assert(!fix::shouldRemapTridentOwnerBone(false, 6, true, true));
        assert(!fix::shouldRemapTridentOwnerBone(true, 6, true, false));

        static_assert(fix::kBoneBindingModeOffset==0xDC);
        static_assert(
            fix::kBindingModeFirstReadCallsiteRva==0x9B37780
        );
        static_assert(
            fix::kBindingModeSecondReadCallsiteRva==0x9B377D8
        );

        constexpr auto shouldForce=[](
            bool isTrident,
            std::uint32_t slot,
            bool isFirstPerson,
            std::uintptr_t callsiteRva,
            std::uint64_t sourceHash,
            std::uint8_t nativeMode,
            bool alreadyResolved
        ) {
            return fix::shouldForceTridentBindingResolve(
                isTrident,
                slot,
                isFirstPerson,
                callsiteRva,
                sourceHash,
                nativeMode,
                alreadyResolved
            );
        };

        assert(shouldForce(
            true,6,true,0x9B37780,fix::fnv1("rightitem"),2,false
        ));
        assert(shouldForce(
            true,6,true,0x9B37780,fix::fnv1("rightItem"),1,false
        ));

        assert(!shouldForce(
            false,6,true,0x9B37780,fix::fnv1("rightitem"),2,false
        ));
        assert(!shouldForce(
            true,5,true,0x9B37780,fix::fnv1("rightitem"),2,false
        ));
        assert(!shouldForce(
            true,6,false,0x9B37780,fix::fnv1("rightitem"),2,false
        ));
        assert(!shouldForce(
            true,6,true,0x9B377D8,fix::fnv1("rightitem"),2,false
        ));
        assert(!shouldForce(
            true,6,true,0x9B37780,fix::fnv1("leftitem"),2,false
        ));
        assert(!shouldForce(
            true,6,true,0x9B37780,fix::fnv1("pole"),2,false
        ));
        assert(!shouldForce(
            true,6,true,0x9B37780,fix::fnv1("rightitem"),0,false
        ));
        assert(!shouldForce(
            true,6,true,0x9B37780,fix::fnv1("rightitem"),2,true
        ));
    }

    void testResolvedBindingCacheLifecycleAndRetry() {
        fix::ResolvedBindingCache<2> cache;
        int first=0;
        int second=0;
        int third=0;

        assert(cache.synchronize(10));
        assert(!cache.contains(&first));

        assert(!cache.recordResolution(&first,false));
        assert(!cache.contains(&first));
        assert(cache.recordResolution(&first,true));
        assert(cache.contains(&first));

        cache.clear();
        assert(!cache.contains(&first));
        assert(cache.recordResolution(&first,true));

        assert(!cache.synchronize(10));
        assert(cache.contains(&first));
        assert(cache.synchronize(11));
        assert(!cache.contains(&first));

        assert(cache.recordResolution(&first,true));
        assert(cache.recordResolution(&second,true));
        assert(cache.recordResolution(&third,true));
        assert(!cache.contains(&first));
        assert(cache.contains(&second));
        assert(cache.contains(&third));
    }

    void testScopedHookReadPublishesAndReleasesReaders() {
        std::atomic_bool ready{false};
        std::atomic_uint32_t activeReaders{0};

        {
            fix::ScopedHookRead read(ready,activeReaders);
            assert(!read.entered());
            assert(activeReaders.load()==0);
        }

        ready.store(true);
        {
            fix::ScopedHookRead read(ready,activeReaders);
            assert(read.entered());
            assert(activeReaders.load()==1);

            ready.store(false);
            assert(activeReaders.load()==1);

            {
                fix::ScopedHookRead nested(ready,activeReaders);
                assert(nested.entered());
                assert(activeReaders.load()==1);
            }

            assert(activeReaders.load()==1);
        }

        assert(activeReaders.load()==0);

        fix::ScopedHookRead rejectedAfterClose(ready,activeReaders);
        assert(!rejectedAfterClose.entered());
        assert(activeReaders.load()==0);
    }

    void testScopedHookReadKeepsNestedForwardingDuringConcurrentClose() {
        std::atomic_bool forwardingAvailable{true};
        std::atomic_uint32_t activeReaders{0};
        std::atomic_bool outerEntered{false};
        std::atomic_bool allowNested{false};
        std::atomic_bool nestedEntered{false};
        std::atomic_bool nestedChecked{false};
        std::atomic_bool releaseNested{false};

        std::thread renderThread([&] {
            fix::ScopedHookRead outer(
                forwardingAvailable,
                activeReaders
            );
            assert(outer.entered());
            outerEntered.store(true,std::memory_order_release);

            while(!allowNested.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            fix::ScopedHookRead nested(
                forwardingAvailable,
                activeReaders
            );
            nestedEntered.store(
                nested.entered(),
                std::memory_order_release
            );
            nestedChecked.store(true,std::memory_order_release);

            while(!releaseNested.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
        });

        while(!outerEntered.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        forwardingAvailable.store(false,std::memory_order_seq_cst);
        allowNested.store(true,std::memory_order_release);

        while(!nestedChecked.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }

        assert(nestedEntered.load(std::memory_order_acquire));
        assert(activeReaders.load(std::memory_order_seq_cst)==1);
        releaseNested.store(true,std::memory_order_release);
        renderThread.join();
        assert(activeReaders.load(std::memory_order_seq_cst)==0);
    }

    void testBowTppSliderControlsOnlyHorizontalDistance() {
        fix::LocalAttachmentPose pose{
            {-0.50F, -3.0F, -2.0F},
            {152.0F, -9.0F, 25.0F}
        };

        assert(fix::mirrorAndOffsetBowLocalPose(pose,0.15F));

        const fix::LocalAttachmentPose expected{
            {0.65F, -3.0F, -2.0F},
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

        assert(fix::mirrorAndOffsetBowLocalPose(oppositeSide,0.15F));
        assert(std::fabs(oppositeSide.position[0]+0.65F)<1.0e-6F);
    }

    void testBowTppSliderValueIsBounded() {
        static_assert(fix::kBowTppHorizontalMin==0.10F);
        static_assert(fix::kBowTppHorizontalMax==0.20F);
        static_assert(fix::kBowTppHorizontalDefault==0.10F);

        assert(
            std::fabs(
                fix::normalizeBowTppHorizontalOffset(0.05F)-0.10F
            )<1.0e-6F
        );
        assert(
            std::fabs(
                fix::normalizeBowTppHorizontalOffset(0.15F)-0.15F
            )<1.0e-6F
        );
        assert(
            std::fabs(
                fix::normalizeBowTppHorizontalOffset(0.25F)-0.20F
            )<1.0e-6F
        );
        assert(
            std::fabs(
                fix::normalizeBowTppHorizontalOffset(
                    std::numeric_limits<float>::quiet_NaN()
                )-0.10F
            )<1.0e-6F
        );
    }

    void testTridentLocalPoseMovesLeftAndTurnsHeadUp() {
        fix::LocalAttachmentPose pose{
            {-7.0F, -3.0F, -2.0F},
            {152.0F, -9.0F, 25.0F}
        };

        assert(fix::mirrorAndRotateTridentLocalPose(pose));

        const fix::LocalAttachmentPose expected{
            {7.0F, -3.0F, -2.0F},
            {152.0F, -9.0F, 205.0F}
        };
        assert(pose==expected);
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


    void testBowTppTiltClamp() {
        assert(std::fabs(fix::normalizeBowTppTiltDegrees(-60.0F)+45.0F)<1.0e-6F);
        assert(std::fabs(fix::normalizeBowTppTiltDegrees(60.0F)-45.0F)<1.0e-6F);
        assert(std::fabs(fix::normalizeBowTppTiltDegrees(-25.2F)+25.2F)<1.0e-6F);
    }

    void testTridentFppHorizontalClamp() {
        assert(fix::normalizeTridentFppHorizontalOffset(-9.0F)==-1.5F);
        assert(fix::normalizeTridentFppHorizontalOffset(9.0F)==1.5F);
        assert(fix::normalizeTridentFppHorizontalOffset(0.875F)==0.875F);
        assert(
            fix::normalizeTridentFppHorizontalOffset(
                std::numeric_limits<float>::infinity()
            )==fix::kTridentFppHorizontalDefault
        );
    }

    void testTridentPostComposeRotationPreservesTranslation() {
        std::array<float,16> matrix{
            1.0F,2.0F,3.0F,4.0F,
            5.0F,6.0F,7.0F,8.0F,
            9.0F,10.0F,11.0F,12.0F,
            13.0F,14.0F,15.0F,16.0F
        };
        assert(fix::rotateTridentPoleHeadUp(matrix.data()));
        assert(matrix[0]==-1.0F);
        assert(matrix[7]==-8.0F);
        assert(matrix[8]==9.0F);
        assert(matrix[12]==13.0F);
        assert(matrix[13]==14.0F);
        assert(matrix[14]==15.0F);
    }

    void testTridentExpressionBindingMapsRightOwnerToLeftItem() {
        assert(
            fix::tridentOffhandExpressionOwnerHash(
                fix::kRightItemCamelHash
            )==fix::kLeftItemCamelHash
        );
        assert(
            fix::tridentOffhandExpressionOwnerHash(
                fix::kRightItemLowerHash
            )==fix::kLeftItemCamelHash
        );
        assert(
            fix::tridentOffhandExpressionOwnerHash(0x1234ULL)==fix::kLeftItemCamelHash
        );
    }

    void testInvalidLocalPoseIsNotMutated() {
        fix::LocalAttachmentPose pose{
            {std::numeric_limits<float>::infinity(), 2.0F, 3.0F},
            {4.0F, 5.0F, 6.0F}
        };
        const auto original=pose;

        assert(!fix::mirrorAndOffsetBowLocalPose(pose,0.15F));
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
                &applyDefaultBowTppPose
            );
            assert(override.active());

            fix::LocalAttachmentPose during{};
            std::memcpy(
                &during,
                boneState.data()+fix::kBoneLocalPoseOffset,
                sizeof(during)
            );
            assert(std::fabs(during.position[0]-4.10F)<1.0e-6F);
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
    testTridentLocalPoseScope();
    testTridentBindingScope();
    testResolvedBindingCacheLifecycleAndRetry();
    testScopedHookReadPublishesAndReleasesReaders();
    testScopedHookReadKeepsNestedForwardingDuringConcurrentClose();
    testBowTppSliderControlsOnlyHorizontalDistance();
    testBowTppSliderValueIsBounded();
    testTridentLocalPoseMovesLeftAndTurnsHeadUp();
    testOwnerBoneHashClassificationForNativeProbe();
    testProbeBudgetStopsLogSpamAtLimit();
    testBowTppTiltClamp();
    testTridentFppHorizontalClamp();
    testTridentPostComposeRotationPreservesTranslation();
    testTridentExpressionBindingMapsRightOwnerToLeftItem();
    testInvalidLocalPoseIsNotMutated();
    testScopedLocalPoseOverrideRestoresBoneState();
    testScopedOverrideForcesRecomposeAndRestoresNativeCache();
    testInactiveLocalPoseOverrideLeavesBoneStateUntouched();
}
