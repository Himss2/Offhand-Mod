#include "render/OffhandBlockRenderPatch.hpp"
#include "render/NativeAttachmentFix.hpp"
#include <android/log.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <memory>
#include <thread>
#include <pl/memory/Hook.hpp>
#include <pl/memory/Signature.hpp>

namespace levioffhand::render {
    namespace {
        constexpr char kMinecraftLibrary[]="libminecraftpe.so";
        constexpr char kLogTag[]="Levi Offhand";

        constexpr std::uintptr_t kRenderItemRva=0xADDEA08;
        constexpr std::uintptr_t kDefaultTransformRva=0xA1DEE04;
        constexpr std::uintptr_t kMatrixMultiplyRva=0x94E96F4;
        constexpr std::uintptr_t kItemStackMatchesRva=0xF63BE90;
        constexpr std::uintptr_t kHandEquipPredicateRva=0xF644970;
        constexpr std::uintptr_t kOffDispatchCallsiteRva=0xADEA0BC;
        // v0.2.38: the native first-person DataDrivenRenderer runs before
        // the generic offhand item path. Bow is temporarily hidden from that
        // one pass only; the real stack is restored before renderOffhandItem.
        //
        // Static 1.26.45.1 proof:
        //   ADE9E9C -> A31662C  : first-person DataDrivenRenderer pass
        //   EC9D62C             : actor offhand ItemStack getter
        constexpr std::uintptr_t kFirstPersonDataDrivenRenderRva=0xA31662C;
        constexpr std::uintptr_t kFirstPersonDataDrivenCallsiteRva=0xADE9E9C;
        constexpr std::uintptr_t kGetOffhandStackRva=0xEC9D62C;

        // v0.2.56 keeps the v0.2.55 Bow/Fishing-Rod generic LEFT route,
        // but Trident returns to its native slot-6 3D attachment.  Bow TPP gets
        // only a grip-pivot tilt correction and Fishing Rod TPP gets only a
        // small vertical delta; neither correction is shared with FPP.
        constexpr bool kReferenceRouteDiagnostic=true;
        constexpr float kBowTppGripPivotTiltDegrees=25.20f;
        constexpr float kFishingRodTppVerticalDelta=-0.06f;
        constexpr std::uintptr_t kThirdPersonOffhandRenderItemCallsiteRva=0xA32F030;
        constexpr std::uintptr_t kRenderItemAttachableEnabledCallsiteRva=0xADDEADC;
        constexpr std::uintptr_t kAttachableStateRva=0xA32F0F4;
        constexpr std::uint32_t kOffhandInventorySlot=34;

        // Native attachment pipeline recovered from libminecraftpe.so 1.26.45.1:
        //   9B36A80 prepares one attachment and its slot/view context.
        //   F147CB0 returns the cached owner-binding mode at state +0xDC.
        //   AF3A1E4 resolves a name-bound attachment bone against the owner.
        //   9B3A228 draws one attachment stack/slot.
        //   F147ED0 composes each attachment bone matrix.
        constexpr std::uintptr_t kPrepareAttachmentRva=0x9B36A80;
        constexpr std::uintptr_t kAttachmentBindingModeRva=0xF147CB0;
        constexpr std::uintptr_t kAttachmentBindingModeFirstCallsiteRva=0x9B37780;
        constexpr std::uintptr_t kAttachmentBindingModeSecondCallsiteRva=0x9B377D8;
        constexpr std::uintptr_t kResolveOwnerBoneByNameRva=0xAF3A1E4;
        constexpr std::uintptr_t kResolveOwnerBoneFirstCallsiteRva=0x9B3779C;
        constexpr std::uintptr_t kResolveOwnerBoneSecondCallsiteRva=0x9B37814;
        constexpr std::uintptr_t kDrawAttachmentRva=0x9B3A228;
        constexpr std::uintptr_t kComposeAttachmentBoneMatrixRva=0xF147ED0;
        constexpr std::uintptr_t kComposeAttachmentBoneMatrixCallsiteRva=0x9B254C8;

        constexpr std::array<std::uint8_t,16> kPrepareAttachmentFingerprint{
            0xFD,0x7B,0xBA,0xA9,0xFC,0x6F,0x01,0xA9,
            0xFA,0x67,0x02,0xA9,0xF8,0x5F,0x03,0xA9
        };
        constexpr std::array<std::uint8_t,8>
            kAttachmentBindingModeFingerprint{
                0x00,0x70,0x43,0x39,0xC0,0x03,0x5F,0xD6
            };
        static_assert(
            kAttachmentBindingModeFirstCallsiteRva
            == native_attachment_fix::kBindingModeFirstReadCallsiteRva
        );
        static_assert(
            kAttachmentBindingModeSecondCallsiteRva
            == native_attachment_fix::kBindingModeSecondReadCallsiteRva
        );
        constexpr std::array<std::uint8_t,16> kResolveOwnerBoneByNameFingerprint{
            0xFF,0x43,0x02,0xD1,0xFD,0x7B,0x03,0xA9,
            0xFC,0x6F,0x04,0xA9,0xFA,0x67,0x05,0xA9
        };
        constexpr std::array<std::uint8_t,16> kDrawAttachmentFingerprint{
            0xFF,0x03,0x05,0xD1,0xE8,0x6B,0x00,0xFD,
            0xFD,0x7B,0x0E,0xA9,0xFC,0x6F,0x0F,0xA9
        };
        constexpr std::array<std::uint8_t,16>
            kComposeAttachmentBoneMatrixFingerprint{
                0x08,0x78,0x43,0x39,0xA8,0x00,0x00,0x34,
                0x00,0x84,0x41,0xAD,0x02,0x8C,0x42,0xAD
            };

        constexpr std::uintptr_t kFinalOffhandMatrixTopRva=0x107CC804;
        constexpr std::uintptr_t kFinalOffhandMatrixReturnRva=0xADE56D8;

        constexpr std::uintptr_t kBowIdRva=0x126F0FB8;
        constexpr std::uintptr_t kCrossbowIdRva=0x126F0FE0;
        constexpr std::uintptr_t kTridentIdRva=0x126F11C0;
        constexpr std::uintptr_t kFishingRodIdRva=0x126F26E8;

        constexpr std::uintptr_t kCopperSpearIdRva=0x126F4858;
        constexpr std::uintptr_t kDiamondSpearIdRva=0x126F4B00;
        constexpr std::uintptr_t kGoldenSpearIdRva=0x126F50C8;
        constexpr std::uintptr_t kIronSpearIdRva=0x126F53E8;
        constexpr std::uintptr_t kNetheriteSpearIdRva=0x126F5898;
        constexpr std::uintptr_t kStoneSpearIdRva=0x126F6130;
        constexpr std::uintptr_t kWoodenSpearIdRva=0x126F6590;

        constexpr char kRenderOffhandSignature[]=
            "FF C3 05 D1 "
            "EC 73 00 FD "
            "EB 2B 0F 6D "
            "E9 23 10 6D "
            "FD 7B 11 A9 "
            "FC 6F 12 A9 "
            "FA 67 13 A9 "
            "F8 5F 14 A9 "
            "F6 57 15 A9 "
            "F4 4F 16 A9 "
            "FD 43 04 91 "
            "5C D0 3B D5";

        constexpr char kBlockPredicateSignature[]=
            "FD 7B BE A9 "
            "F3 0B 00 F9 "
            "FD 03 00 91 "
            "F3 03 00 AA "
            "B2 DB CB 97 "
            "A0 00 00 36 "
            "E0 03 1F 2A "
            "F3 0B 40 F9 "
            "FD 7B C2 A8 "
            "C0 03 5F D6 "
            "60 E2 01 91 "
            "F3 0B 40 F9 "
            "FD 7B C2 A8 "
            "43 89 36 15";

        constexpr char kCanTessellateSignature[]=
            "FD 7B BE A9 "
            "F4 4F 01 A9 "
            "FD 03 00 91 "
            "F3 03 00 AA "
            "89 69 21 95 "
            "08 00 40 F9 "
            "28 02 00 B4 "
            "00 01 40 F9 "
            "E0 01 00 B4 "
            "44 C1 D0 97 "
            "92 C4 D0 97 "
            "F4 03 00 2A "
            "E0 03 13 AA "
            "4D 69 21 95";

        constexpr char kRenderObjectSignature[]=
            "FD 7B BA A9 "
            "FC 6F 01 A9 "
            "FA 67 02 A9 "
            "F8 5F 03 A9 "
            "F6 57 04 A9 "
            "F4 4F 05 A9 "
            "FD 03 00 91 "
            "FF C3 07 D1 "
            "E3 13 00 F9 "
            "5A D0 3B D5";

        constexpr char kItemTransformSignature[]=
            "FF 83 02 D1 "
            "FD 7B 08 A9 "
            "F4 4F 09 A9 "
            "FD 03 02 91 "
            "54 D0 3B D5 "
            "F3 03 08 AA "
            "29 00 80 52 "
            "88 16 40 F9";

        constexpr char kHandEquipPredicateSignature[]=
            "FD 7B BE A9 "
            "F3 0B 00 F9 "
            "FD 03 00 91 "
            "08 04 40 F9 "
            "88 04 00 B4 "
            "08 01 40 F9 "
            "48 04 00 B4 "
            "09 01 40 F9 "
            "F3 03 00 AA "
            "E0 03 08 AA "
            "29 2D 40 F9 "
            "20 01 3F D6";

        constexpr std::size_t kOffhandItemStackOffset=0xD0;
        constexpr std::size_t kItemWeakPtrOffset=0x08;
        constexpr std::size_t kItemStackBlockOffset=0x18;
        constexpr std::size_t kBlockTypeOffset=0x68;

        constexpr std::size_t kBannerWallBlockOffset=0x1C0;
        constexpr std::size_t kBannerStandingBlockOffset=0x1C8;

        constexpr std::uint32_t kFirstpersonRightHand=1;
        constexpr std::uint32_t kFirstpersonLeftHand=2;

        constexpr float kPi=3.14159265358979323846f;

        constexpr float kSkullExtraLeftTranslation=-0.50f;

        constexpr float kBannerScale=1.56f;
        constexpr float kBannerShiftX=-0.78f;
        constexpr float kBannerShiftY=-0.28f;
        constexpr float kBannerYawDegrees=180.0f;

        constexpr float kPotScale=1.40f;
        constexpr float kPotX=-0.04f;
        constexpr float kPotY=-0.24f;
        constexpr float kPotZ=0.28f;
        constexpr float kPotRotX=-32.40f;
        constexpr float kPotRotY=-180.00f;
        constexpr float kPotRotZ=-39.60f;

        constexpr float kCopperScale=1.03f;
        constexpr float kCopperX=-0.32f;
        constexpr float kCopperY=-0.04f;
        constexpr float kCopperZ=0.08f;
        constexpr float kCopperRotX=-14.40f;
        constexpr float kCopperRotY=140.40f;
        constexpr float kCopperRotZ=3.60f;

        // Calibration values frozen from the accepted semantic-axis settings.
        // Bow returns to the same generic FIRSTPERSON_LEFT path as Crossbow.
        // Spear intentionally remains native.
        constexpr OffhandBlockRenderPatch::ToolCalibration kBowCalibration{
            -0.30f, -0.12f, 0.78f,
            3.60f, 154.80f, 3.60f
        };

        constexpr OffhandBlockRenderPatch::ToolCalibration kCrossbowCalibration{
            0.18f, 0.18f, -0.48f,
            36.00f, 28.80f, 79.20f
        };

        constexpr OffhandBlockRenderPatch::ToolCalibration kFishingRodCalibration{
            0.78f, 0.30f, 0.66f,
            -25.20f, -147.60f, 14.40f
        };

        thread_local std::uint32_t gOffhandDepth=0;
        thread_local void* gRenderer=nullptr;
        thread_local void* gPlayer=nullptr;

        thread_local std::uint32_t gBridgeDepth=0;
        thread_local bool gBridgeConsumed=false;

        thread_local const void* gLastPolicyItem=nullptr;
        thread_local const void* gLastBridgeItem=nullptr;
        thread_local const void* gLastTransformItem=nullptr;
        thread_local const void* gLastSkullTransformItem=nullptr;

        enum class ToolFamily:std::uint8_t {
            None,
            Bow,
            Crossbow,
            Trident,
            Spear,
            FishingRod
        };

        thread_local const void* gLastNativeToolItem=nullptr;
        thread_local std::uint32_t gDispatchFixLoggedMask=0;

        thread_local ToolFamily gCurrentToolFamily=ToolFamily::None;
        thread_local bool gToolFinalMatrixApplied=false;

        thread_local const void* gLastCalibratedToolItem=nullptr;
        thread_local const void* gLastSuppressedTridentItem=nullptr;

        constexpr std::size_t kCalibrationFamilyCount=5;
        constexpr std::size_t kCalibrationAxisCount=6;

        using CalibrationAtomicRow=
            std::array<
                std::atomic<float>,
                kCalibrationAxisCount
            >;

        std::array<
            CalibrationAtomicRow,
            kCalibrationFamilyCount
        > gToolCalibration{};

        std::uintptr_t gItemStackMatchesTarget=0;
        std::uintptr_t gMinecraftBase=0;

        std::unique_ptr<
            pl::memory::HookHandle
        > gHandEquipPredicateHook;

        void* gHandEquipPredicateOriginal=nullptr;
        std::uintptr_t gHandEquipPredicateTarget=0;

        std::unique_ptr<
            pl::memory::HookHandle
        > gFirstPersonDataDrivenHook;

        void* gFirstPersonDataDrivenOriginal=nullptr;
        std::uintptr_t gFirstPersonDataDrivenTarget=0;

        std::uintptr_t gGetOffhandStackTarget=0;

        std::unique_ptr<pl::memory::HookHandle> gRenderItemRouteHook;
        void* gRenderItemRouteOriginal=nullptr;

        std::unique_ptr<pl::memory::HookHandle> gAttachableStateRouteHook;
        void* gAttachableStateRouteOriginal=nullptr;
        std::uintptr_t gAttachableStateRouteTarget=0;

        thread_local std::uint32_t gRenderItemRouteDepth=0;
        thread_local ToolFamily gRenderItemRouteFamily=ToolFamily::None;
        thread_local std::uint32_t gRenderItemRouteSlot=0;
        thread_local std::uintptr_t gRenderItemRouteCallsiteRva=0;
        thread_local bool gRenderItemAttachableCheckSeen=false;
        thread_local bool gRenderItemNativeAttachable=false;
        thread_local bool gRenderItemForcedGeneric=false;
        thread_local bool gTppReferenceMatrixApplied=false;
        thread_local bool gBowTppGripPivotLogged=false;
        thread_local bool gFishingRodTppLowerLogged=false;
        thread_local std::uint32_t gTppReferenceLoggedMask=0;
        thread_local bool gBowTppNativeSuppressLogged=false;
        thread_local bool gTridentFppNativeSuppressLogged=false;
        thread_local bool gTridentFppGenericLogged=false;
        thread_local bool gShieldFppReferenceLogged=false;
        thread_local bool gShieldFppObjectLogged=false;

        std::unique_ptr<pl::memory::HookHandle> gPrepareAttachmentHook;
        void* gPrepareAttachmentOriginal=nullptr;
        std::atomic<void*> gPrepareAttachmentOriginalPublished{nullptr};
        std::uintptr_t gPrepareAttachmentTarget=0;

        std::unique_ptr<pl::memory::HookHandle> gAttachmentBindingModeHook;
        void* gAttachmentBindingModeOriginal=nullptr;
        std::atomic<void*> gAttachmentBindingModeOriginalPublished{nullptr};
        std::uintptr_t gAttachmentBindingModeTarget=0;

