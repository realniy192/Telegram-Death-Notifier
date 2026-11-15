#pragma once

#include "Level.hpp"
#include <map>
#include <string>

// Own session journal. Death Tracker has no attempt history per day, so this
// keeps attempts, deaths, runs and attempt time per calendar day (local time)
// in <save dir>/journal/YYYY-MM-DD.json, plus in-memory totals for the current
// level visit and the current game launch. Main thread only.
namespace tg::journal {

struct LevelStats {
    std::string name;
    int attempts = 0;
    std::map<std::string, int> deaths; // "54" -> count, normal mode from 0% ("100" = completion)
    std::map<std::string, int> runs;   // "13-45" -> count, start positions / practice / checkpoints
    double playtime = 0;               // seconds of attempt time, pauses excluded
    LevelStats& operator+=(LevelStats const& other);
};

enum class Scope { LevelVisit, GameLaunch, Today };

void beginVisit(LevelRef const& level);
void endVisit();
void addAttempts(int count);
void addDeath(int percent);
void addRun(int start, int end);
void addPlaytime(double seconds);
// Empty key: all levels together. LevelVisit always means the current visit.
LevelStats stats(Scope scope, std::string const& levelKey = "");
// Local calendar day, "YYYY-MM-DD".
std::string today();
void flush();
// Per level for one scope (LevelVisit: just the current level).
std::map<std::string, LevelStats> perLevel(Scope scope);
// Every saved day for one level (flushes first).
LevelStats allTime(std::string const& levelKey);
// Key of the level being recorded, empty outside a level.
std::string currentKey();

}
