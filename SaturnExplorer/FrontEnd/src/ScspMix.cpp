#include "ScspMix.h"

#include <algorithm>

namespace sfe
{
namespace
{
// Resampling is linear interpolation between neighbouring source samples. A preview does
// not need a windowed filter, and the alternative (nearest) audibly aliases on the
// low-rate voices that are common in Saturn music.
int16_t SampleAt(const ScspMixVoice& v, double pos)
{
    const size_t i = static_cast<size_t>(pos);
    if (i + 1 >= v.frames) return v.pcm[v.frames - 1];
    const double frac = pos - static_cast<double>(i);
    const double a = v.pcm[i], b = v.pcm[i + 1];
    return static_cast<int16_t>(a + (b - a) * frac);
}

// How a voice is read: its resampling step, and where it goes on reaching the end. All of
// it is fixed per voice, so the mix loop advances a position rather than re-deriving one
// (and a loop wrap) for every one of its millions of samples.
struct VoiceCursor
{
    double step = 1.0;       // source samples per output sample
    double end = 0.0;        // past this the voice has run out
    double loopEnd = 0.0;    // wrap target, when it loops
    double loopSpan = 0.0;   // 0 = does not loop: the voice simply stops

    VoiceCursor(const ScspMixVoice& v, uint32_t outRate)
    {
        step = static_cast<double>(v.rate) / static_cast<double>(outRate);
        end = static_cast<double>(v.frames);
        // LEA can point past what actually decoded (a sample running off the end of sound
        // RAM), so the loop is bounded by whichever came first.
        if (v.loopMode != 0 && v.loopEnd > v.loopStart && v.loopStart < v.frames)
        {
            loopEnd = static_cast<double>(std::min<uint32_t>(
                v.loopEnd, static_cast<uint32_t>(v.frames)));
            loopSpan = loopEnd - static_cast<double>(v.loopStart);
        }
    }

    // Bring 'pos' back into the loop after it has run past the end, or say the voice is
    // done. A while, not an if: a voice pitched above its loop length steps over the whole
    // loop in one output sample.
    bool Wrap(double& pos) const
    {
        if (loopSpan <= 0.0) return false;
        do { pos -= loopSpan; } while (pos >= loopEnd);
        return true;
    }
};
}  // namespace

int ScspVLevel(uint16_t egLevel, uint8_t totalLevel)
{
    const int v = static_cast<int>(egLevel) + (static_cast<int>(totalLevel) << 2);
    return v > 0x3FF ? 0x3FF : v;
}

int16_t ScspAttenuate(int16_t sample, int vlevel)
{
    if (vlevel < 0) vlevel = 0;
    if (vlevel > 0x3FF) vlevel = 0x3FF;
    const int mantissa = (vlevel & 0x3F) ^ 0x7F;
    return static_cast<int16_t>((static_cast<int>(sample) * mantissa) >> ((vlevel >> 6) + 7));
}

void ScspDirectVolume(uint8_t directLevel, uint8_t directPan, int& outLeft, int& outRight)
{
    const unsigned level = directLevel & 0x7u;
    const unsigned pan = directPan & 0x1Fu;
    const bool panWhich = (pan & 0x10u) != 0u;

    unsigned basev = level ? (0x80u << level) : 0u;
    unsigned panv = basev >> ((pan & 0x0Fu) >> 1);
    if (pan & 0x01u) panv -= (panv >> 2);
    if ((pan & 0x0Fu) == 0x0Fu) panv = 0u;

    // Index 0 is left. DIPAN bit 4 selects which side keeps the un-attenuated level, and
    // the panel reads that bit as "panned left" -- so a left-panned voice attenuates right.
    int vol[2];
    vol[panWhich ? 1 : 0] = static_cast<int>(panv);
    vol[panWhich ? 0 : 1] = static_cast<int>(basev);
    outLeft = vol[0];
    outRight = vol[1];
}

size_t ScspMixVoices(const ScspMixVoice* voices, size_t n, uint32_t outRate,
                     size_t maxFrames, std::vector<int16_t>& outStereo)
{
    outStereo.clear();
    if (!voices || n == 0 || outRate == 0 || maxFrames == 0) return 0;

    // How long the mix runs: the longest voice, so nothing is truncated. A looping voice
    // gets at least a second, otherwise a short sustain loop is a click next to a one-shot.
    size_t frames = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const ScspMixVoice& v = voices[i];
        if (!v.pcm || v.frames == 0 || v.rate == 0) continue;
        const double secs = static_cast<double>(v.frames) / static_cast<double>(v.rate);
        frames = std::max(frames, static_cast<size_t>(secs * outRate));
        if (v.loopMode != 0 && v.loopEnd > v.loopStart)
            frames = std::max(frames, static_cast<size_t>(outRate));
    }
    if (frames == 0) return 0;
    frames = std::min(frames, maxFrames);

    std::vector<int32_t> acc(frames * 2, 0);
    for (size_t i = 0; i < n; ++i)
    {
        const ScspMixVoice& v = voices[i];
        if (!v.pcm || v.frames == 0 || v.rate == 0) continue;

        int volL = 0, volR = 0;
        ScspDirectVolume(v.directLevel, v.directPan, volL, volR);
        if (volL == 0 && volR == 0) continue;   // DISDL 0: not sent to the DAC at all
        const int vlevel = ScspVLevel(v.egLevel, v.totalLevel);

        const VoiceCursor cur(v, outRate);
        double pos = 0.0;
        for (size_t f = 0; f < frames; ++f)
        {
            // Past the end: loop back, or stop and leave the rest of the mix as it stands.
            if (pos >= cur.end && !cur.Wrap(pos)) break;
            const int s = ScspAttenuate(SampleAt(v, pos), vlevel);
            acc[f * 2 + 0] += (s * volL) >> 14;   // 1.14 fixed point
            acc[f * 2 + 1] += (s * volR) >> 14;
            pos += cur.step;
        }
    }

    outStereo.resize(frames * 2);
    for (size_t i = 0; i < acc.size(); ++i)
    {
        const int32_t s = std::max(-32768, std::min(32767, acc[i]));
        outStereo[i] = static_cast<int16_t>(s);
    }
    return frames;
}

}  // namespace sfe
