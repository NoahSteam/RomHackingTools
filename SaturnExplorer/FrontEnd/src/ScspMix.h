// ScspMix — mix the SCSP voices sounding on one frame into a single stereo preview.
//
// The Sound panel's per-voice Play previews one voice's raw sample; this is the same idea
// for the whole frame, so you can hear what a moment of the game is made of. Each sounding
// voice is resampled to a common rate, attenuated and panned the way the hardware would,
// and summed.
//
// What this is NOT: the emulator's actual output. The reconstruction is static -- every
// voice is played from the start of its sample with its envelope and pan frozen at the
// values this frame captured, rather than evolving as the EG and the sequencer drive them.
// The SCSP's DSP (reverb and the rest of the effect path, EFSDL/EFPAN) is not modelled at
// all, only the direct send. Treat it as "which sounds are in play here", not as a mixdown.
//
// The attenuation and pan arithmetic is transcribed from the emulator's own SCSP rather
// than from a dB formula, so the relative balance of the voices matches what the emulator
// is doing: see ScspAttenuate and ScspDirectVolume.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace sfe
{

// One sounding voice to fold into the mix: its decoded mono sample plus the register state
// that decides how loud, how fast and how far to either side it sits.
struct ScspMixVoice
{
    const int16_t* pcm = nullptr;   // decoded mono PCM (se_decode_scsp_sample)
    size_t         frames = 0;
    uint32_t       rate = 44100;    // the voice's natural rate from OCT/FNS

    uint16_t egLevel = 0;           // current envelope attenuation, 0 = loudest
    uint8_t  totalLevel = 0;        // TL
    uint8_t  directLevel = 7;       // DISDL (0 = muted)
    uint8_t  directPan = 0;         // DIPAN
    uint8_t  loopMode = 0;          // LPCTL: 0 none, 1 fwd, 2 rev, 3 alt
    uint32_t loopStart = 0;         // LSA, in samples
    uint32_t loopEnd = 0;           // LEA, in samples
};

// Combined 10-bit attenuation, 0 = loudest, saturating at 0x3FF (silence). The envelope and
// TL share one domain in the hardware, with TL scaled by 4 -- which is where the panel's
// 0.375 dB per TL step comes from (a 0x40 step in this domain is one 6 dB shift).
int ScspVLevel(uint16_t egLevel, uint8_t totalLevel);

// Apply an attenuation from ScspVLevel to one sample. This is the hardware's piecewise
// mantissa/shift approximation of a log curve, not a multiply by a dB-derived float, so a
// mix built on it keeps the same relative balance the emulator produces.
int16_t ScspAttenuate(int16_t sample, int vlevel);

// DISDL + DIPAN -> per-channel volumes in 1.14 fixed point (0x4000 = unity). DISDL 0 is
// silence, and each step up doubles; DIPAN attenuates whichever channel it points away
// from, muting it entirely at the extreme. Left is index 0.
void ScspDirectVolume(uint8_t directLevel, uint8_t directPan, int& outLeft, int& outRight);

// Peak the normalised mix is scaled to: just under full scale, so a preview is always at a
// usable listening level whatever the game's own mix was doing.
const int kScspMixTargetPeak = 29500;

// Mix 'n' voices into interleaved stereo at 'outRate'. Returns the frame count written to
// 'outStereo' (which is resized to frames*2). When 'outPeak' is non-null it receives the
// mix's peak BEFORE normalisation, on the 32767 scale -- the frame's true level, which the
// caller can report since normalisation otherwise hides it.
//
// Length is the longest voice, so nothing is cut off; a voice that loops is repeated to
// fill that length rather than clicking off early, which matters because a sustained
// instrument's loop is often only a few hundred samples. Reverse and alternating loops are
// played forwards -- a preview distinction not worth the machinery.
//
// The result is peak-normalised. Voice levels (TL) and sends span a huge range -- a voice
// sitting at -32 dB in the game's mix is perfectly normal -- so a faithful absolute level
// makes a quiet moment inaudible even though every voice in it is real and individually
// audible. Normalising preserves what the mix is actually for: the balance between the
// voices, their panning, and which of them dominates. It also means a dense frame no longer
// has to clip to fit.
size_t ScspMixVoices(const ScspMixVoice* voices, size_t n, uint32_t outRate,
                     size_t maxFrames, std::vector<int16_t>& outStereo,
                     int* outPeak = nullptr);

}  // namespace sfe
