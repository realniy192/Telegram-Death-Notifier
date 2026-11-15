#include "RatingPanel.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <Geode/ui/TextInput.hpp>

using namespace geode::prelude;

namespace {
constexpr char kTextChars[] = " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?.,:;'()[]-_+=*/@#&~^|<>";

class RatingPanel : public CCNode {
    std::function<void(tg::RatingResult)> m_done;
    bool m_finished = false;
    bool m_askEnjoyment = false;
    int m_enjoyment = 0; // 0 = not set
    CCLabelBMFont* m_value = nullptr;
    TextInput* m_experience = nullptr;

    void finish(bool skipped) {
        if (m_finished) return;
        m_finished = true;
        if (m_experience) m_experience->defocus();
        tg::RatingResult result;
        if (!skipped) {
            if (m_askEnjoyment && m_enjoyment > 0) result.enjoyment = m_enjoyment;
            if (m_experience) result.experience = std::string(utils::string::trim(m_experience->getString()));
        }
        auto done = std::move(m_done);
        if (done) done(std::move(result));
    }

    // Buttons live in this panel, so never remove it inside their callback.
    void removeLater() {
        this->retain();
        geode::queueInMainThread([this] {
            this->removeFromParent();
            this->release();
        });
    }

    void updateValue() {
        if (!m_value) return;
        m_value->setString(m_enjoyment ? fmt::format("{}/10", m_enjoyment).c_str() : "-/10");
    }

    CCLabelBMFont* text(char const* value, CCPoint pos, float scale, char const* font = "chatFont.fnt") {
        auto label = CCLabelBMFont::create(value, font);
        label->setScale(scale);
        label->setPosition(pos);
        this->addChild(label);
        return label;
    }

    void addButton(CCMenu* menu, char const* label, CCPoint pos, SEL_MenuHandler callback,
                   char const* texture, float scale, int width = 40) {
        auto sprite = ButtonSprite::create(label, width, true, "goldFont.fnt", texture, 30.f, .7f);
        sprite->setScale(scale);
        auto item = CCMenuItemSpriteExtra::create(sprite, this, callback);
        item->setPosition(pos);
        menu->addChild(item);
    }

    bool init(bool enjoyment, bool experience, std::function<void(tg::RatingResult)> done) {
        if (!CCNode::init()) return false;
        m_askEnjoyment = enjoyment;
        m_done = std::move(done);
        constexpr float width = 118.f;
        float height = 34.f + (enjoyment ? 46.f : 0.f) + (experience ? 42.f : 0.f) + 30.f;
        this->setContentSize({width, height});
        this->setAnchorPoint({.5f, .5f});
        auto background = NineSlice::create("square02_001.png");
        background->setContentSize({width, height});
        background->setOpacity(180);
        background->setAnchorPoint({0.f, 0.f});
        this->addChild(background, -1);
        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        this->addChild(menu);

        float y = height - 14.f;
        text("Rate this level", {width / 2, y}, .42f, "goldFont.fnt");
        y -= 22.f;
        if (enjoyment) {
            text("Enjoyment", {width / 2, y}, .55f);
            y -= 20.f;
            addButton(menu, "-", {20.f, y}, menu_selector(RatingPanel::onMinus), "GJ_button_04.png", .5f, 20);
            m_value = text("-/10", {width / 2, y}, .4f, "bigFont.fnt");
            addButton(menu, "+", {width - 20.f, y}, menu_selector(RatingPanel::onPlus), "GJ_button_04.png", .5f, 20);
            updateValue();
            y -= 26.f;
        }
        if (experience) {
            text("Experience", {width / 2, y}, .55f);
            y -= 20.f;
            constexpr float inputScale = .6f;
            m_experience = TextInput::create((width - 14.f) / inputScale, "Your thoughts", "bigFont.fnt");
            m_experience->setScale(inputScale);
            m_experience->setFilter(kTextChars);
            m_experience->setMaxCharCount(120);
            m_experience->setPosition({width / 2, y});
            this->addChild(m_experience);
            y -= 22.f;
        }
        addButton(menu, "Send", {width / 2 - 27.f, 16.f}, menu_selector(RatingPanel::onSend), "GJ_button_01.png", .55f);
        addButton(menu, "Skip", {width / 2 + 27.f, 16.f}, menu_selector(RatingPanel::onSkip), "GJ_button_06.png", .55f);
        return true;
    }

public:
    void onMinus(CCObject*) {
        m_enjoyment = std::max(1, (m_enjoyment ? m_enjoyment : 6) - 1);
        updateValue();
    }
    void onPlus(CCObject*) {
        m_enjoyment = std::min(10, (m_enjoyment ? m_enjoyment : 4) + 1);
        updateValue();
    }
    void onSend(CCObject*) {
        finish(false);
        removeLater();
    }
    void onSkip(CCObject*) {
        finish(true);
        removeLater();
    }
    // Replay / menu / closing the screen: send what was entered so far.
    void onExit() override {
        finish(false);
        CCNode::onExit();
    }

    static RatingPanel* create(bool enjoyment, bool experience, std::function<void(tg::RatingResult)> done) {
        auto panel = new RatingPanel();
        if (panel->init(enjoyment, experience, std::move(done))) {
            panel->autorelease();
            return panel;
        }
        delete panel;
        return nullptr;
    }
};
}

void tg::showRatingPanel(CCNode* parent, bool enjoyment, bool experience,
                         std::function<void(RatingResult)> done) {
    if (!parent || (!enjoyment && !experience)) {
        if (done) done({});
        return;
    }
    auto panel = RatingPanel::create(enjoyment, experience, std::move(done));
    if (!panel) return;
    auto size = CCDirector::get()->getWinSize();
    // Left margin of the end screen, clear of its main panel and buttons.
    panel->setPosition({8.f + panel->getContentSize().width / 2, size.height / 2});
    parent->addChild(panel, 100);
    // Menus added after the screen opened need the screen's touch priority.
    for (auto node = parent; node; node = node->getParent()) {
        if (typeinfo_cast<EndLevelLayer*>(node)) {
            handleTouchPriority(node, true);
            break;
        }
    }
}
