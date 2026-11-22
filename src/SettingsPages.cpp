#include "SettingsPopupImpl.hpp"
#include "DeathTracker.hpp"

#include <Geode/Geode.hpp>
#include <Geode/ui/TextArea.hpp>

using namespace geode::prelude;

namespace tg::ui {

namespace {
constexpr char kTokenChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789:_-";
constexpr char kChatChars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789@_-";
constexpr char kMessageChars[] = " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789{}%!?.,:;'()[]-_+=*/@#&~^|<>";
}

void SettingsPopup::buildRuns(Page& page, PlayLayer* layer) {
    section(page, std::string(layer->m_level->m_levelName), "This level only");
    toggle(page.content, "Enabled for this level", m_level.enabled, nextRow(page, 32.f));
    note(page, "*Off by default.*  Nothing is posted until you turn a level on here.", 16.f);
    button(page.content, "Attempts & linked copies", {m_width / 2, nextRow(page, 30.f)},
           menu_selector(SettingsPopup::onAttempts), 170.f, 22.f);
    spacer(page, 4.f);

    float y = nextRow(page, 34.f);
    auto modes = PageMenu::create();
    modes->setPosition({0, 0});
    page.content->addChild(modes);
    // Compact pair centered in the page, not stretched edge to edge.
    float gap = 10.f;
    float choiceWidth = std::min(150.f, (m_width - 8.f - gap) / 2);
    float center = m_width / 2;
    m_startButton = choiceButton(modes, "Death % per start pos", {center - (choiceWidth + gap) / 2, y},
        choiceWidth, 24.f, menu_selector(SettingsPopup::onStartMode));
    m_stepButton = choiceButton(modes, "Run length", {center + (choiceWidth + gap) / 2, y},
        choiceWidth, 24.f, menu_selector(SettingsPopup::onStepMode));
    spacer(page, 6.f);

    float top = page.cursor;
    m_startPanel = CCNode::create();
    m_stepPanel = CCNode::create();
    page.content->addChild(m_startPanel);
    page.content->addChild(m_stepPanel);

    // Death % per start position: one card per start position.
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
        header("Post from %", m_width - 36.f, false);
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
        auto item = toggler(cfg.enabled);
        item->setPosition({m_width * .56f, rowY});
        menu->addChild(item);
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
        m_numbers.push_back({input, &cfg.minPercent, 0, 100, entry.label + " post from %"});
    }
    if (!m_entries.empty()) {
        infoBox(m_startPanel, top - 24.f - m_entries.size() * 32.f, {
            "Posts when you die *at or after* this %, counted from 0% of the level.",
            "Example:  SP [14%] with [56]  >>  [14-56%] posts, [14-50%] does not.",
        });
    }

    // Run length: one threshold plus a short, plain explanation.
    numberField(m_stepPanel, "Minimum run length (%)", m_level.minRunProgress, top - 16.f);
    float boxTop = top - 37.f;
    boxTop -= infoBox(m_stepPanel, boxTop, {
        "Run length = *death %* minus *start %*.",
        "Example:  [14 - 69%]  >>  [55% run], so [56] does not post it.",
        "Want a minimum *death %* instead? Use *Death % per start pos*.",
    }) + 6.f;
    infoBox(m_stepPanel, boxTop, {
        "*Noclip:*  every death starts a new run.",
        "*Practice:*  the start moves only after a respawn.",
        "[0%]  =  every run is sent.",
    }, false, kMutedColor, .48f, 15.f);
    m_startPanel->setVisible(m_level.mode == tg::RunMode::StartPositions);
    m_stepPanel->setVisible(m_level.mode == tg::RunMode::MinimumProgress);
}

void SettingsPopup::buildLevels(Page& page) {
    section(page, "Enabled levels", "Posting is on only here");
    // Same value as the Runs tab checkbox; the two stay in sync.
    toggle(page.content, "This level: " + m_levelRef.name, m_level.enabled, nextRow(page, 32.f));
    if (m_otherLevels.empty()) {
        note(page, "No other levels have posting turned on.", 16.f);
    }
    for (auto const& [key, name] : m_otherLevels) {
        toggle(page.content, name.empty() ? key : name, m_levelFlags[key], nextRow(page, 32.f));
    }
    spacer(page, 4.f);
    note(page, "Uncheck a level to stop posting there. Saved when you press *Apply*.", 16.f);
    note(page, "To add a level, open it and turn it on in its *Runs* tab.", 16.f);
}

