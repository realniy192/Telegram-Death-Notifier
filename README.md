# Telegram Death Notifier

<img src="logo.png" width="150" alt="Telegram Death Notifier logo" />

A Geode mod for Geometry Dash that sends your progress to a **Telegram channel**: death screenshots, startpos runs and level completions. Like Discord death bots, but for Telegram.

## Features
- Death screenshots with your percent
- Startpos runs (e.g. `71-100%`) with separate legacy settings for every startpos
- Level complete screenshot on the end screen
- Per-level master switch and legacy endpoint filter in the pause menu
- Independent minimum run progress: `end - start >= threshold`
- Practice support: placing/removing checkpoints does not interrupt a run; actually respawning on one starts a new segment
- Noclip support: only the first collision ends a run; later collisions and the subsequent 100% cannot send again until a restart/respawn
- Whole percentages by default; optional hundredths for both filtering and messages
- Custom messages and nickname

## Setup
1. Create a bot with [@BotFather](https://t.me/BotFather) and copy its token
2. Add the bot to your channel as an **admin**
3. Open the mod settings and paste the bot token and channel (`@name` or `-100...`)
4. In a level, pause and press the Telegram button to enable the mod for that level

## Filters and precision
The per-level **Enabled** switch is the master switch for both filters.

- **Old / End %** in the level popup preserves the previous filter and saved settings: compare the death/end percentage with the configured endpoint. **Only new bests** applies only to this filter, for normal-mode deaths from 0%; practice is not compared with the normal-mode best.
- **Enable minimum run progress** in the gear settings is independent, globally configured and **off by default**. **Minimum run progress (%)** defaults to **0**, meaning no minimum distance restriction when enabled. For example, a threshold of 40 accepts `2-42%` and `10-55%`.
- If either filter qualifies, one notification is sent, not two. For **step-only** notifications, keep the level enabled, turn off the old rows and enable minimum run progress in the gear settings.
- **Show decimals** defaults to **off**: truncate both endpoints to whole percentages before subtracting and display integers. When enabled, round both endpoints to hundredths before subtracting and display up to two decimal places. Thus `2.9-42.1%` measures 40 in integer mode but 39.2 in decimal mode. The raw spawn percentage is retained even if the precision setting changes mid-attempt.

In practice, a clean run starting at 0 remains a `0-end%` run even if checkpoints are placed along the way. Only restoration through `loadFromCheckpoint` changes its start. After respawning at 4%, reaching 54% gives a 50% segment. Practice and checkpoint results use the run template rather than claiming a normal-mode level completion.

The first real death or noclip collision consumes the attempt even if no filter qualifies. A clean run to 100% also checks the filters. This relies on the noclip mod calling the hooked `PlayLayer::destroyPlayer`; mods that bypass this function cannot expose their collisions to this notifier.

## Message placeholders
| Placeholder | Meaning |
|---|---|
| `{player}` | Your nickname |
| `{percent}` | Death/end percent |
| `{start}` / `{end}` | Run start and end percent |
| `{level}` | Level name |

## Screenshot implementation
`src/Screenshot.cpp` owns capture, PNG encoding and Telegram delivery. Android captures to an isolated render texture after the scene draw, uses the physical drawable dimensions, resets the ambient projection/modelview for capture and restores rendering state afterward. Scene node/camera transforms that are part of the level are not removed. Other platforms capture the finished frame before buffer swap. Readback dimensions, RGBA row layout and GL errors are checked; invalid captures are dropped with a log entry instead of sending corrupted pixels.

Capture/encoding is limited to one frame at a time. Uploading is a separate bounded queue (up to four PNG batches / 24 MiB); a slow upload does not block the next capture. Overload drops are logged rather than attaching an old caption to a later frame. Android scenes are visited again for the screenshot, so levels or mods with custom draw side effects/overlays require in-game testing. Unsupported custom director projections, incompatible framebuffer state or oversized images are rejected rather than sent as broken captures.

## Download
Download published `.geode` packages from [Releases](../../releases). Source targets Geode **5.11.2** and GD **2.2081**; see the verification notes below before treating Android support as device-tested.

## Building
```sh
geode build
```
On Windows, build with Clang if MSVC crashes:
```sh
geode build -- -T ClangCL
```
For a fresh Windows build without installing into the game:
```sh
cmake -S . -B build-verify -G "Visual Studio 17 2022" -A x64 -T ClangCL -DGEODE_DONT_INSTALL_MODS=ON
cmake --build build-verify --config Release
```
Set `GEODE_SDK` to the SDK path. The installed SDK requires CMake 3.25 or newer. Do not reuse a CMake cache copied from a different source directory.

Standalone logic tests do not require Geode:
```sh
cmake -S tests -B build-tests
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

## Manual verification
Automated logic tests do not simulate Geometry Dash, OpenGL drivers or other mods. Check these in-game before publishing:
1. With the level disabled, neither filter sends. With only the step filter enabled at 40, verify 2-42 succeeds and 10-49 does not. With both filters enabled and passing, verify exactly one message.
2. In practice, place checkpoints without dying and confirm the start remains 0. Die, respawn at a checkpoint and verify the next caption/threshold uses that restored percent. Try checkpoint removal and a full restart too.
3. With noclip, hit an obstacle once, then more obstacles and finish: at most the first collision sends. Restart and verify notifications are re-armed. Test dual-player collisions as well.
4. Toggle Show decimals and check the documented 2.9-42.1 boundary. Verify a clean 60-100 completion passes 40, while 61-100 does not in step-only mode.
5. On Android32/64 devices, compare the screenshot with the screen at landscape/native resolutions, including non-power-of-two sizes and camera/shader levels. Check for skew, crop, vertical inversion and changed rendering after capture. Test practice and the completion overlay.
6. Test Telegram delivery only with a channel/bot you control. No real Telegram messages are required for the standalone tests.

## Credits
Made by m3reana with AI assistance (sry...)
