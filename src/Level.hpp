#pragma once

#include <string>

class GJGameLevel;

namespace tg {

// Death Tracker compatible key: "<id>[-local|-editor][-daily][-gauntlet]".
// Editor levels get a stable ID from cvolton.level-id-api instead of 0.
std::string levelKey(GJGameLevel* level);

struct LevelRef {
    std::string key;    // current key, used for every new save
    std::string legacy; // pre-1.2 key (raw level ID), empty when unsafe
    std::string name;
    static LevelRef from(GJGameLevel* level);
};

}
