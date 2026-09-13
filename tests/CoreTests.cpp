#include "core/SortEngine.hpp"
#include "core/ClickPlanner.hpp"
#include "runtime/BedrockCompatibility.hpp"
#include "runtime/ChestRuntime.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace betterinventory::core;

namespace {
int failures = 0;

void check(bool condition, const char *expr, const char *test, int line) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL " << test << ':' << line << " expected " << expr << '\n';
    }
}

#define CHECK(expr) check(static_cast<bool>(expr), #expr, __func__, __LINE__)

ItemIdentity item(std::string id, int max = 64, int aux = 0, std::string fingerprint = {}) {
    return ItemIdentity{std::move(id), aux, std::move(fingerprint), max};
}

SlotSnapshot stack(std::string id, int count, int max = 64, int aux = 0,
                   std::string fingerprint = {}) {
    return SlotSnapshot::occupied(item(std::move(id), max, aux, std::move(fingerprint)), count);
}

InventorySnapshot padded(std::vector<SlotSnapshot> slots, std::size_t size) {
    slots.resize(size, SlotSnapshot::empty());
    return InventorySnapshot{std::move(slots)};
}

void test_sort_merges_and_orders() {
    auto input = padded({stack("minecraft:stone", 32), stack("minecraft:apple", 4),
                         stack("minecraft:stone", 40), stack("minecraft:diamond_sword", 1, 1),
                         stack("minecraft:apple", 9)}, 27);
    auto result = sortSnapshot(input);
    CHECK(result.ok());
    CHECK(result.snapshot.slots.size() == 27);
    CHECK(result.snapshot.slots[0] == stack("minecraft:apple", 13));
    CHECK(result.snapshot.slots[1] == stack("minecraft:diamond_sword", 1, 1));
    CHECK(result.snapshot.slots[2] == stack("minecraft:stone", 64));
    CHECK(result.snapshot.slots[3] == stack("minecraft:stone", 8));
    CHECK(result.snapshot.slots[4].isEmpty());
}

void test_sort_keeps_component_variants_separate() {
    auto input = padded({stack("minecraft:diamond_sword", 1, 1, 0, "sharpness=5"),
                         stack("minecraft:diamond_sword", 1, 1, 0, "smite=5")}, 27);
    auto result = sortSnapshot(input);
    CHECK(result.ok());
    CHECK(result.snapshot.slots[0].identity->componentFingerprint == "sharpness=5");
    CHECK(result.snapshot.slots[1].identity->componentFingerprint == "smite=5");
}

void test_sort_respects_aux_value() {
    auto input = padded({stack("minecraft:wool", 8, 64, 14), stack("minecraft:wool", 9, 64, 0)}, 27);
    auto result = sortSnapshot(input);
    CHECK(result.ok());
    CHECK(result.snapshot.slots[0].identity->aux == 0);
    CHECK(result.snapshot.slots[1].identity->aux == 14);
}

void test_sort_preserves_large_chest_slot_count() {
    auto input = padded({stack("minecraft:cobblestone", 64), stack("minecraft:cobblestone", 64),
                         stack("minecraft:torch", 3)}, 54);
    auto result = sortSnapshot(input);
    CHECK(result.ok());
    CHECK(result.snapshot.slots.size() == 54);
    CHECK(result.snapshot.totalCount("minecraft:cobblestone") == 128);
    CHECK(result.snapshot.totalCount("minecraft:torch") == 3);
}

void test_sort_rejects_invalid_stack_count() {
    InventorySnapshot input{{SlotSnapshot::occupied(item("minecraft:stone"), 65)}};
    auto result = sortSnapshot(input);
    CHECK(!result.ok());
    CHECK(!result.error.empty());
}

void test_sort_nonstackable_items_remain_individual() {
    auto input = padded({stack("minecraft:bow", 1, 1, 0, "durability=10"),
                         stack("minecraft:bow", 1, 1, 0, "durability=11")}, 27);
    auto result = sortSnapshot(input);
    CHECK(result.ok());
    CHECK(result.snapshot.nonEmptyCount() == 2);
}



void test_click_planner_reaches_sorted_small_chest() {
    auto input = padded({stack("minecraft:stone", 32), stack("minecraft:apple", 4),
                         stack("minecraft:stone", 40), stack("minecraft:diamond_sword", 1, 1),
                         stack("minecraft:apple", 9)}, 27);
    auto target = sortSnapshot(input);
    CHECK(target.ok());
    auto plan = planLeftClicks(input, target.snapshot);
    CHECK(plan.ok());
    CHECK(!plan.actions.empty());
    for (const auto& action : plan.actions) {
        CHECK(action.collection == "container_items");
        CHECK(action.kind == ClickKind::Left);
        CHECK(action.slot < 27);
    }
    auto simulation = simulateLeftClicks(input, plan.actions);
    CHECK(simulation.ok());
    CHECK(simulation.cursor.isEmpty());
    CHECK(simulation.snapshot == target.snapshot);
}

