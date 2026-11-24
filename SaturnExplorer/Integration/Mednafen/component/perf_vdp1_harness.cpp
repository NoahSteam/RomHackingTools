// F4 component test: the REAL patched Mednafen vdp1.cpp (VDP1::Update / DoDrawing and its resumable
// command loop), driven with a tiny VRAM command list. Only the devices around it are stubbed: the
// event scheduler, the SCU interrupt line, VDP2, the command handlers (each returns a fixed cost, so
// the expected figures are exact) and the DMA-hold predicate. A stubbed DoDrawing would prove nothing,
// so vdp1.cpp is #included, not mocked; the harness therefore also reaches its file-scope statics.
//
// Built and run by Integration/Mednafen/check_component.py, which patches a scratch copy of the
// pristine Mednafen source with the current apply.py and defines SE_VDP1_CPP.
#include SE_VDP1_CPP

#include <cstdio>
#include <cstring>

extern "C" { unsigned long long SeSsPerfC[SE_PERF_COUNT]; unsigned int SeSsCacheMode; }

namespace MDFN_IEN_SS
{
uint32 ss_horrible_hacks;
int32 SH7095_mem_timestamp;
event_list_entry events[SS_EVENT__COUNT];
void SS_SetEventNT(event_list_entry*, const sscpu_timestamp_t) {}
void SS_SetPhysMemMap(uint32, uint32, uint16*, uint32, bool) {}
void SCU_SetInt(unsigned, bool) {}
static bool gDmaHold;
bool SCU_CheckVDP1HaltKludge(void) { return gDmaHold; }
namespace VDP2 { sscpu_timestamp_t Update(sscpu_timestamp_t t) { return t; } }
namespace VDP1
{
// Each drawing command costs a fixed number of cycles; a sprite's resume phase costs 40 once.
static int gSpriteCost = 400;
static int gResumeLeft = 0;
int32 CMD_NormalSprite(const uint16*) { gResumeLeft = 1; return gSpriteCost; }
int32 RESUME_Sprite(const uint16*) { if (gResumeLeft) { --gResumeLeft; return 40; } return 0; }
int32 CMD_ScaledSprite(const uint16*) { return 0; }
int32 CMD_DistortedSprite(const uint16*) { return 0; }
int32 CMD_Polygon(const uint16*) { return 0; }
int32 CMD_Line(const uint16*) { return 0; }
int32 RESUME_Polygon(const uint16*) { return 0; }
int32 RESUME_Line(const uint16*) { return 0; }
}
}

using namespace MDFN_IEN_SS;

namespace
{
int gFail = 0;
sscpu_timestamp_t gNow = 0;

void Expect(const char* what, unsigned long long got, unsigned long long want)
{
    if (got != want) { std::printf("FAIL: %s: got %llu, want %llu\n", what, got, want); ++gFail; }
    else std::printf("  ok  %s = %llu\n", what, got);
}

void Fresh()
{
    std::memset(SeSsPerfC, 0, sizeof SeSsPerfC);
    gDmaHold = false;
    VDP1::gSpriteCost = 400;
    VDP1::gResumeLeft = 0;
    ss_horrible_hacks = 0;
    VDP1::Reset(true);
    gNow = 0;
    VDP1::lastts = 0;
}
// Entry n of the command table: 32 bytes = 16 words each (CurCommandAddr counts words, in steps of 0x10).
void Cmd(unsigned n, uint16 w0) { VDP1::VRAM[n * 16] = w0; }
enum : uint16 { END = 0x8000, SPRITE = 0x0000 };
void Start() { VDP1::Write16_DB(0x100000 + (0x2 << 1), 1); }   // PTMR = 1: draw now
}

