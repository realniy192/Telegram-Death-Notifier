#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include "RunTracking.hpp"
#include "Screenshot.hpp"
#include <set>

using namespace geode::prelude;

// ======================= settings storage =======================
struct RunCfg { bool enabled; int minPercent; };

static bool levelEnabled(int id) {
    return Mod::get()->getSavedValue<bool>(
        fmt::format("lvl-{}-on", id), Mod::get()->getSettingValue<bool>("default-enabled"));
}

static void setLevelEnabled(int id, bool on) {
    Mod::get()->setSavedValue(fmt::format("lvl-{}-on", id), on);
}

// run = "zero" (from 0%) or "sp-<x>" (start position at x)
static RunCfg loadRunCfg(int id, std::string const& run) {
    auto mod = Mod::get();
    auto base = fmt::format("lvl-{}-{}", id, run);
    int defMin = (int)mod->getSettingValue<int64_t>("default-min");
    return {
        mod->getSavedValue<bool>(base + "-on", true),
        (int)mod->getSavedValue<int64_t>(base + "-min", defMin)
    };
}

static void saveRunCfg(int id, std::string const& run, RunCfg c) {
    auto base = fmt::format("lvl-{}-{}", id, run);
    Mod::get()->setSavedValue(base + "-on", c.enabled);
    Mod::get()->setSavedValue<int64_t>(base + "-min", c.minPercent);
}

static std::string spKey(float x) {
    return fmt::format("sp-{}", (int)std::round(x));
}

static std::string nickname() {
    auto mod = Mod::get();
    if (mod->getSettingValue<bool>("custom-nickname")) {
        auto custom = std::string(utils::string::trim(mod->getSettingValue<std::string>("nickname")));
        if (!custom.empty()) return custom;
    }
    std::string name = GJAccountManager::get()->m_username;
    return name.empty() ? "Player" : name;
}

// takes a message template from settings and fills in {placeholders}
static std::string formatMsg(char const* setting, char const* fallback,
                             std::vector<std::pair<std::string, std::string>> const& vars) {
    auto tpl = std::string(utils::string::trim(Mod::get()->getSettingValue<std::string>(setting)));
    if (tpl.empty()) tpl = fallback;

    for (auto const& [name, value] : vars) {
        std::string token = "{" + name + "}";
        size_t pos = 0;
        while ((pos = tpl.find(token, pos)) != std::string::npos) {
            tpl.replace(pos, token.size(), value);
            pos += value.size();
        }
    }
    return tpl;
}

// completion caption waiting for the end screen
static std::optional<std::string> g_pendingWin;

// ======================= gameplay =======================
class $modify(TGPlayLayer, PlayLayer) {
    struct Fields {
        tg::RunTracking run;
        bool resetting = false;
        bool restoredCheckpoint = false;
    };

    static void onModify(auto& self) {
        // run before other mods (noclip hacks) so we see every death
        if (!self.setHookPriorityPre("PlayLayer::destroyPlayer", Priority::First)) {
            log::warn("Failed to set destroyPlayer hook priority");
        }
    }

    tg::RunFilters currentFilters() {
        auto mod = Mod::get();
        int id = m_level->m_levelID.value();
        std::string key = m_startPosObject ? spKey(m_startPosObject->getPositionX()) : "zero";
        auto cfg = loadRunCfg(id, key);
        return { levelEnabled(id), cfg.enabled, cfg.minPercent,
                 mod->getSettingValue<bool>("run-progress-enabled"),
                 (int)mod->getSettingValue<int64_t>("minimum-run-progress"),
                 mod->getSettingValue<bool>("only-best") };
    }

