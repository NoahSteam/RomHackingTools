// Shared by the Step 2 decompiler tests: the check macro, Windows crash reporting that names
// its cause on CI, scratch-directory helpers, and the synthetic SH-2 fixtures (each documented
// where it was generated and verified).
#pragma once

#ifdef _WIN32
// First, so its LoadImage macro (-> LoadImageA) is gone before any Ghidra header is seen.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef LoadImage
#include <crtdbg.h>
#else
#include <sys/stat.h>
#endif

#include "Decompiler/MemorySnapshot.h"
#include "FileWrite.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <memory>
#include <string>
#include <vector>

namespace dts
{

inline int& Failures()
{
    static int failures = 0;
    return failures;
}

inline void Check(bool ok, const char* what, const char* file, int line)
{
    if (ok) return;
    std::printf("CHECK failed at %s:%d: %s\n", file, line, what);
    ++Failures();
}

#define CHECK(expression) dts::Check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)

// Each case announces itself first; with stdout unbuffered, the last name printed is where a
// test that never returns got stuck.
#define RUN(call) (std::printf("-- %s\n", #call), call)

inline double MsSince(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// How long a test waits for a decompile before it gives up: a guard against a hang, not a time limit. An unoptimised build (no NDEBUG:
// CMake's Debug, MSVC's Debug CRT) decompiles the large fixtures several times slower, past 30 s on CI's Windows runner.
#ifdef NDEBUG
const double kWaitMs = 30000.0;
#else
const double kWaitMs = 300000.0;
#endif

// ---- Dying loudly -------------------------------------------------------------------------
// Under ctest stdout is a pipe and fully buffered, so a test killed by its timeout would show
// nothing; unbuffered, the last line says how far it got. On Windows a crash, a Debug-CRT
// assertion or abort() otherwise opens a dialog nobody clicks on CI, or ends the process with
// no text: these report on stderr and exit instead.
#ifdef _WIN32
inline void OnInvalidParameter(const wchar_t* expr, const wchar_t* func, const wchar_t* file, unsigned line, uintptr_t)
{
    std::fwprintf(stderr, L"FATAL: CRT invalid parameter: %ls in %ls (%ls:%u)\n",
                  expr ? expr : L"?", func ? func : L"?", file ? file : L"?", line);
    std::_Exit(3);
}

inline LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info)
{
    const EXCEPTION_RECORD* r = info->ExceptionRecord;
    std::fprintf(stderr, "FATAL: exception 0x%08lX at %p\n", static_cast<unsigned long>(r->ExceptionCode), r->ExceptionAddress);
    std::_Exit(3);
}
#endif

inline void PrepareProcess()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#ifdef _WIN32
    ::SetUnhandledExceptionFilter(OnUnhandledException);
    _set_invalid_parameter_handler(OnInvalidParameter);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    for (int type : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT })
    {
        _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
    }
#endif
}

