#pragma once

// Building blocks of the settings popup pages (moved out of SettingsPopup.cpp).
#include "PopupWidgets.hpp"

#include <Geode/ui/NineSlice.hpp>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

namespace tg::ui {

class PopupKit : public Popup {
protected:
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

    std::vector<TextInput*> m_inputs;
    std::vector<NumberField> m_numbers;
    std::vector<CCMenuItemToggler*> m_toggles;
    std::vector<bool*> m_toggleValues;
    std::vector<std::function<void()>> m_refresh;
    float m_width = 0;

    // Called after every edit; refreshes labels and the unsaved-changes state.
    virtual void changed() = 0;

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

    float nextRow(Page& page, float height) {
        page.cursor -= height;
        return page.cursor + height / 2;
    }

    // Small muted line under a control.
    void note(Page& page, std::string const& markup, float height = 18.f) {
        richLine(page.content, markup, 12.f, nextRow(page, height), .48f, kMutedColor);
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

    CCMenu* pageMenu(Page& page) {
        auto menu = PageMenu::create();
        menu->setPosition({0, 0});
        page.content->addChild(menu);
        return menu;
    }

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
        auto item = toggler(value);
        item->setPosition({m_width - 22.f, y});
        menu->addChild(item);
        return item;
    }

    // A bare checkbox bound to value; the caller places it.
    CCMenuItemToggler* toggler(bool& value) {
        auto item = CCMenuItemToggler::createWithStandardSprites(this,
            menu_selector(PopupKit::onToggle), .55f);
        item->toggle(value);
        item->setTag((int)m_toggles.size());
        m_toggles.push_back(item);
        m_toggleValues.push_back(&value);
        return item;
    }

    void defocus() {
        for (auto input : m_inputs) input->defocus();
    }

    // Copies every number field into its value; false (with an alert) on a bad one.
    bool validate() {
        for (auto const& field : m_numbers) {
            auto number = numFromString<int>(field.input->getString());
            if (!number || number.unwrap() < field.min || number.unwrap() > field.max) {
                auto range = field.max == std::numeric_limits<int>::max()
                    ? fmt::format("of at least {}", field.min)
                    : fmt::format("from {} to {}", field.min, field.max);
                FLAlertLayer::create("Invalid value",
                    fmt::format("{} must be a whole number {}.", field.label, range), "OK")->show();
                return false;
            }
            *field.value = number.unwrap();
        }
        return true;
    }

    // Several checkboxes can show the same value (this level on the Runs and
    // Levels tabs). The clicked one flips itself after its callback, so the
    // others are matched to the values on the next frame.
    void syncTogglesLater() {
        this->retain();
        geode::queueInMainThread([this] {
            for (size_t i = 0; i < m_toggles.size(); ++i) {
                bool value = *m_toggleValues[i];
                if (m_toggles[i]->isToggled() != value) m_toggles[i]->toggle(value);
            }
            this->release();
        });
    }

public:
    void onToggle(CCObject* sender) {
        int index = sender->getTag();
        // CCMenuItemToggler invokes its callback before toggling the sprite.
        *m_toggleValues.at(index) = !m_toggles.at(index)->isToggled();
        changed();
        syncTogglesLater();
    }
};

}
