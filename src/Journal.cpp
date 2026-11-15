#include "Journal.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/AppDelegate.hpp>
#include <matjson/stl_serialize.hpp>
#include <cmath>
#include <ctime>

using namespace geode::prelude;
using tg::journal::LevelStats;

template <>
struct matjson::Serialize<LevelStats> {
    static geode::Result<LevelStats> fromJson(matjson::Value const& value) {
        LevelStats stats;
        if (value.contains("name")) stats.name = value["name"].asString().unwrapOrDefault();
        if (value.contains("attempts")) stats.attempts = (int)value["attempts"].asInt().unwrapOrDefault();
        if (value.contains("deaths")) stats.deaths = value["deaths"].as<std::map<std::string, int>>().unwrapOrDefault();
        if (value.contains("runs")) stats.runs = value["runs"].as<std::map<std::string, int>>().unwrapOrDefault();
        if (value.contains("playtime")) stats.playtime = value["playtime"].asDouble().unwrapOrDefault();
        return geode::Ok(stats);
    }
    static matjson::Value toJson(LevelStats const& stats) {
        return matjson::makeObject({
            { "name", stats.name },
            { "attempts", stats.attempts },
            { "deaths", stats.deaths },
            { "runs", stats.runs },
            { "playtime", stats.playtime }
        });
    }
};

namespace {
using Day = std::map<std::string, LevelStats>;
constexpr int kFlushEvery = 20;

struct State {
    std::string key;
    std::string name;
    bool inVisit = false;
    LevelStats visit;
    Day launch;
    std::string day;
    Day today;
    bool todayLoaded = false;
    int unsaved = 0;
};

State& state() {
    static State s;
    return s;
}

std::filesystem::path dayPath(std::string const& day) {
    return Mod::get()->getSaveDir() / "journal" / (day + ".json");
}

void saveDay() {
    auto& s = state();
    if (!s.todayLoaded || s.unsaved == 0) return;
    auto path = dayPath(s.day);
    (void)file::createDirectoryAll(path.parent_path());
    auto result = file::writeString(path, matjson::Value(s.today).dump(matjson::NO_INDENTATION));
    if (!result) log::warn("Could not save the session journal");
    s.unsaved = 0;
}

Day loadDay(std::string const& day) {
    auto path = dayPath(day);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return {};
    auto json = file::readJson(path);
    if (!json) {
        log::warn("Session journal for {} is unreadable; starting it fresh", day);
        return {};
    }
    return json.unwrap().as<Day>().unwrapOrDefault();
}

// Every record lands in the calendar day it happens in.
Day& todayBucket() {
    auto& s = state();
    auto now = tg::journal::today();
    if (!s.todayLoaded || s.day != now) {
        saveDay();
        s.day = now;
        s.today = loadDay(now);
        s.todayLoaded = true;
    }
    return s.today;
}

template <class F>
void record(F&& apply) {
    auto& s = state();
    if (!s.inVisit) return;
    apply(s.visit);
    auto& launch = s.launch[s.key];
    launch.name = s.name;
    apply(launch);
    auto& day = todayBucket()[s.key];
    day.name = s.name;
    apply(day);
    if (++s.unsaved >= kFlushEvery) saveDay();
}

template <class Map>
void addCounts(Map& target, Map const& source) {
    for (auto const& [key, value] : source) target[key] += value;
}
}

LevelStats& LevelStats::operator+=(LevelStats const& other) {
    if (name.empty()) name = other.name;
    attempts += other.attempts;
    addCounts(deaths, other.deaths);
    addCounts(runs, other.runs);
    playtime += other.playtime;
    return *this;
}

std::string tg::journal::today() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef GEODE_IS_WINDOWS
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[16] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &local);
    return buffer;
}

void tg::journal::beginVisit(LevelRef const& level) {
    auto& s = state();
    if (s.inVisit) endVisit();
    s.key = level.key;
    s.name = level.name;
    s.visit = {};
    s.visit.name = level.name;
    s.inVisit = true;
}

void tg::journal::endVisit() {
    auto& s = state();
    if (!s.inVisit) return;
    s.inVisit = false;
    saveDay();
}

void tg::journal::addAttempts(int count) {
    if (count <= 0) return;
    record([count](LevelStats& stats) { stats.attempts += count; });
}

void tg::journal::addDeath(int percent) {
    auto key = std::to_string(std::clamp(percent, 0, 100));
    record([&](LevelStats& stats) { stats.deaths[key]++; });
}

void tg::journal::addRun(int start, int end) {
    auto key = fmt::format("{}-{}", std::clamp(start, 0, 100), std::clamp(end, 0, 100));
    record([&](LevelStats& stats) { stats.runs[key]++; });
}

void tg::journal::addPlaytime(double seconds) {
    if (!std::isfinite(seconds) || seconds <= 0) return;
    seconds = std::min(seconds, 86400.0);
    record([seconds](LevelStats& stats) { stats.playtime += seconds; });
}

LevelStats tg::journal::stats(Scope scope, std::string const& levelKey) {
    auto& s = state();
    if (scope == Scope::LevelVisit) return s.visit;
    Day const& source = scope == Scope::GameLaunch ? s.launch : todayBucket();
    if (!levelKey.empty()) {
        auto it = source.find(levelKey);
        return it == source.end() ? LevelStats{} : it->second;
    }
    LevelStats total;
    for (auto const& [key, stats] : source) total += stats;
    return total;
}

void tg::journal::flush() {
    saveDay();
}

std::map<std::string, LevelStats> tg::journal::perLevel(Scope scope) {
    auto& s = state();
    if (scope == Scope::LevelVisit) {
        if (!s.inVisit) return {};
        return {{s.key, s.visit}};
    }
    return scope == Scope::GameLaunch ? s.launch : todayBucket();
}

LevelStats tg::journal::allTime(std::string const& levelKey) {
    saveDay();
    LevelStats total;
    auto folder = Mod::get()->getSaveDir() / "journal";
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return total;
    for (auto const& entry : std::filesystem::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        auto day = loadDay(utils::string::pathToString(entry.path().stem()));
        if (auto it = day.find(levelKey); it != day.end()) total += it->second;
    }
    return total;
}

std::string tg::journal::currentKey() {
    auto& s = state();
    return s.inVisit ? s.key : "";
}

// The game can close straight from a level; keep the last attempts.
class $modify(TGJournalSave, AppDelegate) {
    void trySaveGame(bool force) {
        tg::journal::flush();
        AppDelegate::trySaveGame(force);
    }
};
