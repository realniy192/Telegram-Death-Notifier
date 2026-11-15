#include "Stats.hpp"
#include "Journal.hpp"
#include "Screenshot.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <ctime>
#include <thread>

using namespace geode::prelude;
using tg::stats::Config;

template <>
struct matjson::Serialize<Config> {
    static geode::Result<Config> fromJson(matjson::Value const& value) {
        Config c;
        auto flag = [&](char const* key, bool& out) {
            if (!value.contains(key)) return;
            if (auto read = value[key].asBool()) out = read.unwrap();
        };
        auto number = [&](char const* key, int& out, int min, int max) {
            if (!value.contains(key)) return;
            if (auto read = value[key].asInt()) out = std::clamp((int)read.unwrap(), min, max);
        };
        flag("onComplete", c.onComplete);
        flag("onRuns", c.onRuns);
        flag("listDeaths", c.listDeaths);
        number("listPercent", c.listPercent, 1, 100);
        flag("customDeaths", c.customDeaths);
        number("customPercent", c.customPercent, 1, 100);
        flag("attempts", c.attempts);
        flag("attemptsFromDT", c.attemptsFromDT);
        flag("playtime", c.playtime);
        flag("worstFail", c.worstFail);
        flag("bestRuns", c.bestRuns);
        number("bestRunsCount", c.bestRunsCount, 1, 10);
        number("bestRunsMinLength", c.bestRunsMinLength, 0, 100);
        flag("askEnjoyment", c.askEnjoyment);
        flag("askExperience", c.askExperience);
        flag("nRuns", c.nRuns);
        if (value.contains("nRunsMessage")) {
            if (auto read = value["nRunsMessage"].asString()) c.nRunsMessage = read.unwrap();
        }
        number("sessionScope", c.sessionScope, 0, 2);
        return geode::Ok(c);
    }
    static matjson::Value toJson(Config const& c) {
        return matjson::makeObject({
            { "onComplete", c.onComplete }, { "onRuns", c.onRuns },
            { "listDeaths", c.listDeaths }, { "listPercent", c.listPercent },
            { "customDeaths", c.customDeaths }, { "customPercent", c.customPercent },
            { "attempts", c.attempts }, { "attemptsFromDT", c.attemptsFromDT },
            { "playtime", c.playtime }, { "worstFail", c.worstFail },
            { "bestRuns", c.bestRuns }, { "bestRunsCount", c.bestRunsCount },
            { "bestRunsMinLength", c.bestRunsMinLength },
            { "askEnjoyment", c.askEnjoyment }, { "askExperience", c.askExperience },
            { "nRuns", c.nRuns }, { "nRunsMessage", c.nRunsMessage },
            { "sessionScope", c.sessionScope }
        });
    }
};

namespace {
constexpr char kConfigKey[] = "stats-config";

std::string chainKey(tg::LevelRef const& level) {
    return fmt::format("lvl-{}-nruns", level.key);
}

tg::stats::LevelData fromDT(tg::dt::Data const& data) {
    tg::stats::LevelData out;
    out.deaths = data.deaths;
    out.runs = data.runs;
    out.playtimeNs = data.playtimeNs;
    out.dtAttempts = data.attempts;
    return out;
}

void addJournal(tg::stats::LevelData& data, tg::journal::LevelStats const& stats) {
    for (auto const& [key, count] : stats.deaths) {
        auto percent = numFromString<int>(key);
        if (percent) data.deaths[std::clamp(percent.unwrap(), 0, 100)] += count;
    }
    for (auto const& [key, count] : stats.runs) {
        auto dash = key.find('-');
        if (dash == std::string::npos) continue;
        auto start = numFromString<int>(key.substr(0, dash));
        auto end = numFromString<int>(key.substr(dash + 1));
        if (start && end && end.unwrap() > start.unwrap()) data.runs[{start.unwrap(), end.unwrap()}] += count;
    }
    data.playtimeNs += (uint64_t)(std::max(0.0, stats.playtime) * 1e9);
}

std::string percentLabel(std::pair<int, int> const& run) {
    return run.first == 0 ? fmt::format("{}%", run.second) : fmt::format("{}-{}%", run.first, run.second);
}
}

