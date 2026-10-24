# Saturn Explorer light theme implementation

## Target appearance

Implement the approved light-theme concept: off-white panel bodies, pale cool-gray tabs and headers, charcoal text, restrained teal accents, and a pale red paused banner. Keep the existing dense docking layout and controls. Game frames, sprite pixels, and color RAM values retain their original colors.

Reference mockup: [light-theme screenshot](Images/SaturnExplorerLightTheme.png).

This document records the implementation target. The `feature/light-theme` branch implements explicit Light/Dark selection; see the delivery notes below for verification limits. The generated screenshot is a visual direction, not a specification for changing labels, selected tabs, data, or panel geometry.

## Palette

Define named semantic tokens in `FrontEnd/src/Theme.h` and `Theme.cpp`, with values for both themes. Keep existing dark values as the default.

| Token | Light value | Usage |
| --- | --- | --- |
| App background | `#E9EDF2` | Dock gaps and renderer clear color |
| Panel background | `#F7F8FA` | Windows and panel bodies |
| Raised background | `#E9EDF2` | Tabs, headers, menu bars |
| Input background | `#FFFFFF` | Inputs and editable table cells |
| Hover background | `#DFE7EE` | Hovered controls |
| Border | `#CDD3DC` | Panel edges and separators |
| Primary text | `#242B36` | Labels and data |
| Secondary text | `#596579` | Help text, addresses, metadata |
| Disabled text | `#687386` | Disabled labels; verify against their surface |
| Accent | `#087F8C` | Checkmarks, focus rings, active tab line |
| Accent hover | `#066B76` | Hovered primary actions |
| Selection background | `#D1EEF0` | Selected table rows and tree items |
| Selected text | `#242B36` | Text on selected rows |
| Alternating row | `#EEF2F6` | Tables and memory rows |
| Error background | `#FCE2E2` | Paused/error banner |
| Error text | `#A51D24` | Paused reason and error labels |
| Success text | `#16753C` | Emulator connection status |
| Warning text | `#805B00` | Warnings on light surfaces |
| Checkerboard light / dark | `#FFFFFF` / `#D9DEE5` | Transparent sprite previews |

Use opaque backgrounds for selected rows so their appearance is predictable. Text selection can use the accent with about 25% alpha. Keep the amber selection outline over game imagery; do not substitute the teal UI selection color there. A thin dark outer outline can improve visibility over bright pixels.

## 1. Extend the shared theme API

The existing `ApplyTheme(ImGuiStyle&)` in `FrontEnd/src/Theme.cpp` calls `ImGui::StyleColorsDark()` and overrides colors and geometry. Both platform backends use it.

Introduce `enum class ThemeMode { Dark, Light };` and an overload such as `ApplyTheme(ImGuiStyle&, ThemeMode)`. Preserve the existing one-argument overload as a dark default for existing callers and tests. Keep the selected mode in application state; expose semantic color accessors that use that mode for custom drawing.

Separate palette application from geometry initialization:

- At startup or DPI changes, create a fresh base `ImGuiStyle`, apply shared geometry and the chosen palette, then scale geometry once using the current DPI factor.
- During a theme switch, update colors and semantic tokens only. Avoid applying base geometry to an already scaled style.
- Do not reload fonts or change `io.FontScaleMain` when switching colors.

Start the light palette with `ImGui::StyleColorsLight(&style)`, then explicitly override the relevant slots. Retain identical geometry in both themes.

| ImGui slots | Palette mapping |
| --- | --- |
| `Text`, `TextDisabled` | Primary text, disabled text |
| `WindowBg`, `ChildBg` | Panel background, transparent child background |
| `PopupBg` | Panel background, opaque |
| `Border`, `Separator` | Border |
| `BorderShadow` | Transparent |
| `FrameBg`, `FrameBgHovered`, `FrameBgActive` | Input, hover, selection |
| Title backgrounds, `MenuBarBg`, `TableHeaderBg` | Raised background |
| `Button`, `ButtonHovered`, `ButtonActive` | Raised, hover, selection |
| `Header`, `HeaderHovered`, `HeaderActive` | Selection and progressively stronger teal tints |
| `CheckMark`, slider grabs, `NavCursor` | Accent |
| Scrollbar background / grabs | Transparent / border, secondary text on hover |
| `Tab`, `TabDimmed` | Raised background |
| `TabHovered` | Hover background |
| `TabSelected`, `TabDimmedSelected` | Panel background |
| `TabSelectedOverline` | Accent |
| `DockingEmptyBg`, `DockingPreview` | App background, translucent accent |
| Table borders | Border, with a lighter internal divider |
| `TableRowBg`, `TableRowBgAlt` | Transparent, alternating row |
| `TextSelectedBg` | Translucent accent |
| Resize grips | Accent at increasing alpha for idle, hover, active |

