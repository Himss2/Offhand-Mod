// Execute production detours with fake native ABI objects. These tests do not
// emulate Bedrock transactions or establish in-game compatibility.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <utility>
#include <vector>
#include <string>
#include <iostream>
#include <dlfcn.h>
#include <link.h>
#include "runtime/ActionHandContext.hpp"
#include "runtime/OffhandPlacementAnimation.hpp"
#include <pl/Mod.hpp>
#define private public
#include "runtime/RightUseRouter.hpp"
#undef private

static constexpr std::uintptr_t testBase = 0x10000000;
static int testDladdr(const void*, Dl_info* out) {
    out->dli_fname = "libminecraftpe.so";
    return 1;
}
static int testIterate(int (*callback)(dl_phdr_info*, std::size_t, void*), void* arg) {
    dl_phdr_info info{};
    info.dlpi_name = "libminecraftpe.so";
    info.dlpi_addr = testBase;
    return callback(&info, sizeof(info), arg);
}
#define dladdr testDladdr
#define dl_iterate_phdr testIterate
#include "runtime/RightUseRouter.cpp"
#undef dladdr
#undef dl_iterate_phdr

using namespace levioffhand::runtime;
struct Item { void** table; int damage = 0; int duration = 0; };
struct Stack {
    void* table{};
    const Item** weak{};
    void* userData{};
    void* block{};
    std::uint16_t aux{};
    std::uint8_t count{};
    bool valid{true};
    int id{};
    std::array<std::byte, 112> pad{};
};
static_assert(offsetof(Stack, count) == 0x22);
static_assert(offsetof(Stack, valid) == 0x23);
static_assert(sizeof(Stack) == 0x98);
struct Player { void* table{}; } player;
struct GameMode { void* table{}; const Player* owner = &player; } gameMode;
static std::array<void*, 134> mainTable{}, offTable{};
static Item mainItem{mainTable.data()}, offItem{offTable.data()};
static const Item* mainWeak = &mainItem;
static const Item* offWeak = &offItem;
static Stack mainStack, offStack, activeStack;
static std::vector<unsigned char> calls;
static std::uint32_t mainResult = 0, offResult = 1;
static bool usingItem = false, startUse = false, detached = true;
static int copies = 0, destroys = 0;
static bool mutateOffOnMain = false;
static bool mutateOffCountOnBlock = false;
static int offInputCount = -1;
static int offhandSetterCalls = 0;
static int damage(const void* p) { return static_cast<const Item*>(p)->damage; }
static int duration(const void* p, const void*) { return static_cast<const Item*>(p)->duration; }
static bool cannotAttack(const void*) { return false; }
static bool isNull(const void* p) { return !p || static_cast<const Stack*>(p)->count == 0; }
static bool differs(const void* a, const void* b) {
    return static_cast<const Stack*>(a)->id != static_cast<const Stack*>(b)->id;
}
static const void* selected(const void*) { return &mainStack; }
static const void* offhand(const void*) { return &offStack; }
static void setHand(void* owner, unsigned char hand, const void* stack) {
    if (owner == &player && hand == 1 && stack != nullptr) {
        ++offhandSetterCalls;
        offStack = *static_cast<const Stack*>(stack);
    }
}
static bool isUsing(const void*) { return usingItem; }
static const void* active(const void*) { return &activeStack; }
static void copyStack(void* out, const void* in) { ++copies; std::memcpy(out, in, sizeof(Stack)); }
static void destroyStack(void*) { ++destroys; }
static std::uint32_t blockUse(void*, const void* stack, const void*, int, const void*, unsigned char hand, std::uintptr_t, bool) {
    calls.push_back(hand);
    if (hand == 1) {
        detached &= stack != &offStack;
        if (mutateOffCountOnBlock && offResult != 0) {
            auto* mutableStack = const_cast<Stack*>(
                static_cast<const Stack*>(stack)
            );
            if (mutableStack->count != 0) {
                --mutableStack->count;
            }
        }
    }
    return hand == 0 ? mainResult : offResult;
}
static bool airUse(void*, const void* stack, unsigned char hand) {
    calls.push_back(hand);
    if (hand == 0 && mutateOffOnMain) offStack.count = 7;
    if (hand == 1) {
        offInputCount = static_cast<const Stack*>(stack)->count;
        detached &= stack != &offStack;
        if (startUse) { usingItem = true; activeStack = offStack; }
    }
    return hand == 0 ? mainResult : offResult;
}
static void configureTable(std::array<void*,134>& t) {
    t[0x30/8] = reinterpret_cast<void*>(&duration);
    t[0x130/8] = reinterpret_cast<void*>(&damage);
    t[0x298/8] = reinterpret_cast<void*>(&cannotAttack);
    t[0x290/8] = reinterpret_cast<void*>(testBase + kBaseItemUseRva);
    t[0x1a8/8] = reinterpret_cast<void*>(testBase + kBaseItemRequiresInteractRva);
    t[0x418/8] = reinterpret_cast<void*>(testBase + kBaseItemUseOnRva);
}
static bool check(bool ok, const char* message) {
    if (!ok) std::cerr << "FAIL: " << message << '\n';
    return ok;
}
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    configureTable(mainTable); configureTable(offTable);
    player.table = mainTable.data();
    mainStack.weak = &mainWeak; mainStack.id = 10; mainStack.count = 1;
    offStack.weak = &offWeak; offStack.id = 20; offStack.count = 16;
    auto& router = RightUseRouter::instance();
    RightUseRouter::sInstance = &router;
    router.mFeatureEnabled = true;
    router.mSelectedItemOriginal = reinterpret_cast<void*>(&selected);
    router.mUseItemOnBlockOriginal = reinterpret_cast<void*>(&blockUse);
    router.mBaseUseItemOriginal = reinterpret_cast<void*>(&airUse);
    gGetOffhandSlot = offhand; gSetItemInHandSlot = setHand;
    gStackIsNull = isNull;
    gPlayerIsUsingItem = isUsing; gItemInUseStack = active;
    gStackDiffersForUse = differs; gItemStackCopyCtor = copyStack; gItemStackDtor = destroyStack;
    const std::string test = argv[1];
    bool ok = true;
    if (test == "sword") {
        mainItem.damage = 7;
        // Exact native WeaponItem::use is mov x0,x1; ret. An override is not an action.
        mainTable[0x290/8] = reinterpret_cast<void*>(testBase + 0xFD66F30);
        ok &= check(!stackClaimsMainhandRightClick(&mainStack), "no-op sword must yield right-click");
        RightUseRouter::useItemOnBlockDetour(&gameMode, &mainStack, nullptr, 0, nullptr, 0, 0, false);
        ok &= check(calls == std::vector<unsigned char>{1}, "sword must not swallow offhand placement");
    } else if (test == "off_count_sync") {
        mainItem.damage = 7;
        mainTable[0x290/8] = reinterpret_cast<void*>(testBase + 0xFD66F30);
        offStack.count = 16;
        mutateOffCountOnBlock = true;
        const auto result = RightUseRouter::useItemOnBlockDetour(
            &gameMode, &mainStack, nullptr, 0, nullptr, 0, 0, false
        );
        ok &= check(result == 1, "OFF placement must remain handled");
        ok &= check(detached, "OFF placement must keep detached transaction input");
        ok &= check(offStack.count == 15, "OFFHAND count must reconcile 16 -> 15");
        ok &= check(offhandSetterCalls == 1, "native OFFHAND setter must run once");
    } else if (test == "main_pass" || test == "both_pass" || test == "main_success" || test == "main_terminal") {
        mainTable[0x418/8] = reinterpret_cast<void*>(testBase + 0x1000AF40);
        if (test == "main_success") mainResult = 1;
        if (test == "main_terminal") mainResult = 2;
        if (test == "both_pass") offResult = 0;
        const auto result = RightUseRouter::useItemOnBlockDetour(&gameMode, &mainStack, nullptr, 0, nullptr, 0, 0, false);
        ok &= check(calls == (mainResult ? std::vector<unsigned char>{0} : std::vector<unsigned char>{0,1}), "only PASS falls back; MAIN must run at most once");
        ok &= check(result == (mainResult ? mainResult : offResult), "preserve native result bits");
        ok &= check(detached, "placement uses detached offhand snapshot");
    } else if (test == "air_main_success") {
        mainTable[0x290/8] = reinterpret_cast<void*>(testBase + 0xFF78D40);
        mainResult = 1;
        RightUseRouter::baseUseItemDetour(&gameMode, &mainStack, 0);
        ok &= check(calls == std::vector<unsigned char>{0} && copies == 0,
                    "successful MAIN air-use must not snapshot or attempt OFF");
    } else if (test == "bow_block_pass") {
        mainTable[0x290/8] = reinterpret_cast<void*>(testBase + 0xFF78D40);
        RightUseRouter::useItemOnBlockDetour(&gameMode, &mainStack, nullptr, 0, nullptr, 0, 0, false);
        ok &= check(calls == std::vector<unsigned char>{0}, "defer Bow MAIN self-use to upper dispatcher before OFF block-use");
    } else if (test == "air_snapshot") {
        startUse = true;
        RightUseRouter::baseUseItemDetour(&gameMode, &mainStack, 0);
        ok &= check(detached && copies == 1 && destroys == 1, "self-use requires detached input with balanced lifetime");
        ok &= check(gSessionPlayer == &player, "active offhand use must retain hand ownership");
    } else if (test == "main_scope") {
        usingItem = true; activeStack = offStack; gSessionPlayer = &player;
        ScopedActionHand main(ActionHand::MainHand, ActionKind::UseAir);
        ok &= check(RightUseRouter::selectedItemDetour(&player) == &mainStack, "explicit MAIN scope must override a prior OFF session");
    } else if (test == "off_terminal") {
        offResult = 2;
        const auto result = RightUseRouter::useItemOnBlockDetour(&gameMode, &mainStack, nullptr, 0, nullptr, 0, 0, false);
        ok &= check(result == 2 && calls == std::vector<unsigned char>{1}, "nonzero OFF result must not replay MAIN");
    } else if (test == "air_main_pass") {
        mainTable[0x290/8] = reinterpret_cast<void*>(testBase + 0xFF78D40);
        mutateOffOnMain = true;
        const bool result = RightUseRouter::baseUseItemDetour(&gameMode, &mainStack, 0);
        ok &= check(result && calls == std::vector<unsigned char>{0,1}, "MAIN air PASS falls through once");
        ok &= check(detached && offInputCount == 7 && copies == 1, "OFF snapshot reflects MAIN callback slot changes");
    } else if (test == "missing_snapshot") {
        gItemStackCopyCtor = nullptr;
        RightUseRouter::baseUseItemDetour(&gameMode, &mainStack, 0);
        ok &= check(calls == std::vector<unsigned char>{0} && copies == 0, "missing snapshot helper must preserve MAIN fallback");
    } else if (test == "disabled") {
        router.mFeatureEnabled = false;
        RightUseRouter::useItemOnBlockDetour(&gameMode, &mainStack, nullptr, 0, nullptr, 0, 0, false);
        ok &= check(calls == std::vector<unsigned char>{0}, "disabled router preserves vanilla");
    } else return 2;
    ok &= check(copies == destroys, "all snapshots destroyed");
    if (ok) std::cout << "PASS: " << test << '\n';
    return ok ? 0 : 1;
}