Config tg::stats::load() {
    auto mod = Mod::get();
    if (!mod->hasSavedValue(kConfigKey)) return {};
    return mod->getSavedValue<Config>(kConfigKey);
}

void tg::stats::save(Config const& config) {
    Mod::get()->setSavedValue(kConfigKey, config);
}

tg::stats::Snapshot tg::stats::takeSnapshot(LevelRef const& level) {
    Snapshot snapshot;
    if (dt::available()) {
        snapshot.root = dt::savePath();
        snapshot.quickSave = dt::quickSave();
        if (auto data = dt::readLevel(snapshot.root, level.key)) snapshot.base = fromDT(*data);
        snapshot.fromDT = true;
        return snapshot;
    }
    addJournal(snapshot.base, journal::allTime(level.key));
    return snapshot;
}

tg::stats::LevelData tg::stats::currentData(Snapshot const& snapshot, LevelRef const& level) {
    if (snapshot.fromDT && snapshot.quickSave) {
        if (auto fresh = dt::readLevel(snapshot.root, level.key)) return fromDT(*fresh);
    }
    LevelData data = snapshot.base;
    auto visit = journal::stats(journal::Scope::LevelVisit);
    addJournal(data, visit);
    if (data.dtAttempts >= 0) data.dtAttempts += visit.attempts;
    return data;
}

std::string tg::stats::ratingLines(Ratings const& ratings) {
    std::vector<std::string> lines;
    if (ratings.enjoyment) lines.push_back(fmt::format("Enjoyment: {}/10", *ratings.enjoyment));
    auto experience = std::string(utils::string::trim(ratings.experience));
    if (!experience.empty()) lines.push_back("Experience: " + experience);
    std::string out;
    for (auto const& line : lines) out += (out.empty() ? "" : "\n") + line;
    return out;
}

std::string tg::stats::block(Config const& config, LevelData const& data, int attempts) {
    std::vector<std::string> lines;
    auto deathsFrom = [&](int percent) {
        int total = 0;
        for (auto const& [key, count] : data.deaths) if (key >= percent && key < 100) total += count;
        return total;
    };
    if (config.listDeaths) lines.push_back(fmt::format("Deaths after {}%: {}", config.listPercent, deathsFrom(config.listPercent)));
    if (config.customDeaths) lines.push_back(fmt::format("Deaths after {}%: {}", config.customPercent, deathsFrom(config.customPercent)));
    if (config.attempts && attempts >= 0) lines.push_back(fmt::format("Attempts: {}", attempts));
    if (config.playtime && data.playtimeNs > 0) lines.push_back("Playtime: " + formatDuration(data.playtimeNs));
    if (config.worstFail) {
        int worst = -1;
        for (auto const& [key, count] : data.deaths) if (count > 0 && key < 100) worst = std::max(worst, key);
        if (worst >= 0) lines.push_back(fmt::format("Worst fail: {}%", worst));
    }
    if (config.bestRuns) {
        std::vector<int> starts;
        for (auto const& [run, count] : data.runs) {
            if (count > 0 && run.second >= 100 && run.first > 0 && 100 - run.first >= config.bestRunsMinLength) {
                starts.push_back(run.first);
            }
        }
        std::sort(starts.begin(), starts.end());
        starts.erase(std::unique(starts.begin(), starts.end()), starts.end());
        if (!starts.empty()) {
            std::string list;
            for (int i = 0; i < std::min<int>(config.bestRunsCount, starts.size()); ++i) {
                list += (list.empty() ? "" : ", ") + fmt::format("{}-100", starts[i]);
            }
            lines.push_back("Best runs: " + list);
        }
    }
    if (lines.empty()) return "";
    std::string out = "Stats:";
    for (auto const& line : lines) out += "\n" + line;
    return out;
}

