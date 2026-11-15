#pragma once

#include <functional>
#include <optional>
#include <string>

namespace cocos2d { class CCNode; }

namespace tg {

struct RatingResult {
    std::optional<int> enjoyment; // 1-10, empty when not set
    std::string experience;
};

// Compact panel on the left edge of the level complete screen. `done` runs
// exactly once: Send (entered values), Skip (nothing) or when the screen
// closes (whatever was entered so far).
void showRatingPanel(cocos2d::CCNode* parent, bool enjoyment, bool experience,
                     std::function<void(RatingResult)> done);

}
