// Every reader and writer of emulated memory must agree on which byte is which.
//
// The bugs this guards against were each in one stage of a pipeline whose other stages were right,
// so each stage's own test passed: RGB888 CRAM captured in Mednafen's split storage order, the VDP1
// frame buffer exported host-order and poked big-endian, a savestate importer pairing two colours'
// halves, a palette decoder reading [R][G][B] where the bus holds [B][G][R], VDP2 VRAM pokes that
// never reached the renderer's copy. Here the whole chain runs, end to end:
//
//   fake Mednafen storage --(real apply.py accessors + real glue)--> real se_export server
//     --(socket)--> real LiveDriver --> real core context --> se_read_vram / palette decode
//   and back: se_write_vram --> LiveDriver --> server --> glue --> apply.py pokes --> storage
//   and sideways: the same storage written as a Mednafen savestate --> SavestateDriver,
//   and a recorded frame --> FrameRecorder --> staged edit --> Play From Here replay.
//
// Only Mednafen's storage is faked, and it is faked the way Mednafen holds it: host-order uint16
// arrays, RGB888 CRAM split into high and low halves, VDP2 VRAM duplicated in a renderer copy that
// only the renderer-aware writers update. The ground truth every path is checked against is BusRead
// -- what the SH-2 reads at a bus address -- transcribed from Mednafen's own read paths (ne16_rbo_be
// on the fast map, vdp2.cpp's RW<>() for CRAM), and colours are checked against vdp2_render.cpp's
// CacheCRE. The accessor code is compiled from apply.py itself (emit_mednafen_accessors.py), so the
// test exercises what is injected, not a copy of it.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"
#include "LiveDriver.h"
#include "SavestateDriver.h"
#include "FrameRecorder.h"
#include "ScrubState.h"
#include "se_export.h"

extern "C" void SeMednafenFrameHook(void);
extern "C" void SeMednafenEndFrameHook(void);

// ------------------------------------------------------------------------------------------------
// Mednafen's storage, and the apply.py accessors compiled against it.
// ------------------------------------------------------------------------------------------------
typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;

