# Application shell, platform bridge and ancillary tools

Baseline: `ff6a3b1fb1c54e4c0344613094ea65292b0023c2`. Verified from source except stated gaps.
The Updates row and the update-check notes were rechecked against the source at `de6c3bd3`; the rest of the page
stands at the baseline.

## Purpose and responsibilities

The portable `sfe::App` coordinates source selection, debugger models, panels and export requests.
Platform implementations own OS interaction and GPU/audio resources. This page covers the shell,
settings, demo and sound tools; specialized systems own their own algorithms.

## Source locations, types and entry points

| Concern | Source and symbols |
|---|---|
| App orchestration | [App.h](../../../FrontEnd/src/App.h), [App.cpp](../../../FrontEnd/src/App.cpp): `App::Initialize`, `BuildUI`, `Shutdown`, `CloseData` |
| Platform contract | [IPlatform.h](../../../FrontEnd/src/Platform/IPlatform.h): `IPlatform`, `PlatformConfig`, `SaveOutcome`, `TextureHandle` |
| Windows entry/backend | [WinMain.cpp](../../../FrontEnd/Platforms/Windows/WinMain.cpp): `wWinMain`; [WindowsPlatform.cpp](../../../FrontEnd/Platforms/Windows/WindowsPlatform.cpp): `WindowsPlatform` |
| SDL desktop and browser | [WebMain.cpp](../../../FrontEnd/Platforms/Web/WebMain.cpp): `main`, `se_web_load_file`; [WebPlatform.cpp](../../../FrontEnd/Platforms/Web/WebPlatform.cpp): `WebPlatform` (also used natively) |
| Commands/menus | [TopBar.h](../../../FrontEnd/src/TopBar.h): `TopBarCommand`, `TopBarViewModel`; [TopBar.cpp](../../../FrontEnd/src/TopBar.cpp): `TopBarCommandEnabled`; [NativeMenu.h](../../../FrontEnd/src/NativeMenu.h) |
| Settings | [Settings.cpp](../../../FrontEnd/src/Settings.cpp): `Settings::Load`, `SaveTo`, `ConfigDir`, `ConfigSubPath` |
| Common widgets | [PanelWidgets.h](../../../FrontEnd/src/PanelWidgets.h): `RowSelectable`, `HorizontalSplitter`, `ComboWidth` |
| Sound previews | [ScspMix.cpp](../../../FrontEnd/src/ScspMix.cpp): `ScspMixVoices`, `ScspResampleMono`, `ScspAttenuate`; App's `DecodeSlotSample`, `PlaySound`, `PlaySoundFrame`, `ExportSound`, `DrawSound` |
| Feature tour | [DemoPlayer.cpp](../../../FrontEnd/src/Demo/DemoPlayer.cpp): `DemoPlayer::Tick`, `ConsumeDirty`; [DemoScript.cpp](../../../FrontEnd/src/Demo/DemoScript.cpp): `DemoParseText`; `App::UpdateDemo`, `ApplyDemoBeat` |
| Updates | [UpdateChecker.cpp](../../../FrontEnd/src/UpdateChecker.cpp): `UpdateChecker::Start`, `Run`, `EvaluateLatestRelease`, `EvaluateSourceBuild`, `ExtractReleaseTag`, `CompareReleaseTags`; `App::DrawUpdateModal` |

## Execution flow and interactions

The platform initializes ImGui before `App::Initialize`. App sets render defaults, recorder edit
sink, layout-file path, preferences, watch session, assembly comments and function names/bounds.
Desktop builds start the optional decompiler runner. Failure to start it is logged and the rest
of the application remains available.

`wWinMain` drives `PumpEvents → BeginFrame → App::BuildUI → EndFrame`. `WebMain.cpp` supplies
both the native SDL loop and Emscripten loop; its browser file callback sends bytes to
`App::OpenSavestateBuffer`. Windows startup reads the optional live endpoint and enables
live auto-connect; do not assume its argument handling matches the SDL entry point.

`BuildUI` polls workers, refreshes eligible live captures, reconciles emulator identity and stops,
records eligible frames, then collects toolbar/native-menu/shortcut commands. It rechecks
`TopBarCommandEnabled` before each command because earlier commands can invalidate the same
batch. `ExecuteTopBarCommand` owns command side effects. `DispatchNativeMenuAction` translates
native selections; modal dialogs suppress native menu dispatch.

After transport commands, App selects a recorded context when scrubbing. A scoped context swap
makes data panels and exports see that displayed frame, then restores the live context on return.
Consequently screenshot/dump operations are queued until after context selection.
`WarnIfNotFromDisplayedSnapshot` detects exports incorrectly invoked during command dispatch.
See [memory/history](MemoryAndHistory.md) for capture, restore and edit rules.

The Sound panel obtains SCSP slot state and PCM through the host ABI (`se_get_scsp_slots`,
`se_decode_scsp_sample`). Per-voice playback resamples rates outside the audio device bounds;
export retains the natural rate. `PlaySoundFrame` decodes active voices, binds sample pointers
only after collection, calls `ScspMixVoices`, then `IPlatform::PlayAudio`. The static preview
freezes envelope/pan and omits DSP; it is not the emulator's evolving output. `DrawSoundCpu`
disassembles sound RAM through the 68000 decoder.

