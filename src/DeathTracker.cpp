#include "DeathTracker.hpp"

#include <Geode/Geode.hpp>
#include <chrono>

using namespace geode::prelude;

namespace {
constexpr char kModID[] = "elohmrow.death_tracker";

Mod* dtMod() {
    return Loader::get()->getLoadedMod(kModID);
}

std::map<std::string, int> counts(matjson::Value const& value, char const* key) {
    if (!value.contains(key)) return {};
    return value[key].as<std::map<std::string, int>>().unwrapOrDefault();
}

uint64_t playtime(matjson::Value const& value, char const* key) {
    if (!value.contains(key)) return 0;
    auto const& pair = value[key];
    uint64_t total = 0;
    for (auto part : {"playtimeF0", "playtimeRuns"}) {
        if (pair.contains(part)) total += pair[part].as<uint64_t>().unwrapOrDefault();
    }
    return total;
}

// GeneralData shape: { deaths: {"54": n}, runs: {"13-45": n}, playtimeGeneral: {...} }
void parseGeneral(matjson::Value const& value, tg::dt::Data& data) {
    for (auto const& [key, count] : counts(value, "deaths")) {
        auto percent = numFromString<int>(key);
        if (percent && count > 0) data.deaths[std::clamp(percent.unwrap(), 0, 100)] += count;
    }
    for (auto const& [key, count] : counts(value, "runs")) {
        auto dash = key.find('-', 1);
        if (dash == std::string::npos || count <= 0) continue;
        auto start = numFromString<int>(key.substr(0, dash));
        auto end = numFromString<int>(key.substr(dash + 1));
        if (!start || !end || start.unwrap() < 0 || end.unwrap() <= start.unwrap()) continue;
        data.runs[{start.unwrap(), std::min(end.unwrap(), 100)}] += count;
    }
    data.playtimeNs += playtime(value, "playtimeGeneral");
}

std::optional<matjson::Value> readJson(std::filesystem::path const& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) return std::nullopt;
    auto json = file::readJson(path);
    if (!json) return std::nullopt;
    return json.unwrap();
}

void readMetadata(std::filesystem::path const& folder, tg::dt::Data& data) {
    auto meta = readJson(folder / "metadata");
    if (!meta) return;
    if (meta->contains("levelName")) data.name = (*meta)["levelName"].asString().unwrapOrDefault();
    if (meta->contains("attempts")) data.attempts = (int)(*meta)["attempts"].asInt().unwrapOrDefault();
}
}

tg::dt::Data& tg::dt::Data::operator+=(Data const& other) {
    if (name.empty()) name = other.name;
    for (auto const& [key, count] : other.deaths) deaths[key] += count;
    for (auto const& [key, count] : other.runs) runs[key] += count;
    playtimeNs += other.playtimeNs;
    if (other.attempts >= 0) attempts = std::max(attempts, 0) + other.attempts;
    return *this;
}

bool tg::dt::available() {
    return dtMod() != nullptr;
}

std::string tg::dt::versionString() {
    auto mod = dtMod();
    return mod ? mod->getVersion().toVString() : "";
}

bool tg::dt::quickSave() {
    auto mod = dtMod();
    return mod && mod->getSettingValue<bool>("quick-save");
}

std::string tg::dt::sessionMethod() {
    auto mod = dtMod();
    return mod ? mod->getSettingValue<std::string>("session-method") : "";
}

std::filesystem::path tg::dt::savePath() {
    auto mod = dtMod();
    if (!mod) return {};
    auto fallback = mod->getSaveDir() / "levels";
    auto path = utils::string::pathToString(mod->getSettingValue<std::filesystem::path>("save-path-new"));
    // The default is "{mod_save_dir}/levels"; expand it if it was stored as is.
    constexpr std::string_view kToken = "{mod_save_dir}";
    if (auto pos = path.find(kToken); pos != std::string::npos) {
        path.replace(pos, kToken.size(), utils::string::pathToString(mod->getSaveDir()));
    }
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_directory(path, ec)) return fallback;
    return std::filesystem::path(path);
}

std::optional<tg::dt::Data> tg::dt::readLevel(std::filesystem::path const& root, std::string const& key) {
    if (root.empty() || key.empty()) return std::nullopt;
    auto folder = root / key;
    std::error_code ec;
    if (!std::filesystem::is_directory(folder, ec)) return std::nullopt;
    Data data;
    if (auto general = readJson(folder / "general.dt")) parseGeneral(*general, data);
    readMetadata(folder, data);
    return data;
}

std::map<std::string, tg::dt::Data> tg::dt::readSessions(std::filesystem::path const& root, long long from, long long to) {
    std::map<std::string, Data> result;
    std::error_code ec;
    if (root.empty() || !std::filesystem::is_directory(root, ec)) return result;
    // Skip files not written since `from` without parsing them. Durations work
    // across clocks, so no file clock to system clock conversion is needed.
    auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    auto threshold = std::filesystem::file_time_type::clock::now()
        - std::chrono::seconds(std::max(0LL, nowSeconds - from) + 3600);
    for (auto const& level : std::filesystem::directory_iterator(root, ec)) {
        if (!level.is_directory()) continue;
        auto sessions = level.path() / "sessions";
        if (!std::filesystem::is_directory(sessions, ec)) continue;
        auto key = utils::string::pathToString(level.path().filename());
        bool matched = false;
        for (auto const& entry : std::filesystem::directory_iterator(sessions, ec)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".dt") continue;
            std::error_code timeError;
            auto written = std::filesystem::last_write_time(entry.path(), timeError);
            if (!timeError && written < threshold) continue;
            auto json = readJson(entry.path());
            if (!json) continue;
            long long last = json->contains("lastPlayed") ? (*json)["lastPlayed"].as<long long>().unwrapOr(-1) : -1;
            if (last <= 0) last = json->contains("sessionStartDate")
                ? (*json)["sessionStartDate"].as<long long>().unwrapOr(-1) : -1;
            if (last < from || last >= to) continue;
            auto const& body = json->contains("data") ? (*json)["data"] : *json;
            parseGeneral(body, result[key]);
            matched = true;
        }
        if (matched) {
            Data meta;
            readMetadata(level.path(), meta);
            result[key].name = meta.name;
        }
    }
    return result;
}
