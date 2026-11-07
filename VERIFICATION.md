# Verification — 2026-11-07

## Passed
- Windows x64 Release build and `.geode` packaging with Geode SDK 5.11.2, GD 2.2081, ClangCL 19.1.5, Visual Studio 2022 BuildTools.
- Standalone C++ tests: **42 checks**, CTest 1/1 passed.
- `mod.json` parses successfully; new defaults are `minimum-run-progress: 0`, `run-progress-enabled: false`, `show-decimals: false`.
- `git diff --check`.
- Supplemental Android-branch **syntax-only shim** check: ClangCL `/Zs` with the module's Android conditionals selected, Windows Geode headers and aliases for `glClearDepthf`/`glDepthRangef`. This checks C++ syntax/API spelling, not GLES header availability or Android binary compatibility.

## Not verified
- No Android NDK was found in the available conventional locations. **No Android32/Android64 binary was built or linked.**
- No Geometry Dash device/runtime tests, screenshot visual comparison, real checkpoint hook execution, or interaction with third-party noclip/rendering mods.
- No actual Telegram requests. No installation into the game, commit, push or release publication.

## Build details
A fresh `build-verify` directory was used because the original build cache points at another source directory. The initial configure was stopped during a bindings clone; configuration was rerun using this checkout's existing `build/_deps/*-src` dependencies through `GEODE_BINDINGS_REPO_PATH` and `FETCHCONTENT_SOURCE_DIR_*` overrides. `GEODE_DONT_INSTALL_MODS=ON` prevented installation.

```sh
cmake --build build-verify --config Release
cmake -S tests -B build-tests -G "Visual Studio 17 2022" -A x64 -T ClangCL
cmake --build build-tests --config Release
ctest --test-dir build-tests -C Release --output-on-failure
```

The build has warnings from SDK/dependency headers, a pre-existing environment warning about `CMAKE_GENERATOR_TOOLSET`, and a Windows CRT deprecation warning for `sscanf` in the GL-version parser. None prevented compilation or packaging.

## Before release
Use the manual verification checklist in README.md. In particular, compare Android screenshots at native landscape resolution and verify checkpoint respawns in-game. A successful Windows build must not be interpreted as a verified fix on all Android devices.
