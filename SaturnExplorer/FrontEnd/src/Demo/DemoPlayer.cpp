#include "Demo/DemoPlayer.h"

namespace sfe
{

bool DemoPlayer::Load(DemoScript script)
{
    mScript = std::move(script);
    mIndex = -1;
    mElapsed = 0.0;
    mPlaying = false;
    mDirty = false;
    return mScript.ok;
}

void DemoPlayer::Start()
{
    if (!Loaded()) return;
    mIndex = 0;
    mElapsed = 0.0;
    mPlaying = true;
    mDirty = true;
}

void DemoPlayer::Stop()
{
    mPlaying = false;
    mIndex = -1;
    mElapsed = 0.0;
    mDirty = false;
}

void DemoPlayer::Tick(double dt)
{
    if (!mPlaying || !mAuto || mIndex < 0) return;
    // Don't start counting down a beat the host hasn't applied yet. Start()/Next() run in the
    // same frame as this Tick, and dt is the *previous* frame's delta (which can be huge after
    // a file dialog or a stall) -- without this a beat could be skipped before it ever showed.
    if (mDirty) return;
    mElapsed += dt;
    if (mElapsed < mScript.beats[mIndex].hold) return;
    if (mIndex + 1 < Count())
    {
        ++mIndex;
        mElapsed = 0.0;
        mDirty = true;
    }
    else
    {
        Stop();   // ran off the end of an auto playthrough
    }
}

void DemoPlayer::Next()
{
    if (!mPlaying) return;
    if (mIndex + 1 < Count())
    {
        ++mIndex;
        mElapsed = 0.0;
        mDirty = true;
    }
    else
    {
        Stop();
    }
}

void DemoPlayer::Prev()
{
    if (!mPlaying || mIndex <= 0) return;
    --mIndex;
    mElapsed = 0.0;
    mDirty = true;
}

const DemoBeat* DemoPlayer::Current() const
{
    if (mIndex < 0 || mIndex >= Count()) return nullptr;
    return &mScript.beats[mIndex];
}

double DemoPlayer::CurrentHold() const
{
    const DemoBeat* b = Current();
    return b ? b->hold : 0.0;
}

bool DemoPlayer::ConsumeDirty()
{
    const bool d = mDirty;
    mDirty = false;
    return d;
}

}  // namespace sfe
