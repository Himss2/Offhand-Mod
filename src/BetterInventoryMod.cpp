#include "runtime/ChestRuntime.hpp"

#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

#include <string_view>

namespace betterinventory {
namespace {
constexpr char kModuleId[] = "better_inventory.chest_sorting";
}

class BetterInventoryMod {
public:
    static BetterInventoryMod& instance() {
        static BetterInventoryMod value;
        return value;
    }

    BetterInventoryMod() : mSelf(*ll::mod::NativeMod::current()) {}

    bool load() {
        mSelf.getLogger().info("Better Inventory v0.2.0 loading (Chest / Large Chest)");
        return true;
    }

    bool enable() {
        mRuntime.setSortingEnabled(true);
        if (!mRuntime.enable()) {
            mSelf.getLogger().error("Better Inventory runtime disabled: {}", mRuntime.lastError());
            return false;
        }

        const bool moduleRegistered =
            pl::modmenu::ModuleBuilder(kModuleId, "Better Inventory")
                .modId(mSelf.getId())
                .description("Sort Chest and Large Chest contents using vanilla inventory interactions.")
                .defaultEnabled(true)
                .hideInHudEditor(true)
                .onToggle(onModuleToggle)
                .registerModule();

        if (!moduleRegistered) {
            mSelf.getLogger().error("Failed to register Better Inventory in Mod Menu");
            mRuntime.disable();
            return false;
        }

        mModuleRegistered = true;
        mSelf.getLogger().info(
            "Better Inventory runtime enabled for Minecraft 1.26.45.1; JSON-UI sort events: {} / {}",
            runtime::ChestRuntime::smallSortButtonId(), runtime::ChestRuntime::largeSortButtonId());
        mSelf.getLogger().info("Registered Mod Menu module {}", kModuleId);
        return true;
    }

    bool disable() {
        unregisterModule();
        mRuntime.disable();
        mSelf.getLogger().info("Better Inventory runtime disabled");
        return true;
    }

    bool unload() {
        unregisterModule();
        mRuntime.disable();
        return true;
    }

private:
    ll::mod::NativeMod& mSelf;
    runtime::ChestRuntime mRuntime;
    bool mModuleRegistered{};

    static void onModuleToggle(std::string_view moduleId, bool enabled) {
        if (moduleId != kModuleId) return;
        auto& self = instance();
        self.mRuntime.setSortingEnabled(enabled);
        self.mSelf.getLogger().info(
            "Better Inventory chest sorting {}", enabled ? "enabled" : "disabled");
    }

    void unregisterModule() {
        if (!mModuleRegistered) return;
        pl::modmenu::unregisterModule(kModuleId);
        mModuleRegistered = false;
    }
};

} // namespace betterinventory

PL_REGISTER_MOD(betterinventory::BetterInventoryMod, betterinventory::BetterInventoryMod::instance())
