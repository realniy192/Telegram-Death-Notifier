#include "Level.hpp"

#include <Geode/Geode.hpp>
#include <cvolton.level-id-api/include/EditorIDs.hpp>

using namespace geode::prelude;

std::string tg::levelKey(GJGameLevel* level) {
    if (!level) return "unknown";
    std::string key;
    if (level->m_levelType == GJLevelType::Editor) {
        key = fmt::format("{}-editor", EditorIDs::getID(level));
    }
    else {
        key = std::to_string(level->m_levelID.value());
        if (level->m_levelType == GJLevelType::Main) key += "-local";
    }
    if (level->m_dailyID > 0) key += "-daily";
    if (level->m_gauntletLevel) key += "-gauntlet";
    return key;
}

tg::LevelRef tg::LevelRef::from(GJGameLevel* level) {
    LevelRef ref;
    ref.key = levelKey(level);
    ref.name = level ? std::string(level->m_levelName) : "Unknown";
    // Every editor level used to share ID 0, so their old settings can't be
    // attributed to any single level. Only migrate real IDs.
    if (level && level->m_levelType != GJLevelType::Editor) {
        ref.legacy = std::to_string(level->m_levelID.value());
    }
    if (ref.legacy == ref.key) ref.legacy.clear();
    return ref;
}