void SettingsPopup::buildTelegram(Page& page) {
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
    note(page, "If a post fails, an *error notification* shows the reason in game.", 16.f);
    note(page, "*Clear* won't delete the saved token until you press *Apply*.", 16.f);
}

void SettingsPopup::preview(Page& page, std::string& message, std::string const& fallback, bool run) {
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
            {"percent", tg::formatPercent(tg::percentUnits(54.32, digits), digits)},
            {"attempts", "1234"}, {"total_attempts", "1530"}, {"count", "3"},
            {"run", "58-67"}, {"runs", "54-70, 54-73, 54-83"},
            {"n", "3"}, {"chain", "14%, 13-45%, 45-100%"}
        });
        node->setText("Preview: " + value);
    });
}

// Placeholder cheat sheet: a 3-column grid of chips, each with its meaning.
void SettingsPopup::placeholders(Page& page) {
    struct Item { char const* tag; char const* meaning; };
    Item const items[] = {
        {"{player}", "Your Name"}, {"{level}", "Level Name"}, {"{percent}", "Run %"},
        {"{start}", "Run Start %"}, {"{end}", "Run End %"}, {"{attempts}", "GD attempts"},
        {"{total_attempts}", "All attempts"}, {"{count}", "Streak length"},
        {"{run}", "Streak run"}, {"{runs}", "Streak runs"}, {"{n}", "Runs needed"},
        {"{chain}", "Run chain"},
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

void SettingsPopup::buildMessages(Page& page) {
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
    buildStreaks(page);
    spacer(page, 4.f);
    section(page, "Nickname");
    toggle(page.content, "Use custom nickname", m_global.customNickname, nextRow(page, 32.f));
    auto nickname = textField(page, "Nickname", m_global.nickname,
        " abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.");
    m_refresh.push_back([this, nickname] { nickname->setEnabled(m_global.customNickname); });
    note(page, "Previews are local - nothing is sent.");
}

void SettingsPopup::buildStats(Page& page) {
    section(page, "Stats", "Shared by all levels");
    toggle(page.content, "Attach to level complete (100%)", m_stats.onComplete, nextRow(page, 32.f));
    toggle(page.content, "Attach to runs", m_stats.onRuns, nextRow(page, 32.f));
    if (tg::dt::available()) {
        note(page, fmt::format("*Death Tracker {}* found: deaths, runs and playtime come from it.",
            tg::dt::versionString()), 16.f);
    }
    else note(page, "*Death Tracker* is not installed: the mod's own journal is used.", 16.f);
    spacer(page, 4.f);

    section(page, "Metrics", "Each one can be turned off");
    toggle(page.content, "Deaths after List %", m_stats.listDeaths, nextRow(page, 32.f));
    numberField(page.content, "List % (1-100)", m_stats.listPercent, nextRow(page, 34.f), 1, 100);
    auto list = m_numbers.back().input;
    toggle(page.content, "Deaths after Custom %", m_stats.customDeaths, nextRow(page, 32.f));
    numberField(page.content, "Custom % (1-100)", m_stats.customPercent, nextRow(page, 34.f), 1, 100);
    auto custom = m_numbers.back().input;
    toggle(page.content, "Attempts", m_stats.attempts, nextRow(page, 32.f));
    {
        float y = nextRow(page, 32.f);
        auto menu = pageMenu(page);
        char const* names[] = {"From GD", "From Death Tracker"};
        float gap = 8.f, width = std::min(150.f, (m_width - 16.f - gap) / 2);
        for (int i = 0; i < 2; ++i) {
            auto choice = choiceButton(menu, names[i], {m_width / 2 + (i - .5f) * (width + gap), y},
                width, 22.f, menu_selector(SettingsPopup::onAttemptSource));
            choice.button->setTag(i);
            m_attemptSources.push_back(choice);
        }
    }
    note(page, "*From GD* includes linked copies and lost attempts.", 16.f);
    toggle(page.content, "Playtime", m_stats.playtime, nextRow(page, 32.f));
    toggle(page.content, "Worst fail", m_stats.worstFail, nextRow(page, 32.f));
    toggle(page.content, "Best runs to 100%", m_stats.bestRuns, nextRow(page, 32.f));
    numberField(page.content, "Runs to show (1-10)", m_stats.bestRunsCount, nextRow(page, 34.f), 1, 10);
    auto bestCount = m_numbers.back().input;
    numberField(page.content, "Minimum run length %", m_stats.bestRunsMinLength, nextRow(page, 34.f), 0, 100);
    auto bestLength = m_numbers.back().input;
    note(page, "Hides short runs: with [20], *90-100* is not listed.", 16.f);
    toggle(page.content, "Ask Enjoyment on 100%", m_stats.askEnjoyment, nextRow(page, 32.f));
    toggle(page.content, "Ask Experience on 100%", m_stats.askExperience, nextRow(page, 32.f));
    note(page, "A small panel on the complete screen; the post waits for it.", 16.f);
    spacer(page, 4.f);

    section(page, "Level in N runs");
    toggle(page.content, "Post when fewer runs are needed", m_stats.nRuns, nextRow(page, 32.f));
    textField(page, "Message", m_stats.nRunsMessage, kMessageChars);
    preview(page, m_stats.nRunsMessage, "{player} can now beat {level} in {n} runs: {chain}", false);
    note(page, "Overlapping runs from [0%] to [100%]. Never posted for a full run.", 16.f);
    // Session summary is hidden until it is finished (code kept in Stats.cpp).

    m_refresh.push_back([this, list, custom, bestCount, bestLength] {
        list->setEnabled(m_stats.listDeaths);
        custom->setEnabled(m_stats.customDeaths);
        bestCount->setEnabled(m_stats.bestRuns);
        bestLength->setEnabled(m_stats.bestRuns);
        for (int i = 0; i < (int)m_attemptSources.size(); ++i) {
            setSelected(m_attemptSources[i], m_stats.attemptsFromDT == (i == 1));
            m_attemptSources[i].button->setEnabled(m_stats.attempts && (i == 0 || tg::dt::available()));
        }
    });
}

void SettingsPopup::buildStreaks(Page& page) {
    section(page, "Streaks", "Progress in a row");
    toggle(page.content, "Post streaks", m_global.streakEnabled, nextRow(page, 32.f));
    numberField(page.content, "Post from this many in a row (2+)", m_global.streakMin,
                nextRow(page, 34.f), tg::kStreakMinFloor, tg::kStreakMinCeiling);
    auto minimum = m_numbers.back().input;
    note(page, "Every new progress after that posts again. A plain death breaks the streak.");
    float y = nextRow(page, 32.f);
    auto menu = pageMenu(page);
    char const* names[] = {"Separate", "Replace", "Combine"};
    float gap = 8.f;
    float width = std::min(110.f, (m_width - 16.f - gap * 2) / 3);
    for (int i = 0; i < 3; ++i) {
        auto choice = choiceButton(menu, names[i], {m_width / 2 + (i - 1) * (width + gap), y},
            width, 22.f, menu_selector(SettingsPopup::onStreakMode));
        choice.button->setTag(i);
        m_streakModes.push_back(choice);
    }
    m_refresh.push_back([this, minimum] {
        minimum->setEnabled(m_global.streakEnabled);
        for (int i = 0; i < (int)m_streakModes.size(); ++i) {
            setSelected(m_streakModes[i], m_global.streakMode == i);
            m_streakModes[i].button->setEnabled(m_global.streakEnabled);
        }
    });
    note(page, "*Separate:* run photo + streak text   *Replace:* streak only   *Combine:* one caption", 16.f);
    textField(page, "Same run in a row", m_global.streakSameMessage, kMessageChars);
    preview(page, m_global.streakSameMessage, "{player} did {run}% on {level} {count}x in a row", true);
    textField(page, "Different runs in a row", m_global.streakMixedMessage, kMessageChars);
    preview(page, m_global.streakMixedMessage, "{player} got {runs} on {level} back to back", true);
}

void SettingsPopup::buildExtra(Page& page) {
    section(page, "Precision", "Shared by all levels");
    toggle(page.content, "Show decimals", m_global.showDecimals, nextRow(page, 32.f));
    numberField(page.content, "Decimal places (1-4)", m_global.decimalPlaces,
                nextRow(page, 34.f), 1, 4);
    auto digits = m_numbers.back().input;
    m_refresh.push_back([this, digits] { digits->setEnabled(m_global.showDecimals); });
    note(page, "*Off:*  [54%]      *On:*  [54.32%]");
    toggle(page.content, "Only new bests", m_global.onlyBest, nextRow(page, 32.f));
    note(page, "Only a new best from [0%] is sent. Works only in *Death % per start pos* mode.");
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
    section(page, "Updates", "From GitHub releases");
    note(page, fmt::format("Installed version:  *{}*", Mod::get()->getVersion().toVString()), 16.f);
    button(page.content, "Check for updates", {m_width / 2, nextRow(page, 32.f)},
           menu_selector(SettingsPopup::onCheckUpdates), 150.f, 22.f);
    note(page, "Install downloads the new .geode file, then asks you to restart.", 16.f);
}

}
