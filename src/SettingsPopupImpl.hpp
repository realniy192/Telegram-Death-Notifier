#pragma once

// The pause-menu settings popup. Pages are built in SettingsPages.cpp,
// state, saving and callbacks live in SettingsPopup.cpp.
#include "PopupKit.hpp"
#include "Settings.hpp"
#include "Stats.hpp"

#include <map>
#include <optional>
#include <set>

namespace tg::ui {

// Page order.
enum Tab { TabRuns, TabLevels, TabStats, TabTelegram, TabMessages, TabExtra, kTabCount };

class SettingsPopup : public PopupKit {
    tg::LevelRef m_levelRef;
    PlayLayer* m_playLayer = nullptr;
    std::vector<Choice> m_streakModes;
    tg::stats::Config m_stats, m_originalStats;
    std::vector<Choice> m_attemptSources;
    tg::LevelCfg m_level, m_originalLevel;
    tg::GlobalCfg m_global, m_originalGlobal;
    // Other levels with posting on (key -> name) and their checkboxes.
    // Unchecked ones are switched off when Apply is pressed.
    tg::EnabledLevels m_otherLevels;
    std::map<std::string, bool> m_levelFlags, m_originalLevelFlags;
    std::vector<tg::RunEntry> m_entries;
    std::vector<Page> m_pages;
    std::vector<Choice> m_tabs;
    CCNode* m_startPanel = nullptr;
    CCNode* m_stepPanel = nullptr;
    TextInput* m_token = nullptr;
    CCLabelBMFont* m_connection = nullptr;
    CCLabelBMFont* m_dirty = nullptr;
    CCMenuItemSpriteExtra* m_apply = nullptr;
    std::optional<Choice> m_startButton;
    std::optional<Choice> m_stepButton;
    bool m_confirming = false;
    int m_activeTab = 0;

    // ---------- state (SettingsPopup.cpp) ----------
    bool dirty() const;
    void changed() override;
    float runsHeight() const;
    void layoutRuns();
    void selectMode(tg::RunMode mode);
    bool apply();
    bool init(PlayLayer* layer);
    void selectTab(int index);
    void closeNow();

    // ---------- pages (SettingsPages.cpp) ----------
    void buildRuns(Page& page, PlayLayer* layer);
    void buildLevels(Page& page);
    void buildTelegram(Page& page);
    void preview(Page& page, std::string& message, std::string const& fallback, bool run);
    void placeholders(Page& page);
    void buildMessages(Page& page);
    void buildStats(Page& page);
    void buildStreaks(Page& page);
    void buildExtra(Page& page);

public:
    void onTab(CCObject* sender);
    void onStartMode(CCObject*);
    void onStepMode(CCObject*);
    void onClearToken(CCObject*);
    void onStreakMode(CCObject* sender);
    void onAttemptSource(CCObject* sender);
    void onAttempts(CCObject*);
    void onCheckUpdates(CCObject*);
    void onApply(CCObject*);
    void onClose(CCObject* sender) override;
    static SettingsPopup* create(PlayLayer* layer);
};

}
