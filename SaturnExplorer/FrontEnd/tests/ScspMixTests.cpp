// ScspMix: the SCSP attenuation / pan laws and the frame mixer behind the Sound panel's
// "Play Frame". Pure -- no context, no audio device.
//
// The laws are transcribed from the emulator's own SCSP, so the reference implementations
// below are deliberately literal copies of that arithmetic: the tests are checking the
// transcription, and a paraphrase would just re-make any mistake in it.

#include <cmath>
#include <cstdio>
#include <iostream>
#include <vector>

#include "ScspMix.h"

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
#define CHECK(expr) Check((expr), #expr, __LINE__)

// scsp.inc: SDL_PAN_ToVolume.
void RefDirectVolume(unsigned level, unsigned pan, int& l, int& r)
{
    const bool panWhich = (bool)(pan & 0x10);
    unsigned basev = (0x80 << level);
    if (!level) basev = 0;
    unsigned panv = basev >> ((pan & 0x0F) >> 1);
    if (pan & 0x01) panv -= (panv >> 2);
    if ((pan & 0x0F) == 0x0F) panv = 0;
    int vol[2];
    vol[panWhich] = (int)panv;
    vol[!panWhich] = (int)basev;
    l = vol[0]; r = vol[1];
}

// scsp.inc: the sample scaling in the slot loop.
int RefAttenuate(int sample, int vlevel)
{
    return (sample * ((vlevel & 0x3F) ^ 0x7F)) >> ((vlevel >> 6) + 7);
}

void TestAttenuationMatchesHardware()
{
    // Every attenuation, against a spread of sample values including the extremes.
    const int samples[] = { 0, 1, -1, 100, -100, 12345, -12345, 32767, -32768 };
    for (int vlevel = 0; vlevel <= 0x3FF; ++vlevel)
        for (int s : samples)
            if (ScspAttenuate((int16_t)s, vlevel) != (int16_t)RefAttenuate(s, vlevel))
            {
                std::cerr << "attenuate mismatch vlevel=" << vlevel << " sample=" << s << '\n';
                ++gFailures;
                return;
            }

    // Out-of-range attenuations are clamped rather than shifting by a wild amount.
    CHECK(ScspAttenuate(1000, -5) == ScspAttenuate(1000, 0));
    CHECK(ScspAttenuate(1000, 0x7FF) == ScspAttenuate(1000, 0x3FF));
    // Full attenuation is silence; none is (near) unity.
    CHECK(ScspAttenuate(20000, 0x3FF) == 0);
    CHECK(ScspAttenuate(20000, 0) > 19000);
}

void TestVLevelCombinesEnvelopeAndTotalLevel()
{
    CHECK(ScspVLevel(0, 0) == 0);                 // loudest
    CHECK(ScspVLevel(0x100, 0) == 0x100);         // envelope alone
    CHECK(ScspVLevel(0, 1) == 4);                 // TL is scaled by 4 into the same domain
    CHECK(ScspVLevel(0, 64) == 256);
    CHECK(ScspVLevel(0x10, 4) == 0x20);           // and they add
    CHECK(ScspVLevel(0x3FF, 255) == 0x3FF);       // saturates, never wraps
    CHECK(ScspVLevel(0x3FF, 0) == 0x3FF);
    CHECK(ScspVLevel(0x3F0, 8) == 0x3FF);

    // A 0x40 step in this domain is one shift, i.e. 6 dB -- which is the 0.375 dB per TL
    // step the panel displays (TL scaled by 4: 4/64 * 6 dB).
    const int16_t a = ScspAttenuate(16384, ScspVLevel(0, 0));
    const int16_t b = ScspAttenuate(16384, ScspVLevel(0, 64));   // +0x100 => 4 shifts
    CHECK(b > 0 && a / b >= 15 && a / b <= 17);
}