namespace MDFN_IEN_SS
{
// Mednafen's ne16_*_be helpers: big-endian byte order inside each host-order 16-bit word.
template <typename T> static inline void ne16_wbo_be(uint16* base, uint32 off, T v)
{
    static_assert(sizeof(T) == 1, "byte writes only");
    const unsigned sh = ((off & 1) ^ 1) * 8;
    uint16& w = base[off >> 1];
    w = static_cast<uint16>((w & ~(0xFF << sh)) | (static_cast<unsigned>(v) << sh));
}
static inline uint8 RdBe8(const uint16* base, uint32 off)
{
    return static_cast<uint8>(base[off >> 1] >> (((off & 1) ^ 1) * 8));
}

static uint16 WorkRAML[0x80000];
static uint16 WorkRAMH[0x80000];
static uint16 SoundRAM[0x40000];

namespace VDP1
{
uint16 VRAM[0x40000];
uint16 FB[2][0x20000];
bool FBDrawWhich = false;
uint16 TVMR, FBCR, PTMR, EWDR, EWLR, EWRR, EDSR, LOPR;
#include "vdp1_accessors.inc"
#include "vdp1_poke.inc"
}  // namespace VDP1

namespace VDP2
{
enum { CRAM_MODE_RGB555_1024 = 0, CRAM_MODE_RGB555_2048 = 1, CRAM_MODE_RGB888_1024 = 2, CRAM_MODE_ILLEGAL = 3 };
static uint8 CRAM_Mode;
static uint16 CRAM[2048];
static uint16 VRAM[0x40000];
static uint16 RawRegs[0x100];
// vdp2_render.cpp's private copies, fed only through VDP2REND_Write16_DB.
static uint16 RendVRAM[0x40000];
static uint16 RendCRAM[2048];

// vdp2.cpp's Write16_DB: the renderer's copy first, then RW<uint16, true>().
uint32 Write16_DB(uint32 A, uint16 DB)
{
    A &= 0x1FFFFF;
    if (A < 0x100000)
    {
        VRAM[(A & 0x7FFFF) >> 1] = DB;
        RendVRAM[(A & 0x7FFFF) >> 1] = DB;
    }
    else if (A < 0x180000)
    {
        const unsigned cri = (A & 0xFFF) >> 1;
        switch (CRAM_Mode)
        {
        case CRAM_MODE_RGB555_1024:
            (CRAM + 0x000)[cri & 0x3FF] = (CRAM + 0x400)[cri & 0x3FF] = DB;
            (RendCRAM + 0x000)[cri & 0x3FF] = (RendCRAM + 0x400)[cri & 0x3FF] = DB;
            break;
        case CRAM_MODE_RGB555_2048: CRAM[cri] = RendCRAM[cri] = DB; break;
        default:
            CRAM[((cri >> 1) & 0x3FF) | ((cri & 1) << 10)] = DB;
            RendCRAM[((cri >> 1) & 0x3FF) | ((cri & 1) << 10)] = DB;
            break;
        }
    }
    return 0;
}
// vdp2.cpp's PokeVRAM: the array, then the renderer's copy of the word.
void PokeVRAM(uint32 addr, const uint8 val)
{
    addr &= 0x7FFFF;
    ne16_wbo_be<uint8>(VRAM, addr, val);
    RendVRAM[addr >> 1] = VRAM[addr >> 1];
}
#include "vdp2_accessors.inc"
#include "vdp2_poke.inc"
}  // namespace VDP2

// ss.cpp's CheatMemWrite over the fast map (SS_SetPhysMemMap calls in ss.cpp, vdp1.cpp, vdp2.cpp,
// sound.cpp). Anything unmapped -- CRAM, the frame buffer -- is dropped, as in Mednafen.
static uint16* FastMap(uint32 A, uint32* off)
{
    A &= (1U << 27) - 1;
    if (A >= 0x00200000u && A < 0x00400000u) { *off = A & 0xFFFFF; return WorkRAML; }
    if (A >= 0x06000000u && A < 0x08000000u) { *off = A & 0xFFFFF; return WorkRAMH; }
    if (A >= 0x05C00000u && A < 0x05C80000u) { *off = A & 0x7FFFF; return VDP1::VRAM; }
    if (A >= 0x05E00000u && A < 0x05F00000u) { *off = A & 0x7FFFF; return VDP2::VRAM; }
    if (A >= 0x05A00000u && A < 0x05A80000u) { *off = A & 0x7FFFF; return SoundRAM; }
    return nullptr;
}
static void CheatMemWrite(uint32 A, uint8 V)
{
    uint32 off = 0;
    if (uint16* base = FastMap(A, &off)) ne16_wbo_be<uint8>(base, off, V);
}
extern "C" void SsDbgVdp1LatchPokeByte(unsigned int off, unsigned char val);   // as ss.cpp declares it
#include "ss_poke.inc"
}  // namespace MDFN_IEN_SS

using namespace MDFN_IEN_SS;

