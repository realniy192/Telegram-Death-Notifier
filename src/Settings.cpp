#include "Settings.hpp"
#include "SecureTokenSetting.hpp"

#include <Geode/Geode.hpp>
#include <matjson/stl_serialize.hpp>
#include <set>

using namespace geode::prelude;

namespace {
constexpr char kBlacklistKey[] = "blacklist";
constexpr char const* kStreakModes[] = {"Separate", "Replace", "Combine"};

int streakModeIndex(std::string const& name) {
    for (int i = 0; i < 3; ++i) if (name == kStreakModes[i]) return i;
    return 2;
}

std::string savedName(std::string const& level, std::string const& suffix) {
    return fmt::format("lvl-{}-{}", level, suffix);
}

// New saves use the current key. Pre-1.2 saves are read as a fallback until
// the level is saved again; nothing is written under the old key.
template <class T>
T readLevel(tg::LevelRef const& ref, std::string const& suffix, T fallback) {
    auto mod = Mod::get();
    auto name = savedName(ref.key, suffix);
    if (mod->hasSavedValue(name)) return mod->getSavedValue<T>(name);
    if (!ref.legacy.empty()) {
        auto old = savedName(ref.legacy, suffix);
        if (mod->hasSavedValue(old)) return mod->getSavedValue<T>(old);
    }
    return fallback;
}

int intSetting(char const* key, int min, int max) {
    return std::clamp((int)Mod::get()->getSettingValue<int64_t>(key), min, max);
}
}

std::string tg::spKey(float x) {
    return fmt::format("sp-{}", (int)std::round(x));
}

bool tg::levelEnabled(LevelRef const& level) {
    return readLevel<bool>(level, "on", Mod::get()->getSettingValue<bool>("default-enabled"));
}

tg::RunCfg tg::loadRunCfg(LevelRef const& level, std::string const& run) {
    RunCfg cfg;
    cfg.enabled = readLevel<bool>(level, run + "-on", true);
    cfg.minPercent = std::clamp((int)readLevel<int64_t>(level, run + "-min",
        Mod::get()->getSettingValue<int64_t>("default-min")), 0, 100);
    return cfg;
}

void tg::saveRunCfg(LevelRef const& level, std::string const& run, RunCfg cfg) {
    auto mod = Mod::get();
    mod->setSavedValue(savedName(level.key, run + "-on"), cfg.enabled);
    mod->setSavedValue<int64_t>(savedName(level.key, run + "-min"), cfg.minPercent);
}

tg::LevelCfg tg::loadLevelCfg(LevelRef const& level) {
    auto mod = Mod::get();
    LevelCfg cfg;
    cfg.enabled = levelEnabled(level);
    bool step = readLevel<bool>(level, "step-mode", mod->getSettingValue<bool>("run-progress-enabled"));
    cfg.mode = step ? RunMode::MinimumProgress : RunMode::StartPositions;
    cfg.minRunProgress = std::clamp((int)readLevel<int64_t>(level, "step-min",
        mod->getSettingValue<int64_t>("minimum-run-progress")), 0, 100);
    return cfg;
}

void tg::saveLevelCfg(LevelRef const& level, LevelCfg const& cfg) {
    auto mod = Mod::get();
    mod->setSavedValue(savedName(level.key, "on"), cfg.enabled);
    mod->setSavedValue(savedName(level.key, "step-mode"), cfg.mode == RunMode::MinimumProgress);
    mod->setSavedValue<int64_t>(savedName(level.key, "step-min"), cfg.minRunProgress);
    for (auto const& [key, run] : cfg.runs) saveRunCfg(level, key, run);
}

tg::GlobalCfg tg::loadGlobalCfg() {
    auto mod = Mod::get();
    GlobalCfg cfg;
    cfg.token = botToken();
    cfg.chat = mod->getSettingValue<std::string>("chat-id");
    cfg.deathMessage = mod->getSettingValue<std::string>("msg-death");
    cfg.runMessage = mod->getSettingValue<std::string>("msg-run");
    cfg.beatMessage = mod->getSettingValue<std::string>("msg-beat");
    cfg.bestMessage = mod->getSettingValue<std::string>("msg-best");
    cfg.customNickname = mod->getSettingValue<bool>("custom-nickname");
    cfg.nickname = mod->getSettingValue<std::string>("nickname");
    cfg.showDecimals = mod->getSettingValue<bool>("show-decimals");
    cfg.decimalPlaces = intSetting("decimal-places", 1, 4);
    cfg.onlyBest = mod->getSettingValue<bool>("only-best");
    cfg.defaultEnabled = mod->getSettingValue<bool>("default-enabled");
    cfg.defaultStepMode = mod->getSettingValue<bool>("run-progress-enabled");
    cfg.defaultMin = intSetting("default-min", 0, 100);
    cfg.defaultMinRunProgress = intSetting("minimum-run-progress", 0, 100);
    cfg.noCaption = mod->getSettingValue<bool>("no-caption");
    cfg.skewShot = mod->getSettingValue<bool>("skewed-screenshot");
    cfg.skewStrength = intSetting("skew-strength", 10, 400);
    cfg.completeDelayMs = intSetting("complete-delay-ms", 0, 5000);
    cfg.deathDelayMs = intSetting("death-delay-ms", 0, 1000);
    cfg.uploadTimeout = intSetting("upload-timeout", 5, 120);
    cfg.streakEnabled = mod->getSettingValue<bool>("streak-enabled");
    cfg.streakMin = intSetting("streak-min", 2, 20);
    cfg.streakMode = streakModeIndex(mod->getSettingValue<std::string>("streak-mode"));
    cfg.streakSameMessage = mod->getSettingValue<std::string>("msg-streak-same");
    cfg.streakMixedMessage = mod->getSettingValue<std::string>("msg-streak-mixed");
    return cfg;
}

