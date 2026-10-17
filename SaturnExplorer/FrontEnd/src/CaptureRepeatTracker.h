// Which captures of a live context showed nothing new (A1-7).
//
// se_derive_serial moves on every se_begin_frame, whether or not the emulator published anything
// different: a paused emulator that App keeps capturing (a state load nobody has confirmed, a step
// settling, a session catching up) moves it every UI frame over the same bytes. A consumer that keys
// a result on the serial -- the SH-2 Pseudocode panel, whose LivePaused view follows every move at
// once -- then asks again each frame, and a function that takes longer than a frame is superseded
// before it can be shown.
//
// The live driver can say when two captures are byte-identical (se_live_captured_revision: it moves
// exactly when a published snapshot differs from the one before). This class turns that into a
// serial for consumers that care about content: the raw serial minus the number of captures that
// repeated the one before. It is injective on the states the raw serial tells apart, because
//   - a capture that repeats moves the raw serial by one and the count by one, so the result does
//     not move;
//   - everything else (a capture with new content, an in-place edit) moves the raw serial by at
//     least one and the count by none, so the result moves;
//   - a capture is counted only when nothing else re-derived the context since the previous one
//     (an edit in between wrote bytes the capture then replaced: that is a change of the view, not
//     a repeat of it).
// The count is kept for the life of the context and never lowered: lowering it would let a later
// state take an earlier one's value.
//
// No ImGui, no driver: pure arithmetic, so the model test can drive it without a socket.
#pragma once

#include <cstdint>

namespace sfe
{

class CaptureRepeatTracker
{
public:
    // One capture of 'ctx'. 'serialBefore' and 'serialAfter' are the context's derive serial read
    // immediately around the capture; 'revision' is the captured snapshot's revision read right
    // after it (0 when the source cannot say, which never counts as a repeat).
    void Note(const void* ctx, uint64_t serialBefore, uint64_t serialAfter, uint64_t revision)
    {
        // A capture re-derives exactly once. Anything else (the capture failed and left the context
        // as it was) says nothing about the content on screen: leave the record alone.
        if (serialAfter != serialBefore + 1) return;
        if (!SameContext(ctx, serialBefore))
            mRepeats = 0;
        else if (revision != 0 && revision == mRevision && serialBefore == mSerialAfter)
            ++mRepeats;
        mCtx = ctx;
        mSerialAfter = serialAfter;
        mRevision = revision;
    }

    // The serial a content-keyed consumer should see for the context's raw serial now. Anything
    // not yet seen by Note (another context, a serial of another range) passes through unchanged.
    uint64_t Apply(const void* ctx, uint64_t serial) const
    {
        return SameContext(ctx, serial) ? serial - mRepeats : serial;
    }

    uint64_t Repeats() const { return mRepeats; }

private:
    // A context's serials start in a range of their own (Context::NextSerialRange: the high 32
    // bits), so a new context allocated where an old one was is told apart by its range.
    bool SameContext(const void* ctx, uint64_t serial) const
    {
        return mCtx && ctx == mCtx && (serial >> 32) == (mSerialAfter >> 32);
    }

    const void* mCtx = nullptr;
    uint64_t mSerialAfter = 0;   // the context's serial when the last capture finished
    uint64_t mRevision = 0;
    uint64_t mRepeats = 0;
};

}  // namespace sfe
