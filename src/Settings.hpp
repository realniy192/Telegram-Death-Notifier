#pragma once

#include "Level.hpp"
#include "RunTracking.hpp"
#include <map>
#include <string>
#include <vector>

class PlayLayer;

namespace tg {

struct RunCfg {
    bool enabled = true;
    int minPercent = 0;
    bool operator==(RunCfg const&) const = default;
};

struct LevelCfg {
    bool enabled = false;
    RunMode mode = RunMode::StartPositions;
    int minRunProgress = 0;
    std::map<std::string, RunCfg> runs;
    bool operator==(LevelCfg const&) const = default;
};

struct GlobalCfg {
    std::string token;
    std::string chat;
    std::string deathMessage;
    std::string runMessage;
    std::string beatMessage;
    std::string bestMessage;
    bool customNickname = false;
    std::string nickname;
    bool showDecimals = false;
    int decimalPlaces = 2;
    bool onlyBest = false;
    bool defaultEnabled = false;
    bool defaultStepMode = false;
    int defaultMin = 0;
    int defaultMinRunProgress = 0;
    bool noCaption = false;
    bool skewShot = false;
    int skewStrength = 60;
    int completeDelayMs = 1200;
    int deathDelayMs = 0;
    int uploadTimeout = 45;
    bool streakEnabled = true;
    int streakMin = 2;
    int streakMode = 2; // 0 Separate, 1 Replace, 2 Combine
    std::string streakSameMessage;
    std::string streakMixedMessage;
    bool operator==(GlobalCfg const&) const = default;
};

struct RunEntry {
    std::string key;
    std::string label;
};

// level key -> level name, so the list stays readable after the level is gone
using Blacklist = std::map<std::string, std::string>;

std::string spKey(float x);
bool levelEnabled(LevelRef const& level);
RunCfg loadRunCfg(LevelRef const& level, std::string const& run);
void saveRunCfg(LevelRef const& level, std::string const& run, RunCfg cfg);
LevelCfg loadLevelCfg(LevelRef const& level);
void saveLevelCfg(LevelRef const& level, LevelCfg const& cfg);
GlobalCfg loadGlobalCfg();
void saveGlobalCfg(GlobalCfg const& cfg);
int currentDecimalPlaces();
// 0 Separate, 1 Replace, 2 Combine (see StreakMode)
int currentStreakMode();
// Custom nickname when enabled, otherwise the GD username.
std::string playerName();
std::vector<RunEntry> collectRunEntries(PlayLayer* layer);
std::string formatTemplate(std::string text, std::string const& fallback,
    std::vector<std::pair<std::string, std::string>> const& variables);

Blacklist loadBlacklist();
bool isBlacklisted(std::string const& key);
void setBlacklisted(LevelRef const& level, bool blacklisted);
void removeFromBlacklist(std::string const& key);

}
