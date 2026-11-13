# Telegram Death Notifier

<img src="logo.png" width="150" alt="Telegram Death Notifier logo" />

Sends a screenshot to your Telegram channel when you die, get a good run or beat a level.

## Features
- Death screenshots straight to Telegram
- Run notifications from start positions, practice and noclip
- Level complete screenshots
- Per-level settings: by start position or by minimum run length
- Custom messages and nickname
- Optional decimal percentages

## Setup
1. Create a bot with [@BotFather](https://t.me/BotFather) and copy its token.
2. Add the bot to your channel as an admin.
3. Pause a level, press the Telegram button, open the **Telegram** tab, paste the token and channel (`@name` or `-100...`) and press **Apply**.
4. In the **Runs** tab, enable the level and pick a mode.

## Modes
- **Start positions** - pick which start positions notify and the % you need to reach.
- **Minimum progress** - notify when a run is long enough. Run length = death % - start %, so 4% -> 54% is a 50% run.

## Placeholders
`{player}` `{level}` `{percent}` `{start}` `{end}` - work in any message.