inline int Finish(const char* name, std::chrono::steady_clock::time_point start)
{
    std::printf("%s: %.1f ms total\n", name, MsSince(start));
    if (Failures())
    {
        std::printf("%d failure(s)\n", Failures());
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}

// ---- Scratch directories ------------------------------------------------------------------

inline std::string Join(const std::string& a, const std::string& b) { return a + sfe::PathSeparator() + b; }

// The scratch tree is the test's alone, so a plain recursive delete is safe.
inline void RemoveTree(const std::string& path)
{
    if (!sfe::PathEntryExists(path)) return;
    if (!sfe::ExistsAsNonRegularFile(path))
    {
        sfe::RemoveFile(path);
        return;
    }
    std::vector<std::string> names;
    sfe::ListDirectory(path, names);
    for (const std::string& n : names) RemoveTree(Join(path, n));
    sfe::RemoveEmptyDirectory(path);
}

// ---- Building snapshots -------------------------------------------------------------------

inline void Put16(std::vector<uint8_t>& v, size_t off, uint16_t w)
{
    if (v.size() < off + 2) v.resize(off + 2, 0);
    v[off] = uint8_t(w >> 8);
    v[off + 1] = uint8_t(w);
}

inline void Put32(std::vector<uint8_t>& v, size_t off, uint32_t w)
{
    Put16(v, off, uint16_t(w >> 16));
    Put16(v, off + 2, uint16_t(w));
}

// A snapshot holding full-size, zeroed LWRAM and HWRAM, with 'hwram' bytes placed from
// 0x06000000. 'serial' becomes the key's derive serial, as an in-place edit would move it.
inline std::shared_ptr<sfe::decomp::MemorySnapshot> MakeSnapshot(const std::vector<uint8_t>& hwram, uint64_t serial = 1)
{
    using sfe::decomp::MemorySnapshot;
    auto s = std::make_shared<MemorySnapshot>();
    sfe::decomp::MemoryRegion lw;
    lw.name = "LWRAM";
    lw.base = MemorySnapshot::kLwramBase;
    lw.bytes.assign(MemorySnapshot::kWorkRamSize, 0);
    sfe::decomp::MemoryRegion hw;
    hw.name = "HWRAM";
    hw.base = MemorySnapshot::kHwramBase;
    hw.bytes.assign(MemorySnapshot::kWorkRamSize, 0);
    for (size_t i = 0; i < hwram.size() && i < hw.bytes.size(); ++i) hw.bytes[i] = hwram[i];
    s->regions.push_back(std::move(lw));
    s->regions.push_back(std::move(hw));
    s->key.sourceId = 1;
    s->key.deriveSerial = serial;
    s->key.regionSetHash = s->ComputeRegionSetHash();
    return s;
}

// ---- Fixture 1 / 1b (Docs/Decompiler/spike/tools/fixture1.py, every word Capstone-checked) --

const uint32_t kFixture1Caller = 0x06004000u;
const uint32_t kFixture1Callee = 0x06005000u;
const uint32_t kFixture1Literal = 0x0600401Cu;
const uint32_t kFixture1AddOne = 0x06004012u;   // add #1,r0
const uint32_t kFixture1Jsr = 0x0600400Eu;
const uint32_t kFixture1Rts = 0x06004016u;
const uint32_t kFixture1MovZero = 0x06004006u;  // mov #0,r0

// Fixture 1's caller and callee placed in a HWRAM image. 'literal' is the word the caller calls
// through: the callee for fixture 1, 0x00000600 (BIOS, never captured) for fixture 1b.
inline void PlaceFixture1(std::vector<uint8_t>& hw, uint32_t literal = kFixture1Callee)
{
    static const uint16_t caller[] = {
        0x2FE6,   // mov.l r14,@-r15
        0x4F22,   // sts.l pr,@-r15
        0x6E43,   // mov r4,r14
        0xE000,   // mov #0,r0
        0x4415,   // cmp/pl r4
        0x8B03,   // bf 0x06004014
        0xD003,   // mov.l @(12,pc),r0   -> literal at 0x0600401C
        0x400B,   // jsr @r0
        0x64E3,   //   mov r14,r4        (delay slot: the argument)
        0x7001,   // add #1,r0
        0x4F26,   // lds.l @r15+,pr      <- 0x06004014
        0x000B,   // rts
        0x6EF6,   //   mov.l @r15+,r14   (delay slot)
        0x0009,   // nop (aligns the literal)
    };
    static const uint16_t callee[] = {
        0x6042,   // mov.l @r4,r0
        0x000B,   // rts
        0x7002,   //   add #2,r0         (delay slot)
    };
    size_t off = kFixture1Caller - 0x06000000u;
    for (uint16_t w : caller) { Put16(hw, off, w); off += 2; }
    Put32(hw, off, literal);
    off = kFixture1Callee - 0x06000000u;
    for (uint16_t w : callee) { Put16(hw, off, w); off += 2; }
}

// ---- Fixture 3 (Docs/Decompiler/spike/tools/fixture3.py, Capstone-checked; PLAN.md A3) -----

const uint32_t kFixture3Bra = 0x06006000u;
const uint32_t kFixture3BraSlot = 0x06006002u;
const uint32_t kFixture3Bfs = 0x06006100u;
const uint32_t kFixture3BfsSlot = 0x06006104u;
const uint32_t kFixture3Direct = 0x11111111u;   // at the address the spec computes
const uint32_t kFixture3Slot = 0x22222222u;     // at the address the hardware reads

// The other delayed-branch kinds, each with a PC-relative slot instruction. Unsupported unless
// the branch is bra or bsr, which patch 0005 fixes in the spec.
struct Fixture3Case { const char* name; uint32_t entry, slot; };
const Fixture3Case kFixture3Others[] = {
    { "rts + mov.w", 0x06006200u, 0x06006202u },
    { "jmp + mov.l", 0x06006400u, 0x06006402u },
    { "braf + mov.l", 0x06006500u, 0x06006502u },
    { "jsr + mov.l", 0x06006800u, 0x06006802u },
};
const uint32_t kFixture3BsrMova = 0x06006300u;
// Step 4 cases: a bsr slot load and the load after the pair; a bra slot load and the load at the
// bra's target; and a bra slot that a bt also jumps straight to (still unsupported).
const uint32_t kFixture3BsrLoad = 0x06006600u;
const uint32_t kFixture3BraTarget = 0x06006700u;
const uint32_t kFixture3SlotJumped = 0x06006900u;
const uint32_t kFixture3SlotJumpedSlot = 0x06006906u;
// Review of Step 4: a recovered switch whose case 0 enters a bra normally and whose case 1 enters
// that bra's delay slot directly. The slot is a jump target too -- through the table, which no
// immediate branch shows -- so it stays unsupported.
const uint32_t kFixture3SwitchSlot = 0x06006A00u;
const uint32_t kFixture3SwitchSlotSlot = 0x06006A1Au;

inline void PlaceFixture3(std::vector<uint8_t>& hw)
{
    auto put = [&](uint32_t a, std::initializer_list<uint16_t> words) {
        size_t off = a - 0x06000000u;
        for (uint16_t w : words) { Put16(hw, off, w); off += 2; }
    };
    // bra 0x06006008 ; mov.l @(12,pc),r1 (slot: spec reads 0x06006010, hardware 0x06006014) ;
    // nop ; nop ; rts (<- target) ; mov r1,r0 ; nop ; nop
    put(kFixture3Bra, { 0xA002, 0xD103, 0x0009, 0x0009, 0x000B, 0x6013, 0x0009, 0x0009 });
    Put32(hw, 0x06006010u - 0x06000000u, kFixture3Direct);
    Put32(hw, 0x06006014u - 0x06000000u, kFixture3Slot);
    // cmp/pl r4 ; bf/s 0x0600610A ; mov.l @(8,pc),r1 (slot: spec 0x06006110, hardware when taken
    // 0x06006114) ; add #1,r1 ; nop ; rts (<- target) ; mov r1,r0 ; nop
    put(kFixture3Bfs, { 0x4415, 0x8F02, 0xD102, 0x7101, 0x0009, 0x000B, 0x6013, 0x0009 });
    Put32(hw, 0x06006110u - 0x06000000u, kFixture3Direct);
    Put32(hw, 0x06006114u - 0x06000000u, kFixture3Slot);
    // rts ; mov.w @(2,pc),r0 (slot) ; nop ; nop ; .word 0x1234
    put(0x06006200u, { 0x000B, 0x9001, 0x0009, 0x0009, 0x1234 });
    // bsr 0x06006308 ; mova @(4,pc),r0 (slot) ; rts ; nop ; rts (the bsr's target) ; nop
    put(0x06006300u, { 0xB002, 0xC701, 0x000B, 0x0009, 0x000B, 0x0009 });
    // jmp @r1 ; mov.l @(4,pc),r1 (slot) ; nop ; nop ; .long 0x44444444
    put(0x06006400u, { 0x412B, 0xD101, 0x0009, 0x0009 });
    Put32(hw, 0x06006408u - 0x06000000u, 0x44444444u);
    // braf r1 ; mov.l @(4,pc),r1 (slot) ; nop ; nop ; .long 0x55555555
    put(0x06006500u, { 0x0123, 0xD101, 0x0009, 0x0009 });
    Put32(hw, 0x06006508u - 0x06000000u, 0x55555555u);
    // bsr 0x0600660C ; mov.l @(12,pc),r4 (slot: spec without 0005 0x06006610, hardware
    // 0x06006618) ; mov.l @(12,pc),r0 (after the pair: 0x06006614) ; rts ; nop ; nop ;
    // rts (<- target) ; mov r4,r0
    put(kFixture3BsrLoad, { 0xB004, 0xD403, 0xD003, 0x000B, 0x0009, 0x0009, 0x000B, 0x6043 });
    Put32(hw, 0x06006610u - 0x06000000u, 0x33333333u);
    Put32(hw, 0x06006614u - 0x06000000u, 0x35353535u);
    Put32(hw, 0x06006618u - 0x06000000u, 0x34343434u);
    // bra 0x06006708 ; mov.l @(12,pc),r1 (slot: spec without 0005 0x06006710, hardware
    // 0x06006714) ; nop ; nop ; mov.l @(12,pc),r0 (<- target: 0x06006718) ; rts ; add r1,r0 ; nop
    put(kFixture3BraTarget, { 0xA002, 0xD103, 0x0009, 0x0009, 0xD003, 0x000B, 0x301C, 0x0009 });
    Put32(hw, 0x06006710u - 0x06000000u, 0x33333333u);
    Put32(hw, 0x06006714u - 0x06000000u, 0x01000000u);
    Put32(hw, 0x06006718u - 0x06000000u, 0x00000234u);
    // jsr @r1 ; mov.l @(4,pc),r1 (slot) ; rts ; nop ; .long 0x66666666
    put(0x06006800u, { 0x410B, 0xD101, 0x000B, 0x0009 });
    Put32(hw, 0x06006808u - 0x06000000u, 0x66666666u);
    // cmp/pl r4 ; bt 0x06006906 ; bra 0x0600690C ; mov.l @(12,pc),r1 (slot and the bt's target:
    // 0x06006914 reached directly, 0x06006918 in the slot) ; nop ; nop ; rts (<- target) ; mov r1,r0
    put(kFixture3SlotJumped, { 0x4415, 0x8900, 0xA002, 0xD103, 0x0009, 0x0009, 0x000B, 0x6013 });
    Put32(hw, 0x06006914u - 0x06000000u, kFixture3Direct);
    Put32(hw, 0x06006918u - 0x06000000u, kFixture3Slot);
    // mov #2,r1 ; cmp/hs r1,r4 ; bt 0x06006A24 (default) ; mova 0x06006A10,r0 ; shll2 r4 ;
    // mov.l @(r0,r4),r1 ; jmp @r1 ; nop ; .long 0x06006A18, 0x06006A1A (the table) ;
    // 0x06006A18 (case 0): bra 0x06006A20 ; mov.l @(12,pc),r1 (slot, and case 1: 0x06006A28 reached
    // directly, 0x06006A2C in the slot) ; nop ; nop ; rts (<- target) ; mov r1,r0 ;
    // 0x06006A24 (default): rts ; mov #0,r0
    put(kFixture3SwitchSlot, { 0xE102, 0x3412, 0x890E, 0xC702, 0x4408, 0x014E, 0x412B, 0x0009 });
    Put32(hw, 0x06006A10u - 0x06000000u, 0x06006A18u);
    Put32(hw, 0x06006A14u - 0x06000000u, 0x06006A1Au);
    put(0x06006A18u, { 0xA002, 0xD103, 0x0009, 0x0009, 0x000B, 0x6013, 0x000B, 0xE000 });
    Put32(hw, 0x06006A28u - 0x06000000u, kFixture3Direct);
    Put32(hw, 0x06006A2Cu - 0x06000000u, kFixture3Slot);
}

// ---- The cancellation spike's fixtures (Docs/Decompiler/spike/cancel_spike.cpp) ---------------

// N diamonds: mov.l @r4,r1 ; add #1,r1 ; mov.l r1,@r4 ; cmp/pl r1 ; bt .+4 ; add #2,r2
const uint32_t kDiamondsBase = 0x06010000u;
inline void PlaceDiamonds(std::vector<uint8_t>& hw, int n)
{
    size_t off = kDiamondsBase - 0x06000000u;
    for (int i = 0; i < n; ++i)
        for (uint16_t w : { 0x6142, 0x7101, 0x2412, 0x4115, 0x8900, 0x7202 }) { Put16(hw, off, uint16_t(w)); off += 2; }
    Put16(hw, off, 0x000B);
    Put16(hw, off + 2, 0x0009);
}

// N x (mov.l @r4,r1 ; add #1,r1 ; mov.l r1,@r4): one straight-line run, no branches.
const uint32_t kStraightBase = 0x06030000u;
inline void PlaceStraight(std::vector<uint8_t>& hw, int n)
{
    size_t off = kStraightBase - 0x06000000u;
    for (int i = 0; i < n; ++i)
        for (uint16_t w : { 0x6142, 0x7101, 0x2412 }) { Put16(hw, off, uint16_t(w)); off += 2; }
    Put16(hw, off, 0x000B);
    Put16(hw, off + 2, 0x0009);
}

// GCC-style switch: bounds check, shll2, mova table, mov.l @(r0,r1),r3, jmp @r3, a table of n
// longs, n cases of k straight-line triples each.
const uint32_t kJumpTableBase = 0x06050000u;
inline void PlaceJumpTable(std::vector<uint8_t>& hw, int n, int k)
{
    const uint32_t base = kJumpTableBase;
    const uint32_t table = base + 0x14, deflt = table + 4 * n, cases = deflt + 6, caseSize = 6 * k + 6;
    size_t off = base - 0x06000000u;
    auto put = [&](uint16_t w) { Put16(hw, off, w); off += 2; };
    put(0x6143);                                                  // mov r4,r1
    put(uint16_t(0xE200 | (n - 1)));                              // mov #n-1,r2
    put(0x3126);                                                  // cmp/hi r2,r1
    put(uint16_t(0x8900 | ((deflt - (base + 0x06 + 4)) / 2)));    // bt default
    put(0x4108);                                                  // shll2 r1
    put(0xC702);                                                  // mova table,r0
    put(0x031E);                                                  // mov.l @(r0,r1),r3
    put(0x432B);                                                  // jmp @r3
    put(0x0009); put(0x0009);                                     // nop (slot), nop (align)
    for (int i = 0; i < n; ++i) { Put32(hw, off, cases + caseSize * i); off += 4; }
    put(0xE0FF); put(0x000B); put(0x0009);                        // default: mov #-1,r0 ; rts ; nop
    for (int i = 0; i < n; ++i)
    {
        for (int j = 0; j < k; ++j) for (uint16_t w : { 0x6142, 0x7101, 0x2412 }) put(uint16_t(w));
        put(uint16_t(0xE000 | i)); put(0x000B); put(0x0009);
    }
}

}  // namespace dts
