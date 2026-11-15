#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include "Attempts.hpp"
#include "Journal.hpp"
#include "Settings.hpp"
#include "SettingsPopup.hpp"
#include "Screenshot.hpp"
#include "Streak.hpp"

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

static int intSetting(char const* key, int min, int max) {
    return std::clamp((int)Mod::get()->getSettingValue<int64_t>(key), min, max);
}

static std::optional<tg::ShotRequest> g_pendingWin;

// ======================= gameplay =======================
class $modify(TGPlayLayer, PlayLayer) {
    struct Fields {
        tg::RunTracking run;
        tg::StreakTracker streak;
        tg::LevelRef ref;
        bool resetting = false;
        bool restoredCheckpoint = false;
        bool handlingCollision = false;
        bool journaling = false;   // this visit is being recorded
        bool timeCounted = false;  // current attempt time already added
        int seenAttempts = 0;      // PlayLayer::m_attempts already recorded
    };

    enum class Kind { Death, Best, Run, Beat };

    static void onModify(auto& self) {
        // run before other mods (noclip hacks) so we see every death
        if (!self.setHookPriorityPre("PlayLayer::destroyPlayer", Priority::First)) {
            log::warn("Failed to set destroyPlayer hook priority");
        }
    }

    // Read on every event, so toggling the blacklist mid-level applies at once.
    bool ignored() {
        return tg::isBlacklisted(m_fields->ref.key);
    }

    tg::RunFilters currentFilters() {
        auto const& ref = m_fields->ref;
        auto level = tg::loadLevelCfg(ref);
        std::string key = m_startPosObject ? tg::spKey(m_startPosObject->getPositionX()) : "zero";
        auto cfg = tg::loadRunCfg(ref, key);
        return { level.enabled, level.mode, cfg.enabled, cfg.minPercent, level.minRunProgress,
                 Mod::get()->getSettingValue<bool>("only-best") };
    }

    bool fromZero(tg::RunResult const& run) {
        return run.start == 0 && !run.fromCheckpoint && !m_isPracticeMode;
    }

    Kind captionKind(tg::RunResult const& run, bool completed, int previousBest) {
        if (!fromZero(run)) return Kind::Run;
        if (completed) return Kind::Beat;
        // GD stores bests as whole percents: 54.32% does not beat a 54% best.
        if (run.end / run.scale() > previousBest) return Kind::Best;
        return Kind::Death;
    }

    std::vector<std::pair<std::string, std::string>> baseVars() {
        return {
            { "player", nickname() }, { "level", std::string(m_level->m_levelName) },
            { "attempts", std::to_string(tg::gdAttempts(m_level)) },
            { "total_attempts", std::to_string(tg::attemptTotals(m_fields->ref, m_level).total()) }
        };
    }

    std::string runCaption(tg::RunResult const& run, bool completed, int previousBest) {
        auto vars = baseVars();
        vars.push_back({ "start", tg::formatPercent(run.start, run.decimalPlaces) });
        vars.push_back({ "end", tg::formatPercent(run.end, run.decimalPlaces) });
        vars.push_back({ "percent", tg::formatPercent(run.end, run.decimalPlaces) });
        char const* setting;
        char const* fallback;
        switch (captionKind(run, completed, previousBest)) {
            case Kind::Run:
                setting = "msg-run";
                fallback = "{player} got a {start}-{end}% run on {level}.";
                break;
            case Kind::Beat:
                setting = "msg-beat";
                fallback = "{player} beat {level}!";
                break;
            case Kind::Best:
                setting = "msg-best";
                fallback = "{player} got a new best of {percent}% on {level}!";
                break;
            default:
                setting = "msg-death";
                fallback = "{player} died at {percent}% on {level}.";
                break;
        }
        return tg::formatTemplate(Mod::get()->getSettingValue<std::string>(setting), fallback, vars);
    }

