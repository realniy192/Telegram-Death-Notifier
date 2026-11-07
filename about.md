# Telegram Death Notifier

Sends a screenshot to your **Telegram channel** when you die, get a startpos run or beat a level. Like Discord death bots, but for Telegram.

## Features
- Death screenshots with your percent
- Startpos runs (e.g. 71-100%) with separate settings for every startpos
- Level complete screenshot on the end screen
- Noclip support: the first noclip death counts as a death
- Custom messages and nickname
- Per-level settings right from the pause menu

## Setup
1. Create a bot with @BotFather and copy its token
2. Add the bot to your channel as an admin
3. Paste the bot token and channel (@name or -100...) in the mod settings
4. Pause in a level and press the Telegram button to enable the mod for that level

## Message placeholders
- {player} - your nickname
- {percent} - death percent
- {start} / {end} - run start and end percent
- {level} - level name

Practice mode is supported. Placing checkpoints does not change the run start; respawning on one does.

The optional Minimum run progress filter compares end minus start, independently of the old endpoint filter. It defaults to disabled with a threshold of 0. Both filters respect the level master switch; turn off the old rows for step-only notifications.

Show decimals is off by default: calculations and messages use whole percentages. Enable it to calculate and display up to two decimal places. Noclip runs end on their first collision; restart or respawn to begin a new run.