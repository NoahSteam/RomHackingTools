// SavestateSlots: block tracking (keyframe + delta reconstruction) and the slot file format.
// Covers the logic behind Save State without needing an emulator or a window.

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "SavestateSlots.h"
#include "SeLiveProtocol.h"
#include "SeStateCodec.h"

using namespace sfe;

namespace
{
int gFailures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
    ++gFailures;
}

// A state image with enough zero runs that the RLE has something to do, seeded so two
// "frames" differ in a handful of bytes the way real consecutive savestates do.
std::vector<uint8_t> MakeState(size_t n, unsigned seed)
{
    std::vector<uint8_t> v(n, 0);
    for (size_t i = 0; i < n; i += 37) v[i] = static_cast<uint8_t>((i * 31 + seed) & 0xFF);
    return v;
}

std::vector<uint8_t> Rle(const std::vector<uint8_t>& src)
{
    std::vector<uint8_t> out(src.size() * 2 + 64);
    const size_t n = se_state_rle_encode(out.data(), out.size(), src.data(), src.size());
    out.resize(n);
    return out;
}

std::vector<uint8_t> Xor(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b)
{
    std::vector<uint8_t> out(a.size());
    for (size_t i = 0; i < a.size(); ++i) out[i] = a[i] ^ b[i];
    return out;
}
}  // namespace

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

namespace
{

void TestKeyframeIsTheLatestState()
{
    SavestateSlots slots;
    CHECK(!slots.HaveState());

    const std::vector<uint8_t> full = MakeState(4096, 1);
    const std::vector<uint8_t> payload = Rle(full);
    slots.OnBlock(100, SE_LIVE_STATE_KIND_KEYFRAME, 100,
                  static_cast<uint32_t>(full.size()), payload.data(), payload.size());

    CHECK(slots.HaveState());
    std::vector<uint8_t> out;
    uint64_t frame = 0;
    CHECK(slots.Latest(out, frame));
    CHECK(frame == 100);
    CHECK(out == full);
}

void TestDeltaReconstructsAgainstItsKeyframe()
{
    SavestateSlots slots;
    const std::vector<uint8_t> base = MakeState(4096, 1);
    const std::vector<uint8_t> later = MakeState(4096, 2);
    const std::vector<uint8_t> kfPayload = Rle(base);
    const std::vector<uint8_t> dPayload = Rle(Xor(later, base));
    const uint32_t fullLen = static_cast<uint32_t>(base.size());

    slots.OnBlock(100, SE_LIVE_STATE_KIND_KEYFRAME, 100, fullLen,
                  kfPayload.data(), kfPayload.size());
    slots.OnBlock(107, SE_LIVE_STATE_KIND_DELTA, 100, fullLen,
                  dPayload.data(), dPayload.size());

    std::vector<uint8_t> out;
    uint64_t frame = 0;
    CHECK(slots.Latest(out, frame));
    CHECK(frame == 107);
    CHECK(out == later);   // keyframe XOR delta, not the keyframe
}

void TestDeltaForAnotherKeyframeIsIgnored()
{
    // After a rewind, blocks diffed against an older keyframe can still arrive. Applying one
    // would reconstruct a state that never existed, so it must be dropped rather than XORed
    // onto whatever keyframe happens to be held.
    SavestateSlots slots;
    const std::vector<uint8_t> base = MakeState(4096, 1);
    const std::vector<uint8_t> kfPayload = Rle(base);
    const std::vector<uint8_t> stale = Rle(MakeState(4096, 9));
    const uint32_t fullLen = static_cast<uint32_t>(base.size());

    slots.OnBlock(200, SE_LIVE_STATE_KIND_KEYFRAME, 200, fullLen,
                  kfPayload.data(), kfPayload.size());
    slots.OnBlock(205, SE_LIVE_STATE_KIND_DELTA, 100 /* a different keyframe */, fullLen,
                  stale.data(), stale.size());

    std::vector<uint8_t> out;
    uint64_t frame = 0;
    CHECK(slots.Latest(out, frame));
    CHECK(frame == 200);
    CHECK(out == base);
}

void TestResetDropsEverything()
{
    SavestateSlots slots;
    const std::vector<uint8_t> full = MakeState(1024, 3);
    const std::vector<uint8_t> payload = Rle(full);
    slots.OnBlock(5, SE_LIVE_STATE_KIND_KEYFRAME, 5,
                  static_cast<uint32_t>(full.size()), payload.data(), payload.size());
    CHECK(slots.HaveState());
    slots.Reset();
    CHECK(!slots.HaveState());
    std::vector<uint8_t> out;
    uint64_t frame = 0;
    CHECK(!slots.Latest(out, frame));
}

void TestSlotFileRoundTrip()
{
    // Writes into the real config dir, so use a ROM name no game would produce and clean up.
    const std::string rom = "/tmp/__se_selftest_rom__.cue";
    const std::string path = SavestateSlots::SlotPath(rom, 3);
    if (path.empty())
    {
        std::cerr << "note: no resolvable config dir; skipping the slot-file round trip\n";
        return;
    }

    SavestateSlots slots;
    const std::vector<uint8_t> full = MakeState(8192, 7);
    const std::vector<uint8_t> payload = Rle(full);
    slots.OnBlock(4242, SE_LIVE_STATE_KIND_KEYFRAME, 4242,
                  static_cast<uint32_t>(full.size()), payload.data(), payload.size());

    CHECK(!SavestateSlots::SlotExists(rom, 3));
    std::string error;
    CHECK(slots.SaveToSlot(rom, 3, error));
    CHECK(error.empty());
    CHECK(SavestateSlots::SlotExists(rom, 3));
    CHECK(!SavestateSlots::SlotLabel(rom, 3).empty());

    std::vector<uint8_t> back;
    uint64_t frame = 0;
    CHECK(SavestateSlots::LoadFromSlot(rom, 3, back, frame, error));
    CHECK(frame == 4242);
    CHECK(back == full);

    // A slot never written reads as empty rather than erroring out oddly.
    std::vector<uint8_t> none;
    uint64_t noFrame = 0;
    std::string missing;
    CHECK(!SavestateSlots::LoadFromSlot(rom, 4, none, noFrame, missing));
    CHECK(!missing.empty());

    std::remove(path.c_str());
}

void TestSaveWithoutAStateFails()
{
    SavestateSlots slots;
    std::string error;
    CHECK(!slots.SaveToSlot("/tmp/__se_selftest_rom__.cue", 0, error));
    CHECK(!error.empty());
}

}  // namespace

int main()
{
    TestKeyframeIsTheLatestState();
    TestDeltaReconstructsAgainstItsKeyframe();
    TestDeltaForAnotherKeyframeIsIgnored();
    TestResetDropsEverything();
    TestSlotFileRoundTrip();
    TestSaveWithoutAStateFails();
    if (gFailures != 0)
    {
        std::cerr << gFailures << " savestate slot check(s) failed\n";
        return 1;
    }
    std::cout << "Savestate slot tests passed\n";
    return 0;
}