        std::unique_ptr<pl::memory::HookHandle> gResolveOwnerBoneByNameHook;
        void* gResolveOwnerBoneByNameOriginal=nullptr;
        std::atomic<void*> gResolveOwnerBoneByNameOriginalPublished{nullptr};
        std::uintptr_t gResolveOwnerBoneByNameTarget=0;

        std::unique_ptr<pl::memory::HookHandle> gDrawAttachmentHook;
        void* gDrawAttachmentOriginal=nullptr;
        std::uintptr_t gDrawAttachmentTarget=0;

        std::unique_ptr<pl::memory::HookHandle> gComposeAttachmentBoneMatrixHook;
        void* gComposeAttachmentBoneMatrixOriginal=nullptr;
        std::uintptr_t gComposeAttachmentBoneMatrixTarget=0;

        thread_local std::uint32_t gBowTppBindingDepth=0;
        thread_local std::uint32_t gTridentFppBindingDepth=0;
        thread_local std::uint32_t gFirstPersonDataDrivenDepth=0;
        thread_local std::uint32_t gBowTppAttachmentDepth=0;
        thread_local std::uint32_t gTridentFppAttachmentDepth=0;
        std::atomic<float> gBowTppHorizontalOffset{
            native_attachment_fix::kBowTppHorizontalDefault
        };
        thread_local float gActiveBowTppHorizontalOffset=
            native_attachment_fix::kBowTppHorizontalDefault;
        constexpr std::size_t kResolvedTridentBindingBoneCapacity=8;
        std::atomic<std::uint64_t> gTridentFppBindingGeneration{1};
        // Mutation readiness is separate from trampoline lifetime. Teardown
        // closes mutation first, then reader admission; a reentrant prepare
        // transaction may still forward nested mode/resolver calls safely.
        std::atomic_bool gNativeAttachmentHooksReady{false};
        std::atomic_bool gNativeAttachmentTrampolinesAvailable{false};
        std::atomic_uint32_t gActiveTridentFppBindingScopes{0};
        std::atomic_uint32_t gActiveNativeAttachmentHookReaders{0};
        thread_local native_attachment_fix::ResolvedBindingCache<
            kResolvedTridentBindingBoneCapacity
        > gResolvedTridentFppBindingBones{};
        thread_local bool gBowTppBindingLogged=false;
        thread_local bool gTridentFppBindingLogged=false;
        thread_local bool gTridentFppBindingCacheLogged=false;
        thread_local bool gBowTppLocalPoseLogged=false;
        thread_local bool gTridentFppLocalPoseLogged=false;
        thread_local bool gTridentFppPrepareProbeLogged=false;
        thread_local std::uint32_t gTridentFppBindingProbeCount=0;

        struct BindingPrefix {
            std::int32_t ownerBoneIndex{-1};
            std::int32_t ownerGeometryIndex{-1};
            std::uint64_t nameHash{0};
        };

        static_assert(sizeof(BindingPrefix)==16);
        static_assert(offsetof(BindingPrefix,ownerGeometryIndex)==4);
        static_assert(offsetof(BindingPrefix,nameHash)==8);

        bool applyActiveBowTppLocalPose(
            native_attachment_fix::LocalAttachmentPose& pose
        ) noexcept {
            return native_attachment_fix::mirrorAndOffsetBowLocalPose(
                pose,
                gActiveBowTppHorizontalOffset
            );
        }

        void invalidateTridentFppBindingGeneration() noexcept {
            gTridentFppBindingGeneration.fetch_add(
                1,
                std::memory_order_acq_rel
            );
        }

        void synchronizeTridentFppBindingGeneration() noexcept {
            const std::uint64_t generation=
                gTridentFppBindingGeneration.load(
                    std::memory_order_acquire
                );

            if(!gResolvedTridentFppBindingBones.synchronize(generation)) {
                return;
            }

            gTridentFppBindingLogged=false;
            gTridentFppBindingCacheLogged=false;
            gTridentFppPrepareProbeLogged=false;
            gTridentFppBindingProbeCount=0;
        }

        void waitForNativeAttachmentHookReaders() noexcept {
            while(
                gActiveNativeAttachmentHookReaders.load(
                    std::memory_order_seq_cst
                )!=0
            ) {
                std::this_thread::yield();
            }
        }

        std::unique_ptr<
            pl::memory::HookHandle
        > gFinalOffhandMatrixHook;

        void* gFinalOffhandMatrixOriginal=nullptr;
        std::uintptr_t gFinalOffhandMatrixTarget=0;
        std::uintptr_t gToolMatrixMultiplyTarget=0;

        template<typename T>
        T readValue(
            const void* base,
            std::size_t offset,
            T fallback={}
        ) noexcept {

            if(!base) {
                return fallback;
            }

            T value{};

            std::memcpy(
                &value,
                static_cast<const std::byte*>(base)+offset,
                sizeof(T)
            );

            return value;
        }

        template<typename T>
        void writeValue(
            void* base,
            std::size_t offset,
            const T& value
        ) noexcept {

            if(!base) {
                return;
            }

            std::memcpy(
                static_cast<std::byte*>(base)+offset,
                &value,
                sizeof(T)
            );
        }

        [[nodiscard]]
        std::uint8_t nativeBindingModeDirect(
            const void* bindingState
        ) noexcept {
            return readValue<std::uint8_t>(
                bindingState,
                native_attachment_fix::kBoneBindingModeOffset,
                0
            );
        }

        template<std::size_t Size>
        [[nodiscard]]
        bool matchesFingerprint(
            std::uintptr_t target,
            const std::array<std::uint8_t,Size>& expected
        ) noexcept {
            return
                target!=0
                && std::memcmp(
                    reinterpret_cast<const void*>(target),
                    expected.data(),
                    expected.size()
                )==0;
        }

        class BowFppWeakItemMask final {
        public:
            BowFppWeakItemMask(
                void* stack,
                bool active
            ) noexcept
                : mStack(stack),
                  mOriginalWeakStorage(
                      readValue<const void*>(
                          stack,
                          kItemWeakPtrOffset,
                          nullptr
                      )
                  ),
                  mActive(
                      active
                      &&
                      stack
                      &&
                      mOriginalWeakStorage
                  ) {

                if(mActive) {
                    writeValue<const void*>(
                        mStack,
                        kItemWeakPtrOffset,
                        nullptr
                    );
                }
            }

            ~BowFppWeakItemMask() {
                if(mActive) {
                    writeValue<const void*>(
                        mStack,
                        kItemWeakPtrOffset,
                        mOriginalWeakStorage
                    );
                }
            }

            [[nodiscard]]
            bool active() const noexcept {
                return mActive;
            }

        private:
            void* mStack=nullptr;
            const void* mOriginalWeakStorage=nullptr;
            bool mActive=false;
        };

        bool belongsToMinecraft(
            std::uintptr_t address
        ) noexcept {

            if(!address) {
                return false;
            }

            Dl_info info{};

            if(
                !dladdr(
                    reinterpret_cast<void*>(address),
                    &info
                )
                ||
                !info.dli_fname
            ) {
                return false;
            }

            return
                std::strstr(
                    info.dli_fname,
                    kMinecraftLibrary
                )
                !=nullptr;
        }

        std::uintptr_t moduleBaseOf(
            std::uintptr_t address
        ) noexcept {

            if(!address) {
                return 0;
            }

            Dl_info info{};

            if(
                !dladdr(
                    reinterpret_cast<void*>(address),
                    &info
                )
                ||
                !info.dli_fbase
            ) {
                return 0;
            }

            return
                reinterpret_cast<std::uintptr_t>(
                    info.dli_fbase
                );
        }

        bool minecraftObject(
            const void* object
        ) noexcept {

            if(!object) {
                return false;
            }

            const void* vtable=
                readValue<const void*>(
                    object,
                    0,
                    nullptr
                );

            return
                belongsToMinecraft(
                    reinterpret_cast<std::uintptr_t>(
                        vtable
                    )
                );
        }

        const char* rttiName(
            const void* object
        ) noexcept {

            if(!minecraftObject(object)) {
                return "?";
            }

            const void* vtable=
                readValue<const void*>(
                    object,
                    0,
                    nullptr
                );

            const auto* bytes=
                static_cast<const std::byte*>(
                    vtable
                );

            const void* typeInfo=nullptr;

            std::memcpy(
                &typeInfo,
                bytes-sizeof(void*),
                sizeof(typeInfo)
            );

            if(
                !belongsToMinecraft(
                    reinterpret_cast<std::uintptr_t>(
                        typeInfo
                    )
                )
            ) {
                return "?";
            }

            const char* name=
                readValue<const char*>(
                    typeInfo,
                    sizeof(void*),
                    nullptr
                );

            if(
                !name
                ||
                !belongsToMinecraft(
                    reinterpret_cast<std::uintptr_t>(
                        name
                    )
                )
            ) {
                return "?";
            }

            return name;
        }

        bool contains(
            const char* text,
            const char* token
        ) noexcept {

            return
                text
                &&
                token
                &&
                std::strstr(
                    text,
                    token
                )
                !=nullptr;
        }

        class OffhandScope final {
        public:

            OffhandScope(
                void* renderer,
                void* player
            ) noexcept
                :
                mOldRenderer(gRenderer),
                mOldPlayer(gPlayer),
                mOldConsumed(gBridgeConsumed)
            {

                ++gOffhandDepth;

                gRenderer=renderer;
                gPlayer=player;

                gBridgeConsumed=false;
            }

            ~OffhandScope() {

                gRenderer=mOldRenderer;
                gPlayer=mOldPlayer;
                gBridgeConsumed=mOldConsumed;

                if(gOffhandDepth) {
                    --gOffhandDepth;
                }
            }

        private:

            void* mOldRenderer;
            void* mOldPlayer;
            bool mOldConsumed;
        };

        class BridgeScope final {
        public:

            BridgeScope() noexcept {
                ++gBridgeDepth;
            }

            ~BridgeScope() {

                if(gBridgeDepth) {
                    --gBridgeDepth;
                }
            }
        };

        class StackBlockOverride final {
        public:

            StackBlockOverride(
                void* stack,
                const void* replacement
            ) noexcept
                :
                mStack(stack),
                mOriginal(
                    readValue<const void*>(
                        stack,
                        kItemStackBlockOffset,
                        nullptr
                    )
                ),
                mActive(
                    stack
                    &&
                    replacement
                    &&
                    !mOriginal
                )
            {

                if(mActive) {

                    writeValue<const void*>(
                        mStack,
                        kItemStackBlockOffset,
                        replacement
                    );
                }
            }

            ~StackBlockOverride() {

                if(mActive) {

                    writeValue<const void*>(
                        mStack,
                        kItemStackBlockOffset,
                        mOriginal
                    );
                }
            }

            [[nodiscard]]
            bool active() const noexcept {
                return mActive;
            }

        private:

            void* mStack;
            const void* mOriginal;
            bool mActive;
        };

        bool inOffhand() noexcept {

            return
                gOffhandDepth!=0
                &&
                gRenderer!=nullptr;
        }

        void* offhandStackMutable() noexcept {

            if(!inOffhand()) {
                return nullptr;
            }

            return
                static_cast<std::byte*>(
                    gRenderer
                )
                +
                kOffhandItemStackOffset;
        }

        const void* offhandStack() noexcept {
            return offhandStackMutable();
        }

        const void* offhandItem() noexcept {

            const void* stack=
                offhandStack();

            if(!stack) {
                return nullptr;
            }

            const void* weakStorage=
                readValue<const void*>(
                    stack,
                    kItemWeakPtrOffset,
                    nullptr
                );

            if(!weakStorage) {
                return nullptr;
            }

            return
                readValue<const void*>(
                    weakStorage,
                    0,
                    nullptr
                );
        }

        const void* offhandBlock() noexcept {

            return
                readValue<const void*>(
                    offhandStack(),
                    kItemStackBlockOffset,
                    nullptr
                );
        }

        const void* blockTypeOf(
            const void* block
        ) noexcept {

            if(!block) {
                return nullptr;
            }

            const void* type=
                readValue<const void*>(
                    block,
                    kBlockTypeOffset,
                    nullptr
                );

            return
                minecraftObject(type)
                ?
                type
                :
                nullptr;
        }

        using ItemStackMatchesFn=
            bool(*)(
                const void*,
                const void*,
                std::uint32_t
            );

        [[nodiscard]]
        bool stackMatchesId(
            const void* stack,
            std::uintptr_t idRva
        ) noexcept {

            if(
                stack==nullptr
                ||
                gMinecraftBase==0
                ||
                gItemStackMatchesTarget==0
            ) {
                return false;
            }

            const auto matcher=
                reinterpret_cast<ItemStackMatchesFn>(
                    gItemStackMatchesTarget
                );

            return
                matcher(
                    stack,
                    reinterpret_cast<const void*>(
                        gMinecraftBase+idRva
                    ),
                    0
                );
        }

        [[nodiscard]]
        ToolFamily classifyTool(
            const void* stack
        ) noexcept {

            if(stack==nullptr) {
                return ToolFamily::None;
            }

            if(
                readValue<const void*>(
                    stack,
                    kItemStackBlockOffset,
                    nullptr
                )
                !=nullptr
            ) {
                return ToolFamily::None;
            }

            if(
                stackMatchesId(
                    stack,
                    kBowIdRva
                )
            ) {
                return ToolFamily::Bow;
            }

            if(
                stackMatchesId(
                    stack,
                    kCrossbowIdRva
                )
            ) {
                return ToolFamily::Crossbow;
            }

            if(
                stackMatchesId(
                    stack,
                    kTridentIdRva
                )
            ) {
                return ToolFamily::Trident;
            }

            if(
                stackMatchesId(
                    stack,
                    kFishingRodIdRva
                )
            ) {
                return ToolFamily::FishingRod;
            }

            if(
                stackMatchesId(
                    stack,
                    kCopperSpearIdRva
                )
                ||
                stackMatchesId(
                    stack,
                    kDiamondSpearIdRva
                )
                ||
                stackMatchesId(
                    stack,
                    kGoldenSpearIdRva
                )
                ||
                stackMatchesId(
                    stack,
                    kIronSpearIdRva
                )
                ||
                stackMatchesId(
                    stack,
                    kNetheriteSpearIdRva
                )
                ||
                stackMatchesId(
                    stack,
                    kStoneSpearIdRva
                )
                ||
                stackMatchesId(
                    stack,
                    kWoodenSpearIdRva
                )
            ) {
                return ToolFamily::Spear;
            }

            const void* item=
                offhandItem();

            const char* itemClass=
                rttiName(
                    item
                );

            if(
                contains(
                    itemClass,
                    "CrossbowItem"
                )
            ) {
                return ToolFamily::Crossbow;
            }

            if(
                contains(
                    itemClass,
                    "BowItem"
                )
            ) {
                return ToolFamily::Bow;
            }

            if(
                contains(
                    itemClass,
                    "TridentItem"
                )
            ) {
                return ToolFamily::Trident;
            }

            if(
                contains(
                    itemClass,
                    "FishingRodItem"
                )
            ) {
                return ToolFamily::FishingRod;
            }

            return ToolFamily::None;
        }

        [[nodiscard]]
        const char* toolFamilyName(
            ToolFamily family
        ) noexcept {

            switch(family) {

                case ToolFamily::Bow:
                    return "Bow";

                case ToolFamily::Crossbow:
                    return "Crossbow";

                case ToolFamily::Trident:
                    return "Trident";

                case ToolFamily::Spear:
                    return "Spear";

                case ToolFamily::FishingRod:
                    return "FishingRod";

                default:
                    return "None";
            }
        }

