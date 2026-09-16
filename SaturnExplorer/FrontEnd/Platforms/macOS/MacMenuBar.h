// MacMenuBar -- the native macOS (Cocoa NSMenu) implementation of the NativeMenu bridge, the
// macOS counterpart of Platforms/Windows/Win32MenuBar. It builds the application menu bar that
// mirrors the ImGui top toolbar and installs it as [NSApp mainMenu]; selections arrive through a
// target/action handler and are queued as NativeMenuAction, which App drains and runs through the
// same TopBarCommand path the toolbar uses. Enable/disable and checkmarks are refreshed from the
// NativeMenuState App supplies each frame -- lazily, when a menu is about to open (NSMenuDelegate
// menuNeedsUpdate:), so the flags are always current the instant a menu opens without rebuilding
// the tree under the user.
//
// This header is deliberately Cocoa-free (all AppKit lives in MacMenuBar.mm, behind an opaque
// Impl) so the plain-C++ WebPlatform.cpp -- the shared SDL2 desktop backend used on macOS -- can
// own a MacMenuBar member without being compiled as Objective-C++. Only App-facing / std types
// appear here; the portable App never sees an NSMenu, exactly as it never sees an HMENU.
#pragma once

#include <vector>

#include "NativeMenu.h"

namespace sfe
{

// Opaque implementation, defined in MacMenuBar.mm. Declared at namespace scope (rather than as a
// private nested type) so the file's Objective-C glue can name MacMenuBarImpl* without tripping
// access control.
struct MacMenuBarImpl;

class MacMenuBar
{
public:
    MacMenuBar();
    ~MacMenuBar();
    MacMenuBar(const MacMenuBar&) = delete;
    MacMenuBar& operator=(const MacMenuBar&) = delete;

    // Called once per frame with the latest App state. Rebuilds the menu structure only when the
    // labels/lists actually change (never while a menu is open); otherwise the stored state just
    // feeds the next menuNeedsUpdate: enable/check refresh.
    void Sync(const NativeMenuState& state);

    // Hand App the menu selections seen since the last drain, then clear them.
    void Drain(std::vector<NativeMenuAction>& out);

    // Restore the main menu that was in place before the first Sync (WebPlatform::Shutdown).
    void Detach();

private:
    MacMenuBarImpl* mImpl = nullptr;
};

}  // namespace sfe