void test_click_planner_reaches_sorted_large_chest() {
    auto input = padded({stack("minecraft:torch", 2), stack("minecraft:stone", 12),
                         stack("minecraft:apple", 2), stack("minecraft:stone", 60),
                         stack("minecraft:torch", 63), stack("minecraft:apple", 62)}, 54);
    auto target = sortSnapshot(input);
    CHECK(target.ok());
    auto plan = planLeftClicks(input, target.snapshot);
    CHECK(plan.ok());
    auto simulation = simulateLeftClicks(input, plan.actions);
    CHECK(simulation.ok());
    CHECK(simulation.cursor.isEmpty());
    CHECK(simulation.snapshot == target.snapshot);
}

void test_click_planner_keeps_component_variants_distinct() {
    auto input = padded({stack("minecraft:potion", 1, 1, 0, "effect=swiftness"),
                         stack("minecraft:potion", 1, 1, 0, "effect=healing")}, 27);
    auto target = sortSnapshot(input);
    CHECK(target.ok());
    auto plan = planLeftClicks(input, target.snapshot);
    CHECK(plan.ok());
    auto simulation = simulateLeftClicks(input, plan.actions);
    CHECK(simulation.ok());
    CHECK(simulation.snapshot == target.snapshot);
}

void test_click_planner_rejects_different_slot_counts() {
    auto current = padded({stack("minecraft:stone", 1)}, 27);
    auto target = padded({stack("minecraft:stone", 1)}, 54);
    auto plan = planLeftClicks(current, target);
    CHECK(!plan.ok());
}

void test_click_planner_rejects_different_item_totals() {
    auto current = padded({stack("minecraft:stone", 1)}, 27);
    auto target = padded({stack("minecraft:stone", 2)}, 27);
    auto plan = planLeftClicks(current, target);
    CHECK(!plan.ok());
}

void test_chest_runtime_sorting_gate_defaults_on_and_toggles() {
    betterinventory::runtime::ChestRuntime runtime;
    CHECK(runtime.sortingEnabled());
    runtime.setSortingEnabled(false);
    CHECK(!runtime.sortingEnabled());
    runtime.setSortingEnabled(true);
    CHECK(runtime.sortingEnabled());
}

void test_bedrock_compatibility_metadata_matches_target() {
    using namespace betterinventory::runtime;
    const auto& c = compatibility();
    CHECK(c.minecraftVersion == "1.26.45.1");
    CHECK(c.buildId == "868e275cb295e9a275bb29d2258edc2f7dc48761");
    CHECK(c.containerCollection == "container_items");
    CHECK(c.smallChestScreen == "chest.small_chest_screen");
    CHECK(c.largeChestScreen == "chest.large_chest_screen");
    CHECK(c.containerSlotPressedVtableIndex == 62);
    CHECK(c.visualItemStackVtableIndex == 71);
    CHECK(c.itemStackWeakItemOffset == 0x08);
    CHECK(c.itemStackUserDataOffset == 0x10);
    CHECK(c.itemStackAuxOffset == 0x20);
    CHECK(c.itemStackCountOffset == 0x22);
    CHECK(c.itemMaxStackSizeOffset == 0xA8);
    CHECK(c.itemIdOffset == 0xAA);
    CHECK(c.screenControllerHandleEventRva == 0x096D73DC);
}

} // namespace

int main() {
    test_sort_merges_and_orders();
    test_sort_keeps_component_variants_separate();
    test_sort_respects_aux_value();
    test_sort_preserves_large_chest_slot_count();
    test_sort_rejects_invalid_stack_count();
    test_sort_nonstackable_items_remain_individual();
    test_click_planner_reaches_sorted_small_chest();
    test_click_planner_reaches_sorted_large_chest();
    test_click_planner_keeps_component_variants_distinct();
    test_click_planner_rejects_different_slot_counts();
    test_click_planner_rejects_different_item_totals();
    test_chest_runtime_sorting_gate_defaults_on_and_toggles();
    test_bedrock_compatibility_metadata_matches_target();
    if (failures != 0) {
        std::cerr << failures << " assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All Better Inventory core tests passed\n";
    return EXIT_SUCCESS;
}