tg::stats::Chain tg::stats::minRuns(LevelData const& data) {
    std::vector<std::pair<int, int>> intervals;
    int best = 0;
    for (auto const& [key, count] : data.deaths) if (count > 0) best = std::max(best, key);
    if (best > 0) intervals.push_back({0, best});
    for (auto const& [run, count] : data.runs) {
        if (count > 0 && run.second > run.first) intervals.push_back(run);
    }
    Chain chain;
    int reach = 0;
    while (reach < 100) {
        std::optional<std::pair<int, int>> pick;
        for (auto const& interval : intervals) {
            // The first run starts at 0%; every next one starts inside the previous one.
            bool starts = chain.runs.empty() ? interval.first == 0 : interval.first <= reach;
            if (starts && interval.second > reach && (!pick || interval.second > pick->second)) pick = interval;
        }
        if (!pick) return {};
        chain.runs.push_back(*pick);
        reach = pick->second;
    }
    return chain;
}

std::string tg::stats::chainLabel(Chain const& chain) {
    std::string out;
    for (auto const& run : chain.runs) out += (out.empty() ? "" : ", ") + percentLabel(run);
    return out;
}

void tg::stats::initChain(LevelRef const& level, LevelData const& data) {
    auto mod = Mod::get();
    auto key = chainKey(level);
    if (mod->hasSavedValue(key)) return;
    mod->setSavedValue<int64_t>(key, minRuns(data).count());
}

std::optional<int> tg::stats::improvedChain(LevelRef const& level, Chain const& chain) {
    int count = chain.count();
    if (count == 0) return std::nullopt;
    auto mod = Mod::get();
    auto key = chainKey(level);
    int stored = mod->hasSavedValue(key) ? (int)mod->getSavedValue<int64_t>(key) : 0;
    if (stored != 0 && count >= stored) return std::nullopt;
    mod->setSavedValue<int64_t>(key, count);
    if (count < 2) return std::nullopt;
    return count;
}

std::string tg::stats::formatDuration(uint64_t nanoseconds) {
    uint64_t seconds = nanoseconds / 1'000'000'000ULL;
    uint64_t hours = seconds / 3600, minutes = (seconds % 3600) / 60;
    if (hours) return fmt::format("{}h {}m", hours, minutes);
    if (minutes) return fmt::format("{}m {}s", minutes, seconds % 60);
    return fmt::format("{}s", seconds);
}