void tg::saveGlobalCfg(GlobalCfg const& cfg) {
    auto mod = Mod::get();
    setBotToken(cfg.token);
    mod->setSettingValue<std::string>("chat-id", cfg.chat);
    mod->setSettingValue<std::string>("msg-death", cfg.deathMessage);
    mod->setSettingValue<std::string>("msg-run", cfg.runMessage);
    mod->setSettingValue<std::string>("msg-beat", cfg.beatMessage);
    mod->setSettingValue<std::string>("msg-best", cfg.bestMessage);
    mod->setSettingValue<bool>("custom-nickname", cfg.customNickname);
    mod->setSettingValue<std::string>("nickname", cfg.nickname);
    mod->setSettingValue<bool>("show-decimals", cfg.showDecimals);
    mod->setSettingValue<int64_t>("decimal-places", cfg.decimalPlaces);
    mod->setSettingValue<bool>("only-best", cfg.onlyBest);
    mod->setSettingValue<bool>("default-enabled", cfg.defaultEnabled);
    mod->setSettingValue<bool>("run-progress-enabled", cfg.defaultStepMode);
    mod->setSettingValue<int64_t>("default-min", cfg.defaultMin);
    mod->setSettingValue<int64_t>("minimum-run-progress", cfg.defaultMinRunProgress);
    mod->setSettingValue<bool>("no-caption", cfg.noCaption);
    mod->setSettingValue<bool>("skewed-screenshot", cfg.skewShot);
    mod->setSettingValue<int64_t>("skew-strength", cfg.skewStrength);
    mod->setSettingValue<int64_t>("complete-delay-ms", cfg.completeDelayMs);
    mod->setSettingValue<int64_t>("death-delay-ms", cfg.deathDelayMs);
    mod->setSettingValue<int64_t>("upload-timeout", cfg.uploadTimeout);
    mod->setSettingValue<bool>("streak-enabled", cfg.streakEnabled);
    mod->setSettingValue<int64_t>("streak-min", cfg.streakMin);
    mod->setSettingValue<std::string>("streak-mode", kStreakModes[std::clamp(cfg.streakMode, 0, 2)]);
    mod->setSettingValue<std::string>("msg-streak-same", cfg.streakSameMessage);
    mod->setSettingValue<std::string>("msg-streak-mixed", cfg.streakMixedMessage);
}

int tg::currentDecimalPlaces() {
    auto mod = Mod::get();
    return mod->getSettingValue<bool>("show-decimals") ? intSetting("decimal-places", 1, 4) : 0;
}

int tg::currentStreakMode() {
    return streakModeIndex(Mod::get()->getSettingValue<std::string>("streak-mode"));
}

std::vector<tg::RunEntry> tg::collectRunEntries(PlayLayer* layer) {
    std::set<int> xs;
    for (auto obj : CCArrayExt<GameObject*>(layer->m_objects)) {
        if (obj && obj->m_objectID == 31) xs.insert((int)std::round(obj->getPositionX()));
    }
    std::vector<RunEntry> entries { { "zero", "From 0%" } };
    for (int x : xs) {
        int percent = layer->m_levelLength > 0
            ? std::clamp((int)(x / layer->m_levelLength * 100.f), 0, 100) : 0;
        entries.push_back({ spKey((float)x), fmt::format("SP {}%", percent) });
    }
    return entries;
}

std::string tg::formatTemplate(std::string text, std::string const& fallback,
    std::vector<std::pair<std::string, std::string>> const& variables) {
    if (utils::string::trim(text).empty()) text = fallback;
    for (auto const& [name, value] : variables) {
        std::string token = "{" + name + "}";
        size_t pos = 0;
        while ((pos = text.find(token, pos)) != std::string::npos) {
            text.replace(pos, token.size(), value);
            pos += value.size();
        }
    }
    return text;
}

tg::Blacklist tg::loadBlacklist() {
    auto mod = Mod::get();
    if (!mod->hasSavedValue(kBlacklistKey)) return {};
    return mod->getSavedValue<Blacklist>(kBlacklistKey);
}

bool tg::isBlacklisted(std::string const& key) {
    return loadBlacklist().contains(key);
}

void tg::setBlacklisted(LevelRef const& level, bool blacklisted) {
    auto list = loadBlacklist();
    if (blacklisted) list[level.key] = level.name;
    else list.erase(level.key);
    Mod::get()->setSavedValue(kBlacklistKey, list);
}

void tg::removeFromBlacklist(std::string const& key) {
    auto list = loadBlacklist();
    if (list.erase(key)) Mod::get()->setSavedValue(kBlacklistKey, list);
}
