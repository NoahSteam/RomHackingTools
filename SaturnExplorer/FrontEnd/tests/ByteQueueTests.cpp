// ByteQueue's whole reason for existing is that its byte total must track its contents: the
// live driver bounds its queues in bytes, and every path that adds or removes an entry has to
// keep the total honest or the bound stops meaning anything. The socket-level test
// (LiveReconnectTests) can only reach the filling half, because a server that never answers
// never lets the driver drain — so the accounting is pinned here instead, in both directions.
#include "ByteQueue.h"

#include <cstdio>
#include <deque>
#include <vector>

namespace {

int gFailures = 0;

void Check(bool ok, const char* what, int line)
{
    if (ok) return;
    std::printf("CHECK failed at line %d: %s\n", line, what);
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

using Q = sfe::ByteQueue<std::vector<uint8_t>>;

std::vector<uint8_t> Item(size_t n) { return std::vector<uint8_t>(n, 0xAB); }

// An entry of 'n' bytes that says which one it is, so a test can tell the survivors of a trim
// from the casualties -- identical entries cannot distinguish dropping the front from the back.
std::vector<uint8_t> Tagged(size_t n, uint8_t tag) { return std::vector<uint8_t>(n, tag); }

// Push counts bytes, refuses what does not fit, and a refusal changes nothing.
void TestPushRespectsTheBudget()
{
    Q q;
    CHECK(q.Empty());
    CHECK(q.bytes == 0);

    CHECK(q.Push(Item(60), 100));
    CHECK(q.bytes == 60);
    CHECK(!q.Empty());

    CHECK(!q.Push(Item(41), 100));          // 60 + 41 > 100
    CHECK(q.bytes == 60);                   // a refused push queues nothing
    CHECK(q.q.size() == 1);

    CHECK(q.Push(Item(40), 100));           // exactly fills it
    CHECK(q.bytes == 100);
    CHECK(!q.Push(Item(1), 100));           // and now nothing more fits

    // A budget of 0 refuses everything with a payload, but an empty entry still fits.
    Q z;
    CHECK(!z.Push(Item(1), 0));
    CHECK(z.Push(Item(0), 0));
}

// Popping gives the bytes back, so a queue that drains can accept work again. Without this the
// budget is a one-way ratchet: the queue fills once and refuses forever after.
void TestPopReturnsTheBudget()
{
    Q q;
    CHECK(q.Push(Item(70), 100));
    CHECK(q.Push(Item(30), 100));
    CHECK(q.bytes == 100);

    const std::vector<uint8_t> first = q.Pop();
    CHECK(first.size() == 70);              // FIFO: the oldest comes out
    CHECK(q.bytes == 30);
    CHECK(q.Push(Item(70), 100));           // the freed budget is usable again
    CHECK(q.bytes == 100);

    while (!q.Empty()) q.Pop();
    CHECK(q.bytes == 0);                    // draining fully returns to zero, not a residue
}

// TrimTo drops the oldest until the total fits, and never empties the queue entirely.
void TestTrimToDropsOldest()
{
    Q q;
    for (uint8_t tag = 1; tag <= 5; ++tag) q.PushEvicting(Tagged(30, tag));
    CHECK(q.bytes == 150);
    CHECK(q.q.size() == 5);

    q.TrimTo(100);
    CHECK(q.bytes == 90);                   // 3 x 30 is the most that fits
    CHECK(q.q.size() == 3);
    // The newest three survive: entries 1 and 2 were the oldest and went first. Dropping from
    // the other end would leave a rewind history with its most recent frames missing.
    CHECK(q.q[0][0] == 3 && q.q[1][0] == 4 && q.q[2][0] == 5);

    // One oversized entry survives its own bound: dropping it would lose the newest arrival.
    Q big;
    big.PushEvicting(Item(500));
    big.TrimTo(100);
    CHECK(big.q.size() == 1);
    CHECK(big.bytes == 500);

    q.TrimTo(0);
    CHECK(q.q.size() == 1);
    CHECK(q.bytes == 30);                   // the total still describes what is left
    CHECK(q.q[0][0] == 5);                  // and the one kept is the newest
}

// Drain hands everything over and resets the total. Forgetting that reset is the bug this
// replaced: the count used to be zeroed by hand at the drain site, a third place to remember.
// The destination is cleared rather than swapped into, so the reset cannot be wrong.
void TestDrainResetsTheTotal()
{
    Q q;
    CHECK(q.Push(Item(40), 100));
    CHECK(q.Push(Item(20), 100));

    std::deque<std::vector<uint8_t>> taken;
    taken.push_back(Item(9));               // a non-empty destination: its contents are dropped
    q.Drain(taken);

    CHECK(taken.size() == 2);
    CHECK(taken[0].size() == 40 && taken[1].size() == 20);
    CHECK(q.Empty());
    CHECK(q.bytes == 0);
    CHECK(q.Push(Item(100), 100));          // the whole budget is available again
}

}  // namespace

int main()
{
    TestPushRespectsTheBudget();
    TestPopReturnsTheBudget();
    TestTrimToDropsOldest();
    TestDrainResetsTheTotal();
    if (gFailures)
    {
        std::printf("ByteQueueTests: %d check(s) failed\n", gFailures);
        return 1;
    }
    std::printf("ByteQueueTests: all checks passed\n");
    return 0;
}
