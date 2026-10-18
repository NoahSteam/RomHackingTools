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
#include "sleigh_arch.hh"

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
// forward half of CircleRange (push-forward, strided intersect) has open defects (REVIEW_LEDGER.md) and is not pinned here; the part below holds today.
//  1. step-1 ranges of one byte, every (left,right) pair against a sample of partners: intersect and circleUnion are exactly the set operation
//     when they answer 0, invert is the complement, translate2Op's comparison is true on exactly the range, getSize counts, contains agrees
//  2. the pull-back at 3 and 4 bytes (round 10's sweep is exhaustive at 1 and 2), the points where the truth can change, each +-3, and random ones
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
        for (int in : { 3, 4 })
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
                    if (b.opc == CPUI_INT_RIGHT || b.opc == CPUI_INT_SRIGHT) { slot = (next() % 4) ? 0 : 1; if (slot == 0) val = next() % (8 * in + 3); }
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
        typedef __int128 s128;
        long n = 0;
        for (int sz : { 1, 2, 4, 8 })
        {
            const uint64_t m = PbMask(sz);
            auto sx = [&](uint64_t v) { return (s128)PbSigned(v, sz); };
            std::vector<uint64_t> ed = { 0, 1, 2, 3, 7, 8, 15, 16, 31, 32, 63, 64, 127, 128, 129, 255, 256, m, m - 1, m >> 1, (m >> 1) + 1, (m >> 1) - 1, 0x5555555555555555ull & m, 0xAAAAAAAAAAAAAAAAull & m };
            for (uint64_t& v : ed) v &= m;
            auto pair = [&](uint64_t a, uint64_t b) {
                ++n;
                const s128 lo = -((s128)1 << (8 * sz - 1)), hi = ((s128)1 << (8 * sz - 1)) - 1;
                auto bad = [&](const char* nm) { fail(nm, std::to_string(sz) + " bytes " + hex(a) + " " + hex(b)); };
                if (add.evaluateBinary(sz, sz, a, b) != ((a + b) & m)) bad("INT_ADD");
                if (sub.evaluateBinary(sz, sz, a, b) != ((a - b) & m)) bad("INT_SUB");
                if (mul.evaluateBinary(sz, sz, a, b) != ((a * b) & m)) bad("INT_MULT");
                if (andb.evaluateBinary(sz, sz, a, b) != (a & b) || orb.evaluateBinary(sz, sz, a, b) != (a | b) || xorb.evaluateBinary(sz, sz, a, b) != (a ^ b)) bad("bitwise");
                if (eq.evaluateBinary(1, sz, a, b) != (uint64_t)(a == b) || lt.evaluateBinary(1, sz, a, b) != (uint64_t)(a < b)) bad("compare");
                if (slt.evaluateBinary(1, sz, a, b) != (uint64_t)(sx(a) < sx(b))) bad("INT_SLESS");
                if (carry.evaluateBinary(1, sz, a, b) != (uint64_t)(((unsigned __int128)a + b) > (unsigned __int128)m)) bad("INT_CARRY");
                { const s128 s = sx(a) + sx(b); if (scarry.evaluateBinary(1, sz, a, b) != (uint64_t)(s < lo || s > hi)) bad("INT_SCARRY"); }
                { const s128 s = sx(a) - sx(b); if (sborrow.evaluateBinary(1, sz, a, b) != (uint64_t)(s < lo || s > hi)) bad("INT_SBORROW"); }
                if (b != 0)
                {
                    if (udiv.evaluateBinary(sz, sz, a, b) != a / b || urem.evaluateBinary(sz, sz, a, b) != a % b) bad("INT_DIV/REM");
                    if (!(sz == 8 && a == 0x8000000000000000ull && b == m))   // INT64_MIN s/ -1 is a host trap (A2-72)
                        if (sdiv.evaluateBinary(sz, sz, a, b) != (uint64_t)((sx(a) / sx(b)) & (s128)m) || srem.evaluateBinary(sz, sz, a, b) != (uint64_t)((sx(a) % sx(b)) & (s128)m)) bad("INT_SDIV/SREM");
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
        RUN(TestSha256KnownAnswers());
        RUN(TestDecompileThroughBundle(config));
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
