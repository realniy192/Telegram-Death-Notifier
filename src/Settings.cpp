#include "Settings.hpp"
#include "SecureTokenSetting.hpp"

#include <Geode/Geode.hpp>
#include <set>

using namespace geode::prelude;

std::string tg::spKey(float x) {
    return fmt::format("sp-{}", (int)std::round(x));
}

bool tg::levelEnabled(int id) {
    return Mod::get()->getSavedValue<bool>(fmt::format("lvl-{}-on", id),
        Mod::get()->getSettingValue<bool>("default-enabled"));
}

tg::RunCfg tg::loadRunCfg(int id, std::string const& run) {
    auto mod = Mod::get();
    auto base = fmt::format("lvl-{}-{}", id, run);
    return { mod->getSavedValue<bool>(base + "-on", true),
             std::clamp((int)mod->getSavedValue<int64_t>(base + "-min",
                 mod->getSettingValue<int64_t>("default-min")), 0, 100) };
}

void tg::saveRunCfg(int id, std::string const& run, RunCfg cfg) {
    auto base = fmt::format("lvl-{}-{}", id, run);
    Mod::get()->setSavedValue(base + "-on", cfg.enabled);
    Mod::get()->setSavedValue<int64_t>(base + "-min", cfg.minPercent);
}

tg::LevelCfg tg::loadLevelCfg(int id) {
    auto mod = Mod::get();
    bool step = mod->getSavedValue<bool>(fmt::format("lvl-{}-step-mode", id),
        mod->getSettingValue<bool>("run-progress-enabled"));
    return { levelEnabled(id), step ? RunMode::MinimumProgress : RunMode::StartPositions,
             std::clamp((int)mod->getSavedValue<int64_t>(fmt::format("lvl-{}-step-min", id),
                 mod->getSettingValue<int64_t>("minimum-run-progress")), 0, 100), {} };
}

void tg::saveLevelCfg(int id, LevelCfg const& cfg) {
    auto mod = Mod::get();
    mod->setSavedValue(fmt::format("lvl-{}-on", id), cfg.enabled);
    mod->setSavedValue(fmt::format("lvl-{}-step-mode", id), cfg.mode == RunMode::MinimumProgress);
    mod->setSavedValue<int64_t>(fmt::format("lvl-{}-step-min", id), cfg.minRunProgress);
    for (auto const& [key, run] : cfg.runs) saveRunCfg(id, key, run);
}

tg::GlobalCfg tg::loadGlobalCfg() {
    auto mod = Mod::get();
    return { botToken(), mod->getSettingValue<std::string>("chat-id"),
             mod->getSettingValue<std::string>("msg-death"),
             mod->getSettingValue<std::string>("msg-run"),
             mod->getSettingValue<std::string>("msg-beat"),
             mod->getSettingValue<bool>("custom-nickname"), mod->getSettingValue<std::string>("nickname"),
             mod->getSettingValue<bool>("show-decimals"),
             std::clamp((int)mod->getSettingValue<int64_t>("decimal-places"), 1, 4),
             mod->getSettingValue<bool>("only-best"), mod->getSettingValue<bool>("default-enabled"),
             mod->getSettingValue<bool>("run-progress-enabled"),
             (int)mod->getSettingValue<int64_t>("default-min"),
             (int)mod->getSettingValue<int64_t>("minimum-run-progress") };
}

void tg::saveGlobalCfg(GlobalCfg const& cfg) {
    auto mod = Mod::get();
    setBotToken(cfg.token);
    mod->setSettingValue<std::string>("chat-id", cfg.chat);
    mod->setSettingValue<std::string>("msg-death", cfg.deathMessage);
    mod->setSettingValue<std::string>("msg-run", cfg.runMessage);
    mod->setSettingValue<std::string>("msg-beat", cfg.beatMessage);
    mod->setSettingValue<bool>("custom-nickname", cfg.customNickname);
    mod->setSettingValue<std::string>("nickname", cfg.nickname);
    mod->setSettingValue<bool>("show-decimals", cfg.showDecimals);
    mod->setSettingValue<int64_t>("decimal-places", cfg.decimalPlaces);
    mod->setSettingValue<bool>("only-best", cfg.onlyBest);
    mod->setSettingValue<bool>("default-enabled", cfg.defaultEnabled);
    mod->setSettingValue<bool>("run-progress-enabled", cfg.defaultStepMode);
    mod->setSettingValue<int64_t>("default-min", cfg.defaultMin);
    mod->setSettingValue<int64_t>("minimum-run-progress", cfg.defaultMinRunProgress);
}

int tg::currentDecimalPlaces() {
    auto mod = Mod::get();
    return mod->getSettingValue<bool>("show-decimals")
        ? std::clamp((int)mod->getSettingValue<int64_t>("decimal-places"), 1, 4) : 0;
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
