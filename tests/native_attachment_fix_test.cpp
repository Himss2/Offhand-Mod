#include "render/NativeAttachmentFix.hpp"

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
    void testKnownHashesAndOffsets() {
        static_assert(fix::fnv1("main_hand") == 0xEF6FF81E3179CD3EULL);
        static_assert(fix::fnv1("off_hand") == 0x5D4C22812BA3AF8CULL);
        static_assert(fix::fnv1("rightitem") == 0x35397D324D54C8C4ULL);
        static_assert(fix::fnv1("leftitem") == 0x1CF3FDCBB0AB92F7ULL);
        static_assert(fix::fnv1("pole") == 0xACCBE87EB1D18FCBULL);
        static_assert(fix::kBoneLocalPoseOffset == 0x70);
        static_assert(fix::kBoneComposedMatrixOffset == 0x30);
        static_assert(fix::kBoneMatrixCachedOffset == 0xDE);
    }

    void testBowOwnerRemapIsSlotScopedNotPerspectiveScoped() {
        assert(fix::shouldRemapBowOwnerBone(true,6,true));
        assert(!fix::shouldRemapBowOwnerBone(true,5,true));
        assert(!fix::shouldRemapBowOwnerBone(false,6,true));
        assert(!fix::shouldRemapBowOwnerBone(true,6,false));

        std::uint64_t mapped=0;
        assert(fix::mapRightOwnerBoneToLeft(fix::fnv1("rightitem"),mapped));
        assert(mapped==fix::fnv1("leftitem"));
        assert(fix::mapRightOwnerBoneToLeft(fix::fnv1("rightItem"),mapped));
        assert(mapped==fix::fnv1("leftItem"));

        mapped=0x1234ULL;
        assert(!fix::mapRightOwnerBoneToLeft(fix::fnv1("pole"),mapped));
        assert(mapped==0x1234ULL);
    }

    void testTridentPoseMirrorUsesCurrentAnimatedPose() {
        fix::LocalAttachmentPose pose{
            {-7.0F,-3.0F,-2.0F},
            {152.0F,-9.0F,25.0F}
        };
        assert(fix::mirrorTridentOffhandLocalPose(pose));
        const fix::LocalAttachmentPose expected{
            {7.0F,-3.0F,-2.0F},
            {152.0F,9.0F,-25.0F}
        };
        assert(pose==expected);

        fix::LocalAttachmentPose dynamic{
            {1.25F,9.0F,-0.75F},
            {87.0F,13.0F,-42.0F}
        };
        assert(fix::mirrorTridentOffhandLocalPose(dynamic));
        const fix::LocalAttachmentPose dynamicExpected{
            {-1.25F,9.0F,-0.75F},
            {87.0F,-13.0F,42.0F}
        };
        assert(dynamic==dynamicExpected);
    }

    void testInvalidPoseIsRejectedWithoutMutation() {
        fix::LocalAttachmentPose pose{
            {std::numeric_limits<float>::infinity(),2.0F,3.0F},
            {4.0F,5.0F,6.0F}
        };
        const auto original=pose;
        assert(!fix::mirrorTridentOffhandLocalPose(pose));
        assert(pose==original);
    }

    void testScopedPoseOverridePreservesNativeCacheAndComposedMatrix() {
        std::array<std::byte,fix::kBoneMatrixCachedOffset+1> boneState{};
        const fix::LocalAttachmentPose originalPose{
            {-2.0F,4.0F,6.0F},
            {10.0F,20.0F,30.0F}
        };
        std::array<std::byte,fix::kBoneComposedMatrixSize> originalMatrix{};
        for(std::size_t i=0;i<originalMatrix.size();++i) {
            originalMatrix[i]=std::byte{static_cast<unsigned char>(i+1)};
        }
        std::memcpy(
            boneState.data()+fix::kBoneLocalPoseOffset,
            &originalPose,
            sizeof(originalPose)
        );
        std::memcpy(
            boneState.data()+fix::kBoneComposedMatrixOffset,
            originalMatrix.data(),
            originalMatrix.size()
        );
        boneState[fix::kBoneMatrixCachedOffset]=std::byte{1};

        {
            fix::ScopedLocalPoseOverride override(
                boneState.data(),
                &fix::mirrorTridentOffhandLocalPose
            );
            assert(override.active());

            fix::LocalAttachmentPose during{};
            std::memcpy(
                &during,
                boneState.data()+fix::kBoneLocalPoseOffset,
                sizeof(during)
            );
            const fix::LocalAttachmentPose expectedDuring{
                {2.0F,4.0F,6.0F},
                {10.0F,-20.0F,-30.0F}
            };
            assert(during==expectedDuring);
            assert(boneState[fix::kBoneMatrixCachedOffset]==std::byte{1});
            assert(std::memcmp(
                boneState.data()+fix::kBoneComposedMatrixOffset,
                originalMatrix.data(),
                originalMatrix.size()
            )==0);
        }

        fix::LocalAttachmentPose restored{};
        std::memcpy(
            &restored,
            boneState.data()+fix::kBoneLocalPoseOffset,
            sizeof(restored)
        );
        assert(restored==originalPose);
        assert(boneState[fix::kBoneMatrixCachedOffset]==std::byte{1});
        assert(std::memcmp(
            boneState.data()+fix::kBoneComposedMatrixOffset,
            originalMatrix.data(),
            originalMatrix.size()
        )==0);
    }

    void testInactivePoseOverrideLeavesStateUntouched() {
        std::array<std::byte,fix::kBoneMatrixCachedOffset+1> boneState{};
        const fix::LocalAttachmentPose original{
            {1.0F,2.0F,3.0F},
            {4.0F,5.0F,6.0F}
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
        assert(unchanged==original);
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
            fix::ScopedHookRead outer(ready,activeReaders);
            assert(outer.entered());
            assert(activeReaders.load()==1);
            ready.store(false);
            {
                fix::ScopedHookRead nested(ready,activeReaders);
                assert(nested.entered());
                assert(activeReaders.load()==1);
            }
            assert(activeReaders.load()==1);
        }
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
            fix::ScopedHookRead outer(forwardingAvailable,activeReaders);
            assert(outer.entered());
            outerEntered.store(true,std::memory_order_release);
            while(!allowNested.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            fix::ScopedHookRead nested(forwardingAvailable,activeReaders);
            nestedEntered.store(nested.entered(),std::memory_order_release);
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
}

int main() {
    testKnownHashesAndOffsets();
    testBowOwnerRemapIsSlotScopedNotPerspectiveScoped();
    testTridentPoseMirrorUsesCurrentAnimatedPose();
    testInvalidPoseIsRejectedWithoutMutation();
    testScopedPoseOverridePreservesNativeCacheAndComposedMatrix();
    testInactivePoseOverrideLeavesStateUntouched();
    testScopedHookReadPublishesAndReleasesReaders();
    testScopedHookReadKeepsNestedForwardingDuringConcurrentClose();
}
