// SavestateSlots -- numbered save states for the live emulator.
//
// The emulator streams one savestate block per frame (a periodic keyframe, then deltas
// against it). FrameRecorder keeps those for rewind, but only for frames resident in its
// ring, which means only while recording. This class tracks the most recent state
// independently of that, so Save State works whenever a live emulator is attached, and
// writes it to a numbered slot file.
//
// A slot holds the emulator's own savestate image (Mednafen's MDFNSS data_only stream) plus
// the frame it came from, which the LST verb needs. It is not interchangeable with one of
// Mednafen's own .mcN files -- those carry a different wrapper.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sfe
{

// "2026-09-19 11:24" from a unix timestamp, or "" for 0. Shared so the two slot lists in
// the State menu -- Saturn Explorer's and the emulator's -- cannot drift into different
// date formats, and so the localtime_r/localtime_s shim exists once.
std::string FormatLocalTime(uint64_t unixSeconds);

class SavestateSlots
{
public:
    static const int kSlotCount = 10;

    // Feed every block the emulator sends. Blocks lag their frame by a few frames and a
    // delta is only usable with the keyframe it was diffed against, so a block whose base
    // is not the keyframe currently held is dropped -- the next keyframe re-syncs us.
    void OnBlock(uint64_t frame, uint8_t kind, uint64_t baseKeyframe,
                 uint32_t fullLen, const uint8_t* payload, size_t len);
    // Forget everything (disconnect, or a load that invalidates the stream).
    void Reset();

    // True once a full state can be reconstructed -- i.e. at least one keyframe has arrived.
    // Keyframes are forced every few hundred frames, so this goes true a few seconds in.
    bool HaveState() const { return !mKeyframe.empty(); }

    // Reconstruct the most recent full savestate and the frame it belongs to.
    bool Latest(std::vector<uint8_t>& out, uint64_t& frame) const;

    // <config>/states/<rom file name>.slotN.sestate. Empty if the config dir is unresolvable.
    static std::string SlotPath(const std::string& romPath, int slot);
    static bool SlotExists(const std::string& romPath, int slot);
    // "frame 12345  -  2026-09-19 11:24", or "" when the slot is empty. For the menu.
    static std::string SlotLabel(const std::string& romPath, int slot);

    // Write the latest state to a slot. False (with 'error' set) if no state is available
    // yet or the file could not be written.
    bool SaveToSlot(const std::string& romPath, int slot, std::string& error) const;
    // Read a slot back. False (with 'error' set) if it is missing or malformed.
    static bool LoadFromSlot(const std::string& romPath, int slot,
                             std::vector<uint8_t>& state, uint64_t& frame, std::string& error);

private:
    std::vector<uint8_t> mKeyframe;        // decoded full state of the keyframe we hold
    uint64_t             mKeyframeFrame = 0;
    std::vector<uint8_t> mDelta;           // RLE payload of the newest delta against it
    uint64_t             mDeltaFrame = 0;
    std::vector<uint8_t> mScratch;         // decode buffer, kept so keyframes don't realloc
};

}  // namespace sfe
