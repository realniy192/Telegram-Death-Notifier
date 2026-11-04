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

Practice mode deaths are never sent.