// Does the vendored Ghidra engine decompile SH-2 when it is given its language files the way
// the app gives them (PLAN.md Step 1, A5)?
//
// The spec is never read from the source tree here: SpecBundle materialises the copy embedded
// in Sh2SpecData.h into a config directory, and the engine is pointed at exactly that
// directory, so this exercises what a user's build runs. The decompile is the spike's fixture 1
// (Docs/Decompiler/spike/tools/fixture1.py, every word checked against Capstone): a caller
// that calls through a literal pool with the argument moved in the delay slot.
//
// The rest pins SpecBundle's recovery rules: a damaged directory is repaired, a directory for
// another build's hash is left alone, and a config directory that cannot be written yields
// "unavailable" with the path, never a crash.
//
// usage: SaturnExplorerSh2DecompilerEngineTests <scratch dir>
#ifdef _WIN32
// First, so its LoadImage macro (-> LoadImageA) can be removed before ghidra::LoadImage is seen.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#undef LoadImage
#endif

#include "Decompiler/SpecBundle.h"
#include "Disc/PathUtil.h"   // ReadWholeFile
#include "FileWrite.h"

#include "libdecomp.hh"
#include "loadimage.hh"
#include "opbehavior.hh"
#include "rangeutil.hh"
#include "ruleaction.hh"
#include "sleigh_arch.hh"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <crtdbg.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

int gFailures = 0;

void Check(bool ok, const char* what, int line)
{
    if (ok) return;
    std::printf("CHECK failed at line %d: %s\n", line, what);
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

double MsSince(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

// ---- Fixture 1 (spike/tools/fixture1.py) -------------------------------------------------

const uint32_t kImageBase = 0x06004000u;
const uint32_t kCaller = 0x06004000u;
const uint32_t kCallee = 0x06005000u;
const uint32_t kLiteral = 0x0600401Cu;

std::vector<uint8_t> Fixture1()
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
    std::vector<uint8_t> img(0x2000, 0);
    size_t off = 0;
    for (uint16_t w : caller) { img[off++] = uint8_t(w >> 8); img[off++] = uint8_t(w); }
    const uint32_t lit = kCallee;
    for (int i = 0; i < 4; ++i) img[off++] = uint8_t(lit >> (24 - 8 * i));
    off = kCallee - kImageBase;
    for (uint16_t w : callee) { img[off++] = uint8_t(w >> 8); img[off++] = uint8_t(w); }
    return img;
}

// ---- The minimum engine glue (from spike/sh2_decomp_spike.cpp) ---------------------------

// One captured region and nothing else: anything outside it throws DataUnavailError, which is
// how the engine learns to stop following flow there. (Saturn bus folding is Step 2, A2.)
class RegionLoadImage : public ghidra::LoadImage
{
public:
    RegionLoadImage(uint32_t base, std::vector<uint8_t> bytes)
        : ghidra::LoadImage("saturn-test"), mBase(base), mBytes(std::move(bytes)) {}

    ghidra::RangeList readonly;   // the literal-pool words the engine may fold to constants

    // When the engine reads exactly 'trapSize' bytes at 'trapAddr', 'trapFlag' is set: a way to
    // request cancellation at a precise point inside the analysis rather than on a timer.
    uint64_t trapAddr = 0;
    int trapSize = 0;
    std::atomic<bool>* trapFlag = nullptr;
    bool trapped = false;

    void loadFill(ghidra::uint1* ptr, ghidra::int4 size, const ghidra::Address& addr) override
    {
        const uint64_t a = addr.getOffset();
        if (trapFlag && a == trapAddr && size == trapSize)
        {
            trapFlag->store(true);
            trapped = true;
        }
        if (a < mBase || a + size > mBase + mBytes.size())
        {
            std::ostringstream e;
            e << "Unable to load " << size << " bytes at " << std::hex << a;
            throw ghidra::DataUnavailError(e.str());
        }
        std::memcpy(ptr, mBytes.data() + (a - mBase), size);
    }
    std::string getArchType() const override { return "saturn"; }
    void adjustVma(long) override {}
    void getReadonly(ghidra::RangeList& list) const override { list = readonly; }

private:
    uint32_t mBase;
    std::vector<uint8_t> mBytes;
};

class TestArchitecture : public ghidra::SleighArchitecture
{
public:
    TestArchitecture(RegionLoadImage* image, std::ostream* err)
        : ghidra::SleighArchitecture("saturn-test", "SuperH:BE:32:SH-2:default", err), mImage(image) {}

private:
    void buildLoader(ghidra::DocumentStorage&) override
    {
        collectSpecFiles(*errorstream);
        loader = mImage;   // owned by the Architecture from here on
    }
    void resolveArchitecture() override
    {
        archid = getTarget();
        ghidra::SleighArchitecture::resolveArchitecture();
    }
    void postSpecFile() override { ghidra::Architecture::postSpecFile(); }

    RegionLoadImage* mImage;
};

// Decompile fixture 1's caller with the callee registered and the literal marked read-only
// (the two things Step 2's wrapper does automatically). Returns the C text, or "" on failure.
std::string DecompileFixture1()
{
    auto* image = new RegionLoadImage(kImageBase, Fixture1());
    std::ostringstream errs;
    TestArchitecture arch(image, &errs);
    ghidra::DocumentStorage store;
    arch.readonlypropagate = true;
    try
    {
        arch.init(store);
    }
    catch (ghidra::LowlevelError& e)
    {
        std::printf("  Architecture::init failed: %s\n  %s\n", e.explain.c_str(), errs.str().c_str());
        return std::string();
    }
    ghidra::AddrSpace* ram = arch.getDefaultCodeSpace();
    arch.symboltab->setPropertyRange(ghidra::Varnode::readonly, ghidra::Range(ram, kLiteral, kLiteral + 3));

    const ghidra::Address lo(ram, 0), hi(ram, ram->getHighest());
    const ghidra::Address calleeAddr(ram, kCallee);
    std::string calleeName;
    arch.nameFunction(calleeAddr, calleeName);
    ghidra::Funcdata* callee = arch.symboltab->getGlobalScope()->addFunction(calleeAddr, calleeName)->getFunction();

    const ghidra::Address entry(ram, kCaller);
    std::string name;
    arch.nameFunction(entry, name);
    ghidra::Funcdata* fd = arch.symboltab->getGlobalScope()->addFunction(entry, name)->getFunction();
    std::ostringstream out;
    try
    {
        callee->followFlow(lo, hi);
        fd->followFlow(lo, hi);
        arch.allacts.getCurrent()->reset(*fd);
        arch.allacts.getCurrent()->perform(*fd);
        arch.print->setOutputStream(&out);
        arch.print->docFunction(fd);
    }
    catch (ghidra::LowlevelError& e)
    {
        std::printf("  decompile failed: %s\n", e.explain.c_str());
        return std::string();
    }
    return out.str();
}

// ---- Cancellation inside jump-table recovery (patches/0001-cancel-flag.patch) --------------

// The spike's GCC-style switch, except the table base comes from a literal-pool word rather
// than mova. Marked read-only, that word is folded by the "jumptable" simplification that
// FlowInfo runs on a partial copy of the function -- so a trap on reading it requests
// cancellation from inside that nested perform(), the one place where Ghidra used to catch
// the resulting CancelError as an ordinary recovery failure and carry on.
const uint32_t kSwitchBase = 0x06020000u;
const uint32_t kSwitchLiteral = kSwitchBase + 0x14;

std::vector<uint8_t> SwitchFixture()
{
    const int n = 4;
    const uint32_t table = kSwitchBase + 0x18, deflt = table + 4 * n, cases = deflt + 6;
    std::vector<uint8_t> b;
    auto put = [&](uint16_t w) { b.push_back(uint8_t(w >> 8)); b.push_back(uint8_t(w)); };
    auto put32 = [&](uint32_t w) { put(uint16_t(w >> 16)); put(uint16_t(w)); };
    put(0x6143);                                          // 00 mov r4,r1
    put(0xE200 | (n - 1));                                // 02 mov #n-1,r2
    put(0x3126);                                          // 04 cmp/hi r2,r1
    put(uint16_t(0x8900 | ((deflt - (kSwitchBase + 0x06 + 4)) / 2)));   // 06 bt default
    put(0x4108);                                          // 08 shll2 r1
    put(0xD002);                                          // 0A mov.l @(8,pc),r0 -> literal at 0x14
    put(0x031E);                                          // 0C mov.l @(r0,r1),r3
    put(0x432B);                                          // 0E jmp @r3
    put(0x0009); put(0x0009);                             // 10 nop (slot), 12 nop (align)
    put32(table);                                         // 14 literal: the table's address
    for (int i = 0; i < n; ++i) put32(cases + 6 * i);     // 18 table
    put(0xE0FF); put(0x000B); put(0x0009);                // default: mov #-1,r0 ; rts ; nop
    for (int i = 0; i < n; ++i) { put(uint16_t(0xE000 | i)); put(0x000B); put(0x0009); }
    b.resize(b.size() + 16, 0);   // the decoder fetches up to 16 bytes ahead of the last insn
    return b;
}

// One followFlow of the switch fixture with the trap armed. Its own scope: the engine's SLEIGH
// translator is process-global and every Architecture resets it, so two must not overlap.
void FollowSwitchWithTrap(bool& trapped, bool& cancelled)
{
    std::atomic<bool> cancel(false);
    auto* image = new RegionLoadImage(kSwitchBase, SwitchFixture());
    image->trapAddr = kSwitchLiteral;
    image->trapSize = 4;
    image->trapFlag = &cancel;
    std::ostringstream errs;
    TestArchitecture arch(image, &errs);
    ghidra::DocumentStorage store;
    arch.readonlypropagate = true;
    try { arch.init(store); }
    catch (ghidra::LowlevelError& e) { std::printf("  init failed: %s\n", e.explain.c_str()); trapped = cancelled = false; return; }
    arch.cancelRequested = &cancel;
    ghidra::AddrSpace* ram = arch.getDefaultCodeSpace();
    arch.symboltab->setPropertyRange(ghidra::Varnode::readonly, ghidra::Range(ram, kSwitchLiteral, kSwitchLiteral + 3));

    const ghidra::Address entry(ram, kSwitchBase);
    std::string name;
    arch.nameFunction(entry, name);
    ghidra::Funcdata* fd = arch.symboltab->getGlobalScope()->addFunction(entry, name)->getFunction();
    cancelled = false;
    try
    {
        fd->followFlow(ghidra::Address(ram, 0), ghidra::Address(ram, ram->getHighest()));
    }
    catch (ghidra::CancelError&) { cancelled = true; }
    catch (ghidra::LowlevelError& e) { std::printf("  followFlow: %s\n", e.explain.c_str()); }
    trapped = image->trapped;
}

void TestCancelDuringJumpTableRecovery()
{
    bool trapped = false, cancelled = false;
    FollowSwitchWithTrap(trapped, cancelled);
    std::printf("jump-table cancel: literal read during recovery %s, CancelError %s\n",
                trapped ? "yes" : "no", cancelled ? "reached the host" : "was swallowed");
    CHECK(trapped);   // else the fixture no longer reaches the nested simplification
    CHECK(cancelled);

    // The same function with no cancellation still recovers the switch, so the trap is what
    // made the difference, not a fixture the engine cannot decompile.
    const std::string c = [&] {
        auto* again = new RegionLoadImage(kSwitchBase, SwitchFixture());
        std::ostringstream e2;
        TestArchitecture a2(again, &e2);
        ghidra::DocumentStorage st;
        a2.readonlypropagate = true;
        std::ostringstream out;
        try
        {
            a2.init(st);
            ghidra::AddrSpace* r = a2.getDefaultCodeSpace();
            a2.symboltab->setPropertyRange(ghidra::Varnode::readonly, ghidra::Range(r, kSwitchLiteral, kSwitchLiteral + 3));
            const ghidra::Address en(r, kSwitchBase);
            std::string nm;
            a2.nameFunction(en, nm);
            ghidra::Funcdata* f = a2.symboltab->getGlobalScope()->addFunction(en, nm)->getFunction();
            f->followFlow(ghidra::Address(r, 0), ghidra::Address(r, r->getHighest()));
            a2.allacts.getCurrent()->reset(*f);
            a2.allacts.getCurrent()->perform(*f);
            a2.print->setOutputStream(&out);
            a2.print->docFunction(f);
        }
        catch (ghidra::LowlevelError& e) { out << "error: " << e.explain; }
        return out.str();
    }();
    CHECK(c.find("switch") != std::string::npos);
    if (c.find("switch") == std::string::npos) std::printf("control decompile:\n%s\n", c.c_str());
}

// ---- Filesystem helpers -----------------------------------------------------------------

std::string Join(const std::string& a, const std::string& b) { return a + sfe::PathSeparator() + b; }

// The scratch tree is ours alone, so a plain recursive delete is safe here.
void RemoveTree(const std::string& path)
{
    if (!sfe::PathEntryExists(path)) return;
    if (!sfe::ExistsAsNonRegularFile(path))
    {
        sfe::RemoveFile(path);
        return;
    }
#ifndef _WIN32
    ::chmod(path.c_str(), 0700);   // a read-only case may have left it unwritable
#endif
    std::vector<std::string> names;
    sfe::ListDirectory(path, names);
    for (const std::string& n : names) RemoveTree(Join(path, n));
    sfe::RemoveEmptyDirectory(path);
}

bool WriteFile(const std::string& path, const void* data, size_t size)
{
    std::string error;
    return sfe::WriteFileAtomically(path, data, size, error);
}

bool MatchesEmbedded(const std::string& dir)
{
    for (const sfe::decomp::SpecFileData& f : sfe::decomp::Sh2SpecFiles())
    {
        std::string bytes;
        if (!sfe::ReadWholeFile(Join(dir, f.name), bytes)) return false;
        if (bytes.size() != f.size || std::memcmp(bytes.data(), f.data, f.size) != 0) return false;
    }
    return true;
}

// ---- Dying loudly ------------------------------------------------------------------------

// On Windows a crash, a Debug-CRT assertion, abort() or a CRT invalid-parameter stop inside the
// engine otherwise ends the process with no text at all, or opens a dialog nobody clicks on CI.
// These say what happened on stderr instead, so a CI failure names its cause. (POSIX needs
// nothing: ctest already reports a signal, and the C++ exceptions are caught in main.)
#ifdef _WIN32
void OnInvalidParameter(const wchar_t* expr, const wchar_t* func, const wchar_t* file, unsigned line, uintptr_t)
{
    std::fwprintf(stderr, L"FATAL: CRT invalid parameter: %ls in %ls (%ls:%u)\n",
                  expr ? expr : L"?", func ? func : L"?", file ? file : L"?", line);
    std::_Exit(3);
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info)
{
    const EXCEPTION_RECORD* r = info->ExceptionRecord;
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             static_cast<LPCSTR>(r->ExceptionAddress), &module))
        ::GetModuleFileNameA(module, name, sizeof name);
    std::fprintf(stderr, "FATAL: exception 0x%08lX at %p (%s + 0x%llx)\n",
                 static_cast<unsigned long>(r->ExceptionCode), r->ExceptionAddress, name,
                 static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(r->ExceptionAddress) -
                                                 reinterpret_cast<uintptr_t>(module)));
    std::_Exit(3);
}
#endif