// The rest of the glue's accessor surface, none of which these tests depend on.
extern "C" {
const unsigned short* SsDbgWramL(void) { return WorkRAML; }
const unsigned short* SsDbgWramH(void) { return WorkRAMH; }
const unsigned short* SsDbgSoundRam(void) { return SoundRAM; }
int SsDbgScspSlots(unsigned char*) { return 0; }
int SsDbgCdStatus(unsigned char*) { return 0; }
void SsDbgSh2Regs(int, unsigned int o[23]) { std::memset(o, 0, 23 * sizeof(unsigned int)); }
void SsDbgAddExecBp(int, unsigned int) {}
void SsDbgAddMemBp(int, unsigned int, unsigned int, unsigned int) {}
void SsDbgClearBps(void) {}
void SsDbgSetTraceActive(int) {}
void SsDbgSetPad(unsigned int, unsigned int) {}
unsigned short SsDbgReadOpcode(unsigned int) { return 0x0009; }
int SsDbgQueryKeyMap(unsigned int, int out[13]) { for (int i = 0; i < 13; ++i) out[i] = -1; return 0; }
const char* SsDbgPortDeviceName(unsigned int) { return ""; }
int SsDbgEmuSlotInfo(unsigned, unsigned long long*) { return 0; }
int SsDbgEmuLoadSlot(unsigned) { return -1; }

// A full state: every array this test models, in a fixed order.
struct Blob { void* p; size_t n; };
static std::vector<Blob> StateParts()
{
    return { { VDP1::VRAM, sizeof VDP1::VRAM }, { VDP1::FB, sizeof VDP1::FB },
             { &VDP1::FBDrawWhich, sizeof VDP1::FBDrawWhich },
             { VDP2::VRAM, sizeof VDP2::VRAM }, { VDP2::CRAM, sizeof VDP2::CRAM },
             { VDP2::RawRegs, sizeof VDP2::RawRegs }, { &VDP2::CRAM_Mode, sizeof VDP2::CRAM_Mode },
             { VDP2::RendVRAM, sizeof VDP2::RendVRAM }, { VDP2::RendCRAM, sizeof VDP2::RendCRAM },
             { WorkRAML, sizeof WorkRAML }, { WorkRAMH, sizeof WorkRAMH }, { SoundRAM, sizeof SoundRAM } };
}
size_t SsDbgSaveState(unsigned char* buf, size_t cap)
{
    size_t total = 0;
    for (const Blob& b : StateParts()) total += b.n;
    if (!buf) return total;
    if (cap < total) return 0;
    for (const Blob& b : StateParts()) { std::memcpy(buf, b.p, b.n); buf += b.n; }
    return total;
}
std::atomic<int> gLoads{ 0 };
int SsDbgLoadState(const unsigned char* buf, size_t len)
{
    if (len != SsDbgSaveState(nullptr, 0)) return -1;
    for (const Blob& b : StateParts()) { std::memcpy(b.p, buf, b.n); buf += b.n; }
    ++gLoads;
    return 0;
}
}  // extern "C"

namespace
{
int gFailures = 0;
void Check(bool ok, const std::string& what)
{
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++gFailures; }
}
void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// ------------------------------------------------------------------------------------------------
// Ground truth: what the SH-2 reads at a bus address.
// ------------------------------------------------------------------------------------------------
struct Region
{
    se_vram_kind kind;
    uint32_t     bus;    // bus address of offset 0
    uint32_t     size;
    const char*  name;
};
const Region kRegions[] = {
    { SE_VRAM_KIND_VDP1_VRAM, 0x05C00000u, 0x80000u,  "VDP1 VRAM" },
    { SE_VRAM_KIND_VDP1_FB,   0x05C80000u, 0x40000u,  "VDP1 FB" },
    { SE_VRAM_KIND_VDP2_VRAM, 0x05E00000u, 0x80000u,  "VDP2 VRAM" },
    { SE_VRAM_KIND_CRAM,      0x05F00000u, 0x1000u,   "CRAM" },
    { SE_VRAM_KIND_WRAM_LOW,  0x00200000u, 0x100000u, "work RAM low" },
    { SE_VRAM_KIND_WRAM_HIGH, 0x06000000u, 0x100000u, "work RAM high" },
    { SE_VRAM_KIND_SOUND_RAM, 0x05A00000u, 0x80000u,  "sound RAM" },
};

uint8_t BusRead(uint32_t A)
{
    A &= (1U << 27) - 1;
    if (A >= 0x05F00000u && A < 0x05F80000u)
    {
        // vdp2.cpp RW<uint16, false>(): RGB555 reads CRAM[cri]; RGB888 (and the illegal mode)
        // reads the split storage.
        const unsigned cri = (A & 0xFFF) >> 1;
        const uint16 w = (VDP2::CRAM_Mode < 2) ? VDP2::CRAM[cri]
                                               : VDP2::CRAM[((cri >> 1) & 0x3FF) | ((cri & 1) << 10)];
        return static_cast<uint8_t>((A & 1) ? w : (w >> 8));
    }
    if (A >= 0x05C80000u && A < 0x05CC0000u)   // the displayed bank, which the snapshot shows
        return RdBe8(VDP1::FB[!VDP1::FBDrawWhich], A & 0x3FFFF);
    uint32 off = 0;
    if (const uint16* base = FastMap(A, &off)) return RdBe8(base, off);
    return 0;
}

