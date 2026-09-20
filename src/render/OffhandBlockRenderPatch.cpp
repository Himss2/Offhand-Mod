#include "runtime/OffhandPlacementAnimation.hpp"
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
        constexpr std::uintptr_t kOffDispatchCallsiteRva=0xADEA0BC;
        constexpr std::uintptr_t kThirdPersonOffhandRenderItemCallsiteRva=0xA32F030;
        constexpr std::uintptr_t kRenderItemAttachableEnabledCallsiteRva=0xADDEADC;
        constexpr std::uintptr_t kAttachableStateRva=0xA32F0F4;
        constexpr std::uint32_t kOffhandInventorySlot=34;
        constexpr std::uintptr_t kNativeAttachmentHandEquipCallsiteRva=0x9B369C8;
        static_assert(
            kNativeAttachmentHandEquipCallsiteRva
            == native_attachment_fix::kNativeAttachmentHandEquipCallsiteRva
        );

        // Exact attachment boundaries recovered from libminecraftpe.so
        // 1.26.45.1. Bow FPP keeps the accepted generic LEFT route. Bow TPP
        // follows the Fishing-Rod-style generic LEFT RenderItem transaction.
        // Native 3D Trident/Spear FPP mirror only the animated rightitem owner
        // carrier before their local pole/spear animation is composed.
        constexpr std::uintptr_t kPrepareAttachmentRva=0x9B36A80;
        constexpr std::uintptr_t kFindOwnerBoneVectorRva=0xF14355C;
        constexpr std::uintptr_t kCopyOwnerMatrixRva=0xF147CC0;
        constexpr std::uintptr_t kOwnerVectorMode2PrimaryCallsiteRva=0x9B377FC;
        constexpr std::uintptr_t kOwnerVectorMode2FallbackCallsiteRva=0x9B3783C;
        constexpr std::uintptr_t kOwnerVectorMode3CallsiteRva=0x9B378B8;
        constexpr std::uintptr_t kOwnerMatrixCopyMode2CallsiteRva=0x9B37870;
        constexpr std::uintptr_t kOwnerMatrixCopyMode3CallsiteRva=0x9B37CB0;
        constexpr std::uintptr_t kResolveOwnerBoneByNameRva=0xAF3A1E4;
        constexpr std::uintptr_t kResolveOwnerBoneFirstCallsiteRva=0x9B3779C;
        constexpr std::uintptr_t kResolveOwnerBoneSecondCallsiteRva=0x9B37814;
        constexpr std::uintptr_t kLegacyAttachmentRouteRva=0x9B368D4;
        constexpr std::uintptr_t kComposeAttachmentBoneMatrixRva=0xF147ED0;

        // Exact native variable.is_first_person path used inside 9B368D4.
        constexpr std::uintptr_t kAttachmentActorTypeRva=0xEC8A478;
        constexpr std::uintptr_t kMolangVariableLookupRva=0xEE63508;
        constexpr std::uintptr_t kMolangValueViewRva=0xEEA721C;
        constexpr std::uintptr_t kVariableIsFirstPersonStringRva=0x2652B1D;
        constexpr std::uint64_t kVariableIsFirstPersonHash=0x2739F381184DE4AEULL;
        constexpr std::uint32_t kLegacyFirstPersonActorType=0x13F;

        constexpr std::array<std::uint8_t,16> kPrepareAttachmentFingerprint{
            0xFD,0x7B,0xBA,0xA9,0xFC,0x6F,0x01,0xA9,
            0xFA,0x67,0x02,0xA9,0xF8,0x5F,0x03,0xA9
        };
        constexpr std::array<std::uint8_t,16> kFindOwnerBoneVectorFingerprint{
            0xFD,0x7B,0xBF,0xA9,0xFD,0x03,0x00,0x91,
            0x09,0x70,0x41,0xF9,0x69,0x06,0x00,0xB4
        };
        constexpr std::array<std::uint8_t,16> kCopyOwnerMatrixFingerprint{
            0x20,0x00,0x40,0xBD,0x00,0x30,0x00,0xBD,
            0x20,0x04,0x40,0xBD,0x00,0x34,0x00,0xBD
        };
        constexpr std::array<std::uint8_t,16> kResolveOwnerBoneByNameFingerprint{
            0xFF,0x43,0x02,0xD1,0xFD,0x7B,0x03,0xA9,
            0xFC,0x6F,0x04,0xA9,0xFA,0x67,0x05,0xA9
        };
        constexpr std::array<std::uint8_t,16> kLegacyAttachmentRouteFingerprint{
            0xFD,0x7B,0xBA,0xA9,0xFC,0x6F,0x01,0xA9,
            0xFA,0x67,0x02,0xA9,0xF8,0x5F,0x03,0xA9
        };
        constexpr std::array<std::uint8_t,16> kAttachmentActorTypeFingerprint{
            0xFD,0x7B,0xBF,0xA9,0xFD,0x03,0x00,0x91,
            0x09,0x08,0x40,0xF9,0x2A,0x83,0x94,0x52
        };
        constexpr std::array<std::uint8_t,16> kMolangVariableLookupFingerprint{
            0xFF,0xC3,0x00,0xD1,0xFD,0x7B,0x01,0xA9,
            0xF4,0x4F,0x02,0xA9,0xFD,0x43,0x00,0x91
        };
        constexpr std::array<std::uint8_t,8> kMolangValueViewFingerprint{
            0x00,0x20,0x00,0x91,0xC0,0x03,0x5F,0xD6
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

        thread_local ToolFamily gCurrentToolFamily=ToolFamily::None;
        thread_local bool gToolFinalMatrixApplied=false;

        thread_local const void* gLastCalibratedToolItem=nullptr;

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

        std::unique_ptr<pl::memory::HookHandle> gHandEquipPredicateHook;
        void* gHandEquipPredicateOriginal=nullptr;
        std::uintptr_t gHandEquipPredicateTarget=0;
        thread_local std::uint32_t gGenericLeftFppLoggedMask=0;

        std::unique_ptr<pl::memory::HookHandle> gRenderItemRouteHook;
        void* gRenderItemRouteOriginal=nullptr;
        std::uintptr_t gRenderItemRouteTarget=0;
        std::unique_ptr<pl::memory::HookHandle> gAttachableStateRouteHook;
        void* gAttachableStateRouteOriginal=nullptr;
        std::uintptr_t gAttachableStateRouteTarget=0;
        thread_local std::uint32_t gBowTppFishingRodDepth=0;
        thread_local bool gBowTppRouteLogged=false;
        thread_local bool gBowTppNativeSuppressLogged=false;

        std::unique_ptr<pl::memory::HookHandle> gPrepareAttachmentHook;
        void* gPrepareAttachmentOriginal=nullptr;
        std::atomic<void*> gPrepareAttachmentOriginalPublished{nullptr};
        std::uintptr_t gPrepareAttachmentTarget=0;

        std::unique_ptr<pl::memory::HookHandle> gFindOwnerBoneVectorHook;
        void* gFindOwnerBoneVectorOriginal=nullptr;
        std::uintptr_t gFindOwnerBoneVectorTarget=0;

        std::unique_ptr<pl::memory::HookHandle> gCopyOwnerMatrixHook;
        void* gCopyOwnerMatrixOriginal=nullptr;
        std::uintptr_t gCopyOwnerMatrixTarget=0;

        std::unique_ptr<pl::memory::HookHandle> gResolveOwnerBoneByNameHook;
        void* gResolveOwnerBoneByNameOriginal=nullptr;
        std::atomic<void*> gResolveOwnerBoneByNameOriginalPublished{nullptr};
        std::uintptr_t gResolveOwnerBoneByNameTarget=0;

        std::unique_ptr<pl::memory::HookHandle> gLegacyAttachmentRouteHook;
        void* gLegacyAttachmentRouteOriginal=nullptr;
        std::uintptr_t gLegacyAttachmentRouteTarget=0;

        std::unique_ptr<pl::memory::HookHandle> gComposeAttachmentBoneMatrixHook;
        void* gComposeAttachmentBoneMatrixOriginal=nullptr;
        std::uintptr_t gComposeAttachmentBoneMatrixTarget=0;

        std::uintptr_t gAttachmentActorTypeTarget=0;
        std::uintptr_t gMolangVariableLookupTarget=0;
        std::uintptr_t gMolangValueViewTarget=0;
        std::uintptr_t gVariableIsFirstPersonStringTarget=0;

        thread_local std::uint32_t gBowOffhandBindingDepth=0;
        thread_local std::uint32_t gNative3dWeaponFppDepth=0;
        thread_local ToolFamily gNative3dFppFamily=ToolFamily::None;
        thread_local bool gBowNativeBindingLogged=false;
        thread_local bool gNative3dLeftCarrierLogged=false;

        // Mutation readiness is separate from trampoline lifetime.  Prepare
        // calls the owner resolver reentrantly, so teardown lets admitted
        // readers finish forwarding before the published trampolines vanish.
        std::atomic_bool gNativeAttachmentHooksReady{false};
        std::atomic_bool gNativeAttachmentTrampolinesAvailable{false};
        std::atomic_uint32_t gActiveNativeAttachmentHookReaders{0};

        struct BindingPrefix {
            std::int32_t ownerBoneIndex{-1};
            std::int32_t ownerGeometryIndex{-1};
            std::uint64_t nameHash{0};
        };

        static_assert(sizeof(BindingPrefix)==16);
        static_assert(offsetof(BindingPrefix,ownerGeometryIndex)==4);
        static_assert(offsetof(BindingPrefix,nameHash)==8);

        struct OwnerBoneVector {
            const std::byte* begin=nullptr;
            const std::byte* end=nullptr;
        };
        thread_local OwnerBoneVector gNative3dOwnerVector{};

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

        [[nodiscard]]
        bool validOwnerBoneVector(const OwnerBoneVector& vector) noexcept {
            if(!vector.begin || !vector.end || vector.end<vector.begin) {
                return false;
            }
            const auto bytes=static_cast<std::size_t>(vector.end-vector.begin);
            return bytes!=0
                && bytes%0xE0==0
                && bytes/0xE0<=256;
        }

        [[nodiscard]]
        const float* findOwnerBoneMatrixByHash(
            const OwnerBoneVector& vector,
            std::uint64_t hash
        ) noexcept {
            if(!validOwnerBoneVector(vector)) {
                return nullptr;
            }
            for(auto* bone=vector.begin;bone<vector.end;bone+=0xE0) {
                if(readValue<std::uint64_t>(bone,8,0)==hash) {
                    return reinterpret_cast<const float*>(
                        bone+native_attachment_fix::kBoneComposedMatrixOffset
                    );
                }
            }
            return nullptr;
        }

        [[nodiscard]]
        const float* findRightOwnerBoneMatrix(
            const OwnerBoneVector& vector
        ) noexcept {
            if(const auto* matrix=findOwnerBoneMatrixByHash(
                vector,native_attachment_fix::kRightItemLowerHash
            )) {
                return matrix;
            }
            return findOwnerBoneMatrixByHash(
                vector,native_attachment_fix::kRightItemCamelHash
            );
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
        bool isExactMinecraftCallsite(
            std::uintptr_t returnAddress,
            std::uintptr_t callsiteRva
        ) noexcept {
            return
                gMinecraftBase!=0
                && returnAddress==gMinecraftBase+callsiteRva+4;
        }

        using HandEquipPredicateFn=bool(*)(const void*);

        bool handEquipPredicateDetour(
            const void* stack
        ) noexcept {
            const auto original=reinterpret_cast<HandEquipPredicateFn>(
                gHandEquipPredicateOriginal
            );
            if(!original) {
                return false;
            }

            const bool vanilla=original(stack);
            if(
                !OffhandBlockRenderPatch::instance().featureEnabled()
                || !stack
            ) {
                return vanilla;
            }

            const auto returnAddress=reinterpret_cast<std::uintptr_t>(
                __builtin_return_address(0)
            );
            const std::uintptr_t callsiteRva=
                returnAddress>=gMinecraftBase+4
                ? returnAddress-gMinecraftBase-4
                : 0;
            const ToolFamily family=classifyTool(stack);

            if(
                gNative3dWeaponFppDepth!=0
                && native_attachment_fix::shouldAdmitNativeSpearFirstPerson(
                    true,
                    family==ToolFamily::Spear,
                    native_attachment_fix::kOffhandSlot,
                    true,
                    callsiteRva
                )
            ) {
                const std::uint32_t bit=
                    1u<<static_cast<std::uint32_t>(family);
                if((gGenericLeftFppLoggedMask&bit)==0) {
                    gGenericLeftFppLoggedMask|=bit;
                    __android_log_print(
                        ANDROID_LOG_INFO,kLogTag,
                        "[NativeSpearFppRoute] slot6 nativePredicate=0"
                    );
                }
                return false;
            }

            if(vanilla) {
                return true;
            }

            if(!isExactMinecraftCallsite(
                returnAddress,kOffDispatchCallsiteRva
            )) {
                return vanilla;
            }

            const bool genericLeft=
                native_attachment_fix::shouldRouteGenericLeftFirstPerson(
                    true,
                    (
                        family==ToolFamily::Bow
                        ||
                        family==ToolFamily::Crossbow
                    ),
                    native_attachment_fix::kOffhandSlot,
                    true
                );
            if(!genericLeft) {
                return vanilla;
            }

            const std::uint32_t bit=
                1u<<static_cast<std::uint32_t>(family);
            if((gGenericLeftFppLoggedMask&bit)==0) {
                gGenericLeftFppLoggedMask|=bit;
                __android_log_print(
                    ANDROID_LOG_INFO,kLogTag,
                    "[GenericLeftFppRoute] %s genericDispatch=1",
                    toolFamilyName(family)
                );
            }
            return true;
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

            const auto returnAddress=reinterpret_cast<std::uintptr_t>(
                __builtin_return_address(0)
            );
            const ToolFamily family=
                OffhandBlockRenderPatch::instance().featureEnabled()
                ? classifyTool(stack)
                : ToolFamily::None;
            const bool bowTppReference=
                family==ToolFamily::Bow
                && slot==kOffhandInventorySlot
                && isExactMinecraftCallsite(
                    returnAddress,kThirdPersonOffhandRenderItemCallsiteRva
                );

            if(!bowTppReference) {
                original(self,renderContext,actor,stack,arg4,slot,arg6,arg7);
                return;
            }

            ++gBowTppFishingRodDepth;
            original(self,renderContext,actor,stack,arg4,slot,arg6,arg7);
            if(gBowTppFishingRodDepth!=0) {
                --gBowTppFishingRodDepth;
            }

            if(!gBowTppRouteLogged) {
                gBowTppRouteLogged=true;
                __android_log_print(
                    ANDROID_LOG_INFO,kLogTag,
                    "[BowFishingRodTppRoute] slot34 caller=0xA32F030 "
                    "genericLeft=1"
                );
            }
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
            if(
                gBowTppFishingRodDepth!=0
                && isExactMinecraftCallsite(
                    reinterpret_cast<std::uintptr_t>(
                        __builtin_return_address(0)
                    ),
                    kRenderItemAttachableEnabledCallsiteRva
                )
            ) {
                // Match Fishing Rod at this exact TPP RenderItem transaction:
                // continue through generic thirdperson_lefthand instead of the
                // attachable branch.  No cross-call pending latch is used.
                return false;
            }
            return nativeResult;
        }

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
            const auto original=reinterpret_cast<PrepareAttachmentFn>(
                readGuard.entered()
                ? gPrepareAttachmentOriginalPublished.load(
                    std::memory_order_acquire
                )
                : gPrepareAttachmentOriginal
            );
            if(!original) {
                return;
            }

            const std::uint32_t slot=readValue<std::uint32_t>(
                slotPointer,0,static_cast<std::uint32_t>(-1)
            );
            const bool isBow=
                OffhandBlockRenderPatch::instance().featureEnabled()
                && stack
                && stackMatchesId(stack,kBowIdRva);
            const bool remapBowOwner=
                native_attachment_fix::shouldRemapBowOwnerBone(
                    isBow,
                    slot,
                    gNativeAttachmentHooksReady.load(
                        std::memory_order_acquire
                    )
                );

            if(remapBowOwner) {
                ++gBowOffhandBindingDepth;
            }

            original(
                self,stack,slotPointer,parentContext,actor,
                isFirstPerson,enabled
            );

            if(remapBowOwner && gBowOffhandBindingDepth!=0) {
                --gBowOffhandBindingDepth;
            }
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
            const auto original=reinterpret_cast<ResolveOwnerBoneByNameFn>(
                readGuard.entered()
                ? gResolveOwnerBoneByNameOriginalPublished.load(
                    std::memory_order_acquire
                )
                : gResolveOwnerBoneByNameOriginal
            );
            if(!original) {
                return false;
            }

            const std::uintptr_t returnAddress=
                reinterpret_cast<std::uintptr_t>(
                    __builtin_return_address(0)
                );
            const bool exactResolverCall=
                isExactMinecraftCallsite(
                    returnAddress,kResolveOwnerBoneFirstCallsiteRva
                )
                || isExactMinecraftCallsite(
                    returnAddress,kResolveOwnerBoneSecondCallsiteRva
                );
            const bool remapBow=
                OffhandBlockRenderPatch::instance().featureEnabled()
                && gNativeAttachmentHooksReady.load(
                    std::memory_order_acquire
                )
                && gBowOffhandBindingDepth!=0;

            if(!remapBow || !exactResolverCall || !bindingState) {
                return original(self,ownerGeometry,bindingState);
            }

            const BindingPrefix source=
                readValue<BindingPrefix>(bindingState,0,{});
            BindingPrefix candidate=source;
            std::uint64_t leftHash=0;
            if(!native_attachment_fix::mapRightOwnerBoneToLeft(
                source.nameHash,leftHash
            )) {
                return original(self,ownerGeometry,bindingState);
            }

            candidate.ownerBoneIndex=-1;
            candidate.ownerGeometryIndex=-1;
            candidate.nameHash=leftHash;
            if(!original(self,ownerGeometry,&candidate)) {
                return original(self,ownerGeometry,bindingState);
            }

            writeValue<std::int32_t>(
                bindingState,0,candidate.ownerBoneIndex
            );
            writeValue<std::int32_t>(
                bindingState,sizeof(std::int32_t),
                candidate.ownerGeometryIndex
            );

            if(!gBowNativeBindingLogged) {
                gBowNativeBindingLogged=true;
                __android_log_print(
                    ANDROID_LOG_INFO,kLogTag,
                    "[BowNativeOwnerBinding] slot6 local=rightitem owner=leftitem"
                );
            }
            return true;
        }

        using FindOwnerBoneVectorFn=void*(*)(
            void*,std::int32_t,bool
        );

        void* findOwnerBoneVectorDetour(
            void* ownerModel,
            std::int32_t geometryId,
            bool createIfMissing
        ) noexcept {
            const auto original=reinterpret_cast<FindOwnerBoneVectorFn>(
                gFindOwnerBoneVectorOriginal
            );
            if(!original) {
                return nullptr;
            }
            void* result=original(ownerModel,geometryId,createIfMissing);
            if(
                !result
                || gNative3dWeaponFppDepth==0
                || !OffhandBlockRenderPatch::instance().featureEnabled()
            ) {
                return result;
            }

            const std::uintptr_t returnAddress=
                reinterpret_cast<std::uintptr_t>(
                    __builtin_return_address(0)
                );
            const bool ownerVectorCall=
                isExactMinecraftCallsite(
                    returnAddress,kOwnerVectorMode2PrimaryCallsiteRva
                )
                || isExactMinecraftCallsite(
                    returnAddress,kOwnerVectorMode2FallbackCallsiteRva
                )
                || isExactMinecraftCallsite(
                    returnAddress,kOwnerVectorMode3CallsiteRva
                );
            if(!ownerVectorCall) {
                return result;
            }

            OwnerBoneVector candidate{
                readValue<const std::byte*>(result,0,nullptr),
                readValue<const std::byte*>(result,sizeof(void*),nullptr)
            };
            if(validOwnerBoneVector(candidate)) {
                gNative3dOwnerVector=candidate;
            }
            return result;
        }

        using CopyOwnerMatrixFn=void(*)(void*,const float*);

        void copyOwnerMatrixDetour(
            void* targetBoneState,
            const float* ownerMatrix
        ) noexcept {
            const auto original=reinterpret_cast<CopyOwnerMatrixFn>(
                gCopyOwnerMatrixOriginal
            );
            if(!original) {
                return;
            }

            const std::uintptr_t returnAddress=
                reinterpret_cast<std::uintptr_t>(
                    __builtin_return_address(0)
                );
            const bool exactOwnerCopy=
                isExactMinecraftCallsite(
                    returnAddress,kOwnerMatrixCopyMode2CallsiteRva
                )
                || isExactMinecraftCallsite(
                    returnAddress,kOwnerMatrixCopyMode3CallsiteRva
                );
            const std::uint64_t boneHash=
                targetBoneState
                ? readValue<std::uint64_t>(targetBoneState,8,0)
                : 0;
            const bool native3dRoot=
                gNative3dWeaponFppDepth!=0
                && exactOwnerCopy
                && (
                    (
                        gNative3dFppFamily==ToolFamily::Trident
                        && boneHash==native_attachment_fix::kPoleBoneHash
                    )
                    || (
                        gNative3dFppFamily==ToolFamily::Spear
                        && boneHash==native_attachment_fix::kSpearBoneHash
                    )
                );
            if(!native3dRoot) {
                original(targetBoneState,ownerMatrix);
                return;
            }

            const float* rightOwnerMatrix=
                findRightOwnerBoneMatrix(gNative3dOwnerVector);
            if(!rightOwnerMatrix) {
                original(targetBoneState,ownerMatrix);
                return;
            }

            native_attachment_fix::Matrix4 leftCarrier{};
            std::memcpy(
                leftCarrier.data(),rightOwnerMatrix,sizeof(leftCarrier)
            );
            const float beforeX=leftCarrier[12];
            const float beforeY=leftCarrier[13];
            const float beforeZ=leftCarrier[14];
            if(!native_attachment_fix::mirrorOwnerCarrierAcrossX(leftCarrier)) {
                original(targetBoneState,ownerMatrix);
                return;
            }

            // Seed only the mirrored owner carrier. Minecraft then composes the
            // native spear/pole local animation unchanged on top of this frame.
            original(targetBoneState,leftCarrier.data());

            if(!gNative3dLeftCarrierLogged) {
                gNative3dLeftCarrierLogged=true;
                __android_log_print(
                    ANDROID_LOG_INFO,kLogTag,
                    "[Native3dLeftCarrier] family=%u "
                    "R=(%.3f,%.3f,%.3f) L=(%.3f,%.3f,%.3f)",
                    static_cast<unsigned>(gNative3dFppFamily),
                    static_cast<double>(beforeX),
                    static_cast<double>(beforeY),
                    static_cast<double>(beforeZ),
                    static_cast<double>(leftCarrier[12]),
                    static_cast<double>(leftCarrier[13]),
                    static_cast<double>(leftCarrier[14])
                );
            }
        }

        using LegacyAttachmentRouteFn=void(*)(
            void*,
            const void*,
            const std::uint32_t*,
            void*,
            void*
        );
        using AttachmentActorTypeFn=std::uint32_t(*)(void*);
        using MolangVariableLookupFn=void*(*)(
            void*,std::uint64_t,const char*
        );
        using MolangValueViewFn=const float*(*)(void*);

        [[nodiscard]]
        bool queryNativeFirstPerson(
            void* parentContext,
            void* actor
        ) noexcept {
            if(
                !parentContext
                || !actor
                || !belongsToMinecraft(gAttachmentActorTypeTarget)
                || !belongsToMinecraft(gMolangVariableLookupTarget)
                || !belongsToMinecraft(gMolangValueViewTarget)
                || !belongsToMinecraft(gVariableIsFirstPersonStringTarget)
            ) {
                return false;
            }

            const auto actorType=reinterpret_cast<AttachmentActorTypeFn>(
                gAttachmentActorTypeTarget
            );
            if(actorType(actor)!=kLegacyFirstPersonActorType) {
                return false;
            }

            void* variableContext=readValue<void*>(parentContext,8,nullptr);
            if(!variableContext) {
                return false;
            }

            const auto lookup=reinterpret_cast<MolangVariableLookupFn>(
                gMolangVariableLookupTarget
            );
            const auto view=reinterpret_cast<MolangValueViewFn>(
                gMolangValueViewTarget
            );
            auto* valueObject=lookup(
                variableContext,
                kVariableIsFirstPersonHash,
                reinterpret_cast<const char*>(
                    gVariableIsFirstPersonStringTarget
                )
            );
            if(!valueObject) {
                return false;
            }
            const float* value=view(valueObject);
            return value && std::isfinite(*value) && *value!=0.0F;
        }

        class ScopedNativeDepth final {
        public:
            explicit ScopedNativeDepth(
                std::uint32_t& depth,
                bool active
            ) noexcept : mDepth(active?&depth:nullptr) {
                if(mDepth) {
                    ++*mDepth;
                }
            }
            ~ScopedNativeDepth() {
                if(mDepth && *mDepth!=0) {
                    --*mDepth;
                }
            }
            ScopedNativeDepth(const ScopedNativeDepth&)=delete;
            ScopedNativeDepth& operator=(const ScopedNativeDepth&)=delete;
        private:
            std::uint32_t* mDepth=nullptr;
        };

        void legacyAttachmentRouteDetour(
            void* self,
            const void* stack,
            const std::uint32_t* slotPointer,
            void* parentContext,
            void* actor
        ) noexcept {
            const auto original=reinterpret_cast<LegacyAttachmentRouteFn>(
                gLegacyAttachmentRouteOriginal
            );
            if(!original) {
                return;
            }

            const std::uint32_t slot=readValue<std::uint32_t>(
                slotPointer,0,static_cast<std::uint32_t>(-1)
            );
            const bool enabled=
                OffhandBlockRenderPatch::instance().featureEnabled()
                && gNativeAttachmentHooksReady.load(
                    std::memory_order_acquire
                );
            const ToolFamily family=enabled
                ? classifyTool(stack)
                : ToolFamily::None;
            const bool nativeFirstPerson=
                enabled
                && slot==native_attachment_fix::kOffhandSlot
                && queryNativeFirstPerson(parentContext,actor);

            const bool genericLeftBowFpp=
                native_attachment_fix::shouldRouteGenericLeftFirstPerson(
                    enabled,
                    family==ToolFamily::Bow,
                    slot,
                    nativeFirstPerson
                );
            if(genericLeftBowFpp) {
                const std::uint32_t bit=
                    1u<<static_cast<std::uint32_t>(family);
                if((gGenericLeftFppLoggedMask&bit)==0) {
                    gGenericLeftFppLoggedMask|=bit;
                    __android_log_print(
                        ANDROID_LOG_INFO,kLogTag,
                        "[GenericLeftFppRoute] Bow slot6 nativeSuppressed=1"
                    );
                }
                return;
            }

            const bool bowWorldTpp=
                enabled
                && gBowTppFishingRodDepth!=0
                && slot==native_attachment_fix::kOffhandSlot
                && !nativeFirstPerson
                && family==ToolFamily::Bow;
            if(bowWorldTpp) {
                if(!gBowTppNativeSuppressLogged) {
                    gBowTppNativeSuppressLogged=true;
                    __android_log_print(
                        ANDROID_LOG_INFO,kLogTag,
                        "[BowFishingRodTppNativeSuppress] slot6 native Bow "
                        "attachment suppressed; generic LEFT world route active"
                    );
                }
                return;
            }

            const bool native3dFpp=
                nativeFirstPerson
                && (
                    family==ToolFamily::Trident
                    || family==ToolFamily::Spear
                );

            const ToolFamily previousNative3dFamily=gNative3dFppFamily;
            if(native3dFpp) {
                gNative3dFppFamily=family;
                gNative3dOwnerVector={};
            }
            ScopedNativeDepth native3dScope(
                gNative3dWeaponFppDepth,native3dFpp
            );
            original(self,stack,slotPointer,parentContext,actor);
            if(native3dFpp) {
                gNative3dFppFamily=previousNative3dFamily;
                gNative3dOwnerVector={};
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
            if(original) {
                original(boneState,pivot,matrix);
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
        if(installed()) {
            return true;
        }

        uninstall(context);
        auto& logger=context.logger();

        mRenderOffhandTarget=pl::memory::resolveSignature(
            kRenderOffhandSignature,kMinecraftLibrary
        );
        mBlockPredicateTarget=pl::memory::resolveSignature(
            kBlockPredicateSignature,kMinecraftLibrary
        );
        mCanTessellateTarget=pl::memory::resolveSignature(
            kCanTessellateSignature,kMinecraftLibrary
        );
        mRenderObjectTarget=pl::memory::resolveSignature(
            kRenderObjectSignature,kMinecraftLibrary
        );
        mItemTransformTarget=pl::memory::resolveSignature(
            kItemTransformSignature,kMinecraftLibrary
        );

        gHandEquipPredicateTarget=pl::memory::resolveSignature(
            kHandEquipPredicateSignature,kMinecraftLibrary
        );

        const std::uintptr_t base=moduleBaseOf(mRenderOffhandTarget);
        if(base) {
            mRenderItemTarget=base+kRenderItemRva;
            gRenderItemRouteTarget=base+kRenderItemRva;
            gAttachableStateRouteTarget=base+kAttachableStateRva;
            mDefaultTransformTarget=base+kDefaultTransformRva;
            mMatrixMultiplyTarget=base+kMatrixMultiplyRva;
            gMinecraftBase=base;
            gItemStackMatchesTarget=base+kItemStackMatchesRva;
            gFinalOffhandMatrixTarget=base+kFinalOffhandMatrixTopRva;

            gPrepareAttachmentTarget=base+kPrepareAttachmentRva;
            gFindOwnerBoneVectorTarget=base+kFindOwnerBoneVectorRva;
            gCopyOwnerMatrixTarget=base+kCopyOwnerMatrixRva;
            gResolveOwnerBoneByNameTarget=base+kResolveOwnerBoneByNameRva;
            gLegacyAttachmentRouteTarget=base+kLegacyAttachmentRouteRva;
            gComposeAttachmentBoneMatrixTarget=
                base+kComposeAttachmentBoneMatrixRva;

            gAttachmentActorTypeTarget=base+kAttachmentActorTypeRva;
            gMolangVariableLookupTarget=base+kMolangVariableLookupRva;
            gMolangValueViewTarget=base+kMolangValueViewRva;
            gVariableIsFirstPersonStringTarget=
                base+kVariableIsFirstPersonStringRva;

            gToolMatrixMultiplyTarget=mMatrixMultiplyTarget;
        }

        const bool nativeTargetsValid=
            belongsToMinecraft(gPrepareAttachmentTarget)
            && belongsToMinecraft(gFindOwnerBoneVectorTarget)
            && belongsToMinecraft(gCopyOwnerMatrixTarget)
            && belongsToMinecraft(gResolveOwnerBoneByNameTarget)
            && belongsToMinecraft(gLegacyAttachmentRouteTarget)
            && belongsToMinecraft(gComposeAttachmentBoneMatrixTarget)
            && belongsToMinecraft(gAttachmentActorTypeTarget)
            && belongsToMinecraft(gMolangVariableLookupTarget)
            && belongsToMinecraft(gMolangValueViewTarget)
            && belongsToMinecraft(gVariableIsFirstPersonStringTarget)
            && matchesFingerprint(
                gPrepareAttachmentTarget,kPrepareAttachmentFingerprint
            )
            && matchesFingerprint(
                gFindOwnerBoneVectorTarget,kFindOwnerBoneVectorFingerprint
            )
            && matchesFingerprint(
                gCopyOwnerMatrixTarget,kCopyOwnerMatrixFingerprint
            )
            && matchesFingerprint(
                gResolveOwnerBoneByNameTarget,
                kResolveOwnerBoneByNameFingerprint
            )
            && matchesFingerprint(
                gLegacyAttachmentRouteTarget,
                kLegacyAttachmentRouteFingerprint
            )
            && matchesFingerprint(
                gComposeAttachmentBoneMatrixTarget,
                kComposeAttachmentBoneMatrixFingerprint
            )
            && matchesFingerprint(
                gAttachmentActorTypeTarget,kAttachmentActorTypeFingerprint
            )
            && matchesFingerprint(
                gMolangVariableLookupTarget,kMolangVariableLookupFingerprint
            )
            && matchesFingerprint(
                gMolangValueViewTarget,kMolangValueViewFingerprint
            )
            && std::strcmp(
                reinterpret_cast<const char*>(
                    gVariableIsFirstPersonStringTarget
                ),
                "variable.is_first_person"
            )==0;

        if(
            !belongsToMinecraft(mRenderOffhandTarget)
            || !belongsToMinecraft(mBlockPredicateTarget)
            || !belongsToMinecraft(mCanTessellateTarget)
            || !belongsToMinecraft(mRenderObjectTarget)
            || !belongsToMinecraft(mItemTransformTarget)
            || !belongsToMinecraft(mRenderItemTarget)
            || !belongsToMinecraft(gRenderItemRouteTarget)
            || !belongsToMinecraft(gAttachableStateRouteTarget)
            || !belongsToMinecraft(mDefaultTransformTarget)
            || !belongsToMinecraft(mMatrixMultiplyTarget)
            || !belongsToMinecraft(gItemStackMatchesTarget)
            || !belongsToMinecraft(gHandEquipPredicateTarget)
            || !belongsToMinecraft(gFinalOffhandMatrixTarget)
            || !nativeTargetsValid
        ) {
            logger.error(
                "Offhand visual: Minecraft 1.26.45.1 target validation failed"
            );
            uninstall(context);
            return false;
        }

        mRenderOffhandOriginal=nullptr;
        mBlockPredicateOriginal=nullptr;
        mRenderObjectOriginal=nullptr;
        mItemTransformOriginal=nullptr;
        gRenderItemRouteOriginal=nullptr;
        gAttachableStateRouteOriginal=nullptr;
        gPrepareAttachmentOriginal=nullptr;
        gResolveOwnerBoneByNameOriginal=nullptr;
        gLegacyAttachmentRouteOriginal=nullptr;
        gComposeAttachmentBoneMatrixOriginal=nullptr;
        gHandEquipPredicateOriginal=nullptr;
        gFinalOffhandMatrixOriginal=nullptr;
        gPrepareAttachmentOriginalPublished.store(nullptr,std::memory_order_release);
        gResolveOwnerBoneByNameOriginalPublished.store(
            nullptr,std::memory_order_release
        );
        gNativeAttachmentHooksReady.store(false,std::memory_order_seq_cst);
        gNativeAttachmentTrampolinesAvailable.store(
            false,std::memory_order_seq_cst
        );

        gBowOffhandBindingDepth=0;
        gNative3dWeaponFppDepth=0;
        gNative3dFppFamily=ToolFamily::None;
        gNative3dOwnerVector={};
        gGenericLeftFppLoggedMask=0;
        gBowTppFishingRodDepth=0;
        gBowNativeBindingLogged=false;
        gNative3dLeftCarrierLogged=false;
        gBowTppRouteLogged=false;
        gBowTppNativeSuppressLogged=false;
        gLastNativeToolItem=nullptr;
        gCurrentToolFamily=ToolFamily::None;
        gToolFinalMatrixApplied=false;
        gLastCalibratedToolItem=nullptr;
        gOffhandDepth=0;
        gRenderer=nullptr;
        gPlayer=nullptr;
        gBridgeDepth=0;
        gBridgeConsumed=false;
        gLastPolicyItem=nullptr;
        gLastBridgeItem=nullptr;
        gLastTransformItem=nullptr;
        gLastSkullTransformItem=nullptr;
        mBannerBridgeLogged.store(false,std::memory_order_relaxed);
        mBannerCompositeLogged.store(false,std::memory_order_relaxed);
        mPotCompositeLogged.store(false,std::memory_order_relaxed);

        sInstance=this;

        auto fail=[&](const char* message) noexcept {
            logger.error(message);
            uninstall(context);
            return false;
        };

        mRenderOffhandHook=std::make_unique<pl::memory::HookHandle>(
            reinterpret_cast<void*>(mRenderOffhandTarget),
            reinterpret_cast<void*>(&OffhandBlockRenderPatch::renderOffhandDetour),
            &mRenderOffhandOriginal,
            pl::memory::HookPriority::Normal
        );
        if(!mRenderOffhandHook || !mRenderOffhandHook->installed()
            || !mRenderOffhandOriginal) {
            return fail("renderOffhand hook failed");
        }

        mBlockPredicateHook=std::make_unique<pl::memory::HookHandle>(
            reinterpret_cast<void*>(mBlockPredicateTarget),
            reinterpret_cast<void*>(
                &OffhandBlockRenderPatch::blockRenderPredicateDetour
            ),
            &mBlockPredicateOriginal,
            pl::memory::HookPriority::Normal
        );
        if(!mBlockPredicateHook || !mBlockPredicateHook->installed()
            || !mBlockPredicateOriginal) {
            return fail("predicate hook failed");
        }

        mRenderObjectHook=std::make_unique<pl::memory::HookHandle>(
            reinterpret_cast<void*>(mRenderObjectTarget),
            reinterpret_cast<void*>(&OffhandBlockRenderPatch::renderObjectDetour),
            &mRenderObjectOriginal,
            pl::memory::HookPriority::Normal
        );
        if(!mRenderObjectHook || !mRenderObjectHook->installed()
            || !mRenderObjectOriginal) {
            return fail("renderObject hook failed");
        }

        mItemTransformHook=std::make_unique<pl::memory::HookHandle>(
            reinterpret_cast<void*>(mItemTransformTarget),
            reinterpret_cast<void*>(&OffhandBlockRenderPatch::itemTransformDetour),
            &mItemTransformOriginal,
            pl::memory::HookPriority::Normal
        );
        if(!mItemTransformHook || !mItemTransformHook->installed()
            || !mItemTransformOriginal) {
            return fail("item transform hook failed");
        }

        gHandEquipPredicateHook=std::make_unique<pl::memory::HookHandle>(
            reinterpret_cast<void*>(gHandEquipPredicateTarget),
            reinterpret_cast<void*>(&handEquipPredicateDetour),
            &gHandEquipPredicateOriginal,
            pl::memory::HookPriority::Normal
        );
        if(
            !gHandEquipPredicateHook
            || !gHandEquipPredicateHook->installed()
            || !gHandEquipPredicateOriginal
        ) {
            return fail("Bow generic-left FPP route hook failed");
        }

        gRenderItemRouteHook=std::make_unique<pl::memory::HookHandle>(
            reinterpret_cast<void*>(gRenderItemRouteTarget),
            reinterpret_cast<void*>(&renderItemRouteDetour),
            &gRenderItemRouteOriginal,
            pl::memory::HookPriority::Normal
        );
        if(
            !gRenderItemRouteHook
            || !gRenderItemRouteHook->installed()
            || !gRenderItemRouteOriginal
        ) {
            return fail("Bow TPP RenderItem route hook failed");
        }

        gAttachableStateRouteHook=std::make_unique<pl::memory::HookHandle>(
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
            return fail("Bow TPP attachable-state route hook failed");
        }

        gPrepareAttachmentHook=std::make_unique<pl::memory::HookHandle>(
            reinterpret_cast<void*>(gPrepareAttachmentTarget),
            reinterpret_cast<void*>(&prepareAttachmentDetour),
            &gPrepareAttachmentOriginal,
            pl::memory::HookPriority::Normal
        );
        if(!gPrepareAttachmentHook || !gPrepareAttachmentHook->installed()
            || !gPrepareAttachmentOriginal) {
            return fail("native Bow attachment prepare hook failed");
        }
        gPrepareAttachmentOriginalPublished.store(
            gPrepareAttachmentOriginal,std::memory_order_release
        );

        gFindOwnerBoneVectorHook=std::make_unique<pl::memory::HookHandle>(
            reinterpret_cast<void*>(gFindOwnerBoneVectorTarget),
            reinterpret_cast<void*>(&findOwnerBoneVectorDetour),
            &gFindOwnerBoneVectorOriginal,
            pl::memory::HookPriority::Normal
        );
        if(
            !gFindOwnerBoneVectorHook
            || !gFindOwnerBoneVectorHook->installed()
            || !gFindOwnerBoneVectorOriginal
        ) {
            return fail("native 3D owner-vector hook failed");
        }

        gCopyOwnerMatrixHook=std::make_unique<pl::memory::HookHandle>(
            reinterpret_cast<void*>(gCopyOwnerMatrixTarget),
            reinterpret_cast<void*>(&copyOwnerMatrixDetour),
            &gCopyOwnerMatrixOriginal,
            pl::memory::HookPriority::Normal
        );
        if(
            !gCopyOwnerMatrixHook
            || !gCopyOwnerMatrixHook->installed()
            || !gCopyOwnerMatrixOriginal
        ) {
            return fail("native 3D owner-matrix copy hook failed");
        }

        gResolveOwnerBoneByNameHook=std::make_unique<pl::memory::HookHandle>(
            reinterpret_cast<void*>(gResolveOwnerBoneByNameTarget),
            reinterpret_cast<void*>(&resolveOwnerBoneByNameDetour),
            &gResolveOwnerBoneByNameOriginal,
            pl::memory::HookPriority::Normal
        );
        if(!gResolveOwnerBoneByNameHook
            || !gResolveOwnerBoneByNameHook->installed()
            || !gResolveOwnerBoneByNameOriginal) {
            return fail("native Bow owner-bone resolver hook failed");
        }
        gResolveOwnerBoneByNameOriginalPublished.store(
            gResolveOwnerBoneByNameOriginal,std::memory_order_release
        );
        gNativeAttachmentTrampolinesAvailable.store(
            true,std::memory_order_seq_cst
        );

        gLegacyAttachmentRouteHook=std::make_unique<pl::memory::HookHandle>(
            reinterpret_cast<void*>(gLegacyAttachmentRouteTarget),
            reinterpret_cast<void*>(&legacyAttachmentRouteDetour),
            &gLegacyAttachmentRouteOriginal,
            pl::memory::HookPriority::Normal
        );
        if(!gLegacyAttachmentRouteHook
            || !gLegacyAttachmentRouteHook->installed()
            || !gLegacyAttachmentRouteOriginal) {
            return fail("native Trident/Spear FPP route hook failed");
        }

        gComposeAttachmentBoneMatrixHook=
            std::make_unique<pl::memory::HookHandle>(
                reinterpret_cast<void*>(gComposeAttachmentBoneMatrixTarget),
                reinterpret_cast<void*>(&composeAttachmentBoneMatrixDetour),
                &gComposeAttachmentBoneMatrixOriginal,
                pl::memory::HookPriority::Normal
            );
        if(!gComposeAttachmentBoneMatrixHook
            || !gComposeAttachmentBoneMatrixHook->installed()
            || !gComposeAttachmentBoneMatrixOriginal) {
            return fail("native Bow TPP compose hook failed");
        }

        gFinalOffhandMatrixHook=std::make_unique<pl::memory::HookHandle>(
            reinterpret_cast<void*>(gFinalOffhandMatrixTarget),
            reinterpret_cast<void*>(&finalOffhandMatrixTopDetour),
            &gFinalOffhandMatrixOriginal,
            pl::memory::HookPriority::Normal
        );
        if(!gFinalOffhandMatrixHook || !gFinalOffhandMatrixHook->installed()
            || !gFinalOffhandMatrixOriginal) {
            return fail("tool orientation final-matrix hook failed");
        }

        mFeatureEnabled.store(true,std::memory_order_release);
        gNativeAttachmentHooksReady.store(true,std::memory_order_seq_cst);

        logger.info("Offhand visual active");
        logger.info(
            "Bow native-only: slot6 local rightitem resolves owner leftitem"
        );
        logger.info(
            "Trident/Spear FPP: native 3D with mirrored rightitem owner carrier"
        );
        logger.info(
            "Bow FPP: one generic LEFT submission with calibrated final matrix"
        );
        logger.info(
            "Bow TPP: Fishing-Rod-style generic thirdperson LEFT route"
        );
        logger.info(
            "Banner/Pot/Copper/Skull and unrelated item paths retained"
        );
        return true;
    }


    void
    OffhandBlockRenderPatch::
    uninstall(
        pl::mod::ModContext& context
    ) noexcept {
        gNativeAttachmentHooksReady.store(false,std::memory_order_seq_cst);
        gNativeAttachmentTrampolinesAvailable.store(
            false,std::memory_order_seq_cst
        );
        waitForNativeAttachmentHookReaders();
        gResolveOwnerBoneByNameOriginalPublished.store(
            nullptr,std::memory_order_release
        );
        gPrepareAttachmentOriginalPublished.store(
            nullptr,std::memory_order_release
        );

        if(gComposeAttachmentBoneMatrixHook) {
            gComposeAttachmentBoneMatrixHook->reset();
            gComposeAttachmentBoneMatrixHook.reset();
        }
        gComposeAttachmentBoneMatrixOriginal=nullptr;
        gComposeAttachmentBoneMatrixTarget=0;

        if(gLegacyAttachmentRouteHook) {
            gLegacyAttachmentRouteHook->reset();
            gLegacyAttachmentRouteHook.reset();
        }
        gLegacyAttachmentRouteOriginal=nullptr;
        gLegacyAttachmentRouteTarget=0;

        if(gCopyOwnerMatrixHook) {
            gCopyOwnerMatrixHook->reset();
            gCopyOwnerMatrixHook.reset();
        }
        gCopyOwnerMatrixOriginal=nullptr;
        gCopyOwnerMatrixTarget=0;

        if(gFindOwnerBoneVectorHook) {
            gFindOwnerBoneVectorHook->reset();
            gFindOwnerBoneVectorHook.reset();
        }
        gFindOwnerBoneVectorOriginal=nullptr;
        gFindOwnerBoneVectorTarget=0;

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

        gAttachmentActorTypeTarget=0;
        gMolangVariableLookupTarget=0;
        gMolangValueViewTarget=0;
        gVariableIsFirstPersonStringTarget=0;

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
        gRenderItemRouteTarget=0;

        if(gHandEquipPredicateHook) {
            gHandEquipPredicateHook->reset();
            gHandEquipPredicateHook.reset();
        }
        gHandEquipPredicateOriginal=nullptr;
        gHandEquipPredicateTarget=0;

        if(gFinalOffhandMatrixHook) {
            gFinalOffhandMatrixHook->reset();
            gFinalOffhandMatrixHook.reset();
        }
        gFinalOffhandMatrixOriginal=nullptr;
        gFinalOffhandMatrixTarget=0;
        gToolMatrixMultiplyTarget=0;

        if(mItemTransformHook) {
            mItemTransformHook->reset();
            mItemTransformHook.reset();
        }
        if(mRenderObjectHook) {
            mRenderObjectHook->reset();
            mRenderObjectHook.reset();
        }
        if(mBlockPredicateHook) {
            mBlockPredicateHook->reset();
            mBlockPredicateHook.reset();
        }
        if(mRenderOffhandHook) {
            mRenderOffhandHook->reset();
            mRenderOffhandHook.reset();
        }

        if(sInstance==this) {
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
        gCurrentToolFamily=ToolFamily::None;
        gToolFinalMatrixApplied=false;
        gLastCalibratedToolItem=nullptr;
        gBowOffhandBindingDepth=0;
        gNative3dWeaponFppDepth=0;
        gNative3dFppFamily=ToolFamily::None;
        gNative3dOwnerVector={};
        gGenericLeftFppLoggedMask=0;
        gBowTppFishingRodDepth=0;
        gBowNativeBindingLogged=false;
        gNative3dLeftCarrierLogged=false;
        gBowTppRouteLogged=false;
        gBowTppNativeSuppressLogged=false;

        mRenderOffhandOriginal=nullptr;
        mBlockPredicateOriginal=nullptr;
        mRenderObjectOriginal=nullptr;
        mItemTransformOriginal=nullptr;
        mRenderOffhandTarget=0;
        mBlockPredicateTarget=0;
        mRenderObjectTarget=0;
        mRenderItemTarget=0;
        gRenderItemRouteTarget=0;
        gAttachableStateRouteTarget=0;
        mCanTessellateTarget=0;
        mItemTransformTarget=0;
        mDefaultTransformTarget=0;
        mMatrixMultiplyTarget=0;
        gItemStackMatchesTarget=0;
        gMinecraftBase=0;

        context.logger().info("Levi Offhand visual hooks removed");
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
    setFeatureEnabled(
        bool enabled
    ) noexcept {
        mFeatureEnabled.store(enabled,std::memory_order_release);
        gLastPolicyItem=nullptr;
        gLastBridgeItem=nullptr;
        gLastTransformItem=nullptr;
        gLastSkullTransformItem=nullptr;
        gLastNativeToolItem=nullptr;
        gCurrentToolFamily=ToolFamily::None;
        gToolFinalMatrixApplied=false;
        gLastCalibratedToolItem=nullptr;
        gBowOffhandBindingDepth=0;
        gNative3dWeaponFppDepth=0;
        gNative3dFppFamily=ToolFamily::None;
        gNative3dOwnerVector={};
        gGenericLeftFppLoggedMask=0;
        gBowTppFishingRodDepth=0;
        gBowNativeBindingLogged=false;
        gNative3dLeftCarrierLogged=false;
        gBowTppRouteLogged=false;
        gBowTppNativeSuppressLogged=false;

        __android_log_print(
            ANDROID_LOG_INFO,kLogTag,
            "Offhand visual %s",enabled?"ON":"OFF"
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
            mRenderOffhandHook && mRenderOffhandHook->installed()
            && mBlockPredicateHook && mBlockPredicateHook->installed()
            && mRenderObjectHook && mRenderObjectHook->installed()
            && mItemTransformHook && mItemTransformHook->installed()
            && gHandEquipPredicateHook && gHandEquipPredicateHook->installed()
            && gRenderItemRouteHook && gRenderItemRouteHook->installed()
            && gAttachableStateRouteHook && gAttachableStateRouteHook->installed()
            && gPrepareAttachmentHook && gPrepareAttachmentHook->installed()
            && gFindOwnerBoneVectorHook
            && gFindOwnerBoneVectorHook->installed()
            && gCopyOwnerMatrixHook
            && gCopyOwnerMatrixHook->installed()
            && gResolveOwnerBoneByNameHook
            && gResolveOwnerBoneByNameHook->installed()
            && gLegacyAttachmentRouteHook
            && gLegacyAttachmentRouteHook->installed()
            && gComposeAttachmentBoneMatrixHook
            && gComposeAttachmentBoneMatrixHook->installed()
            && gFinalOffhandMatrixHook
            && gFinalOffhandMatrixHook->installed();
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
            // 1.26.51.1: never fabricate ItemStack::mBlock from historical
            // BannerItem field offsets. The supplied crash showed that those
            // offsets now yield an invalid small pointer in native item/block
            // lookup. Keep the native item render transaction instead.
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
                    "[BannerFix] safe native item route active; stale Block* bridge disabled"
                );
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

        // Apply only inside the validated first-person OFFHAND block scope.
        const auto placementAnimated=[&](Matrix64 matrix) noexcept -> Matrix64 {
            if(block==nullptr) return matrix;
            const float progress=runtime::OffhandPlacementAnimation::instance().progress();
            if(progress<=0.0f || progress>=1.0f) return matrix;
            const float wave=std::sin(kPi*progress);
            const float impulse=wave*wave;
            matrix.value[12]+=0.08f*impulse;
            matrix.value[13]-=0.18f*impulse;
            matrix.value[14]+=0.12f*impulse;
            if(instance->mMatrixMultiplyTarget) {
                const auto multiply=reinterpret_cast<MatrixMultiplyFn>(instance->mMatrixMultiplyTarget);
                applyIndependentEuler(matrix,multiply,-20.0f*wave,9.0f*wave,7.0f*wave);
            }
            return matrix;
        };

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
            return placementAnimated(original(transforms, type));
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

            return placementAnimated(original(transforms, type));
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

            return placementAnimated(rightHand);
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

                return placementAnimated(original(transforms, type));
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

            return placementAnimated(result);
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

            return placementAnimated(copper);
        }

        return placementAnimated(original(transforms, kFirstpersonRightHand));
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