void TestDirectVolumeMatchesHardware()
{
    for (unsigned level = 0; level < 8; ++level)
        for (unsigned pan = 0; pan < 32; ++pan)
        {
            int l = 0, r = 0, rl = 0, rr = 0;
            ScspDirectVolume((uint8_t)level, (uint8_t)pan, l, r);
            RefDirectVolume(level, pan, rl, rr);
            if (l != rl || r != rr)
            {
                std::cerr << "pan mismatch level=" << level << " pan=" << pan
                          << " got " << l << "," << r << " want " << rl << "," << rr << '\n';
                ++gFailures;
                return;
            }
        }

    int l = 0, r = 0;
    // DISDL 0 is silence on both sides -- the voice is not sent to the DAC at all.
    ScspDirectVolume(0, 0, l, r);
    CHECK(l == 0 && r == 0);
    // Centre pan is equal; level 7 is unity in 1.14.
    ScspDirectVolume(7, 0, l, r);
    CHECK(l == r && l == 0x4000);
    // Each level step doubles.
    int l6 = 0, r6 = 0;
    ScspDirectVolume(6, 0, l6, r6);
    CHECK(l6 * 2 == l);
    // Bit 4 set reads as "panned left" in the panel, so the right side is the attenuated
    // one. This is the orientation a mix can silently get backwards.
    ScspDirectVolume(7, 0x10 | 0x0F, l, r);
    CHECK(l == 0x4000 && r == 0);
    ScspDirectVolume(7, 0x0F, l, r);
    CHECK(r == 0x4000 && l == 0);
}

// A voice built over a constant sample value, so mixing maths is easy to reason about.
std::vector<int16_t> Const(size_t n, int16_t v) { return std::vector<int16_t>(n, v); }

ScspMixVoice MakeVoice(const std::vector<int16_t>& pcm, uint32_t rate)
{
    ScspMixVoice v;
    v.pcm = pcm.data();
    v.frames = pcm.size();
    v.rate = rate;
    v.directLevel = 7;
    v.directPan = 0;
    return v;
}

void TestMixEmptyAndDegenerate()
{
    std::vector<int16_t> out;
    CHECK(ScspMixVoices(nullptr, 0, 44100, 1000, out) == 0);
    CHECK(out.empty());

    std::vector<int16_t> pcm = Const(100, 1000);
    ScspMixVoice v = MakeVoice(pcm, 44100);
    CHECK(ScspMixVoices(&v, 1, 0, 1000, out) == 0);        // no output rate
    CHECK(ScspMixVoices(&v, 1, 44100, 0, out) == 0);       // no room

    // A voice with no sample contributes nothing and must not be mistaken for a mix.
    ScspMixVoice empty;
    empty.rate = 44100;
    CHECK(ScspMixVoices(&empty, 1, 44100, 1000, out) == 0);
}

void TestMixLengthAndStereoLayout()
{
    std::vector<int16_t> shortPcm = Const(4410, 1000);    // 0.1s at 44100
    std::vector<int16_t> longPcm = Const(8820, 1000);     // 0.2s at 44100
    ScspMixVoice v[2] = { MakeVoice(shortPcm, 44100), MakeVoice(longPcm, 44100) };

    std::vector<int16_t> out;
    const size_t frames = ScspMixVoices(v, 2, 44100, 44100 * 4, out);
    CHECK(frames == 8820);                 // the longest voice, nothing truncated
    CHECK(out.size() == frames * 2);       // interleaved stereo

    // The cap bounds the result.
    const size_t capped = ScspMixVoices(v, 2, 44100, 1000, out);
    CHECK(capped == 1000);
    CHECK(out.size() == 2000);
}

void TestResamplingStretchesByRateRatio()
{
    // A voice at half the output rate lasts twice as many output frames.
    std::vector<int16_t> pcm = Const(1000, 500);
    ScspMixVoice v = MakeVoice(pcm, 22050);
    std::vector<int16_t> out;
    const size_t frames = ScspMixVoices(&v, 1, 44100, 44100 * 4, out);
    CHECK(frames == 2000);
}

void TestLoopingVoiceFillsTheMix()
{
    // A short looping voice against a long one-shot: without loop handling the loop would
    // stop after a few ms and the tail would be silent on its account.
    std::vector<int16_t> loopPcm = Const(441, 4000);     // 10ms
    std::vector<int16_t> onePcm = Const(44100, 4000);    // 1s
    ScspMixVoice v[2] = { MakeVoice(loopPcm, 44100), MakeVoice(onePcm, 44100) };
    v[0].loopMode = 1;
    v[0].loopStart = 0;
    v[0].loopEnd = 441;

    std::vector<int16_t> out;
    const size_t frames = ScspMixVoices(v, 2, 44100, 44100 * 4, out);
    CHECK(frames == 44100);

    // Both voices are constant and centre-panned, so every frame should carry both. Sample
    // near the end, well past the loop's own 10ms.
    const int16_t early = out[10 * 2];
    const int16_t late = out[(frames - 10) * 2];
    CHECK(late == early);

    // And with looping off the tail drops to one voice's worth.
    v[0].loopMode = 0;
    ScspMixVoices(v, 2, 44100, 44100 * 4, out);
    CHECK(out[(frames - 10) * 2] < early);
}

