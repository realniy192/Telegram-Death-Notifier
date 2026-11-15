#include "AttemptsPopup.hpp"
#include "Attempts.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/NineSlice.hpp>

using namespace geode::prelude;

namespace {
constexpr ccColor3B kHintColor = {245, 230, 205};
constexpr ccColor3B kMutedColor = {200, 168, 130};
constexpr ccColor3B kAccentColor = {255, 214, 70};
constexpr char kNameChars[] = " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!?.,:;'()[]-_+=*/@#&~^|<>";

// Rows scrolled out of the list must not take touches.
class ListMenu : public CCMenu {
public:
    ScrollLayer* m_scroll = nullptr;
    bool ccTouchBegan(CCTouch* touch, CCEvent* event) override {
        if (m_scroll) {
            auto point = m_scroll->convertToNodeSpace(touch->getLocation());
            if (!CCRect({0, 0}, m_scroll->getContentSize()).containsPoint(point)) return false;
        }
        return CCMenu::ccTouchBegan(touch, event);
    }
    static ListMenu* create(ScrollLayer* scroll) {
        auto menu = new ListMenu();
        if (menu->init()) {
            menu->m_scroll = scroll;
            menu->autorelease();
            return menu;
        }
        delete menu;
        return nullptr;
    }
};

void toast(std::string const& text, NotificationIcon icon) {
    Notification::create(text, icon)->show();
}

class AttemptsPopup : public Popup {
    GJGameLevel* m_level = nullptr; // retained for the popup's lifetime
    tg::LevelRef m_ref;
    tg::LinkedLevels m_links;
    CCLabelBMFont* m_summary = nullptr;
    CCLabelBMFont* m_missing = nullptr;
    TextInput* m_idInput = nullptr;
    TextInput* m_nameInput = nullptr;
    TextInput* m_lostInput = nullptr;
    ScrollLayer* m_list = nullptr;
    std::vector<std::string> m_rowKeys;
    float m_width = 0, m_height = 0;

    ~AttemptsPopup() override {
        if (m_level) m_level->release();
    }

    CCLabelBMFont* text(CCNode* parent, std::string const& value, CCPoint pos, float scale,
                        ccColor3B color = kHintColor, float anchorX = 0.f) {
        auto label = CCLabelBMFont::create(value.c_str(), "chatFont.fnt");
        label->setScale(scale);
        label->setColor(color);
        label->setAnchorPoint({anchorX, .5f});
        label->setPosition(pos);
        parent->addChild(label);
        return label;
    }

    CCMenuItemSpriteExtra* button(CCMenu* menu, char const* label, CCPoint pos, SEL_MenuHandler callback,
                                  char const* texture = "GJ_button_01.png", int width = 60) {
        auto sprite = ButtonSprite::create(label, width, true, "goldFont.fnt", texture, 30.f, .7f);
        sprite->setScale(.6f);
        auto item = CCMenuItemSpriteExtra::create(sprite, this, callback);
        item->setPosition(pos);
        menu->addChild(item);
        return item;
    }

    TextInput* input(float width, char const* placeholder, CCPoint pos) {
        auto field = TextInput::create(width / .7f, placeholder, "bigFont.fnt");
        field->setScale(.7f);
        field->setPosition(pos);
        m_mainLayer->addChild(field);
        return field;
    }

    void defocus() {
        for (auto field : {m_idInput, m_nameInput, m_lostInput}) if (field) field->defocus();
    }

    void refresh() {
        auto totals = tg::attemptTotals(m_ref, m_level);
        m_summary->setString(fmt::format("This level: {}    Linked: {}    Lost: {}    Total: {}",
            totals.own, totals.linked, totals.lost, totals.total()).c_str());
        m_summary->limitLabelWidth(m_width - 24.f, .6f, .3f);
        m_missing->setString(totals.missing
            ? fmt::format("{} linked level(s) not found on this device, counted as 0.", totals.missing).c_str()
            : "");
        rebuildList();
    }

    // Row buttons rebuild the list they live in, so never do it inside their callback.
    void refreshLater() {
        this->retain();
        geode::queueInMainThread([this] {
            refresh();
            this->release();
        });
    }

    void rebuildList() {
        auto content = m_list->m_contentLayer;
        content->removeAllChildren();
        m_rowKeys.clear();
        constexpr float rowHeight = 24.f;
        float listWidth = m_list->getContentWidth();
        float height = std::max(m_list->getContentHeight(), rowHeight * m_links.size());
        content->setContentSize({listWidth, height});
        if (m_links.empty()) {
            text(content, "No linked levels yet.", {listWidth / 2, height / 2}, .55f, kMutedColor, .5f);
            m_list->scrollToTop();
            return;
        }
        auto menu = ListMenu::create(m_list);
        menu->setPosition({0, 0});
        content->addChild(menu);
        int row = 0;
        for (auto const& [key, name] : m_links) {
            float y = height - rowHeight * (row + .5f);
            auto level = tg::findLevel(key);
            auto line = level
                ? fmt::format("{}  ({} attempts)", name, tg::gdAttempts(level))
                : fmt::format("{}  (not found)", name);
            auto label = text(content, line, {6.f, y}, .55f, level ? kHintColor : kMutedColor);
            label->limitLabelWidth(listWidth - 70.f, .55f, .3f);
            auto remove = button(menu, "Remove", {listWidth - 30.f, y},
                menu_selector(AttemptsPopup::onRemove), "GJ_button_06.png", 50);
            remove->setTag(row);
            m_rowKeys.push_back(key);
            ++row;
        }
        m_list->scrollToTop();
    }

