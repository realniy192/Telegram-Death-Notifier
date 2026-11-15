#include "Attempts.hpp"

#include <Geode/Geode.hpp>
#include <cvolton.level-id-api/include/EditorIDs.hpp>
#include <matjson/stl_serialize.hpp>
#include <cctype>

using namespace geode::prelude;

namespace {
constexpr int kMaxLostAttempts = 10'000'000;

std::string linksKey(tg::LevelRef const& level) {
    return fmt::format("lvl-{}-links", level.key);
}

std::string lostKey(tg::LevelRef const& level) {
    return fmt::format("lvl-{}-lost-attempts", level.key);
}

std::string lower(std::string text) {
    for (auto& c : text) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return text;
}
}

tg::LinkedLevels tg::loadLinks(LevelRef const& level) {
    auto mod = Mod::get();
    auto key = linksKey(level);
    if (!mod->hasSavedValue(key)) return {};
    return mod->getSavedValue<LinkedLevels>(key);
}

void tg::saveLinks(LevelRef const& level, LinkedLevels const& links) {
    Mod::get()->setSavedValue(linksKey(level), links);
}

int tg::lostAttempts(LevelRef const& level) {
    auto mod = Mod::get();
    auto key = lostKey(level);
    if (!mod->hasSavedValue(key)) return 0;
    return std::clamp((int)mod->getSavedValue<int64_t>(key), 0, kMaxLostAttempts);
}

void tg::setLostAttempts(LevelRef const& level, int attempts) {
    Mod::get()->setSavedValue<int64_t>(lostKey(level), std::clamp(attempts, 0, kMaxLostAttempts));
}

GJGameLevel* tg::findOnlineLevel(int id) {
    if (id <= 0) return nullptr;
    return GameLevelManager::get()->getSavedLevel(id);
}

GJGameLevel* tg::findLevel(std::string const& key) {
    auto dash = key.find('-');
    auto id = numFromString<int>(key.substr(0, dash));
    if (!id || id.unwrap() <= 0) return nullptr;
    std::string suffix = dash == std::string::npos ? "" : key.substr(dash + 1);
    if (suffix.starts_with("editor")) {
        for (auto level : CCArrayExt<GJGameLevel*>(LocalLevelManager::get()->m_localLevels)) {
            if (level && EditorIDs::getID(level) == id.unwrap()) return level;
        }
        return nullptr;
    }
    if (suffix.starts_with("local")) return GameLevelManager::get()->getMainLevel(id.unwrap(), true);
    // Daily and gauntlet copies are looked up as the saved online level.
    return findOnlineLevel(id.unwrap());
}

std::vector<GJGameLevel*> tg::findEditorLevelsByName(std::string const& name) {
    std::vector<GJGameLevel*> found;
    auto wanted = lower(std::string(utils::string::trim(name)));
    if (wanted.empty()) return found;
    for (auto level : CCArrayExt<GJGameLevel*>(LocalLevelManager::get()->m_localLevels)) {
        if (level && lower(std::string(level->m_levelName)) == wanted) found.push_back(level);
    }
    return found;
}

int tg::gdAttempts(GJGameLevel* level) {
    return level ? std::max(0, (int)level->m_attempts.value()) : 0;
}

tg::AttemptTotals tg::attemptTotals(LevelRef const& level, GJGameLevel* current) {
    AttemptTotals totals;
    totals.own = gdAttempts(current);
    totals.lost = lostAttempts(level);
    for (auto const& [key, name] : loadLinks(level)) {
        auto linked = findLevel(key);
        if (!linked || linked == current) {
            ++totals.missing;
            continue;
        }
        totals.linked += gdAttempts(linked);
    }
    return totals;
}
