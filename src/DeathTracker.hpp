#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>

// Read-only adapter for Death Tracker (elohmrow.death_tracker, v3 save format).
// It has no API for other mods, so its JSON files are read directly:
//   <save path>/<level key>/general.dt   deaths, runs, playtime (all time)
//   <save path>/<level key>/metadata     attempts, levelName, LinkedLevels
//   <save path>/<level key>/sessions/<sessionStartDate>.dt
// Without Quick Save, Death Tracker writes these only when the level is closed.
namespace tg::dt {

struct Data {
    std::string name;
    std::map<int, int> deaths;                 // percent -> count, normal mode from 0%
    std::map<std::pair<int, int>, int> runs;   // (start, end) -> count
    uint64_t playtimeNs = 0;
    int attempts = -1;                         // metadata attempts, -1 when unknown
    Data& operator+=(Data const& other);
};

// Main thread only.
bool available();
std::string versionString();
bool quickSave();
std::string sessionMethod();
std::filesystem::path savePath();

// Any thread (file I/O only).
std::optional<Data> readLevel(std::filesystem::path const& root, std::string const& key);
// Sessions last played in [from, to) (epoch seconds), merged per level key.
std::map<std::string, Data> readSessions(std::filesystem::path const& root, long long from, long long to);

}
