// ControllerPanel logic that needs no window: macro playback vs. deletion, and adopting the
// emulator's key map. The panel's drawing is not exercised here.

#include <cstdio>
#include "ControllerPanel.h"
#include "SeLiveProtocol.h"
#include "imgui.h"

using namespace sfe;

namespace
{
int gFailures = 0;

void Check(bool ok, const char* expr, int line)
{
    if (ok) return;
    std::fprintf(stderr, "FAIL line %d: %s\n", line, expr);
    ++gFailures;
}
#define CHECK(c) Check((c), #c, __LINE__)

std::vector<unsigned int> Held(unsigned int bits) { return std::vector<unsigned int>(10, bits); }

// A macro holding A for several frames, deleted while it plays, must release A at once --
// Update() skips a macro whose index is no longer valid, so nothing else would.
void DeletingThePlayingMacroReleasesIt()
{
    ControllerPanel p;
    p.Update(true, 0);                // connect first: the first connected tick clears state
    p.AddMacro("hold", Held(SE_PAD_A));
    p.PlayMacro(0);
    p.Update(true, 1);
    CHECK(p.FinalState() == SE_PAD_A);
    p.DeleteMacro(0);
    CHECK(p.FinalState() == 0);
    p.Update(true, 2);
    p.Update(true, 3);
    CHECK(p.FinalState() == 0);
}

// Deleting an earlier macro shifts the playing one down; it must keep playing, not stop
// and not turn into its neighbour.
void DeletingAnEarlierMacroKeepsPlayback()
{
    ControllerPanel p;
    p.Update(true, 0);
    p.AddMacro("b", Held(SE_PAD_B));
    p.AddMacro("a", Held(SE_PAD_A));
    p.PlayMacro(1);
    p.Update(true, 1);
    CHECK(p.FinalState() == SE_PAD_A);
    p.DeleteMacro(0);
    p.Update(true, 2);
    CHECK(p.FinalState() == SE_PAD_A);
    p.Update(true, 50);               // past the end: released as usual
    CHECK(p.FinalState() == 0);
}

void DeletingALaterMacroKeepsPlayback()
{
    ControllerPanel p;
    p.Update(true, 0);
    p.AddMacro("a", Held(SE_PAD_A));
    p.AddMacro("b", {SE_PAD_B});
    p.PlayMacro(0);
    p.Update(true, 1);
    p.DeleteMacro(1);
    p.Update(true, 2);
    CHECK(p.FinalState() == SE_PAD_A);
}

// Mednafen reporting a button unbound means SE must not keep its own default key for it.
void UnboundEmulatorKeysAreCleared()
{
    ControllerPanel p;
    CHECK(p.KeyBindingFor(SE_PAD_A) == ImGuiKey_Z);        // SE's default
    int32_t map[13];
    for (int32_t& m : map) m = -1;
    map[0] = 26;                                           // UP -> W (USB-HID 26)
    // map[4] (A) stays unbound
    p.ApplyLiveKeyMap(map, 13);
    CHECK(p.KeyBindingFor(SE_PAD_UP) == ImGuiKey_W);
    CHECK(p.KeyBindingFor(SE_PAD_A) == ImGuiKey_None);
    CHECK(p.KeyBindingFor(SE_PAD_START) == ImGuiKey_None);
}

// A key SE has no ImGuiKey for (a joystick button reports -1; an exotic key reports a
// scancode with no mapping) is also not something SE can mirror.
void UnsupportedKeysAreCleared()
{
    ControllerPanel p;
    int32_t map[13];
    for (int32_t& m : map) m = 4 + 0;                      // everything on A... then
    map[5] = 200;                                          // B -> a scancode with no ImGuiKey
    p.ApplyLiveKeyMap(map, 13);
    CHECK(p.KeyBindingFor(SE_PAD_B) == ImGuiKey_None);
}

// A server that reports fewer entries leaves the rest as they were.
void ShortMapLeavesTheRest()
{
    ControllerPanel p;
    int32_t map[2] = {26, -1};                             // UP -> W, DOWN unbound
    p.ApplyLiveKeyMap(map, 2);
    CHECK(p.KeyBindingFor(SE_PAD_UP) == ImGuiKey_W);
    CHECK(p.KeyBindingFor(SE_PAD_DOWN) == ImGuiKey_None);
    CHECK(p.KeyBindingFor(SE_PAD_A) == ImGuiKey_Z);        // not reported: untouched
}
}  // namespace

int main()
{
    DeletingThePlayingMacroReleasesIt();
    DeletingAnEarlierMacroKeepsPlayback();
    DeletingALaterMacroKeepsPlayback();
    UnboundEmulatorKeysAreCleared();
    UnsupportedKeysAreCleared();
    ShortMapLeavesTheRest();
    if (gFailures) { std::fprintf(stderr, "%d failure(s)\n", gFailures); return 1; }
    std::printf("ControllerPanel tests passed\n");
    return 0;
}