// ======================= session summary =======================
namespace {
struct SummaryLevel {
    std::string name;
    int attempts = 0;
    tg::stats::LevelData data;
};

std::pair<long long, long long> todayRange() {
    std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef GEODE_IS_WINDOWS
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    local.tm_hour = 0;
    local.tm_min = 0;
    local.tm_sec = 0;
    local.tm_isdst = -1;
    auto from = (long long)std::mktime(&local);
    local.tm_mday += 1;
    local.tm_isdst = -1;
    auto to = (long long)std::mktime(&local);
    return {from, to};
}

std::map<std::string, SummaryLevel> fromJournal(std::map<std::string, tg::journal::LevelStats> const& levels) {
    std::map<std::string, SummaryLevel> out;
    for (auto const& [key, stats] : levels) {
        auto& level = out[key];
        level.name = stats.name;
        level.attempts = stats.attempts;
        addJournal(level.data, stats);
    }
    return out;
}

std::string summaryText(std::string const& player, int scope, std::map<std::string, SummaryLevel> const& levels) {
    char const* scopes[] = {"this level visit", "this game launch", "today"};
    std::string title = fmt::format("{} - session summary ({})", player, scopes[std::clamp(scope, 0, 2)]);
    if (scope == tg::stats::ScopeToday) title += ", " + tg::journal::today();
    int attempts = 0, deaths = 0;
    uint64_t playtime = 0;
    std::vector<std::pair<std::string, SummaryLevel>> rows(levels.begin(), levels.end());
    for (auto const& [key, level] : rows) {
        attempts += level.attempts;
        playtime += level.data.playtimeNs;
        for (auto const& [percent, count] : level.data.deaths) if (percent < 100) deaths += count;
    }
    if (rows.empty() || (attempts == 0 && deaths == 0 && playtime == 0)) {
        return title + "\nNothing played in this session yet.";
    }
    std::string text = title;
    std::string totals;
    if (attempts) totals += fmt::format("Attempts: {}", attempts);
    totals += (totals.empty() ? "" : "  |  ") + fmt::format("Deaths: {}", deaths);
    if (playtime) totals += "  |  Playtime: " + tg::stats::formatDuration(playtime);
    text += "\n" + totals;
    std::sort(rows.begin(), rows.end(), [](auto const& a, auto const& b) {
        if (a.second.attempts != b.second.attempts) return a.second.attempts > b.second.attempts;
        return a.second.data.playtimeNs > b.second.data.playtimeNs;
    });
    constexpr size_t kMaxRows = 5;
    for (size_t i = 0; i < std::min(rows.size(), kMaxRows); ++i) {
        auto const& level = rows[i].second;
        std::vector<std::string> parts;
        if (level.attempts) parts.push_back(fmt::format("{} att.", level.attempts));
        int best = -1;
        for (auto const& [percent, count] : level.data.deaths) if (count > 0) best = std::max(best, percent);
        if (best >= 0) parts.push_back(fmt::format("best {}%", best));
        std::optional<std::pair<int, int>> top;
        for (auto const& [run, count] : level.data.runs) {
            if (count > 0 && (!top || run.second - run.first > top->second - top->first)) top = run;
        }
        if (top) parts.push_back(fmt::format("top run {}-{}", top->first, top->second));
        std::string line = (level.name.empty() ? rows[i].first : level.name) + ":";
        for (size_t p = 0; p < parts.size(); ++p) line += (p ? ", " : " ") + parts[p];
        text += "\n" + line;
    }
    if (rows.size() > kMaxRows) text += fmt::format("\n...and {} more", rows.size() - kMaxRows);
    return text;
}
}

void tg::stats::sendSummary(int scope, std::string const& player) {
    scope = std::clamp(scope, 0, 2);
    auto journalScope = scope == ScopeLevelVisit ? journal::Scope::LevelVisit
        : scope == ScopeGameLaunch ? journal::Scope::GameLaunch : journal::Scope::Today;
    auto levels = fromJournal(journal::perLevel(journalScope));
    if (scope != ScopeToday || !dt::available()) {
        sendTelegramText(summaryText(player, scope, levels));
        return;
    }
    // Today with Death Tracker: deaths, runs and playtime come from its sessions
    // last played today; attempts stay from the journal (sessions have none).
    auto root = dt::savePath();
    bool quick = dt::quickSave();
    auto visit = journal::stats(journal::Scope::LevelVisit);
    auto visitKey = journal::currentKey();
    auto range = todayRange();
    long long from = range.first, to = range.second;
    std::thread([=, levels = std::move(levels)]() mutable {
        auto sessions = dt::readSessions(root, from, to);
        geode::queueInMainThread([=, levels = std::move(levels), sessions = std::move(sessions)]() mutable {
            for (auto const& [key, data] : sessions) {
                auto& level = levels[key];
                if (level.name.empty()) level.name = data.name;
                level.data.deaths = data.deaths;
                level.data.runs = data.runs;
                level.data.playtimeNs = data.playtimeNs;
            }
            // The visit in progress is not on disk yet without Quick Save.
            if (!quick && !visitKey.empty() && sessions.contains(visitKey)) addJournal(levels[visitKey].data, visit);
            sendTelegramText(summaryText(player, scope, levels));
        });
    }).detach();
}
