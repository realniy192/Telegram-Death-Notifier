#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/modify/EndLevelLayer.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/GeodeUI.hpp>
#include <Geode/utils/web.hpp>
#include <Geode/utils/async.hpp>
#include <thread>
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

// ======================= telegram =======================
static void sendToTelegram(ByteVector png, std::string caption) {
    auto token = std::string(utils::string::trim(Mod::get()->getSettingValue<std::string>("bot-token")));
    auto chat  = std::string(utils::string::trim(Mod::get()->getSettingValue<std::string>("chat-id")));
    if (token.starts_with("bot")) token = token.substr(3);
    if (token.empty() || chat.empty()) return;

    web::MultipartForm form;
    form.param("chat_id", chat);
    form.param("caption", caption);
    form.file("photo", png, "shot.png", "image/png");

    auto req = web::WebRequest();
    req.bodyMultipart(form);

    async::spawn(
        req.post(fmt::format("https://api.telegram.org/bot{}/sendPhoto", token)),
        [](web::WebResponse res) {
            if (!res.ok()) {
                log::warn("Telegram error {}: {}", res.code(), res.string().unwrapOr("?"));
            }
        }
    );
}

// ======================= screenshot =======================
#include <Geode/modify/CCEGLView.hpp>

static std::vector<std::string> g_pendingShots;   // captions waiting for the next frame
static int g_shotCounter = 0;

// background thread: flip + PNG encode + file IO, then send on main thread
static void encodeAndSend(std::shared_ptr<std::vector<uint8_t>> pixels, int w, int h, std::string caption) {
    auto path = Mod::get()->getSaveDir() / fmt::format("shot-{}.png", g_shotCounter++);

    std::thread([pixels, w, h, path, caption = std::move(caption)]() mutable {
        // OpenGL gives the image upside down -> flip it
        size_t row = (size_t)w * 4;
        std::vector<uint8_t> flipped(pixels->size());
        for (int y = 0; y < h; y++) {
            std::memcpy(flipped.data() + (size_t)y * row,
                        pixels->data() + (size_t)(h - 1 - y) * row, row);
        }

        auto image = new CCImage();
        bool ok = image->initWithImageData(flipped.data(), (int)flipped.size(), CCImage::kFmtRawData, w, h, 8);
        if (ok) ok = image->saveToFile(path.string().c_str(), true);
        image->release();
        if (!ok) return;

        auto data = file::readBinary(path);
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (!data) return;

        geode::queueInMainThread([png = std::move(data).unwrap(), caption = std::move(caption)]() mutable {
            sendToTelegram(std::move(png), std::move(caption));
        });
    }).detach();
}

// grab the finished frame right before it is shown on screen
static void tgCaptureFrame() {
    if (g_pendingShots.empty()) return;
    auto captions = std::move(g_pendingShots);
    g_pendingShots.clear();

    GLint vp[4] = { 0, 0, 0, 0 };
    glGetIntegerv(GL_VIEWPORT, vp);
    int x = vp[0], y = vp[1], w = vp[2], h = vp[3];
    if (w <= 0 || h <= 0) {
        auto fs = CCDirector::get()->getOpenGLView()->getFrameSize();
        x = 0; y = 0; w = (int)fs.width; h = (int)fs.height;
    }
    if (w <= 0 || h <= 0) return;

    auto pixels = std::make_shared<std::vector<uint8_t>>((size_t)w * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels->data());
    for (auto& c : captions) encodeAndSend(pixels, w, h, std::move(c));
}

#ifdef GEODE_IS_ANDROID
// on Android the real buffer swap happens in Java after drawScene
class $modify(TGDirector, CCDirector) {
    void drawScene() {
        CCDirector::drawScene();
        tgCaptureFrame();
    }
};
#else
class $modify(TGEGLView, CCEGLView) {
    void swapBuffers() {
        tgCaptureFrame();
        CCEGLView::swapBuffers();
    }
};
#endif

// ask for a screenshot of the current frame
static void shootAndSend(std::string caption) {
    g_pendingShots.push_back(std::move(caption));
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
        int startPercent = 0;  // where this attempt started
        bool failed = false;   // a death (real or noclip) already happened this attempt
    };

    static void onModify(auto& self) {
        // run before other mods (noclip hacks) so we see every death
        if (!self.setHookPriorityPre("PlayLayer::destroyPlayer", Priority::First)) {
            log::warn("Failed to set destroyPlayer hook priority");
        }
    }

    RunCfg currentRunCfg() {
        int id = m_level->m_levelID.value();
        if (!levelEnabled(id)) return { false, 0 };
        std::string run = m_startPosObject ? spKey(m_startPosObject->getPositionX()) : "zero";
        return loadRunCfg(id, run);
    }

    std::string levelName() {
        return std::string(m_level->m_levelName);
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        m_fields->startPercent = this->getCurrentPercentInt();
        m_fields->failed = false;
        g_pendingWin.reset();
    }

    void destroyPlayer(PlayerObject* player, GameObject* obj) {
        int prevBest = m_level->m_normalPercent.value();
        PlayLayer::destroyPlayer(player, obj);

        if (obj == m_anticheatSpike) return;

        // only the FIRST death of the attempt counts (real or noclip)
        if (m_fields->failed) return;
        m_fields->failed = true;

        if (m_isPracticeMode) return;

        auto cfg = currentRunCfg();
        int start = m_fields->startPercent;
        int percent = this->getCurrentPercentInt();

        if (!cfg.enabled || percent < cfg.minPercent) return;
        if (Mod::get()->getSettingValue<bool>("only-best") && start == 0 && percent <= prevBest) return;

        std::string caption = start > 0
    ? formatMsg("msg-run", "{player} got a {start}-{end}% run on {level}.", {
          { "player", nickname() }, { "start", std::to_string(start) },
          { "end", std::to_string(percent) }, { "level", levelName() } })
    : formatMsg("msg-death", "{player} died at {percent}% on {level}.", {
          { "player", nickname() }, { "percent", std::to_string(percent) },
          { "level", levelName() } });

        shootAndSend(std::move(caption));
    }

    void levelComplete() {
        g_pendingWin.reset();

        if (!m_fields->failed && !m_isPracticeMode && currentRunCfg().enabled) {
            int start = m_fields->startPercent;
            g_pendingWin = start > 0
    ? formatMsg("msg-run", "{player} got a {start}-{end}% run on {level}.", {
          { "player", nickname() }, { "start", std::to_string(start) },
          { "end", "100" }, { "level", levelName() } })
    : formatMsg("msg-beat", "{player} beat {level}!", {
          { "player", nickname() }, { "level", levelName() } });
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
        shootAndSend(std::move(caption));
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
        addLabel("On", { listX + listW * 0.6f, listY + listH + 10.f }, 0.35f);
        addLabel("Min %", { listX + listW * 0.85f, listY + listH + 10.f }, 0.35f);

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