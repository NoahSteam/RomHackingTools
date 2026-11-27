// ScrubBar -- which recorded entry the transport's scrub slider shows, and the label on it, kept
// apart from App so the transitions that used to leave it stale can be unit-tested.
//
// The slider has two states. While the user is scrubbing, the selected index is what is shown. Any
// other time (live, or paused with nothing scrubbed -- after Play, or after Play From Here) the bar
// rests on the newest recorded entry and the selected index means nothing: it is a leftover of an
// earlier scrub. Reading it anyway made the label and handle sit on an old index (#14133 775 / 903)
// while the Profiler, which asks only "is a scrub view up", showed the newest frame.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace sfe
{

// The recorded index the slider's handle and label show. -1 when nothing is recorded.
//  - not scrubbing: always the newest entry, whatever 'selected' holds;
//  - scrubbing: 'selected', or the newest when it is out of range (the ring evicted past it).
inline int ScrubBarIndex(bool scrubbing, int selected, int count)
{
    if (count <= 0) return -1;
    if (!scrubbing) return count - 1;
    return (selected >= 0 && selected < count) ? selected : count - 1;
}

// The index a step-back moves to: one before what the slider shows, so stepping back from a
// resting bar goes to the entry before the newest, not before an old selection. -1 when empty.
inline int ScrubBarStepBackIndex(bool scrubbing, int selected, int count)
{
    const int at = ScrubBarIndex(scrubbing, selected, count);
    return at > 0 ? at - 1 : at;
}

// Leaving the scrub view (Play, Play From Here, step, or no longer paused) forgets the selection,
// so nothing can read a stale one once recording resumes and the ring grows, evicts or is truncated.
inline void ScrubBarLeave(bool& scrubbing, int& selected)
{
    scrubbing = false;
    selected = -1;
}

// "#<frame>   <position> / <count>" for the entry at 'index' whose frame number is 'frame'.
inline std::string ScrubBarLabel(uint64_t frame, int index, int count)
{
    char buf[64];
    std::snprintf(buf, sizeof(buf), "#%llu   %d / %d", static_cast<unsigned long long>(frame), index + 1, count);
    return buf;
}

}  // namespace sfe