Review remaining ImGui slots, including plots, drag/drop targets, dimmed backgrounds, and dimmed selected tab indicators. They must remain readable rather than inheriting unsuitable dark overrides.

## 2. Save the user's preference

Use the existing `Settings` INI API in `FrontEnd/src/Settings.h`; no new configuration subsystem is needed.

```ini
[appearance]
theme = light
```

Read with `Get("appearance", "theme", "dark")`; accept `light` and `dark`, and fall back to dark for missing or invalid values. Write with `Set("appearance", "theme", "light")` or `"dark"`, then use the existing save/error handling path. Do not overwrite unrelated settings or layout state. If saving fails, retain the pending preference and report the failure through the existing UI mechanism.

`App.cpp` currently calls `mSettings.Load()` during initialization, while platform setup also applies the theme. Resolve this ordering explicitly: apply the loaded preference before the first visible frame, and ensure later DPI resets reuse that preference. Avoid independent Settings instances that could save stale configuration.

For web builds, verify how configuration storage is persisted and synchronized by the existing web runtime. A successful in-memory INI write alone does not prove persistence across browser reloads.

## 3. Add the theme control

Add **Settings → Appearance → Light / Dark**, with the selected mode checked. Apply changes immediately and persist them. Route both ImGui and native menus through the same application action.

Relevant integration points:

- `FrontEnd/src/App.cpp`: Settings menu and application preference ownership.
- `FrontEnd/src/NativeMenu.h` and `NativeMenu.cpp`: shared menu commands and checked state; update platform menu builders that consume them.
- `FrontEnd/Platforms/Windows/WindowsPlatform.cpp`: startup and DPI-change theme application.
- `FrontEnd/Platforms/Web/WebPlatform.cpp`: startup theme application and background clear color.

The web backend currently has a fixed dark `glClearColor`; derive it from the active app-background token. Inspect the Windows clear path and any additional platform implementations for the same issue. Native title/menu bars need platform-specific appearance handling; ImGui colors cannot recolor OS chrome. The screenshot's macOS menu bar is an appearance reference, not evidence of a macOS backend in these files.

Ship explicit Light and Dark first. A future System option needs OS/browser appearance detection, change notifications, and separate storage of the requested mode and resolved palette.

## 4. Update custom panel drawing

Changing ImGui style alone will leave dark islands and low-contrast labels. Audit `IM_COL32`, `ImVec4`, `PushStyleColor`, and direct draw-list colors outside `Theme.cpp`.

Prioritize these surfaces from the mockup:

- `App.cpp`: paused banner, VRAM map background/grid/borders, status colors, custom controls, selected-object overlays.
- `PanelWidgets.h`: sprite transparency checkerboard and shared panel helpers.
- `HexEditorPanel.cpp`: alternating rows, address/ASCII text, changed-byte highlights, selected ranges, editable cells.
- `AssemblyPanel.cpp`: syntax colors currently described as intended for the dark theme.
- `WatchPanel.cpp`: positive, negative, and neutral change highlights.
- Other debugger, decompiler, memory comparison, logging, and tooltip panels: semantic warnings, errors, disabled text, and selection rendering.

Introduce tokens such as `PausedBackground`, `PausedText`, `SuccessText`, `WarningText`, `CheckerLight`, `CheckerDark`, `GridLine`, and syntax colors. Resolve them at draw time or refresh cached packed colors when the mode changes. File-scope constants computed once will not follow live theme changes.

Do not blanket-replace every color literal. Preserve decoded game colors, palette swatches, texture contents, and meaningful VRAM category hues. Add readable borders or labels to category swatches when needed. Distinguish UI text from overlays drawn over game imagery: those overlays need contrast against arbitrary game pixels in either theme.

Primary teal buttons need white text, while ordinary pale buttons use charcoal text. Checkboxes can use teal checkmarks on pale surfaces, or white checkmarks on filled teal backgrounds; avoid teal-on-teal marks.

## 5. Validate and finish

Use the same game, frame, docking layout, and selected sprite as the reference for a visual comparison. Do not alter panel sizing to compensate for color differences.

Acceptance checks:

