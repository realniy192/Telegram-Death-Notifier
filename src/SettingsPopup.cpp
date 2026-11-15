#include "SettingsPopup.hpp"
#include "Settings.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/ui/NineSlice.hpp>
#include <Geode/ui/TextArea.hpp>
#include <functional>
#include <set>

using namespace geode::prelude;

namespace {
constexpr char kTokenChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:_-";
constexpr char kChatChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@_-";
constexpr char kMessageChars[] = " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789{}%!?.,:;'()[]-_+=*/@#&~^|<>";

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

// Page order. Stage 3 inserts a Stats tab right after Runs.
enum Tab { TabRuns, TabTelegram, TabMessages, TabExtra, kTabCount };
constexpr char const* kTabTitles[kTabCount] = {"Runs", "Telegram", "Messages", "Extra"};

ScrollLayer* parentScroll(CCNode* node) {
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

class SettingsPopup : public Popup {
    struct Page {
        ScrollLayer* scroll;
        CCNode* content;
        float cursor;
        float contentHeight;
    };
    struct NumberField {
        TextInput* input;
        int* value;
        int min;
        int max;
        std::string label;
    };
    // A selectable button: both looks are prebuilt, selection only swaps visibility.
    struct Choice {
        CCMenuItemSpriteExtra* button;
        CCNode* idle;
        CCNode* active;
    };

    tg::LevelRef m_levelRef;
    tg::LevelCfg m_level, m_originalLevel;
    tg::GlobalCfg m_global, m_originalGlobal;
    bool m_blacklistCurrent = false, m_originalBlacklistCurrent = false;
    tg::Blacklist m_blacklist;
    std::set<std::string> m_removedBlacklist;
    std::vector<std::string> m_blacklistKeys;
    std::map<std::string, Choice> m_blacklistRows;
    std::vector<tg::RunEntry> m_entries;
    std::vector<Page> m_pages;
    std::vector<Choice> m_tabs;
    std::vector<TextInput*> m_inputs;
    std::vector<NumberField> m_numbers;
    std::vector<CCMenuItemToggler*> m_toggles;
    std::vector<bool*> m_toggleValues;
    std::vector<std::function<void()>> m_refresh;
    CCNode* m_startPanel = nullptr;
    CCNode* m_stepPanel = nullptr;
    TextInput* m_token = nullptr;
    CCLabelBMFont* m_connection = nullptr;
    CCLabelBMFont* m_dirty = nullptr;
    CCMenuItemSpriteExtra* m_apply = nullptr;
    std::optional<Choice> m_startButton;
    std::optional<Choice> m_stepButton;
    bool m_confirming = false;
    float m_width = 0;
    int m_activeTab = 0;

    // ---------- building blocks ----------

    CCLabelBMFont* label(CCNode* parent, std::string const& text, float x, float y,
                         float scale = .35f, bool left = true, char const* font = "bigFont.fnt") {
        auto node = CCLabelBMFont::create(text.c_str(), font);
        node->setAnchorPoint({ left ? 0.f : .5f, .5f });
        node->setPosition({ x, y });
        float desiredScale = std::string_view(font) == "chatFont.fnt" ? scale * 2.2f : scale;
        node->limitLabelWidth(m_width - 28.f, desiredScale, desiredScale * .7f);
        parent->addChild(node);
        return node;
    }

    CCLabelBMFont* hint(CCNode* parent, std::string const& text, float x, float y,
                        float scale = .25f, ccColor3B color = kHintColor) {
        auto node = label(parent, text, x, y, scale, true, "chatFont.fnt");
        node->setColor(color);
        return node;
    }

    // Rounded translucent card behind a row, like the inset panels in vanilla GD.
    void card(CCNode* parent, float y, float height, float x = 4.f, float width = 0.f) {
        if (width <= 0.f) width = m_width - x * 2.f;
        auto bg = NineSlice::create("square02b_001.png");
        // Built at 2x and scaled down so the corner radius stays small on short rows.
        bg->setContentSize({ width * 2.f, height * 2.f });
        bg->setScale(.5f);
        bg->setColor({0, 0, 0});
        bg->setOpacity(70);
        bg->setAnchorPoint({0.f, .5f});
        bg->setPosition({x, y});
        parent->addChild(bg, -1);
    }

    // Builds a vanilla ButtonSprite and scales it to fit inside width x height.
    // ButtonSprite adds its own border padding, so the real size is measured
    // instead of trusting the requested width.
    ButtonSprite* buttonSprite(std::string const& text, float width, float height,
                               char const* font, char const* texture, float textScale) {
        float scale = height / kButtonHeight;
        auto sprite = ButtonSprite::create(text.c_str(), (int)(width / scale), true,
            font, texture, kButtonHeight, textScale);
        auto size = sprite->getContentSize();
        sprite->setScale(std::min(width / size.width, height / size.height));
        return sprite;
    }

    CCMenuItemSpriteExtra* button(CCNode* parent, std::string const& text, CCPoint pos,
                                  SEL_MenuHandler callback, float width, float height,
                                  char const* texture = kIdleTexture) {
        auto menu = PageMenu::create();
        menu->setPosition({0, 0});
        parent->addChild(menu);
        auto sprite = buttonSprite(text, width, height, kIdleFont, texture, .7f);
        auto item = CCMenuItemSpriteExtra::create(sprite, this, callback);
        item->setPosition(pos);
        menu->addChild(item);
        return item;
    }

    // Two prebuilt looks (idle / selected), each with its own text and texture.
    Choice makeChoice(CCMenu* menu, std::string const& idleText, std::string const& activeText,
                      char const* idleTexture, CCPoint pos, float width, float height,
                      SEL_MenuHandler callback) {
        auto idle = buttonSprite(idleText, width, height, kIdleFont, idleTexture, .65f);
        auto active = buttonSprite(activeText, width, height, kActiveFont, kActiveTexture, .55f);
        auto idleSize = idle->getScaledContentSize();
        auto activeSize = active->getScaledContentSize();
        CCSize size{std::max(idleSize.width, activeSize.width), std::max(idleSize.height, activeSize.height)};
        auto holder = CCNode::create();
        holder->setContentSize(size);
        idle->setPosition(size / 2);
        active->setPosition(size / 2);
        holder->addChild(idle);
        holder->addChild(active);
        active->setVisible(false);
        auto item = CCMenuItemSpriteExtra::create(holder, this, callback);
        item->setPosition(pos);
        menu->addChild(item);
        return {item, idle, active};
    }

    Choice choiceButton(CCMenu* menu, std::string const& text, CCPoint pos, float width,
                        float height, SEL_MenuHandler callback) {
        return makeChoice(menu, text, text, kIdleTexture, pos, width, height, callback);
    }

    void setSelected(Choice const& choice, bool selected) {
        choice.idle->setVisible(!selected);
        choice.active->setVisible(selected);
    }

    // One line of mixed-color text.
    // Markup: [text] = yellow value, *text* = white keyword, >> = drawn arrow.
    CCNode* richLine(CCNode* parent, std::string const& markup, float x, float y,
                     float scale = .52f, ccColor3B base = kHintColor) {
        auto line = CCNode::create();
        std::vector<CCNode*> parts;
        float cursor = 0.f, height = 0.f;
        auto addText = [&](std::string const& text, ccColor3B color) {
            if (text.empty()) return;
            auto node = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
            node->setScale(scale);
            node->setColor(color);
            node->setAnchorPoint({0.f, .5f});
            node->setPositionX(cursor);
            line->addChild(node);
            parts.push_back(node);
            cursor += node->getScaledContentWidth();
            height = std::max(height, node->getScaledContentHeight());
        };
        auto addArrow = [&] {
            // chatFont has no arrow glyph, so the arrow is drawn.
            float gap = 5.f * scale, length = 18.f * scale, head = 6.f * scale;
            auto color = ccc4FFromccc3B(kAccentColor);
            auto arrow = CCDrawNode::create();
            arrow->drawSegment({0.f, 0.f}, {length - head * .5f, 0.f}, .9f * scale, color);
            CCPoint tip[] = {{length, 0.f}, {length - head, head * .7f}, {length - head, -head * .7f}};
            arrow->drawPolygon(tip, 3, color, 0.f, color);
            arrow->setPositionX(cursor + gap);
            line->addChild(arrow);
            parts.push_back(arrow);
            cursor += gap * 2.f + length;
        };
        std::string buffer;
        bool value = false, keyword = false;
        auto flush = [&] {
            addText(buffer, value ? kAccentColor : keyword ? kBrightColor : base);
            buffer.clear();
        };
        for (size_t i = 0; i < markup.size(); ++i) {
            char c = markup[i];
            if (c == '[' || c == ']') { flush(); value = c == '['; continue; }
            if (c == '*') { flush(); keyword = !keyword; continue; }
            if (c == '>' && i + 1 < markup.size() && markup[i + 1] == '>') { flush(); addArrow(); ++i; continue; }
            buffer += c;
        }
        flush();
        line->setContentSize({cursor, height});
        for (auto part : parts) part->setPositionY(height / 2);
        line->setAnchorPoint({0.f, .5f});
        line->setPosition({x, y});
        float maxWidth = m_width - x - 8.f;
        if (cursor > maxWidth) line->setScale(maxWidth / cursor);
        parent->addChild(line);
        return line;
    }

    // Soft card with a yellow accent bar: groups explanation lines so the
    // page reads as one block instead of loose floating text.
    float infoBox(CCNode* parent, float top, std::vector<std::string> const& lines,
                  bool accent = true, ccColor3B base = kHintColor, float scale = .52f,
                  float lineHeight = 16.f) {
        float height = lines.size() * lineHeight + 10.f;
        card(parent, top - height / 2, height);
        float textX = 12.f;
        if (accent) {
            auto bar = CCLayerColor::create({255, 200, 60, 210}, 2.f, height - 10.f);
            bar->setPosition({9.f, top - height + 5.f});
            parent->addChild(bar);
            textX = 17.f;
        }
        for (size_t i = 0; i < lines.size(); ++i) {
            richLine(parent, lines[i], textX, top - 5.f - lineHeight * (i + .5f), scale, base);
        }
        return height;
    }

    void infoBox(Page& page, std::vector<std::string> const& lines, bool accent = true,
                 ccColor3B base = kHintColor, float scale = .52f) {
        float height = infoBox(page.content, page.cursor - 2.f, lines, accent, base, scale);
        page.cursor -= height + 6.f;
    }

    // Yellow pill with a placeholder name inside. Returns its width.
    float chip(CCNode* parent, std::string const& text, float x, float y, float scale = .46f) {
        auto label = CCLabelBMFont::create(text.c_str(), "chatFont.fnt");
        label->setScale(scale);
        label->setColor(kAccentColor);
        float width = label->getScaledContentWidth() + 10.f;
        float height = 15.f;
        auto bg = NineSlice::create("square02b_001.png");
        bg->setContentSize({width * 2.f, height * 2.f});
        bg->setScale(.5f);
        bg->setColor({255, 190, 50});
        bg->setOpacity(50);
        bg->setAnchorPoint({0.f, .5f});
        bg->setPosition({x, y});
        parent->addChild(bg);
        label->setAnchorPoint({.5f, .5f});
        label->setPosition({x + width / 2, y});
        parent->addChild(label);
        return width;
    }

    // Placeholder cheat sheet: a 3-column grid of chips, each with its meaning.
    void placeholders(Page& page) {
        struct Item { char const* tag; char const* meaning; };
        Item const items[] = {
            {"{player}", "Your Name"}, {"{level}", "Level Name"}, {"{percent}", "Run %"},
            {"{start}", "Run Start %"}, {"{end}", "Run End %"},
        };
        constexpr int columns = 3;
        constexpr int count = sizeof(items) / sizeof(items[0]);
        constexpr int rows = (count + columns - 1) / columns;
        float header = 16.f, rowHeight = 20.f;
        float top = page.cursor - 2.f;
        float height = header + rows * rowHeight + 12.f;
        card(page.content, top - height / 2, height);
        auto bar = CCLayerColor::create({255, 200, 60, 210}, 2.f, height - 10.f);
        bar->setPosition({9.f, top - height + 5.f});
        page.content->addChild(bar);

        float left = 17.f;
        richLine(page.content, "*Placeholders*  -  put them in any message below.", left,
                 top - 5.f - header / 2, .46f, kMutedColor);
        float columnWidth = (m_width - left - 8.f) / columns;
        for (int i = 0; i < count; ++i) {
            float x = left + columnWidth * (i % columns);
            float y = top - 5.f - header - rowHeight * (i / columns + .5f);
            float width = chip(page.content, items[i].tag, x, y);
            auto meaning = CCLabelBMFont::create(items[i].meaning, "chatFont.fnt");
            meaning->setColor(kHintColor);
            meaning->setAnchorPoint({0.f, .5f});
            meaning->setPosition({x + width + 5.f, y});
            meaning->limitLabelWidth(columnWidth - width - 10.f, .44f, .25f);
            page.content->addChild(meaning);
        }
        page.cursor -= height + 6.f;
    }

    // Small muted line under a control.
    void note(Page& page, std::string const& markup, float height = 18.f) {
        richLine(page.content, markup, 12.f, nextRow(page, height), .48f, kMutedColor);
    }

    float nextRow(Page& page, float height) {
        page.cursor -= height;
        return page.cursor + height / 2;
    }

    // Gold section title with an optional muted caption on the right.
    void section(Page& page, std::string const& title, std::string const& caption = "") {
        float y = nextRow(page, 26.f);
        auto titleLabel = CCLabelBMFont::create(title.c_str(), "goldFont.fnt");
        titleLabel->setAnchorPoint({0.f, .5f});
        titleLabel->setPosition({8.f, y});
        titleLabel->limitLabelWidth(caption.empty() ? m_width - 16.f : m_width * .62f, .5f, .25f);
        page.content->addChild(titleLabel);
        if (!caption.empty()) {
            auto captionLabel = CCLabelBMFont::create(caption.c_str(), "chatFont.fnt");
            captionLabel->setAnchorPoint({1.f, .5f});
            captionLabel->setPosition({m_width - 8.f, y});
            captionLabel->limitLabelWidth(m_width * .33f, .55f, .35f);
            captionLabel->setColor(kMutedColor);
            page.content->addChild(captionLabel);
        }
    }

    void description(Page& page, std::string const& text, float height = 18.f) {
        richLine(page.content, text, 12.f, nextRow(page, height));
    }

    void spacer(Page& page, float height = 6.f) { page.cursor -= height; }

    TextInput* textField(Page& page, std::string const& title, std::string& value,
                         char const* filter, bool password = false, float reserved = 0) {
        float y = nextRow(page, 50.f);
        card(page.content, y, 46.f);
        label(page.content, title, 12.f, y + 12.f, .3f);
        constexpr float inputScale = .7f;
        auto input = PageInput::create((m_width - 24.f - reserved) / inputScale, password ? "Token" : "Text");
        input->setTextAlign(TextInputAlign::Left);
        input->setScale(inputScale);
        input->setPasswordMode(password);
        input->setFilter(filter);
        input->setString(value);
        input->setPosition({ (m_width - reserved) / 2, y - 8.f });
        input->setCallback([this, target = &value](std::string const& text) {
            *target = text;
            changed();
        });
        page.content->addChild(input);
        m_inputs.push_back(input);
        return input;
    }

    void numberField(CCNode* parent, std::string const& title, int& value,
                     float y, int min = 0, int max = 100) {
        card(parent, y, 30.f);
        auto titleLabel = label(parent, title, 12.f, y, .32f);
        titleLabel->limitLabelWidth(m_width - 105.f, .32f, .2f);
        auto input = PageInput::create(65.f, "0");
        input->setCommonFilter(CommonFilter::Uint);
        input->setScale(.65f);
        input->setString(std::to_string(value));
        input->setPosition({m_width - 36.f, y});
        input->setCallback([this, target = &value, min, max](std::string const& text) {
            auto number = numFromString<int>(text);
            if (number && number.unwrap() >= min && number.unwrap() <= max) *target = number.unwrap();
            changed();
        });
        parent->addChild(input);
        m_numbers.push_back({input, &value, min, max, title});
        m_inputs.push_back(input);
    }

    CCMenuItemToggler* toggle(CCNode* parent, std::string const& title, bool& value, float y) {
        card(parent, y, 28.f);
        auto titleLabel = label(parent, title, 12.f, y, .32f);
        titleLabel->limitLabelWidth(m_width - 70.f, .32f, .2f);
        auto menu = PageMenu::create();
        menu->setPosition({0, 0});
        parent->addChild(menu);
        auto item = CCMenuItemToggler::createWithStandardSprites(this,
            menu_selector(SettingsPopup::onToggle), .55f);
        item->toggle(value);
        item->setPosition({m_width - 22.f, y});
        item->setTag((int)m_toggles.size());
        menu->addChild(item);
        m_toggles.push_back(item);
        m_toggleValues.push_back(&value);
        return item;
    }

    // ---------- state ----------

    bool dirty() const {
        if (!(m_global == m_originalGlobal) || !(m_level == m_originalLevel)) return true;
        if (m_blacklistCurrent != m_originalBlacklistCurrent || !m_removedBlacklist.empty()) return true;
        for (auto const& field : m_numbers) {
            if (std::string(field.input->getString()) != std::to_string(*field.value)) return true;
        }
        return false;
    }

    void changed() {
        bool isDirty = dirty();
        m_dirty->setString(isDirty ? "Unsaved changes - press Apply" : "All changes saved");
        m_dirty->setColor(isDirty ? kWarnColor : kOkColor);
        for (auto const& refresh : m_refresh) refresh();
        if (m_connection) {
            bool token = !utils::string::trim(m_global.token).empty();
            bool chat = !utils::string::trim(m_global.chat).empty();
            bool telegramDirty = m_global.token != m_originalGlobal.token || m_global.chat != m_originalGlobal.chat;
            m_connection->setString(!token ? "Bot token is missing"
                : !chat ? "Channel is missing"
                : telegramDirty ? "Unsaved changes - press Apply to save."
                : "Token and channel are saved.");
            m_connection->setColor(!token || !chat ? kErrorColor : telegramDirty ? kWarnColor : kOkColor);
        }
        if (m_startButton) setSelected(*m_startButton, m_level.mode == tg::RunMode::StartPositions);
        if (m_stepButton) setSelected(*m_stepButton, m_level.mode == tg::RunMode::MinimumProgress);
    }

    void defocus() {
        for (auto input : m_inputs) input->defocus();
    }

    float runsHeight() const {
        return std::max(290.f, 218.f + m_entries.size() * 32.f);
    }

    float extraHeight() const {
        return 780.f + m_blacklist.size() * 32.f;
    }

    void layoutRuns() {
        auto& page = m_pages[TabRuns];
        float height = m_level.mode == tg::RunMode::MinimumProgress ? 300.f
            : std::max(page.scroll->getContentHeight(), runsHeight());
        page.content->setPositionY(height - page.contentHeight);
        page.scroll->m_contentLayer->setContentHeight(height);
        page.scroll->scrollToTop();
    }

    void selectMode(tg::RunMode mode) {
        defocus();
        m_level.mode = mode;
        layoutRuns();
        m_startPanel->setVisible(mode == tg::RunMode::StartPositions);
        m_stepPanel->setVisible(mode == tg::RunMode::MinimumProgress);
        changed();
    }

    // ---------- pages ----------

    void buildRuns(Page& page, PlayLayer* layer) {
        section(page, std::string(layer->m_level->m_levelName), "This level only");
        toggle(page.content, "Enabled for this level", m_level.enabled, nextRow(page, 32.f));
        toggle(page.content, "Blacklist this level", m_blacklistCurrent, nextRow(page, 32.f));
        note(page, "*Blacklisted:*  nothing is sent or tracked on this level.", 16.f);
        spacer(page, 4.f);

        float y = nextRow(page, 34.f);
        auto modes = PageMenu::create();
        modes->setPosition({0, 0});
        page.content->addChild(modes);
        // Compact pair centered in the page, not stretched edge to edge.
        float gap = 10.f;
        float choiceWidth = std::min(140.f, (m_width - 8.f - gap) / 2);
        float center = m_width / 2;
        m_startButton = choiceButton(modes, "Start Positions", {center - (choiceWidth + gap) / 2, y},
            choiceWidth, 24.f, menu_selector(SettingsPopup::onStartMode));
        m_stepButton = choiceButton(modes, "Minimum progress", {center + (choiceWidth + gap) / 2, y},
            choiceWidth, 24.f, menu_selector(SettingsPopup::onStepMode));
        spacer(page, 6.f);

        float top = page.cursor;
        m_startPanel = CCNode::create();
        m_stepPanel = CCNode::create();
        page.content->addChild(m_startPanel);
        page.content->addChild(m_stepPanel);

        // Start positions: one card per start position.
        if (m_entries.empty()) {
            infoBox(m_startPanel, top - 2.f, {"No start positions in this level - only runs from *0%* count."},
                false, kMutedColor, .5f);
        } else {
            auto header = [&](std::string const& text, float x, bool left) {
                auto node = label(m_startPanel, text, x, top - 10.f, .26f, left);
                node->setColor(kMutedColor);
            };
            header("Start", 12.f, true);
            header("Notify", m_width * .56f, false);
            header("Min. end %", m_width - 36.f, false);
        }
        for (size_t i = 0; i < m_entries.size(); ++i) {
            auto const& entry = m_entries[i];
            auto& cfg = m_level.runs.at(entry.key);
            float rowY = top - 36.f - i * 32.f;
            card(m_startPanel, rowY, 28.f);
            label(m_startPanel, entry.label, 12.f, rowY, .32f);
            auto menu = PageMenu::create();
            menu->setPosition({0, 0});
            m_startPanel->addChild(menu);
            auto item = CCMenuItemToggler::createWithStandardSprites(this,
                menu_selector(SettingsPopup::onToggle), .55f);
            item->toggle(cfg.enabled);
            item->setTag((int)m_toggles.size());
            item->setPosition({m_width * .56f, rowY});
            menu->addChild(item);
            m_toggles.push_back(item);
            m_toggleValues.push_back(&cfg.enabled);
            auto input = PageInput::create(65.f, "0");
            input->setCommonFilter(CommonFilter::Uint);
            input->setScale(.65f);
            input->setPosition({m_width - 36.f, rowY});
            input->setString(std::to_string(cfg.minPercent));
            input->setCallback([this, value = &cfg.minPercent](std::string const& text) {
                auto parsed = numFromString<int>(text);
                if (parsed && parsed.unwrap() >= 0 && parsed.unwrap() <= 100) *value = parsed.unwrap();
                changed();
            });
            m_startPanel->addChild(input);
            m_inputs.push_back(input);
            m_numbers.push_back({input, &cfg.minPercent, 0, 100, entry.label + " minimum end"});
        }

        // Minimum progress: one threshold plus a short, plain explanation.
        numberField(m_stepPanel, "Minimum run length (%)", m_level.minRunProgress, top - 16.f);
        float boxTop = top - 37.f;
        boxTop -= infoBox(m_stepPanel, boxTop, {
            "Sends a message when a run is at least this long.",
            "Example:  [4 - 54%] >> [50% run]",
        }) + 6.f;
        infoBox(m_stepPanel, boxTop, {
            "*Noclip:*  every death starts a new run.",
            "*Practice:*  the start moves only after a respawn.",
            "[0%]  =  every run is sent.",
        }, false, kMutedColor, .48f, 15.f);
        m_startPanel->setVisible(m_level.mode == tg::RunMode::StartPositions);
        m_stepPanel->setVisible(m_level.mode == tg::RunMode::MinimumProgress);
    }

    void buildTelegram(Page& page) {
        section(page, "Telegram", "Shared by all levels");
        m_token = textField(page, "Bot token (always hidden)", m_global.token, kTokenChars, true, 64.f);
        button(page.content, "Clear", {m_width - 36.f, m_token->getPositionY()},
               menu_selector(SettingsPopup::onClearToken), 48.f, 20.f, kDangerTexture);
        textField(page, "Channel (@name or -100...)", m_global.chat, kChatChars);
        m_connection = label(page.content, "", 10.f, nextRow(page, 22.f), .26f, true, "chatFont.fnt");
        spacer(page, 4.f);
        section(page, "Setup");
        infoBox(page, {
            "[1.]   Create a bot with [@BotFather] and copy its token.",
            "[2.]   Add the bot to your channel as an [admin].",
            "[3.]   Paste the token and channel, then press [Apply].",
        });
        note(page, "Token and channel aren't verified until the first message is sent.", 16.f);
        note(page, "*Clear* won't delete the saved token until you press *Apply*.", 16.f);
    }

    void preview(Page& page, std::string& message, std::string const& fallback, bool run) {
        auto node = SimpleTextArea::create("Preview", "chatFont.fnt", .5f, m_width - 24.f);
        node->setAlignment(kCCTextAlignmentLeft);
        node->setMaxLines(2);
        node->setWrappingMode(WORD_WRAP);
        node->setAnchorPoint({0.f, .5f});
        node->setPosition({12.f, nextRow(page, 30.f)});
        node->setColor({kPreviewColor.r, kPreviewColor.g, kPreviewColor.b, 255});
        page.content->addChild(node);
        m_refresh.push_back([this, node, text = &message, fallback, run] {
            int digits = m_global.showDecimals ? m_global.decimalPlaces : 0;
            std::string player = m_global.customNickname && !m_global.nickname.empty() ? m_global.nickname : "Player";
            auto value = tg::formatTemplate(*text, fallback, {
                {"player", player}, {"level", "Stereo Madness"},
                {"start", tg::formatPercent(tg::percentUnits(run ? 4.25 : 0, digits), digits)},
                {"end", tg::formatPercent(tg::percentUnits(54.32, digits), digits)},
                {"percent", tg::formatPercent(tg::percentUnits(54.32, digits), digits)}
            });
            node->setText("Preview: " + value);
        });
    }

    void buildMessages(Page& page) {
        section(page, "Messages", "Shared by all levels");
        placeholders(page);
        textField(page, "Death message", m_global.deathMessage, kMessageChars);
        preview(page, m_global.deathMessage, "{player} died at {percent}% on {level}.", false);
        textField(page, "New best message (from 0%)", m_global.bestMessage, kMessageChars);
        preview(page, m_global.bestMessage, "{player} got a new best of {percent}% on {level}!", false);
        textField(page, "Run message (start pos / practice / noclip)", m_global.runMessage, kMessageChars);
        preview(page, m_global.runMessage, "{player} got a {start}-{end}% run on {level}.", true);
        textField(page, "Level complete message", m_global.beatMessage, kMessageChars);
        preview(page, m_global.beatMessage, "{player} beat {level}!", false);
        spacer(page, 4.f);
        section(page, "Nickname");
        toggle(page.content, "Use custom nickname", m_global.customNickname, nextRow(page, 32.f));
        auto nickname = textField(page, "Nickname", m_global.nickname,
            " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.");
        m_refresh.push_back([this, nickname] { nickname->setEnabled(m_global.customNickname); });
        note(page, "Previews are local - nothing is sent.");
    }

    void buildExtra(Page& page) {
        section(page, "Precision", "Shared by all levels");
        toggle(page.content, "Show decimals", m_global.showDecimals, nextRow(page, 32.f));
        numberField(page.content, "Decimal places (1-4)", m_global.decimalPlaces,
                    nextRow(page, 34.f), 1, 4);
        auto digits = m_numbers.back().input;
        m_refresh.push_back([this, digits] { digits->setEnabled(m_global.showDecimals); });
        note(page, "*Off:*  [54%]      *On:*  [54.32%]");
        toggle(page.content, "Only new bests", m_global.onlyBest, nextRow(page, 32.f));
        note(page, "Only a new best from [0%] is sent. Works only when *Start Positions* mode is enabled.");
        spacer(page, 6.f);
        section(page, "Screenshots", "Shared by all levels");
        toggle(page.content, "Send screenshots without text", m_global.noCaption, nextRow(page, 32.f));
        note(page, "Only the picture is sent, with no caption.");
        numberField(page.content, "Level complete delay (ms)", m_global.completeDelayMs,
                    nextRow(page, 34.f), 0, 5000);
        numberField(page.content, "Death screenshot delay (ms)", m_global.deathDelayMs,
                    nextRow(page, 34.f), 0, 1000);
        note(page, "If the level restarts first, the frame of the death is sent.");
        numberField(page.content, "Upload timeout (s)", m_global.uploadTimeout,
                    nextRow(page, 34.f), 5, 120);
        spacer(page, 6.f);
        section(page, "Fun");
        toggle(page.content, "Skewed screenshot", m_global.skewShot, nextRow(page, 32.f));
        numberField(page.content, "Skew strength (10-400)", m_global.skewStrength,
                    nextRow(page, 34.f), 10, 400);
        auto skew = m_numbers.back().input;
        m_refresh.push_back([this, skew] { skew->setEnabled(m_global.skewShot); });
        note(page, "Recreates the old diagonal render bug.  [60]  =  classic look.");
        spacer(page, 6.f);
        section(page, "Defaults", "Levels without settings");
        toggle(page.content, "Enabled on new levels", m_global.defaultEnabled, nextRow(page, 32.f));
        toggle(page.content, "Default mode: minimum progress", m_global.defaultStepMode, nextRow(page, 32.f));
        note(page, "*Off:* new levels use *Start Positions* mode.");
        numberField(page.content, "Default minimum end %", m_global.defaultMin, nextRow(page, 34.f));
        numberField(page.content, "Default minimum run length %", m_global.defaultMinRunProgress, nextRow(page, 34.f));
        note(page, "Used only by levels you haven't set up yet.");
        spacer(page, 6.f);
        buildBlacklist(page);
    }

    // Other blacklisted levels; the current one is toggled on the Runs tab.
    void buildBlacklist(Page& page) {
        section(page, "Blacklist", "Ignored levels");
        std::vector<std::pair<std::string, std::string>> rows;
        for (auto const& [key, name] : m_blacklist) {
            if (key != m_levelRef.key) rows.push_back({key, name});
        }
        if (rows.empty()) {
            note(page, "No other blacklisted levels. Add this one on the *Runs* tab.");
            return;
        }
        auto menu = PageMenu::create();
        menu->setPosition({0, 0});
        page.content->addChild(menu);
        for (auto const& [key, name] : rows) {
            float y = nextRow(page, 32.f);
            card(page.content, y, 28.f);
            auto title = label(page.content, name.empty() ? key : name, 12.f, y, .32f);
            title->limitLabelWidth(m_width - 110.f, .32f, .2f);
            auto row = makeChoice(menu, "Remove", "Removed", kDangerTexture, {m_width - 44.f, y},
                70.f, 20.f, menu_selector(SettingsPopup::onBlacklistRemove));
            row.button->setTag((int)m_blacklistKeys.size());
            m_blacklistKeys.push_back(key);
            m_blacklistRows.emplace(key, row);
        }
        note(page, "Removals are saved when you press *Apply*.");
    }

    bool validate() {
        for (auto const& field : m_numbers) {
            auto number = numFromString<int>(field.input->getString());
            if (!number || number.unwrap() < field.min || number.unwrap() > field.max) {
                FLAlertLayer::create("Invalid value", fmt::format("{} must be a whole number from {} to {}.",
                    field.label, field.min, field.max), "OK")->show();
                return false;
            }
            *field.value = number.unwrap();
        }
        return true;
    }

    bool apply() {
        defocus();
        bool hadChanges = dirty();
        if (!validate()) return false;
        if (!(m_level == m_originalLevel)) tg::saveLevelCfg(m_levelRef, m_level);
        if (!(m_global == m_originalGlobal)) tg::saveGlobalCfg(m_global);
        if (m_blacklistCurrent != m_originalBlacklistCurrent) tg::setBlacklisted(m_levelRef, m_blacklistCurrent);
        for (auto const& key : m_removedBlacklist) {
            tg::removeFromBlacklist(key);
            if (auto row = m_blacklistRows.find(key); row != m_blacklistRows.end()) {
                row->second.button->setEnabled(false);
            }
        }
        m_removedBlacklist.clear();
        m_originalGlobal = m_global;
        m_originalLevel = m_level;
        m_originalBlacklistCurrent = m_blacklistCurrent;
        for (auto const& field : m_numbers) field.input->setString(std::to_string(*field.value));
        changed();
        if (hadChanges) Notification::create("Settings saved", NotificationIcon::Success)->show();
        return true;
    }

    bool init(PlayLayer* layer) {
        auto screen = CCDirector::get()->getWinSize();
        // The close button sits on the top-left corner and sticks out of the
        // popup, so leave enough margin for it to stay fully on screen.
        float width = std::min(440.f, screen.width - 64.f);
        float height = std::min(290.f, screen.height - 44.f);
        if (!Popup::init(width, height)) return false;
        if (m_closeBtn) m_closeBtn->setPosition(m_closeBtn->getPosition() + CCPoint{4.f, -4.f});
        m_width = width - 24.f;
        m_levelRef = tg::LevelRef::from(layer->m_level);
        m_global = tg::loadGlobalCfg();
        m_level = tg::loadLevelCfg(m_levelRef);
        m_blacklist = tg::loadBlacklist();
        m_blacklistCurrent = m_blacklist.contains(m_levelRef.key);
        m_entries = tg::collectRunEntries(layer);
        for (auto const& entry : m_entries) m_level.runs[entry.key] = tg::loadRunCfg(m_levelRef, entry.key);
        m_originalGlobal = m_global;
        m_originalLevel = m_level;
        m_originalBlacklistCurrent = m_blacklistCurrent;

        setTitle("Telegram Death Notifier", "goldFont.fnt", .7f, 22.f);
        m_title->limitLabelWidth(width - 90.f, .7f, .3f);

        // Footer: save state on the left, Apply on the right.
        m_dirty = label(m_mainLayer, "", 16.f, 22.f, .24f, true, "chatFont.fnt");
        m_dirty->limitLabelWidth(width - 130.f, .53f, .4f);
        m_apply = button(m_mainLayer, "Apply", {width - 48.f, 22.f},
            menu_selector(SettingsPopup::onApply), 64.f, 24.f);

        // Tabs sit in their own row above the panel instead of overlapping it.
        auto tabs = PageMenu::create();
        tabs->setPosition({0, 0});
        m_mainLayer->addChild(tabs);
        // Compact, evenly spaced tab row centered under the title.
        float tabGap = 6.f;
        float tabWidth = std::min(84.f, (m_width - tabGap * (kTabCount - 1)) / kTabCount);
        float rowWidth = tabWidth * kTabCount + tabGap * (kTabCount - 1);
        float rowStart = 12.f + (m_width - rowWidth) / 2;
        float tabY = height - 50.f;
        for (int i = 0; i < kTabCount; ++i) {
            auto tab = choiceButton(tabs, kTabTitles[i],
                {rowStart + tabWidth / 2 + (tabWidth + tabGap) * i, tabY}, tabWidth, 22.f,
                menu_selector(SettingsPopup::onTab));
            tab.button->setTag(i);
            m_tabs.push_back(tab);
        }

        float panelBottom = 42.f;
        float viewHeight = tabY - 17.f - panelBottom;
        auto panel = NineSlice::create("square02_001.png");
        panel->setContentSize({m_width, viewHeight});
        panel->setAnchorPoint({0.f, 0.f});
        panel->setPosition({12.f, panelBottom});
        panel->setOpacity(95);
        m_mainLayer->addChild(panel);

        // Scroll area is inset so rows never touch the rounded panel edge.
        float inset = 4.f;
        float scrollHeight = viewHeight - inset * 2;
        float heights[kTabCount] = {};
        heights[TabRuns] = runsHeight();
        heights[TabTelegram] = 250.f;
        heights[TabMessages] = 480.f;
        heights[TabExtra] = extraHeight();
        for (int i = 0; i < kTabCount; ++i) {
            auto scroll = ScrollLayer::create({m_width, scrollHeight});
            scroll->setPosition({12.f, panelBottom + inset});
            scroll->m_contentLayer->setContentSize({m_width, heights[i]});
            scroll->m_contentLayer->setPositionY(scrollHeight - heights[i]);
            auto content = CCNode::create();
            content->setContentSize({m_width, heights[i]});
            scroll->m_contentLayer->addChild(content);
            scroll->setStealingTouches(true);
            m_mainLayer->addChild(scroll);
            m_pages.push_back({scroll, content, heights[i] - 2.f, heights[i]});
        }
        buildRuns(m_pages[TabRuns], layer);
        buildTelegram(m_pages[TabTelegram]);
        buildMessages(m_pages[TabMessages]);
        buildExtra(m_pages[TabExtra]);
        for (int i = 0; i < kTabCount; ++i) {
            if (i == TabRuns) continue; // laid out by layoutRuns()
            auto& page = m_pages[i];
            float usedHeight = page.contentHeight - page.cursor + 8.f;
            float pageHeight = std::max(page.scroll->getContentHeight(), usedHeight);
            page.content->setPositionY(pageHeight - page.contentHeight);
            page.scroll->m_contentLayer->setContentHeight(pageHeight);
            page.scroll->scrollToTop();
        }
        layoutRuns();
        selectTab(TabRuns);
        changed();
        return true;
    }

    void selectTab(int index) {
        defocus();
        m_activeTab = index;
        for (int i = 0; i < kTabCount; ++i) {
            m_pages[i].scroll->setVisible(i == index);
            m_pages[i].scroll->setTouchEnabled(i == index);
            m_pages[i].scroll->enableScrollWheel(i == index);
            setSelected(m_tabs[i], i == index);
        }
    }

    void closeNow() { Popup::onClose(nullptr); }

public:
    void onTab(CCObject* sender) { selectTab(sender->getTag()); }
    void onStartMode(CCObject*) { selectMode(tg::RunMode::StartPositions); }
    void onStepMode(CCObject*) { selectMode(tg::RunMode::MinimumProgress); }
    void onClearToken(CCObject*) {
        m_token->defocus();
        m_token->setString("", true);
    }
    void onToggle(CCObject* sender) {
        int index = sender->getTag();
        // CCMenuItemToggler invokes its callback before toggling the sprite.
        *m_toggleValues.at(index) = !m_toggles.at(index)->isToggled();
        changed();
    }
    void onBlacklistRemove(CCObject* sender) {
        auto const& key = m_blacklistKeys.at(sender->getTag());
        bool removed = !m_removedBlacklist.contains(key);
        if (removed) m_removedBlacklist.insert(key);
        else m_removedBlacklist.erase(key);
        setSelected(m_blacklistRows.at(key), removed);
        changed();
    }
    void onApply(CCObject*) { apply(); }
    void onClose(CCObject* sender) override {
        defocus();
        if (!dirty()) { Popup::onClose(sender); return; }
        if (m_confirming) return;
        m_confirming = true;
        this->retain();
        auto confirm = ConfirmClosePopup::create([this](ConfirmClosePopup::Choice choice) {
            m_confirming = false;
            if (choice == ConfirmClosePopup::Choice::Save) {
                if (apply()) closeNow();
            }
            else if (choice == ConfirmClosePopup::Choice::Discard) {
                Notification::create("Changes discarded", NotificationIcon::None)->show();
                closeNow();
            }
            this->release();
        });
        if (confirm) confirm->show();
        else {
            m_confirming = false;
            this->release();
        }
    }
    static SettingsPopup* create(PlayLayer* layer) {
        auto popup = new SettingsPopup();
        if (popup->init(layer)) { popup->autorelease(); return popup; }
        delete popup;
        return nullptr;
    }
};
}

void tg::openLevelSettings(PlayLayer* layer) {
    if (auto popup = SettingsPopup::create(layer)) popup->show();
}
