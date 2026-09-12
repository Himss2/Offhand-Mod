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
                    "Tool transforms use frozen native calibration values."
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
            "Tool calibration frozen: Bow/Crossbow/FishingRod; Spear native"
        );
        context.logger().info(
            "Bow FPP fixed: generic FIRSTPERSON_LEFT with native DataDriven Bow masked"
        );
        context.logger().info(
            "Bow TPP v0.2.52: local X mirrored plus 0.10 extra-left offset"
        );
        context.logger().info(
            "Trident FPP v0.2.52: cached right-owner binding reopened and resolved left"
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
