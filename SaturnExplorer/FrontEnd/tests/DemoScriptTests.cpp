// Unit tests for the feature-tour demo engine: the .sedemo parser (DemoScript) and the beat
// sequencer (DemoPlayer). Both are pure — no ImGui, no App — so the parse result, the
// manual/auto advance behaviour, and the one-shot "dirty" contract can be asserted directly.
#include "Demo/DemoPlayer.h"
#include "Demo/DemoScript.h"

#include <cstdio>
#include <string>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; } }
}  // namespace

int main()
{
    // --- Parser: beats, holds, notes, actions, comments, quoted strings ---
    {
        const std::string text =
            "# a comment line\n"
            "\n"
            "@beat intro hold 4\n"
            "  note \"Saturn Explorer: a visual RE tool.\"\n"
            "  solo vdpOutput worldView\n"
            "\n"
            "@beat pick\n"                       // no hold -> default
            "  note \"part one\"\n"
            "  note \"part two\"\n"              // notes join with a space
            "  select 42\n"
            "  layer vdp1 off\n"
            "  focus selectedObject\n"
            "  load \"states/Battle3.yss\"\n";   // quoted path with a space-free dir

        DemoScript s = DemoParseText(text);
        Check(s.ok, s.ok ? "parse ok" : s.error.c_str());
        Check(s.beats.size() == 2, "two beats parsed");

        Check(s.beats[0].id == "intro", "beat id");
        Check(s.beats[0].hold == 4.0, "explicit hold honored");
        Check(s.beats[0].note == "Saturn Explorer: a visual RE tool.", "quoted note preserved");
        Check(s.beats[0].actions.size() == 1, "note is not an action");
        Check(s.beats[0].actions[0].verb == DemoVerb::Solo &&
              s.beats[0].actions[0].args.size() == 2 &&
              s.beats[0].actions[0].args[0] == "vdpOutput" &&
              s.beats[0].actions[0].args[1] == "worldView", "solo keeps all panel args");

        Check(s.beats[1].hold == kDemoDefaultHold, "missing hold -> default");
        Check(s.beats[1].note == "part one part two", "multi-line notes joined");
        const auto& a = s.beats[1].actions;
        Check(a.size() == 4, "four non-note actions");
        Check(a[0].verb == DemoVerb::Select && a[0].args[0] == "42", "select index");
        Check(a[1].verb == DemoVerb::Layer && a[1].args[0] == "vdp1" && a[1].args[1] == "off",
              "layer name + state");
        Check(a[2].verb == DemoVerb::Focus && a[2].args[0] == "selectedObject", "focus key");
        Check(a[3].verb == DemoVerb::Load && a[3].args[0] == "states/Battle3.yss", "quoted load path");
    }

    // --- Parser: unknown verb tolerated (forward-compatible), errors flagged ---
    {
        DemoScript s = DemoParseText("@beat b\n  wiggle foo bar\n");
        Check(s.ok, "unknown verb does not fail the parse");
        Check(s.beats.size() == 1 && s.beats[0].actions.size() == 1 &&
              s.beats[0].actions[0].verb == DemoVerb::Unknown, "unknown verb kept as Unknown");

        DemoScript e1 = DemoParseText("  solo vdpOutput\n");   // action before any @beat
        Check(!e1.ok, "action before @beat is an error");

        DemoScript e2 = DemoParseText("@beat b\n  note \"oops\n");   // unterminated quote
        Check(!e2.ok, "unterminated quote is an error");

        DemoScript e3 = DemoParseText("@beat b hold -1\n");
        Check(!e3.ok, "non-positive hold is an error");
    }

    // --- Parser: unquoted notes keep the whole line; '#' comments are stripped ---
    {
        const std::string text =
            "@beat b hold 3          # trailing comment after a beat header\n"
            "  note this note is not quoted and keeps every word\n"
            "  solo vdpOutput worldView   # hide the rest\n"
            "  note \"a quoted # hash survives\"\n";
        DemoScript s = DemoParseText(text);
        Check(s.ok, s.ok ? "parse ok (comments)" : s.error.c_str());
        Check(s.beats.size() == 1, "one beat");
        Check(s.beats[0].hold == 3.0, "hold parses with a trailing comment");
        Check(s.beats[0].note ==
                  "this note is not quoted and keeps every word a quoted # hash survives",
              "unquoted note keeps every word; a quoted '#' survives");
        Check(s.beats[0].actions.size() == 1, "a trailing comment is not an action");
        Check(s.beats[0].actions[0].verb == DemoVerb::Solo &&
              s.beats[0].actions[0].args.size() == 2,
              "inline comment words do not become panel keys");
    }

    // --- Player: manual advance + one-shot dirty ---
    {
        DemoPlayer p;
        Check(p.LoadText("@beat a\n@beat b\n@beat c\n"), "player loads a script");
        Check(p.Loaded() && p.Count() == 3, "three beats loaded");
        Check(!p.Playing() && p.Current() == nullptr, "stopped before Start");
        Check(!p.ConsumeDirty(), "no dirty before Start");

        p.Start();
        Check(p.Playing() && p.Index() == 0, "Start at beat 0");
        Check(p.ConsumeDirty(), "Start marks first beat dirty");
        Check(!p.ConsumeDirty(), "dirty is one-shot");
        Check(p.Current() && p.Current()->id == "a", "current is beat a");

        p.Next();
        Check(p.Index() == 1 && p.ConsumeDirty(), "Next -> beat 1, dirty");
        p.Prev();
        Check(p.Index() == 0 && p.ConsumeDirty(), "Prev -> beat 0, dirty");
        p.Prev();
        Check(p.Index() == 0 && !p.ConsumeDirty(), "Prev at start is a no-op");

        // Manual mode ignores the clock.
        p.Tick(100.0);
        Check(p.Index() == 0 && !p.ConsumeDirty(), "Tick does nothing in manual mode");

        p.Next(); p.ConsumeDirty();
        p.Next(); p.ConsumeDirty();
        Check(p.Index() == 2, "advanced to last beat");
        p.Next();
        Check(!p.Playing(), "Next past the last beat stops playback");
    }

    // --- Player: auto advance by hold time ---
    {
        DemoPlayer p;
        p.LoadText("@beat a hold 2\n@beat b hold 3\n");
        p.SetAuto(true);
        p.Start();
        Check(p.ConsumeDirty(), "auto: first beat dirty on Start");
        p.Tick(1.0);
        Check(p.Index() == 0 && !p.ConsumeDirty(), "auto: still on beat 0 before hold elapses");
        p.Tick(1.5);   // 2.5 >= 2.0 -> advance
        Check(p.Index() == 1 && p.ConsumeDirty(), "auto: advanced after hold elapsed");
        p.Tick(3.0);   // past last beat's hold -> stop
        Check(!p.Playing(), "auto: stops after the last beat");
    }

    // --- Player: auto mode never advances past a beat the host has not applied yet ---
    {
        DemoPlayer p;
        p.LoadText("@beat a hold 1\n@beat b hold 1\n");
        p.SetAuto(true);
        p.Start();
        // dt is the PREVIOUS frame's delta and can be huge (a stall, or the frame a file
        // dialog closed); it must not consume a beat that was never shown.
        p.Tick(60.0);
        Check(p.Index() == 0, "auto: a large delta does not skip the unapplied first beat");
        Check(p.ConsumeDirty(), "auto: first beat is still pending");
        p.Tick(60.0);   // applied now, so it may advance
        Check(p.Index() == 1 && p.ConsumeDirty(), "auto: advances once the beat was applied");

        DemoPlayer one;
        one.LoadText("@beat only hold 1\n");
        one.SetAuto(true);
        one.Start();
        one.Tick(60.0);
        Check(one.Playing() && one.ConsumeDirty(),
              "auto: a single-beat script still gets its beat applied");
    }

    if (gFail == 0) std::printf("All Demo engine tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
