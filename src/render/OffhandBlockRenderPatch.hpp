#pragma once

#include <atomic>
#include <cstdint>
#include <memory>

#include <pl/Mod.hpp>

namespace pl::memory {
class HookHandle;
} // namespace pl::memory

namespace levioffhand::render {

class OffhandBlockRenderPatch final {
public:
    struct Matrix64 {
        float value[16];
    };

    enum class ToolCalibrationFamily : std::uint8_t {
        Bow = 0,
        Crossbow = 1,
        Spear = 2,
        FishingRod = 3,
        Trident = 4,
    };

    enum class ToolCalibrationAxis : std::uint8_t {
        PosX = 0,
        PosY = 1,
        PosZ = 2,
        RotX = 3,
        RotY = 4,
        RotZ = 5,
    };

    struct ToolCalibration {
        float posX{};
        float posY{};
        float posZ{};
        float rotX{};
        float rotY{};
        float rotZ{};
    };

    static OffhandBlockRenderPatch& instance() noexcept;
    ~OffhandBlockRenderPatch();

    bool install(pl::mod::ModContext& context) noexcept;
    void uninstall(pl::mod::ModContext& context) noexcept;

    void setFeatureEnabled(bool enabled) noexcept;
    [[nodiscard]] bool featureEnabled() const noexcept;
    [[nodiscard]] bool installed() const noexcept;

    // Temporary calibration API. These controls will be removed once the
    // chosen values are frozen as compiled defaults.
    void setToolCalibration(
        ToolCalibrationFamily family,
        ToolCalibrationAxis axis,
        float value
    ) noexcept;

    void resetToolCalibration() noexcept;

    [[nodiscard]] ToolCalibration toolCalibration(
        ToolCalibrationFamily family
    ) const noexcept;

    // Kept for source/ABI parity with the v0.2.53 Bow TPP calibration path.
    // v0.2.55 diagnostic routing does not expose this as a Mod Menu control,
    // but the out-of-line implementation still exists and must be declared.
    void setBowTppHorizontalOffset(float value) noexcept;
    [[nodiscard]] float bowTppHorizontalOffset() const noexcept;

private:
    OffhandBlockRenderPatch() = default;

    static void renderOffhandDetour(
        void* self,
        void* renderContext,
        void* player,
        std::uint32_t itemFlags
    ) noexcept;

    static bool blockRenderPredicateDetour(const void* block) noexcept;

    static void renderObjectDetour(
        void* self,
        void* renderContext,
        const void* renderObject,
        const void* renderMetadata,
        std::uint32_t itemFlags
    ) noexcept;

    static Matrix64 itemTransformDetour(
        void* transforms,
        std::uint32_t type
    ) noexcept;

private:
    static OffhandBlockRenderPatch* sInstance;

    std::unique_ptr<pl::memory::HookHandle> mRenderOffhandHook;
    std::unique_ptr<pl::memory::HookHandle> mBlockPredicateHook;
    std::unique_ptr<pl::memory::HookHandle> mRenderObjectHook;
    std::unique_ptr<pl::memory::HookHandle> mItemTransformHook;

    void* mRenderOffhandOriginal{nullptr};
    void* mBlockPredicateOriginal{nullptr};
    void* mRenderObjectOriginal{nullptr};
    void* mItemTransformOriginal{nullptr};

    std::uintptr_t mRenderOffhandTarget{0};
    std::uintptr_t mBlockPredicateTarget{0};
    std::uintptr_t mRenderObjectTarget{0};
    std::uintptr_t mRenderItemTarget{0};
    std::uintptr_t mCanTessellateTarget{0};
    std::uintptr_t mItemTransformTarget{0};
    std::uintptr_t mDefaultTransformTarget{0};
    std::uintptr_t mMatrixMultiplyTarget{0};

    std::atomic_bool mFeatureEnabled{true};
    std::atomic_bool mBannerBridgeLogged{false};
    std::atomic_bool mBannerCompositeLogged{false};
    std::atomic_bool mPotCompositeLogged{false};
};

} // namespace levioffhand::render