    // Adds a qualifying progress; returns the streak text once it is long enough.
    std::optional<std::string> extendStreak(tg::RunResult const& run) {
        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("streak-enabled")) return std::nullopt;
        auto& streak = m_fields->streak;
        auto count = streak.push({ run.start, run.end, run.decimalPlaces, fromZero(run) });
        if ((int)count < intSetting("streak-min", 2, 20)) return std::nullopt;
        auto vars = baseVars();
        vars.push_back({ "count", std::to_string(count) });
        vars.push_back({ "run", tg::StreakTracker::label(streak.entries.back(), false) });
        vars.push_back({ "runs", streak.list() });
        bool same = streak.allSame();
        return tg::formatTemplate(
            mod->getSettingValue<std::string>(same ? "msg-streak-same" : "msg-streak-mixed"),
            same ? "{player} did {run}% on {level} {count}x in a row"
                 : "{player} got {runs} on {level} back to back",
            vars);
    }

    tg::ShotRequest compose(std::string caption, std::optional<std::string> streak, int delayMs) {
        tg::ShotRequest request;
        request.delayMs = delayMs;
        if (!streak) {
            request.caption = std::move(caption);
            return request;
        }
        switch (static_cast<tg::StreakMode>(tg::currentStreakMode())) {
            case tg::StreakMode::Separate:
                request.caption = std::move(caption);
                request.followUp = std::move(*streak);
                break;
            case tg::StreakMode::Replace:
                request.caption = std::move(*streak);
                break;
            default:
                request.caption = caption + "\n" + *streak;
                break;
        }
        return request;
    }

    // ---------- journal ----------

    void syncAttempts() {
        if (!m_fields->journaling) return;
        int now = m_attempts;
        if (now > m_fields->seenAttempts) tg::journal::addAttempts(now - m_fields->seenAttempts);
        m_fields->seenAttempts = now;
    }

    void countAttemptTime() {
        if (!m_fields->journaling || m_fields->timeCounted) return;
        m_fields->timeCounted = true;
        tg::journal::addPlaytime(m_gameState.m_levelTime);
    }

    void journalResult(tg::RunResult const& run) {
        if (!m_fields->journaling) return;
        int start = run.start / run.scale(), end = run.end / run.scale();
        if (fromZero(run)) tg::journal::addDeath(end);
        else tg::journal::addRun(start, end);
    }

    // ---------- hooks ----------

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;
        m_fields->ref = tg::LevelRef::from(level);
        m_fields->run.restart(this->getCurrentPercent(), m_fields->run.fromCheckpoint);
        if (!ignored()) {
            tg::journal::beginVisit(m_fields->ref);
            m_fields->journaling = true;
            syncAttempts();
        }
        return true;
    }

    void resetLevel() {
        // A delayed death shot must show the death, not the next attempt.
        tg::flushDelayedScreenshots();
        countAttemptTime();
        m_fields->resetting = true;
        m_fields->restoredCheckpoint = false;
        g_pendingWin.reset();
        PlayLayer::resetLevel();
        // Keep the exact percent recorded inside loadFromCheckpoint.
        if (!m_fields->restoredCheckpoint) m_fields->run.restart(this->getCurrentPercent());
        m_fields->resetting = false;
        m_fields->timeCounted = false;
        syncAttempts();
    }

    void onQuit() {
        tg::flushDelayedScreenshots();
        countAttemptTime();
        syncAttempts();
        if (m_fields->journaling) tg::journal::endVisit();
        m_fields->journaling = false;
        PlayLayer::onQuit();
    }

    void loadFromCheckpoint(CheckpointObject* checkpoint) {
        PlayLayer::loadFromCheckpoint(checkpoint);
        if (!checkpoint) return;
        m_fields->run.restart(this->getCurrentPercent(), true);
        if (m_fields->resetting) m_fields->restoredCheckpoint = true;
        g_pendingWin.reset();
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        if (obj == m_anticheatSpike || m_fields->resetting || m_fields->handlingCollision || ignored()) {
            PlayLayer::destroyPlayer(player, obj);
            return;
        }
        m_fields->handlingCollision = true;
        auto filters = currentFilters();
        double percent = this->getCurrentPercent();
        // Read before the original call: GD updates the best inside it.
        int previousBest = m_level->m_normalPercent.value();
        auto result = m_fields->run.collide(percent, tg::currentDecimalPlaces(), m_gameState.m_levelTime);
        std::optional<tg::ShotRequest> shot;
        if (result) {
            if (tg::shouldNotify(*result, filters, previousBest, m_isPracticeMode, false)) {
                auto caption = runCaption(*result, false, previousBest);
                auto streak = extendStreak(*result);
                shot = compose(std::move(caption), std::move(streak), intSetting("death-delay-ms", 0, 1000));
            }
            // Any collision that is not a progress (noclip included) breaks the streak.
            else m_fields->streak.reset();
        }
        PlayLayer::destroyPlayer(player, obj);
        bool actuallyDead = (player && player->m_isDead) || (m_player1 && m_player1->m_isDead) ||
            (m_gameState.m_isDualMode && m_player2 && m_player2->m_isDead);
        if (result && actuallyDead) journalResult(*result);
        if (result && !actuallyDead && filters.mode == tg::RunMode::MinimumProgress) {
            m_fields->run.continueAfterCollision(percent);
        }
        m_fields->handlingCollision = false;
        if (shot) tg::queueScreenshot(std::move(*shot));
    }

    void levelComplete() {
        if (ignored()) {
            g_pendingWin.reset();
            PlayLayer::levelComplete();
            return;
        }
        int previousBest = m_level->m_normalPercent.value();
        auto run = m_fields->run.finish(100.0, tg::currentDecimalPlaces());
        if (run) {
            g_pendingWin.reset();
            journalResult(*run);
            countAttemptTime();
            if (tg::shouldNotify(*run, currentFilters(), previousBest, m_isPracticeMode, true)) {
                auto caption = runCaption(*run, true, previousBest);
                auto streak = extendStreak(*run);
                g_pendingWin = compose(std::move(caption), std::move(streak), 0);
            }
            else m_fields->streak.reset();
        }
        PlayLayer::levelComplete();
    }
};

// ======================= end screen shot =======================
class $modify(TGEndLayer, EndLevelLayer) {
    void customSetup() {
        EndLevelLayer::customSetup();
        if (!g_pendingWin) return;
        // Lets the end screen animation finish before the capture.
        float delay = intSetting("complete-delay-ms", 0, 5000) / 1000.f;
        this->scheduleOnce(schedule_selector(TGEndLayer::onTGShot), delay);
    }

    void onTGShot(float) {
        if (!g_pendingWin) return;
        auto request = std::move(*g_pendingWin);
        g_pendingWin.reset();
        tg::queueScreenshot(std::move(request));
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
