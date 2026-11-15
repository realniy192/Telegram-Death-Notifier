#pragma once

#include "Level.hpp"
#include <map>
#include <string>
#include <vector>

class GJGameLevel;

namespace tg {

// level key -> level name (kept for display when the level is not found)
using LinkedLevels = std::map<std::string, std::string>;

LinkedLevels loadLinks(LevelRef const& level);
void saveLinks(LevelRef const& level, LinkedLevels const& links);
int lostAttempts(LevelRef const& level);
void setLostAttempts(LevelRef const& level, int attempts);

// Resolves a Death Tracker style key to a level object, or nullptr.
GJGameLevel* findLevel(std::string const& key);
GJGameLevel* findOnlineLevel(int id);
std::vector<GJGameLevel*> findEditorLevelsByName(std::string const& name);
int gdAttempts(GJGameLevel* level);

struct AttemptTotals {
    int own = 0;     // GD attempts of this level
    int linked = 0;  // GD attempts of linked copies
    int lost = 0;    // manual correction
    int missing = 0; // linked levels that could not be found
    int total() const { return own + linked + lost; }
};

AttemptTotals attemptTotals(LevelRef const& level, GJGameLevel* current);

}