void InstallFatalReporters()
{
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

// ---- Cases --------------------------------------------------------------------------------

void TestSha256KnownAnswers()
{
    using sfe::decomp::Sha256Hex;
    CHECK(Sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(Sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const char* two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";   // crosses a block
    CHECK(Sha256Hex(two, std::strlen(two)) == "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    const std::string million(1000000, 'a');
    CHECK(Sha256Hex(million.data(), million.size()) == "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

// The engine, through the materialised directory.
void TestDecompileThroughBundle(const std::string& config)
{
    auto t0 = std::chrono::steady_clock::now();
    const sfe::decomp::SpecBundleResult b = sfe::decomp::MaterialiseSh2SpecBundle(config);
    std::printf("materialise: %.1f ms -> %s\n", MsSince(t0), b.ok ? b.dir.c_str() : b.error.c_str());
    CHECK(b.ok);
    CHECK(b.extracted);
    CHECK(!b.repaired);
    if (!b.ok) return;
    const std::string hash = sfe::decomp::SpecBundleHash(sfe::decomp::Sh2SpecFiles());
    CHECK(hash.size() == 16);
    CHECK(b.dir == Join(Join(config, "decompiler"), hash));
    CHECK(MatchesEmbedded(b.dir));

    t0 = std::chrono::steady_clock::now();
    ghidra::startDecompilerLibrary(std::vector<std::string>{ b.dir });
    const std::string c = DecompileFixture1();
    std::printf("decompile fixture 1 (library start, Architecture init, decompile, print): %.1f ms\n", MsSince(t0));
    std::printf("%s\n", c.c_str());
    CHECK(c.find("func_0x06004000(") != std::string::npos);
    CHECK(c.find("func_0x06005000(param_1)") != std::string::npos);   // the call through the literal pool
    CHECK(c.find("iVar1 + 1") != std::string::npos);
    CHECK(c.find("0 < param_1") != std::string::npos);                // the cmp/pl guard

    // A second Architecture on the same process-global translator gives the same text.
    t0 = std::chrono::steady_clock::now();
    CHECK(DecompileFixture1() == c);
    std::printf("decompile fixture 1 again: %.1f ms\n", MsSince(t0));
}

void TestReuseAndRepair(const std::string& config)
{
    sfe::decomp::SpecBundleResult b = sfe::decomp::MaterialiseSh2SpecBundle(config);
    CHECK(b.ok && !b.extracted && !b.repaired);   // already there and valid: used as it is
    if (!b.ok) return;
    const std::string dir = b.dir;

    // One flipped byte in the .sla.
    std::string sla;
    CHECK(sfe::ReadWholeFile(Join(dir, "sh-2.sla"), sla));
    sla[sla.size() / 2] ^= 0x5A;
    CHECK(WriteFile(Join(dir, "sh-2.sla"), sla.data(), sla.size()));
    b = sfe::decomp::MaterialiseSh2SpecBundle(config);
    CHECK(b.ok && b.repaired && b.extracted);
    CHECK(MatchesEmbedded(dir));

    // A truncated .cspec (same name, wrong size).
    CHECK(WriteFile(Join(dir, "superh.cspec"), "<", 1));
    b = sfe::decomp::MaterialiseSh2SpecBundle(config);
    CHECK(b.ok && b.repaired);
    CHECK(MatchesEmbedded(dir));

    // A missing file, and separately a missing manifest.
    CHECK(sfe::RemoveFile(Join(dir, "superh.ldefs")));
    b = sfe::decomp::MaterialiseSh2SpecBundle(config);
    CHECK(b.ok && b.repaired);
    CHECK(MatchesEmbedded(dir));
    CHECK(sfe::RemoveFile(Join(dir, "manifest.json")));
    b = sfe::decomp::MaterialiseSh2SpecBundle(config);
    CHECK(b.ok && b.repaired);
    CHECK(sfe::FileOrDirectoryExists(Join(dir, "manifest.json")));

    // A manifest from some other build (consistent with itself, not with these files).
    const char other[] = "{ \"bundle\": \"0000000000000000\", \"files\": [] }\n";
    CHECK(WriteFile(Join(dir, "manifest.json"), other, sizeof other - 1));
    b = sfe::decomp::MaterialiseSh2SpecBundle(config);
    CHECK(b.ok && b.repaired);

    // No staging directory is left behind by any of the above.
    std::vector<std::string> names;
    CHECK(sfe::ListDirectory(Join(config, "decompiler"), names));
    for (const std::string& n : names) CHECK(n.find(".tmp-") == std::string::npos);
}

void TestForeignHashUntouched(const std::string& config)
{
    const std::string root = Join(config, "decompiler");
    const std::string foreign = Join(root, "0123456789abcdef");
    CHECK(sfe::MakeDirectory(foreign));
    const char junk[] = "another build's spec, deliberately not valid for this one";
    CHECK(WriteFile(Join(foreign, "sh-2.sla"), junk, sizeof junk));

    // Damage ours too, so the repair path runs while the foreign directory sits beside it.
    const sfe::decomp::SpecBundleResult first = sfe::decomp::MaterialiseSh2SpecBundle(config);
    CHECK(first.ok);
    CHECK(sfe::RemoveFile(Join(first.dir, "sh-2.sla")));
    const sfe::decomp::SpecBundleResult b = sfe::decomp::MaterialiseSh2SpecBundle(config);
    CHECK(b.ok && b.repaired);

    std::string bytes;
    CHECK(sfe::ReadWholeFile(Join(foreign, "sh-2.sla"), bytes));
    CHECK(bytes.size() == sizeof junk && std::memcmp(bytes.data(), junk, sizeof junk) == 0);
    std::vector<std::string> names;
    CHECK(sfe::ListDirectory(foreign, names) && names.size() == 1);
}

// Something sitting where the bundle directory should be is a link to another directory --
// planted, or left by a user's tidy-up. Verification fails, and the repair must remove the link
// itself: following it would delete the files of whatever it points at. POSIX only (creating a
// symlink on Windows needs a privilege CI lacks; FileWriteTests covers RemoveFlatDirectory).
void TestLinkInPlaceOfBundle(const std::string& config)
{
#ifndef _WIN32
    const sfe::decomp::SpecBundleResult first = sfe::decomp::MaterialiseSh2SpecBundle(config);
    CHECK(first.ok);
    if (!first.ok) return;
    const std::string victim = Join(config, "unrelated");
    CHECK(sfe::MakeDirectory(victim));
    CHECK(WriteFile(Join(victim, "keep.txt"), "keep", 4));
    RemoveTree(first.dir);
    CHECK(::symlink(victim.c_str(), first.dir.c_str()) == 0);

    const sfe::decomp::SpecBundleResult b = sfe::decomp::MaterialiseSh2SpecBundle(config);
    CHECK(b.ok && b.repaired);
    CHECK(!sfe::IsLink(first.dir));
    CHECK(MatchesEmbedded(first.dir));
    std::vector<std::string> names;
    CHECK(sfe::ListDirectory(victim, names) && names.size() == 1);
    CHECK(sfe::FileOrDirectoryExists(Join(victim, "keep.txt")));
    RemoveTree(victim);
#else
    (void)config;
#endif
}

// Several instances find the same damaged bundle at once. Under the bundle lock exactly one
// repairs it and the rest see a valid directory; without it, a slow instance acting on its
// stale "damaged" verdict deletes the copy another has just verified and handed to its engine.
// Threads stand in for processes: flock and LockFileEx lock per open file, so they exclude
// each other within one process exactly as across processes.
void TestConcurrentRepair(const std::string& config)
{
    const sfe::decomp::SpecBundleResult first = sfe::decomp::MaterialiseSh2SpecBundle(config);
    CHECK(first.ok);
    if (!first.ok) return;
    CHECK(sfe::RemoveFile(Join(first.dir, "sh-2.sla")));

    const int n = 8;
    std::atomic<int> ready(0);
    std::vector<sfe::decomp::SpecBundleResult> results(n);
    std::vector<int> valid(n, 0);
    std::vector<std::thread> threads;
    for (int i = 0; i < n; ++i)
        threads.emplace_back([&, i] {
            ++ready;
            while (ready.load() < n) std::this_thread::yield();
            results[i] = sfe::decomp::MaterialiseSh2SpecBundle(config);
            valid[i] = results[i].ok && MatchesEmbedded(results[i].dir);   // still there on return
        });
    for (std::thread& t : threads) t.join();

    int repairs = 0;
    for (int i = 0; i < n; ++i)
    {
        CHECK(results[i].ok);
        CHECK(valid[i]);
        if (results[i].repaired) ++repairs;
    }
    std::printf("concurrent repair: %d of %d instances repaired\n", repairs, n);
    CHECK(repairs == 1);
    CHECK(MatchesEmbedded(first.dir));
}

// A staged copy that does not verify is never published: the failure is reported and the real
// directory does not appear. Before the fix the staged files were renamed into place first and
// only checked afterwards, leaving a bad directory under the real name.
void DamageStagedSla(const std::string& stagingDir)
{
    WriteFile(Join(stagingDir, "sh-2.sla"), "x", 1);
}

void TestStagedCopyVerifiedBeforePublishing(const std::string& config)
{
    const std::string dir = Join(Join(config, "decompiler"), sfe::decomp::SpecBundleHash(sfe::decomp::Sh2SpecFiles()));
    RemoveTree(dir);
    sfe::decomp::SetSpecBundleStagingHookForTests(DamageStagedSla);
    const sfe::decomp::SpecBundleResult b = sfe::decomp::MaterialiseSh2SpecBundle(config);
    sfe::decomp::SetSpecBundleStagingHookForTests(nullptr);
    std::printf("damaged staging copy -> %s\n", b.error.c_str());
    CHECK(!b.ok);
    CHECK(b.error.find("did not verify") != std::string::npos);
    CHECK(!sfe::PathEntryExists(dir));
    std::vector<std::string> names;
    CHECK(sfe::ListDirectory(Join(config, "decompiler"), names));
    for (const std::string& n : names) CHECK(n.find(".tmp-") == std::string::npos);

    const sfe::decomp::SpecBundleResult again = sfe::decomp::MaterialiseSh2SpecBundle(config);
    CHECK(again.ok && again.extracted);
}

// Deny writes into 'path' (and, on Windows, everything below it) the way a locked-down config
// dir would: mode 0500 on POSIX, a deny-write ACE for Everyone on Windows. Undone by
// AllowWrites. Root ignores mode bits, which the caller detects by probing.
void DenyWrites(const std::string& path)
{
#ifdef _WIN32
    const std::string cmd = "icacls \"" + path + "\" /deny \"*S-1-1-0:(OI)(CI)(WD,AD,DC)\" >nul 2>&1";
    CHECK(std::system(cmd.c_str()) == 0);
#else
    ::chmod(path.c_str(), 0500);
#endif
}

void AllowWrites(const std::string& path)
{
#ifdef _WIN32
    const std::string cmd = "icacls \"" + path + "\" /remove:d \"*S-1-1-0\" >nul 2>&1";
    CHECK(std::system(cmd.c_str()) == 0);
#else
    ::chmod(path.c_str(), 0700);
#endif
}

// A config directory nothing can be written into: "unavailable" naming the path, not a crash.
// One that was materialised while writable is still used; a damaged one cannot be repaired
// and says so, naming the directory.
void TestUnwritableConfigDir(const std::string& scratch)
{
    const std::string config = Join(scratch, "readonly-config");
    RemoveTree(config);
    CHECK(sfe::MakeDirectory(config));

    DenyWrites(config);
    const std::string probe = Join(config, "probe");
    const bool enforced = !sfe::MakeDirectory(probe);
    if (!enforced) sfe::RemoveEmptyDirectory(probe);
    // Only root may fall back: it bypasses mode bits by design. Anywhere else an unenforced
    // denial means this test has lost the coverage it exists for, and must say so.
#ifdef _WIN32
    const bool mayFallBack = false;
#else
    const bool mayFallBack = ::geteuid() == 0;
#endif
    CHECK(enforced || mayFallBack);
    std::string target = config;
    if (!enforced)
    {
        // Running as root: mode bits are not enforced. A config "directory" whose parent is a
        // regular file is unwritable for anyone.
        AllowWrites(config);
        const std::string file = Join(scratch, "not-a-directory");
        CHECK(WriteFile(file, "x", 1));
        target = Join(file, "config");
    }
    std::printf("unwritable config dir: %s\n", enforced ? "writes denied on the directory" : "path below a regular file");

    const sfe::decomp::SpecBundleResult b = sfe::decomp::MaterialiseSh2SpecBundle(target);
    std::printf("  -> %s\n", b.error.c_str());
    CHECK(!b.ok);
    CHECK(b.dir.empty());
    CHECK(b.error.find("unavailable") != std::string::npos);
    CHECK(b.error.find(target) != std::string::npos);

    if (enforced)
    {
        AllowWrites(config);
        const sfe::decomp::SpecBundleResult ok = sfe::decomp::MaterialiseSh2SpecBundle(config);
        CHECK(ok.ok);
        if (ok.ok)
        {
            const std::string root = Join(config, "decompiler");
            const std::string all[] = { config, root, ok.dir };
            for (const std::string& p : all) DenyWrites(p);
            const sfe::decomp::SpecBundleResult again = sfe::decomp::MaterialiseSh2SpecBundle(config);
            CHECK(again.ok && !again.extracted);
            for (int i = 2; i >= 0; --i) AllowWrites(all[i]);

            CHECK(sfe::RemoveFile(Join(ok.dir, "sh-2.sla")));
            for (const std::string& p : all) DenyWrites(p);
            const sfe::decomp::SpecBundleResult broken = sfe::decomp::MaterialiseSh2SpecBundle(config);
            std::printf("  damaged and read-only -> %s\n", broken.error.c_str());
            CHECK(!broken.ok);
            CHECK(broken.error.find(ok.dir) != std::string::npos);
            for (int i = 2; i >= 0; --i) AllowWrites(all[i]);
        }
    }
    RemoveTree(config);
}


// ---- CircleRange::pullBackUnary / pullBackBinary, against brute force (area 2, round 10: A2-64) -------------------------------------------
// RuleRangeMeld and the jump-table guards pull the range of a result back through the operation that made it, and treat what comes back as
// exactly the set of inputs that give such a result. Upstream's pullBackBinary answered a comparison that may be true or false (a range holding
// both 0 and 1) with [0,2) and the mask widened to the input's size where its own comment says "all possible ins": `addv` with a literal printed
// `1 < param_1 && ...` for `0x7fffffff < param_1 && ...` (patch 0058). The same sweep found a constant minus the unknown (INT_SUB, slot 1) pulled
// back as the reversed range, a constant shifted by the unknown (INT_RIGHT, INT_SRIGHT, slot 1) pulled back as the unknown shifted by the
// constant, an arithmetic shift by the size or more, and INT_SEXT's gap placed by `left & step`. Every operation is checked at one and two bytes
// over a grid of edge values and a seeded sample of the rest (ranges with a step of 2 and 4 too): a pull-back that succeeds must equal the set
// of inputs whose result is in the range, with the mask of the input; one that fails is allowed.
struct PullBackTally
{
    long cases = 0, failed = 0, exact = 0, wrong = 0;
};

uint64_t PbMask(int size) { return size >= 8 ? ~static_cast<uint64_t>(0) : ((static_cast<uint64_t>(1) << (8 * size)) - 1); }
int64_t PbSigned(uint64_t v, int size)
{
    const uint64_t sign = static_cast<uint64_t>(1) << (8 * size - 1);
    return static_cast<int64_t>((v ^ sign) - sign);
}

bool PbEvalBinary(ghidra::OpCode opc, uint64_t x, uint64_t val, int slot, int size, uint64_t& out)
{
    const uint64_t m = PbMask(size);
    const uint64_t a = slot == 0 ? x : val, b = slot == 0 ? val : x;
    const uint64_t width = 8 * static_cast<uint64_t>(size);
    switch (opc)
    {
    case ghidra::CPUI_INT_EQUAL: out = a == b; return true;
    case ghidra::CPUI_INT_NOTEQUAL: out = a != b; return true;
    case ghidra::CPUI_INT_LESS: out = a < b; return true;
    case ghidra::CPUI_INT_LESSEQUAL: out = a <= b; return true;
    case ghidra::CPUI_INT_SLESS: out = PbSigned(a, size) < PbSigned(b, size); return true;
    case ghidra::CPUI_INT_SLESSEQUAL: out = PbSigned(a, size) <= PbSigned(b, size); return true;
    case ghidra::CPUI_INT_CARRY: out = ((a + b) & m) < a; return true;
    case ghidra::CPUI_INT_ADD: out = (a + b) & m; return true;
    case ghidra::CPUI_INT_SUB: out = (a - b) & m; return true;
    case ghidra::CPUI_INT_RIGHT: out = (b >= width) ? 0 : (a >> b); return true;
    case ghidra::CPUI_INT_SRIGHT: out = static_cast<uint64_t>(PbSigned(a, size) >> (b >= width ? width - 1 : b)) & m; return true;
    default: return false;
    }
}

uint64_t PbUnary(ghidra::OpCode opc, uint64_t x, int inSize, int outSize)
{
    switch (opc)
    {
    case ghidra::CPUI_BOOL_NEGATE: return x ^ 1;
    case ghidra::CPUI_COPY: return x;
    case ghidra::CPUI_INT_2COMP: return (0 - x) & PbMask(outSize);
    case ghidra::CPUI_INT_NEGATE: return (~x) & PbMask(outSize);
    case ghidra::CPUI_INT_ZEXT: return x;
    default: return static_cast<uint64_t>(PbSigned(x, inSize)) & PbMask(outSize);   // INT_SEXT
    }
}

// True when the pulled-back range is exactly 'truth' (a table over the input space), in the input's mask.
bool PbEqual(const ghidra::CircleRange& q, int inSize, const std::vector<char>& truth, std::string& why)
{
    if (q.isEmpty())
    {
        for (size_t x = 0; x < truth.size(); ++x)
            if (truth[x]) { why = "empty, but " + std::to_string(x) + " belongs"; return false; }
        return true;
    }
    if (q.getMask() != PbMask(inSize)) { why = "mask " + std::to_string(q.getMask()); return false; }
    for (size_t x = 0; x < truth.size(); ++x)
        if (q.contains(x) != (truth[x] != 0))
        {
            why = std::string(truth[x] ? "misses " : "holds ") + std::to_string(x) + " [" + std::to_string(q.getMin()) + "," + std::to_string(q.getEnd()) + ")";
            return false;
        }
    return true;
}

void TestPullBackRanges()
{
    using namespace ghidra;
    // splitmix64
    uint64_t state = 0x5A17;
    auto next = [&state]() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };
    struct Bin { OpCode opc; const char* name; bool cmp; };
    static const Bin bins[] = { { CPUI_INT_EQUAL, "INT_EQUAL", true }, { CPUI_INT_NOTEQUAL, "INT_NOTEQUAL", true }, { CPUI_INT_LESS, "INT_LESS", true },
                                { CPUI_INT_LESSEQUAL, "INT_LESSEQUAL", true }, { CPUI_INT_SLESS, "INT_SLESS", true }, { CPUI_INT_SLESSEQUAL, "INT_SLESSEQUAL", true },
                                { CPUI_INT_CARRY, "INT_CARRY", true }, { CPUI_INT_ADD, "INT_ADD", false }, { CPUI_INT_SUB, "INT_SUB", false },
                                { CPUI_INT_RIGHT, "INT_RIGHT", false }, { CPUI_INT_SRIGHT, "INT_SRIGHT", false } };
    int shown = 0;
    long total = 0, exact = 0, failedPullBacks = 0;
    for (int in = 1; in <= 2; ++in)
    {
        const uint64_t ms = PbMask(in), space = ms + 1;
        std::vector<uint64_t> edge = { 0, 1, 2, 3, 0x7e, 0x7f, 0x80, 0x81, 0xfe, 0xff, ms - 1, ms, ms >> 1, (ms >> 1) + 1, (ms >> 1) + 2 };
        if (in == 2) for (uint64_t v : { 0x100u, 0x101u, 0xff00u, 0x7f00u, 0x8000u }) edge.push_back(v);
        for (uint64_t& v : edge) v &= ms;
        for (const Bin& b : bins)
        {
            const int out = b.cmp ? 1 : in;
            const uint64_t om = PbMask(out);
            for (size_t it = 0; it < edge.size() * edge.size() + 3000; ++it)
            {
                uint64_t l, r, val;
                int slot = static_cast<int>(next() & 1), step = 1;
                if (it < edge.size() * edge.size()) { l = edge[it / edge.size()]; r = edge[it % edge.size()]; val = edge[next() % edge.size()]; }
                else
                {
                    l = next() & om; r = next() & om; val = (next() % 3) ? edge[next() % edge.size()] : (next() & ms);
                    if (!b.cmp && next() % 4 == 0)
                    {
                        step = 1 << (1 + next() % 2);
                        l = ((l - l % step) + (next() % step)) & om;
                        r = ((r - r % step) + (l % step)) & om;
                    }
                }
                if (b.cmp)
                {
                    static const uint64_t bl[5] = { 0, 1, 0, 2, 0 }, br[5] = { 1, 2, 2, 3, 2 };   // {0}, {1}, {0,1}, neither, {0,1} again
                    const size_t pick = it % 5;
                    l = bl[pick]; r = br[pick];
                }
                if (b.opc == CPUI_INT_RIGHT || b.opc == CPUI_INT_SRIGHT) { if (next() % 4) { slot = 0; val = next() % (8 * in + 3); } }
                if (step > 1 && l == r) continue;
                CircleRange range(l, r, out, step);
                std::vector<char> truth(space, 0);
                for (uint64_t x = 0; x < space; ++x)
                {
                    uint64_t o = 0;
                    PbEvalBinary(b.opc, x, val, slot, in, o);
                    truth[x] = range.contains(o);
                }
                CircleRange q = range;
                ++total;
                if (!q.pullBackBinary(b.opc, val, slot, in, out)) { ++failedPullBacks; continue; }
                std::string why;
                if (PbEqual(q, in, truth, why)) { ++exact; continue; }
                CHECK(false);
                if (shown++ < 12)
                    std::printf("pull-back %s, %d byte(s): range [%llx,%llx) step %d, constant %llx in slot %d: %s\n", b.name, in, (unsigned long long)l,
                                (unsigned long long)r, step, (unsigned long long)val, slot, why.c_str());
            }
        }
        struct Un { OpCode opc; const char* name; int in, out; };
        const Un uns[] = { { CPUI_BOOL_NEGATE, "BOOL_NEGATE", 1, 1 }, { CPUI_COPY, "COPY", in, in }, { CPUI_INT_2COMP, "INT_2COMP", in, in },
                           { CPUI_INT_NEGATE, "INT_NEGATE", in, in }, { CPUI_INT_ZEXT, "INT_ZEXT", in, 4 }, { CPUI_INT_SEXT, "INT_SEXT", in, 4 } };
        for (const Un& u : uns)
        {
            const uint64_t om = PbMask(u.out), usp = PbMask(u.in) + 1;
            std::vector<uint64_t> ue = { 0, 1, 2, 3, 0x7e, 0x7f, 0x80, 0x81, 0xfe, 0xff, 0x100, 0x101, 0x7f00, 0x7fff, 0x8000, 0xff00, 0xffffff80u, 0xffffff7fu, 0xffff8000u, 0xffff7fffu, 0xffffffffu };
            for (uint64_t& v : ue) v &= om;
            for (size_t it = 0; it < ue.size() * ue.size() + 3000; ++it)
            {
                uint64_t l, r;
                int step = 1;
                if (it < ue.size() * ue.size()) { l = ue[it / ue.size()]; r = ue[it % ue.size()]; }
                else
                {
                    l = next() & om; r = next() & om;
                    if (next() % 4 == 0)
                    {
                        step = 1 << (1 + next() % 2);
                        l = ((l - l % step) + (next() % step)) & om;
                        r = ((r - r % step) + (l % step)) & om;
                    }
                }
                if (u.opc == CPUI_BOOL_NEGATE) { const size_t pick = it % 3; static const uint64_t bl[3] = { 0, 1, 0 }, br[3] = { 1, 2, 2 }; l = bl[pick]; r = br[pick]; step = 1; }
                if (step > 1 && l == r) continue;
                CircleRange range(l, r, u.out, step);
                std::vector<char> truth(usp, 0);
                for (uint64_t x = 0; x < usp; ++x) truth[x] = range.contains(PbUnary(u.opc, x, u.in, u.out)) && (u.opc != CPUI_BOOL_NEGATE || x < 2);
                if (u.opc == CPUI_BOOL_NEGATE) { for (uint64_t x = 2; x < usp; ++x) truth[x] = 0; }
                CircleRange q = range;
                ++total;
                if (!q.pullBackUnary(u.opc, u.in, u.out)) { ++failedPullBacks; continue; }
                std::string why;
                bool ok;
                if (u.opc == CPUI_BOOL_NEGATE)   // the input is a boolean: only 0 and 1 are asked about
                {
                    ok = q.getMask() == 0xff && q.contains(0) == (truth[0] != 0) && q.contains(1) == (truth[1] != 0);
                    if (!ok) why = "a boolean pull-back differs";
                }
                else
                    ok = PbEqual(q, u.in, truth, why);
                if (ok) { ++exact; continue; }
                CHECK(false);
                if (shown++ < 12)
                    std::printf("pull-back %s, %d -> %d byte(s): range [%llx,%llx) step %d: %s\n", u.name, u.in, u.out, (unsigned long long)l, (unsigned long long)r, step, why.c_str());
            }
        }
    }
    std::printf("pull-back sweep: %ld cases, %ld exact, %ld refused\n", total, exact, failedPullBacks);
    CHECK(total - exact - failedPullBacks == 0);
    CHECK(exact > total / 2);   // the sweep must keep checking something: most pull-backs succeed
    // A2-64 itself: a comparison whose result may be true or false is true or false for every input
    for (OpCode opc : { CPUI_INT_EQUAL, CPUI_INT_NOTEQUAL, CPUI_INT_LESS, CPUI_INT_LESSEQUAL, CPUI_INT_SLESS, CPUI_INT_SLESSEQUAL, CPUI_INT_CARRY })
    {
        CircleRange both(0, 2, 1, 1);   // {0, 1}
        CHECK(both.pullBackBinary(opc, 0xAAAAAAABu, 1, 4, 1));
        CHECK(both.isFull() && both.getMask() == 0xFFFFFFFFu);
    }
}

// Area 2 round 11 (A2-67 to A2-72): what RuleRangeMeld and the jump-table guards rely on besides the pull-back, held against brute force. The
// forward half of CircleRange (push-forward) and the strided set operations are pinned by TestForwardRangesRound11 below (patch 0059).
//  1. step-1 ranges of one byte, every (left,right) pair against a sample of partners: intersect and circleUnion are exactly the set operation
//     when they answer 0, invert is the complement, translate2Op's comparison is true on exactly the range, getSize counts, contains agrees
//  2. the pull-back at 3, 4 and 8 bytes (round 10's sweep is exhaustive at 1 and 2), the points where the truth can change, each +-3, and random ones
//     (the shifts by 0 and by the size less one at 8 bytes: A2-69)
//  3. the integer OpBehavior (the constant folder and the p-code emulator share it): every operand pair at one byte, edge pairs and random at 2, 4, 8
void TestRangeAlgebraRound11()
{
    using namespace ghidra;
    const auto t0 = std::chrono::steady_clock::now();
    uint64_t state = 0x11AA55;
    auto next = [&state]() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };
    int shown = 0;
    auto fail = [&shown](const char* what, const std::string& detail) {
        CHECK(false);
        if (shown++ < 12) std::printf("round 11: %s: %s\n", what, detail.c_str());
    };
    auto hex = [](uint64_t v) { char b[32]; std::snprintf(b, sizeof b, "%llx", (unsigned long long)v); return std::string(b); };
    // ---- 1. set operations on step-1 ranges, one byte
    {
        std::vector<std::vector<char>> elems(65536);
        for (unsigned l = 0; l < 256; ++l)
            for (unsigned r = 0; r < 256; ++r)
            {
                std::vector<char>& e = elems[l * 256 + r];
                e.assign(256, 0);
                if (l == r) { e.assign(256, 1); continue; }
                for (unsigned v = l; v != r; v = (v + 1) & 0xff) e[v] = 1;
            }
        long checked = 0;
        for (unsigned l = 0; l < 256; ++l)
            for (unsigned r = 0; r < 256; ++r)
            {
                const std::vector<char>& ea = elems[l * 256 + r];
                const bool full = (l == r);
                CircleRange a(l, r, 1, 1);
                if (full) a.setFull(1);
                // contains / size / invert / translate2Op on a alone
                long n = 0;
                for (unsigned v = 0; v < 256; ++v)
                {
                    n += ea[v];
                    if (a.contains((uint64_t)v) != (ea[v] != 0)) { fail("contains(value)", hex(l) + "," + hex(r) + " at " + hex(v)); break; }
                }
                if (!full && (long)a.getSize() != n) fail("getSize", hex(l) + "," + hex(r));
                if (full && a.getSize() != 256) fail("getSize(full)", hex(a.getSize()));
                {
                    CircleRange inv = a;
                    inv.invert();
                    for (unsigned v = 0; v < 256; ++v)
                        if (inv.contains((uint64_t)v) == (ea[v] != 0)) { fail("invert", hex(l) + "," + hex(r) + " at " + hex(v)); break; }
                }
                {
                    OpCode opc = CPUI_COPY;
                    uintb c = 0;
                    int cslot = 0;
                    const int rc = a.translate2Op(opc, c, cslot);
                    if (rc == 0)
                    {
                        for (unsigned v = 0; v < 256; ++v)
                        {
                            const uint64_t x = v, lhs = cslot == 1 ? x : c, rhs = cslot == 1 ? c : x;
                            bool t = false;
                            if (opc == CPUI_INT_EQUAL) t = x == c;
                            else if (opc == CPUI_INT_NOTEQUAL) t = x != c;
                            else if (opc == CPUI_INT_LESS) t = lhs < rhs;
                            else if (opc == CPUI_INT_SLESS) t = PbSigned(lhs, 1) < PbSigned(rhs, 1);
                            else { fail("translate2Op", "op " + std::to_string((int)opc)); break; }
                            if (t != (ea[v] != 0)) { fail("translate2Op", hex(l) + "," + hex(r) + " at " + hex(v)); break; }
                        }
                    }
                    else if (rc == 1) { if (n != 256) fail("translate2Op all", hex(l) + "," + hex(r)); }
                    else if (rc == 3) { if (n != 0) fail("translate2Op none", hex(l) + "," + hex(r)); }
                }
                // against partners
                for (int k = 0; k < 3; ++k)
                {
                    const unsigned bl = (k == 0) ? (unsigned)(next() & 0xff) : (k == 1 ? l : (unsigned)(next() & 0xff));
                    const unsigned br = (k == 0) ? (unsigned)(next() & 0xff) : (k == 1 ? (unsigned)((r + 1) & 0xff) : (unsigned)(next() & 0xff));
                    const std::vector<char>& eb = elems[bl * 256 + br];
                    CircleRange b(bl, br, 1, 1);
                    if (bl == br) b.setFull(1);
                    ++checked;
                    {
                        CircleRange q = a;
                        if (q.intersect(b) == 0)
                            for (unsigned v = 0; v < 256; ++v)
                                if (q.contains((uint64_t)v) != (ea[v] && eb[v])) { fail("intersect", hex(l) + "," + hex(r) + " & " + hex(bl) + "," + hex(br) + " at " + hex(v)); break; }
                    }
                    {
                        CircleRange q = a;
                        if (q.circleUnion(b) == 0)
                            for (unsigned v = 0; v < 256; ++v)
                                if (q.contains((uint64_t)v) != (ea[v] || eb[v])) { fail("circleUnion", hex(l) + "," + hex(r) + " | " + hex(bl) + "," + hex(br) + " at " + hex(v)); break; }
                    }
                }
            }
        std::printf("round 11 range algebra: %ld pairs\n", checked);
    }
    // ---- 2. the pull-back at 3 and 4 bytes
    {
        struct Bin { OpCode opc; bool cmp; };
        static const Bin bins[] = { { CPUI_INT_EQUAL, true }, { CPUI_INT_NOTEQUAL, true }, { CPUI_INT_LESS, true }, { CPUI_INT_LESSEQUAL, true }, { CPUI_INT_SLESS, true },
                                    { CPUI_INT_SLESSEQUAL, true }, { CPUI_INT_CARRY, true }, { CPUI_INT_ADD, false }, { CPUI_INT_SUB, false }, { CPUI_INT_RIGHT, false },
                                    { CPUI_INT_SRIGHT, false } };
        auto inRange = [](uint64_t v, uint64_t l, uint64_t r, int step, int osz) {
            v &= PbMask(osz);
            if (v % step != l % step) return false;
            if (l == r) return true;
            return l < r ? (v >= l && v < r) : (v < r || v >= l);
        };
        for (int in : { 3, 4, 8 })
        {
            const uint64_t ms = PbMask(in);
            std::vector<uint64_t> ed = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 31, 32, 33, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x10000, 0xffffff, 0x1000000, 0x7fffffff, 0x80000000ull,
                                         0xffffffffull, ms, ms - 1, ms >> 1, (ms >> 1) + 1, 0xaaaaaaabull, 0x55555555ull };
            for (uint64_t& v : ed) v &= ms;
            for (const Bin& b : bins)
            {
                const int out = b.cmp ? 1 : in;
                const uint64_t om = PbMask(out);
                for (int it = 0; it < 2500; ++it)
                {
                    uint64_t l, r, val;
                    int slot = (int)(next() & 1), step = 1;
                    auto pick = [&](uint64_t m) { return (next() % 3) ? (ed[next() % ed.size()] & m) : (next() & m); };
                    if (b.cmp) { const int p = (int)(next() % 3); l = p == 1 ? 1 : 0; r = p == 0 ? 1 : 2; val = pick(ms); }
                    else
                    {
                        l = pick(om); r = (next() % 8) ? pick(om) : l; val = pick(ms);
                        if (next() % 4 == 0) { step = 1 << (1 + next() % 3); const uint64_t rem = next() % step; l = ((l - l % step) + rem) & om; r = ((r - r % step) + rem) & om; if (l % step != r % step) r = l; }
                        if (l == r) l = r = (step == 1) ? 0 : l % step;
                    }
                    if (b.opc == CPUI_INT_RIGHT || b.opc == CPUI_INT_SRIGHT)
                    {
                        slot = (next() % 4) ? 0 : 1;
                        if (slot == 0)
                        {
                            const uint64_t amounts[] = { 0, 1, 2, static_cast<uint64_t>(8 * in - 2), static_cast<uint64_t>(8 * in - 1), static_cast<uint64_t>(8 * in), static_cast<uint64_t>(8 * in + 1) };
                            val = (next() % 3) ? amounts[next() % 7] : next() % (8 * in + 3);
                        }
                    }
                    CircleRange rng(l, r, out, step), q = rng;
                    if (!q.pullBackBinary(b.opc, val, slot, in, out)) continue;
                    std::vector<uint64_t> T = { 0, 1, ms, ms >> 1, (ms >> 1) + 1, val, l, r, 0 - l, 0 - r, ~l, ~r, val - l, val - r, val + l, val + r, l - val, r - val, 0 - val,
                                                q.getMin(), q.getEnd(), q.getMax(), r - 1, l << (val & 63), r << (val & 63), (r << (val & 63)) - 1 };
                    std::vector<uint64_t> X;
                    for (uint64_t t : T) for (int d = -3; d <= 3; ++d) X.push_back((t + d) & ms);
                    for (int k = 0; k < 20; ++k) X.push_back(next() & ms);
                    if (!q.isEmpty() && q.getMask() != ms) { fail("pull-back mask", std::to_string(in) + " bytes"); continue; }
                    for (uint64_t x : X)
                    {
                        uint64_t o = 0;
                        PbEvalBinary(b.opc, x, val, slot, in, o);
                        const bool want = inRange(o, l, r, step, out), got = !q.isEmpty() && q.contains(x);
                        if (want != got)
                        {
                            fail("pull-back", "op " + std::to_string((int)b.opc) + " " + std::to_string(in) + " bytes range [" + hex(l) + "," + hex(r) + ") step " + std::to_string(step) + " constant " + hex(val) +
                                                 " slot " + std::to_string(slot) + " at " + hex(x));
                            break;
                        }
                    }
                }
            }
        }
    }
    // ---- 3. the integer OpBehavior
    {
        OpBehaviorIntAdd add; OpBehaviorIntSub sub; OpBehaviorIntMult mul; OpBehaviorIntAnd andb; OpBehaviorIntOr orb; OpBehaviorIntXor xorb;
        OpBehaviorIntLeft shl; OpBehaviorIntRight shr; OpBehaviorIntSright sar; OpBehaviorEqual eq; OpBehaviorIntLess lt; OpBehaviorIntSless slt;
        OpBehaviorIntCarry carry; OpBehaviorIntScarry scarry; OpBehaviorIntSborrow sborrow; OpBehaviorIntDiv udiv; OpBehaviorIntSdiv sdiv;
        OpBehaviorIntRem urem; OpBehaviorIntSrem srem; OpBehaviorInt2Comp neg; OpBehaviorIntNegate nt; OpBehaviorIntSext sext;
        long n = 0;
        for (int sz : { 1, 2, 4, 8 })
        {
            const uint64_t m = PbMask(sz);
            auto sx = [&](uint64_t v) { return PbSigned(v, sz); };
            std::vector<uint64_t> ed = { 0, 1, 2, 3, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 129, 255, 256, m, m - 1, m >> 1, (m >> 1) + 1, (m >> 1) - 1, 0x5555555555555555ull & m, 0xAAAAAAAAAAAAAAAAull & m };
            for (uint64_t& v : ed) v &= m;
            auto pair = [&](uint64_t a, uint64_t b) {
                ++n;
                const uint64_t sign = uint64_t(1) << (8 * sz - 1);
                const int64_t lo = PbSigned(sign, sz), hi = static_cast<int64_t>(sign - 1);
                const int64_t sa = sx(a), sb = sx(b);
                auto bad = [&](const char* nm) { fail(nm, std::to_string(sz) + " bytes " + hex(a) + " " + hex(b)); };
                if (add.evaluateBinary(sz, sz, a, b) != ((a + b) & m)) bad("INT_ADD");
                if (sub.evaluateBinary(sz, sz, a, b) != ((a - b) & m)) bad("INT_SUB");
                if (mul.evaluateBinary(sz, sz, a, b) != ((a * b) & m)) bad("INT_MULT");
                if (andb.evaluateBinary(sz, sz, a, b) != (a & b) || orb.evaluateBinary(sz, sz, a, b) != (a | b) || xorb.evaluateBinary(sz, sz, a, b) != (a ^ b)) bad("bitwise");
                if (eq.evaluateBinary(1, sz, a, b) != (uint64_t)(a == b) || lt.evaluateBinary(1, sz, a, b) != (uint64_t)(a < b)) bad("compare");
                if (slt.evaluateBinary(1, sz, a, b) != (uint64_t)(sx(a) < sx(b))) bad("INT_SLESS");
                // Compare against the remaining range instead of evaluating a sum
                // or difference that can overflow. This oracle also works on MSVC,
                // which has no __int128 type.
                if (carry.evaluateBinary(1, sz, a, b) != (uint64_t)(a > m - b)) bad("INT_CARRY");
                const bool addOverflow = (sb > 0 && sa > hi - sb) || (sb < 0 && sa < lo - sb);
                const bool subOverflow = (sb < 0 && sa > hi + sb) || (sb > 0 && sa < lo + sb);
                if (scarry.evaluateBinary(1, sz, a, b) != (uint64_t)addOverflow) bad("INT_SCARRY");
                if (sborrow.evaluateBinary(1, sz, a, b) != (uint64_t)subOverflow) bad("INT_SBORROW");
                if (b != 0)
                {
                    if (udiv.evaluateBinary(sz, sz, a, b) != a / b || urem.evaluateBinary(sz, sz, a, b) != a % b) bad("INT_DIV/REM");
                    // INT64_MIN s/ -1 trapped in the host (A2-70, patch 0059): it is the value itself, and the remainder 0
                    {
                        const bool wraps = sa == lo && sb == -1;
                        const uint64_t quotient = wraps ? a : static_cast<uint64_t>(sa / sb) & m;
                        const uint64_t remainder = wraps ? 0 : static_cast<uint64_t>(sa % sb) & m;
                        if (sdiv.evaluateBinary(sz, sz, a, b) != quotient || srem.evaluateBinary(sz, sz, a, b) != remainder) bad("INT_SDIV/SREM");
                    }
                }
            };
            if (sz == 1) { for (uint64_t a = 0; a < 256; ++a) for (uint64_t b = 0; b < 256; ++b) pair(a, b); }
            else
            {
                for (uint64_t a : ed) for (uint64_t b : ed) pair(a, b);
                for (int i = 0; i < 20000; ++i) pair(next() & m, (i % 4) ? (next() & m) : ed[next() % ed.size()]);
            }
            for (uint64_t a : ed)
                for (uint64_t c : { 0ull, 1ull, 3ull, 7ull, 8ull, 15ull, 16ull, 31ull, 32ull, 33ull, 63ull, 64ull, 65ull, 255ull, 0x100000000ull, ~0ull })
                {
                    const uint64_t exL = c >= (uint64_t)(8 * sz) ? 0 : (a << c) & m, exR = c >= (uint64_t)(8 * sz) ? 0 : a >> c;
                    const uint64_t sh = c >= (uint64_t)(8 * sz) ? 8 * sz - 1 : c;
                    const uint64_t exS = (uint64_t)(sx(a) >> sh) & m;
                    if (shl.evaluateBinary(sz, sz, a, c) != exL || shr.evaluateBinary(sz, sz, a, c) != exR || sar.evaluateBinary(sz, sz, a, c) != exS)
                        fail("shift", std::to_string(sz) + " bytes " + hex(a) + " by " + hex(c));
                }
            for (uint64_t a : ed)
            {
                if (neg.evaluateUnary(sz, sz, a) != ((~a + 1) & m) || nt.evaluateUnary(sz, sz, a) != (~a & m)) fail("INT_2COMP/NEGATE", hex(a));
                if (sz < 8 && sext.evaluateUnary(8, sz, a) != (uint64_t)PbSigned(a, sz)) fail("INT_SEXT", hex(a));
            }
        }
        std::printf("round 11 integer behaviors: %ld operand pairs (%.0f ms in all)\n", n, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    }
}


// ---- The forward half of CircleRange and the set operations with a step, against brute force (area 2 round 11 fix: A2-67, A2-68) -------------------
// ValueSet::iterate pushes the value sets of the inputs of an operation forward through it and unions them; Heritage::analyzeNewLoadGuards takes
// the range of a computed pointer from the answer. An answer that omits a value the operation yields guards too little of the stack. The rule
// (patch 0059): a push-forward that returns true, and a union or container, holds every value the operation can produce from the elements of
// its inputs, in the output's mask, and is a well-formed range; one that returns false is allowed. One byte is exhaustive (every range of step 1,
// 2, 4 and 8: 123,000 of them, wrapped, full and single ones among them, against every shift amount and a set of multipliers and partners); two,
// four and eight bytes are the edge values of the size and a seeded sample, with the elements at both ends of each input and random ones.
struct FwRange
{
    uint64_t l, r;
    int step;
};

bool FwFull(const FwRange& g) { return g.l == g.r; }
uint64_t FwCount(const FwRange& g, uint64_t m)   // the number of elements
{
    if (FwFull(g)) return m / g.step + 1;
    return ((g.r - g.l) & m) / g.step;
}
uint64_t FwElem(const FwRange& g, uint64_t m, uint64_t j) { return (g.l + j * static_cast<uint64_t>(g.step)) & m; }
bool FwMember(uint64_t v, const FwRange& g, uint64_t m)   // the definition of the set, written out (contains() semantics)
{
    v &= m;
    if (v % g.step != g.l % g.step) return false;
    if (g.l == g.r) return true;
    return g.l < g.r ? (v >= g.l && v < g.r) : (v < g.r || v >= g.l);
}
bool FwWellFormed(const ghidra::CircleRange& q, int size, std::string& why)
{
    if (q.isEmpty()) return true;
    if (q.getMask() != PbMask(size)) { why = "mask " + std::to_string(q.getMask()); return false; }
    const int st = q.getStep();
    if (st < 1 || (st & (st - 1))) { why = "step not a power of two"; return false; }
    if ((q.getMin() & ~q.getMask()) || (q.getEnd() & ~q.getMask())) { why = "a bound outside the mask"; return false; }
    if (q.getMin() % st != q.getEnd() % st) { why = "bounds differ modulo the step"; return false; }
    return true;
}
std::string FwText(const FwRange& g)
{
    char b[96];
    std::snprintf(b, sizeof b, "[%llx,%llx)/%d", static_cast<unsigned long long>(g.l), static_cast<unsigned long long>(g.r), g.step);
    return b;
}
std::string FwText(const ghidra::CircleRange& q)
{
    char b[128];
    std::snprintf(b, sizeof b, "[%llx,%llx)/%d m%llx%s", static_cast<unsigned long long>(q.getMin()), static_cast<unsigned long long>(q.getEnd()), q.getStep(),
                  static_cast<unsigned long long>(q.getMask()), q.isEmpty() ? " empty" : "");
    return b;
}

// The elements to push through an operation: all of them when there are few, else both ends and a seeded sample.
template <class Next>
void FwElements(const FwRange& g, uint64_t m, Next& next, std::vector<uint64_t>& out)
{
    out.clear();
    const uint64_t n = FwCount(g, m);
    if (n <= 300)
    {
        for (uint64_t j = 0; j < n; ++j) out.push_back(FwElem(g, m, j));
        return;
    }
    for (uint64_t j = 0; j < 12; ++j) { out.push_back(FwElem(g, m, j)); out.push_back(FwElem(g, m, n - 1 - j)); }
    for (int k = 0; k < 80; ++k) out.push_back(FwElem(g, m, next() % n));
    // the values around the sign boundary and zero, where a range that wraps changes behaviour
    for (uint64_t t : { m >> 1, (m >> 1) + 1, m, static_cast<uint64_t>(0) })
        for (int d = -3; d <= 3; ++d)
        {
            const uint64_t v = (t + d) & m;
            if (FwMember(v, g, m)) out.push_back(v);
        }
}

struct FwTally
{
    long cases = 0, refused = 0, exact = 0;
};

void TestForwardRangesRound11()
{
    using namespace ghidra;
    const auto t0 = std::chrono::steady_clock::now();
    uint64_t state = 0xF0C0FFEE;
    auto next = [&state]() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };
    int shown = 0;
    auto fail = [&shown](const std::string& what, const std::string& detail) {
        CHECK(false);
        if (shown++ < 14) std::printf("forward round 11: %s: %s\n", what.c_str(), detail.c_str());
    };
    auto hex = [](uint64_t v) { char b[32]; std::snprintf(b, sizeof b, "%llx", static_cast<unsigned long long>(v)); return std::string(b); };

    // The ranges of a size. One byte: every one. Larger: the edge values paired, steps 1 to 8, and random ones.
    auto makeRanges = [&](int size, bool exhaustive, int randoms) {
        std::vector<FwRange> out;
        const uint64_t m = PbMask(size);
        if (exhaustive)
        {
            for (int st : { 1, 2, 4, 8 })
                for (uint64_t l = 0; l <= m; ++l)
                    for (uint64_t r = 0; r <= m; ++r)
                    {
                        if (l % st != r % st) continue;
                        if (l == r && l >= static_cast<uint64_t>(st)) continue;   // a full range is written with its phase as the bound
                        out.push_back({ l, r, st });
                    }
            return out;
        }
        std::vector<uint64_t> ed = { 0, 1, 2, 3, 5, 7, 8, 9, 16, 17, 33, 64, 0x7f, 0x80, 0x81, 0xc0, 0xfe, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x1000000, 0x7fffffff, 0x80000000ull,
                                     0xffffffffull, m, m - 1, m >> 1, (m >> 1) + 1, (m >> 1) - 1, m >> 2, (m >> 2) + 1, m - (m >> 2) };
        if (size == 2)   // the 16-bit space is 2^32 ranges: the bounds are every pairing of these, the two sign boundaries and both ends
            for (uint64_t v : { 4, 6, 10, 0x7e, 0x7ffe, 0x7ffd, 0x8002, 0xfffe, 0x3fff, 0x4000, 0xc000, 0xbfff, 0xff, 0x101, 0x180, 0xff00, 0xff01, 0x7f00, 0x8100, 0xfe00 }) ed.push_back(v);
        for (uint64_t& v : ed) v &= m;
        std::sort(ed.begin(), ed.end());
        ed.erase(std::unique(ed.begin(), ed.end()), ed.end());
        auto fix = [&](uint64_t l, uint64_t r, int st) {
            const uint64_t rem = l % st;
            r = ((r - r % st) + rem) & m;
            if (r % st != rem) r = l;
            if (l == r) l = r = rem;
            out.push_back({ l, r, st });
        };
        for (int st : { 1, 2, 4, 8 })
            for (uint64_t l : ed)
                for (uint64_t r : ed) fix(l, r, st);
        for (int k = 0; k < randoms; ++k)
        {
            const int st = 1 << (next() % 4 == 0 ? 1 + next() % 3 : 0);
            const uint64_t l = (next() % 3) ? ed[next() % ed.size()] : (next() & m), r = (next() % 3) ? ed[next() % ed.size()] : (next() & m);
            fix(l, (next() % 12) ? r : l, st);
        }
        return out;
    };

    // ---- 1. push-forward, unary and binary
    long checks = 0, refused = 0;
    std::vector<uint64_t> el, el2;
    auto judge = [&](const char* op, const FwRange& a, const std::string& param, bool ok, const CircleRange& q, int outSize, const std::vector<uint64_t>& images) {
        ++checks;
        if (!ok) { ++refused; return; }
        std::string why;
        if (!FwWellFormed(q, outSize, why)) { fail(op, FwText(a) + " " + param + " -> " + FwText(q) + ": " + why); return; }
        for (uint64_t y : images)
            if (q.isEmpty() || !q.contains(y)) { fail(op, FwText(a) + " " + param + " -> " + FwText(q) + " misses " + hex(y)); return; }
    };
    struct Case { int size; bool exhaustive; int randoms; };
    const Case cases[] = { { 1, true, 0 }, { 2, false, 1500 }, { 4, false, 1500 }, { 8, false, 1500 } };
    for (const Case& cs : cases)
    {
        const int size = cs.size;
        const uint64_t m = PbMask(size);
        const std::vector<FwRange> ranges = makeRanges(size, cs.exhaustive, cs.randoms);
        std::vector<uint64_t> images;
        for (const FwRange& a : ranges)
        {
            const CircleRange A(a.l, a.r, size, a.step);
            FwElements(a, m, next, el);
            // unary, the same size
            for (OpCode opc : { CPUI_COPY, CPUI_INT_2COMP, CPUI_INT_NEGATE })
            {
                images.clear();
                for (uint64_t x : el) images.push_back(opc == CPUI_COPY ? x : opc == CPUI_INT_2COMP ? ((0 - x) & m) : ((~x) & m));
                CircleRange q;
                judge("unary", a, "op " + std::to_string(static_cast<int>(opc)), q.pushForwardUnary(opc, A, size, size), q, size, images);
            }
            if (size <= 4)   // the extensions
                for (OpCode opc : { CPUI_INT_ZEXT, CPUI_INT_SEXT })
                {
                    const int out = size * 2;
                    images.clear();
                    for (uint64_t x : el) images.push_back(opc == CPUI_INT_ZEXT ? x : (static_cast<uint64_t>(PbSigned(x, size)) & PbMask(out)));
                    CircleRange q;
                    judge("extension", a, "op " + std::to_string(static_cast<int>(opc)), q.pushForwardUnary(opc, A, size, out), q, out, images);
                }
            // the shifts by a constant, the amounts round the size as well as small ones
            std::vector<uint64_t> amounts;
            if (cs.exhaustive) amounts = { 0, 1, 2, 3, 4, 7, 8, 9, 64 };
            else
            {
                const uint64_t special[] = { 0, 1, 2, 3, 8, 9, static_cast<uint64_t>(8 * size - 2), static_cast<uint64_t>(8 * size - 1), static_cast<uint64_t>(8 * size), static_cast<uint64_t>(8 * size + 1), 63, 64, 65, 0x100000000ull };
                amounts = { special[next() % 14], special[next() % 14], special[next() % 14], next() % (8 * size + 2) };
            }
            for (uint64_t sa : amounts)
            {
                const CircleRange C(sa, 4);
                const int maxStep = (next() % 3 == 0) ? 1 : 32;
                for (OpCode opc : { CPUI_INT_LEFT, CPUI_INT_RIGHT, CPUI_INT_SRIGHT })
                {
                    images.clear();
                    for (uint64_t x : el)
                    {
                        uint64_t y;
                        if (opc == CPUI_INT_LEFT) y = sa >= static_cast<uint64_t>(8 * size) ? 0 : (x << sa) & m;
                        else if (opc == CPUI_INT_RIGHT) y = sa >= static_cast<uint64_t>(8 * size) ? 0 : x >> sa;
                        else y = static_cast<uint64_t>(PbSigned(x, size) >> (sa >= static_cast<uint64_t>(8 * size) ? 8 * size - 1 : sa)) & m;
                        images.push_back(y);
                    }
                    CircleRange q;
                    judge("shift", a, "op " + std::to_string(static_cast<int>(opc)) + " by " + hex(sa), q.pushForwardBinary(opc, A, C, size, size, maxStep), q, size, images);
                }
            }
            // the product by a constant, either side
            static const uint64_t mults[] = { 0, 1, 2, 3, 4, 6, 0x10, 0x55, 0x7f, 0x80, 0xaa, 0xfe, 0xff, 0x100, 0xffff, 0xffffffffffffffffull, 0xfffffffffffffff0ull, 0x8000000000000000ull };
            for (uint64_t c0 : mults)
            {
                const uint64_t c = c0 & m;
                if (cs.exhaustive ? (c0 > 0xff) : (next() % 3 != 0)) continue;
                images.clear();
                for (uint64_t x : el) images.push_back((x * c) & m);
                const CircleRange C(c, size);
                const int maxStep = (next() % 3 == 0) ? 2 : 32;
                CircleRange q;
                judge("INT_MULT", a, "by " + hex(c), q.pushForwardBinary(CPUI_INT_MULT, A, C, size, size, maxStep), q, size, images);
                CircleRange q2;
                judge("INT_MULT (constant first)", a, "by " + hex(c), q2.pushForwardBinary(CPUI_INT_MULT, C, A, size, size, maxStep), q2, size, images);
            }
            // the sum of two ranges: partners of fixed shapes and two others
            if (cs.exhaustive ? (next() % 8 == 0) : true)
            {
                for (int k = 0; k < (cs.exhaustive ? 3 : 1); ++k)
                {
                    const FwRange& b = ranges[next() % ranges.size()];
                    FwElements(b, m, next, el2);
                    images.clear();
                    for (uint64_t x : el)
                        for (size_t i = 0; i < el2.size() && i < 12; ++i) images.push_back((x + el2[i]) & m);
                    // the sample of b must be the one whose elements are summed: ends and the first 24 of el2 are in b
                    const CircleRange B(b.l, b.r, size, b.step);
                    CircleRange q;
                    judge("INT_ADD", a, "+ " + FwText(b), q.pushForwardBinary(CPUI_INT_ADD, A, B, size, size, 32), q, size, images);
                }
            }
        }
    }
    // SUBPIECE: the low and high bytes of 2, 4 and 8 bytes
    for (int in : { 2, 4, 8 })
    {
        const uint64_t m = PbMask(in);
        const std::vector<FwRange> ranges = makeRanges(in, false, 1500);
        std::vector<uint64_t> images;
        for (const FwRange& a : ranges)
        {
            const CircleRange A(a.l, a.r, in, a.step);
            FwElements(a, m, next, el);
            for (int out = 1; out < in; out *= 2)
                for (int off = 0; off <= in; ++off)
                {
                    if (off + out > in && off != in) continue;
                    images.clear();
                    for (uint64_t x : el) images.push_back(off >= in ? 0 : ((x >> (8 * off)) & PbMask(out)));
                    CircleRange q;
                    judge("SUBPIECE", a, "to " + std::to_string(out) + " bytes at " + std::to_string(off), q.pushForwardBinary(CPUI_SUBPIECE, A, CircleRange(static_cast<uint64_t>(off), 4), in, out, 32), q, out, images);
                }
        }
    }
    std::printf("forward round 11: %ld push-forwards checked (%ld refused)\n", checks, refused);
    CHECK(checks > 1000000 && refused < checks / 2);

    // ---- 2. the set operations with steps, wrapped and full ranges
    long pairs = 0, exactInter = 0, twoPieces = 0;
    for (const Case& cs : cases)
    {
        const int size = cs.size;
        const uint64_t m = PbMask(size);
        std::vector<FwRange> ranges = makeRanges(size, cs.exhaustive, cs.randoms);
        std::vector<std::array<uint64_t, 4>> bits;   // the elements of each one-byte range
        if (cs.exhaustive)
        {
            bits.resize(ranges.size());
            for (size_t i = 0; i < ranges.size(); ++i)
            {
                bits[i] = { 0, 0, 0, 0 };
                const uint64_t n = FwCount(ranges[i], m);
                for (uint64_t j = 0; j < n; ++j) { const uint64_t v = FwElem(ranges[i], m, j); bits[i][v >> 6] |= static_cast<uint64_t>(1) << (v & 63); }
            }
        }
        // fixed partners: a full range of each step and phase, a single, a wrapped one, the halves
        std::vector<FwRange> fixedPartners = { { 0, 0, 1 }, { 1, 1, 2 }, { 0, 0, 4 }, { 3, 3, 4 }, { 2, 2, 8 }, { 5, 6, 1 }, { 1, 3, 2 }, { m - 1, 2, 1 }, { m - 3, 5, 2 }, { m >> 1, (m >> 1) + 1, 1 }, { (m >> 1) + 1, 0, 1 }, { 0, (m >> 1) + 1, 1 } };
        for (FwRange& g : fixedPartners)
        {
            g.l &= m; g.r &= m;
            if (g.l % g.step != g.r % g.step) g.r = g.l;
            if (g.l == g.r) g.l = g.r = g.l % g.step;
        }
        auto bitsOf = [&](const FwRange& g) {
            std::array<uint64_t, 4> b = { 0, 0, 0, 0 };
            const uint64_t n = FwCount(g, m);
            for (uint64_t j = 0; j < n; ++j) { const uint64_t v = FwElem(g, m, j); b[v >> 6] |= static_cast<uint64_t>(1) << (v & 63); }
            return b;
        };
        for (size_t ia = 0; ia < ranges.size(); ++ia)
        {
            const FwRange& a = ranges[ia];
            const CircleRange A(a.l, a.r, size, a.step);
            // getMaxInfo: at least the bits of the largest magnitude of the elements
            {
                FwElements(a, m, next, el);
                int worst = 0;
                for (uint64_t x : el)
                {
                    const int64_t sv = PbSigned(x, size);
                    uint64_t mag = sv < 0 ? static_cast<uint64_t>(0) - static_cast<uint64_t>(sv) : static_cast<uint64_t>(sv);
                    int bl = 0;
                    while (mag) { ++bl; mag >>= 1; }
                    worst = std::max(worst, bl);
                }
                if (A.getMaxInfo() < worst) fail("getMaxInfo", FwText(a) + " is " + std::to_string(A.getMaxInfo()) + ", needs " + std::to_string(worst));
            }
            const int total = cs.exhaustive ? 5 : 6 + static_cast<int>(fixedPartners.size());
            for (int k = 0; k < total; ++k)
            {
                const FwRange b = cs.exhaustive ? (k < 2 ? ranges[next() % ranges.size()] : fixedPartners[next() % fixedPartners.size()])
                                                : (k < 6 ? ranges[next() % ranges.size()] : fixedPartners[k - 6]);
                const CircleRange B(b.l, b.r, size, b.step);
                ++pairs;
                const std::string both = FwText(a) + " , " + FwText(b);
                // How a result is compared: at one byte by the sets of 256 bits, else at the points round each bound, the phases and the ends.
                std::array<uint64_t, 4> ba = { 0, 0, 0, 0 }, bb = ba;
                std::vector<uint64_t> pts;
                if (cs.exhaustive) { ba = bits[ia]; bb = bitsOf(b); }
                else
                {
                    for (uint64_t t : { a.l, a.r, b.l, b.r, static_cast<uint64_t>(0), m, m >> 1, (m >> 1) + 1 })
                        for (int d = -9; d <= 9; ++d) pts.push_back((t + d) & m);
                    for (int q = 0; q < 40; ++q) pts.push_back(next() & m);
                }
                // mode 0: q is exactly A and B; 1: exactly A or B; 2: q holds A or B; 3: q holds everything. Returns a value at which q differs, or -1.
                auto firstDiff = [&](const CircleRange& q, int mode) -> int64_t {
                    if (cs.exhaustive)
                    {
                        std::array<uint64_t, 4> qb = { 0, 0, 0, 0 };
                        if (!q.isEmpty())
                        {
                            uint64_t v = q.getMin();
                            for (int guard = 0; guard < 300; ++guard)
                            {
                                qb[v >> 6] |= static_cast<uint64_t>(1) << (v & 63);
                                if (!q.getNext(v)) break;
                            }
                        }
                        for (int w = 0; w < 4; ++w)
                        {
                            const uint64_t want = mode == 0 ? (ba[w] & bb[w]) : mode == 1 ? (ba[w] | bb[w]) : mode == 2 ? (ba[w] | bb[w]) : ~static_cast<uint64_t>(0);
                            const uint64_t diff = (mode >= 2) ? (want & ~qb[w]) : (want ^ qb[w]);
                            if (diff) { int bit = 0; while (!((diff >> bit) & 1)) ++bit; return static_cast<int64_t>(w * 64 + bit); }
                        }
                        return -1;
                    }
                    for (uint64_t v : pts)
                    {
                        const bool has = !q.isEmpty() && q.contains(v);
                        const bool inAny = FwMember(v, a, m) || FwMember(v, b, m), inBoth = FwMember(v, a, m) && FwMember(v, b, m);
                        if (mode == 0 && has != inBoth) return static_cast<int64_t>(v);
                        if (mode == 1 && has != inAny) return static_cast<int64_t>(v);
                        if (mode == 2 && inAny && !has) return static_cast<int64_t>(v);
                        if (mode == 3 && !has) return static_cast<int64_t>(v);
                    }
                    return -1;
                };
                // intersect
                {
                    CircleRange q = A;
                    const int rc = q.intersect(B);
                    if (rc == 0)
                    {
                        std::string why;
                        if (!FwWellFormed(q, size, why)) fail("intersect", both + " -> " + FwText(q) + ": " + why);
                        else
                        {
                            const int64_t d = firstDiff(q, 0);
                            if (d >= 0) fail("intersect", both + " -> " + FwText(q) + " differs at " + hex(static_cast<uint64_t>(d)));
                            else ++exactInter;
                        }
                    }
                    else
                    {
                        ++twoPieces;
                        if (!(q == A)) fail("intersect (two pieces)", both + " changed this");
                    }
                }
                // circleUnion
                {
                    CircleRange q = A;
                    if (q.circleUnion(B) == 0)
                    {
                        std::string why;
                        if (!FwWellFormed(q, size, why)) fail("circleUnion", both + " -> " + FwText(q) + ": " + why);
                        else
                        {
                            const int64_t d = firstDiff(q, 1);
                            if (d >= 0) fail("circleUnion", both + " -> " + FwText(q) + " differs at " + hex(static_cast<uint64_t>(d)));
                        }
                    }
                }
                // the container of two ranges (what ValueSet::iterate takes when the union is two pieces; a full range reaches it with another step)
                {
                    CircleRange q = A;
                    const bool everything = q.minimalContainer(B, 1 << (next() % 6));
                    std::string why;
                    if (!FwWellFormed(q, size, why)) fail("minimalContainer", both + " -> " + FwText(q) + ": " + why);
                    else
                    {
                        int64_t d = firstDiff(q, 2);
                        if (d >= 0) fail("minimalContainer", both + " -> " + FwText(q) + " misses " + hex(static_cast<uint64_t>(d)));
                        else if (everything && (d = firstDiff(q, 3)) >= 0) fail("minimalContainer (everything)", both + " -> " + FwText(q) + " misses " + hex(static_cast<uint64_t>(d)));
                    }
                }
                // containment claimed is containment
                if (A.contains(B))
                {
                    if (cs.exhaustive)
                    {
                        for (int w = 0; w < 4; ++w)
                            if (bb[w] & ~ba[w]) { fail("contains(range)", both); break; }
                    }
                    else
                        for (uint64_t v : pts)
                            if (FwMember(v, b, m) && !FwMember(v, a, m)) { fail("contains(range)", both + " at " + hex(v)); break; }
                }
            }
        }
    }
    std::printf("forward round 11: %ld range pairs (intersect exact for %ld, two pieces for %ld; %.0f ms in all)\n", pairs, exactInter, twoPieces,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
}

// The push-forwards at every size from 1 to 8 bytes (area 2 round 12). Round 11's pin has 1, 2, 4 and 8; the engine meets 3, 5, 6 and 7 as
// the pieces of a CONCAT and a SUBPIECE, and every extension, not only the doubling one (`extu.b` is 1 to 4). The ranges are drawn from structured
// bounds (powers of two and their neighbours, both sign boundaries, the ends), steps 1 to 32; the elements pushed through an operation are the ends,
// the points round zero, the top and the sign boundary, and for a product the points where it crosses a multiple of the domain.
void TestForwardRangesRound12()
{
    using namespace ghidra;
    const auto t0 = std::chrono::steady_clock::now();
    uint64_t state = 0x12B0A7;
    auto next = [&state]() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };
    int shown = 0;
    auto fail = [&shown](const std::string& what, const std::string& detail) {
        CHECK(false);
        if (shown++ < 14) std::printf("forward round 12: %s: %s\n", what.c_str(), detail.c_str());
    };
    auto hex = [](uint64_t v) { char b[32]; std::snprintf(b, sizeof b, "%llx", static_cast<unsigned long long>(v)); return std::string(b); };
    auto pickVal = [&](int size) -> uint64_t {
        const uint64_t m = PbMask(size);
        switch (next() % 8)
        {
        case 0: return next() & m;
        case 1: return (next() % 17) & m;
        case 2: return (m - next() % 17) & m;
        case 3: return ((m >> 1) + next() % 17 - 8) & m;
        case 4: return ((static_cast<uint64_t>(1) << (next() % (8 * size))) + next() % 9 - 4) & m;
        case 5: return ((0 - (static_cast<uint64_t>(1) << (next() % (8 * size)))) + next() % 9 - 4) & m;
        case 6: return (next() & next() & next()) & m;
        default: return (next() | next() | next()) & m;
        }
    };
    auto genRange = [&](int size) {
        const uint64_t m = PbMask(size);
        static const int steps[] = { 1, 1, 1, 1, 2, 2, 4, 8, 16, 32 };
        int st = steps[next() % 10];
        while (static_cast<uint64_t>(st) > m) st >>= 1;
        FwRange g;
        g.step = st;
        g.l = pickVal(size);
        switch (next() % 12)
        {
        case 0: g.r = g.l; break;
        case 1: g.r = (g.l + st) & m; break;
        case 2: g.r = (g.l + 2 * st) & m; break;
        case 3: g.r = (g.l - st) & m; break;
        default: g.r = pickVal(size);
        }
        const uint64_t rem = g.l % st;
        g.r = (g.r - g.r % st + rem) & m;
        if (g.r % st != rem) g.r = g.l;
        if (g.l == g.r) g.l = g.r = rem;
        return g;
    };
    auto sample = [&](const FwRange& g, int size, const std::vector<uint64_t>& extra, std::vector<uint64_t>& out) {
        const uint64_t m = PbMask(size);
        out.clear();
        const uint64_t n = FwCount(g, m);
        if (n <= 64) for (uint64_t j = 0; j < n; ++j) out.push_back(FwElem(g, m, j));
        else
        {
            for (uint64_t j = 0; j < 8; ++j) { out.push_back(FwElem(g, m, j)); out.push_back(FwElem(g, m, n - 1 - j)); }
            for (int k = 0; k < 40; ++k) out.push_back(FwElem(g, m, next() % n));
        }
        std::vector<uint64_t> targets = { 0, m, m >> 1, (m >> 1) + 1 };
        for (uint64_t t : extra) targets.push_back(t & m);
        for (uint64_t t : targets)
            for (int d = -5; d <= 5; ++d)
            {
                const uint64_t v = (t + d) & m;
                if (FwMember(v, g, m)) out.push_back(v);
            }
    };
    long checks = 0;
    auto judge = [&](const char* op, const FwRange& a, const std::string& param, bool ok, const CircleRange& q, int outSize, const std::vector<uint64_t>& images) {
        ++checks;
        if (!ok) return;
        std::string why;
        if (!FwWellFormed(q, outSize, why)) { fail(op, FwText(a) + " " + param + " -> " + FwText(q) + ": " + why); return; }
        for (uint64_t y : images)
            if (q.isEmpty() || !q.contains(y) || !FwMember(y, FwRange{ q.getMin(), q.getEnd(), q.getStep() }, PbMask(outSize)))
            {
                fail(op, FwText(a) + " " + param + " -> " + FwText(q) + " misses " + hex(y));
                return;
            }
    };
    const int iters = 6000;
    std::vector<uint64_t> el, el2, im;
    for (int size = 1; size <= 8; ++size)
    {
        const uint64_t m = PbMask(size);
        for (int it = 0; it < iters; ++it)
        {
            const FwRange a = genRange(size);
            const CircleRange A(a.l, a.r, size, a.step);
            // shifts by a constant of 1, 4 or 8 bytes
            {
                static const uint64_t amounts[] = { 0, 1, 2, 3, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 255, 0x7fffffffull, 0x80000000ull, 0xffffffffull, 0x100000000ull, 0x8000000000000000ull, ~0ull };
                const uint64_t sa = (next() % 3) ? amounts[next() % (sizeof amounts / sizeof amounts[0])] : next() % (8 * size + 4);
                int asz = sa <= 0xff ? ((next() % 2) ? 1 : 4) : (sa <= 0xffffffffull ? 4 : 8);
                std::vector<uint64_t> extra;
                if (sa > 0 && sa < static_cast<uint64_t>(8 * size))   // the shift 8 * size - sa is below 64
                    for (int j = 1; j <= 3; ++j) for (int d = -1; d <= 1; ++d) extra.push_back(((static_cast<uint64_t>(j) << (8 * size - sa)) + d) & m);
                sample(a, size, extra, el);
                const CircleRange C(sa, asz);
                const int maxStep = 1 << (next() % 6);
                for (OpCode opc : { CPUI_INT_LEFT, CPUI_INT_RIGHT, CPUI_INT_SRIGHT })
                {
                    im.clear();
                    for (uint64_t x : el)
                    {
                        uint64_t y;
                        if (opc == CPUI_INT_LEFT) y = sa >= static_cast<uint64_t>(8 * size) ? 0 : (x << sa) & m;
                        else if (opc == CPUI_INT_RIGHT) y = sa >= static_cast<uint64_t>(8 * size) ? 0 : x >> sa;
                        else y = static_cast<uint64_t>(PbSigned(x, size) >> (sa >= static_cast<uint64_t>(8 * size) ? 8 * size - 1 : sa)) & m;
                        im.push_back(y);
                    }
                    CircleRange q;
                    judge("shift", a, "op " + std::to_string(static_cast<int>(opc)) + " size " + std::to_string(size) + " by " + hex(sa), q.pushForwardBinary(opc, A, C, size, size, maxStep), q, size, im);
                }
            }
            // the product by a constant, either side: the points where it crosses a multiple of the domain among the elements
            {
                uint64_t c;
                switch (next() % 6)
                {
                case 0: c = next() % 20; break;
                case 1: c = 0 - next() % 20; break;
                case 2: c = static_cast<uint64_t>(1) << (next() % (8 * size)); break;
                case 3: c = (static_cast<uint64_t>(1) << (next() % (8 * size))) + next() % 5 - 2; break;
                case 4: c = pickVal(size); break;
                default: c = next();
                }
                c &= m;
                const uint64_t mag = (c & (m ^ (m >> 1))) ? ((0 - c) & m) : c;
                std::vector<uint64_t> extra;
                if (mag > 1)
                    for (int j = 1; j <= 4; ++j)
                    {
                        const long double x = static_cast<long double>(j) * (m == ~static_cast<uint64_t>(0) ? 18446744073709551616.0L : static_cast<long double>(m + 1)) / static_cast<long double>(mag);
                        for (int d = -1; d <= 1; ++d) { const uint64_t xv = static_cast<uint64_t>(x); extra.push_back((xv + d) & m); extra.push_back((0 - xv - d) & m); }
                    }
                sample(a, size, extra, el);
                im.clear();
                for (uint64_t x : el) im.push_back((x * c) & m);
                const CircleRange C(c, size);
                const int maxStep = 1 << (next() % 6);
                CircleRange q, q2;
                judge("INT_MULT", a, "size " + std::to_string(size) + " by " + hex(c), q.pushForwardBinary(CPUI_INT_MULT, A, C, size, size, maxStep), q, size, im);
                judge("INT_MULT (constant first)", a, "size " + std::to_string(size) + " by " + hex(c), q2.pushForwardBinary(CPUI_INT_MULT, C, A, size, size, maxStep), q2, size, im);
            }
            // the sum of two ranges, and unary operations, and every extension from this size
            {
                FwRange b = genRange(size);
                if (next() % 6 == 0) { b.l = pickVal(size); b.step = 1; b.r = (b.l + 1) & m; }
                const CircleRange B(b.l, b.r, size, b.step);
                sample(a, size, {}, el);
                sample(b, size, {}, el2);
                im.clear();
                for (uint64_t x : el) for (size_t i = 0; i < el2.size(); i += 1 + (el2.size() > 40 ? 3 : 0)) im.push_back((x + el2[i]) & m);
                CircleRange q;
                judge("INT_ADD", a, "+ " + FwText(b) + " size " + std::to_string(size), q.pushForwardBinary(CPUI_INT_ADD, A, B, size, size, 32), q, size, im);
                for (OpCode opc : { CPUI_COPY, CPUI_INT_2COMP, CPUI_INT_NEGATE })
                {
                    im.clear();
                    for (uint64_t x : el) im.push_back(opc == CPUI_INT_2COMP ? ((0 - x) & m) : opc == CPUI_INT_NEGATE ? ((~x) & m) : x);
                    CircleRange u;
                    judge("unary", a, "op " + std::to_string(static_cast<int>(opc)) + " size " + std::to_string(size), u.pushForwardUnary(opc, A, size, size), u, size, im);
                }
                for (int out = size + 1; out <= 8; ++out)
                    for (OpCode opc : { CPUI_INT_ZEXT, CPUI_INT_SEXT })
                    {
                        im.clear();
                        for (uint64_t x : el) im.push_back(opc == CPUI_INT_ZEXT ? x : (static_cast<uint64_t>(PbSigned(x, size)) & PbMask(out)));
                        CircleRange u;
                        judge("extension", a, "op " + std::to_string(static_cast<int>(opc)) + " " + std::to_string(size) + " to " + std::to_string(out), u.pushForwardUnary(opc, A, size, out), u, out, im);
                    }
            }
            // every SUBPIECE of this size
            if (size >= 2)
            {
                sample(a, size, {}, el);
                for (int out = 1; out < size; ++out)
                    for (int off = 0; off + out <= size; ++off)
                    {
                        im.clear();
                        for (uint64_t x : el) im.push_back((x >> (8 * off)) & PbMask(out));
                        CircleRange q;
                        judge("SUBPIECE", a, std::to_string(size) + " to " + std::to_string(out) + " bytes at " + std::to_string(off), q.pushForwardBinary(CPUI_SUBPIECE, A, CircleRange(static_cast<uint64_t>(off), 4), size, out, 32), q, out, im);
                    }
            }
        }
    }
    std::printf("forward round 12: %ld push-forwards checked, sizes 1 to 8 (%.0f ms)\n", checks, MsSince(t0));
    CHECK(checks > 1000000);
}

// PcodeOp::getNZMaskLocal for a signed shift by a constant of 64 or more (A2-70): the result of x s>> n is the sign's fill, all bits of the
// size when the sign may be set. The shift of the full mask by 64 is a shift by 0 on x86, which gave a value that is not zero the mask of a zero.
void TestNzMaskShiftRound11()
{
    auto* image = new RegionLoadImage(kImageBase, Fixture1());
    std::ostringstream errs;
    TestArchitecture arch(image, &errs);
    ghidra::DocumentStorage store;
    try
    {
        arch.init(store);
    }
    catch (ghidra::LowlevelError& e)
    {
        std::printf("  Architecture::init failed: %s\n  %s\n", e.explain.c_str(), errs.str().c_str());
        CHECK(false);
        return;
    }
    ghidra::AddrSpace* ram = arch.getDefaultCodeSpace();
    const ghidra::Address entry(ram, kCaller);
    std::string name;
    arch.nameFunction(entry, name);
    ghidra::Funcdata* fd = arch.symboltab->getGlobalScope()->addFunction(entry, name)->getFunction();
    long checked = 0;
    for (int size : { 1, 2, 4, 8 })
    {
        const uint64_t m = PbMask(size);
        for (uint64_t x : { static_cast<uint64_t>(0), static_cast<uint64_t>(1), m >> 1, (m >> 1) + 1, m, (m >> 1) + 1 + (m >> 5) , static_cast<uint64_t>(0xf4000000u) })
        {
            x &= m;
            for (int saSize : { 4, 8 })
                for (uint64_t sa : { 0ull, 1ull, 7ull, 8ull, 31ull, 32ull, 33ull, 63ull, 64ull, 65ull, 71ull, 72ull, 127ull, 128ull, 255ull, 0xffffffffull, 0x100000000ull, 0x8000000000000000ull, ~0ull })
                {
                    if (saSize == 4 && sa > 0xffffffffull) continue;
                    ghidra::PcodeOp* op = fd->newOp(2, entry);
                    fd->opSetOpcode(op, ghidra::CPUI_INT_SRIGHT);
                    fd->newUniqueOut(size, op);
                    fd->opSetInput(op, fd->newConstant(size, x), 0);
                    fd->opSetInput(op, fd->newConstant(saSize, sa), 1);
                    const uint64_t nz = op->getNZMaskLocal(false);
                    const uint64_t truth = static_cast<uint64_t>(PbSigned(x, size) >> (sa >= static_cast<uint64_t>(8 * size) ? 8 * size - 1 : sa)) & m;
                    ++checked;
                    if ((truth & ~nz) != 0 || (nz & ~m) != 0)
                    {
                        CHECK(false);
                        std::printf("non-zero mask of %d-byte %llx s>> %llx is %llx, the result is %llx\n", size, static_cast<unsigned long long>(x), static_cast<unsigned long long>(sa),
                                    static_cast<unsigned long long>(nz), static_cast<unsigned long long>(truth));
                    }
                }
        }
    }
    std::printf("nz mask of signed shifts: %ld checked\n", checked);
}

// ---- Area 2 round 12 P3 fix (patches/0062-engine-range-set-ops-and-rule-guards.patch) ----------------------------------------------------------------
// Three CircleRange operations that patch 0059 did not touch, upstream's, none reached by a workload (ledger A2-73 to A2-75). The rule is 0059's: an
// answer holds every value, or fails, or is everything.

// A set of the one-byte domain, as 4 words: the elements of a range of the given mask that are at most 255.
struct RcBits
{
    uint64_t w[4] = { 0, 0, 0, 0 };
    void set(unsigned v) { w[v >> 6] |= static_cast<uint64_t>(1) << (v & 63); }
    bool get(unsigned v) const { return (w[v >> 6] >> (v & 63)) & 1; }
    bool operator==(const RcBits& o) const { return w[0] == o.w[0] && w[1] == o.w[1] && w[2] == o.w[2] && w[3] == o.w[3]; }
};

// Ranges of one size from structured bounds (the edges of the byte and of the word, the sign boundaries, the ends), steps 1 to 8, in the normal form
// (the bounds congruent modulo the step, a full range written with its phase for both bounds).
std::vector<FwRange> RcStructuredRanges(int size, const std::vector<uint64_t>& edges)
{
    std::vector<FwRange> out;
    const uint64_t m = PbMask(size);
    for (int st : { 1, 2, 4, 8 })
        for (uint64_t l : edges)
            for (uint64_t r0 : edges)
            {
                l &= m;
                const uint64_t rem = l % st;
                uint64_t r = ((r0 & m) - (r0 & m) % st + rem) & m;
                if (r % st != rem) r = l;
                uint64_t ll = l;
                if (ll == r) ll = r = rem;
                out.push_back({ ll, r, st });
            }
    return out;
}

// A2-73: CircleRange::intersect of ranges of two masks. newDomain cuts the wider range to the narrower mask and read a range that starts above the
// narrower mask and ends at the top of the wider domain (the right bound 0, or the phase of the top for a stride) as the wrapped pair
// [left,top] + [0,right): its right bound is not a piece at the bottom of the narrow domain, so the intersection is empty and not all of it.
// An answer of 0 must be exactly the elements of both ranges within the narrower domain (an answer of 2 leaves the range as it was).
void TestIntersectMixedMasksRound12()
{
    using namespace ghidra;
    const auto t0 = std::chrono::steady_clock::now();
    uint64_t state = 0x7A2C73;
    auto next = [&state]() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };
    int shown = 0;
    auto fail = [&shown](const std::string& what, const std::string& detail) {
        CHECK(false);
        if (shown++ < 14) std::printf("intersect mixed masks round 12: %s: %s\n", what.c_str(), detail.c_str());
    };
    // the ledger's three: a one-byte range against a two-byte range that holds values above the byte only (0x8000 to 0xffff, 0x7ffb to 0xfffd step 1,
    // 0x801 to the top at step 8); the intersection is empty
    {
        struct Fx { uint64_t al, ar; int as; uint64_t bl, br; int bs; };
        const Fx fxs[] = { { 0, 0x10, 1, 0x8000, 0, 1 }, { 0xf8, 0xf4, 4, 0x7ffb, 0xfffe, 1 }, { 0x1, 0x9, 8, 0x801, 0xffff, 1 }, { 0, 0, 1, 0x100, 0, 1 }, { 3, 3, 4, 0x8003, 3, 4 } };
        for (const Fx& f : fxs)
            for (int order = 0; order < 2; ++order)
            {
                CircleRange a(f.al, f.ar, 1, f.as), b(f.bl, f.br, 2, f.bs);
                int rc;
                if (order == 0) { rc = a.intersect(b); CHECK(rc == 0 && a.isEmpty()); }
                else { rc = b.intersect(a); CHECK(rc == 0 && b.isEmpty()); }
            }
    }
    std::vector<uint64_t> byteEdges = { 0, 1, 2, 3, 4, 5, 7, 8, 0x10, 0x20, 0x3f, 0x40, 0x7e, 0x7f, 0x80, 0x81, 0xc0, 0xf0, 0xf4, 0xf8, 0xfc, 0xfe, 0xff };
    std::vector<uint64_t> wordEdges = byteEdges;
    for (uint64_t v : { 0x100, 0x101, 0x103, 0x180, 0x7ff8, 0x7ffb, 0x7fff, 0x8000, 0x8001, 0x8003, 0x801, 0x8100, 0xff00, 0xff03, 0xfff0, 0xfff8, 0xfffc, 0xfffe, 0xffff, 0x1000 })
        wordEdges.push_back(v);
    const std::vector<FwRange> bytes = RcStructuredRanges(1, byteEdges), words = RcStructuredRanges(2, wordEdges);
    auto bitsOf = [](const FwRange& g, int size) {
        RcBits b;
        for (unsigned v = 0; v <= 255; ++v)
            if (FwMember(v, g, PbMask(size))) b.set(v);
        return b;
    };
    std::vector<RcBits> byteBits, wordBits;
    for (const FwRange& g : bytes) byteBits.push_back(bitsOf(g, 1));
    for (const FwRange& g : words) wordBits.push_back(bitsOf(g, 2));
    long pairs = 0, answered = 0, empties = 0;
    for (size_t wi = 0; wi < words.size(); ++wi)
        for (int k = 0; k < 48; ++k)
        {
            const size_t bi = next() % bytes.size();
            const FwRange &w = words[wi], &by = bytes[bi];
            RcBits want;
            for (int i = 0; i < 4; ++i) want.w[i] = wordBits[wi].w[i] & byteBits[bi].w[i];
            for (int order = 0; order < 2; ++order)
            {
                const CircleRange B(w.l, w.r, 2, w.step), A(by.l, by.r, 1, by.step);
                CircleRange q = order == 0 ? A : B;
                const int rc = order == 0 ? q.intersect(B) : q.intersect(A);
                ++pairs;
                const std::string both = FwText(by) + "(1 byte) " + FwText(w) + "(2 bytes), " + (order == 0 ? "narrow first" : "wide first");
                if (rc != 0)
                {
                    if (!(q == (order == 0 ? A : B))) fail("an answer of 2 changed the range", both);
                    continue;
                }
                ++answered;
                if (q.isEmpty()) ++empties;
                RcBits got;
                if (!q.isEmpty())
                {
                    std::string why;
                    if (!FwWellFormed(q, 1, why)) { fail("malformed", both + " -> " + FwText(q) + ": " + why); continue; }
                    for (unsigned v = 0; v <= 255; ++v)
                        if (q.contains(v)) got.set(v);
                }
                if (!(got == want))
                {
                    unsigned at = 0;
                    for (unsigned v = 0; v <= 255; ++v) if (got.get(v) != want.get(v)) { at = v; break; }
                    fail(want.get(at) ? "an element is missing" : "an element too many", both + " -> " + FwText(q) + " at " + std::to_string(at));
                }
            }
        }
    std::printf("intersect of two masks round 12: %ld pairs, %ld answered (%ld empty), %.0f ms\n", pairs, answered, empties, MsSince(t0));
    CHECK(pairs > 400000);
    CHECK(empties > 1000);
}

// A2-74: CircleRange::setNZMask built its step as `int4 step = 1; step <<= leastsigbit_set(nzmask)`: for a lowest set bit of 31 or more the shift
// leaves an int4. 4 bytes, mask 0x80000000: the step was -2^31 and the range [0,0) held 0 and not 0x80000000; 8 bytes, bit 63: the same with a bound
// 0x7fffffff80000000; a lowest bit of 32 to 62 shifted by its count modulo 32 (a finer step). No range is the answer for a step that does not fit
// (as for a mask of too many transitions); a range that is answered holds every value under the mask.
void TestSetNZMaskRound12()
{
    using namespace ghidra;
    const auto t0 = std::chrono::steady_clock::now();
    uint64_t state = 0x5E7A2C74;
    auto next = [&state]() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };
    int shown = 0;
    auto fail = [&shown](const std::string& what, const std::string& detail) {
        CHECK(false);
        if (shown++ < 14) std::printf("setNZMask round 12: %s: %s\n", what.c_str(), detail.c_str());
    };
    auto hex = [](uint64_t v) { char b[32]; std::snprintf(b, sizeof b, "%llx", static_cast<unsigned long long>(v)); return std::string(b); };
    long masks = 0, answered = 0, refusedHigh = 0;
    auto one = [&](uint64_t nz, int size) {
        const uint64_t m = PbMask(size);
        nz &= m;
        CircleRange q;
        const bool ok = q.setNZMask(nz, size);
        ++masks;
        const int lowest = nz == 0 ? -1 : __builtin_ctzll(nz);
        if (lowest > 30 && ok) { fail("a step that does not fit", "mask " + hex(nz) + " at " + std::to_string(size) + " bytes -> " + FwText(q)); return; }
        if (!ok) { if (lowest > 30) ++refusedHigh; return; }
        ++answered;
        std::string why;
        if (!FwWellFormed(q, size, why)) { fail("malformed", "mask " + hex(nz) + " at " + std::to_string(size) + " bytes -> " + FwText(q) + ": " + why); return; }
        std::vector<uint64_t> vs = { 0, nz, nz & (0 - nz) };
        if (nz) vs.push_back(static_cast<uint64_t>(1) << (63 - __builtin_clzll(nz)));
        for (int k = 0; k < 24; ++k) vs.push_back(next() & nz);
        if (size <= 2)
            for (uint64_t s = nz;; s = (s - 1) & nz) { vs.push_back(s); if (s == 0 || vs.size() > 4200) break; }
        for (uint64_t v : vs)
            if (!q.contains(v)) { fail("a value under the mask is not in the range", "mask " + hex(nz) + " at " + std::to_string(size) + " bytes -> " + FwText(q) + " misses " + hex(v)); return; }
    };
    for (uint64_t nz = 0; nz <= 0xffff; ++nz) one(nz, 2);          // every mask of one and two bytes
    for (uint64_t nz = 0; nz <= 0xff; ++nz) one(nz, 1);
    for (int size : { 4, 8 })
    {
        const int bits = 8 * size;
        for (int lo = 0; lo < bits; ++lo)                           // a run of bits from lo to hi, and the run to the top
            for (int hi = lo; hi < bits; ++hi)
            {
                const uint64_t run = (hi == 63 ? ~static_cast<uint64_t>(0) : ((static_cast<uint64_t>(1) << (hi + 1)) - 1)) & ~((static_cast<uint64_t>(1) << lo) - 1);
                one(run, size);
            }
        for (int k = 0; k < 4000; ++k)                              // a random mask, with its low bits cleared
        {
            const uint64_t r = next() & (next() | next());
            one(r << (next() % bits), size);
        }
    }
    one(0x80000000u, 4);
    one(static_cast<uint64_t>(1) << 63, 8);
    one(static_cast<uint64_t>(1) << 40, 8);
    one(static_cast<uint64_t>(0xffffffffull) << 32, 8);
    CircleRange q;
    CHECK(!q.setNZMask(0x80000000u, 4));
    CHECK(!q.setNZMask(static_cast<uint64_t>(1) << 63, 8));
    CHECK(q.setNZMask(0x40000000u, 4));       // the largest step that fits is kept as it was
    CHECK(q.getStep() == 0x40000000 && q.contains(0) && q.contains(0x40000000u));
    std::printf("setNZMask round 12: %ld masks, %ld answered, %ld high lowest bits refused, %.0f ms\n", masks, answered, refusedHigh, MsSince(t0));
    CHECK(refusedHigh > 100);
}

// A2-75: CircleRange::widen. A range moved to a containing landmark must hold the old range, in the old step and phase. Upstream's left bound was
// the landmark's as it was (another phase for a stride above the landmark's), its right bound rounded down when the landmark's remainder was above the
// phase (the last element left out), and a full landmark gave the right bound 0, which cut a wrapped range to its first lap.
void TestWidenRound12()
{
    using namespace ghidra;
    const auto t0 = std::chrono::steady_clock::now();
    uint64_t state = 0x5E7A2C75;
    auto next = [&state]() {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    };
    int shown = 0;
    auto fail = [&shown](const std::string& what, const std::string& detail) {
        CHECK(false);
        if (shown++ < 14) std::printf("widen round 12: %s: %s\n", what.c_str(), detail.c_str());
    };
    // the ledger's two
    {
        CircleRange a(0x34, 0x44, 1, 16), b(0xfa, 0xf9, 1, 1);
        a.widen(b, false);
        CHECK(a.contains(0x34) && a.contains(0x44 - 16) && a.getMin() % 16 == a.getEnd() % 16);
        CircleRange c(0x0e, 0x6e, 1, 8), d(0, 0, 1, 2);
        c.widen(d, false);
        CHECK(c.contains(0xe) && c.contains(0x66) && c.getStep() == 8);
        CircleRange e(0x20, 0x10, 1, 1), f(0, 0, 1, 1);     // a wrapped range to a full landmark, right bound unstable: the whole circle, not [0x20,0)
        e.widen(f, true);
        CHECK(e.contains(0x20) && e.contains(0x0f) && e.contains(0x00) && e.contains(0xff));
    }
    // every one-byte landmark of a sample, with sub-ranges of it (the elements of the landmark taken as the bounds), both bounds unstable in turn
    std::vector<FwRange> lands;
    for (int st : { 1, 2, 4, 8 })
        for (uint64_t l = 0; l < 256; ++l)
            for (uint64_t r = 0; r < 256; ++r)
            {
                if (l % st != r % st) continue;
                if (l == r && l >= static_cast<uint64_t>(st)) continue;
                if (next() % (st == 1 ? 3 : 1) != 0 && st != 8) continue;       // a third of the step 1 landmarks, all of the others
                lands.push_back({ l, r, st });
            }
    long pairs = 0, widened = 0, refusedByContains = 0;
    for (const FwRange& lm : lands)
    {
        const CircleRange B(lm.l, lm.r, 1, lm.step);
        for (int k = 0; k < 14; ++k)
        {
            // the old range: a sub-range of the landmark with the same or a bigger step, drawn from the landmark's elements
            const uint64_t n = FwCount(lm, 255);
            if (n == 0) continue;
            const int mult = 1 << (next() % 3);
            const int st = lm.step * mult;
            if (st > 32) continue;
            const uint64_t i = next() % n, len = 1 + next() % n;
            FwRange a;
            a.step = st;
            const uint64_t first = FwElem(lm, 255, i);
            a.l = first;
            a.r = (first + len * static_cast<uint64_t>(lm.step) * mult) & 255;
            if (a.r % st != a.l % st) a.r = a.l;
            if (a.l == a.r) a.l = a.r = a.l % st;
            const CircleRange A(a.l, a.r, 1, a.step);
            if (!B.contains(A)) { ++refusedByContains; continue; }
            for (int leftStable = 0; leftStable < 2; ++leftStable)
            {
                CircleRange q = A;
                q.widen(B, leftStable != 0);
                ++pairs;
                const std::string both = FwText(a) + " in " + FwText(lm) + (leftStable ? " (left stable)" : " (right stable)");
                std::string why;
                if (!FwWellFormed(q, 1, why)) { fail("malformed", both + " -> " + FwText(q) + ": " + why); continue; }
                if (q.getStep() != a.step) { fail("the step changed", both + " -> " + FwText(q)); continue; }
                bool holds = true;
                unsigned at = 0;
                for (unsigned v = 0; v <= 255 && holds; ++v)
                    if (FwMember(v, a, 255) && !q.contains(v)) { holds = false; at = v; }
                if (!holds) { fail("the old range is not held", both + " -> " + FwText(q) + " misses " + std::to_string(at)); continue; }
                // inside a landmark that is not everything the widened range stays inside it (the fallback to all of the phase is for a full landmark
                // only, and a bound rounded the wrong way would hide behind it)
                if (!FwFull(lm))
                {
                    unsigned out = 256;
                    for (unsigned v = 0; v <= 255 && out == 256; ++v)
                        if (q.contains(v) && !FwMember(v, lm, 255)) out = v;
                    if (out != 256) { fail("the widened range leaves the landmark", both + " -> " + FwText(q) + " holds " + std::to_string(out)); continue; }
                }
                // the bound that is stable stays; the other is the landmark's or the range is everything of its phase
                if (leftStable ? q.getMin() != A.getMin() : q.getEnd() != A.getEnd())
                    if (!(q.getMin() == q.getEnd())) fail("the stable bound moved", both + " -> " + FwText(q));
                ++widened;
            }
        }
    }
    std::printf("widen round 12: %ld widenings of %ld sub-ranges checked (%ld pairs the containment test refused), %.0f ms\n", widened, pairs / 2, refusedByContains, MsSince(t0));
    CHECK(widened > 10000);
}

// ---- Rules checked by value on a Funcdata (A2-76, A2-77) --------------------------------------------------------------------------------------
// A rule is applied to a small p-code graph built in a Funcdata and the graph is evaluated before and after for a set of inputs: the rule must leave
// the value of the graph as it was, whatever it rewrites (or not rewrite). The evaluator is the independent definition of each op; it covers only the ops
// the graphs use.
typedef std::vector<std::pair<const ghidra::Varnode*, uint64_t>> RcEnv;

uint64_t RcEval(const ghidra::Varnode* vn, const RcEnv& env)
{
    using namespace ghidra;
    const int size = vn->getSize();
    const uint64_t m = PbMask(size);
    if (vn->isConstant()) return vn->getOffset() & m;
    for (const auto& e : env)
        if (e.first == vn) return e.second & m;
    const PcodeOp* d = vn->getDef();
    if (d == nullptr) { CHECK(false); return 0; }
    const int n = d->numInput();
    const uint64_t a = n > 0 ? RcEval(d->getIn(0), env) : 0, b = n > 1 ? RcEval(d->getIn(1), env) : 0;
    const int sa = n > 0 ? d->getIn(0)->getSize() : 0, sb = n > 1 ? d->getIn(1)->getSize() : 0;
    switch (d->code())
    {
    case CPUI_COPY: return a;
    case CPUI_INT_ADD: return (a + b) & m;
    case CPUI_INT_AND: return a & b;
    case CPUI_INT_OR: return a | b;
    case CPUI_INT_XOR: return a ^ b;
    case CPUI_INT_SUB: return (a - b) & m;
    case CPUI_INT_NEGATE: return ~a & m;
    case CPUI_INT_2COMP: return (0 - a) & m;
    case CPUI_INT_MULT: return (a * b) & m;
    case CPUI_INT_ZEXT: return a;
    case CPUI_INT_SEXT: return static_cast<uint64_t>(PbSigned(a, sa)) & m;
    case CPUI_SUBPIECE: return b >= 8 ? 0 : (a >> (8 * b)) & m;
    case CPUI_PIECE: return ((a << (8 * sb)) | b) & m;
    case CPUI_INT_LEFT: return b >= static_cast<uint64_t>(8 * size) ? 0 : (a << b) & m;
    case CPUI_INT_RIGHT: return b >= static_cast<uint64_t>(8 * sa) ? 0 : a >> b;
    case CPUI_INT_SRIGHT: return static_cast<uint64_t>(PbSigned(a, sa) >> (b >= static_cast<uint64_t>(8 * sa) ? 8 * sa - 1 : b)) & m;
    case CPUI_INT_SDIV: return PbSigned(b, sb) == 0 ? 0 : static_cast<uint64_t>(PbSigned(a, sa) / PbSigned(b, sb)) & m;
    case CPUI_INT_SCARRY:
    {
        const int64_t s = PbSigned(a, sa) + PbSigned(b, sa), lim = static_cast<int64_t>(1) << (8 * sa - 1);   // sizes up to 4
        return (s < -lim || s >= lim) ? 1 : 0;
    }
    case CPUI_INT_LESS: return a < b;
    case CPUI_INT_LESSEQUAL: return a <= b;
    case CPUI_INT_SLESS: return PbSigned(a, sa) < PbSigned(b, sb);
    case CPUI_INT_SLESSEQUAL: return PbSigned(a, sa) <= PbSigned(b, sb);
    case CPUI_INT_EQUAL: return a == b;
    case CPUI_INT_NOTEQUAL: return a != b;
    default: break;
    }
    CHECK(false);
    return 0;
}

// The Funcdata the rules are applied in (no actions run; the graphs are never put in a block).
struct RcFuncdata
{
    RcFuncdata() : image(new RegionLoadImage(kImageBase, Fixture1())), arch(image, &errs)
    {
        ghidra::DocumentStorage store;
        arch.init(store);
        entry = ghidra::Address(arch.getDefaultCodeSpace(), kCaller);
        std::string name;
        arch.nameFunction(entry, name);
        fd = arch.symboltab->getGlobalScope()->addFunction(entry, name)->getFunction();
    }
    ghidra::Varnode* copyOf(int size, uint64_t v)
    {
        ghidra::PcodeOp* op = fd->newOp(1, entry);
        fd->opSetOpcode(op, ghidra::CPUI_COPY);
        ghidra::Varnode* out = fd->newUniqueOut(size, op);
        fd->opSetInput(op, fd->newConstant(size, v), 0);
        return out;
    }
    ghidra::PcodeOp* op1(ghidra::OpCode opc, int outSize, ghidra::Varnode* a)
    {
        ghidra::PcodeOp* op = fd->newOp(1, entry);
        fd->opSetOpcode(op, opc);
        fd->newUniqueOut(outSize, op);
        fd->opSetInput(op, a, 0);
        return op;
    }
    ghidra::PcodeOp* op2(ghidra::OpCode opc, int outSize, ghidra::Varnode* a, ghidra::Varnode* b)
    {
        ghidra::PcodeOp* op = fd->newOp(2, entry);
        fd->opSetOpcode(op, opc);
        fd->newUniqueOut(outSize, op);
        fd->opSetInput(op, a, 0);
        fd->opSetInput(op, b, 1);
        return op;
    }
    ghidra::Varnode* k(int size, uint64_t v) { return fd->newConstant(size, v); }

    RegionLoadImage* image;
    std::ostringstream errs;
    TestArchitecture arch;
    ghidra::Address entry;
    ghidra::Funcdata* fd = nullptr;
};

// The values of a graph's result for every environment, kept to compare after the rule.
std::vector<uint64_t> RcValues(const ghidra::Varnode* result, const std::vector<RcEnv>& envs)
{
    std::vector<uint64_t> out;
    for (const RcEnv& e : envs) out.push_back(RcEval(result, e));
    return out;
}

// The input values of a size that matter: the edges of its sign and width, small ones, and neighbours of the given constant.
std::vector<uint64_t> RcInputs(int size, uint64_t around, uint64_t seed, int randoms)
{
    const uint64_t m = PbMask(size);
    std::vector<uint64_t> v;
    if (size == 1) { for (uint64_t x = 0; x <= 255; ++x) v.push_back(x); return v; }
    for (uint64_t t : { static_cast<uint64_t>(0), m >> 1, (m >> 1) + 1, m, 0 - around, around })
        for (int d = -3; d <= 3; ++d) v.push_back((t + d) & m);
    uint64_t state = seed;
    for (int k = 0; k < randoms; ++k)
    {
        uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        v.push_back((z ^ (z >> 31)) & m);
    }
    return v;
}

// A2-76: RuleScarry. `scarry(V,c) != (V + c s< 0)` is `V s< -c` and its three companions for a constant c, and none of the four is right for the
// minimum (-c is c): upstream refused it only for the operand it had swapped itself. Every constant at one byte, edges at two and four bytes,
// each of the eight forms (the constant first or second in the scarry, the sum written c + V or V + c, equal or not equal, the zero on either
// side of the less-than), the value of the compare before and after the rule for every V (one byte) or for edge values and a sample.
void TestScarryMinimumRound12()
{
    using namespace ghidra;
    RcFuncdata t;
    long graphs = 0, applied = 0, values = 0;
    int shown = 0;
    for (int size : { 1, 2, 4 })
    {
        const uint64_t m = PbMask(size), minimum = (m >> 1) + 1;
        std::vector<uint64_t> consts;
        if (size == 1) for (uint64_t c = 0; c <= 255; ++c) consts.push_back(c);
        else consts = { 0, 1, 2, 5, m >> 1, minimum, minimum + 1, minimum - 2, m, m - 1, m - 4, 0x1234 & m, 0x5a5a5a5a & m };
        for (uint64_t c : consts)
            for (int form = 0; form < 16; ++form)
            {
                const bool constFirst = form & 1, sumConstFirst = form & 2, equal = form & 4, zeroFirst = form & 8;
                Varnode* a = t.copyOf(size, 0x1234 & m);
                Varnode* s = constFirst ? t.op2(CPUI_INT_SCARRY, 1, t.k(size, c), a)->getOut() : t.op2(CPUI_INT_SCARRY, 1, a, t.k(size, c))->getOut();
                Varnode* sum = sumConstFirst ? t.op2(CPUI_INT_ADD, size, t.k(size, c), a)->getOut() : t.op2(CPUI_INT_ADD, size, a, t.k(size, c))->getOut();
                Varnode* less = zeroFirst ? t.op2(CPUI_INT_SLESS, 1, t.k(size, 0), sum)->getOut() : t.op2(CPUI_INT_SLESS, 1, sum, t.k(size, 0))->getOut();
                PcodeOp* cmp = t.op2(equal ? CPUI_INT_EQUAL : CPUI_INT_NOTEQUAL, 1, s, less);
                std::vector<RcEnv> envs;
                for (uint64_t v : RcInputs(size, c, 77 + c, 60)) envs.push_back({ { a, v } });
                const std::vector<uint64_t> before = RcValues(cmp->getOut(), envs);
                RuleScarry rule("scratch");
                const int rc = rule.applyOp(s->getDef(), *t.fd);
                const std::vector<uint64_t> after = RcValues(cmp->getOut(), envs);
                ++graphs;
                if (rc != 0) ++applied;
                for (size_t i = 0; i < envs.size(); ++i)
                {
                    ++values;
                    if (before[i] != after[i])
                    {
                        CHECK(false);
                        if (shown++ < 10)
                            std::printf("RuleScarry: %d-byte constant %llx (form %d): the compare is %llu for V = %llx before the rule and %llu after (rule returned %d)\n", size, static_cast<unsigned long long>(c), form,
                                        static_cast<unsigned long long>(before[i]), static_cast<unsigned long long>(envs[i][0].second), static_cast<unsigned long long>(after[i]), rc);
                        break;
                    }
                }
                if (c == minimum) CHECK(rc == 0);          // the minimum is refused, whichever operand it was
                if (c != minimum && c != 0 && (form & 1) == 0 && (form & 2) == 0) CHECK(rc == 1);   // the plain forms are rewritten
            }
    }
    std::printf("RuleScarry by value: %ld graphs (%ld rewritten), %ld values compared\n", graphs, applied, values);
    CHECK(applied > 300);
}

// A2-77: the three rules whose mutants no family killed, by value on a Funcdata. RuleConcatShift `concat(V,W) >> 8k` for the exact cancellation
// (`zext(V)` for the unsigned shift, `sext(V)` for the signed one: upstream's mutant took the other sign); RuleSignDiv2 `(V + (V s>> 31) * -1) s>> 1`
// is `V s/ 2` and no other shift amount is (`s>> 2` of the same sum is not `V s/ 4`); RuleSignForm `sub(sext(V),c)` is `V s>> 31` for c at least the
// size of V (c one short of it takes the top byte of V as well).
void TestRulesByValueRound12()
{
    using namespace ghidra;
    RcFuncdata t;
    long graphs = 0, values = 0;
    int shown = 0;
    auto compare = [&](const char* rule, const std::string& what, const ghidra::Varnode* result, const std::vector<RcEnv>& envs, const std::vector<uint64_t>& before) {
        const std::vector<uint64_t> after = RcValues(result, envs);
        ++graphs;
        for (size_t i = 0; i < envs.size(); ++i)
        {
            ++values;
            if (before[i] != after[i])
            {
                CHECK(false);
                if (shown++ < 12) std::printf("%s: %s: %llx before the rule and %llx after for the input %llx\n", rule, what.c_str(), static_cast<unsigned long long>(before[i]), static_cast<unsigned long long>(after[i]), static_cast<unsigned long long>(envs[i][0].second));
                return;
            }
        }
    };
    // RuleConcatShift: concat(hi,lo) >> 8*size(lo), unsigned and signed, and one bit short of it (not a cancellation)
    for (int hs : { 1, 2, 4 })
        for (int ls : { 1, 2, 4 })
            for (OpCode shiftOpc : { CPUI_INT_RIGHT, CPUI_INT_SRIGHT })
                for (int delta : { 0, -1 })
                {
                    Varnode* hi = t.copyOf(hs, 0);
                    Varnode* lo = t.copyOf(ls, 0);
                    PcodeOp* piece = t.op2(CPUI_PIECE, hs + ls, hi, lo);
                    PcodeOp* sh = t.op2(shiftOpc, hs + ls, piece->getOut(), t.k(4, 8 * ls + delta));
                    std::vector<RcEnv> envs;
                    for (uint64_t h : RcInputs(hs, 0, 5, 12))
                        for (uint64_t l : { static_cast<uint64_t>(0), static_cast<uint64_t>(1), PbMask(ls), PbMask(ls) >> 1, (PbMask(ls) >> 1) + 1 })
                            envs.push_back({ { hi, h }, { lo, l } });
                    const std::vector<uint64_t> before = RcValues(sh->getOut(), envs);
                    RuleConcatShift rule("scratch");
                    const int rc = rule.applyOp(sh, *t.fd);
                    CHECK(rc == (delta == 0 ? 1 : 0));
                    compare("RuleConcatShift", std::to_string(hs) + "+" + std::to_string(ls) + " bytes, " + (shiftOpc == CPUI_INT_RIGHT ? "unsigned" : "signed") + " shift by " + std::to_string(8 * ls + delta), sh->getOut(), envs, before);
                }
    // RuleSignDiv2: (V + (V s>> 31) * -1) s>> k, the sum written either way round
    for (int size : { 1, 2, 4 })
        for (int k = 1; k <= 5; ++k)
            for (int swap = 0; swap < 2; ++swap)
            {
                Varnode* a = t.copyOf(size, 0);
                PcodeOp* sign = t.op2(CPUI_INT_SRIGHT, size, a, t.k(4, 8 * size - 1));
                PcodeOp* neg = t.op2(CPUI_INT_MULT, size, sign->getOut(), t.k(size, PbMask(size)));
                PcodeOp* add = swap ? t.op2(CPUI_INT_ADD, size, neg->getOut(), a) : t.op2(CPUI_INT_ADD, size, a, neg->getOut());
                PcodeOp* sh = t.op2(CPUI_INT_SRIGHT, size, add->getOut(), t.k(4, k));
                std::vector<RcEnv> envs;
                for (uint64_t v : RcInputs(size, 1, 9, 60)) envs.push_back({ { a, v } });
                const std::vector<uint64_t> before = RcValues(sh->getOut(), envs);
                RuleSignDiv2 rule("scratch");
                const int rc = rule.applyOp(sh, *t.fd);
                CHECK(rc == (k == 1 ? 1 : 0));
                compare("RuleSignDiv2", std::to_string(size) + "-byte value, shift by " + std::to_string(k), sh->getOut(), envs, before);
            }
    // RuleSignForm: sub(sext(V), c), V of 1, 2 or 4 bytes extended to twice its size, c from 0 to the size of V
    for (int size : { 1, 2, 4 })
        for (int c = 0; c <= size; ++c)
        {
            Varnode* a = t.copyOf(size, 0);
            PcodeOp* ext = t.op1(CPUI_INT_SEXT, 2 * size, a);
            PcodeOp* sub = t.op2(CPUI_SUBPIECE, size, ext->getOut(), t.k(4, c));
            std::vector<RcEnv> envs;
            for (uint64_t v : RcInputs(size, 1, 11, 60)) envs.push_back({ { a, v } });
            const std::vector<uint64_t> before = RcValues(sub->getOut(), envs);
            RuleSignForm rule("scratch");
            const int rc = rule.applyOp(sub, *t.fd);
            CHECK(rc == (c >= size ? 1 : 0));
            compare("RuleSignForm", std::to_string(size) + "-byte value, SUBPIECE at " + std::to_string(c), sub->getOut(), envs, before);
        }
    // RulePiece2Sext: concat(V s>> 31, V) is sext(V) and only for the same V (the mutant took the sign of another value); a shift of one short of the
    // sign is no sign
    for (int size : { 1, 2, 4 })
        for (int variant = 0; variant < 3; ++variant)     // 0: the same value, 1: another value, 2: the shift one short
        {
            Varnode* a = t.copyOf(size, 0);
            Varnode* b = t.copyOf(size, 0);
            PcodeOp* sign = t.op2(CPUI_INT_SRIGHT, size, variant == 1 ? b : a, t.k(4, 8 * size - 1 - (variant == 2 ? 1 : 0)));
            PcodeOp* piece = t.op2(CPUI_PIECE, 2 * size, sign->getOut(), a);
            std::vector<RcEnv> envs;
            for (uint64_t v : RcInputs(size, 1, 13, 20))
                for (uint64_t w : { static_cast<uint64_t>(0), PbMask(size), (PbMask(size) >> 1) + 1, static_cast<uint64_t>(3) })
                    envs.push_back({ { a, v }, { b, w } });
            const std::vector<uint64_t> before = RcValues(piece->getOut(), envs);
            RulePiece2Sext rule("scratch");
            const int rc = rule.applyOp(piece, *t.fd);
            CHECK(rc == (variant == 0 ? 1 : 0));
            compare("RulePiece2Sext", std::to_string(size) + "-byte value, variant " + std::to_string(variant), piece->getOut(), envs, before);
        }
    // RuleZextSless (shadowed in the pool by RuleSlessToLess, which sees the clear sign bits first: applied directly): zext(V) s< c is V < c when c
    // has its sign bit clear at the size of V and is left alone otherwise
    for (int size : { 1, 2 })
        for (uint64_t c : { static_cast<uint64_t>(0), static_cast<uint64_t>(1), PbMask(size) >> 1, (PbMask(size) >> 1) + 1, PbMask(size), PbMask(size) + 1, static_cast<uint64_t>(0x7fffffff) })
            for (int slot = 0; slot < 2; ++slot)
                for (OpCode opc : { CPUI_INT_SLESS, CPUI_INT_SLESSEQUAL })
                {
                    Varnode* a = t.copyOf(size, 0);
                    PcodeOp* ext = t.op1(CPUI_INT_ZEXT, 4, a);
                    PcodeOp* cmp = slot == 0 ? t.op2(opc, 1, ext->getOut(), t.k(4, c)) : t.op2(opc, 1, t.k(4, c), ext->getOut());
                    std::vector<RcEnv> envs;
                    for (uint64_t v : RcInputs(size, c, 17, 20)) envs.push_back({ { a, v } });
                    const std::vector<uint64_t> before = RcValues(cmp->getOut(), envs);
                    RuleZextSless rule("scratch");
                    const int rc = rule.applyOp(cmp, *t.fd);
                    CHECK(rc == ((c >> (8 * size - 1)) == 0 ? 1 : 0));
                    compare("RuleZextSless", std::to_string(size) + "-byte value, constant " + std::to_string(c), cmp->getOut(), envs, before);
                }
    // RuleSignForm2 (it changes the input and returns 0, so the rule counter never saw it): sub(sext(V) * c, 4) s>> 31 reads V s>> 31 for a constant c of
    // at most the size of V, and is left alone for a larger one
    for (uint64_t c : { static_cast<uint64_t>(1), static_cast<uint64_t>(5), static_cast<uint64_t>(0x10001), static_cast<uint64_t>(0x7fffffff), static_cast<uint64_t>(0xffffffffu), static_cast<uint64_t>(0x100000000ull), static_cast<uint64_t>(0x8000000000000000ull) })
    {
        Varnode* a = t.copyOf(4, 0);
        PcodeOp* ext = t.op1(CPUI_INT_SEXT, 8, a);
        PcodeOp* mult = t.op2(CPUI_INT_MULT, 8, ext->getOut(), t.k(8, c));
        PcodeOp* high = t.op2(CPUI_SUBPIECE, 4, mult->getOut(), t.k(4, 4));
        PcodeOp* sh = t.op2(CPUI_INT_SRIGHT, 4, high->getOut(), t.k(4, 31));
        std::vector<RcEnv> envs;
        for (uint64_t v : RcInputs(4, 1, 19, 40)) envs.push_back({ { a, v } });
        const std::vector<uint64_t> before = RcValues(sh->getOut(), envs);
        RuleSignForm2 rule("scratch");
        CHECK(rule.applyOp(sh, *t.fd) == 0);                     // it never reports a change
        CHECK((sh->getIn(0) == a) == (c <= 0xffffffffull));      // but it makes one
        compare("RuleSignForm2", "constant " + std::to_string(c), sh->getOut(), envs, before);
    }
    // RuleRightShiftAnd (A2-80): `(V & mask) >> sa` and `s>> sa` is `V >> sa` when the mask keeps every bit that survives the shift; for a shift by the
    // width of V or more nothing survives, both sides of the rule's test were 0, and the mask was dropped, which is wrong for the arithmetic shift (its
    // result is the sign bit, and the mask can clear it): `(b & 0x20) s>> 8` of a byte is 0, not the sign of b
    for (int size : { 1, 2, 4 })
        for (uint64_t sa : { 0, 1, 4, 7, 8, 9, 15, 16, 17, 31, 32, 33, 40, 63, 64 })
            for (OpCode shiftOpc : { CPUI_INT_RIGHT, CPUI_INT_SRIGHT })
                for (uint64_t mk : { PbMask(size), static_cast<uint64_t>(0x20), static_cast<uint64_t>(0xf0), PbMask(size) & ~static_cast<uint64_t>(0xff), (PbMask(size) >> 1) + 1, PbMask(size) >> 1, static_cast<uint64_t>(0xff00), static_cast<uint64_t>(0x8000) })
                {
                    const uint64_t m = PbMask(size);
                    Varnode* a = t.copyOf(size, 0);
                    PcodeOp* andOp = t.op2(CPUI_INT_AND, size, a, t.k(size, mk & m));
                    PcodeOp* sh = t.op2(shiftOpc, size, andOp->getOut(), t.k(4, sa));
                    std::vector<RcEnv> envs;
                    for (uint64_t v : RcInputs(size, 1, 23, 30)) envs.push_back({ { a, v } });
                    const std::vector<uint64_t> before = RcValues(sh->getOut(), envs);
                    RuleRightShiftAnd rule("scratch");
                    const int rc = rule.applyOp(sh, *t.fd);
                    const bool applies = sa < static_cast<uint64_t>(8 * size) && ((mk & m) >> sa) == (m >> sa);
                    CHECK(rc == (applies ? 1 : 0));
                    compare("RuleRightShiftAnd", std::to_string(size) + "-byte value, mask " + std::to_string(mk & m) + ", " + (shiftOpc == CPUI_INT_RIGHT ? "unsigned" : "signed") + " shift by " + std::to_string(sa), sh->getOut(), envs, before);
                }
    std::printf("rules by value: %ld graphs, %ld values compared\n", graphs, values);
}


// ---- Round 13: patch 0062 at every size ------------------------------------------------------------------------------------------------------
// The set operations of 0062 (the intersect of ranges of two masks, setNZMask, widen) at every size from 1 to 8 bytes, by membership: the sets are written
// out from the definition (left, right, step, mask) here and a result is judged on a sample taken where an arithmetic error shows (the bounds and their
// neighbours, the edge of the narrower domain, the ends and the sign boundary of each domain, elements of both ranges). rangeset13.cpp (the round 13 tool)
// is the wider run; this is the part that is cheap enough for the suite, and it fails against the pre-0062 rangeutil.cc at every one of the three.
struct R13Rg { uint64_t l, r; int st, sz; };

bool R13Member(uint64_t v, uint64_t l, uint64_t r, int st, uint64_t m)
{
    v &= m;
    if (v % static_cast<uint64_t>(st) != l % static_cast<uint64_t>(st)) return false;
    if (l == r) return true;
    return l < r ? (v >= l && v < r) : (v < r || v >= l);
}
bool R13Member(uint64_t v, const R13Rg& g) { return R13Member(v, g.l, g.r, g.st, PbMask(g.sz)); }
bool R13QMember(const ghidra::CircleRange& q, uint64_t v) { return !q.isEmpty() && R13Member(v, q.getMin(), q.getEnd(), q.getStep(), q.getMask()); }

struct R13Rng
{
    uint64_t s;
    explicit R13Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 99) {}
    uint64_t next()
    {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
};

uint64_t R13Pick(R13Rng& g, int sz)
{
    const uint64_t m = PbMask(sz);
    switch (g.next() % 9)
    {
    case 0: return g.next() & m;
    case 1: return (g.next() % 17) & m;
    case 2: return (m - g.next() % 17) & m;
    case 3: return ((m >> 1) + (g.next() % 17) - 8) & m;
    case 4: { const int k = static_cast<int>(g.next() % (8 * sz)); return ((1ull << k) + (g.next() % 9) - 4) & m; }
    case 5: { const int k = static_cast<int>(g.next() % (8 * sz)); return ((0 - (1ull << k)) + (g.next() % 9) - 4) & m; }
    case 6: return (g.next() & g.next() & g.next()) & m;
    case 7:
    {   // next to the edge of a narrower domain, where the intersect cuts a wider range
        const int k = 8 * static_cast<int>(1 + g.next() % static_cast<uint64_t>(sz));
        return ((k >= 64 ? ~0ull : ((1ull << k) - 1)) + (g.next() % 9) - 4) & m;
    }
    default: return (g.next() | g.next() | g.next()) & m;
    }
}

R13Rg R13Gen(R13Rng& g, int sz)
{
    const uint64_t m = PbMask(sz);
    static const int steps[] = { 1, 1, 1, 1, 2, 2, 4, 8, 16, 32 };
    int st = steps[g.next() % 10];
    while (static_cast<uint64_t>(st) > m) st >>= 1;
    R13Rg r; r.st = st; r.sz = sz; r.l = R13Pick(g, sz);
    switch (g.next() % 12)
    {
    case 0: r.r = r.l; break;
    case 1: r.r = (r.l + st) & m; break;
    case 2: r.r = (r.l + 2 * st) & m; break;
    case 3: r.r = (r.l - st) & m; break;
    case 4: r.r = 0; break;                                  // ends at the top of the domain
    default: r.r = R13Pick(g, sz);
    }
    const uint64_t rem = r.l % st;
    r.r = (r.r - r.r % st + rem) & m;
    if (r.r % st != rem) r.r = r.l;
    if (r.l == r.r) r.l = r.r = rem;
    return r;
}

void R13Points(const R13Rg& a, R13Rng& g, std::vector<uint64_t>& out)
{
    const uint64_t m = PbMask(a.sz);
    const uint64_t n = a.l == a.r ? m / a.st + 1 : ((a.r - a.l) & m) / a.st;
    auto elem = [&](uint64_t j) { return (a.l + j * static_cast<uint64_t>(a.st)) & m; };
    for (uint64_t j = 0; j < std::min<uint64_t>(n, 5); ++j) { out.push_back(elem(j)); out.push_back(elem(n - 1 - j)); }
    for (int k = 0; k < 10 && n > 0; ++k) out.push_back(elem(g.next() % n));
    for (uint64_t b : { a.l, a.r })
        for (int d = -3; d <= 3; ++d) out.push_back((b + static_cast<uint64_t>(static_cast<int64_t>(d) * a.st)) & m);
}

void R13Edges(int sz, std::vector<uint64_t>& out)
{
    const uint64_t m = PbMask(sz);
    for (uint64_t t : { static_cast<uint64_t>(0), m, m >> 1, (m >> 1) + 1 })
        for (int d = -3; d <= 3; ++d) out.push_back((t + static_cast<uint64_t>(static_cast<int64_t>(d))) & m);
}

bool R13WellFormed(const ghidra::CircleRange& q, uint64_t wantMask)
{
    if (q.isEmpty()) return true;
    const int st = q.getStep();
    return q.getMask() == wantMask && st >= 1 && (st & (st - 1)) == 0 && !((q.getMin() | q.getEnd()) & ~q.getMask()) && q.getMin() % static_cast<uint64_t>(st) == q.getEnd() % static_cast<uint64_t>(st);
}

void TestRangeSetsRound13()
{
    using namespace ghidra;
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    long intersects = 0, answered = 0, nzs = 0, widens = 0;
    R13Rng g(13);
    // the intersect of ranges of every pair of sizes (and of one size): an answered 0 is exactly the elements both ranges have in the narrower domain
    for (int sa = 1; sa <= 8; ++sa)
        for (int sb = 1; sb <= 8; ++sb)
            for (int it = 0; it < 2500; ++it)
            {
                R13Rg a = R13Gen(g, sa), b = R13Gen(g, sb);
                CircleRange A(a.l, a.r, sa, a.st), B(b.l, b.r, sb, b.st);
                const uint64_t mn = PbMask(std::min(sa, sb));
                CircleRange q = A;
                const int rc = q.intersect(B);
                ++intersects;
                if (rc != 0) { CHECK(q == A); continue; }
                ++answered;
                CHECK(R13WellFormed(q, mn));
                std::vector<uint64_t> pts;
                R13Points(a, g, pts); R13Points(b, g, pts); R13Edges(std::min(sa, sb), pts); R13Edges(std::max(sa, sb), pts);
                for (int k = 0; k < 6; ++k) pts.push_back(g.next() & mn);
                for (uint64_t v : pts)
                {
                    if (v > mn) continue;
                    const bool want = R13Member(v, a) && R13Member(v, b);
                    if (want != R13QMember(q, v))
                    {
                        CHECK(false);
                        std::printf("  intersect %d/%d bytes: [%llx,%llx)/%d with [%llx,%llx)/%d gives [%llx,%llx)/%d, which %s %llx\n", sa, sb, (unsigned long long)a.l, (unsigned long long)a.r, a.st, (unsigned long long)b.l,
                                    (unsigned long long)b.r, b.st, (unsigned long long)q.getMin(), (unsigned long long)q.getEnd(), q.getStep(), want ? "misses" : "holds", (unsigned long long)v);
                        break;
                    }
                }
            }
    // setNZMask: every run of bits (and the run plus the low bit or the sign) at every size holds every value under the mask; a refusal is for a mask of
    // more than two transitions or a lowest set bit of 31 or more, nothing else
    for (int sz = 1; sz <= 8; ++sz)
    {
        const uint64_t m = PbMask(sz);
        for (int lo = 0; lo < 8 * sz; ++lo)
            for (int hi = lo; hi < 8 * sz; ++hi)
            {
                const uint64_t run = ((hi - lo + 1 >= 64 ? ~0ull : ((1ull << (hi - lo + 1)) - 1)) << lo) & m;
                for (uint64_t nz : { run, (run | 1) & m, (run | (1ull << (8 * sz - 1))) & m })
                {
                    CircleRange q;
                    ++nzs;
                    if (!q.setNZMask(nz, sz)) continue;
                    CHECK(R13WellFormed(q, m));
                    std::vector<uint64_t> vs = { 0, nz, nz & (0 - nz), nz >> 1 };
                    for (int k = 0; k < 8; ++k) vs.push_back(g.next() & nz);
                    for (uint64_t v : vs)
                        if (!R13QMember(q, v & nz) || !q.contains(v & nz)) { CHECK(false); std::printf("  setNZMask(%llx, %d) misses %llx\n", (unsigned long long)nz, sz, (unsigned long long)(v & nz)); break; }
                    if (q.getStep() > 1) CHECK((nz & static_cast<uint64_t>(q.getStep() - 1)) == 0);
                }
            }
        // the lowest set bit of 31: no range
        if (sz >= 4) { CircleRange q; CHECK(!q.setNZMask(0x80000000ull & m, sz)); }
    }
    // widen: the widened range holds the old one, in its step
    for (int sz = 1; sz <= 8; ++sz)
        for (int it = 0; it < 6000; ++it)
        {
            const uint64_t m = PbMask(sz);
            R13Rg b = R13Gen(g, sz), a = R13Gen(g, sz);
            if (g.next() % 2)
            {
                const uint64_t n = b.l == b.r ? m / b.st + 1 : ((b.r - b.l) & m) / b.st;
                if (n < 2) continue;
                const uint64_t i0 = g.next() % n, len = 1 + g.next() % (n - i0);
                a.st = b.st; a.sz = sz; a.l = (b.l + i0 * static_cast<uint64_t>(b.st)) & m; a.r = (b.l + (i0 + len) * static_cast<uint64_t>(b.st)) & m;
                if (i0 + len >= n && b.l != b.r) a.r = b.r;
                if (a.l == a.r) a.l = a.r = a.l % a.st;
            }
            CircleRange A(a.l, a.r, sz, a.st), B(b.l, b.r, sz, b.st);
            if (!B.contains(A)) continue;
            for (int stable = 0; stable < 2; ++stable)
            {
                CircleRange q = A;
                q.widen(B, stable != 0);
                ++widens;
                CHECK(R13WellFormed(q, m));
                CHECK(q.getStep() == a.st);
                std::vector<uint64_t> pts;
                R13Points(a, g, pts); R13Points(b, g, pts); R13Edges(sz, pts);
                for (uint64_t v : pts)
                    if (R13Member(v, a) && !R13QMember(q, v)) { CHECK(false); std::printf("  widen %d bytes: [%llx,%llx)/%d in [%llx,%llx)/%d (%s stable) misses %llx\n", sz, (unsigned long long)a.l, (unsigned long long)a.r, a.st, (unsigned long long)b.l, (unsigned long long)b.r, b.st, stable ? "left" : "right", (unsigned long long)v); break; }
            }
        }
    std::printf("range sets round 13: %ld intersects (%ld answered), %ld masks, %ld widenings, %.0f ms\n", intersects, answered, nzs, widens, MsSince(t0));
    CHECK(answered > 50000);
    CHECK(widens > 20000);
}

// RuleSLess2Zero and RuleRightShiftAnd on masked signs, with the non-zero masks the graph gives (A2-80, A2-81): `(a OP b) s< 0` and `-1 s< (a OP b)` for OP
// ADD, OR and XOR where each of a and b is a masked sign (`x & hibit`), the sign constant, a boolean moved up to the sign, zero, the bits under the sign or
// a whole value, at sizes 1, 2, 4 and 8, in both orders. The graph is put in a block so that Funcdata::calcNZMask gives each varnode the mask the engine's
// action gives it; the compare is evaluated before and after the rule for the edge values of both inputs. The rule must leave the value alone (the
// high bit of a sum is taken from an operand only when the other has none).
struct R13Graph
{
    R13Graph() : image(new RegionLoadImage(kImageBase, Fixture1())), arch(image, &errs)
    {
        ghidra::DocumentStorage store;
        arch.init(store);
        entry = ghidra::Address(arch.getDefaultCodeSpace(), kCaller);
        std::string name;
        arch.nameFunction(entry, name);
        fd = arch.symboltab->getGlobalScope()->addFunction(entry, name)->getFunction();
        reg = arch.getSpaceByName("register");
    }
    void reset()
    {
        fd->clear();
        ghidra::BlockGraph& g = const_cast<ghidra::BlockGraph&>(fd->getBasicBlocks());
        bb = g.newBlockBasic(fd);
        g.setStartBlock(bb);
        nextReg = 0x100;
    }
    ghidra::Varnode* input(int size)
    {
        ghidra::Varnode* v = fd->newVarnode(size, reg, nextReg);
        nextReg += 16;
        return fd->setInputVarnode(v);
    }
    ghidra::Varnode* op(ghidra::OpCode opc, int outSize, ghidra::Varnode* a, ghidra::Varnode* b = nullptr)
    {
        ghidra::PcodeOp* o = fd->newOp(b ? 2 : 1, entry);
        fd->opSetOpcode(o, opc);
        ghidra::Varnode* out = fd->newUniqueOut(outSize, o);
        fd->opSetInput(o, a, 0);
        if (b) fd->opSetInput(o, b, 1);
        fd->opInsertEnd(o, bb);
        return out;
    }
    ghidra::Varnode* k(int size, uint64_t v) { return fd->newConstant(size, v & PbMask(size)); }

    RegionLoadImage* image;
    std::ostringstream errs;
    TestArchitecture arch;
    ghidra::Address entry;
    ghidra::Funcdata* fd = nullptr;
    ghidra::AddrSpace* reg = nullptr;
    ghidra::BlockBasic* bb = nullptr;
    uint64_t nextReg = 0x100;
};

void TestSignHighBitRound13()
{
    using namespace ghidra;
    R13Graph t;
    long graphs = 0, rewritten = 0, values = 0;
    int shown = 0;
    for (int size : { 1, 2, 4, 8 })
    {
        const uint64_t m = PbMask(size), hib = (m >> 1) + 1;
        for (int ka = 0; ka < 6; ++ka)
            for (int kb = 0; kb < 6; ++kb)
                for (OpCode opc : { CPUI_INT_ADD, CPUI_INT_OR, CPUI_INT_XOR })
                    for (int form = 0; form < 4; ++form)         // s< 0 and -1 s<, the sum first or second in the ADD
                    {
                        t.reset();
                        Varnode* x = t.input(size);
                        Varnode* y = t.input(size);
                        auto operand = [&](int kind, Varnode* src) -> Varnode* {
                            switch (kind)
                            {
                            case 0: return t.op(CPUI_INT_AND, size, src, t.k(size, hib));                                              // x & hibit
                            case 1: return t.k(size, hib);                                                                              // the sign constant
                            case 2:
                            {   // a boolean moved up to the sign
                                Varnode* b = t.op(CPUI_INT_SLESS, 1, src, t.k(size, 0));
                                if (size == 1) return t.op(CPUI_INT_LEFT, 1, b, t.k(4, 7));
                                return t.op(CPUI_INT_LEFT, size, t.op(CPUI_INT_ZEXT, size, b), t.k(4, 8 * size - 1));
                            }
                            case 3: return t.k(size, 0);
                            case 4: return t.op(CPUI_INT_AND, size, src, t.k(size, m >> 1));                                            // the bits under the sign
                            default: return src;                                                                                        // a whole value
                            }
                        };
                        Varnode* a = operand(ka, x);
                        Varnode* b = operand(kb, y);
                        if (a->isConstant() && b->isConstant()) continue;
                        Varnode* sum = (form & 1) ? t.op(opc, size, b, a) : t.op(opc, size, a, b);
                        Varnode* cmp = (form & 2) ? t.op(CPUI_INT_SLESS, 1, t.k(size, m), sum) : t.op(CPUI_INT_SLESS, 1, sum, t.k(size, 0));
                        t.fd->calcNZMask();
                        PcodeOp* cmpOp = cmp->getDef();
                        std::vector<RcEnv> envs;
                        for (uint64_t xv : RcInputs(size, hib, 31, 10))
                            for (uint64_t yv : { static_cast<uint64_t>(0), static_cast<uint64_t>(1), m, m >> 1, hib, hib + 1, hib - 1 })
                                envs.push_back({ { x, xv }, { y, yv } });
                        const std::vector<uint64_t> before = RcValues(cmp, envs);
                        RuleSLess2Zero rule("scratch");
                        const int rc = rule.applyOp(cmpOp, *t.fd);
                        ++graphs;
                        if (rc != 0) ++rewritten;
                        const std::vector<uint64_t> after = RcValues(cmpOp->getOut(), envs);
                        for (size_t i = 0; i < envs.size(); ++i)
                        {
                            ++values;
                            if (before[i] != after[i])
                            {
                                CHECK(false);
                                if (shown++ < 8)
                                    std::printf("RuleSLess2Zero: %d bytes, operands %d and %d, op %d, form %d: %llu before the rule and %llu after for x=%llx y=%llx (rule returned %d)\n", size, ka, kb, static_cast<int>(opc), form,
                                                static_cast<unsigned long long>(before[i]), static_cast<unsigned long long>(after[i]), static_cast<unsigned long long>(envs[i][0].second), static_cast<unsigned long long>(envs[i][1].second), rc);
                                break;
                            }
                        }
                    }
    }
    std::printf("RuleSLess2Zero by value: %ld graphs (%ld rewritten), %ld values compared\n", graphs, rewritten, values);
    CHECK(rewritten > 100);
}

}  // namespace

