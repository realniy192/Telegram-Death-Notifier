#pragma once

#include "DeathTracker.hpp"
#include "Level.hpp"
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace tg::stats {

enum SessionScope { ScopeLevelVisit = 0, ScopeGameLaunch = 1, ScopeToday = 2 };

struct Config {
    bool onComplete = true;
    bool onRuns = false;
    bool listDeaths = false;
    int listPercent = 50;
    bool customDeaths = false;
    int customPercent = 80;
    bool attempts = true;
    bool attemptsFromDT = false;
    bool playtime = true;
    bool worstFail = true;
    bool bestRuns = true;
    int bestRunsCount = 3;
    int bestRunsMinLength = 20;
    bool askEnjoyment = false;
    bool askExperience = false;
    bool nRuns = true;
    std::string nRunsMessage = "{player} can now beat {level} in {n} runs: {chain}";
    int sessionScope = ScopeToday;
    bool operator==(Config const&) const = default;
};

Config load();
void save(Config const& config);

// Whole percents, Death Tracker shape.
struct LevelData {
    std::map<int, int> deaths;               // from 0%, 100 = completion
    std::map<std::pair<int, int>, int> runs; // (start, end)
    uint64_t playtimeNs = 0;
    int dtAttempts = -1;                     // -1 without Death Tracker
};

// Taken when the level opens: Death Tracker files (or the journal) as they
// are on disk, which never include the visit that is starting.
struct Snapshot {
    bool fromDT = false;
    bool quickSave = false;
    std::filesystem::path root;
    LevelData base;
};

Snapshot takeSnapshot(LevelRef const& level);
// Snapshot plus everything recorded during this visit.
LevelData currentData(Snapshot const& snapshot, LevelRef const& level);

struct Ratings {
    std::optional<int> enjoyment;
    std::string experience;
};

std::string ratingLines(Ratings const& ratings);
// Empty when no metric is enabled.
std::string block(Config const& config, LevelData const& data, int attempts);

// Fewest overlapping runs covering 0-100, starting with a run from 0%.
struct Chain {
    std::vector<std::pair<int, int>> runs;
    int count() const { return (int)runs.size(); }
};

Chain minRuns(LevelData const& data);
std::string chainLabel(Chain const& chain);
// Stores the current count silently, so only later improvements post.
void initChain(LevelRef const& level, LevelData const& data);
// The new count when it beats the stored one and is at least 2 (a full
// 0-100 run is never posted here); stores every improvement.
std::optional<int> improvedChain(LevelRef const& level, Chain const& chain);

std::string formatDuration(uint64_t nanoseconds);
// Builds and sends the session summary as a text message.
void sendSummary(int scope, std::string const& player);

}