    bool link(GJGameLevel* level, bool notify) {
        auto ref = tg::LevelRef::from(level);
        if (ref.key == m_ref.key) {
            if (notify) toast("That is this level", NotificationIcon::Error);
            return false;
        }
        if (m_links.contains(ref.key)) {
            if (notify) toast("Already linked", NotificationIcon::Warning);
            return false;
        }
        m_links[ref.key] = ref.name;
        tg::saveLinks(m_ref, m_links);
        if (notify) toast(fmt::format("Linked {}", ref.name), NotificationIcon::Success);
        return true;
    }

    bool init(GJGameLevel* level) {
        m_width = 390.f;
        m_height = 270.f;
        if (!Popup::init(m_width, m_height)) return false;
        m_level = level;
        m_level->retain();
        m_ref = tg::LevelRef::from(level);
        m_links = tg::loadLinks(m_ref);
        setTitle("Attempts", "goldFont.fnt", .7f, 22.f);

        m_summary = text(m_mainLayer, "", {m_width / 2, m_height - 46.f}, .6f, kAccentColor, .5f);

        auto menu = CCMenu::create();
        menu->setPosition({0, 0});
        m_mainLayer->addChild(menu);

        float labelX = 16.f, inputX = 205.f, buttonX = m_width - 50.f;
        float y = m_height - 74.f;
        text(m_mainLayer, "Online level ID", {labelX, y}, .55f);
        m_idInput = input(140.f, "ID", {inputX, y});
        m_idInput->setCommonFilter(CommonFilter::Uint);
        button(menu, "Link", {buttonX, y}, menu_selector(AttemptsPopup::onLinkID));

        y -= 28.f;
        text(m_mainLayer, "Editor level name", {labelX, y}, .55f);
        m_nameInput = input(140.f, "Name", {inputX, y});
        m_nameInput->setFilter(kNameChars);
        button(menu, "Link", {buttonX, y}, menu_selector(AttemptsPopup::onLinkName));

        y -= 28.f;
        text(m_mainLayer, "Lost attempts", {labelX, y}, .55f);
        m_lostInput = input(140.f, "0", {inputX, y});
        m_lostInput->setCommonFilter(CommonFilter::Uint);
        m_lostInput->setString(std::to_string(tg::lostAttempts(m_ref)));
        button(menu, "Set", {buttonX, y}, menu_selector(AttemptsPopup::onSetLost));

        y -= 22.f;
        text(m_mainLayer, "Linked copies (their GD attempts are added to the total)", {labelX, y}, .45f, kMutedColor);

        float listBottom = 34.f;
        float listTop = y - 10.f;
        CCSize listSize{m_width - 32.f, listTop - listBottom};
        auto background = NineSlice::create("square02_001.png");
        background->setContentSize(listSize + CCSize{8.f, 8.f});
        background->setOpacity(90);
        background->setAnchorPoint({0.f, 0.f});
        background->setPosition({12.f, listBottom - 4.f});
        m_mainLayer->addChild(background);
        m_list = ScrollLayer::create(listSize);
        m_list->setPosition({16.f, listBottom});
        m_mainLayer->addChild(m_list);

        m_missing = text(m_mainLayer, "", {m_width / 2, 18.f}, .45f, kMutedColor, .5f);
        refresh();
        return true;
    }

public:
    void onLinkID(CCObject*) {
        defocus();
        auto id = numFromString<int>(m_idInput->getString());
        if (!id || id.unwrap() <= 0) {
            toast("Enter a level ID", NotificationIcon::Error);
            return;
        }
        auto level = tg::findOnlineLevel(id.unwrap());
        if (!level) {
            toast("Not in your saved levels", NotificationIcon::Error);
            return;
        }
        if (link(level, true)) {
            m_idInput->setString("");
            refresh();
        }
    }

    void onLinkName(CCObject*) {
        defocus();
        auto levels = tg::findEditorLevelsByName(m_nameInput->getString());
        if (levels.empty()) {
            toast("No editor level with that name", NotificationIcon::Error);
            return;
        }
        int added = 0;
        for (auto level : levels) if (link(level, false)) ++added;
        if (!added) {
            toast("Already linked", NotificationIcon::Warning);
            return;
        }
        toast(fmt::format("Linked {} level(s)", added), NotificationIcon::Success);
        m_nameInput->setString("");
        refresh();
    }

    void onSetLost(CCObject*) {
        defocus();
        auto value = numFromString<int>(m_lostInput->getString());
        if (!value || value.unwrap() < 0 || value.unwrap() > 10'000'000) {
            toast("Lost attempts: 0 to 10000000", NotificationIcon::Error);
            return;
        }
        tg::setLostAttempts(m_ref, value.unwrap());
        toast("Lost attempts saved", NotificationIcon::Success);
        refresh();
    }

    void onRemove(CCObject* sender) {
        auto index = sender->getTag();
        if (index < 0 || index >= (int)m_rowKeys.size()) return;
        m_links.erase(m_rowKeys[index]);
        tg::saveLinks(m_ref, m_links);
        toast("Link removed", NotificationIcon::Success);
        refreshLater();
    }

    void onClose(CCObject* sender) override {
        defocus();
        Popup::onClose(sender);
    }

    static AttemptsPopup* create(GJGameLevel* level) {
        auto popup = new AttemptsPopup();
        if (popup->init(level)) { popup->autorelease(); return popup; }
        delete popup;
        return nullptr;
    }
};
}

void tg::openAttempts(GJGameLevel* level) {
    if (!level) return;
    if (auto popup = AttemptsPopup::create(level)) popup->show();
}
