#include "ui/SwapButton.hpp"

#include <string_view>
#include <utility>

#include <pl/ModMenu.hpp>

namespace levioffhand::ui {
namespace {

constexpr char kButtonId[] = "levi_offhand.swap_item";

// Temporary code-only visual.  Once the interaction path is proven in-game,
// this is intentionally easy to replace with the same PNG normal/pressed
// treatment used by LeviFreecam.
constexpr std::uint32_t kNormalBg = 0xD9232323U;
constexpr std::uint32_t kPressedBg = 0xFF3A3A3AU;
constexpr std::uint32_t kBorder = 0xFF858585U;
constexpr std::uint32_t kText = 0xFFFFFFFFU;

} // namespace

SwapButton& SwapButton::instance() noexcept {
    static SwapButton button;
    return button;
}

bool SwapButton::registerButton(
    std::string modId,
    std::string moduleId,
    ClickCallback callback
) {
    unregisterButton();

    mOnClick = std::move(callback);

    const bool registered =
        pl::modmenu::ButtonBuilder(kButtonId, "Swap Item")
            .modId(std::move(modId))
            .moduleId(std::move(moduleId))
            .label("F")
            .behavior(pl::modmenu::ButtonBehavior::Click)
            .defaultVisible(true)
            .stylePreset(pl::modmenu::ButtonStylePreset::Keycap)
            .styleColors(kNormalBg, kPressedBg, kBorder)
            .textColor(kText)
            .activeTextColor(kText)
            .sizeScale(1.0f, 1.0f)
            .onEvent(
                [this](
                    std::string_view buttonId,
                    pl::modmenu::ButtonEvent event,
                    float
                ) {
                    if (
                        buttonId != kButtonId ||
                        event != pl::modmenu::ButtonEvent::Click
                    ) {
                        return;
                    }

                    if (mOnClick) {
                        mOnClick();
                    }
                }
            )
            .registerButton();

    if (!registered) {
        mOnClick = {};
        return false;
    }

    mRegistered = true;
    return true;
}

void SwapButton::unregisterButton() noexcept {
    if (mRegistered) {
        pl::modmenu::unregisterButton(kButtonId);
    }
    mOnClick = {};
    mRegistered = false;
}

} // namespace levioffhand::ui