std::vector<uint8_t> BusImage(const Region& r)
{
    std::vector<uint8_t> v(r.size);
    for (uint32_t i = 0; i < r.size; ++i) v[i] = BusRead(r.bus + i);
    return v;
}

std::vector<uint8_t> ClientImage(se_context* ctx, const Region& r)
{
    std::vector<uint8_t> v(r.size, 0);
    size_t got = 0;
    while (got < r.size)
    {
        const size_t n = se_read_vram(ctx, r.kind, static_cast<uint32_t>(got), v.data() + got,
                                      std::min<size_t>(0x10000, r.size - got));
        if (n == 0) break;
        got += n;
    }
    if (got != r.size) v.clear();
    return v;
}

std::string FirstDiff(const std::vector<uint8_t>& want, const std::vector<uint8_t>& got)
{
    if (got.size() != want.size()) return "size " + std::to_string(got.size()) + " vs " + std::to_string(want.size());
    for (size_t i = 0; i < want.size(); ++i)
        if (want[i] != got[i])
        {
            char b[96];
            std::snprintf(b, sizeof b, "offset 0x%zX: got %02X, want %02X", i, got[i], want[i]);
            return b;
        }
    return std::string();
}

// vdp2_render.cpp CacheCRE: the colour Mednafen draws for CRAM entry 'k', from its own storage.
struct Rgb { uint8_t r, g, b; };
Rgb MednafenColor(unsigned k)
{
    if (VDP2::CRAM_Mode & VDP2::CRAM_MODE_RGB888_1024)
    {
        const uint32_t c = ((VDP2::CRAM[k & 0x3FF] & 0x80FFu) << 16) | VDP2::CRAM[0x400 + (k & 0x3FF)];
        return { static_cast<uint8_t>(c), static_cast<uint8_t>(c >> 8), static_cast<uint8_t>(c >> 16) };
    }
    const uint16 t = VDP2::CRAM[k & (VDP2::CRAM_Mode == VDP2::CRAM_MODE_RGB555_1024 ? 0x3FF : 0x7FF)];
    // The core widens 5 bits as x * 255 / 31 (full scale), where Mednafen shifts; same 5-bit value.
    auto w = [](unsigned x) { return static_cast<uint8_t>(x * 255 / 31); };
    return { w(t & 0x1F), w((t >> 5) & 0x1F), w((t >> 10) & 0x1F) };
}

// ------------------------------------------------------------------------------------------------
// The emulator side's frame loop, and fills.
// ------------------------------------------------------------------------------------------------
void EmulatorTick()
{
    SeExportGateFrame();
    SeMednafenFrameHook();
    SeMednafenEndFrameHook();
}
bool Until(const std::function<bool()>& pred, int ticks = 800)
{
    for (int i = 0; i < ticks; ++i)
    {
        EmulatorTick();
        if (pred()) return true;
        Sleep(3);
    }
    return false;
}

uint32_t gRng = 0x12345678u;
uint16 Next16()
{
    gRng ^= gRng << 13; gRng ^= gRng >> 17; gRng ^= gRng << 5;
    return static_cast<uint16>(gRng);
}
void FillAll()
{
    for (uint16& w : WorkRAML) w = Next16();
    for (uint16& w : WorkRAMH) w = Next16();
    for (uint16& w : SoundRAM) w = Next16();
    for (uint16& w : VDP1::VRAM) w = Next16();
    for (auto& bank : VDP1::FB) for (uint16& w : bank) w = Next16();
    for (unsigned i = 0; i < 0x40000; ++i) VDP2::VRAM[i] = VDP2::RendVRAM[i] = Next16();
    for (unsigned i = 0; i < 2048; ++i) VDP2::CRAM[i] = Next16();
}
// RAMCTL and Mednafen's derived mode, as a VDP2 register write sets them.
void SetCramMode(unsigned mode)
{
    VDP2::RawRegs[0x0E >> 1] = static_cast<uint16>(mode << 12);
    VDP2::CRAM_Mode = static_cast<uint8>(mode);
    // RGB555 1024 keeps both halves in step (every write goes to both); a game in that mode never
    // has them differ.
    if (mode == 0) for (unsigned i = 0; i < 0x400; ++i) VDP2::CRAM[0x400 + i] = VDP2::CRAM[i];
    std::memcpy(VDP2::RendCRAM, VDP2::CRAM, sizeof VDP2::CRAM);
}

