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
struct Item {
    void** table{};
    int damage = 0;
    int duration = 0;
    std::array<std::byte, 0x98> pad{};
    std::uint8_t maxStackSize = 64;
    std::byte padA9{};
    std::int16_t itemId = 0;
};
static_assert(offsetof(Item, maxStackSize) == 0xA8);
static_assert(offsetof(Item, itemId) == 0xAA);
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
static int mainWrites = 0, completions = 0, cancellations = 0;
static bool returnContainer = false, emptyReadStayedOff = false;
static void writeMain(void*, const void* stack) { ++mainWrites; mainStack = *static_cast<const Stack*>(stack); }
static void cancelUse(void*) { ++cancellations; usingItem = false; }
static void completeUse(void* owner) {
    ++completions;
    Stack result = *static_cast<const Stack*>(RightUseRouter::selectedItemDetour(owner));
    --result.count;
    if (returnContainer && result.count == 0) { result.id = 99; result.count = 1; }
    RightUseRouter::setSelectedItemDetour(owner, &result);
    if (result.count == 0) emptyReadStayedOff = RightUseRouter::selectedItemDetour(owner) == &offStack;
    usingItem = false;
}
static unsigned char transactionHand = 255;
static int transactions = 0;
static void transaction(void*, unsigned char hand, void*, void (*callback)(void*), void* context) {
    ++transactions; transactionHand = hand; callback(context);
}
static void releaseCallback(void* owner) { completeUse(owner); }
static void releaseUse(void*) {
    RightUseRouter::handTransactionDetour(&player, 0, nullptr, releaseCallback, &player);
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
    router.mCompleteUsingItemOriginal = reinterpret_cast<void*>(&completeUse);
    router.mSetSelectedItemOriginal = reinterpret_cast<void*>(&writeMain);
    gStopUsingItem = cancelUse;
    router.mReleaseUsingItemOriginal = reinterpret_cast<void*>(&releaseUse);
    router.mHandTransactionOriginal = reinterpret_cast<void*>(&transaction);
    gReleaseCallback = reinterpret_cast<std::uintptr_t>(&releaseCallback);
    const std::string test = argv[1];
    bool ok = true;
    if (test == "release_off" || test == "release_main" || test == "release_stale") {
        usingItem = true; activeStack = offStack;
        if (test != "release_main") { gSessionPlayer = &player; gSessionGameMode = &gameMode; }
        else activeStack = mainStack;
        if (test == "release_stale") offStack.id = 77;
        RightUseRouter::releaseUsingItemDetour(&gameMode);
        if (test == "release_stale") {
            ok &= check(transactions == 0 && cancellations == 1 && mainWrites == 0, "stale release cancels without a transaction");
        } else {
            const bool off = test == "release_off";
            ok &= check(transactions == 1 && transactionHand == (off ? 1 : 0), "release uses the correct native transaction hand");
            ok &= check(mainWrites == (off ? 0 : 1) && offhandSetterCalls == (off ? 1 : 0), "release writes only its own slot");
        }
    } else if (test == "transaction_unscoped") {
        gSessionPlayer = &player;
        RightUseRouter::handTransactionDetour(&player, 0, nullptr, releaseCallback, &player);
        ok &= check(transactionHand == 0 && mainWrites == 1, "session alone cannot redirect transactions");
    } else if (test == "consume_food" || test == "consume_container" || test == "consume_last" || test == "consume_native_off") {
        usingItem = true;
        if (test == "consume_last" || test == "consume_container") offStack.count = 1;
        returnContainer = test == "consume_container";
        activeStack = offStack; gSessionPlayer = test == "consume_native_off" ? nullptr : &player; gSessionGameMode = &gameMode;
        RightUseRouter::completeUsingItemDetour(&player);
        ok &= check(completions == 1 && mainWrites == 0 && offhandSetterCalls == 1, "native completion writes OFF exactly once and never MAIN");
        ok &= check(mainStack.id == 10 && mainStack.count == 1, "consumption preserves MAIN");
        ok &= check(offStack.count == ((test == "consume_food" || test == "consume_native_off") ? 15 : returnContainer ? 1 : 0), "native consumed count is preserved");
        if (test == "consume_last") ok &= check(emptyReadStayedOff, "empty result must not redirect callback reads to MAIN");
        ok &= check(!returnContainer || offStack.id == 99, "native returned container lands in OFF");
        ok &= check(gSessionPlayer == nullptr, "completed session ends");
    } else if (test == "consume_stale") {
        usingItem = true; activeStack = offStack; gSessionPlayer = &player;
        offStack.id = 77;
        RightUseRouter::completeUsingItemDetour(&player);
        ok &= check(completions == 0 && cancellations == 1 && mainWrites == 0 && offhandSetterCalls == 0, "stale OFF session cancels without consuming either slot");
    } else if (test == "consume_main") {
        RightUseRouter::completeUsingItemDetour(&player);
        ok &= check(completions == 1 && mainWrites == 1 && offhandSetterCalls == 0, "ordinary MAIN completion remains native");
    } else if (test == "setter_unscoped") {
        gSessionPlayer = &player; usingItem = true; activeStack = offStack;
        RightUseRouter::setSelectedItemDetour(&player, &mainStack);
        ok &= check(mainWrites == 1 && offhandSetterCalls == 0, "session alone cannot redirect inventory writes");
    } else if (test == "shears") {
        mainItem.damage = 3;
        mainItem.maxStackSize = 1;
        mainItem.itemId = kShearsItemId;
        ok &= check(
            stackClaimsMainhandRightClick(&mainStack),
            "Shears must retain MAINHAND right-click ownership"
        );
        RightUseRouter::useItemOnBlockDetour(
            &gameMode, &mainStack, nullptr, 0, nullptr, 0, 0, false
        );
        ok &= check(
            calls == std::vector<unsigned char>{0},
            "Shears must never invoke OFFHAND block placement"
        );
    } else if (test == "sword") {
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
    } else if (
        test == "air_empty_main_instant_manual" ||
        test == "air_empty_main_instant_swap"
    ) {
        mainStack.count = 0;
        Stack dispatcherEmpty = mainStack;
        dispatcherEmpty.id = 0;

        // Model an instant native Item::use override such as EnderpearlItem.
        offTable[0x290/8] = reinterpret_cast<void*>(testBase + 0xFFEA310);
        offItem.duration = 0;

        if (test == "air_empty_main_instant_swap") {
            Stack swappedPearl = offStack;
            offStack.count = 0;
            setHand(&player, 1, &swappedPearl);
            offhandSetterCalls = 0;
        }

        const bool handled = RightUseRouter::baseUseItemDetour(
            &gameMode, &dispatcherEmpty, 0
        );
        ok &= check(handled, "empty MAIN must allow instant OFFHAND native use");
        ok &= check(
            calls == std::vector<unsigned char>{1},
            "instant OFFHAND use must invoke native hand=1 exactly once"
        );
        ok &= check(
            detached && copies == 1,
            "instant OFFHAND use must use one detached live-slot snapshot"
        );
        ok &= check(
            gSessionPlayer == nullptr && !usingItem,
            "instant OFFHAND use must not create a long-use session"
        );
    } else if (test == "air_empty_main_instant_pass") {
        mainStack.count = 0;
        Stack dispatcherEmpty = mainStack;
        dispatcherEmpty.id = 0;
        offTable[0x290/8] = reinterpret_cast<void*>(testBase + 0xFFEA310);
        offItem.duration = 0;
        offResult = 0;

        const bool handled = RightUseRouter::baseUseItemDetour(
            &gameMode, &dispatcherEmpty, 0
        );
        ok &= check(
            !handled,
            "instant OFFHAND PASS with empty MAIN must remain unhandled"
        );
        ok &= check(
            calls == std::vector<unsigned char>{1},
            "instant OFFHAND PASS must not replay vanilla MAIN empty-use"
        );
    } else if (test == "air_empty_main_long_use_blocked") {
        mainStack.count = 0;
        Stack dispatcherEmpty = mainStack;
        dispatcherEmpty.id = 0;

        // The new upper-gate bridge is intentionally instant-use-only.
        // Food/Spyglass/Bow-style duration use remains disabled until its
        // lifecycle is separately proven on device.
        offItem.duration = 32;
        startUse = true;

        const bool handled = RightUseRouter::baseUseItemDetour(
            &gameMode, &dispatcherEmpty, 0
        );
        ok &= check(
            !handled,
            "empty MAIN must not open unproven OFFHAND long-use through the instant gate"
        );
        ok &= check(
            calls.empty() && copies == 0,
            "blocked long-use must not call native use or snapshot the OFF stack"
        );
        ok &= check(
            !usingItem && gSessionPlayer == nullptr,
            "blocked long-use must not create an active OFF session"
        );
    } else if (
        test == "eat_offhand_manual" ||
        test == "eat_offhand_swap_result"
    ) {
        // MAIN is empty. The upper dispatcher is allowed to hand baseUseItem a
        // different EMPTY_ITEM representation than Player::getSelectedItem.
        // OFFHAND origin must be irrelevant once the live slot contains food.
        mainStack.count = 0;
        Stack dispatcherEmpty = mainStack;
        dispatcherEmpty.id = 0;

        if (test == "eat_offhand_swap_result") {
            // Model the post-swap state through the native hand-slot writer,
            // then forget how the item arrived. RightUseRouter must observe
            // only the resulting live OFFHAND stack.
            Stack swappedFood = offStack;
            offStack.count = 0;
            setHand(&player, 1, &swappedFood);
            offhandSetterCalls = 0;
        }

        startUse = true;
        const bool started = RightUseRouter::baseUseItemDetour(
            &gameMode, &dispatcherEmpty, 0
        );
        ok &= check(started, "empty MAIN must allow OFFHAND food self-use");
        ok &= check(
            calls == std::vector<unsigned char>{1},
            "OFFHAND food must start directly with native hand=1"
        );
        ok &= check(
            gSessionPlayer == &player && usingItem,
            "OFFHAND food must pin the native use session"
        );

        gUseTickSelectedOriginal = selected;
        const void* tickStack = useTickSelectedStackBridge(&player);
        ok &= check(
            tickStack == &offStack,
            "native use tick must read the same live OFFHAND stack regardless of item origin"
        );

        RightUseRouter::completeUsingItemDetour(&player);

        ok &= check(
            completions == 1 && mainWrites == 0 && offhandSetterCalls == 1,
            "food completion must write OFF exactly once and never MAIN"
        );
        ok &= check(
            mainStack.count == 0,
            "empty MAIN must remain untouched after OFFHAND eating"
        );
        ok &= check(
            offStack.id == 20 && offStack.count == 15,
            "OFFHAND food must decrement 16 -> 15"
        );
        ok &= check(
            gSessionPlayer == nullptr,
            "OFFHAND food session must end after completion"
        );
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

