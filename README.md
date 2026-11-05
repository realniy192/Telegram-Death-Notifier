# Telegram Death Notifier

<img src="logo.png" width="150" alt="Telegram Death Notifier logo" />

A Geode mod for Geometry Dash that sends your progress to a **Telegram channel**: death screenshots, startpos runs and level completions. Like Discord death bots, but for Telegram.

## Features
- Death screenshots with your percent
- Startpos runs (e.g. `71-100%`) with separate settings for every startpos
- Level complete screenshot on the end screen
- Per-level settings right from the pause menu: enable/disable and minimum percent
- Noclip support: only the first noclip death counts
- Practice mode deaths are never sent
- Custom messages and nickname

## Setup
1. Create a bot with [@BotFather](https://t.me/BotFather) and copy its token
2. Add the bot to your channel as an **admin**
3. Open the mod settings and paste the bot token and channel (`@name` or `-100...`)
4. In a level, pause and press the Telegram button to enable the mod for that level

## Message placeholders
| Placeholder | Meaning |
|---|---|
| `{player}` | Your nickname |
| `{percent}` | Death percent |
| `{start}` / `{end}` | Run start and end percent |
| `{level}` | Level name |

## Download
Download the latest `.geode` from [Releases](../../releases). One file works on both Windows and Android.

## Building
```sh
geode build
```
On Windows, if MSVC crashes while compiling, build with Clang:
```sh
geode build -- -T ClangCL
```
## Credits
Made by m3reana with AI assistance (sry...)