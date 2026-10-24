# Claude review prompt

Review Saturn Explorer's light-theme implementation on branch `feature/light-theme`.
The base commit is `d83a4c3a199cbbd6df0b3098a8f875cc06190af2`.

Read `CLAUDE.md`, `Docs/Codebase/README.md`, the frontend subsystem map, and
`Docs/LightThemeImplementation.md`. Inspect the actual diff against the base commit and its
call sites. The approved visual direction is `Docs/Images/SaturnExplorerLightTheme.png`.
This is a review request: do not merge, push, or make changes unless asked.

Look for actionable defects and missing requirements, especially:

1. Light/Dark selection through both ImGui Settings entry points and native Windows/macOS
   menus; checked states, command dispatch, modal gating, and next-frame application.
2. Preference load before the first visible frame, invalid/missing value fallback, saving the
   requested mode, and failure/retry behavior without losing other settings.
3. DPI changes and repeated switching: no cumulative scaling, font changes, geometry resets,
   or old palette colors restored by ImGui style stacks. Review the palette detection used
   by custom drawing and its behavior with pushed style overrides and multiple contexts.
4. Contrast on all controls and custom panels: hex/ASCII/Shift-JIS, assembly operands,
   decompiler syntax, memory comparison, paused and connection states, tooltips, selection,
   checkerboards, VRAM map, and less common debugger/log/sound panels. Identify hardcoded
   colors that remain unreadable in Light. Distinguish UI chrome from game-image overlays.
5. Preservation of game/sprite/CRAM pixels and semantic VRAM category colors.
6. Native compilation and behavior: macOS AppKit appearance, Windows menu IDs and DPI reset,
   SDL/OpenGL clear colors, and Emscripten compatibility. Do not assume a headless Linux
   build verifies these native backends or browser persistence.
7. Adequacy of `ThemeTests.cpp` and existing affected tests. Run meaningful checks where
   available. Build the native frontend and compare both themes visually if the host supports it.

Report findings first, ordered by severity, with precise file/line references, triggering
conditions, user-visible impact, and a suggested correction. Separate confirmed bugs from
platform checks you could not perform. If there are no findings, say so and list remaining
verification gaps. Do not spend review space on cosmetic style preferences.

Author validation: Linux headless CMake build (`SE_ENABLE_DECOMPILER=OFF`), all 83 configured
tests passing across the main run and reruns (nine needed socket/filesystem permissions),
and separate syntax checks for App/live and DecompilerPanel. SDL2/OpenGL was unavailable;
no native GUI, macOS/Windows compiler, browser, or visual comparison was exercised.
