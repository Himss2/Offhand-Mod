#include "render/OffhandBlockRenderPatch.hpp"
#include "runtime/OffhandValidationHook.hpp"

#include <android/log.h>
#include <string_view>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

namespace levioffhand {
namespace {

constexpr char kModuleId[]="levi_offhand.offhand";
constexpr char kLogTag[]="Levi Offhand";

using Patch=render::OffhandBlockRenderPatch;

void onModuleToggle(
    std::string_view moduleId,
    bool enabled
) {
    if(moduleId!=kModuleId) {
        return;
    }

    runtime::OffhandValidationHook::instance().setFeatureEnabled(enabled);
    Patch::instance().setFeatureEnabled(enabled);

    __android_log_print(
        ANDROID_LOG_INFO,
        kLogTag,
        "Mod Menu Offhand = %s",
        enabled?"ON":"OFF"
    );
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
        if(!runtime::OffhandValidationHook::instance().install(context)) {
            context.logger().error(
                "Levi Offhand: storage installation failed"
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
                    "Arbitrary offhand storage/rendering. "
                    "v0.2.56 Bow TPP tilt + native Trident 3D."
                )
                .defaultEnabled(true)
                .hideInHudEditor(true)
                .onToggle(onModuleToggle)
                .registerModule();

        if(!registered) {
            context.logger().error(
                "Levi Offhand: Mod Menu registration failed"
            );
            patch.uninstall(context);
            runtime::OffhandValidationHook::instance().uninstall(context);
            return false;
        }

        mModMenuRegistered=true;

        context.logger().info("Levi Offhand registered in Mod Menu");
        context.logger().info(
            "v0.2.56 Bow TPP grip-pivot + native Trident 3D active"
        );
        context.logger().info(
            "Bow FPP fixed: generic FIRSTPERSON_LEFT with native DataDriven Bow masked"
        );
        context.logger().info(
            "Bow TPP v0.2.56: generic LEFT route; grip fixed, upper tilt corrected"
        );
        context.logger().info(
            "Trident FPP v0.2.56: native 3D retained; owner rightitem -> leftitem only"
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
        runtime::OffhandValidationHook::instance().uninstall(context);
        return true;
    }

    bool unload(pl::mod::ModContext& context) {
        unregisterModMenu();
        Patch::instance().uninstall(context);
        runtime::OffhandValidationHook::instance().uninstall(context);
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