    std::string runCaption(tg::RunResult const& run, bool completed) {
        auto vars = std::vector<std::pair<std::string, std::string>> {
            { "player", nickname() }, { "level", std::string(m_level->m_levelName) },
            { "start", tg::formatPercent(run.start, run.decimals) },
            { "end", tg::formatPercent(run.end, run.decimals) },
            { "percent", tg::formatPercent(run.end, run.decimals) }
        };
        if (run.start > 0 || run.fromCheckpoint || m_isPracticeMode) {
            return formatMsg("msg-run", "{player} got a {start}-{end}% run on {level}.", vars);
        }
        return completed
            ? formatMsg("msg-beat", "{player} beat {level}!", vars)
            : formatMsg("msg-death", "{player} died at {percent}% on {level}.", vars);
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
        // loadFromCheckpoint records its exact restored percent inside this call.
        if (!m_fields->restoredCheckpoint) {
            m_fields->run.restart(this->getCurrentPercent());
        }
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
        // Take progress/best before the game or another mod changes death state.
        std::optional<std::string> caption;
        if (obj != m_anticheatSpike && !m_fields->resetting) {
            bool decimals = Mod::get()->getSettingValue<bool>("show-decimals");
            auto run = m_fields->run.finish(this->getCurrentPercent(), decimals);
            if (run && tg::shouldNotify(*run, currentFilters(), m_level->m_normalPercent.value(),
                                        m_isPracticeMode, false)) {
                caption = runCaption(*run, false);
            }
        }
        PlayLayer::destroyPlayer(player, obj);
        if (caption) tg::queueScreenshot(std::move(*caption));
    }

