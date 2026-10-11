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

inline void PlaceFixture3(std::vector<uint8_t>& hw)
{
    static const uint16_t bra[] = {
        0xA002,   // bra 0x06006008
        0xD103,   //   mov.l @(12,pc),r1   (slot: spec reads 0x06006010, hardware 0x06006014)
        0x0009, 0x0009,
        0x000B,   // rts                    <- target
        0x6013,   //   mov r1,r0
        0x0009, 0x0009,
    };
    static const uint16_t bfs[] = {
        0x4415,   // cmp/pl r4
        0x8F02,   // bf/s 0x0600610A
        0xD102,   //   mov.l @(8,pc),r1    (slot: spec reads 0x06006110, hardware when taken 0x06006114)
        0x7101,   // add #1,r1
        0x0009,
        0x000B,   // rts                    <- target
        0x6013,   //   mov r1,r0
        0x0009,
    };
    size_t off = kFixture3Bra - 0x06000000u;
    for (uint16_t w : bra) { Put16(hw, off, w); off += 2; }
    Put32(hw, off, kFixture3Direct);
    Put32(hw, off + 4, kFixture3Slot);
    off = kFixture3Bfs - 0x06000000u;
    for (uint16_t w : bfs) { Put16(hw, off, w); off += 2; }
    Put32(hw, off, kFixture3Direct);
    Put32(hw, off + 4, kFixture3Slot);
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
