#pragma once

// Small widgets and the shared look of the settings popup (moved out of
// SettingsPopup.cpp unchanged).
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>

#include <functional>

namespace tg::ui {

using namespace geode::prelude;

// Vanilla GD button look: green + gold text when idle, cyan + white text when selected.
constexpr char kIdleTexture[] = "GJ_button_01.png";
constexpr char kActiveTexture[] = "GJ_button_02.png";
constexpr char kDangerTexture[] = "GJ_button_06.png";
constexpr char kNeutralTexture[] = "GJ_button_04.png";
constexpr char kIdleFont[] = "goldFont.fnt";
constexpr char kActiveFont[] = "bigFont.fnt";
// Native ButtonSprite height; smaller buttons are scaled down instead of squashed.
constexpr float kButtonHeight = 30.f;

// Warm palette that matches the brown GD popup instead of cold blue-gray.
constexpr ccColor3B kHintColor = {245, 230, 205};   // main explanation text (cream)
constexpr ccColor3B kMutedColor = {200, 168, 130};  // secondary notes (warm tan)
constexpr ccColor3B kBrightColor = {255, 255, 255}; // *keyword* in rich text
constexpr ccColor3B kAccentColor = {255, 214, 70};  // [value] in rich text (yellow)
constexpr ccColor3B kPreviewColor = {230, 210, 175};
constexpr ccColor3B kOkColor = {120, 230, 140};
constexpr ccColor3B kWarnColor = {255, 205, 90};
constexpr ccColor3B kErrorColor = {255, 110, 110};

inline ScrollLayer* parentScroll(CCNode* node) {
    for (auto parent = node->getParent(); parent; parent = parent->getParent()) {
        if (auto scroll = dynamic_cast<ScrollLayer*>(parent)) return scroll;
    }
    return nullptr;
}

// Clipped controls must not react to touches outside their page's viewport.
class PageMenu : public CCMenu {
public:
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) override {
        if (auto scroll = parentScroll(this)) {
            auto point = scroll->convertToNodeSpace(touch->getLocation());
            if (!scroll->isVisible() || !CCRect({0, 0}, scroll->getContentSize()).containsPoint(point)) return false;
        }
        return CCMenu::ccTouchBegan(touch, event);
    }

    static PageMenu* create() {
        auto menu = new PageMenu();
        if (menu->init()) { menu->autorelease(); return menu; }
        delete menu;
        return nullptr;
    }
};

class PageInput : public TextInput {
public:
    bool allowTextInput(CCTextInputNode*) override {
        if (auto scroll = parentScroll(this)) {
            auto point = scroll->convertToNodeSpace(convertToWorldSpace(getContentSize() / 2));
            float halfHeight = getScaledContentHeight() / 2;
            return scroll->isVisible() && point.y >= halfHeight &&
                point.y <= scroll->getContentHeight() - halfHeight;
        }
        return true;
    }

    static PageInput* create(float width, char const* placeholder) {
        auto input = new PageInput();
        if (input->init(width, placeholder, "bigFont.fnt")) { input->autorelease(); return input; }
        delete input;
        return nullptr;
    }
};

// Replaces the quick popup: Escape used to close it without a clear answer.
// Here Escape does nothing, the close button means Cancel, and every choice
// is explicit. The callback always runs exactly once.
class ConfirmClosePopup : public Popup {
public:
    enum class Choice { Cancel, Discard, Save };

private:
    std::function<void(Choice)> m_callback;
    bool m_done = false;

    void finish(Choice choice) {
        if (m_done) return;
        m_done = true;
        auto callback = std::move(m_callback);
        this->retain();
        Popup::onClose(nullptr);
        if (callback) callback(choice);
        this->release();
    }

    void addButton(CCMenu* menu, char const* text, char const* texture, float x, SEL_MenuHandler callback) {
        auto sprite = ButtonSprite::create(text, 70, true, kIdleFont, texture, kButtonHeight, .7f);
        sprite->setScale(.8f);
        auto item = CCMenuItemSpriteExtra::create(sprite, this, callback);
        item->setPosition({x, 30.f});
        menu->addChild(item);
    }

    bool init(std::function<void(Choice)> callback) {
        constexpr float width = 290.f, height = 140.f;
        if (!Popup::init(width, height)) return false;
        m_callback = std::move(callback);
        setTitle("Unsaved changes", "goldFont.fnt", .7f, 22.f);
        auto text = CCLabelBMFont::create("Save your changes before closing?", "chatFont.fnt");
        text->setScale(.8f);
        text->setPosition({width / 2, 78.f});
        m_mainLayer->addChild(text);
        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        m_mainLayer->addChild(menu);
        addButton(menu, "Cancel", kNeutralTexture, width / 2 - 92.f, menu_selector(ConfirmClosePopup::onCancel));
        addButton(menu, "Discard", kDangerTexture, width / 2, menu_selector(ConfirmClosePopup::onDiscard));
        addButton(menu, "Save", kIdleTexture, width / 2 + 92.f, menu_selector(ConfirmClosePopup::onSave));
        return true;
    }

public:
    void onCancel(CCObject*) { finish(Choice::Cancel); }
    void onDiscard(CCObject*) { finish(Choice::Discard); }
    void onSave(CCObject*) { finish(Choice::Save); }
    void onClose(CCObject*) override { finish(Choice::Cancel); }
    // Escape must never decide for the user.
    void keyBackClicked() override {}

    static ConfirmClosePopup* create(std::function<void(Choice)> callback) {
        auto popup = new ConfirmClosePopup();
        if (popup->init(std::move(callback))) { popup->autorelease(); return popup; }
        delete popup;
        return nullptr;
    }
};

}
