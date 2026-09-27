// ByteQueue — a FIFO that knows its own size in bytes.
//
// Every queue in the live driver holds entries whose sizes differ by orders of magnitude: a
// savestate keyframe is megabytes where a delta is a few hundred bytes, and a memory poke is
// anything from one byte to a whole region. An entry count therefore says nothing about the
// memory held, so these queues have to be bounded in bytes — and the running total is the part
// that drifts when it lives at the call sites rather than inside the container.
//
// The admission policy stays with the caller, because it genuinely differs between queues:
// Push refuses work that would exceed the budget, PushEvicting + TrimTo drop the oldest until
// it fits. Which is right depends on whether losing the newest or the oldest entry costs more.
//
// An entry's size comes from ByteSizeOf, found by ordinary overload resolution at the point of
// use; this header supplies the std::vector<uint8_t> overload, and a translation unit queueing
// its own type declares one for it.
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace sfe
{

inline size_t ByteSizeOf(const std::vector<uint8_t>& v) { return v.size(); }

template <typename T>
struct ByteQueue
{
    std::deque<T> q;
    size_t        bytes = 0;

    bool Empty() const { return q.empty(); }

    // Queue 'item' unless it would put the total over 'budget'. False means nothing was
    // queued, so the caller can report the work as not accepted.
    bool Push(T&& item, size_t budget)
    {
        const size_t n = ByteSizeOf(item);
        // The bytes > budget arm is what makes 'budget - bytes' safe. No caller mixes Push with
        // TrimTo or varies the budget between calls, so the queue cannot currently be over its
        // budget on entry -- but a queue whose bound silently inverts into "accept everything"
        // when it is, is not worth having.
        if (bytes > budget || n > budget - bytes)
        {
            return false;
        }
        bytes += n;
        q.push_back(std::move(item));
        return true;
    }

    // Queue unconditionally; pair with TrimTo to bound the queue after the fact.
    void PushEvicting(T&& item)
    {
        bytes += ByteSizeOf(item);
        q.push_back(std::move(item));
    }

    // Drop the oldest entries until the total fits 'budget', always keeping one: a queue
    // emptied by its own bound would throw away the newest arrival, which is the one a
    // too-large entry most likely is.
    void TrimTo(size_t budget)
    {
        while (q.size() > 1 && bytes > budget)
        {
            bytes -= ByteSizeOf(q.front());
            q.pop_front();
        }
    }

    // The oldest entry, moved out. Only call on a non-empty queue.
    T Pop()
    {
        T out = std::move(q.front());
        q.pop_front();
        bytes -= ByteSizeOf(out);
        return out;
    }

    // Move the whole queue into 'out', leaving this one empty. 'out' is cleared first: the
    // total is reset to zero here, which is only true if nothing comes back the other way, so
    // this is a drain and not a swap however cheap a swap would look.
    void Drain(std::deque<T>& out)
    {
        out.clear();
        out.swap(q);
        bytes = 0;
    }
};

}  // namespace sfe
