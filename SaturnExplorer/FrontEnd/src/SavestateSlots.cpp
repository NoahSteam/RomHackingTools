#include "SavestateSlots.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sys/stat.h>

#include "Disc/PathUtil.h"    // BaseName
#include "SeLiveProtocol.h"   // SE_LIVE_STATE_KIND_*
#include "SeStateCodec.h"     // XOR + RLE codec (shared with the emulator exporter)
#include "Settings.h"

namespace sfe
{
namespace
{

// Slot file: "SESTATE1" + u32 version + u64 frame + u32 state length + the state image.
const char  kMagic[8] = { 'S', 'E', 'S', 'T', 'A', 'T', 'E', '1' };
const uint32_t kVersion = 1;
const size_t kHeaderLen = sizeof(kMagic) + 4 + 8 + 4;

void Put32(std::vector<uint8_t>& v, uint32_t x)
{
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>((x >> (i * 8)) & 0xFF));
}
void Put64(std::vector<uint8_t>& v, uint64_t x)
{
    for (int i = 0; i < 8; ++i) v.push_back(static_cast<uint8_t>((x >> (i * 8)) & 0xFF));
}
uint32_t Get32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t Get64(const uint8_t* p)
{
    uint64_t x = 0;
    for (int i = 7; i >= 0; --i) x = (x << 8) | p[i];
    return x;
}

// Anything that could confuse a file name, flattened. Keeps the name readable (so the
// states folder can be browsed by hand) without trusting the ROM's own name.
std::string SafeName(const std::string& in)
{
    std::string out;
    for (char c : in)
    {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == ' ';
        out.push_back(ok ? c : '_');
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? std::string("game") : out;
}

}  // namespace

std::string FormatLocalTime(uint64_t unixSeconds)
{
    if (unixSeconds == 0) return std::string();
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", tmv.tm_year + 1900,
                  tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    return buf;
}

void SavestateSlots::Reset()
{
    mKeyframe.clear();
    mKeyframeFrame = 0;
    mDelta.clear();
    mDeltaFrame = 0;
}

void SavestateSlots::OnBlock(uint64_t frame, uint8_t kind, uint64_t baseKeyframe,
                             uint32_t fullLen, const uint8_t* payload, size_t len)
{
    if (!payload || len == 0 || fullLen == 0) return;

    if (kind == SE_LIVE_STATE_KIND_KEYFRAME)
    {
        // Decode into a kept buffer: a fresh vector would zero-fill several megabytes on the
        // frame thread just to overwrite every byte, and keyframes arrive on every scene
        // change, not only on the forced interval.
        mScratch.resize(fullLen);
        if (se_state_rle_decode(mScratch.data(), mScratch.size(), payload, len) != fullLen) return;
        mKeyframe.swap(mScratch);
        mKeyframeFrame = frame;
        mDelta.clear();            // the newest state is now the keyframe itself
        mDeltaFrame = 0;
        return;
    }

    // A delta only means anything against the keyframe it was diffed from. After a rewind
    // (or a dropped block) an older base can still arrive; ignore it and wait for the next
    // keyframe rather than reconstructing something that never existed.
    if (mKeyframe.empty() || baseKeyframe != mKeyframeFrame) return;
    if (fullLen != mKeyframe.size()) return;
    mDelta.assign(payload, payload + len);
    mDeltaFrame = frame;
}

bool SavestateSlots::Latest(std::vector<uint8_t>& out, uint64_t& frame) const
{
    if (mKeyframe.empty()) return false;
    if (mDelta.empty())
    {
        out = mKeyframe;
        frame = mKeyframeFrame;
        return true;
    }
    // The delta is only kept when its full length matches the keyframe's (see OnBlock), so
    // the keyframe's size is the decode target.
    std::vector<uint8_t> delta(mKeyframe.size());
    if (se_state_rle_decode(delta.data(), delta.size(), mDelta.data(), mDelta.size()) !=
        mKeyframe.size())
    {
        return false;
    }
    out = mKeyframe;
    se_state_xor(out.data(), out.data(), delta.data(), out.size());
    frame = mDeltaFrame;
    return true;
}

std::string SavestateSlots::SlotPath(const std::string& romPath, int slot)
{
    if (slot < 0 || slot >= kSlotCount) return std::string();
    char suffix[24];
    std::snprintf(suffix, sizeof(suffix), ".slot%d.sestate", slot);
    return Settings::ConfigSubPath("states", SafeName(BaseName(romPath)) + suffix, false);
}

bool SavestateSlots::SlotExists(const std::string& romPath, int slot)
{
    const std::string path = SlotPath(romPath, slot);
    if (path.empty()) return false;
    struct stat st;
    return stat(path.c_str(), &st) == 0 && st.st_size > static_cast<off_t>(kHeaderLen);
}

std::string SavestateSlots::SlotLabel(const std::string& romPath, int slot)
{
    const std::string path = SlotPath(romPath, slot);
    if (path.empty()) return std::string();
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return std::string();

    // Read just the header for the frame number; the state itself can be megabytes.
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    uint8_t hdr[kHeaderLen];
    in.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
    if (!in || std::memcmp(hdr, kMagic, sizeof(kMagic)) != 0) return std::string();

    const uint64_t frame = Get64(hdr + sizeof(kMagic) + 4);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "frame %llu  -  ", static_cast<unsigned long long>(frame));
    return buf + FormatLocalTime(static_cast<uint64_t>(st.st_mtime));
}