    void levelComplete() {
        bool decimals = Mod::get()->getSettingValue<bool>("show-decimals");
        auto run = m_fields->run.finish(100.0, decimals);
        if (run) {
            g_pendingWin.reset();
            if (tg::shouldNotify(*run, currentFilters(), m_level->m_normalPercent.value(),
                                 m_isPracticeMode, true)) {
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
        if (g_pendingWin) {
            // wait for the drop-down animation to finish
            this->scheduleOnce(schedule_selector(TGEndLayer::onTGShot), 1.2f);
        }
    }

    void onTGShot(float) {
        if (!g_pendingWin) return;
        auto caption = std::move(*g_pendingWin);
        g_pendingWin.reset();
        tg::queueScreenshot(std::move(caption));
    }
};

// ======================= settings popup =======================
static std::vector<float> collectStartPositions(PlayLayer* pl) {
    std::set<int> xs;
    for (auto obj : CCArrayExt<GameObject*>(pl->m_objects)) {
        if (obj && obj->m_objectID == 31) xs.insert((int)std::round(obj->getPositionX()));
    }
    return { xs.begin(), xs.end() };
}

static int startPosPercent(PlayLayer* pl, float x) {
    if (pl->m_levelLength <= 0.f) return 0;
    return std::clamp((int)(x / pl->m_levelLength * 100.f), 0, 100);
}

class TGLevelPopup : public Popup {
    struct Row { std::string run; CCMenuItemToggler* toggle; TextInput* input; };

    int m_id = 0;
    CCMenuItemToggler* m_master = nullptr;
    std::vector<Row> m_rows;

    CCNode* makeRow(std::string const& label, std::string const& run, float width, float height) {
        auto cfg = loadRunCfg(m_id, run);

        auto row = CCNode::create();
        row->setContentSize({ width, height });

        auto lbl = CCLabelBMFont::create(label.c_str(), "bigFont.fnt");
        lbl->setScale(0.45f);
        lbl->setAnchorPoint({ 0.f, 0.5f });
        lbl->setPosition({ 10.f, height / 2 });
        row->addChild(lbl);

        auto menu = CCMenu::create();
        menu->setPosition({ 0.f, 0.f });
        auto toggle = CCMenuItemToggler::createWithStandardSprites(this, nullptr, 0.6f);
        toggle->toggle(cfg.enabled);
        toggle->setPosition({ width * 0.6f, height / 2 });
        menu->addChild(toggle);
        row->addChild(menu);

        auto input = TextInput::create(70.f, "Min");
        input->setCommonFilter(CommonFilter::Uint);
        input->setString(std::to_string(cfg.minPercent));
        input->setScale(0.7f);
        input->setPosition({ width * 0.85f, height / 2 });
        row->addChild(input);

        m_rows.push_back({ run, toggle, input });
        return row;
    }

    void addLabel(char const* text, CCPoint pos, float scale) {
        auto lbl = CCLabelBMFont::create(text, "bigFont.fnt");
        lbl->setScale(scale);
        lbl->setPosition(pos);
        m_mainLayer->addChild(lbl);
    }

protected:
    bool init(PlayLayer* pl) {
        if (!Popup::init(340.f, 260.f)) return false;

        m_id = pl->m_level->m_levelID.value();
        this->setTitle("Telegram Death Notifier");
        if (m_title) m_title->limitLabelWidth(230.f, 0.7f, 0.3f);  // fit next to the gear button

        auto size = m_mainLayer->getContentSize();

        // master toggle + mod settings button
        addLabel("Enabled", { size.width / 2 - 25.f, size.height - 50.f }, 0.45f);
        auto topMenu = CCMenu::create();
        topMenu->setPosition({ 0.f, 0.f });

        m_master = CCMenuItemToggler::createWithStandardSprites(this, nullptr, 0.7f);
        m_master->toggle(levelEnabled(m_id));
        m_master->setPosition({ size.width / 2 + 45.f, size.height - 50.f });
        topMenu->addChild(m_master);

        auto gearSpr = CCSprite::createWithSpriteFrameName("GJ_optionsBtn_001.png");
        gearSpr->setScale(0.6f);
        auto gearBtn = CCMenuItemSpriteExtra::create(gearSpr, this, menu_selector(TGLevelPopup::onModSettings));
        gearBtn->setPosition({ size.width - 25.f, size.height - 25.f });
        topMenu->addChild(gearBtn);

        m_mainLayer->addChild(topMenu);

        // list
        float listX = 15.f, listY = 15.f;
        float listW = size.width - 30.f;
        float listH = size.height - 100.f;
        float rowH = 32.f;

        addLabel("Start", { listX + 35.f, listY + listH + 10.f }, 0.35f);
        addLabel("Old", { listX + listW * 0.6f, listY + listH + 10.f }, 0.35f);
        addLabel("End %", { listX + listW * 0.85f, listY + listH + 10.f }, 0.35f);
        addLabel("Step filter: gear settings (independent of Old)",
                 { size.width / 2, size.height - 68.f }, 0.25f);

        auto bg = CCLayerColor::create({ 0, 0, 0, 80 }, listW, listH);
        bg->setPosition({ listX, listY });
        m_mainLayer->addChild(bg);

        std::vector<std::pair<std::string, std::string>> entries;  // label, run key
        entries.push_back({ "From 0%", "zero" });
        for (float x : collectStartPositions(pl)) {
            entries.push_back({ fmt::format("SP {}%", startPosPercent(pl, x)), spKey(x) });
        }

        auto scroll = ScrollLayer::create({ listW, listH });
        scroll->setPosition({ listX, listY });

        float totalH = std::max(listH, rowH * entries.size());
        scroll->m_contentLayer->setContentSize({ listW, totalH });
        for (size_t i = 0; i < entries.size(); i++) {
            auto row = makeRow(entries[i].first, entries[i].second, listW, rowH);
            row->setPosition({ 0.f, totalH - rowH * (i + 1) });
            scroll->m_contentLayer->addChild(row);
        }
        scroll->m_contentLayer->setPositionY(listH - totalH);  // scroll to top
        m_mainLayer->addChild(scroll);

        return true;
    }

public:
    void onModSettings(CCObject*) {
        openSettingsPopup(Mod::get(), false);
    }

    void onClose(CCObject* sender) override {
        setLevelEnabled(m_id, m_master->isToggled());
        for (auto& r : m_rows) {
            int min = numFromString<int>(r.input->getString()).unwrapOr(0);
            saveRunCfg(m_id, r.run, { r.toggle->isToggled(), std::clamp(min, 0, 100) });
        }
        Popup::onClose(sender);
    }

    static TGLevelPopup* create(PlayLayer* pl) {
        auto ret = new TGLevelPopup();
        if (ret->init(pl)) { ret->autorelease(); return ret; }
        delete ret;
        return nullptr;
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
        if (!spr) spr = ButtonSprite::create("TG");   // fallback if the sprite is missing

        auto btn = CCMenuItemSpriteExtra::create(spr, this, menu_selector(TGPause::onTG));
        btn->setID("tg-button"_spr);
        menu->addChild(btn);
        menu->updateLayout();
    }

    void onTG(CCObject*) {
        if (auto pl = PlayLayer::get()) {
            if (auto popup = TGLevelPopup::create(pl)) popup->show();
        }
    }
};