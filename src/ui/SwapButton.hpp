#pragma once

#include <functional>
#include <string>

namespace levioffhand::ui {

class SwapButton final {
public:
    using ClickCallback = std::function<void()>;

    static SwapButton& instance() noexcept;

    bool registerButton(
        std::string modId,
        std::string moduleId,
        ClickCallback callback
    );

    void unregisterButton() noexcept;

    [[nodiscard]] bool registered() const noexcept {
        return mRegistered;
    }

private:
    SwapButton() = default;

private:
    ClickCallback mOnClick;
    bool mRegistered{false};
};

} // namespace levioffhand::ui
