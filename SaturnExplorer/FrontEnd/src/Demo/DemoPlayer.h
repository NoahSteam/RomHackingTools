// DemoPlayer — sequences a DemoScript's beats for the in-app feature tour.
//
// Pure state machine (no ImGui, no App): it tracks which beat is current, advances on a
// wall-clock timer in "auto" mode or on explicit Next/Prev in "manual" mode, and raises a
// one-shot "dirty" flag whenever the current beat changes so the host can apply it exactly
// once. Manual mode is the default because, when recording narration live, advancing on a
// keypress keeps picture and voice in perfect sync; auto mode is for hands-free playback and
// uses each beat's `hold` time.
//
// Unit-tested headlessly (FrontEnd/tests/DemoScriptTests.cpp).
#pragma once

#include "Demo/DemoScript.h"

namespace sfe
{

class DemoPlayer
{
public:
    // Replace the loaded script and reset to a stopped state. Returns script.ok.
    bool Load(DemoScript script);

    // Parse + load text in one step (for callers that have the raw file contents).
    bool LoadText(const std::string& text) { return Load(DemoParseText(text)); }

    // Begin playback at the first beat (no-op with an empty/failed script). Marks the first
    // beat dirty so the host applies it on the next ConsumeDirty().
    void Start();
    // Stop playback and leave no beat current.
    void Stop();

    // Advance the auto-mode timer by dt seconds. In auto mode, once the elapsed time reaches
    // the current beat's hold, advance to the next beat (or stop after the last). No effect in
    // manual mode or when stopped.
    void Tick(double dt);

    // Manual controls. Next past the last beat stops playback. Both mark the new beat dirty.
    void Next();
    void Prev();

    void SetAuto(bool on) { mAuto = on; }
    bool Auto() const { return mAuto; }
    bool Playing() const { return mPlaying; }
    int  Index() const { return mIndex; }
    int  Count() const { return static_cast<int>(mScript.beats.size()); }
    bool Loaded() const { return mScript.ok && !mScript.beats.empty(); }

    // The current beat, or nullptr when stopped / no script.
    const DemoBeat* Current() const;

    // Seconds the current beat has been showing (auto mode), and its hold time. For the
    // operator overlay's progress readout.
    double Elapsed() const { return mElapsed; }
    double CurrentHold() const;

    // True once if the current beat changed since the last call (start/next/prev/auto-advance).
    // The host calls this each frame and applies the current beat when it returns true.
    bool ConsumeDirty();

private:
    DemoScript mScript;
    int        mIndex = -1;
    double     mElapsed = 0.0;
    bool       mPlaying = false;
    bool       mAuto = false;    // manual advance by default
    bool       mDirty = false;
};

}  // namespace sfe