// The client's view equals the bus, region by region, once a capture taken after 'since' is in.
void CheckCaptureMatchesBus(se_context* ctx, const std::string& when)
{
    for (const Region& r : kRegions)
    {
        const std::vector<uint8_t> want = BusImage(r);
        std::string diff;
        const bool ok = Until([&] {
            se_begin_frame(ctx);
            diff = FirstDiff(want, ClientImage(ctx, r));
            return diff.empty();
        }, 300);
        Check(ok, when + ": " + r.name + " as captured is the bus's view (" + diff + ")");
    }
}

void CheckColorsMatchMednafen(se_context* ctx, const std::string& when)
{
    // CRMD 3 is a prohibited setting. Mednafen draws it as RGB888 (CacheCRE tests the mode's RGB888
    // bit) while the core decodes it as RGB555, as it always has; its bytes are still checked.
    if (VDP2::CRAM_Mode == VDP2::CRAM_MODE_ILLEGAL) return;
    const unsigned count = VDP2::CRAM_Mode == VDP2::CRAM_MODE_RGB555_2048 ? 2048 : 1024;
    std::vector<se_palette_entry> got(count);
    Check(se_read_cram_colors(ctx, 0, static_cast<uint16_t>(count), got.data()) == count,
          when + ": every CRAM colour decodes");
    for (unsigned k = 0; k < count; ++k)
    {
        const Rgb want = MednafenColor(k);
        if (got[k].r != want.r || got[k].g != want.g || got[k].b != want.b)
        {
            char b[160];
            std::snprintf(b, sizeof b, "%s: CRAM colour %u is %02X%02X%02X, Mednafen draws %02X%02X%02X",
                          when.c_str(), k, got[k].r, got[k].g, got[k].b, want.r, want.g, want.b);
            Check(false, b);
            return;
        }
    }
}

// Offsets worth poking in a region: both ends, both bytes of a word, and a spread between.
std::vector<uint32_t> PokeOffsets(uint32_t size)
{
    std::vector<uint32_t> v = { 0, 1, 2, 3, size / 2, size / 2 + 1, size - 2, size - 1 };
    for (int i = 0; i < 8; ++i) v.push_back((Next16() * 2654435761u) % size);
    return v;
}

// ------------------------------------------------------------------------------------------------
// A Mednafen savestate of the current storage, as MDFNSS writes it on a little-endian host.
// ------------------------------------------------------------------------------------------------
void Put32LE(std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back(uint8_t(x >> (8 * i))); }
void AddField(std::vector<uint8_t>& sec, const char* name, const void* p, size_t n)
{
    const uint8_t len = static_cast<uint8_t>(std::strlen(name));
    sec.push_back(len);
    sec.insert(sec.end(), name, name + len);
    Put32LE(sec, static_cast<uint32_t>(n));
    const uint8_t* b = static_cast<const uint8_t*>(p);
    sec.insert(sec.end(), b, b + n);   // host order: uint16 arrays little-endian here
}
void AddSection(std::vector<uint8_t>& f, const char* name, const std::vector<uint8_t>& sec)
{
    const size_t at = f.size();
    f.resize(at + 32, 0);
    std::memcpy(&f[at], name, std::strlen(name));
    Put32LE(f, static_cast<uint32_t>(sec.size()));
    f.insert(f.end(), sec.begin(), sec.end());
}
std::vector<uint8_t> MednafenSavestate()
{
    std::vector<uint8_t> f(32, 0);
    std::memcpy(f.data(), "MDFNSVST", 8);   // endian word at 20 = 0: written little-endian
    std::vector<uint8_t> v1, v2, main;
    AddField(v1, "VRAM", VDP1::VRAM, sizeof VDP1::VRAM);
    AddField(v2, "RawRegs", VDP2::RawRegs, sizeof VDP2::RawRegs);
    AddField(v2, "VRAM", VDP2::VRAM, sizeof VDP2::VRAM);
    AddField(v2, "CRAM", VDP2::CRAM, sizeof VDP2::CRAM);
    AddField(main, "WorkRAML", WorkRAML, sizeof WorkRAML);
    AddField(main, "WorkRAMH", WorkRAMH, sizeof WorkRAMH);
    AddSection(f, "MAIN", main);
    AddSection(f, "VDP1", v1);
    AddSection(f, "VDP2", v2);
    return f;
}
}  // namespace

