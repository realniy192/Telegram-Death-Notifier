#include "SettingsPopup.hpp"
#include "SettingsPopupImpl.hpp"
#include "AttemptsPopup.hpp"
#include "Updater.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/Notification.hpp>
#include <Geode/ui/NineSlice.hpp>

using namespace geode::prelude;

namespace tg::ui {

namespace {
constexpr char const* kTabTitles[kTabCount] = {"Runs", "Levels", "Stats", "Telegram", "Messages", "Extra"};
}

// ---------- state ----------

bool SettingsPopup::dirty() const {
    if (!(m_global == m_originalGlobal) || !(m_level == m_originalLevel)) return true;
    if (m_levelFlags != m_originalLevelFlags) return true;
    if (!(m_stats == m_originalStats)) return true;
    for (auto const& field : m_numbers) {
        if (std::string(field.input->getString()) != std::to_string(*field.value)) return true;
    }
    return false;
}

void SettingsPopup::changed() {
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

float SettingsPopup::runsHeight() const {
    return std::max(340.f, 300.f + m_entries.size() * 32.f);
}

void SettingsPopup::layoutRuns() {
    auto& page = m_pages[TabRuns];
    float height = m_level.mode == tg::RunMode::MinimumProgress ? 370.f
        : std::max(page.scroll->getContentHeight(), runsHeight());
    page.content->setPositionY(height - page.contentHeight);
    page.scroll->m_contentLayer->setContentHeight(height);
    page.scroll->scrollToTop();
}

void SettingsPopup::selectMode(tg::RunMode mode) {
    defocus();
    m_level.mode = mode;
    layoutRuns();
    m_startPanel->setVisible(mode == tg::RunMode::StartPositions);
    m_stepPanel->setVisible(mode == tg::RunMode::MinimumProgress);
    changed();
}

bool SettingsPopup::apply() {
    defocus();
    bool hadChanges = dirty();
    if (!validate()) return false;
    if (!(m_level == m_originalLevel)) tg::saveLevelCfg(m_levelRef, m_level);
    if (!(m_global == m_originalGlobal)) tg::saveGlobalCfg(m_global);
    for (auto const& [key, on] : m_levelFlags) {
        auto original = m_originalLevelFlags.find(key);
        if (original != m_originalLevelFlags.end() && original->second == on) continue;
        tg::setLevelEnabled(key, m_otherLevels[key], on);
    }
    if (!(m_stats == m_originalStats)) tg::stats::save(m_stats);
    m_originalStats = m_stats;
    m_originalGlobal = m_global;
    m_originalLevel = m_level;
    m_originalLevelFlags = m_levelFlags;
    for (auto const& field : m_numbers) field.input->setString(std::to_string(*field.value));
    changed();
    if (hadChanges) Notification::create("Settings saved", NotificationIcon::Success)->show();
    return true;
}

bool SettingsPopup::init(PlayLayer* layer) {
    auto screen = CCDirector::get()->getWinSize();
    // The close button sits on the top-left corner and sticks out of the
    // popup, so leave enough margin for it to stay fully on screen.
    float width = std::min(440.f, screen.width - 64.f);
    float height = std::min(290.f, screen.height - 44.f);
    if (!Popup::init(width, height)) return false;
    if (m_closeBtn) m_closeBtn->setPosition(m_closeBtn->getPosition() + CCPoint{4.f, -4.f});
    m_width = width - 24.f;
    m_levelRef = tg::LevelRef::from(layer->m_level);
    m_playLayer = layer;
    m_global = tg::loadGlobalCfg();
    m_stats = tg::stats::load();
    m_originalStats = m_stats;
    m_level = tg::loadLevelCfg(m_levelRef);
    m_otherLevels = tg::loadEnabledLevels();
    m_otherLevels.erase(m_levelRef.key);
    for (auto const& [key, name] : m_otherLevels) m_levelFlags[key] = true;
    m_originalLevelFlags = m_levelFlags;
    m_entries = tg::collectRunEntries(layer);
    for (auto const& entry : m_entries) m_level.runs[entry.key] = tg::loadRunCfg(m_levelRef, entry.key);
    m_originalGlobal = m_global;
    m_originalLevel = m_level;

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
    float tabGap = 5.f;
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
    heights[TabLevels] = 170.f + m_otherLevels.size() * 32.f;
    heights[TabStats] = 900.f;
    heights[TabTelegram] = 270.f;
    heights[TabMessages] = 860.f;
    heights[TabExtra] = 640.f;
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
    buildLevels(m_pages[TabLevels]);
    buildStats(m_pages[TabStats]);
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

void SettingsPopup::selectTab(int index) {
    defocus();
    m_activeTab = index;
    for (int i = 0; i < kTabCount; ++i) {
        m_pages[i].scroll->setVisible(i == index);
        m_pages[i].scroll->setTouchEnabled(i == index);
        m_pages[i].scroll->enableScrollWheel(i == index);
        setSelected(m_tabs[i], i == index);
    }
}

void SettingsPopup::closeNow() { Popup::onClose(nullptr); }

// ---------- callbacks ----------

void SettingsPopup::onTab(CCObject* sender) { selectTab(sender->getTag()); }
void SettingsPopup::onStartMode(CCObject*) { selectMode(tg::RunMode::StartPositions); }
void SettingsPopup::onStepMode(CCObject*) { selectMode(tg::RunMode::MinimumProgress); }

void SettingsPopup::onClearToken(CCObject*) {
    m_token->defocus();
    m_token->setString("", true);
}

void SettingsPopup::onStreakMode(CCObject* sender) {
    m_global.streakMode = std::clamp(sender->getTag(), 0, 2);
    changed();
}

void SettingsPopup::onAttemptSource(CCObject* sender) {
    m_stats.attemptsFromDT = sender->getTag() == 1;
    changed();
}

void SettingsPopup::onAttempts(CCObject*) {
    defocus();
    if (m_playLayer && m_playLayer->m_level) tg::openAttempts(m_playLayer->m_level);
}

void SettingsPopup::onCheckUpdates(CCObject*) {
    defocus();
    tg::updater::checkForUpdates();
}

void SettingsPopup::onApply(CCObject*) { apply(); }

void SettingsPopup::onClose(CCObject* sender) {
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

SettingsPopup* SettingsPopup::create(PlayLayer* layer) {
    auto popup = new SettingsPopup();
    if (popup->init(layer)) { popup->autorelease(); return popup; }
    delete popup;
    return nullptr;
}

}

void tg::openLevelSettings(PlayLayer* layer) {
    if (auto popup = tg::ui::SettingsPopup::create(layer)) popup->show();
}