void TestLoopFloorGivesShortLoopsRoom()
{
    // Only short looping voices: the mix still runs long enough to hear them sustain.
    std::vector<int16_t> pcm = Const(441, 3000);
    ScspMixVoice v = MakeVoice(pcm, 44100);
    v.loopMode = 1;
    v.loopStart = 0;
    v.loopEnd = 441;
    std::vector<int16_t> out;
    CHECK(ScspMixVoices(&v, 1, 44100, 44100 * 4, out) == 44100);   // one second
}

void TestPanPlacesVoicesOnTheRightSide()
{
    std::vector<int16_t> pcm = Const(4410, 8000);
    ScspMixVoice v = MakeVoice(pcm, 44100);
    v.directPan = 0x10 | 0x0F;   // hard left
    std::vector<int16_t> out;
    ScspMixVoices(&v, 1, 44100, 44100 * 4, out);
    CHECK(out[0] != 0);          // left carries it
    CHECK(out[1] == 0);          // right silent

    v.directPan = 0x0F;          // hard right
    ScspMixVoices(&v, 1, 44100, 44100 * 4, out);
    CHECK(out[0] == 0);
    CHECK(out[1] != 0);
}

void TestSilentVoicesAreSkipped()
{
    std::vector<int16_t> pcm = Const(4410, 8000);
    ScspMixVoice quiet = MakeVoice(pcm, 44100);
    quiet.directLevel = 0;       // not sent to the DAC
    std::vector<int16_t> out;
    ScspMixVoices(&quiet, 1, 44100, 44100 * 4, out);
    CHECK(out[0] == 0 && out[1] == 0);

    // A fully-released envelope is likewise inaudible.
    ScspMixVoice released = MakeVoice(pcm, 44100);
    released.egLevel = 0x3FF;
    ScspMixVoices(&released, 1, 44100, 44100 * 4, out);
    CHECK(out[0] == 0 && out[1] == 0);
}

void TestMixClipsInsteadOfWrapping()
{
    // Many loud centre voices must saturate, not wrap to the opposite sign -- the failure
    // that turns a dense frame into a burst of noise.
    std::vector<int16_t> pcm = Const(1000, 32767);
    std::vector<ScspMixVoice> v;
    for (int i = 0; i < 24; ++i) v.push_back(MakeVoice(pcm, 44100));
    std::vector<int16_t> out;
    const size_t frames = ScspMixVoices(v.data(), v.size(), 44100, 44100 * 4, out);
    CHECK(frames == 1000);
    for (size_t i = 0; i < out.size(); ++i)
        if (out[i] < 0) { std::cerr << "wrapped at " << i << ": " << out[i] << '\n'; ++gFailures; break; }
    CHECK(out[0] == 32767);

    std::vector<int16_t> neg = Const(1000, -32768);
    std::vector<ScspMixVoice> nv;
    for (int i = 0; i < 24; ++i) nv.push_back(MakeVoice(neg, 44100));
    ScspMixVoices(nv.data(), nv.size(), 44100, 44100 * 4, out);
    CHECK(out[0] == -32768);
}
}  // namespace

int main()
{
    TestAttenuationMatchesHardware();
    TestVLevelCombinesEnvelopeAndTotalLevel();
    TestDirectVolumeMatchesHardware();
    TestMixEmptyAndDegenerate();
    TestMixLengthAndStereoLayout();
    TestResamplingStretchesByRateRatio();
    TestLoopingVoiceFillsTheMix();
    TestLoopFloorGivesShortLoopsRoom();
    TestPanPlacesVoicesOnTheRightSide();
    TestSilentVoicesAreSkipped();
    TestMixClipsInsteadOfWrapping();
    if (gFailures)
    {
        std::cerr << gFailures << " check(s) failed\n";
        return 1;
    }
    std::cout << "ScspMix tests passed\n";
    return 0;
}