// Each case announces itself first; with stdout unbuffered, the last name printed is where a
// test that never returns got stuck.
#define RUN(call) (std::printf("-- %s\n", #call), call)

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::printf("usage: %s <scratch dir>\n", argv[0]);
        return 2;
    }
    // Under ctest stdout is a pipe and fully buffered, so a test killed by its timeout shows
    // nothing at all; unbuffered, the last line printed says how far it got. On Windows a
    // Debug-CRT assertion or abort() otherwise opens a message box and waits for a click
    // that never comes on CI -- the test then "times out" with no clue why. Send those
    // reports to stderr and fail instead.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    InstallFatalReporters();
    const auto start = std::chrono::steady_clock::now();
    const std::string scratch = argv[1];
    RemoveTree(scratch);
    CHECK(sfe::MakeDirectory(scratch));
    const std::string config = Join(scratch, "config");
    CHECK(sfe::MakeDirectory(config));

    // C++ exceptions are caught here rather than left to terminate: on Windows the unhandled-
    // exception filter above sees them first, as an opaque 0xE06D7363, without the message.
    try
    {
        RUN(TestPullBackRanges());
        RUN(TestRangeAlgebraRound11());
        RUN(TestForwardRangesRound11());
        RUN(TestForwardRangesRound12());
        RUN(TestSha256KnownAnswers());
        RUN(TestDecompileThroughBundle(config));
        RUN(TestNzMaskShiftRound11());
        RUN(TestIntersectMixedMasksRound12());
        RUN(TestSetNZMaskRound12());
        RUN(TestWidenRound12());
        RUN(TestScarryMinimumRound12());
        RUN(TestRulesByValueRound12());
        RUN(TestRangeSetsRound13());
        RUN(TestSignHighBitRound13());
        RUN(TestCancelDuringJumpTableRecovery());
        RUN(TestReuseAndRepair(config));
        RUN(TestForeignHashUntouched(config));
        RUN(TestLinkInPlaceOfBundle(config));
        RUN(TestConcurrentRepair(config));
        RUN(TestStagedCopyVerifiedBeforePublishing(config));
        RUN(TestUnwritableConfigDir(scratch));
    }
    catch (ghidra::LowlevelError& e)
    {
        std::printf("FATAL: uncaught ghidra::LowlevelError: %s\n", e.explain.c_str());
        return 3;
    }
    catch (std::exception& e)
    {
        std::printf("FATAL: uncaught std::exception: %s\n", e.what());
        return 3;
    }
    catch (...)
    {
        std::printf("FATAL: uncaught exception of unknown type\n");
        return 3;
    }
    ghidra::shutdownDecompilerLibrary();
    RemoveTree(scratch);
    std::printf("Sh2DecompilerEngineTests: %.1f ms total\n", MsSince(start));
    if (gFailures)
    {
        std::printf("%d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("all passed\n");
    return 0;
}
