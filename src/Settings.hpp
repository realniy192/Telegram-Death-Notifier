#pragma once

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
    bool customNickname = false;
    std::string nickname;
    bool showDecimals = false;
    int decimalPlaces = 2;
    bool onlyBest = false;
    bool defaultEnabled = false;
    bool defaultStepMode = false;
    int defaultMin = 0;
    int defaultMinRunProgress = 0;
    bool operator==(GlobalCfg const&) const = default;
};

struct RunEntry {
    std::string key;
    std::string label;
};

std::string spKey(float x);
bool levelEnabled(int id);
RunCfg loadRunCfg(int id, std::string const& run);
void saveRunCfg(int id, std::string const& run, RunCfg cfg);
LevelCfg loadLevelCfg(int id);
void saveLevelCfg(int id, LevelCfg const& cfg);
GlobalCfg loadGlobalCfg();
void saveGlobalCfg(GlobalCfg const& cfg);
int currentDecimalPlaces();
std::vector<RunEntry> collectRunEntries(PlayLayer* layer);
std::string formatTemplate(std::string text, std::string const& fallback,
    std::vector<std::pair<std::string, std::string>> const& variables);

}
