#include "render/OffhandBlockRenderPatch.hpp"
#include "runtime/NativeOffhandPolicy.hpp"
#include "runtime/AutoInsertRouting.hpp"

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
                    "Native arbitrary offhand storage with protected automatic routing "
                    "and native-only Bow/Trident rendering."
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
            runtime::NativeOffhandPolicy::instance().setFeatureEnabled(false);
            runtime::AutoInsertRouting::instance().setFeatureEnabled(false);
            return false;
        }

        mModMenuRegistered=true;

        context.logger().info("Levi Offhand registered in Mod Menu");
        context.logger().info(
            "Native-only Bow/Trident renderer active for Minecraft 1.26.45.1"
        );
        context.logger().info(
            "Bow slot6 keeps native animation and resolves its owner to leftitem"
        );
        context.logger().info(
            "Trident slot6 keeps native mode-3 binding and mirrors only FPP pole pose"
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
