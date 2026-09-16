// Win32MenuBar — the native Win32 (HMENU) implementation of the NativeMenu bridge.
// Builds a standard window menu bar that mirrors the ImGui top toolbar and attaches it to
// the app window with SetMenu. Selections arrive as WM_COMMAND in WindowsPlatform::WndProc,
// which forwards them here; App drains them (as NativeMenuAction) and runs them through the
// same TopBarCommand path the toolbar uses. Enable/disable and checkmarks are refreshed from
// the NativeMenuState App supplies each frame — lazily, on WM_INITMENUPOPUP, so the flags are
// always current the instant a menu opens without rebuilding the tree under the user.
//
// This is a platform file (Platforms/Windows), so it is the only place — alongside
// WindowsPlatform.cpp — allowed to touch OS/GPU types; the portable App never sees an HMENU.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

#include "NativeMenu.h"

namespace sfe
{

class Win32MenuBar
{
public:
    // Bind to the app window. Does not build the menu yet; the first Sync() does, once App
    // has supplied a state to build from.
    void Attach(HWND hwnd) { mHwnd = hwnd; }

    // Tear down the HMENU (WindowsPlatform::Shutdown, before the window is destroyed).
    void Detach();

    // Called once per frame with the latest App state. Rebuilds the menu structure only when
    // the labels/lists actually change (never while a menu is open); otherwise the stored state
    // just feeds the next WM_INITMENUPOPUP refresh.
    void Sync(const NativeMenuState& state);

    // Hand App the menu selections seen since the last drain, then clear them.
    void Drain(std::vector<NativeMenuAction>& out);

    // --- WndProc hooks (WindowsPlatform forwards the matching messages) ---
    bool OnCommand(int commandId);   // WM_COMMAND: true if the id was one of ours
    void OnInitPopup();              // WM_INITMENUPOPUP: refresh enable/check/labels
    void OnEnterMenuLoop() { mMenuOpen = true; }
    void OnExitMenuLoop();           // WM_EXITMENULOOP: apply a rebuild deferred while open

private:
    void Rebuild();        // (re)create the HMENU tree from mState and attach it
    void RefreshState();   // apply mState's enable/check/label flags to the live HMENU

    HWND            mHwnd = nullptr;
    HMENU           mMenu = nullptr;
    NativeMenuState mState;
    bool            mHaveState = false;
    bool            mMenuOpen = false;
    bool            mRebuildPending = false;
    std::string     mBuiltKey;   // structureKey the current HMENU was built from
    std::vector<NativeMenuAction> mQueue;
};

}  // namespace sfe