        [[nodiscard]]
        const char* ownerBoneHashKindName(
            native_attachment_fix::OwnerBoneHashKind kind
        ) noexcept {
            using Kind=native_attachment_fix::OwnerBoneHashKind;

            switch(kind) {
                case Kind::RightItemLower:
                    return "rightitem";
                case Kind::RightItemCamel:
                    return "rightItem";
                case Kind::LeftItemLower:
                    return "leftitem";
                case Kind::LeftItemCamel:
                    return "leftItem";
                case Kind::Other:
                default:
                    return "other";
            }
        }

        [[nodiscard]]
        bool shouldForceOffhandDispatch(
            ToolFamily family,
            std::uintptr_t callsiteRva
        ) noexcept {

            if(
                callsiteRva
                !=
                kOffDispatchCallsiteRva
            ) {
                return false;
            }

            switch(family) {

                case ToolFamily::Bow:
                case ToolFamily::Crossbow:
                case ToolFamily::Trident:
                case ToolFamily::Spear:
                    return true;

                case ToolFamily::FishingRod:
                case ToolFamily::None:
                default:
                    return false;
            }
        }

        [[nodiscard]]
        std::uintptr_t minecraftCallsiteRva(
            std::uintptr_t returnAddress
        ) noexcept {
            if(
                gMinecraftBase==0
                ||
                !belongsToMinecraft(returnAddress)
                ||
                returnAddress<gMinecraftBase+4
            ) {
                return 0;
            }

            return returnAddress-gMinecraftBase-4;
        }

        [[nodiscard]]
        bool isExactMinecraftCallsite(
            std::uintptr_t returnAddress,
            std::uintptr_t callsiteRva
        ) noexcept {
            return
                gMinecraftBase!=0
                && returnAddress==gMinecraftBase+callsiteRva+4;
        }

        void logTridentFppBindingProbe(
            std::uintptr_t callsiteRva,
            bool exactPrepareResolverCall,
            const BindingPrefix& source,
            bool leftAttempted,
            bool leftResolved,
            bool finalResolved,
            const BindingPrefix& finalBinding
        ) noexcept {
            constexpr std::uint32_t kProbeLimit=4;

            if(
                !native_attachment_fix::consumeProbeBudget(
                    gTridentFppBindingProbeCount,
                    kProbeLimit
                )
            ) {
                return;
            }

            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[TridentFppBindingProbe] caller=0x%llX exact=%d "
                "source=%s hash=0x%llX leftAttempt=%d leftResolved=%d "
                "finalResolved=%d bone=%d geometry=%d",
                static_cast<unsigned long long>(callsiteRva),
                exactPrepareResolverCall?1:0,
                ownerBoneHashKindName(
                    native_attachment_fix::classifyOwnerBoneHash(
                        source.nameHash
                    )
                ),
                static_cast<unsigned long long>(source.nameHash),
                leftAttempted?1:0,
                leftResolved?1:0,
                finalResolved?1:0,
                finalBinding.ownerBoneIndex,
                finalBinding.ownerGeometryIndex
            );
        }

        [[nodiscard]]
        bool isTppReferenceFamily(ToolFamily family) noexcept {
            return family==ToolFamily::Bow || family==ToolFamily::FishingRod;
        }

        using RenderItemRouteFn=void(*)(
            void*,
            void*,
            void*,
            const void*,
            bool,
            std::uint32_t,
            bool,
            bool
        );