- Switch Light/Dark repeatedly without resizing controls, changing fonts, losing docking, or modifying data.
- Restart and recover the selected theme; verify missing/invalid preference fallback and save failure behavior.
- Change DPI or move between monitors after selecting Light; confirm the palette remains light and geometry scales once.
- Check all tabs, native and ImGui menus, popups, tooltips, scrollbars, editable fields, selections, and disabled controls.
- Verify memory and assembly views, paused state, connection status, checkerboards, and VRAM map in both themes.
- Verify game pixels, sprite pixels, and color RAM values are unchanged.
- Target WCAG contrast of 4.5:1 for ordinary text and 3:1 for meaningful control outlines and focus indicators. Measure final rendered combinations, including alpha blending.
- Run the relevant existing frontend build/checks documented in `BUILD.md`. Add focused tests for preference parsing/fallback and palette switching; verify color-only switching preserves scaled geometry. Do not snapshot the entire generated image as a correctness test.

Recommended implementation order: shared palette API → saved preference and startup ordering → menu actions → platform/DPI integration → custom panel tokens → visual and build verification. Keep dark as the default for existing users while offering the approved light appearance as an explicit choice.

## Delivery notes

Implemented on `feature/light-theme`: shared light palette, saved explicit preference, both ImGui
settings menus, native Windows/macOS appearance submenus, macOS application appearance, renderer
clear colors, DPI-safe switching, checkerboards, paused banner, VRAM grid, hex/memory comparison,
assembly operands, decompiler syntax, and connection/help status colors. Dark remains the default.
App owns the requested mode; custom panel colors resolve against the currently applied palette.
Palette requests apply at the next frame boundary to avoid ImGui style-stack restoration hazards.

No System mode is added. Windows non-client chrome remains controlled by the existing OS path.
Browser persistence across reloads and native macOS/Windows visual behavior need platform smoke
checks. This is an initial palette audit; infrequently used panels may still contain literal colors
that need contrast review. The reference is a generated concept, not an actual runtime capture.

Validation in this Linux environment:

- CMake build with `SE_ENABLE_DECOMPILER=OFF`: passed (SDL2/OpenGL unavailable, so no native app executable).
- All 83 configured tests passed across the main run and reruns. Nine socket/filesystem tests
  required a rerun outside the sandbox; all nine then passed.
- Direct C++14 syntax checks passed for App with live support enabled and decompiler disabled,
  and separately for the modified decompiler panel. These do not establish a full decompiler build.
- Native Windows/macOS compilation, browser persistence, and visual comparison remain unverified.

## Claude review corrections

The follow-up fixes the Log panel's message/category colors, remaining reported status text,
watch highlight backgrounds, current-PC/confidence markers, primary Launch glyph/text, inline
sprite checkerboards, comparison-header text and changed bytes. Border color is now `#7D8898`;
muted text is `#596579`. Shared 1px base frame borders apply to both themes and scale with DPI.
This intentionally adds subtle outlines in Dark too, keeping color-only switching geometry stable.
Contrary to the review's note, the vendored ImGui does scale `FrameBorderSize` in `ScaleAllSizes`.

Applied mode is stored explicitly for the application's single ImGui context, rather than inferred
from a background channel. Theme parsing accepts mixed case. Dark renderer clear color remains
its original `(0.10, 0.10, 0.12)`. Tests cover contrast for light control outlines/header labels,
mode detection under background overrides, border scaling, and both native theme action mappings.
Native visual checks and browser persistence remain platform verification gaps.

Follow-up validation: the headless rebuild and 10 affected tests pass. ThemeTests now draws the
real Log panel in Light and checks the contrast of rendered font vertices (excluding solid fills
and ImGui's textured lines). That test also caught an unreadable disabled category-filter label,
which now uses the shared disabled-text color. App and the modified UI-only panels passed direct
C++14 syntax checks; native platform execution remains unverified.

Second review follow-up: Hex Editor queued/write/search messages use the Light error red;
the Default badge uses opaque `#DFF1E5` with `#16753C` text/border. Watch Name/Expression
fields explicitly push a zero frame-border width and restore it before the Type dropdown.
Heuristic and tracepoint markers use darker Light variants.

`ThemePreference.h` now owns the requested mode, Settings load/store mapping, native menu
request mapping and deferred palette application. App calls this same component at startup,
save, menu dispatch and the next BuildUI boundary. ThemeTests exercises these paths with a
real Settings object, including an unrelated setting and saving a pending request. SettingsTests
continues to cover disk writes/failures separately; this does not replace a full App/native-menu
integration test or prove browser persistence.