int main()
{
    VDP1::Init();

    std::printf("END-only table: one fetch, one 16-cycle debit, nothing else\n");
    Fresh();
    Cmd(0, END);
    Start();
    VDP1::Update(gNow += 300);
    Expect("commands fetched", SeSsPerfC[SE_PERF_VDP1_COMMANDS], 1);
    Expect("command work cycles", SeSsPerfC[SE_PERF_VDP1_WORK_CYCLES], 16);
    Expect("budget discarded by DMA", SeSsPerfC[SE_PERF_VDP1_DMA_DISCARDED], 0);
    Expect("draw starts", SeSsPerfC[SE_PERF_VDP1_DRAW_STARTS], 1);
    Expect("retired index 6 stays zero", SeSsPerfC[SE_PERF_RETIRED_6], 0);

    std::printf("a draw yields mid-command and resumes: the debit is not charged twice\n");
    Fresh();
    VDP1::gSpriteCost = 300;
    Cmd(0, SPRITE); Cmd(1, END);
    Start();
    VDP1::Update(gNow += 300);              // 263 budget: 16 fetch + 300 command -> yields at -53
    Expect("work after the yield", SeSsPerfC[SE_PERF_VDP1_WORK_CYCLES], 16 + 300);
    Expect("still drawing", VDP1::DrawingActive ? 1 : 0, 1);
    VDP1::Update(gNow += 300);              // resumes AFTER the 300 debit: resume phase 40, then entry 1
    Expect("work after the resume (16+300+40 resume +16 for the END fetch)", SeSsPerfC[SE_PERF_VDP1_WORK_CYCLES], 16 + 300 + 40 + 16);
    Expect("commands fetched", SeSsPerfC[SE_PERF_VDP1_COMMANDS], 2);
    Expect("drawing finished", VDP1::DrawingActive ? 1 : 0, 0);

    std::printf("a DMA holds VDP1 during an active draw: the unspent budget is discarded, not worked\n");
    Fresh();
    Cmd(0, END);
    Start();
    gDmaHold = true;
    VDP1::Update(gNow += 10);               // budget 263 (capped), thrown away
    Expect("discarded budget", SeSsPerfC[SE_PERF_VDP1_DMA_DISCARDED], 263);
    Expect("work while held", SeSsPerfC[SE_PERF_VDP1_WORK_CYCLES], 0);
    Expect("commands while held", SeSsPerfC[SE_PERF_VDP1_COMMANDS], 0);
    gDmaHold = false;
    VDP1::Update(gNow += 300);
    Expect("work once released", SeSsPerfC[SE_PERF_VDP1_WORK_CYCLES], 16);
    Expect("discarded budget unchanged", SeSsPerfC[SE_PERF_VDP1_DMA_DISCARDED], 263);

    std::printf("a DMA while VDP1 is idle has no draw budget to lose\n");
    Fresh();
    gDmaHold = true;
    VDP1::Update(gNow += 500);
    Expect("discarded budget", SeSsPerfC[SE_PERF_VDP1_DMA_DISCARDED], 0);

    std::printf("an automatic start after a long idle interval charges the work, not the interval\n");
    Fresh();
    Cmd(0, END);
    VDP1::Write16_DB(0x100000 + (0x2 << 1), 2);   // PTMR = 2: start when the frame buffer swaps
    VDP1::SetHBVB(gNow, false, true);               // enter v-blank
    gNow += 5000000;
    VDP1::SetHBVB(gNow, true, false);               // leave v-blank: swap, then start + Update(gNow)
    Expect("draw starts", SeSsPerfC[SE_PERF_VDP1_DRAW_STARTS], 1);
    Expect("commands fetched", SeSsPerfC[SE_PERF_VDP1_COMMANDS], 1);
    Expect("command work cycles (not 5,000,000)", SeSsPerfC[SE_PERF_VDP1_WORK_CYCLES], 16);
    Expect("overruns", SeSsPerfC[SE_PERF_VDP1_OVERRUNS], 0);

    std::printf("instant draw: the whole table in one Update, work above the frame budget\n");
    Fresh();
    VDP1::Write16_DB(0x100000 + (0x2 << 1), 0);
    VDP1::SetHBVB(gNow, false, true);
    VDP1::SetHBVB(gNow += 1000, true, false);       // leaving v-blank sets the instant-draw limit
    ss_horrible_hacks = HORRIBLEHACK_VDP1INSTANT;
    for (unsigned i = 0; i < 3; ++i) Cmd(i, SPRITE);
    Cmd(3, END);
    std::memset(SeSsPerfC, 0, sizeof SeSsPerfC);
    VDP1::gResumeLeft = 0;
    Start();
    VDP1::Update(gNow += 10);
    Expect("drawing finished in one Update", VDP1::DrawingActive ? 1 : 0, 0);
    Expect("commands fetched", SeSsPerfC[SE_PERF_VDP1_COMMANDS], 4);
    Expect("work cycles: 3 * (16 + 400 + 40) + 16, above the 263-cycle budget", SeSsPerfC[SE_PERF_VDP1_WORK_CYCLES], 3 * (16 + 400 + 40) + 16);

    std::printf("%s\n", gFail ? "FAILED" : "PASSED");
    return gFail ? 1 : 0;
}
