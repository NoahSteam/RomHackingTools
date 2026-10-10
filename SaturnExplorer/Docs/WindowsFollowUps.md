# Windows follow-ups

Work that a review of `642ca69d` turned up and that can only be finished, or at least verified, on
Windows. The macOS and cross-platform findings from the same review are already fixed (listed at the
bottom, so nobody redoes them). Nothing below has been built on Windows: the shared changes in this pass
compiled and tested on macOS only.

Order is by how likely each is to hurt a user.

## 1. The checked-in Visual Studio project does not link  (P1)

`FrontEnd/FrontEnd.vcxproj` is the "Option 2" build in `BUILD.md`. Its sources are globbed
(`src\**\*.cpp`, `Platforms\Windows\*.cpp`), so new files are picked up, but it disagrees with the CMake
target (`CMakeLists.txt`, `SaturnExplorerFrontEnd`, Windows branch) in two ways:

- **Libraries.** `AdditionalDependencies` is `d3d11;dxgi;d3dcompiler;comdlg32;ws2_32`. CMake also links
  `winhttp` (`WindowsPlatform::HttpsGet`, the update check) and `winmm` (`PlayAudio`). Expect unresolved
  externals on a clean MSBuild build. Add `winhttp.lib;winmm.lib`.
- **`SE_NATIVE_MENUBAR`.** CMake defines it; the vcxproj's `PreprocessorDefinitions` does not. An MSBuild
  build therefore draws the ImGui toolbar and never installs the native menu, which is different behaviour
  from the CMake build. Add it, or the two builds are not the same program.

Also check, since the glob is wider than CMake's explicit list: `src\PosixSpawn.cpp` is compiled by MSBuild
(it is `#if`-guarded to nothing off POSIX, so that is fine), and nothing else under `src\` is POSIX-only.

Decide whether the project should exist at all. The simplest fix that cannot drift again is to make it
delegate to CMake, or drop it and point `BUILD.md` at `cmake -G "Visual Studio 17 2022"` only.

**Verify:** a clean `msbuild` of `RomHackingTools.sln` (Debug x64) links, and the menu bar is the native
one. Add that build to CI next to the CMake one.

## 2. Unicode and long paths  (P2)

Windows paths are handled as the ANSI code page in several places while the rest of the app (and the native
menu labels) treat strings as UTF-8. A ROM or export folder with characters outside the ANSI page (CJK,
Arabic, emoji) can fail validation, fail to launch, or fail to export; ImGui text entry can produce such a
path directly.

Places to convert (UTF-8 inside the app, UTF-16 at every OS call):

- `FrontEnd/Platforms/Windows/WindowsPlatform.cpp` (roughly lines 269-350): the file dialogs use the `A`
  APIs with fixed `MAX_PATH` buffers. Move to `GetOpenFileNameW` / `GetSaveFileNameW` (or the
  `IFileDialog` COM API) with larger buffers, or `SHBrowseForFolderW` / `IFileOpenDialog` for folders.
  `ShellExecuteExA` in `LaunchProcess` / `LaunchTool` and `ShellExecuteA` in `RevealPath` / `OpenURL`
  become the `W` forms.
- `FrontEnd/src/DataSearch.cpp` (`PathExists` and friends, `GetFileAttributesA`) and
  `FrontEnd/src/FileWrite.cpp` (`MoveFileExA`, plus the narrow `fopen` / `_mkdir` / `_unlink` /
  `_stat` it uses): route through one UTF-8 to UTF-16 helper and use `_wfopen`, `_wmkdir`, `_wunlink`,
  `_wstat`, `MoveFileExW`.
- Long paths: prefix with `\\?\` after converting, or opt in through the manifest
  (`longPathAware`) and test both.
- `Settings.cpp` and `ConfigDir()` build `%APPDATA%` paths with the narrow environment API; same treatment.

FileWrite's staging (`WriteFileAtomically`) was just made the settings writer too, so converting it fixes
settings, exports and diff folders together.

**Verify on Windows:** a ROM and an export folder named with CJK, Arabic and an emoji, plus a path longer
than 260 characters, through browse, recent list, launch, Compare Memory (external diff tool folders), export
and settings save/reload.

## 3. Windows emulator launch: finish the move to `LaunchEmulator`  (P2)

`IPlatform::LaunchEmulator` is the new way to start the emulator (argv, not a string, with a reportable
error). Windows still uses the legacy `LaunchProcess(path, argsString, dir)` through the interface's
default forwarder, which quotes the argument vector with `sfe::JoinWindowsCommandLine`. That is correct but
leaves these to do in `WindowsPlatform.cpp`:

- Override `LaunchEmulator` natively: build the parameter string from the vector (as `LaunchTool` already
  does), return the `GetLastError()` text in `error`, then delete the string-form `LaunchProcess` and its
  `IPlatform` declaration.
- **One owned emulator at a time.** `WebPlatform::LaunchEmulator` stops the previous emulator before
  starting another. Windows `LaunchProcess` only closes the old handle (`CloseHandle`), so a second launch
  orphans the first. `App::StopOwnedEmulator` covers the app's own call sites, but the invariant belongs in
  the platform: call `TerminateLaunchedProcess()` at the top of the override.
- **A stop that fails must block the replacement.** `IPlatform::StopEmulator(error)` is the contract: false
  means the emulator is still running and still owned, and the app then refuses to launch another
  (`App::StopOwnedEmulator`). The macOS side implements it (`OwnedChild`); Windows inherits the default,
  which calls `TerminateLaunchedProcess` and always reports success. Override it: `TerminateProcess`, then
  `WaitForSingleObject(handle, ~2000)`; on timeout keep the handle and return false with a message.
- `sei.hProcess` can be NULL when the target reused an existing process; then nothing is tracked and a
  relaunch cannot stop it. Decide what that should mean (probably: report "already running").
- Windows `TerminateLaunchedProcess` calls `TerminateProcess` directly and does not wait. That cannot hang,
  but check it releases whatever the emulator held (the live socket) before the next launch reconnects:
  `WaitForSingleObject(handle, ~2000)` after `TerminateProcess` is the Windows equivalent of
  `StopChild`'s bounded wait.
- The patch script now starts through `LaunchTool("py", {"-3", script})` with `LaunchTool("python", ...)`
  as the fallback (the patch-apply code near `scriptPath` in `App.cpp`, `#ifdef _WIN32` branch). Confirm that `ShellExecuteEx`
  resolves `py` / `python` from `PATH` the way the old `LaunchProcess` did, and that starting it no longer
  replaces the emulator handle.

