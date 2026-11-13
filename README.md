# Telegram Death Notifier

<img src="logo.png" width="150" alt="Telegram Death Notifier logo" />

A Geode mod for Geometry Dash that sends death screenshots, clean run segments and level completions to a **Telegram channel**.

## Setup
1. Create a bot with [@BotFather](https://t.me/BotFather) and copy its token.
2. Add the bot to your channel as an **admin**.
3. Pause a level, press the Telegram button and open the **Telegram** tab.
4. Enter the bot token and channel (`@name` or `-100...`), then press **Apply**.
5. In **Runs**, enable the current level and choose a notification mode.

## Four tabs
### Runs — current level
**Enabled for this level** is the master switch. Choose exactly one mode:

- **Start positions:** configure `From 0%` and each startpos separately. **Notify** enables its row; **Min. end %** is the minimum final/death percentage. Only the first real death or noclip collision counts until a restart/respawn.
- **Minimum progress:** send when `end - segment start >= threshold`. The threshold is 0–100; 0 adds no distance restriction. It does not depend on the startpos rows or Only new bests.

Mode and minimum progress are saved **per level**. Switching modes preserves the other mode's settings. The filters no longer combine with OR: a startpos row cannot bypass the minimum progress threshold.

### Telegram — global
- Bot token is always displayed as password dots, including while editing.
- **Clear** empties the token field; the removal is committed only after **Apply**.
- Channel/chat ID and setup instructions.
- **Token missing**, **Chat missing**, or **Fields filled (not verified)** describes field completeness only, not a verified Telegram connection. No automatic test request/message is sent.

The token is stored locally as before. Masking protects its display; it is **not encryption** of the settings file.

### Messages — global
Edit the death, run and completion templates, custom-nickname toggle and nickname. Local previews use example data and never send anything to Telegram. An empty template uses the built-in fallback.

| Placeholder | Meaning |
|---|---|
| `{player}` | Nickname |
| `{percent}` | End/death percentage |
| `{start}` / `{end}` | Run endpoints |
| `{level}` | Level name |

### Extra — global
- **Show decimals** is **off by default**: truncate both endpoints to whole percentages before subtraction and display integers.
- **Decimal places** chooses **1–4 digits**, default **2**. When decimals are enabled, round both endpoints to that precision before subtraction and display exactly that many digits, including trailing zeros.
- **Only new bests** applies only to Start positions mode, normal-mode deaths from 0%; it does not restrict practice, checkpoint segments or minimum progress.
- Defaults for levels/rows without saved settings: enabled, mode, endpoint and minimum progress.

For example, `2.9 -> 42.1` measures 40 in integer mode, but 39.20 with two decimals. Raw spawn progress is retained, so changing precision does not replace it with an already-rounded value.

## Apply and standard Geode settings
Changing tabs retains the current draft. **Apply** commits it; closing with changes asks **Discard / Save**. Invalid numeric fields show an error rather than silently applying a different number.

All global options also remain on the mod's standard Geode settings page, grouped as **Runs / Telegram / Messages / Extra**. The token there is also masked and has a Clear button. Both interfaces use the same saved global values. Runs defaults on that page apply where no per-level choice is saved; configure a specific level in the pause-menu Runs tab.

Existing level/startpos keys, Telegram credentials, templates and nickname are retained. The prior global minimum-progress toggle/threshold now supply defaults for levels without their own mode/threshold.

## Practice and sequential noclip segments
Placing, passing or removing a checkpoint does **not** change the start of a clean run. Actually respawning restores the exact progress through `loadFromCheckpoint` and begins the next segment. An uninterrupted practice run remains from 0%; a checkpoint respawn at 4% followed by death at 54% is a 50% segment.

**Minimum progress mode** can report multiple clean segments in a single noclip attempt:

| Threshold 40 | Distance | Result |
|---|---:|---|
| 0 -> 42, first collision | 42 | Send |
| 42 -> 85, next collision | 43 | Send |
| 85 -> 100, completion | 15 | Do not send |

Every separate collision starts the next noclip segment, even if the previous segment was too short. Movement across collisions is not combined into one clean run. A real death waits for actual respawn before starting again. Repeated/dual callbacks during the same game step are deduplicated. Completion checks the last clean segment even after earlier segment notifications.

Start positions mode retains the previous first-collision behavior. Noclip support requires the other mod to call the hooked `PlayLayer::destroyPlayer`; a mod bypassing that function cannot expose its collisions to this notifier.

## Screenshot implementation
The working capture path is unchanged by the tab redesign. Android uses an isolated render texture, physical drawable dimensions, explicit ambient projection/modelview reset and restoration of rendering state. In-game scene/node camera transforms are retained. Other platforms read the finished framebuffer before swap. PNG encoding and sequential Telegram uploads remain bounded; overload or invalid captures are dropped with logs rather than paired with later frames.

Android revisits the running scene, so custom rendering side effects/overlays require device testing. The new code changes only the token accessor in this module, not its GL capture or upload queue.

## Building
Requires Geode **5.11.2**, GD **2.2081** and a matching compiler/toolchain. Set `GEODE_SDK`. The installed SDK requires CMake 3.25 or newer.

```sh
geode build
```
Windows with Clang, without installing into the game:
```sh
cmake -S . -B build-verify -G "Visual Studio 17 2022" -A x64 -T ClangCL -DGEODE_DONT_INSTALL_MODS=ON
cmake --build build-verify --config Release
```
Use a fresh cache if moving the source directory.

Standalone tests:
```sh
cmake -S tests -B build-tests
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

GitHub Actions builds Windows, Android32 and Android64 and produces the combined **Build Output** artifact. The combine step sets `zstd: false` for ordinary ZIP Deflate compatibility. A locally built Windows-only `.geode` **cannot be installed on Android**; obtain the freshly combined Actions package instead. Do not merge old Android binaries into a new Windows package.

## Manual acceptance checklist
Automated tests do not simulate the game's UI, hooks or third-party mods. Before publishing:
1. Open all four tabs on a phone. Check scrolling, text focus/keyboard, clipping, selection labels, Apply and unsaved-close confirmation. Input on another tab must be retained.
2. Confirm token dots while typing and after reopening, on both settings interfaces. Clear then Discard preserves the old token; Clear then Save removes it. Do not share screenshots/logs with actual credentials.
3. Enable one level in Start positions and another in Minimum progress. Reopen each and confirm independent modes/thresholds. Startpos rows must not bypass the step threshold.
4. With minimum40 and noclip, verify qualifying 0-42 and 42-85 both report, while 85-100 does not. Test short segments, real death and dual. Start positions should still report only the first collision.
5. In practice, place checkpoints without dying, then respawn on one. Verify the segment begins only at actual respawn. Test full restart/checkpoint removal.
6. Check integer and 1/2/3/4-place percentages against message previews and threshold boundaries. Check the last qualifying segment at 100%.
7. Compare Android screenshots against the displayed game, including camera/shader levels. Existing capture was reported working by the user; this does not cover all devices/mod combinations.
8. Perform real delivery checks only with a bot/channel you control.

See VERIFICATION.md for actual checks performed locally and their limitations.

## Credits
Made by m3reana with AI assistance (sry...)