        void renderItemRouteDetour(
            void* self,
            void* renderContext,
            void* actor,
            const void* stack,
            bool arg4,
            std::uint32_t slot,
            bool arg6,
            bool arg7
        ) noexcept {
            const auto original=reinterpret_cast<RenderItemRouteFn>(
                gRenderItemRouteOriginal
            );
            if(!original) {
                return;
            }

            const ToolFamily family=
                OffhandBlockRenderPatch::instance().featureEnabled()
                ? classifyTool(stack)
                : ToolFamily::None;
            const std::uintptr_t callsiteRva=minecraftCallsiteRva(
                reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))
            );
            const bool referenceCall=
                kReferenceRouteDiagnostic
                && isTppReferenceFamily(family)
                && slot==kOffhandInventorySlot
                && callsiteRva==kThirdPersonOffhandRenderItemCallsiteRva;

            if(!referenceCall) {
                original(
                    self,renderContext,actor,stack,arg4,slot,arg6,arg7
                );
                return;
            }

            const auto oldDepth=gRenderItemRouteDepth;
            const auto oldFamily=gRenderItemRouteFamily;
            const auto oldSlot=gRenderItemRouteSlot;
            const auto oldCallsite=gRenderItemRouteCallsiteRva;
            const auto oldSeen=gRenderItemAttachableCheckSeen;
            const auto oldNative=gRenderItemNativeAttachable;
            const auto oldForced=gRenderItemForcedGeneric;
            const auto oldMatrixApplied=gTppReferenceMatrixApplied;

            ++gRenderItemRouteDepth;
            gRenderItemRouteFamily=family;
            gRenderItemRouteSlot=slot;
            gRenderItemRouteCallsiteRva=callsiteRva;
            gRenderItemAttachableCheckSeen=false;
            gRenderItemNativeAttachable=false;
            gRenderItemForcedGeneric=false;
            gTppReferenceMatrixApplied=false;

            original(self,renderContext,actor,stack,arg4,slot,arg6,arg7);

            const std::uint32_t bit=
                family==ToolFamily::Bow ? 1U : 2U;
            if((gTppReferenceLoggedMask & bit)==0U) {
                gTppReferenceLoggedMask|=bit;
                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "[BowFishingRodTppRoute] family=%s caller=0x%llX "
                    "slot=%u attachableCheck=%d nativeAttachable=%d "
                    "forcedGeneric=%d",
                    toolFamilyName(family),
                    static_cast<unsigned long long>(callsiteRva),
                    static_cast<unsigned>(slot),
                    gRenderItemAttachableCheckSeen?1:0,
                    gRenderItemNativeAttachable?1:0,
                    gRenderItemForcedGeneric?1:0
                );
            }

            gRenderItemRouteDepth=oldDepth;
            gRenderItemRouteFamily=oldFamily;
            gRenderItemRouteSlot=oldSlot;
            gRenderItemRouteCallsiteRva=oldCallsite;
            gRenderItemAttachableCheckSeen=oldSeen;
            gRenderItemNativeAttachable=oldNative;
            gRenderItemForcedGeneric=oldForced;
            gTppReferenceMatrixApplied=oldMatrixApplied;
        }

        using AttachableStateRouteFn=bool(*)(void*);

        bool attachableStateRouteDetour(void* renderer) noexcept {
            const auto original=reinterpret_cast<AttachableStateRouteFn>(
                gAttachableStateRouteOriginal
            );
            if(!original) {
                return false;
            }

            const bool nativeResult=original(renderer);
            const std::uintptr_t callsiteRva=minecraftCallsiteRva(
                reinterpret_cast<std::uintptr_t>(__builtin_return_address(0))
            );

            const bool exactReferenceCheck=
                kReferenceRouteDiagnostic
                && gRenderItemRouteDepth!=0
                && gRenderItemRouteSlot==kOffhandInventorySlot
                && gRenderItemRouteCallsiteRva==
                    kThirdPersonOffhandRenderItemCallsiteRva
                && callsiteRva==kRenderItemAttachableEnabledCallsiteRva
                && isTppReferenceFamily(gRenderItemRouteFamily);

            if(!exactReferenceCheck) {
                return nativeResult;
            }

            gRenderItemAttachableCheckSeen=true;
            gRenderItemNativeAttachable=nativeResult;

            // Fishing Rod is the reference: it naturally continues through the
            // generic LEFT renderer.  Bow is made equivalent only at this exact
            // slot-34 TPP RenderItem transaction.
            if(gRenderItemRouteFamily==ToolFamily::Bow && nativeResult) {
                gRenderItemForcedGeneric=true;
                return false;
            }

            return nativeResult;
        }

        using GetOffhandStackFn=
            const void*(*)(void*);

        using PrepareAttachmentFn=void(*)(
            void*,
            const void*,
            const std::uint32_t*,
            void*,
            void*,
            bool,
            bool
        );

        void prepareAttachmentDetour(
            void* self,
            const void* stack,
            const std::uint32_t* slotPointer,
            void* parentContext,
            void* actor,
            bool isFirstPerson,
            bool enabled
        ) noexcept {
            native_attachment_fix::ScopedHookRead readGuard(
                gNativeAttachmentTrampolinesAvailable,
                gActiveNativeAttachmentHookReaders
            );
            if(!readGuard.entered()) {
                return;
            }

            const auto original=reinterpret_cast<PrepareAttachmentFn>(
                gPrepareAttachmentOriginalPublished.load(
                    std::memory_order_acquire
                )
            );
            if(!original) {
                return;
            }

            const std::uint32_t slot=readValue<std::uint32_t>(
                slotPointer,
                0,
                static_cast<std::uint32_t>(-1)
            );
            const bool featureEnabled=
                OffhandBlockRenderPatch::instance().featureEnabled();
            synchronizeTridentFppBindingGeneration();
            const bool isBow=
                featureEnabled
                && stack
                && stackMatchesId(stack,kBowIdRva);
            const bool isTrident=
                featureEnabled
                && stack
                && stackMatchesId(stack,kTridentIdRva);
            if(
                slot==native_attachment_fix::kOffhandSlot
                && (
                    !featureEnabled
                    || (
                        gFirstPersonDataDrivenDepth!=0
                        && !isTrident
                    )
                )
            ) {
                gResolvedTridentFppBindingBones.clear();
            }
            const bool remapBowOwnerBone=
                !kReferenceRouteDiagnostic
                && native_attachment_fix::shouldRemapBowOwnerBone(
                    isBow,
                    slot,
                    isFirstPerson,
                    gNativeAttachmentHooksReady.load(
                        std::memory_order_acquire
                    )
                );
            // The native bool is not a reliable FPP discriminator for this
            // attachment.  ADE9E9C is, and its depth is already exact-scoped.
            const bool remapTridentOwnerBone=
                native_attachment_fix::shouldRemapTridentOwnerBone(
                    isTrident,
                    slot,
                    gFirstPersonDataDrivenDepth!=0,
                    gNativeAttachmentHooksReady.load(
                        std::memory_order_acquire
                    )
                );

            if(remapBowOwnerBone) {
                ++gBowTppBindingDepth;
            }

            if(remapTridentOwnerBone) {
                gActiveTridentFppBindingScopes.fetch_add(
                    1,
                    std::memory_order_acq_rel
                );
                ++gTridentFppBindingDepth;

                if(!gTridentFppPrepareProbeLogged) {
                    gTridentFppPrepareProbeLogged=true;
                    __android_log_print(
                        ANDROID_LOG_INFO,
                        kLogTag,
                        "[TridentFppPrepareProbe] slot=%u "
                        "nativeFirstPersonArg=%d exactFppScope=1 enabled=%d",
                        static_cast<unsigned>(slot),
                        isFirstPerson?1:0,
                        enabled?1:0
                    );
                }
            }

            original(
                self,
                stack,
                slotPointer,
                parentContext,
                actor,
                isFirstPerson,
                enabled
            );

            if(remapBowOwnerBone && gBowTppBindingDepth!=0) {
                --gBowTppBindingDepth;
            }

            if(
                remapTridentOwnerBone
                && gTridentFppBindingDepth!=0
            ) {
                --gTridentFppBindingDepth;
            }

            if(remapTridentOwnerBone) {
                gActiveTridentFppBindingScopes.fetch_sub(
                    1,
                    std::memory_order_acq_rel
                );
            }
        }

        using AttachmentBindingModeFn=std::uint8_t(*)(const void*);

        std::uint8_t attachmentBindingModeDetour(
            const void* bindingState
        ) noexcept {
            const std::uint8_t directMode=
                nativeBindingModeDirect(bindingState);
            if(!bindingState) {
                return directMode;
            }

            native_attachment_fix::ScopedHookRead readGuard(
                gNativeAttachmentTrampolinesAvailable,
                gActiveNativeAttachmentHookReaders
            );
            if(!readGuard.entered()) {
                return directMode;
            }

            const auto original=reinterpret_cast<AttachmentBindingModeFn>(
                gAttachmentBindingModeOriginalPublished.load(
                    std::memory_order_acquire
                )
            );
            if(!original) {
                return directMode;
            }

            // v0.2.56: preserve Minecraft's native binding/cache mode.  The
            // only Trident intervention is rightitem -> leftitem if the native
            // resolver naturally runs inside the exact FPP slot-6 scope.
            return original(bindingState);
        }

        using ResolveOwnerBoneByNameFn=bool(*)(
            void*,
            const void*,
            void*
        );

        bool resolveOwnerBoneByNameDetour(
            void* self,
            const void* ownerGeometry,
            void* bindingState
        ) noexcept {
            native_attachment_fix::ScopedHookRead readGuard(
                gNativeAttachmentTrampolinesAvailable,
                gActiveNativeAttachmentHookReaders
            );
            if(!readGuard.entered()) {
                return false;
            }

            const auto original=reinterpret_cast<ResolveOwnerBoneByNameFn>(
                gResolveOwnerBoneByNameOriginalPublished.load(
                    std::memory_order_acquire
                )
            );
            if(!original) {
                return false;
            }

            const std::uintptr_t returnAddress=
                reinterpret_cast<std::uintptr_t>(
                    __builtin_return_address(0)
                );
            const std::uintptr_t callsiteRva=
                minecraftCallsiteRva(returnAddress);
            const bool exactPrepareResolverCall=
                isExactMinecraftCallsite(
                    returnAddress,
                    kResolveOwnerBoneFirstCallsiteRva
                )
                ||
                isExactMinecraftCallsite(
                    returnAddress,
                    kResolveOwnerBoneSecondCallsiteRva
                );

            const bool mutationReady=
                gNativeAttachmentHooksReady.load(
                    std::memory_order_acquire
                );
            const bool remapBowOwnerBone=
                mutationReady
                && gBowTppBindingDepth!=0;
            const bool remapTridentOwnerBone=
                !remapBowOwnerBone
                && gTridentFppBindingDepth!=0
                && mutationReady;
            const bool featureEnabled=
                OffhandBlockRenderPatch::instance().featureEnabled();
            const BindingPrefix sourceBinding=
                readValue<BindingPrefix>(bindingState,0,{});

            if(
                (remapBowOwnerBone || remapTridentOwnerBone)
                && exactPrepareResolverCall
                && bindingState
                && featureEnabled
            ) {
                BindingPrefix candidate=sourceBinding;
                std::uint64_t leftHash=0;
                const bool leftAttempted=
                    native_attachment_fix::mapRightOwnerBoneToLeft(
                        candidate.nameHash,
                        leftHash
                    );
                bool leftResolved=false;

                if(leftAttempted) {
                    candidate.ownerBoneIndex=-1;
                    candidate.ownerGeometryIndex=-1;
                    candidate.nameHash=leftHash;
                    leftResolved=original(self,ownerGeometry,&candidate);

                    if(leftResolved) {
                        writeValue<std::int32_t>(
                            bindingState,
                            0,
                            candidate.ownerBoneIndex
                        );
                        writeValue<std::int32_t>(
                            bindingState,
                            sizeof(std::int32_t),
                            candidate.ownerGeometryIndex
                        );

                        if(remapTridentOwnerBone) {
                            static_cast<void>(
                                gResolvedTridentFppBindingBones.
                                    recordResolution(
                                        bindingState,
                                        true
                                    )
                            );
                        }

                        if(
                            remapBowOwnerBone
                            && !gBowTppBindingLogged
                        ) {
                            gBowTppBindingLogged=true;
                            __android_log_print(
                                ANDROID_LOG_INFO,
                                kLogTag,
                                "[BowTppBoneBinding] slot6 native Bow "
                                "bound to owner left-item bone"
                            );
                        }

                        if(
                            remapTridentOwnerBone
                            && !gTridentFppBindingLogged
                        ) {
                            gTridentFppBindingLogged=true;
                            __android_log_print(
                                ANDROID_LOG_INFO,
                                kLogTag,
                                "[TridentFppBoneBinding] slot6 native "
                                "Trident bound to owner left-item bone"
                            );
                        }

                        if(remapTridentOwnerBone) {
                            logTridentFppBindingProbe(
                                callsiteRva,
                                true,
                                sourceBinding,
                                true,
                                true,
                                true,
                                candidate
                            );
                        }

                        return true;
                    }
                }

                if(remapTridentOwnerBone) {
                    const bool nativeResolved=
                        original(self,ownerGeometry,bindingState);
                    const BindingPrefix nativeBinding=
                        readValue<BindingPrefix>(bindingState,0,{});

                    logTridentFppBindingProbe(
                        callsiteRva,
                        true,
                        sourceBinding,
                        leftAttempted,
                        leftResolved,
                        nativeResolved,
                        nativeBinding
                    );

                    return nativeResolved;
                }
            }

            const bool resolved=original(self,ownerGeometry,bindingState);

            if(
                remapTridentOwnerBone
                && bindingState
                && featureEnabled
            ) {
                const BindingPrefix finalBinding=
                    readValue<BindingPrefix>(bindingState,0,{});

                logTridentFppBindingProbe(
                    callsiteRva,
                    false,
                    sourceBinding,
                    false,
                    false,
                    resolved,
                    finalBinding
                );
            }

            return resolved;
        }

        using DrawAttachmentFn=void(*)(
            void*,
            const void*,
            const std::uint32_t*,
            void*,
            void*
        );

        void drawAttachmentDetour(
            void* self,
            const void* stack,
            const std::uint32_t* slotPointer,
            void* parentContext,
            void* actor
        ) noexcept {
            const auto original=reinterpret_cast<DrawAttachmentFn>(
                gDrawAttachmentOriginal
            );
            if(!original) {
                return;
            }

            const std::uint32_t slot=readValue<std::uint32_t>(
                slotPointer,
                0,
                static_cast<std::uint32_t>(-1)
            );
            const bool featureEnabled=
                OffhandBlockRenderPatch::instance().featureEnabled();
            const bool hooksReady=
                gNativeAttachmentHooksReady.load(
                    std::memory_order_acquire
                );
            const bool isBow=
                featureEnabled
                && hooksReady
                && stack
                && stackMatchesId(stack,kBowIdRva);
            const bool isTrident=
                featureEnabled
                && hooksReady
                && stack
                && stackMatchesId(stack,kTridentIdRva);
            const bool isFirstPerson=
                gFirstPersonDataDrivenDepth!=0;
            const std::uintptr_t callsiteRva=minecraftCallsiteRva(
                reinterpret_cast<std::uintptr_t>(
                    __builtin_return_address(0)
                )
            );
            const bool effectiveOffhandDraw=
                native_attachment_fix::isEffectiveOffhandDrawCallsite(
                    callsiteRva
                );
            const bool suppressBowNative=
                kReferenceRouteDiagnostic
                && effectiveOffhandDraw
                && isBow
                && slot==native_attachment_fix::kOffhandSlot
                && !isFirstPerson;
            if(suppressBowNative) {
                if(!gBowTppNativeSuppressLogged) {
                    gBowTppNativeSuppressLogged=true;
                    __android_log_print(
                        ANDROID_LOG_INFO,
                        kLogTag,
                        "[BowFishingRodTppNativeSuppress] slot6 native Bow "
                        "attachment suppressed; generic LEFT route is reference"
                    );
                }
                return;
            }

            const bool offsetBow=
                !kReferenceRouteDiagnostic
                && effectiveOffhandDraw
                && native_attachment_fix::shouldFixBowLocalPose(
                    isBow,
                    slot,
                    isFirstPerson
                );
            const bool fixTrident=false;
            if(offsetBow) {
                ++gBowTppAttachmentDepth;
            }
            if(fixTrident) {
                ++gTridentFppAttachmentDepth;
            }

            if(
                effectiveOffhandDraw
                && isTrident
                && slot==native_attachment_fix::kOffhandSlot
                && isFirstPerson
                && !gTridentFppNativeSuppressLogged
            ) {
                gTridentFppNativeSuppressLogged=true;
                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "[TridentFppNative3D] native slot6 attachment retained"
                );
            }

            original(self,stack,slotPointer,parentContext,actor);

            if(offsetBow && gBowTppAttachmentDepth!=0) {
                --gBowTppAttachmentDepth;
            }
            if(fixTrident && gTridentFppAttachmentDepth!=0) {
                --gTridentFppAttachmentDepth;
            }

        }

        using ComposeAttachmentBoneMatrixFn=void(*)(
            void*,
            const void*,
            OffhandBlockRenderPatch::Matrix64*
        );

        void composeAttachmentBoneMatrixDetour(
            void* boneState,
            const void* pivot,
            OffhandBlockRenderPatch::Matrix64* matrix
        ) noexcept {
            const auto original=
                reinterpret_cast<ComposeAttachmentBoneMatrixFn>(
                    gComposeAttachmentBoneMatrixOriginal
                );
            if(!original) {
                return;
            }

            if(
                !boneState
                || !matrix
                || !OffhandBlockRenderPatch::instance().featureEnabled()
                || !isExactMinecraftCallsite(
                    reinterpret_cast<std::uintptr_t>(
                        __builtin_return_address(0)
                    ),
                    kComposeAttachmentBoneMatrixCallsiteRva
                )
            ) {
                original(boneState,pivot,matrix);
                return;
            }

            const std::uint64_t boneNameHash=readValue<std::uint64_t>(
                boneState,
                8,
                0
            );

            const bool offsetBowRoot=
                gBowTppAttachmentDepth!=0
                && (
                    boneNameHash
                    ==
                    native_attachment_fix::kRightItemLowerHash
                    ||
                    boneNameHash
                    ==
                    native_attachment_fix::kRightItemCamelHash
                );
            if(!offsetBowRoot) {
                original(boneState,pivot,matrix);
                return;
            }

            const auto localPoseBefore=
                readValue<native_attachment_fix::LocalAttachmentPose>(
                    boneState,
                    native_attachment_fix::kBoneLocalPoseOffset,
                    {}
                );

            native_attachment_fix::LocalPoseMutator localPoseMutator=nullptr;
            float bowHorizontalOffset=
                native_attachment_fix::kBowTppHorizontalDefault;
            if(offsetBowRoot) {
                bowHorizontalOffset=gBowTppHorizontalOffset.load(
                    std::memory_order_acquire
                );
                gActiveBowTppHorizontalOffset=bowHorizontalOffset;
                localPoseMutator=&applyActiveBowTppLocalPose;
            }

            // F147ED0 consumes the animated local pose at +0x70 only when its
            // +0xDE matrix-cache flag is clear. The scoped override snapshots
            // the pose, cached matrix, and flag; invalidates the cache for this
            // call; then restores all native state after the corrected output
            // matrix has been returned. Child bones still inherit that output,
            // while later perspectives/actors cannot inherit the temporary
            // pose or cache.
            native_attachment_fix::ScopedLocalPoseOverride localPoseOverride(
                boneState,
                localPoseMutator
            );

            original(boneState,pivot,matrix);

            if(
                offsetBowRoot
                && localPoseOverride.active()
                && !gBowTppLocalPoseLogged
            ) {
                gBowTppLocalPoseLogged=true;
                auto corrected=localPoseBefore;
                static_cast<void>(
                    native_attachment_fix::mirrorAndOffsetBowLocalPose(
                        corrected,
                        bowHorizontalOffset
                    )
                );
                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "[BowTppLocalPose] slot6 rightitem localX %.3f -> "
                    "%.3f (TPP slider %.3f)",
                    static_cast<double>(localPoseBefore.position[0]),
                    static_cast<double>(corrected.position[0]),
                    static_cast<double>(bowHorizontalOffset)
                );
            }

        }

        using FirstPersonDataDrivenFn=
            void(*)(
                void*,
                void*,
                void*,
                const void*,
                const void*,
                bool
            );

        void firstPersonDataDrivenDetour(
            void* self,
            void* renderContext,
            void* actor,
            const void* position,
            const void* rotation,
            bool mode
        ) noexcept {

            const auto original=
                reinterpret_cast<FirstPersonDataDrivenFn>(
                    gFirstPersonDataDrivenOriginal
                );

            if(!original) {
                return;
            }

            const auto callsiteRva=
                minecraftCallsiteRva(
                    reinterpret_cast<std::uintptr_t>(
                        __builtin_return_address(0)
                    )
                );

            const bool featureEnabled=
                OffhandBlockRenderPatch::instance().featureEnabled();

            const bool isFirstPersonCallsite=
                callsiteRva==kFirstPersonDataDrivenCallsiteRva;

            if(
                !featureEnabled
                ||
                !actor
                ||
                !gGetOffhandStackTarget
            ) {
                original(
                    self,
                    renderContext,
                    actor,
                    position,
                    rotation,
                    mode
                );
                return;
            }

            const auto getOffhand=
                reinterpret_cast<GetOffhandStackFn>(
                    gGetOffhandStackTarget
                );

            void* actorOffhand=
                const_cast<void*>(
                    getOffhand(actor)
                );

            const bool offhandBow=
                actorOffhand
                &&
                stackMatchesId(
                    actorOffhand,
                    kBowIdRva
                );

            // FPP keeps the v0.2.38 mechanism that is already proven at runtime:
            // hide Bow identity only while the exact first-person native pass runs.
            BowFppWeakItemMask bowMask(
                actorOffhand,
                offhandBow
                && isFirstPersonCallsite
            );

            if(bowMask.active()) {
                static thread_local bool loggedFpp=false;
                if(!loggedFpp) {
                    loggedFpp=true;
                    __android_log_print(
                        ANDROID_LOG_INFO,
                        kLogTag,
                        "[BowFppNativeMask] temporarily hid offhand Bow "
                        "from first-person DataDrivenRenderer"
                    );
                }
            }

            // The exact ADE9E9C actor pass encloses native attachment draws.
            // Keep a narrow view scope so the Trident correction never reaches
            // TPP, inventory preview, projectiles, or mainhand attachments.
            if(isFirstPersonCallsite) {
                ++gFirstPersonDataDrivenDepth;
            }

            original(
                self,
                renderContext,
                actor,
                position,
                rotation,
                mode
            );

            if(isFirstPersonCallsite && gFirstPersonDataDrivenDepth!=0) {
                --gFirstPersonDataDrivenDepth;
            }
        }

        class ToolRenderScope final {
        public:

            explicit
            ToolRenderScope(
                ToolFamily family
            ) noexcept
                :
                mOldFamily(gCurrentToolFamily),
                mOldApplied(gToolFinalMatrixApplied)
            {

                gCurrentToolFamily=family;
                gToolFinalMatrixApplied=false;
            }

            ~ToolRenderScope() {

                gCurrentToolFamily=mOldFamily;
                gToolFinalMatrixApplied=mOldApplied;
            }

        private:

            ToolFamily mOldFamily;
            bool mOldApplied;
        };

        using HandEquipPredicateFn=
            bool(*)(
                const void*
            );

        bool handEquipPredicateDetour(
            const void* stack
        ) noexcept {

            const auto original=
                reinterpret_cast<HandEquipPredicateFn>(
                    gHandEquipPredicateOriginal
                );

            if(!original) {
                return false;
            }

            const bool result=
                original(
                    stack
                );

            if(
                result
                ||
                !OffhandBlockRenderPatch::
                    instance().
                    featureEnabled()
            ) {
                return result;
            }

            const ToolFamily family=
                classifyTool(
                    stack
                );

            if(
                family
                ==
                ToolFamily::None
            ) {
                return result;
            }

            const std::uintptr_t returnAddress=
                reinterpret_cast<std::uintptr_t>(
                    __builtin_return_address(0)
                );

            if(
                gMinecraftBase==0
                ||
                !belongsToMinecraft(
                    returnAddress
                )
                ||
                returnAddress
                <
                gMinecraftBase+4
            ) {
                return result;
            }

            const std::uintptr_t returnRva=
                returnAddress
                -
                gMinecraftBase;

            const std::uintptr_t callsiteRva=
                returnRva
                -
                4;

            if(
                !result
                &&
                shouldForceOffhandDispatch(
                    family,
                    callsiteRva
                )
            ) {

                const std::uint32_t bit=
                    1u
                    <<
                    static_cast<std::uint32_t>(
                        family
                    );

                if(
                    (
                        gDispatchFixLoggedMask
                        &
                        bit
                    )
                    ==
                    0
                ) {

                    gDispatchFixLoggedMask
                        |=
                        bit;

                    __android_log_print(
                        ANDROID_LOG_INFO,
                        kLogTag,
                        "[ToolDispatchFix] %s "
                        "OFF_DISPATCH false -> true",
                        toolFamilyName(
                            family
                        )
                    );
                }

                return true;
            }

            return result;
        }

        bool isShieldItem(
            const char* itemClass
        ) noexcept {
            return contains(itemClass,"ShieldItem");
        }

        bool isBannerItem(
            const char* itemClass
        ) noexcept {

            return
                contains(
                    itemClass,
                    "BannerItem"
                );
        }

        bool isDecoratedPot(
            const char* itemClass,
            const char* blockClass
        ) noexcept {

            return
                contains(
                    itemClass,
                    "DecoratedPot"
                )
                ||
                contains(
                    blockClass,
                    "DecoratedPot"
                );
        }

        bool isCopperGolemStatue(
            const char* itemClass,
            const char* blockClass
        ) noexcept {

            return
                contains(
                    itemClass,
                    "CopperGolemStatue"
                )
                ||
                contains(
                    blockClass,
                    "CopperGolemStatue"
                );
        }

        bool skullFamily(
            const char* itemClass,
            const char* blockClass
        ) noexcept {

            return
                contains(
                    itemClass,
                    "Skull"
                )
                ||
                contains(
                    itemClass,
                    "Head"
                )
                ||
                contains(
                    blockClass,
                    "Skull"
                )
                ||
                contains(
                    blockClass,
                    "Head"
                );
        }

        bool specialFamily(
            const char* itemClass,
            const char* blockClass
        ) noexcept {

            return
                contains(
                    blockClass,
                    "ChestBlock"
                )
                ||
                contains(
                    itemClass,
                    "ShulkerBox"
                )
                ||
                contains(
                    blockClass,
                    "ShulkerBox"
                )
                ||
                contains(
                    itemClass,
                    "Skull"
                )
                ||
                contains(
                    blockClass,
                    "Skull"
                )
                ||
                contains(
                    itemClass,
                    "Conduit"
                )
                ||
                contains(
                    blockClass,
                    "Conduit"
                )
                ||
                contains(
                    itemClass,
                    "Banner"
                )
                ||
                contains(
                    blockClass,
                    "Banner"
                )
                ||
                contains(
                    itemClass,
                    "DecoratedPot"
                )
                ||
                contains(
                    blockClass,
                    "DecoratedPot"
                )
                ||
                contains(
                    itemClass,
                    "CopperGolemStatue"
                )
                ||
                contains(
                    blockClass,
                    "CopperGolemStatue"
                )
                ||
                contains(
                    itemClass,
                    "Pumpkin"
                )
                ||
                contains(
                    blockClass,
                    "Pumpkin"
                )
                ||
                contains(
                    itemClass,
                    "JackOLantern"
                )
                ||
                contains(
                    blockClass,
                    "JackOLantern"
                );
        }

        bool knownGoodNativeSpecial(
            const char* blockClass
        ) noexcept {

            return
                contains(
                    blockClass,
                    "BellBlock"
                )
                ||
                contains(
                    blockClass,
                    "LanternBlock"
                );
        }

        bool currentSpecialFamily() noexcept {

            const void* item=
                offhandItem();

            const void* block=
                offhandBlock();

            return
                specialFamily(
                    rttiName(
                        item
                    ),
                    rttiName(
                        blockTypeOf(
                            block
                        )
                    )
                );
        }

        bool currentBannerOrPot(
            bool& banner,
            bool& pot
        ) noexcept {

            const void* item=
                offhandItem();

            const void* block=
                offhandBlock();

            const char* itemClass=
                rttiName(
                    item
                );

            const char* blockClass=
                rttiName(
                    blockTypeOf(
                        block
                    )
                );

            banner=
                isBannerItem(
                    itemClass
                );

            pot=
                isDecoratedPot(
                    itemClass,
                    blockClass
                );

            return
                banner
                ||
                pot;
        }

        void scaleMatrixBasis(
            OffhandBlockRenderPatch::Matrix64& matrix,
            float scale
        ) noexcept {

            matrix.value[0]*=scale;
            matrix.value[1]*=scale;
            matrix.value[2]*=scale;

            matrix.value[4]*=scale;
            matrix.value[5]*=scale;
            matrix.value[6]*=scale;

            matrix.value[8]*=scale;
            matrix.value[9]*=scale;
            matrix.value[10]*=scale;
        }

        OffhandBlockRenderPatch::Matrix64
        localXRotationDegrees(
            float degrees
        ) noexcept {

            const float radians=
                degrees
                *
                (
                    kPi
                    /
                    180.0f
                );

            const float c=
                std::cos(
                    radians
                );

            const float s=
                std::sin(
                    radians
                );

            return {{
                1.0f,0.0f,0.0f,0.0f,
                0.0f,c,s,0.0f,
                0.0f,-s,c,0.0f,
                0.0f,0.0f,0.0f,1.0f
            }};
        }

        OffhandBlockRenderPatch::Matrix64
        localYRotationDegrees(
            float degrees
        ) noexcept {

            const float radians=
                degrees
                *
                (
                    kPi
                    /
                    180.0f
                );

            const float c=
                std::cos(
                    radians
                );

            const float s=
                std::sin(
                    radians
                );

            return {{
                c,0.0f,-s,0.0f,
                0.0f,1.0f,0.0f,0.0f,
                s,0.0f,c,0.0f,
                0.0f,0.0f,0.0f,1.0f
            }};
        }

        OffhandBlockRenderPatch::Matrix64
        localZRotationDegrees(
            float degrees
        ) noexcept {

            const float radians=
                degrees
                *
                (
                    kPi
                    /
                    180.0f
                );

            const float c=
                std::cos(
                    radians
                );

            const float s=
                std::sin(
                    radians
                );

            return {{
                c,s,0.0f,0.0f,
                -s,c,0.0f,0.0f,
                0.0f,0.0f,1.0f,0.0f,
                0.0f,0.0f,0.0f,1.0f
            }};
        }

        using CanTessellateFn=
            bool(*)(
                const void*
            );

        using RenderOffhandFn=
            void(*)(
                void*,
                void*,
                void*,
                std::uint32_t
            );

        using BlockPredicateFn=
            bool(*)(
                const void*
            );

        using RenderObjectFn=
            void(*)(
                void*,
                void*,
                const void*,
                const void*,
                std::uint32_t
            );

        using RenderItemFn=
            void(*)(
                void*,
                void*,
                void*,
                const void*,
                bool,
                std::uint32_t,
                bool,
                bool
            );

        using ItemTransformFn=
            OffhandBlockRenderPatch::Matrix64(*)(
                void*,
                std::uint32_t
            );

        using DefaultTransformFn=
            OffhandBlockRenderPatch::Matrix64(*)(
                const std::uint32_t*
            );

        using MatrixMultiplyFn=
            OffhandBlockRenderPatch::Matrix64(*)(
                const OffhandBlockRenderPatch::Matrix64*,
                const OffhandBlockRenderPatch::Matrix64*
            );

        bool canTessellate(
            std::uintptr_t target
        ) noexcept {

            const void* stack=
                offhandStack();

            if(
                !target
                ||
                !stack
            ) {
                return false;
            }

            return
                reinterpret_cast<CanTessellateFn>(
                    target
                )(
                    stack
                );
        }

        void applyIndependentEuler(
            OffhandBlockRenderPatch::Matrix64& matrix,
            MatrixMultiplyFn multiply,
            float rotX,
            float rotY,
            float rotZ
        ) noexcept {

            const float savedX=
                matrix.value[12];

            const float savedY=
                matrix.value[13];

            const float savedZ=
                matrix.value[14];

            if(
                rotX
                !=
                0.0f
            ) {

                const auto rotation=
                    localXRotationDegrees(
                        rotX
                    );

                matrix=
                    multiply(
                        &matrix,
                        &rotation
                    );
            }

            if(
                rotY
                !=
                0.0f
            ) {

                const auto rotation=
                    localYRotationDegrees(
                        rotY
                    );

                matrix=
                    multiply(
                        &matrix,
                        &rotation
                    );
            }

            if(
                rotZ
                !=
                0.0f
            ) {

                const auto rotation=
                    localZRotationDegrees(
                        rotZ
                    );

                matrix=
                    multiply(
                        &matrix,
                        &rotation
                    );
            }

            matrix.value[12]=savedX;
            matrix.value[13]=savedY;
            matrix.value[14]=savedZ;
        }

        [[nodiscard]]
        OffhandBlockRenderPatch::ToolCalibration
        fixedCalibrationFor(
            ToolFamily family
        ) noexcept {

            switch(family) {
                case ToolFamily::Bow:
                    return kBowCalibration;

                case ToolFamily::Crossbow:
                    return kCrossbowCalibration;

                case ToolFamily::FishingRod:
                    return kFishingRodCalibration;

                case ToolFamily::Spear:
                case ToolFamily::Trident:
                case ToolFamily::None:
                default:
                    return {};
            }
        }

        [[nodiscard]]
        std::size_t calibrationFamilyIndex(
            OffhandBlockRenderPatch::
                ToolCalibrationFamily family
        ) noexcept {

            return
                static_cast<std::size_t>(
                    family
                );
        }

        [[nodiscard]]
        std::size_t calibrationAxisIndex(
            OffhandBlockRenderPatch::
                ToolCalibrationAxis axis
        ) noexcept {

            return
                static_cast<std::size_t>(
                    axis
                );
        }

        [[nodiscard]]
        bool normalizeBasisVector(
            const OffhandBlockRenderPatch::Matrix64& matrix,
            std::size_t index,
            float& x,
            float& y,
            float& z
        ) noexcept {
            x=matrix.value[index+0];
            y=matrix.value[index+1];
            z=matrix.value[index+2];

            const float lengthSquared=
                x*x+y*y+z*z;

            if(
                !std::isfinite(lengthSquared)
                ||
                lengthSquared<=1.0e-12f
            ) {
                x=0.0f;
                y=0.0f;
                z=0.0f;
                return false;
            }

            const float inverseLength=
                1.0f/std::sqrt(lengthSquared);

            x*=inverseLength;
            y*=inverseLength;
            z*=inverseLength;

            return true;
        }

        void applyCalibrationInHandBasis(
            OffhandBlockRenderPatch::Matrix64& matrix,
            MatrixMultiplyFn multiply,
            const OffhandBlockRenderPatch::
                ToolCalibration& calibration
        ) noexcept {
            /*
             * Semantic slider axes recovered from the actual Bedrock held
             * item matrix and confirmed by device calibration:
             *
             *     UI X -> native basis column 1 (matrix[4..6])
             *     UI Y -> native basis column 2 (matrix[8..10])
             *     UI Z -> native basis column 0 (matrix[0..2])
             *
             * The previous build exposed the native column order directly,
             * which is why Pos X behaved like Z, Pos Y like X and Pos Z like Y.
             */
            float semanticXX=0.0f;
            float semanticXY=0.0f;
            float semanticXZ=0.0f;

            float semanticYX=0.0f;
            float semanticYY=0.0f;
            float semanticYZ=0.0f;

            float semanticZX=0.0f;
            float semanticZY=0.0f;
            float semanticZZ=0.0f;

            const bool semanticXValid=
                normalizeBasisVector(
                    matrix,
                    4,
                    semanticXX,
                    semanticXY,
                    semanticXZ
                );

            const bool semanticYValid=
                normalizeBasisVector(
                    matrix,
                    8,
                    semanticYX,
                    semanticYY,
                    semanticYZ
                );

            const bool semanticZValid=
                normalizeBasisVector(
                    matrix,
                    0,
                    semanticZX,
                    semanticZY,
                    semanticZZ
                );

            if(multiply) {
                /*
                 * Native matrix rotations are Rx/Ry/Rz around basis columns
                 * 0/1/2 respectively. With the semantic permutation above:
                 *
                 *     semantic Rot X -> native Ry
                 *     semantic Rot Y -> native Rz
                 *     semantic Rot Z -> native Rx
                 *
                 * Keep native X->Y->Z multiplication order so converted
                 * calibration values preserve the exact pose from v0.2.24a.
                 */
                applyIndependentEuler(
                    matrix,
                    multiply,
                    calibration.rotZ,
                    calibration.rotX,
                    calibration.rotY
                );
            }

            if(semanticXValid) {
                matrix.value[12]+=semanticXX*calibration.posX;
                matrix.value[13]+=semanticXY*calibration.posX;
                matrix.value[14]+=semanticXZ*calibration.posX;
            }

            if(semanticYValid) {
                matrix.value[12]+=semanticYX*calibration.posY;
                matrix.value[13]+=semanticYY*calibration.posY;
                matrix.value[14]+=semanticYZ*calibration.posY;
            }

            if(semanticZValid) {
                matrix.value[12]+=semanticZX*calibration.posZ;
                matrix.value[13]+=semanticZY*calibration.posZ;
                matrix.value[14]+=semanticZZ*calibration.posZ;
            }
        }

        using FinalMatrixTopFn=
            void* (*)(
                void*
            );

        void*
        finalOffhandMatrixTopDetour(
            void* matrixStack
        ) noexcept {

            const auto original=
                reinterpret_cast<
                    FinalMatrixTopFn
                >(
                    gFinalOffhandMatrixOriginal
                );

            if(!original) {
                return nullptr;
            }

            void* result=
                original(
                    matrixStack
                );

            /*
             * v0.2.24:
             *
             * The late matrix is the only stage proven on-device to affect
             * Bow/Crossbow/Spear/FishingRod held visuals. The previous build
             * wrote camera-space XYZ here and the item appeared to lag behind
             * the camera. We keep this proven stage, but position is now
             * expressed through the matrix's normalized native hand basis.
             *
             * Trident remains a separate dedicated path.
             */
            if(
                gCurrentToolFamily
                ==
                ToolFamily::None
            ) {
                return result;
            }

            if(
                !result
                ||
                gToolFinalMatrixApplied
                ||
                !OffhandBlockRenderPatch::
                    instance().
                    featureEnabled()
            ) {
                return result;
            }

            const std::uintptr_t caller=
                reinterpret_cast<std::uintptr_t>(
                    __builtin_return_address(0)
                );

            if(
                gMinecraftBase==0
                ||
                caller
                !=
                gMinecraftBase
                +
                kFinalOffhandMatrixReturnRva
            ) {
                return result;
            }

            const auto multiply=
                reinterpret_cast<
                    MatrixMultiplyFn
                >(
                    gToolMatrixMultiplyTarget
                );

            auto* matrix=
                static_cast<
                    OffhandBlockRenderPatch::Matrix64*
                >(
                    result
                );

            if(
                gCurrentToolFamily==ToolFamily::Trident
                || gCurrentToolFamily==ToolFamily::Spear
            ) {
                // Dedicated native attachment paths are corrected separately.
                gToolFinalMatrixApplied=true;
                return result;
            }

            const auto calibration=
                fixedCalibrationFor(
                    gCurrentToolFamily
                );

            applyCalibrationInHandBasis(
                *matrix,
                multiply,
                calibration
            );

            // v0.2.56 TPP-only correction must run AFTER the accepted generic
            // calibration.  Applying it earlier rotates the basis used by the
            // calibration's XYZ offsets and moves the already-correct grip.
            if(
                gRenderItemRouteDepth!=0
                && !gTppReferenceMatrixApplied
                && gRenderItemRouteSlot==kOffhandInventorySlot
                && gRenderItemRouteCallsiteRva==
                    kThirdPersonOffhandRenderItemCallsiteRva
            ) {
                if(gRenderItemRouteFamily==ToolFamily::Bow) {
                    const float preservedBowTx=matrix->value[12];
                    const float preservedBowTy=matrix->value[13];
                    const float preservedBowTz=matrix->value[14];

                    applyIndependentEuler(
                        *matrix,
                        multiply,
                        0.0f,
                        0.0f,
                        kBowTppGripPivotTiltDegrees
                    );

                    // Keep the hand anchor exactly where v0.2.55 placed it.
                    matrix->value[12]=preservedBowTx;
                    matrix->value[13]=preservedBowTy;
                    matrix->value[14]=preservedBowTz;

                    if(!gBowTppGripPivotLogged) {
                        gBowTppGripPivotLogged=true;
                        __android_log_print(
                            ANDROID_LOG_INFO,
                            kLogTag,
                            "[BowTppGripPivot] semanticRotYDelta=%.2f "
                            "translationPreserved=1",
                            static_cast<double>(
                                kBowTppGripPivotTiltDegrees
                            )
                        );
                    }
                } else if(
                    gRenderItemRouteFamily==ToolFamily::FishingRod
                ) {
                    float yx=0.0f;
                    float yy=0.0f;
                    float yz=0.0f;
                    if(normalizeBasisVector(*matrix,8,yx,yy,yz)) {
                        matrix->value[12]+=
                            yx*kFishingRodTppVerticalDelta;
                        matrix->value[13]+=
                            yy*kFishingRodTppVerticalDelta;
                        matrix->value[14]+=
                            yz*kFishingRodTppVerticalDelta;

                        if(!gFishingRodTppLowerLogged) {
                            gFishingRodTppLowerLogged=true;
                            __android_log_print(
                                ANDROID_LOG_INFO,
                                kLogTag,
                                "[FishingRodTppLower] semanticYDelta=%.2f",
                                static_cast<double>(
                                    kFishingRodTppVerticalDelta
                                )
                            );
                        }
                    }
                }
                gTppReferenceMatrixApplied=true;
            }

            gToolFinalMatrixApplied=true;

            const void* item=
                offhandItem();

            if(
                item
                !=
                gLastCalibratedToolItem
            ) {
                gLastCalibratedToolItem=item;

                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "[ToolCalibrationFinalLocal] %s "
                    "XYZ=(%.2f,%.2f,%.2f) "
                    "R=(%.1f,%.1f,%.1f)",
                    toolFamilyName(
                        gCurrentToolFamily
                    ),
                    static_cast<double>(
                        calibration.posX
                    ),
                    static_cast<double>(
                        calibration.posY
                    ),
                    static_cast<double>(
                        calibration.posZ
                    ),
                    static_cast<double>(
                        calibration.rotX
                    ),
                    static_cast<double>(
                        calibration.rotY
                    ),
                    static_cast<double>(
                        calibration.rotZ
                    )
                );
            }

            return result;
        }

    } // namespace


    OffhandBlockRenderPatch*
        OffhandBlockRenderPatch::
        sInstance=nullptr;


    OffhandBlockRenderPatch::
    ~OffhandBlockRenderPatch()=default;


    OffhandBlockRenderPatch&
    OffhandBlockRenderPatch::
    instance() noexcept {

        static
            OffhandBlockRenderPatch
            instance;

        return instance;
    }


    bool
    OffhandBlockRenderPatch::
    install(
        pl::mod::ModContext& context
    ) noexcept {

        if(
            installed()
        ) {
            return true;
        }

        uninstall(
            context
        );

        auto& logger=
            context.logger();

        mRenderOffhandTarget=
            pl::memory::
            resolveSignature(
                kRenderOffhandSignature,
                kMinecraftLibrary
            );

        mBlockPredicateTarget=
            pl::memory::
            resolveSignature(
                kBlockPredicateSignature,
                kMinecraftLibrary
            );

        mCanTessellateTarget=
            pl::memory::
            resolveSignature(
                kCanTessellateSignature,
                kMinecraftLibrary
            );

        mRenderObjectTarget=
            pl::memory::
            resolveSignature(
                kRenderObjectSignature,
                kMinecraftLibrary
            );

        mItemTransformTarget=
            pl::memory::
            resolveSignature(
                kItemTransformSignature,
                kMinecraftLibrary
            );

        gHandEquipPredicateTarget=
            pl::memory::
            resolveSignature(
                kHandEquipPredicateSignature,
                kMinecraftLibrary
            );

        const std::uintptr_t base=
            moduleBaseOf(
                mRenderOffhandTarget
            );

        if(base) {

            mRenderItemTarget=
                base
                +
                kRenderItemRva;

            mDefaultTransformTarget=
                base
                +
                kDefaultTransformRva;

            mMatrixMultiplyTarget=
                base
                +
                kMatrixMultiplyRva;

            gMinecraftBase=
                base;

            gItemStackMatchesTarget=
                base
                +
                kItemStackMatchesRva;

            gFinalOffhandMatrixTarget=
                base
                +
                kFinalOffhandMatrixTopRva;

            gFirstPersonDataDrivenTarget=
                base
                +
                kFirstPersonDataDrivenRenderRva;

            gGetOffhandStackTarget=
                base
                +
                kGetOffhandStackRva;

            gAttachableStateRouteTarget=
                base
                +
                kAttachableStateRva;

            gPrepareAttachmentTarget=
                base
                +
                kPrepareAttachmentRva;

            gAttachmentBindingModeTarget=
                base
                +
                kAttachmentBindingModeRva;

            gResolveOwnerBoneByNameTarget=
                base
                +
                kResolveOwnerBoneByNameRva;

            gDrawAttachmentTarget=
                base
                +
                kDrawAttachmentRva;

            gComposeAttachmentBoneMatrixTarget=
                base
                +
                kComposeAttachmentBoneMatrixRva;

            gToolMatrixMultiplyTarget=
                mMatrixMultiplyTarget;
        }

        if(
            !belongsToMinecraft(
                mRenderOffhandTarget
            )
            ||
            !belongsToMinecraft(
                mBlockPredicateTarget
            )
            ||
            !belongsToMinecraft(
                mCanTessellateTarget
            )
            ||
            !belongsToMinecraft(
                mRenderObjectTarget
            )
            ||
            !belongsToMinecraft(
                mItemTransformTarget
            )
            ||
            !belongsToMinecraft(
                mRenderItemTarget
            )
            ||
            !belongsToMinecraft(
                mDefaultTransformTarget
            )
            ||
            !belongsToMinecraft(
                mMatrixMultiplyTarget
            )
            ||
            !belongsToMinecraft(
                gItemStackMatchesTarget
            )


            ||
            !belongsToMinecraft(
                gHandEquipPredicateTarget
            )
            ||
            !belongsToMinecraft(
                gFinalOffhandMatrixTarget
            )
            ||
            !belongsToMinecraft(
                gFirstPersonDataDrivenTarget
            )
            ||
            !belongsToMinecraft(
                gGetOffhandStackTarget
            )
            ||
            !belongsToMinecraft(gAttachableStateRouteTarget)
            ||
            !belongsToMinecraft(gPrepareAttachmentTarget)
            ||
            !belongsToMinecraft(gAttachmentBindingModeTarget)
            ||
            !belongsToMinecraft(gResolveOwnerBoneByNameTarget)
            ||
            !belongsToMinecraft(gDrawAttachmentTarget)
            ||
            !belongsToMinecraft(gComposeAttachmentBoneMatrixTarget)
            ||
            !matchesFingerprint(
                gPrepareAttachmentTarget,
                kPrepareAttachmentFingerprint
            )
            ||
            !matchesFingerprint(
                gAttachmentBindingModeTarget,
                kAttachmentBindingModeFingerprint
            )
            ||
            !matchesFingerprint(
                gResolveOwnerBoneByNameTarget,
                kResolveOwnerBoneByNameFingerprint
            )
            ||
            !matchesFingerprint(
                gDrawAttachmentTarget,
                kDrawAttachmentFingerprint
            )
            ||
            !matchesFingerprint(
                gComposeAttachmentBoneMatrixTarget,
                kComposeAttachmentBoneMatrixFingerprint
            )
            ||
            gHandEquipPredicateTarget
            !=
            base
            +
            kHandEquipPredicateRva
        ) {

            logger.error(
                "Offhand visual: "
                "target resolution failed"
            );

            uninstall(
                context
            );

            return false;
        }

        mRenderOffhandOriginal=nullptr;
        mBlockPredicateOriginal=nullptr;
        mRenderObjectOriginal=nullptr;
        mItemTransformOriginal=nullptr;

        gHandEquipPredicateOriginal=nullptr;
        gFirstPersonDataDrivenOriginal=nullptr;
        gRenderItemRouteOriginal=nullptr;
        gAttachableStateRouteOriginal=nullptr;
        gPrepareAttachmentOriginal=nullptr;
        gPrepareAttachmentOriginalPublished.store(
            nullptr,
            std::memory_order_release
        );
        gAttachmentBindingModeOriginal=nullptr;
        gAttachmentBindingModeOriginalPublished.store(
            nullptr,
            std::memory_order_release
        );
        gResolveOwnerBoneByNameOriginal=nullptr;
        gResolveOwnerBoneByNameOriginalPublished.store(
            nullptr,
            std::memory_order_release
        );
        gDrawAttachmentOriginal=nullptr;
        gComposeAttachmentBoneMatrixOriginal=nullptr;
        gFinalOffhandMatrixOriginal=nullptr;

        gLastNativeToolItem=nullptr;
        gDispatchFixLoggedMask=0;
        gBowTppBindingDepth=0;
        gTridentFppBindingDepth=0;
        gFirstPersonDataDrivenDepth=0;
        gBowTppAttachmentDepth=0;
        gTridentFppAttachmentDepth=0;
        gResolvedTridentFppBindingBones.clear();
        gBowTppBindingLogged=false;
        gTridentFppBindingLogged=false;
        gTridentFppBindingCacheLogged=false;
        gBowTppLocalPoseLogged=false;
        gTridentFppLocalPoseLogged=false;
        gTridentFppPrepareProbeLogged=false;
        gTridentFppBindingProbeCount=0;

        gCurrentToolFamily=
            ToolFamily::None;

        gToolFinalMatrixApplied=false;

        gLastCalibratedToolItem=nullptr;
        gLastSuppressedTridentItem=nullptr;
        gRenderItemRouteDepth=0;
        gRenderItemRouteFamily=ToolFamily::None;
        gRenderItemRouteSlot=0;
        gRenderItemRouteCallsiteRva=0;
        gRenderItemAttachableCheckSeen=false;
        gRenderItemNativeAttachable=false;
        gRenderItemForcedGeneric=false;
        gTppReferenceLoggedMask=0;
        gBowTppNativeSuppressLogged=false;
        gTridentFppNativeSuppressLogged=false;
        gTridentFppGenericLogged=false;
        gShieldFppReferenceLogged=false;
        gShieldFppObjectLogged=false;


        mBannerBridgeLogged.store(
            false,
            std::memory_order_relaxed
        );

        mBannerCompositeLogged.store(
            false,
            std::memory_order_relaxed
        );

        mPotCompositeLogged.store(
            false,
            std::memory_order_relaxed
        );

        gOffhandDepth=0;
        gRenderer=nullptr;
        gPlayer=nullptr;


        gBridgeDepth=0;
        gBridgeConsumed=false;

        gLastPolicyItem=nullptr;
        gLastBridgeItem=nullptr;
        gLastTransformItem=nullptr;
        gLastSkullTransformItem=nullptr;

        sInstance=this;

        mRenderOffhandHook=
            std::make_unique<
                pl::memory::HookHandle
            >(
                reinterpret_cast<void*>(
                    mRenderOffhandTarget
                ),

                reinterpret_cast<void*>(
                    &OffhandBlockRenderPatch::
                    renderOffhandDetour
                ),

                &mRenderOffhandOriginal,

                pl::memory::
                    HookPriority::Normal
            );

        if(
            !mRenderOffhandHook
            ||
            !mRenderOffhandHook->
                installed()
            ||
            !mRenderOffhandOriginal
        ) {

            logger.error(
                "renderOffhand hook failed"
            );

            uninstall(
                context
            );

            return false;
        }

        mBlockPredicateHook=
            std::make_unique<
                pl::memory::HookHandle
            >(
                reinterpret_cast<void*>(
                    mBlockPredicateTarget
                ),

                reinterpret_cast<void*>(
                    &OffhandBlockRenderPatch::
                    blockRenderPredicateDetour
                ),

                &mBlockPredicateOriginal,

                pl::memory::
                    HookPriority::Normal
            );

        if(
            !mBlockPredicateHook
            ||
            !mBlockPredicateHook->
                installed()
            ||
            !mBlockPredicateOriginal
        ) {

            logger.error(
                "predicate hook failed"
            );

            uninstall(
                context
            );

            return false;
        }

        mRenderObjectHook=
            std::make_unique<
                pl::memory::HookHandle
            >(
                reinterpret_cast<void*>(
                    mRenderObjectTarget
                ),

                reinterpret_cast<void*>(
                    &OffhandBlockRenderPatch::
                    renderObjectDetour
                ),

                &mRenderObjectOriginal,

                pl::memory::
                    HookPriority::Normal
            );

        if(
            !mRenderObjectHook
            ||
            !mRenderObjectHook->
                installed()
            ||
            !mRenderObjectOriginal
        ) {

            logger.error(
                "renderObject hook failed"
            );

            uninstall(
                context
            );

            return false;
        }

        mItemTransformHook=
            std::make_unique<
                pl::memory::HookHandle
            >(
                reinterpret_cast<void*>(
                    mItemTransformTarget
                ),

                reinterpret_cast<void*>(
                    &OffhandBlockRenderPatch::
                    itemTransformDetour
                ),

                &mItemTransformOriginal,

                pl::memory::
                    HookPriority::Normal
            );

        if(
            !mItemTransformHook
            ||
            !mItemTransformHook->
                installed()
            ||
            !mItemTransformOriginal
        ) {

            logger.error(
                "item transform hook failed"
            );

            uninstall(
                context
            );

            return false;
        }

        gRenderItemRouteHook=
            std::make_unique<pl::memory::HookHandle>(
                reinterpret_cast<void*>(mRenderItemTarget),
                reinterpret_cast<void*>(&renderItemRouteDetour),
                &gRenderItemRouteOriginal,
                pl::memory::HookPriority::Normal
            );

        if(
            !gRenderItemRouteHook
            || !gRenderItemRouteHook->installed()
            || !gRenderItemRouteOriginal
        ) {
            logger.error("TPP reference RenderItem hook failed");
            uninstall(context);
            return false;
        }

        gAttachableStateRouteHook=
            std::make_unique<pl::memory::HookHandle>(
                reinterpret_cast<void*>(gAttachableStateRouteTarget),
                reinterpret_cast<void*>(&attachableStateRouteDetour),
                &gAttachableStateRouteOriginal,
                pl::memory::HookPriority::Normal
            );

        if(
            !gAttachableStateRouteHook
            || !gAttachableStateRouteHook->installed()
            || !gAttachableStateRouteOriginal
        ) {
            logger.error("TPP attachable-state reference hook failed");
            uninstall(context);
            return false;
        }

        gPrepareAttachmentHook=
            std::make_unique<pl::memory::HookHandle>(
                reinterpret_cast<void*>(gPrepareAttachmentTarget),
                reinterpret_cast<void*>(&prepareAttachmentDetour),
                &gPrepareAttachmentOriginal,
                pl::memory::HookPriority::Normal
            );

        if(
            !gPrepareAttachmentHook
            || !gPrepareAttachmentHook->installed()
            || !gPrepareAttachmentOriginal
        ) {
            logger.error("native attachment preparation hook failed");
            uninstall(context);
            return false;
        }

        gPrepareAttachmentOriginalPublished.store(
            gPrepareAttachmentOriginal,
            std::memory_order_release
        );

        gResolveOwnerBoneByNameHook=
            std::make_unique<pl::memory::HookHandle>(
                reinterpret_cast<void*>(gResolveOwnerBoneByNameTarget),
                reinterpret_cast<void*>(&resolveOwnerBoneByNameDetour),
                &gResolveOwnerBoneByNameOriginal,
                pl::memory::HookPriority::Normal
            );

        if(
            !gResolveOwnerBoneByNameHook
            || !gResolveOwnerBoneByNameHook->installed()
            || !gResolveOwnerBoneByNameOriginal
        ) {
            logger.error("native owner-bone resolver hook failed");
            uninstall(context);
            return false;
        }

        gResolveOwnerBoneByNameOriginalPublished.store(
            gResolveOwnerBoneByNameOriginal,
            std::memory_order_release
        );

        // Install the cache gate only after the left-owner resolver is live.
        // The reverse uninstall order prevents a concurrent prepare from
        // being forced into the unmodified right-owner resolver.
        gAttachmentBindingModeHook=
            std::make_unique<pl::memory::HookHandle>(
                reinterpret_cast<void*>(gAttachmentBindingModeTarget),
                reinterpret_cast<void*>(&attachmentBindingModeDetour),
                &gAttachmentBindingModeOriginal,
                pl::memory::HookPriority::Normal
            );

        if(
            !gAttachmentBindingModeHook
            || !gAttachmentBindingModeHook->installed()
            || !gAttachmentBindingModeOriginal
        ) {
            logger.error("native attachment binding-mode hook failed");
            uninstall(context);
            return false;
        }

        gAttachmentBindingModeOriginalPublished.store(
            gAttachmentBindingModeOriginal,
            std::memory_order_release
        );
        gNativeAttachmentTrampolinesAvailable.store(
            true,
            std::memory_order_seq_cst
        );

        gDrawAttachmentHook=
            std::make_unique<pl::memory::HookHandle>(
                reinterpret_cast<void*>(gDrawAttachmentTarget),
                reinterpret_cast<void*>(&drawAttachmentDetour),
                &gDrawAttachmentOriginal,
                pl::memory::HookPriority::Normal
            );

        if(
            !gDrawAttachmentHook
            || !gDrawAttachmentHook->installed()
            || !gDrawAttachmentOriginal
        ) {
            logger.error("native attachment draw hook failed");
            uninstall(context);
            return false;
        }

        gComposeAttachmentBoneMatrixHook=
            std::make_unique<pl::memory::HookHandle>(
                reinterpret_cast<void*>(gComposeAttachmentBoneMatrixTarget),
                reinterpret_cast<void*>(&composeAttachmentBoneMatrixDetour),
                &gComposeAttachmentBoneMatrixOriginal,
                pl::memory::HookPriority::Normal
            );

        if(
            !gComposeAttachmentBoneMatrixHook
            || !gComposeAttachmentBoneMatrixHook->installed()
            || !gComposeAttachmentBoneMatrixOriginal
        ) {
            logger.error("native attachment bone-matrix hook failed");
            uninstall(context);
            return false;
        }

        gFirstPersonDataDrivenHook=
            std::make_unique<
                pl::memory::HookHandle
            >(
                reinterpret_cast<void*>(
                    gFirstPersonDataDrivenTarget
                ),
                reinterpret_cast<void*>(
                    &firstPersonDataDrivenDetour
                ),
                &gFirstPersonDataDrivenOriginal,
                pl::memory::HookPriority::Normal
            );

        if(
            !gFirstPersonDataDrivenHook
            ||
            !gFirstPersonDataDrivenHook->installed()
            ||
            !gFirstPersonDataDrivenOriginal
        ) {
            logger.error(
                "first-person DataDrivenRenderer scope hook failed"
            );
            uninstall(context);
            return false;
        }

        gFinalOffhandMatrixHook=
            std::make_unique<
                pl::memory::HookHandle
            >(
                reinterpret_cast<void*>(
                    gFinalOffhandMatrixTarget
                ),

                reinterpret_cast<void*>(
                    &finalOffhandMatrixTopDetour
                ),

                &gFinalOffhandMatrixOriginal,

                pl::memory::
                    HookPriority::Normal
            );

        if(
            !gFinalOffhandMatrixHook
            ||
            !gFinalOffhandMatrixHook->
                installed()
            ||
            !gFinalOffhandMatrixOriginal
        ) {

            logger.error(
                "tool orientation "
                "final-matrix hook failed"
            );

            uninstall(
                context
            );

            return false;
        }

        gHandEquipPredicateHook=
            std::make_unique<
                pl::memory::HookHandle
            >(
                reinterpret_cast<void*>(
                    gHandEquipPredicateTarget
                ),

                reinterpret_cast<void*>(
                    &handEquipPredicateDetour
                ),

                &gHandEquipPredicateOriginal,

                pl::memory::
                    HookPriority::Normal
            );

        if(
            !gHandEquipPredicateHook
            ||
            !gHandEquipPredicateHook->
                installed()
            ||
            !gHandEquipPredicateOriginal
        ) {

            logger.error(
                "tool offhand dispatch hook failed"
            );

            uninstall(
                context
            );

            return false;
        }

        mFeatureEnabled.store(
            true,
            std::memory_order_release
        );

        invalidateTridentFppBindingGeneration();
        gNativeAttachmentHooksReady.store(
            true,
            std::memory_order_seq_cst
        );

        logger.info(
            "Offhand visual active"
        );

        logger.info(
            "Banner final: "
            "scale=1.56 X=-0.78 Y=-0.28 yaw=180"
        );

        logger.info(
            "Decorated Pot final: "
            "S=1.40 XYZ=(-0.04,-0.24,0.28) "
            "R=(-32.40,-180.00,-39.60)"
        );

        logger.info(
            "Copper Statue final: "
            "S=1.03 XYZ=(-0.32,-0.04,0.08) "
            "R=(-14.40,140.40,3.60)"
        );

        logger.info(
            "Head/Skull frozen: "
            "RIGHT + X=-0.50"
        );

        logger.info(
            "Native tool offhand fix active: "
            "Bow/Crossbow/Trident/Spear "
            "OFF_DISPATCH enabled only at 0xADEA0BC"
        );

        logger.info(
            "Tool transforms use Minecraft native "
            "FIRSTPERSON_LEFT; FishingRod remains "
            "fully vanilla-dispatched"
        );

        logger.info(
            "Tool calibration frozen: "
            "Bow/Crossbow/FishingRod; Spear native"
        );

        logger.info(
            "Bow generic FIRSTPERSON_LEFT active; "
            "native FPP Bow masked only during DataDrivenRenderer pass"
        );

        logger.info(
            "v0.2.56 Bow TPP: generic LEFT route + grip-pivot tilt; "
            "native slot6 Bow draw suppressed"
        );

        logger.info(
            "v0.2.56 Trident FPP: native slot6 3D attachment retained; "
            "generic 2D item form suppressed"
        );

        return true;
    }


    void
    OffhandBlockRenderPatch::
    uninstall(
        pl::mod::ModContext& context
    ) noexcept {

        gNativeAttachmentHooksReady.store(
            false,
            std::memory_order_seq_cst
        );
        gNativeAttachmentTrampolinesAvailable.store(
            false,
            std::memory_order_seq_cst
        );
        waitForNativeAttachmentHookReaders();
        gAttachmentBindingModeOriginalPublished.store(
            nullptr,
            std::memory_order_release
        );
        gResolveOwnerBoneByNameOriginalPublished.store(
            nullptr,
            std::memory_order_release
        );
        gPrepareAttachmentOriginalPublished.store(
            nullptr,
            std::memory_order_release
        );
        invalidateTridentFppBindingGeneration();

        // Remove the closed entry hooks immediately after admitted readers
        // drain, minimizing the interval in which a brand-new native caller
        // would see the safe no-op/direct fallback.
        if(gAttachmentBindingModeHook) {
            gAttachmentBindingModeHook->reset();
            gAttachmentBindingModeHook.reset();
        }
        gAttachmentBindingModeOriginal=nullptr;
        gAttachmentBindingModeTarget=0;

        if(gResolveOwnerBoneByNameHook) {
            gResolveOwnerBoneByNameHook->reset();
            gResolveOwnerBoneByNameHook.reset();
        }
        gResolveOwnerBoneByNameOriginal=nullptr;
        gResolveOwnerBoneByNameTarget=0;

        if(gPrepareAttachmentHook) {
            gPrepareAttachmentHook->reset();
            gPrepareAttachmentHook.reset();
        }
        gPrepareAttachmentOriginal=nullptr;
        gPrepareAttachmentTarget=0;

        if(
            gHandEquipPredicateHook
        ) {

            gHandEquipPredicateHook->
                reset();

            gHandEquipPredicateHook.
                reset();
        }

        gHandEquipPredicateOriginal=nullptr;
        gHandEquipPredicateTarget=0;

        if(gComposeAttachmentBoneMatrixHook) {
            gComposeAttachmentBoneMatrixHook->reset();
            gComposeAttachmentBoneMatrixHook.reset();
        }
        gComposeAttachmentBoneMatrixOriginal=nullptr;
        gComposeAttachmentBoneMatrixTarget=0;

        if(gDrawAttachmentHook) {
            gDrawAttachmentHook->reset();
            gDrawAttachmentHook.reset();
        }
        gDrawAttachmentOriginal=nullptr;
        gDrawAttachmentTarget=0;

        if(gFirstPersonDataDrivenHook) {
            gFirstPersonDataDrivenHook->reset();
            gFirstPersonDataDrivenHook.reset();
        }
        gFirstPersonDataDrivenOriginal=nullptr;
        gFirstPersonDataDrivenTarget=0;
        gGetOffhandStackTarget=0;

        if(gAttachableStateRouteHook) {
            gAttachableStateRouteHook->reset();
            gAttachableStateRouteHook.reset();
        }
        gAttachableStateRouteOriginal=nullptr;
        gAttachableStateRouteTarget=0;

        if(gRenderItemRouteHook) {
            gRenderItemRouteHook->reset();
            gRenderItemRouteHook.reset();
        }
        gRenderItemRouteOriginal=nullptr;

        if(
            gFinalOffhandMatrixHook
        ) {

            gFinalOffhandMatrixHook->
                reset();

            gFinalOffhandMatrixHook.
                reset();
        }

        gFinalOffhandMatrixOriginal=nullptr;
        gFinalOffhandMatrixTarget=0;
        gToolMatrixMultiplyTarget=0;

        gItemStackMatchesTarget=0;
        gMinecraftBase=0;

        if(
            mItemTransformHook
        ) {

            mItemTransformHook->
                reset();

            mItemTransformHook.
                reset();
        }

        if(
            mRenderObjectHook
        ) {

            mRenderObjectHook->
                reset();

            mRenderObjectHook.
                reset();
        }

        if(
            mBlockPredicateHook
        ) {

            mBlockPredicateHook->
                reset();

            mBlockPredicateHook.
                reset();
        }

        if(
            mRenderOffhandHook
        ) {

            mRenderOffhandHook->
                reset();

            mRenderOffhandHook.
                reset();
        }

        if(
            sInstance
            ==
            this
        ) {
            sInstance=nullptr;
        }

        gOffhandDepth=0;
        gRenderer=nullptr;
        gPlayer=nullptr;

        gBridgeDepth=0;
        gBridgeConsumed=false;

        gLastPolicyItem=nullptr;
        gLastBridgeItem=nullptr;
        gLastTransformItem=nullptr;
        gLastSkullTransformItem=nullptr;

        gLastNativeToolItem=nullptr;
        gDispatchFixLoggedMask=0;

        gCurrentToolFamily=
            ToolFamily::None;

        gToolFinalMatrixApplied=false;

        gLastCalibratedToolItem=nullptr;
        gLastSuppressedTridentItem=nullptr;
        gRenderItemRouteDepth=0;
        gRenderItemRouteFamily=ToolFamily::None;
        gRenderItemRouteSlot=0;
        gRenderItemRouteCallsiteRva=0;
        gRenderItemAttachableCheckSeen=false;
        gRenderItemNativeAttachable=false;
        gRenderItemForcedGeneric=false;
        gTppReferenceLoggedMask=0;
        gBowTppNativeSuppressLogged=false;
        gTridentFppNativeSuppressLogged=false;
        gTridentFppGenericLogged=false;
        gShieldFppReferenceLogged=false;
        gShieldFppObjectLogged=false;

        gBowTppBindingDepth=0;
        gTridentFppBindingDepth=0;
        gFirstPersonDataDrivenDepth=0;
        gBowTppAttachmentDepth=0;
        gTridentFppAttachmentDepth=0;
        gResolvedTridentFppBindingBones.clear();
        gBowTppBindingLogged=false;
        gTridentFppBindingLogged=false;
        gTridentFppBindingCacheLogged=false;
        gBowTppLocalPoseLogged=false;
        gTridentFppLocalPoseLogged=false;
        gTridentFppPrepareProbeLogged=false;
        gTridentFppBindingProbeCount=0;


        mRenderOffhandOriginal=nullptr;
        mBlockPredicateOriginal=nullptr;
        mRenderObjectOriginal=nullptr;
        mItemTransformOriginal=nullptr;

        mRenderOffhandTarget=0;
        mBlockPredicateTarget=0;
        mRenderObjectTarget=0;
        mRenderItemTarget=0;
        mCanTessellateTarget=0;
        mItemTransformTarget=0;
        mDefaultTransformTarget=0;
        mMatrixMultiplyTarget=0;

        context.logger().info(
            "Levi Offhand visual hooks removed"
        );
    }


    void
    OffhandBlockRenderPatch::
    setToolCalibration(
        ToolCalibrationFamily family,
        ToolCalibrationAxis axis,
        float value
    ) noexcept {

        const std::size_t fi=
            calibrationFamilyIndex(
                family
            );

        const std::size_t ai=
            calibrationAxisIndex(
                axis
            );

        if(
            fi>=kCalibrationFamilyCount
            ||
            ai>=kCalibrationAxisCount
        ) {
            return;
        }

        gToolCalibration[fi][ai].
            store(
                value,
                std::memory_order_release
            );

        gLastCalibratedToolItem=nullptr;

        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[CalibrationValue] "
            "family=%u axis=%u value=%.3f",
            static_cast<unsigned>(
                fi
            ),
            static_cast<unsigned>(
                ai
            ),
            static_cast<double>(
                value
            )
        );
    }


    void
    OffhandBlockRenderPatch::
    resetToolCalibration() noexcept {

        for(
            auto& family:
            gToolCalibration
        ) {

            for(
                auto& value:
                family
            ) {

                value.store(
                    0.0f,
                    std::memory_order_release
                );
            }
        }

        gLastCalibratedToolItem=nullptr;
    }


    OffhandBlockRenderPatch::ToolCalibration
    OffhandBlockRenderPatch::
    toolCalibration(
        ToolCalibrationFamily family
    ) const noexcept {

        ToolCalibration result{};

        const std::size_t fi=
            calibrationFamilyIndex(
                family
            );

        if(
            fi>=kCalibrationFamilyCount
        ) {
            return result;
        }

        const auto& row=
            gToolCalibration[fi];

        result.posX=
            row[
                calibrationAxisIndex(
                    ToolCalibrationAxis::PosX
                )
            ].
            load(
                std::memory_order_acquire
            );

        result.posY=
            row[
                calibrationAxisIndex(
                    ToolCalibrationAxis::PosY
                )
            ].
            load(
                std::memory_order_acquire
            );

        result.posZ=
            row[
                calibrationAxisIndex(
                    ToolCalibrationAxis::PosZ
                )
            ].
            load(
                std::memory_order_acquire
            );

        result.rotX=
            row[
                calibrationAxisIndex(
                    ToolCalibrationAxis::RotX
                )
            ].
            load(
                std::memory_order_acquire
            );

        result.rotY=
            row[
                calibrationAxisIndex(
                    ToolCalibrationAxis::RotY
                )
            ].
            load(
                std::memory_order_acquire
            );

        result.rotZ=
            row[
                calibrationAxisIndex(
                    ToolCalibrationAxis::RotZ
                )
            ].
            load(
                std::memory_order_acquire
            );

        return result;
    }


    void
    OffhandBlockRenderPatch::
    setBowTppHorizontalOffset(
        float value
    ) noexcept {

        const float normalized=
            native_attachment_fix::normalizeBowTppHorizontalOffset(value);
        gBowTppHorizontalOffset.store(
            normalized,
            std::memory_order_release
        );

        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "[BowTppSlider] horizontal=%.3f (TPP only)",
            static_cast<double>(normalized)
        );
    }


    float
    OffhandBlockRenderPatch::
    bowTppHorizontalOffset()
    const noexcept {

        return gBowTppHorizontalOffset.load(
            std::memory_order_acquire
        );
    }


    void
    OffhandBlockRenderPatch::
    setFeatureEnabled(
        bool enabled
    ) noexcept {

        invalidateTridentFppBindingGeneration();
        mFeatureEnabled.store(
            enabled,
            std::memory_order_release
        );

        gLastPolicyItem=nullptr;
        gLastBridgeItem=nullptr;
        gLastTransformItem=nullptr;
        gLastSkullTransformItem=nullptr;

        gLastNativeToolItem=nullptr;
        gDispatchFixLoggedMask=0;

        gCurrentToolFamily=
            ToolFamily::None;

        gToolFinalMatrixApplied=false;

        gLastCalibratedToolItem=nullptr;
        gLastSuppressedTridentItem=nullptr;
        gRenderItemRouteDepth=0;
        gRenderItemRouteFamily=ToolFamily::None;
        gRenderItemRouteSlot=0;
        gRenderItemRouteCallsiteRva=0;
        gRenderItemAttachableCheckSeen=false;
        gRenderItemNativeAttachable=false;
        gRenderItemForcedGeneric=false;
        gTppReferenceLoggedMask=0;
        gBowTppNativeSuppressLogged=false;
        gTridentFppNativeSuppressLogged=false;
        gTridentFppGenericLogged=false;
        gShieldFppReferenceLogged=false;
        gShieldFppObjectLogged=false;

        gBowTppBindingDepth=0;
        gTridentFppBindingDepth=0;
        gFirstPersonDataDrivenDepth=0;
        gBowTppAttachmentDepth=0;
        gTridentFppAttachmentDepth=0;
        gResolvedTridentFppBindingBones.clear();
        gBowTppBindingLogged=false;
        gTridentFppBindingLogged=false;
        gTridentFppBindingCacheLogged=false;
        gBowTppLocalPoseLogged=false;
        gTridentFppLocalPoseLogged=false;
        gTridentFppPrepareProbeLogged=false;
        gTridentFppBindingProbeCount=0;

        __android_log_print(
            ANDROID_LOG_INFO,
            kLogTag,
            "Offhand visual %s",
            enabled
            ?
            "ON"
            :
            "OFF"
        );
    }


    bool
    OffhandBlockRenderPatch::
    featureEnabled()
    const noexcept {

        return
            mFeatureEnabled.load(
                std::memory_order_acquire
            );
    }


    bool
    OffhandBlockRenderPatch::
    installed()
    const noexcept {

        return
            mRenderOffhandHook
            &&
            mRenderOffhandHook->
                installed()

            &&

            mBlockPredicateHook
            &&
            mBlockPredicateHook->
                installed()

            &&

            mRenderObjectHook
            &&
            mRenderObjectHook->
                installed()

            &&

            mItemTransformHook
            &&
            mItemTransformHook->
                installed()

            &&

            gHandEquipPredicateHook
            &&
            gHandEquipPredicateHook->
                installed()

            &&

            gRenderItemRouteHook
            &&
            gRenderItemRouteHook->installed()

            &&

            gAttachableStateRouteHook
            &&
            gAttachableStateRouteHook->installed()

            &&

            gPrepareAttachmentHook
            &&
            gPrepareAttachmentHook->installed()
            &&

            gAttachmentBindingModeHook
            &&
            gAttachmentBindingModeHook->installed()
            &&

            gResolveOwnerBoneByNameHook
            &&
            gResolveOwnerBoneByNameHook->installed()
            &&

            gDrawAttachmentHook
            &&
            gDrawAttachmentHook->installed()
            &&

            gComposeAttachmentBoneMatrixHook
            &&
            gComposeAttachmentBoneMatrixHook->installed()
            &&

            gFirstPersonDataDrivenHook
            &&
            gFirstPersonDataDrivenHook->installed()

            &&

            gFinalOffhandMatrixHook
            &&
            gFinalOffhandMatrixHook->
                installed();
    }


    void
    OffhandBlockRenderPatch::
    renderOffhandDetour(
        void* self,
        void* renderContext,
        void* player,
        std::uint32_t itemFlags
    ) noexcept {

        auto* instance=
            sInstance;

        if(
            !instance
            ||
            !instance->
                mRenderOffhandOriginal
        ) {
            return;
        }

        const auto original=
            reinterpret_cast<
                RenderOffhandFn
            >(
                instance->
                    mRenderOffhandOriginal
            );

        OffhandScope
            scope(
                self,
                player
            );

        void* stack=
            offhandStackMutable();

        const void* item=
            offhandItem();

        const char* itemClass=
            rttiName(
                item
            );

        const ToolFamily toolFamily=
            instance->
                featureEnabled()
            ?
            classifyTool(
                stack
            )
            :
            ToolFamily::None;

        ToolRenderScope
            toolRenderScope(
                toolFamily
            );

        if(
            instance->featureEnabled()
            && toolFamily==ToolFamily::Trident
            && !gTridentFppGenericLogged
        ) {
            gTridentFppGenericLogged=true;
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[TridentFppNative3D] renderOffhandItem observed; "
                "generic 2D submission will be suppressed"
            );
        }

        if(
            instance->featureEnabled()
            && isShieldItem(itemClass)
            && !gShieldFppReferenceLogged
        ) {
            gShieldFppReferenceLogged=true;
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[ShieldFppReference] Shield entered renderOffhandItem"
            );
        }

        if(
            toolFamily
            !=
            ToolFamily::None
            &&
            item
            !=
            gLastNativeToolItem
        ) {

            gLastNativeToolItem=
                item;

            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[NativeToolOffhand] %s "
                "entered renderOffhandItem",
                toolFamilyName(
                    toolFamily
                )
            );
        }

        if(
            instance->
                featureEnabled()
            &&
            isBannerItem(
                itemClass
            )
            &&
            stack
            &&
            item
        ) {

            const void* wall=
                readValue<const void*>(
                    item,
                    kBannerWallBlockOffset,
                    nullptr
                );

            const void* standing=
                readValue<const void*>(
                    item,
                    kBannerStandingBlockOffset,
                    nullptr
                );

            StackBlockOverride
                blockOverride(
                    stack,
                    standing
                );

            if(
                blockOverride.
                    active()
            ) {

                bool expected=false;

                if(
                    instance->
                    mBannerBridgeLogged.
                    compare_exchange_strong(
                        expected,
                        true,
                        std::memory_order_relaxed
                    )
                ) {

                    __android_log_print(
                        ANDROID_LOG_INFO,
                        kLogTag,
                        "[BannerFix] "
                        "block bridge active once: "
                        "wall=%p standing=%p",
                        wall,
                        standing
                    );
                }
            }

            original(
                self,
                renderContext,
                player,
                itemFlags
            );

            return;
        }

        original(
            self,
            renderContext,
            player,
            itemFlags
        );
    }


    bool
    OffhandBlockRenderPatch::
    blockRenderPredicateDetour(
        const void* block
    ) noexcept {

        auto* instance=
            sInstance;

        if(
            !instance
            ||
            !instance->
                mBlockPredicateOriginal
        ) {
            return false;
        }

        const auto original=
            reinterpret_cast<
                BlockPredicateFn
            >(
                instance->
                    mBlockPredicateOriginal
            );

        const bool vanilla=
            original(
                block
            );

        if(
            !instance->
                featureEnabled()
            ||
            !inOffhand()
        ) {
            return vanilla;
        }

        if(vanilla) {
            return true;
        }

        const void* item=
            offhandItem();

        const void* currentBlock=
            offhandBlock();

        const char* itemClass=
            rttiName(
                item
            );

        const char* blockClass=
            rttiName(
                blockTypeOf(
                    currentBlock
                )
            );

        if(
            knownGoodNativeSpecial(
                blockClass
            )
        ) {
            return false;
        }

        if(
            currentBlock
            &&
            specialFamily(
                itemClass,
                blockClass
            )
        ) {

            if(
                item
                !=
                gLastPolicyItem
            ) {

                gLastPolicyItem=
                    item;

                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "[RenderPolicy] SPECIAL "
                    "item=%s block=%s "
                    "-> BLOCK TRANSFORM",
                    itemClass,
                    blockClass
                );
            }

            return true;
        }

        return
            canTessellate(
                instance->
                    mCanTessellateTarget
            );
    }


    OffhandBlockRenderPatch::Matrix64
    OffhandBlockRenderPatch::
    itemTransformDetour(
        void* transforms,
        std::uint32_t type
    ) noexcept {

        auto* instance=
            sInstance;

        if(
            !instance
            ||
            !instance->
                mItemTransformOriginal
        ) {
            return {};
        }

        const auto original=
            reinterpret_cast<
                ItemTransformFn
            >(
                instance->
                    mItemTransformOriginal
            );

        if(
            !instance->
                featureEnabled()
            ||
            !inOffhand()
            ||
            gBridgeDepth!=0
            ||
            type
            !=
            kFirstpersonLeftHand
        ) {

            return
                original(
                    transforms,
                    type
                );
        }

        const void* item=
            offhandItem();

        const void* block=
            offhandBlock();

        const char* itemClass=
            rttiName(
                item
            );

        const char* blockClass=
            rttiName(
                blockTypeOf(
                    block
                )
            );

        const ToolFamily toolFamily=
            classifyTool(
                offhandStack()
            );

        /*
         * Tool calibration intentionally does NOT live in ItemTransforms.
         * Several held-tool branches bypass this FIRSTPERSON_LEFT result.
         * Return Minecraft's native transform unchanged here; the temporary
         * sliders are applied later at the proven final hand matrix using its
         * normalized local basis. Trident remains dedicated and slider-free.
         */
        if(
            toolFamily
            !=
            ToolFamily::None
        ) {
            return
                original(
                    transforms,
                    type
                );
        }

        if(
            !specialFamily(
                itemClass,
                blockClass
            )
            ||
            knownGoodNativeSpecial(
                blockClass
            )
        ) {

            return
                original(
                    transforms,
                    type
                );
        }

        if(
            skullFamily(
                itemClass,
                blockClass
            )
        ) {

            Matrix64 rightHand=
                original(
                    transforms,
                    kFirstpersonRightHand
                );

            const float oldX=
                rightHand.value[12];

            rightHand.value[12]+=
                kSkullExtraLeftTranslation;

            if(
                item
                !=
                gLastSkullTransformItem
            ) {

                gLastSkullTransformItem=
                    item;

                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "[SkullFix] RIGHT local X "
                    "%.4f -> %.4f",
                    static_cast<double>(
                        oldX
                    ),
                    static_cast<double>(
                        rightHand.value[12]
                    )
                );
            }

            return rightHand;
        }

        bool banner=false;
        bool pot=false;

        (void)
        currentBannerOrPot(
            banner,
            pot
        );

        if(
            banner
            ||
            pot
        ) {

            if(
                !instance->
                    mDefaultTransformTarget
                ||
                !instance->
                    mMatrixMultiplyTarget
            ) {

                return
                    original(
                        transforms,
                        type
                    );
            }

            const auto getDefault=
                reinterpret_cast<
                    DefaultTransformFn
                >(
                    instance->
                        mDefaultTransformTarget
                );

            const auto multiply=
                reinterpret_cast<
                    MatrixMultiplyFn
                >(
                    instance->
                        mMatrixMultiplyTarget
                );

            const std::uint32_t
                mainhandType=
                    kFirstpersonRightHand;

            const Matrix64
                defaultMainhand=
                    getDefault(
                        &mainhandType
                    );

            const Matrix64
                jsonMainhand=
                    original(
                        transforms,
                        kFirstpersonRightHand
                    );

            Matrix64 result=
                multiply(
                    &defaultMainhand,
                    &jsonMainhand
                );

            if(banner) {

                scaleMatrixBasis(
                    result,
                    kBannerScale
                );

                const float savedX=
                    result.value[12];

                const float savedY=
                    result.value[13];

                const float savedZ=
                    result.value[14];

                const Matrix64 yaw=
                    localYRotationDegrees(
                        kBannerYawDegrees
                    );

                result=
                    multiply(
                        &result,
                        &yaw
                    );

                result.value[12]=
                    savedX
                    +
                    kBannerShiftX;

                result.value[13]=
                    savedY
                    +
                    kBannerShiftY;

                result.value[14]=
                    savedZ;
            }

            if(pot) {

                scaleMatrixBasis(
                    result,
                    kPotScale
                );

                applyIndependentEuler(
                    result,
                    multiply,
                    kPotRotX,
                    kPotRotY,
                    kPotRotZ
                );

                result.value[12]+=
                    kPotX;

                result.value[13]+=
                    kPotY;

                result.value[14]+=
                    kPotZ;
            }

            if(
                item
                !=
                gLastTransformItem
            ) {

                gLastTransformItem=
                    item;

                std::atomic_bool& once=
                    banner
                    ?
                    instance->
                        mBannerCompositeLogged
                    :
                    instance->
                        mPotCompositeLogged;

                bool expected=false;

                if(
                    once.
                    compare_exchange_strong(
                        expected,
                        true,
                        std::memory_order_relaxed
                    )
                ) {

                    if(banner) {

                        __android_log_print(
                            ANDROID_LOG_INFO,
                            kLogTag,
                            "[TransformFix] Banner final "
                            "lowered + bottom-safe scale"
                        );
                    }

                    else {

                        __android_log_print(
                            ANDROID_LOG_INFO,
                            kLogTag,
                            "[TransformFix] DecoratedPot "
                            "final calibration active"
                        );
                    }
                }
            }

            return result;
        }

        if(
            isCopperGolemStatue(
                itemClass,
                blockClass
            )
        ) {

            Matrix64 copper=
                original(
                    transforms,
                    kFirstpersonRightHand
                );

            scaleMatrixBasis(
                copper,
                kCopperScale
            );

            if(
                instance->
                    mMatrixMultiplyTarget
            ) {

                const auto multiply=
                    reinterpret_cast<
                        MatrixMultiplyFn
                    >(
                        instance->
                            mMatrixMultiplyTarget
                    );

                applyIndependentEuler(
                    copper,
                    multiply,
                    kCopperRotX,
                    kCopperRotY,
                    kCopperRotZ
                );
            }

            copper.value[12]+=
                kCopperX;

            copper.value[13]+=
                kCopperY;

            copper.value[14]+=
                kCopperZ;

            if(
                item
                !=
                gLastTransformItem
            ) {

                gLastTransformItem=
                    item;

                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "[TransformFix] CopperStatue "
                    "final calibration active"
                );
            }

            return copper;
        }

        return
            original(
                transforms,
                kFirstpersonRightHand
            );
    }


    void
    OffhandBlockRenderPatch::
    renderObjectDetour(
        void* self,
        void* renderContext,
        const void* renderObject,
        const void* renderMetadata,
        std::uint32_t itemFlags
    ) noexcept {

        auto* instance=
            sInstance;

        if(
            !instance
            ||
            !instance->
                mRenderObjectOriginal
        ) {
            return;
        }

        const auto original=
            reinterpret_cast<
                RenderObjectFn
            >(
                instance->
                    mRenderObjectOriginal
            );

        if(
            gBridgeDepth
            !=
            0
        ) {

            original(
                self,
                renderContext,
                renderObject,
                renderMetadata,
                itemFlags
            );

            return;
        }

        if(
            !instance->
                featureEnabled()
            ||
            !inOffhand()
        ) {

            original(
                self,
                renderContext,
                renderObject,
                renderMetadata,
                itemFlags
            );

            return;
        }

        const void* stack=
            offhandStack();

        const ToolFamily toolFamily=
            classifyTool(
                stack
            );

        /*
         * v0.2.56: the visible Trident must come only from Minecraft's native
         * slot-6 DataDriven attachment.  Suppress the duplicate generic item
         * form here; this is the 2D sprite observed in v0.2.55.
         */
        if(toolFamily==ToolFamily::Trident) {
            if(gLastSuppressedTridentItem!=offhandItem()) {
                gLastSuppressedTridentItem=offhandItem();
                __android_log_print(
                    ANDROID_LOG_INFO,
                    kLogTag,
                    "[TridentFppNative3D] suppress generic 2D item form"
                );
            }
            return;
        }

        const void* referenceItem=offhandItem();
        if(
            kReferenceRouteDiagnostic
            && referenceItem
            && isShieldItem(rttiName(referenceItem))
            && !gShieldFppObjectLogged
        ) {
            gShieldFppObjectLogged=true;
            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[ShieldFppReference] Shield reached generic renderObject"
            );
        }

        if(
            !currentSpecialFamily()
        ) {

            original(
                self,
                renderContext,
                renderObject,
                renderMetadata,
                itemFlags
            );

            return;
        }

        if(
            gBridgeConsumed
        ) {
            return;
        }

        const void* item=
            offhandItem();

        if(
            !stack
            ||
            !item
            ||
            !gPlayer
            ||
            !instance->
                mRenderItemTarget
        ) {

            original(
                self,
                renderContext,
                renderObject,
                renderMetadata,
                itemFlags
            );

            return;
        }

        gBridgeConsumed=true;

        const void* block=
            offhandBlock();

        const char* blockClass=
            rttiName(
                blockTypeOf(
                    block
                )
            );

        if(
            item
            !=
            gLastBridgeItem
        ) {

            gLastBridgeItem=
                item;

            __android_log_print(
                ANDROID_LOG_INFO,
                kLogTag,
                "[SpecialBridge] "
                "item=%s block=%s "
                "posJson=0 matrixAsIs=1 mainHand=0",
                rttiName(
                    item
                ),
                blockClass
            );
        }

        const auto renderItem=
            reinterpret_cast<
                RenderItemFn
            >(
                instance->
                    mRenderItemTarget
            );

        BridgeScope
            bridgeScope;

        renderItem(
            self,
            renderContext,
            gPlayer,
            stack,
            false,
            itemFlags,
            true,
            false
        );
    }

} // namespace levioffhand::render
