// F2 / F9 component test: the REAL patched SH7095::SetActive and SH7095::Reset (sh7095.inc, compiled as
// part of the REAL patched ss.cpp) driven with a clock the test controls, calling the REAL glue
// (se_mednafen_glue.c, compiled unwired, plus se_export.c) through the hooks apply.py injected. Nothing
// about the transition is mocked: the slave's power edge and the watchdog reset are Mednafen's own
// code; the rest of the machine is never run (its symbols are unresolved and never reached).
//
// What this proves: the power edges and the reset reach the accounting at the right side of the state
// change, with the CPU's own timestamp, and the redundant SetActive produces no edge. What it cannot:
// that SMPC delivers the transitions in the order a game expects (verification V1, needs a BIOS), or the
// frame-close and state-load hooks (inside Emulate / StateAction, which need a whole machine; their
// placement is checked by check_component.py against the patched text).
//
// Built and run by Integration/Mednafen/check_component.py (defines SE_SS_CPP).
#include SE_SS_CPP

#include <cstdio>
#include <cstring>

extern "C"
{
unsigned int SeExportCallStackEpoch(void);
unsigned int SeExportCurrentFunction(int cpu);
void SeExportPushFrame(int cpu, unsigned int callSite, unsigned int func, unsigned int ret, unsigned int sp, unsigned long long cycle);
}

// Data the rest of ss.cpp refers to and this test never reaches (hidden-visibility externs the linker will not
// leave undefined). Nothing here is read or written by SetActive / Reset.
namespace Mednafen
{
MDFNGI* MDFNGameInfo;
const std::vector<CheatFormatStruct> CheatFormatInfo_Empty;
}
// NativeVFS's constructor is in a TU this test does not link, so its storage is given the symbol directly.
extern "C" { char SeHarnessNvfsStorage[512] asm("_ZN8Mednafen4NVFSE") __attribute__((visibility("hidden"), aligned(64))); }
namespace MDFN_IEN_SS
{
CartInfo Cart;
const std::vector<InputPortInfoStruct> SMPC_PortInfo;
void (*const DSP_GenFuncTable[2][16][8][8][4])(void) = {};
void (*const DSP_MVIFuncTable[2][16][128])(void) = {};
void (*const DSP_JMPFuncTable[2][128])(void) = {};
void (*const DSP_MiscFuncTable[2][4])(void) = {};
namespace VDP1
{
uint16 VRAM[0x40000];
uint16 FB[2][0x20000];
}
}

using namespace MDFN_IEN_SS;

namespace
{
int gFail = 0;
void Expect(const char* what, long long got, long long want)
{
    if (got != want) { std::printf("FAIL: %s: got %lld, want %lld\n", what, got, want); ++gFail; }
    else std::printf("  ok  %s = %lld\n", what, got);
}

// The frame ends: what the injected hook in Emulate() passes, with the clock at 'end'.
void CloseFrame(int end)
{
    SeMednafenFrameClose(end, (int)CPU[0].timestamp, (int)CPU[1].timestamp, CPU[1].timestamp != SS_EVENT_DISABLED_TS);
}
// The slave has run up to 'ts' (its own timestamp), as it would by executing.
void SlaveRunsTo(int ts) { CPU[1].timestamp = ts; }
}

int main()
{
    InitEvents();                              // the real scheduler list SetActive's event updates walk
    CPU[1].timestamp = SS_EVENT_DISABLED_TS;   // powered off, as SMPC leaves it until SSHON
    SeMednafenStateLoaded(0);
    std::memset(SeSsPerfC, 0, sizeof(unsigned long long) * SE_PERF_COUNT);

    std::printf("slave on during [0,200) and [800,1000): exactly 400 on-cycles\n");
    SH7095_mem_timestamp = 0;
    CPU[1].SetActive(true);
    Expect("the slave is on", CPU[1].timestamp != SS_EVENT_DISABLED_TS, 1);
    SlaveRunsTo(150);
    SH7095_mem_timestamp = 150;
    CPU[1].SetActive(true);                       // redundant: not an edge
    SlaveRunsTo(200);
    SH7095_mem_timestamp = 200;
    CPU[1].SetActive(false);
    Expect("the slave is off", CPU[1].timestamp == SS_EVENT_DISABLED_TS, 1);
    SH7095_mem_timestamp = 500;
    CPU[1].SetActive(false);                      // redundant
    SH7095_mem_timestamp = 800;
    CPU[1].SetActive(true);
    SlaveRunsTo(950);
    CloseFrame(1000);
    Expect("integrated on-cycles (counter 5)", (long long)SeSsPerfC[SE_PERF_SSH2_ON_CYCLES], 400);
    Expect("index 54 is never produced", (long long)SeSsPerfC[SE_PERF_RESERVED_54], 0);

    std::printf("a watchdog reset with the power left on drops the recorded stack, not the on-time\n");
    std::memset(SeSsPerfC, 0, sizeof(unsigned long long) * SE_PERF_COUNT);
    CPU[1].timestamp = 10;                        // the next frame, rebased
    SH7095_mem_timestamp = 10;
    SeExportPushFrame(1, 0x06001000u, 0x06002000u, 0x06001004u, 0x060F0000u, 0);
    Expect("a function is recorded", SeExportCurrentFunction(1) != 0, 1);
    const unsigned epoch = SeExportCallStackEpoch();
    CPU[1].Reset(false, true);
    Expect("the stack was reset (epoch moved)", SeExportCallStackEpoch() != epoch, 1);
    Expect("and no function is named for the reset code", (long long)SeExportCurrentFunction(1), 0);
    Expect("the slave is still on", CPU[1].timestamp != SS_EVENT_DISABLED_TS, 1);
    SlaveRunsTo(900);
    CloseFrame(1000);
    Expect("on-time is the whole frame (the reset opened no gap)", (long long)SeSsPerfC[SE_PERF_SSH2_ON_CYCLES], 1000);

    std::printf("%s\n", gFail ? "FAILED" : "PASSED");
    return gFail ? 1 : 0;
}
