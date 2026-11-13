#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include "Settings.hpp"
#include "SettingsPopup.hpp"
#include "Screenshot.hpp"

using namespace geode::prelude;

static std::string nickname() {
    auto mod = Mod::get();
    if (mod->getSettingValue<bool>("custom-nickname")) {
        auto custom = std::string(utils::string::trim(mod->getSettingValue<std::string>("nickname")));
        if (!custom.empty()) return custom;
    }
    std::string name = GJAccountManager::get()->m_username;
    return name.empty() ? "Player" : name;
}

static std::optional<std::string> g_pendingWin;

// ======================= gameplay =======================
class $modify(TGPlayLayer, PlayLayer) {
    struct Fields {
        tg::RunTracking run;
        bool resetting = false;
        bool restoredCheckpoint = false;
        bool handlingCollision = false;
    };

    static void onModify(auto& self) {
        // run before other mods (noclip hacks) so we see every death
        if (!self.setHookPriorityPre("PlayLayer::destroyPlayer", Priority::First)) {
            log::warn("Failed to set destroyPlayer hook priority");
        }
    }

    tg::RunFilters currentFilters() {
        auto level = tg::loadLevelCfg(m_level->m_levelID.value());
        std::string key = m_startPosObject ? tg::spKey(m_startPosObject->getPositionX()) : "zero";
        auto cfg = tg::loadRunCfg(m_level->m_levelID.value(), key);
        return { level.enabled, level.mode, cfg.enabled, cfg.minPercent, level.minRunProgress,
                 Mod::get()->getSettingValue<bool>("only-best") };
    }

    std::string runCaption(tg::RunResult const& run, bool completed) {
        auto vars = std::vector<std::pair<std::string, std::string>> {
            { "player", nickname() }, { "level", std::string(m_level->m_levelName) },
            { "start", tg::formatPercent(run.start, run.decimalPlaces) },
            { "end", tg::formatPercent(run.end, run.decimalPlaces) },
            { "percent", tg::formatPercent(run.end, run.decimalPlaces) }
        };
        char const* setting;
        char const* fallback;
        if (run.start > 0 || run.fromCheckpoint || m_isPracticeMode) {
            setting = "msg-run";
            fallback = "{player} got a {start}-{end}% run on {level}.";
        }
        else if (completed) {
            setting = "msg-beat";
            fallback = "{player} beat {level}!";
        }
        else {
            setting = "msg-death";
            fallback = "{player} died at {percent}% on {level}.";
        }
        return tg::formatTemplate(Mod::get()->getSettingValue<std::string>(setting), fallback, vars);
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        m_fields->run.restart(this->getCurrentPercent(), m_fields->run.fromCheckpoint);
        return true;
    }

    void resetLevel() {
        m_fields->resetting = true;
        m_fields->restoredCheckpoint = false;
        g_pendingWin.reset();
        PlayLayer::resetLevel();
        // Keep the exact percent recorded inside loadFromCheckpoint.
        if (!m_fields->restoredCheckpoint) m_fields->run.restart(this->getCurrentPercent());
        m_fields->resetting = false;
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::loadFromCheckpoint(checkpoint);
        if (!checkpoint) return;
        m_fields->run.restart(this->getCurrentPercent(), true);
        if (m_fields->resetting) m_fields->restoredCheckpoint = true;
        g_pendingWin.reset();
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (obj == m_anticheatSpike || m_fields->resetting || m_fields->handlingCollision) {
            PlayLayer::destroyPlayer(player, obj);
            return;
        }
        m_fields->handlingCollision = true;
        auto filters = currentFilters();
        double percent = this->getCurrentPercent();
        auto result = m_fields->run.collide(percent, tg::currentDecimalPlaces(), m_gameState.m_levelTime);
        std::optional<std::string> caption;
        if (result && tg::shouldNotify(*result, filters, m_level->m_normalPercent.value(), m_isPracticeMode, false)) {
            caption = runCaption(*result, false);
        }
        PlayLayer::destroyPlayer(player, obj);
        bool actuallyDead = (player && player->m_isDead) || (m_player1 && m_player1->m_isDead) ||
            (m_gameState.m_isDualMode && m_player2 && m_player2->m_isDead);
        if (result && !actuallyDead && filters.mode == tg::RunMode::MinimumProgress) {
            m_fields->run.continueAfterCollision(percent);
        }
        m_fields->handlingCollision = false;
        if (caption) tg::queueScreenshot(std::move(*caption));
    }

    void levelComplete() {
        auto run = m_fields->run.finish(100.0, tg::currentDecimalPlaces());
        if (run) {
            g_pendingWin.reset();
            if (tg::shouldNotify(*run, currentFilters(), m_level->m_normalPercent.value(), m_isPracticeMode, true)) {
                g_pendingWin = runCaption(*run, true);
            }
        }
        PlayLayer::levelComplete();
    }
};

// ======================= end screen shot =======================
class $modify(TGEndLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        if (g_pendingWin) this->scheduleOnce(schedule_selector(TGEndLayer::onTGShot), 1.2f);
    }

    void onTGShot(float) {
        if (!g_pendingWin) return;
        auto caption = std::move(*g_pendingWin);
        g_pendingWin.reset();
        tg::queueScreenshot(std::move(caption));
    }
};

// ======================= pause button =======================
class $modify(TGPause, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();
        auto menu = this->getChildByID("right-button-menu");
        if (!menu) return;
        CCSprite* spr = CCSprite::create("TG_notifier.png"_spr);
        if (spr) spr->setScale(0.62f);
        if (!spr) spr = ButtonSprite::create("TG");
        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(TGPause::onTG));
        btn->setID("tg-button"_spr);
        menu->addChild(btn);
        menu->updateLayout();
    }

    void onTG(CCObject*) {
        if (auto layer = PlayLayer::get()) tg::openLevelSettings(layer);
    }
};
