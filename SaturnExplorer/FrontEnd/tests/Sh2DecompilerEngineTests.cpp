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
#include "sleigh_arch.hh"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
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

// A config directory nothing can be written into: "unavailable" naming the path, not a crash.
void TestUnwritableConfigDir(const std::string& scratch)
{
    const std::string config = Join(scratch, "readonly-config");
    RemoveTree(config);
    CHECK(sfe::MakeDirectory(config));

    bool readOnlyDir = false;
#ifndef _WIN32
    // A permission-bit read-only directory, unless we are root (which ignores the bits).
    if (::chmod(config.c_str(), 0500) == 0 && ::access(config.c_str(), W_OK) != 0) readOnlyDir = true;
#endif
    std::string target = config;
    if (!readOnlyDir)
    {
        // Windows does not enforce a read-only directory attribute, and root ignores mode bits;
        // a config "directory" whose parent is a regular file is unwritable everywhere.
        const std::string file = Join(scratch, "not-a-directory");
        CHECK(WriteFile(file, "x", 1));
        target = Join(file, "config");
    }
    std::printf("unwritable config dir: %s\n", readOnlyDir ? "read-only directory (mode 0500)" : "path below a regular file");

    const sfe::decomp::SpecBundleResult b = sfe::decomp::MaterialiseSh2SpecBundle(target);
    std::printf("  -> %s\n", b.error.c_str());
    CHECK(!b.ok);
    CHECK(b.dir.empty());
    CHECK(b.error.find("unavailable") != std::string::npos);
    CHECK(b.error.find(target) != std::string::npos);

#ifndef _WIN32
    if (readOnlyDir)
    {
        // Materialised while writable, then the config dir became read-only: still usable,
        // since the engine only reads it. A damaged one cannot be repaired and says so.
        ::chmod(config.c_str(), 0700);
        sfe::decomp::SpecBundleResult ok = sfe::decomp::MaterialiseSh2SpecBundle(config);
        CHECK(ok.ok);
        const std::string root = Join(config, "decompiler");
        ::chmod(root.c_str(), 0500);
        ::chmod(config.c_str(), 0500);
        sfe::decomp::SpecBundleResult again = sfe::decomp::MaterialiseSh2SpecBundle(config);
        CHECK(again.ok && !again.extracted);

        ::chmod(root.c_str(), 0700);
        CHECK(sfe::RemoveFile(Join(ok.dir, "sh-2.sla")));
        ::chmod(root.c_str(), 0500);
        ::chmod(ok.dir.c_str(), 0500);
        sfe::decomp::SpecBundleResult broken = sfe::decomp::MaterialiseSh2SpecBundle(config);
        std::printf("  damaged and read-only -> %s\n", broken.error.c_str());
        CHECK(!broken.ok);
        CHECK(broken.error.find(ok.dir) != std::string::npos);
        ::chmod(ok.dir.c_str(), 0700);
        ::chmod(root.c_str(), 0700);
    }
#endif
    RemoveTree(config);
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
        RUN(TestSha256KnownAnswers());
        RUN(TestDecompileThroughBundle(config));
        RUN(TestCancelDuringJumpTableRecovery());
        RUN(TestReuseAndRepair(config));
        RUN(TestForeignHashUntouched(config));
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