bool SavestateSlots::SaveToSlot(const std::string& romPath, int slot, std::string& error) const
{
    std::vector<uint8_t> state;
    uint64_t frame = 0;
    if (!Latest(state, frame))
    {
        error = "No savestate available yet. The emulator sends one a few seconds after "
                "connecting; if this persists, its rewind support may be off.";
        return false;
    }
    char suffix[24];
    std::snprintf(suffix, sizeof(suffix), ".slot%d.sestate", slot);
    // create=true here (and only here): the states directory is made on first save.
    const std::string path =
        Settings::ConfigSubPath("states", SafeName(BaseName(romPath)) + suffix, true);
    if (path.empty())
    {
        error = "Could not resolve the Saturn Explorer config directory.";
        return false;
    }

    std::vector<uint8_t> header;
    header.insert(header.end(), kMagic, kMagic + sizeof(kMagic));
    Put32(header, kVersion);
    Put64(header, frame);
    Put32(header, static_cast<uint32_t>(state.size()));

    // Write to a temporary beside the slot and rename over it, so an interrupted save
    // leaves the previous state intact rather than a truncated file.
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) { error = "Could not open " + tmp; return false; }
        out.write(reinterpret_cast<const char*>(header.data()),
                  static_cast<std::streamsize>(header.size()));
        out.write(reinterpret_cast<const char*>(state.data()),
                  static_cast<std::streamsize>(state.size()));
        if (!out) { error = "Could not write " + tmp; return false; }
    }
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0)
    {
        error = "Could not replace " + path;
        std::remove(tmp.c_str());
        return false;
    }
    return true;
}

bool SavestateSlots::LoadFromSlot(const std::string& romPath, int slot,
                                  std::vector<uint8_t>& state, uint64_t& frame,
                                  std::string& error)
{
    const std::string path = SlotPath(romPath, slot);
    if (path.empty()) { error = "Could not build the slot path."; return false; }
    std::ifstream in(path, std::ios::binary);
    if (!in) { error = "Slot is empty."; return false; }

    uint8_t hdr[kHeaderLen];
    in.read(reinterpret_cast<char*>(hdr), sizeof(hdr));
    if (!in || std::memcmp(hdr, kMagic, sizeof(kMagic)) != 0)
    {
        error = "Not a Saturn Explorer save state: " + path;
        return false;
    }
    if (Get32(hdr + sizeof(kMagic)) != kVersion)
    {
        error = "Save state was written by a different version of Saturn Explorer.";
        return false;
    }
    frame = Get64(hdr + sizeof(kMagic) + 4);
    const uint32_t len = Get32(hdr + sizeof(kMagic) + 12);
    if (len == 0) { error = "Save state is empty."; return false; }
    state.resize(len);
    in.read(reinterpret_cast<char*>(state.data()), len);
    if (static_cast<uint32_t>(in.gcount()) != len)
    {
        error = "Save state is truncated.";
        return false;
    }
    return true;
}

}  // namespace sfe
