#include "render/OffhandBlockRenderPatch.hpp"
#include "runtime/NativeOffhandPolicy.hpp"
#include "runtime/AutoInsertRouting.hpp"

#include <android/log.h>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <string>
#include <string_view>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

namespace levioffhand {
namespace {

constexpr char kModuleId[]="levi_offhand.offhand";
constexpr char kLogTag[]="Levi Offhand";
constexpr char kBowTppTiltKey[]="bow_tpp_tilt";
constexpr char kTridentFppHorizontalKey[]="trident_fpp_horizontal";

using Patch=render::OffhandBlockRenderPatch;

void onModuleToggle(
    std::string_view moduleId,
    bool enabled
) {
    if(moduleId!=kModuleId) {
        return;
    }

    runtime::NativeOffhandPolicy::instance().setFeatureEnabled(enabled);
    runtime::AutoInsertRouting::instance().setFeatureEnabled(enabled);
    Patch::instance().setFeatureEnabled(enabled);

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "Mod Menu Offhand = %s",
        enabled?"ON":"OFF"
    );
}

void onModuleConfigChanged(
    std::string_view moduleId,
    std::string_view key,
    std::string_view value
) {
    if(
        moduleId!=kModuleId
        || (
            key!=kBowTppTiltKey
            && key!=kTridentFppHorizontalKey
        )
    ) {
        return;
    }

    const std::string ownedValue{value};
    char* parseEnd=nullptr;
    errno=0;
    const float parsedValue=std::strtof(ownedValue.c_str(),&parseEnd);
    if(
        errno==ERANGE
        || parseEnd==ownedValue.c_str()
        || parseEnd!=ownedValue.c_str()+ownedValue.size()
        || !std::isfinite(parsedValue)
    ) {
        __android_log_print(
            ANDROID_LOG_WARN,
            kLogTag,
            "[OffhandCalibration] rejected invalid value for %.*s",
            static_cast<int>(key.size()),
            key.data()
        );
        return;
    }

    if(key==kBowTppTiltKey) {
        Patch::instance().setBowTppTiltDegrees(parsedValue);
        return;
    }

    Patch::instance().setTridentFppHorizontalOffset(parsedValue);
}


} // namespace

class LeviOffhandMod final {
public:
    static LeviOffhandMod& instance() noexcept {
        static LeviOffhandMod mod;
        return mod;
    }

    bool load(pl::mod::ModContext& context) {
        context.logger().info("Levi Offhand loaded");
        return true;
    }

    bool enable(pl::mod::ModContext& context) {
        // Install the routing guard before enabling native capability.
        // If the policy patch fails, removing the guard is safe because no
        // new offhand capability was committed by this enable attempt.
        if(!runtime::AutoInsertRouting::instance().install(context)) {
            context.logger().error(
                "Levi Offhand: automatic routing installation failed"
            );
            return false;
        }

        if(!runtime::NativeOffhandPolicy::instance().install(context)) {
            runtime::AutoInsertRouting::instance().uninstall(context);
            context.logger().error(
                "Levi Offhand: native offhand policy installation failed"
            );
            return false;
        }

        auto& patch=Patch::instance();
        const bool visualInstalled=patch.install(context);

        if(!visualInstalled) {
            context.logger().error(
                "Levi Offhand: visual unavailable; stable storage remains active"
            );
        }

        const bool registered=
            pl::modmenu::ModuleBuilder(kModuleId,"Offhand")
                .modId(context.id())
                .description(
                    "Native arbitrary offhand storage with automatic-routing protection. "
                    "Visual calibration remains v0.2.59 behavior."
                )
                .defaultEnabled(true)
                .hideInHudEditor(true)
                .onToggle(onModuleToggle)
                .onConfigChanged(onModuleConfigChanged)
                .config(
                    kBowTppTiltKey,
                    "Bow TPP Tilt (TEMP)",
                    pl::modmenu::ConfigType::SliderFloat,
                    "-25.20",
                    "-45.00",
                    "45.00"
                )
                .config(
                    kTridentFppHorizontalKey,
                    "Trident FPP Horizontal (TEMP)",
                    pl::modmenu::ConfigType::SliderFloat,
                    "0.875",
                    "-1.500",
                    "1.500"
                )
                .registerModule();

        if(!registered) {
            context.logger().error(
                "Levi Offhand: Mod Menu registration failed"
            );
            patch.uninstall(context);
            runtime::NativeOffhandPolicy::instance().setFeatureEnabled(false);
            runtime::AutoInsertRouting::instance().setFeatureEnabled(false);
            return false;
        }

        mModMenuRegistered=true;

        context.logger().info("Levi Offhand registered in Mod Menu");
        context.logger().info(
            "v0.2.59 crash-safe Bow Rot-Z + native-left Trident calibration active"
        );
        context.logger().info(
            "Bow FPP fixed: generic FIRSTPERSON_LEFT with native DataDriven Bow masked"
        );
        context.logger().info(
            "Bow TPP v0.2.59: generic LEFT route + live semantic Rot-Z tilt"
        );
        context.logger().info(
            "Trident FPP v0.2.59: native off_hand->leftitem + pole Z180 + live horizontal"
        );
        context.logger().info(
            "Decorated Pot/Copper calibration frozen as default values"
        );
        context.logger().info("Levi Offhand ready");

        if(visualInstalled) {
            context.logger().info("Ordinary/special block split active");
            context.logger().info(
                "Correct Android UseAnimation diagnostic active"
            );
        }

        return true;
    }

    bool disable(pl::mod::ModContext& context) {
        unregisterModMenu();
        Patch::instance().uninstall(context);
        runtime::NativeOffhandPolicy::instance().setFeatureEnabled(false);
        runtime::AutoInsertRouting::instance().setFeatureEnabled(false);
        return true;
    }

    bool unload(pl::mod::ModContext& context) {
        unregisterModMenu();
        Patch::instance().uninstall(context);
        runtime::AutoInsertRouting::instance().uninstall(context);
        runtime::NativeOffhandPolicy::instance().uninstall(context);
        return true;
    }

private:
    LeviOffhandMod()=default;

    void unregisterModMenu() noexcept {
        if(!mModMenuRegistered) {
            return;
        }
        pl::modmenu::unregisterModule(kModuleId);
        mModMenuRegistered=false;
    }

private:
    bool mModMenuRegistered{false};
};

} // namespace levioffhand

PL_REGISTER_MOD(
    levioffhand::LeviOffhandMod,
    levioffhand::LeviOffhandMod::instance()
)
