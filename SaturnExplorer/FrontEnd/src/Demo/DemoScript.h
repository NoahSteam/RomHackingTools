// DemoScript — the parsed model of a ".sedemo" feature-tour script, and its text parser.
//
// A demo script is a sequence of "beats". Each beat dwells for a hold time (in auto mode),
// carries a narration note (read aloud over the recording — it is NOT drawn on screen), and
// a list of actions that drive the real UI when the beat becomes current (show/hide/solo
// panels, focus a panel, select a VDP1 command, toggle a render layer, load a savestate, or
// fire a whitelisted toolbar command).
//
// This header is deliberately free of ImGui and App: it is pure data + a pure parser, so the
// whole thing is unit-tested headlessly (see FrontEnd/tests/DemoScriptTests.cpp). App owns the
// one impure step — turning a DemoAction into a real UI mutation (App::ApplyDemoBeat).
//
// Text format (blank lines ignored; '#' at a token boundary starts a comment that runs to
// end of line -- whole-line or trailing. A '#' inside a quoted string is kept):
//
//     @beat <id> [hold <seconds>]
//       note "narration text for this beat"
//       solo vdpOutput worldView        # show exactly these panels, hide the rest
//       show textureViewer              # reveal one panel
//       hide log                        # hide one panel
//       focus selectedObject            # bring a panel's tab forward
//       select 42                       # select VDP1 command #42
//       layer vdp1 off                  # toggle a render layer on/off
//       load "path/to/state.yss"        # load a savestate/dump
//       command step                    # fire a safe toolbar command
#pragma once

#include <string>
#include <vector>

namespace sfe
{

enum class DemoVerb
{
    Unknown,
    Show,     // args[0] = panel key
    Hide,     // args[0] = panel key
    Solo,     // args    = panel keys to show (all others hidden)
    Focus,    // args[0] = panel key
    Select,   // args[0] = VDP1 command index (decimal)
    Layer,    // args[0] = layer name, args[1] = "on"/"off"
    Load,     // args[0] = savestate/dump path
    Command,  // args[0] = whitelisted command name (pause/step/screenshot)
};

struct DemoAction
{
    DemoVerb                 verb = DemoVerb::Unknown;
    std::vector<std::string> args;
};

struct DemoBeat
{
    std::string             id;               // short label, e.g. "pick-sprite"
    double                  hold = 6.0;       // auto-mode dwell, seconds
    std::string             note;             // narration text (spoken, never drawn)
    std::vector<DemoAction> actions;
};

struct DemoScript
{
    bool                  ok = false;
    std::string           error;              // parse error (with 1-based line number) if !ok
    std::vector<DemoBeat> beats;
};

// Parse demo-script text. On a structural error (an action before any @beat, a malformed
// hold, an unterminated quote) returns a script with ok=false and a human-readable error;
// unknown verbs/layer names are tolerated (kept as DemoVerb::Unknown) so a newer script
// degrades gracefully on an older build rather than refusing to load.
DemoScript DemoParseText(const std::string& text);

// Default dwell when a beat omits "hold".
constexpr double kDemoDefaultHold = 6.0;

}  // namespace sfe