## State and lifetime

App stores panel visibility, selections, models and core/source handles. `CloseData` tears down
the context and clears `mDataSource` (its function pointers otherwise reference destroyed state),
invalidation keys, selection and per-session debug state. `Shutdown` stops the data search,
resets/joins the decompiler runner, persists settings/watch/comments/names, then closes data.
Entry points call platform shutdown afterwards.

Settings and ImGui layout are separate files in the per-user config directory. Settings uses
case-insensitive section/key lookup. `Settings::SaveTo` stages and replaces the file; App keeps
failed preference saves pending for retry. App's `mIniPath` must outlive ImGui's borrowed pointer.

Platform owns GPU uploads/draws and audio output; the core supplies CPU images/geometry.
`IPlatform::HttpsGet` is blocking and must run off the UI thread. `UpdateChecker` protects its
shared result with a mutex; its worker uses shared state and can detach at destruction. The
interface also requires the platform to outlive a check: this documentation does not establish
that every shutdown ordering satisfies that lifetime contract.

## Extension points and constraints

- Add platform-independent behavior through App/panel models. Extend `IPlatform` when actual
  OS capability is required; use its unsupported defaults deliberately on browser/headless builds.
- For a command exposed in multiple UI surfaces, use `TopBarCommand`, centralized enablement,
  native-menu translation and command execution. Update settings/default layout if adding a panel.
- Reuse `Settings::ConfigSubPath` for persistent locations; `FileWrite` for checked writes;
  `SaveOutcome` distinguishes cancel from failure. Route exports through `App::ReportSave`.
- Launch emulators via `LaunchEmulator`/`StopEmulator` and auxiliary tools via `LaunchTool`.
  These have different process ownership; use argument vectors, not assembled shell strings.
- Reuse shared widget helpers, especially `RowSelectable` for editable table rows.
- Add a demo operation to the parser/model and App's beat application together.

## Testing

See [CMakeLists.txt](../../../CMakeLists.txt) for platform guards and registered test names.
From `SaturnExplorer/`: `cmake -S . -B build`, `cmake --build build`,
`ctest --test-dir build --output-on-failure -R 'TopBar|Settings|PanelInteraction|ControllerPanel|ScspMix|DemoScript|FileWrite|LaunchPath|PosixSpawn'`.
Tests live in [FrontEnd/tests](../../../FrontEnd/tests), including `ImGuiHarness.h` for headless
interaction. They exercise policy/models/widgets rather than the entire `App::BuildUI` loop.
Windows/macOS menu, dialogs, GPU/audio and packaged app behavior still require native checks.
UpdateChecker's full network/shutdown behavior was not dynamically verified here.

## Related documentation

[Architecture](../ARCHITECTURE.md), [data flow](../DATA_FLOW.md), [build](../../BUILD.md),
[existing architecture](../../ARCHITECTURE.md), [demo scripts](../../../FrontEnd/demo/README.md),
[debugger](Debugger.md), [graphics](Graphics.md), [state sources](StateSources.md).

## Appearance (inspected on feature/light-theme, 2026-10-24)

`Theme.h/.cpp` owns the shared Dark/Light palettes and base geometry. App owns the requested
`ThemeMode`, loads `[appearance] theme=light|dark` before the first frame, and saves it through
the existing retryable Settings path. Missing/invalid values choose Dark. Both ImGui Settings
entry points and native Windows/macOS menus expose Appearance → Light/Dark through the same
native action handler. Menu requests apply colors at the next BuildUI entry, before style stacks
are pushed. `ApplyThemeColors` preserves scaled geometry and fonts; the Windows DPI reset passes
the active palette back to `ApplyTheme` before scaling once. Custom panel colors resolve per draw
against an explicit applied-mode value (the application owns one ImGui context). Temporary
WindowBg overrides cannot change the resolved mode. Game textures and decoded CRAM colors remain unchanged.
macOS updates NSApp appearance; the OS still controls system-wide menu-bar appearance. Windows
native chrome uses its existing system appearance. Browser preferences use the existing Settings
filesystem path; persistence across browser reloads is not established by this implementation.

`SaturnExplorerThemeTests` covers parsing/fallback, repeated palette switches, stable scaled
geometry, DPI resets and preserved graphics category/selection colors. Native menu/chrome visual
verification still requires the corresponding OS build.

Review follow-up: shared 1px base frame borders in both modes keep geometry stable while making
light inputs visible. ImGui scales that border on DPI changes. Light borders and muted text meet
3:1 control-outline and 4.5:1 header-text contrast checks. Log/status/marker palettes and both
checkerboard paths resolve light colors per draw. Theme parsing accepts mixed case. The renderer
clear color preserves the pre-theme Dark value; Light uses the shared app background.

`ThemePreference.h` factors App's preference state into a headless-testable component:
Load/Store map to Settings, Request accepts the two native theme actions without applying colors,
and ApplyPending updates the palette at startup or the next BuildUI boundary. ThemeTests covers
these transitions with a Settings object, including persisting a request while the previous
palette is still active. Disk I/O remains covered separately by SettingsTests.
