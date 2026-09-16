#include "runtime/NativeCapabilityProbe.hpp"

#include <android/log.h>

#include <cstddef>
#include <cstring>
#include <dlfcn.h>

namespace levioffhand::runtime {
namespace {

constexpr char kMinecraftLibrary[] = "libminecraftpe.so";
constexpr char kLogTag[] = "Levi Offhand";

// GameMode is a polymorphic class.  On the exact 1.26.45.1 ABI the first
// data member after the vptr is Player& mPlayer.  Every action detour still
// validates the recovered Player object's vtable before using this pointer.
constexpr std::size_t kGameModePlayerOffset = sizeof(void*);

constexpr char kSelectedItemSymbol[] = "_ZNK6Player15getSelectedItemEv";
constexpr char kOffhandSlotSymbol[] = "_ZNK5Actor14getOffhandSlotEv";
constexpr char kStackIsNullSymbol[] = "_ZNK13ItemStackBase6isNullEv";
constexpr char kStackGetIdSymbol[] = "_ZNK13ItemStackBase5getIdEv";
constexpr char kPlayerIsUsingItemSymbol[] = "_ZNK6Player11isUsingItemEv";

[[nodiscard]] bool belongsToMinecraft(std::uintptr_t address) noexcept {
    if (address == 0) {
        return false;
    }

    Dl_info info{};
    return dladdr(reinterpret_cast<void*>(address), &info) != 0 &&
        info.dli_fname != nullptr &&
        std::strstr(info.dli_fname, kMinecraftLibrary) != nullptr;
}

template <typename Fn>
[[nodiscard]] Fn resolveMinecraftExport(const char* symbol) noexcept {
    void* raw = dlsym(RTLD_DEFAULT, symbol);
    if (raw == nullptr) {
        return nullptr;
    }
    const auto address = reinterpret_cast<std::uintptr_t>(raw);
    if (!belongsToMinecraft(address)) {
        return nullptr;
    }
    return reinterpret_cast<Fn>(raw);
}

} // namespace

NativeCapabilityProbe& NativeCapabilityProbe::instance() noexcept {
    static NativeCapabilityProbe value;
    return value;
}

bool NativeCapabilityProbe::install(pl::mod::ModContext& context) noexcept {
    if (available()) {
        return true;
    }

    mGetSelectedItem = resolveMinecraftExport<SelectedItemFn>(kSelectedItemSymbol);
    mGetOffhandSlot = resolveMinecraftExport<OffhandItemFn>(kOffhandSlotSymbol);
    mStackIsNull = resolveMinecraftExport<StackIsNullFn>(kStackIsNullSymbol);
    mStackGetId = resolveMinecraftExport<StackGetIdFn>(kStackGetIdSymbol);
    mPlayerIsUsingItem = resolveMinecraftExport<PlayerIsUsingItemFn>(
        kPlayerIsUsingItemSymbol
    );

    mSelectedItemTarget = reinterpret_cast<std::uintptr_t>(mGetSelectedItem);
    mAvailable =
        mGetSelectedItem != nullptr &&
        mGetOffhandSlot != nullptr &&
        mStackIsNull != nullptr &&
        mStackGetId != nullptr &&
        mPlayerIsUsingItem != nullptr;

    if (!mAvailable) {
        context.logger().warn(
            "[NativeCapabilityProbe] fail-closed: one or more exact native accessors are not exported"
        );
        uninstall(context);
        return false;
    }

    context.logger().info(
        "[NativeCapabilityProbe] main/offhand stack accessors resolved"
    );
    return true;
}

void NativeCapabilityProbe::uninstall(pl::mod::ModContext& context) noexcept {
    mGetSelectedItem = nullptr;
    mGetOffhandSlot = nullptr;
    mStackIsNull = nullptr;
    mStackGetId = nullptr;
    mPlayerIsUsingItem = nullptr;
    mSelectedItemTarget = 0;
    mAvailable = false;
    context.logger().info("[NativeCapabilityProbe] accessors released");
}

bool NativeCapabilityProbe::available() const noexcept {
    return mAvailable;
}

bool NativeCapabilityProbe::validatePlayerObject(const void* player) const noexcept {
    if (player == nullptr) {
        return false;
    }

    const auto raw = reinterpret_cast<std::uintptr_t>(player);
    if ((raw & (alignof(void*) - 1U)) != 0U) {
        return false;
    }

    // The object itself is heap memory, but its vtable must live in
    // libminecraftpe.so.  This prevents calling Player methods on an arbitrary
    // pointer if the target layout ever changes.
    const void* vtable = nullptr;
    std::memcpy(&vtable, player, sizeof(vtable));
    return belongsToMinecraft(reinterpret_cast<std::uintptr_t>(vtable));
}

const void* NativeCapabilityProbe::playerFromGameMode(void* gameMode) const noexcept {
    if (!available() || gameMode == nullptr) {
        return nullptr;
    }

    const void* player = nullptr;
    const auto* bytes = static_cast<const std::byte*>(gameMode);
    std::memcpy(&player, bytes + kGameModePlayerOffset, sizeof(player));
    if (!validatePlayerObject(player)) {
        return nullptr;
    }
    return player;
}

const void* NativeCapabilityProbe::mainhandStack(void* gameMode) const noexcept {
    const void* player = playerFromGameMode(gameMode);
    if (player == nullptr || mGetSelectedItem == nullptr) {
        return nullptr;
    }
    return mGetSelectedItem(player);
}

const void* NativeCapabilityProbe::offhandStack(void* gameMode) const noexcept {
    const void* player = playerFromGameMode(gameMode);
    return offhandStackForPlayer(player);
}

const void* NativeCapabilityProbe::offhandStackForPlayer(
    const void* player
) const noexcept {
    if (!available() || player == nullptr || mGetOffhandSlot == nullptr) {
        return nullptr;
    }
    return mGetOffhandSlot(player);
}

bool NativeCapabilityProbe::stackIsNull(const void* stack) const noexcept {
    return stack == nullptr || mStackIsNull == nullptr || mStackIsNull(stack);
}

bool NativeCapabilityProbe::playerIsUsingItem(const void* player) const noexcept {
    return player != nullptr &&
        mPlayerIsUsingItem != nullptr &&
        mPlayerIsUsingItem(player);
}

std::int16_t NativeCapabilityProbe::stackItemId(const void* stack) const noexcept {
    if (stack == nullptr || mStackGetId == nullptr) {
        return 0;
    }
    return mStackGetId(stack);
}

std::uintptr_t NativeCapabilityProbe::selectedItemTarget() const noexcept {
    return mSelectedItemTarget;
}

} // namespace levioffhand::runtime
