#include "render/OffhandBlockRenderPatch.hpp"
#include "runtime/NativeOffhandPolicy.hpp"
#include "runtime/AutoInsertRouting.hpp"
#include "runtime/RightUseRouter.hpp"

#include <android/log.h>
#include <string_view>

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

namespace levioffhand {
namespace {

constexpr char kModuleId[]="levi_offhand.offhand";
constexpr char kLogTag[]="Levi Offhand";

using Patch=render::OffhandBlockRenderPatch;

void onModuleToggle(std::string_view moduleId, bool enabled) {
    if(moduleId!=kModuleId) {
        return;
    }

    auto& rightUse=runtime::RightUseRouter::instance();
    if(rightUse.installed()) {
        rightUse.setFeatureEnabled(enabled);
    }

    auto& policy=runtime::NativeOffhandPolicy::instance();
    if(policy.installed()) {
        policy.setFeatureEnabled(enabled);
    }

    auto& autoInsert=runtime::AutoInsertRouting::instance();
    if(autoInsert.installed()) {
        autoInsert.setFeatureEnabled(enabled);
    }

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
        // 1.26.51.1 compatibility is staged by subsystem. Storage policy and
        // right-use validate their own native signatures independently so a
        // stale legacy helper must not disable the entire mod.
        const bool autoInsertInstalled=
            runtime::AutoInsertRouting::instance().install(context);
        if(!autoInsertInstalled) {
            context.logger().warn(
                "Levi Offhand: legacy auto-insert guard unavailable on this game build"
            );
        }

        const bool policyInstalled=
            runtime::NativeOffhandPolicy::instance().install(context);
        if(!policyInstalled) {
            context.logger().warn(
                "Levi Offhand: arbitrary-offhand policy unavailable on this game build"
            );
        } else if(!autoInsertInstalled) {
            context.logger().warn(
                "Levi Offhand: arbitrary-offhand placement restored, but automatic insertion guard still needs 1.26.51.1 revalidation"
            );
        }

        const bool rightUseInstalled=
            runtime::RightUseRouter::instance().install(context);
        if(!rightUseInstalled) {
            context.logger().warn(
                "Levi Offhand: Minecraft 1.26.51.1 right-use routing unavailable"
            );
        }

        auto& patch=Patch::instance();
        const bool visualInstalled=patch.install(context);
        if(!visualInstalled) {
            context.logger().warn(
                "Levi Offhand: legacy visual patch unavailable on this game build"
            );
        }

        const bool registered=
            pl::modmenu::ModuleBuilder(kModuleId,"Offhand")
                .modId(context.id())
                .description(
                    "Offhand compatibility with Minecraft 1.26.51.1 right-use routing; "
                    "left-click remains native mainhand."
                )
                .defaultEnabled(true)
                .hideInHudEditor(true)
                .onToggle(onModuleToggle)
                .registerModule();

        if(!registered) {
            context.logger().error("Levi Offhand: Mod Menu registration failed");
            patch.uninstall(context);
            if(rightUseInstalled) {
                runtime::RightUseRouter::instance().uninstall(context);
            }
            if(policyInstalled) {
                runtime::NativeOffhandPolicy::instance().uninstall(context);
            }
            if(autoInsertInstalled) {
                runtime::AutoInsertRouting::instance().uninstall(context);
            }
            return false;
        }

        mModMenuRegistered=true;
        context.logger().info("Levi Offhand registered in Mod Menu");
        if(policyInstalled) {
            context.logger().info(
                "Minecraft 1.26.51.1 offhand storage policy active"
            );
        }
        if(rightUseInstalled) {
            context.logger().info(
                "Minecraft 1.26.51.1 right-use active: OFFHAND first, MAINHAND fallback; left-click untouched"
            );
        }
        context.logger().info("Levi Offhand ready");
        return true;
    }

    bool disable(pl::mod::ModContext& context) {
        unregisterModMenu();

        auto& rightUse=runtime::RightUseRouter::instance();
        if(rightUse.installed()) {
            rightUse.setFeatureEnabled(false);
        }

        Patch::instance().uninstall(context);

        auto& policy=runtime::NativeOffhandPolicy::instance();
        if(policy.installed()) {
            policy.setFeatureEnabled(false);
        }

        auto& autoInsert=runtime::AutoInsertRouting::instance();
        if(autoInsert.installed()) {
            autoInsert.setFeatureEnabled(false);
        }
        return true;
    }

    bool unload(pl::mod::ModContext& context) {
        unregisterModMenu();
        runtime::RightUseRouter::instance().uninstall(context);
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