**Verify:** launch an emulator, apply a patch, relaunch: the original emulator is the one that is stopped,
and exactly one is running afterwards. Same after Launch Settings > Test Launch.

## 4. Native menu while a dialog is open  (P2, both platforms)

The dispatch side is fixed: while an ImGui modal is open, native menu selections are dropped, and every
queued command is re-checked against `TopBarCommandEnabled` against the current state before it runs
(`App.cpp`, the native-menu drain and the command loop). What is left is cosmetic and per platform: the OS
menu items still look enabled behind a modal and silently do nothing.

- Add `bool modalOpen` to `NativeMenuState`, set it in `App::BuildNativeMenuState`
  (`ImGui::GetTopMostAndVisiblePopupModal() != nullptr`), and have `Win32MenuBar.cpp` grey every item while
  it is set (`MacMenuBar.mm` should do the same; it is the same change, listed here so it is done in one
  pass). Keep the structure key unchanged: enablement is refreshed live, not by rebuilding the HMENU.

**Verify:** open Launch Settings (a modal), click through the menu bar: nothing is selectable, and closing
the dialog restores the menu. Also queue Disconnect then Pause (shortcut and menu in one frame) and confirm
Pause is skipped.

## 5. Things compiled only on macOS  (verify, probably nothing to do)

- `FrontEnd/src/ArgSplit.h` (header-only), `Launcher.cpp` (`BuildLaunchArgv`), `DiffTool.cpp` (now uses
  `ArgSplit.h`): plain C++14, expected to build; the `{rom}` substitution on Windows now goes through
  `LaunchEmulator`'s default (`JoinWindowsCommandLine`) rather than a hand-quoted string. The launch preview
  in Launch Settings still uses the old string builder `BuildLaunchArgs` and is display-only.
- `IPlatform.h` now includes `CommandLine.h` (same directory) for the default `LaunchEmulator`.
- `Settings::Save` now stages through `WriteFileAtomically` instead of `std::ofstream`. On Windows the
  rename-over-existing path in `FileWrite.cpp` is what replaces `settings.ini`; confirm it succeeds while
  the installer (`Integration/install.py`) is not holding the file, and that a read-only `settings.ini`
  produces the "Could not save settings" message rather than data loss.
- `AbsolutePath` (`Launcher.cpp`) uses `_fullpath` on Windows, which has never been compiled or run there.
  Check rooted (`\\x`), drive-relative (`C:x`), UNC and long paths launch the file that was picked. The
  complete-launch test (`LaunchPathTests.cpp`) is POSIX-only; a Windows version needs a `.bat`/`.exe` stand-in.
- CMake: four existing test targets (`TopBar`, `ControllerPanel`, `PanelInteraction`, `SavestateSlots`)
  gained `FileWrite.cpp`. The new `SaturnExplorerSettingsTests` target is POSIX-only (it uses `chmod` and
  `/dev/full`); a Windows version would need an equivalent for the read-only-folder case.

## Fixed already (not Windows work)

For reference, from the same review, done in the macOS/cross-platform pass:

1. Shell expansion of ROM filenames on macOS: the emulator is started as an argv (`SpawnChild`), the launch
   template is split before the ROM is substituted (`BuildLaunchArgv`, `ArgSplit.h`).
2. Settings saves are all-or-nothing and failures are reported and retried (`Settings::SaveTo`,
   `App::SaveSettings`).
5. The patch script no longer takes over the emulator's process slot (`LaunchTool`), Test Launch is an
   owned launch (`App::StopOwnedEmulator`), and a second emulator launch stops the first in the platform.
6. macOS launch reports failures (missing, not executable, bad working directory) and resolves relative
   executable paths before changing directory.
7. macOS relaunch can no longer block forever: `StopChild` is SIGTERM, a bounded wait, then SIGKILL.
8. Native menu selections are ignored behind a modal and re-checked for enablement when they run (the
   greying-out is item 4 above).
10. Relative ROM and BIOS paths are made absolute against SE's directory before they reach the emulator
   (`AbsolutePath`, used by Launch and Test Launch), since the emulator starts in its own folder.
11. A stop that fails keeps the emulator owned and blocks the replacement launch (`OwnedChild`,
    `IPlatform::StopEmulator`).
9. macOS packaging fails the install when `codesign` fails and verifies the signature afterwards.
