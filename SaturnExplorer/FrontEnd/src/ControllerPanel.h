// ControllerPanel — a Sega Saturn control pad the user can press (mouse or keyboard)
// to drive a running game. It renders the pad with ImGui draw primitives, hit-tests
// each button, highlights whatever is currently pressed, and reports the pressed
// state as an emulator-agnostic SE_PAD_* bitmask (see SeLiveProtocol.h). The App
// forwards that mask to the live emulator, which drives the emulated pad directly.
//
// Input sources, all OR'd into the reported mask:
//   * mouse (momentary): press-and-hold a button — one at a time (single active item).
//   * auto-hold (latch): click toggles a button on/off, so multiple can be held.
//   * keyboard: a default key map, active only while this window is focused, so
//     several buttons (D-pad + face) can be held at once for actual play.
#pragma once

namespace sfe
{

class ControllerPanel
{
public:
    // Draw the pad in the current ImGui window and return the pressed SE_PAD_* mask.
    // 'liveConnected' only affects the status hint shown to the user.
    unsigned int Draw(bool liveConnected);

    int Port() const { return mPort; }

private:
    int          mPort = 0;          // 0 = Controller 1, 1 = Controller 2
    bool         mAutoHold = false;  // click latches instead of momentary hold
    bool         mKeyboard = true;   // accept keyboard while the window is focused
    unsigned int mLatched = 0;       // buttons latched on in auto-hold mode
};

}  // namespace sfe