int main()
{
    {
        const uint16 probe = 1;
        if (*reinterpret_cast<const uint8_t*>(&probe) != 1)
        {
            // The glue's swaps assume a little-endian emulator host; so does this test's model.
            std::printf("skipped: big-endian host\n");
            return 0;
        }
    }

    FillAll();
    SetCramMode(0);
    SeMednafenFrameHook();   // the glue's first call starts the server and installs its hooks

    se_data_source ds{};
    se_result r = SE_ERR_IO;
    for (int i = 0; i < 400 && r != SE_OK; ++i)
    {
        EmulatorTick();
        r = se_live_open(SE_LIVE_DEFAULT_SOCK_PATH, &ds);
        if (r != SE_OK) Sleep(5);
    }
    if (r != SE_OK) { std::printf("could not open the live source\n"); return 1; }
    se_live_set_rewind_enabled(&ds, 0);   // no per-frame savestates: nothing here needs the ring
    se_config cfg{};
    cfg.abi_version = SE_ABI_VERSION;
    se_context* ctx = se_create(&ds, &cfg);
    if (!ctx) { std::printf("se_create failed\n"); return 1; }
    Check(Until([&] { return se_begin_frame(ctx) == SE_OK && se_live_server_version(&ds) >= 23u; }),
          "the live source attaches to a v23+ server");
    {
        uint32_t applied = 0, dropped = 0, lost = 0, unconfirmed = 0, caps = 0;
        Check(Until([&] {
                  se_live_poke_info(&ds, &applied, &dropped, &lost, &unconfirmed, &caps);
                  return (caps & SE_LIVE_CAP_VDP_POKE) != 0;
              }),
              "the glue's VDP writer is advertised");
    }

    for (unsigned mode = 0; mode < 4; ++mode)
    {
        const std::string m = "CRAM mode " + std::to_string(mode);
        FillAll();
        SetCramMode(mode);
        VDP1::FBDrawWhich = (mode & 1) != 0;   // both displayed-bank selections get a turn
        // The snapshot shows VDP1 VRAM from the draw-end latch once a draw has completed, and from
        // live VRAM before the first one: both get a turn.
        if (mode & 1) VDP1::SsDbgVdp1LatchDrawEnd(); else VDP1::SsDbgVdp1LatchInvalidate();

        // 1. Live capture: every byte of every region is the byte the bus holds there.
        CheckCaptureMatchesBus(ctx, m + ", live");
        Check(se_get_cram_mode(ctx) == (mode == 1 ? SE_CRAM_RGB555_2048
                                        : mode == 2 ? SE_CRAM_RGB888_1024 : SE_CRAM_RGB555_1024),
              m + ": the client reads the mode from RAMCTL");
        // 2. Colours: what the core decodes is what Mednafen draws.
        CheckColorsMatchMednafen(ctx, m + ", live");

        // 3. Live pokes: each lands on exactly the byte poked, for every region.
        for (const Region& reg : kRegions)
        {
            std::vector<uint8_t> want = BusImage(reg);
            for (uint32_t off : PokeOffsets(reg.size))
            {
                const uint8_t v = static_cast<uint8_t>(want[off] ^ 0x5A);
                if (se_write_vram(ctx, reg.kind, off, &v, 1) != 1)
                {
                    Check(false, m + ": " + reg.name + " poke at " + std::to_string(off) + " is accepted");
                    continue;
                }
                want[off] = v;
                // In RGB555 1024 a write lands in both halves of the storage, so the bus view at the
                // mirrored offset changes with it.
                if (reg.kind == SE_VRAM_KIND_CRAM && mode == 0) want[off ^ 0x800] = v;
            }
            // Before any capture, the view already shows what the emulator will hold: an edit the
            // hardware applies to more than one byte (CRAM's mirrored halves) shows on all of them.
            std::string diff = FirstDiff(want, ClientImage(ctx, reg));
            Check(diff.empty(), m + ": " + reg.name + " pokes show in the view as the emulator applies them (" + diff + ")");
            const bool landed = Until([&] { diff = FirstDiff(want, BusImage(reg)); return diff.empty(); });
            Check(landed, m + ": " + reg.name + " pokes change exactly the poked bytes (" + diff + ")");
        }
        // 3b. One CRAM write covering a byte AND its mirror, with different data at each: the whole
        //     of CRAM, and a run straddling the halves. The hardware applies it in address order, so
        //     in RGB555 1024 the later half's bytes win in both halves.
        {
            const Region& reg = kRegions[3];   // CRAM
            struct Span { uint32_t off, len; };
            for (const Span sp : { Span{ 0, 0x1000 }, Span{ 0x400, 0x800 }, Span{ 0x7F0, 0x20 } })
            {
                std::vector<uint8_t> want = BusImage(reg);
                std::vector<uint8_t> data(sp.len);
                for (uint32_t i = 0; i < sp.len; ++i) data[i] = static_cast<uint8_t>(i * 7 + (sp.off + i) / 0x800 * 0x80 + 1);
                const std::string what = m + ": a CRAM write of " + std::to_string(sp.len) + " at " + std::to_string(sp.off);
                if (se_write_vram(ctx, SE_VRAM_KIND_CRAM, sp.off, data.data(), sp.len) != sp.len)
                {
                    Check(false, what + " is accepted");
                    continue;
                }
                for (uint32_t i = 0; i < sp.len; ++i)
                {
                    want[sp.off + i] = data[i];
                    if (mode == 0) want[(sp.off + i) ^ 0x800] = data[i];
                }
                std::string diff = FirstDiff(want, ClientImage(ctx, reg));
                Check(diff.empty(), what + " shows in the view as the emulator applies it (" + diff + ")");
                const bool landed = Until([&] { diff = FirstDiff(want, BusImage(reg)); return diff.empty(); });
                Check(landed, what + " lands as the hardware applies it (" + diff + ")");
            }
        }
        Check(std::memcmp(VDP2::RendVRAM, VDP2::VRAM, sizeof VDP2::VRAM) == 0,
              m + ": VDP2 VRAM pokes reach the renderer's copy");
        Check(std::memcmp(VDP2::RendCRAM, VDP2::CRAM, sizeof VDP2::CRAM) == 0,
              m + ": CRAM pokes reach the renderer's copy");
        // ...and the next capture shows the same bytes the bus now holds.
        CheckCaptureMatchesBus(ctx, m + ", after pokes");
        CheckColorsMatchMednafen(ctx, m + ", after pokes");

        // 4. A Mednafen savestate of the same machine reads back the same bytes and colours.
        {
            const std::vector<uint8_t> state = MednafenSavestate();
            se_data_source sds{};
            Check(se_savestate_open_buffer(state.data(), state.size(), &sds) == SE_OK, m + ": the savestate opens");
            se_context* sctx = se_create(&sds, &cfg);
            Check(sctx && se_begin_frame(sctx) == SE_OK, m + ": and captures");
            if (sctx)
            {
                for (const Region& reg : kRegions)
                {
                    // A Mednafen state's frame buffer and sound RAM are not imported.
                    if (reg.kind == SE_VRAM_KIND_VDP1_FB || reg.kind == SE_VRAM_KIND_SOUND_RAM) continue;
                    const std::string diff = FirstDiff(BusImage(reg), ClientImage(sctx, reg));
                    Check(diff.empty(), m + ", savestate: " + reg.name + " is the bus's view (" + diff + ")");
                }
                CheckColorsMatchMednafen(sctx, m + ", savestate");
                se_destroy(sctx);
            }
        }

        // 5. A recorded frame shows the same bytes; an edit staged on it and replayed by Play From
        //    Here lands where a live poke at that offset does.
        {
            sfe::FrameRecorder rec;
            rec.Configure(4);
            sfe::StagedEdits staged;
            struct Sink { sfe::StagedEdits* s; } sink{ &staged };
            rec.SetEditSink(&sink, [](void* u, int isSound, uint32_t addr, const uint8_t* b, size_t n) {
                static_cast<Sink*>(u)->s->Record(1, isSound != 0, addr, b, n);
            });
            se_begin_frame(ctx);
            const std::vector<uint8_t> restoreTo(SsDbgSaveState(nullptr, 0));
            std::vector<uint8_t> state(restoreTo.size());
            SsDbgSaveState(state.data(), state.size());
            rec.Capture(ctx, 1);
            for (int i = 0; i < 400 && rec.Count() == 0; ++i) Sleep(5);
            se_data_source rds{};
            Check(rec.Count() == 1 && rec.Select(0, &rds), m + ": the frame is recorded");
            se_context* rctx = se_create(&rds, &cfg);
            Check(rctx && se_begin_frame(rctx) == SE_OK, m + ": and opens");
            if (rctx)
            {
                for (const Region& reg : kRegions)
                {
                    const std::string diff = FirstDiff(BusImage(reg), ClientImage(rctx, reg));
                    Check(diff.empty(), m + ", recorded frame: " + reg.name + " is the bus's view (" + diff + ")");
                }
                // Stage one edit per region on the recorded frame.
                std::vector<std::vector<uint8_t>> want;
                for (const Region& reg : kRegions)
                {
                    std::vector<uint8_t> img = BusImage(reg);
                    const uint32_t off = (Next16() * 40503u) % reg.size;
                    const uint8_t v = static_cast<uint8_t>(img[off] ^ 0xC3);
                    Check(se_write_vram(rctx, reg.kind, off, &v, 1) == 1,
                          m + ": a " + reg.name + " edit on the recorded frame is accepted");
                    img[off] = v;
                    if (reg.kind == SE_VRAM_KIND_CRAM && mode == 0) img[off ^ 0x800] = v;
                    want.push_back(std::move(img));
                }
                // An offset past its region is refused, not staged into the next window.
                {
                    const uint8_t v = 0x11;
                    const size_t before = staged.Pokes().size();
                    Check(rds.write_vram(rds.user, SE_VRAM_KIND_VDP1_VRAM, 0x200000u, &v, 1) == 0 &&
                          rds.write_vram(rds.user, SE_VRAM_KIND_CRAM, 0xFFFu, &v, 2) == 0 &&
                          staged.Pokes().size() == before,
                          m + ": a recorded-frame edit past its region is refused");
                }
                se_destroy(rctx);
                // Move the emulator on, then Play From Here: restore the recorded state + the edits.
                for (uint16& w : WorkRAMH) w ^= 0xFFFF;
                const int loadsBefore = gLoads.load();
                const std::vector<uint8_t> blob = sfe::EncodeEditBlob(staged);
                Check(se_load_state(ctx, 1, state.data(), state.size(), blob.data(), blob.size()) == SE_OK,
                      m + ": Play From Here is accepted");
                Check(Until([&] { return gLoads.load() == loadsBefore + 1; }), m + ": the state is restored");
                size_t i = 0;
                for (const Region& reg : kRegions)
                {
                    const std::string diff = FirstDiff(want[i++], BusImage(reg));
                    Check(diff.empty(), m + ", Play From Here: " + reg.name +
                                        " is the recorded frame plus exactly its staged edit (" + diff + ")");
                }
                Check(std::memcmp(VDP2::RendVRAM, VDP2::VRAM, sizeof VDP2::VRAM) == 0,
                      m + ": a replayed VDP2 VRAM edit reaches the renderer's copy");
                // Pokes are refused until the restore settles; let it, before the next mode.
                const uint8_t z = 0;
                Check(Until([&] { se_begin_frame(ctx); return ds.write_main_ram(ds.user, 0x06000000u, &z, 1) == 1; }),
                      m + ": editing resumes once the restore has settled");
            }
        }
    }

    se_destroy(ctx);
    SeExportDeinit();
    if (gFailures) { std::printf("%d memory round-trip check(s) failed\n", gFailures); return 1; }
    std::printf("memory round trip: all checks passed\n");
    return 0;
}
