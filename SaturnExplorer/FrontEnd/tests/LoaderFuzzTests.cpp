// Area 9 of the review program (REVIEW_PROGRAM.md): BIOS and external files. The oracle for every
// loader the decompiler's path depends on, fed truncated, oversized, bit-flipped and malformed
// inputs, by two instances at once, and through links:
//
//   savestate   .yss, Mednafen MDFNSVST (raw and gzip), a linear dump at any base, a region
//               directory (Drivers/Savestate), then the core's snapshot and the decompiler's
//               MemorySnapshot::Capture over what opened
//   bios        LoadBiosImageFile: the path the BIOS button takes
//   settings    Settings::Load / Save: settings.ini ([decompiler] bios, volatile, autorefresh)
//   names       FunctionNames::Load / Import / Save: saturn_functions.txt, saturn_symbols.txt
//   bounds      FunctionBounds::Load / Save: saturn_function_bounds.txt
//   comments    AssemblyPanel::LoadComments / SaveComments: assembly_comments.txt (the notes the pseudocode shows)
//   ranges      ParseAddressRanges: the volatile list, as typed and as settings.ini keeps it
//   specbundle  MaterialiseSh2SpecBundle over a damaged directory
//   links       the BIOS path as a symlink, a dangling or looping link, a directory, a FIFO,
//               /dev/zero; the same file read again after it changed
//   concurrent  several processes on one file: nobody reads a half-written file
//
// The invariants, held after every case:
//   I1  no crash (signal, sanitizer report), no hang (a case takes under kSlowMs), no unbounded
//       allocation (a loader declares what it may hold; asking for more is the violation).
//   I2  a rejected file is rejected with an error (a result code, a sentence naming the path).
//   I3  nothing partly loaded is shown as valid: a region is whole or absent, an entry comes from
//       a line that is one, a failed load leaves the state it found.
//   I4  two instances on one file do not corrupt each other.
//
// A violation is a line "VIOLATION <module>/<class> ..." with the case that made it (seed, index,
// the mutation) and the input saved under <scratch>/repro. A class with a ledger row that is not
// yet fixed is in kKnown and is counted, not failed; a class that stops occurring while listed
// there is reported, so the entry is removed with the fix.
//
// Default: a fixed number of cases per module, seeded, a few seconds. Long runs:
//   SE_LOADFUZZ_CASES=<n>      cases per module (default 160; modules scale it)
//   SE_LOADFUZZ_SEED=<n>       base seed (default 9)
//   SE_LOADFUZZ_FROM=<k>       first case index (a long run in pieces: FROM=0 CASES=5000, FROM=5000 ...; the directed
//                              cases run only when FROM is 0)
//   SE_LOADFUZZ_NOFIXED=1      skip the directed cases (pins, oversize, links, concurrent)
//   SE_LOADFUZZ_ONLY=<name>    one module
//   SE_LOADFUZZ_CASE=<k>       one case, with the mutation printed (with ONLY)
//   SE_LOADFUZZ_PROCS=<n>      writers and readers in the concurrency module (default 3)
//   SE_LOADFUZZ_ROUNDS=<n>     rounds of the concurrency module (default 40)
#include "AssemblyPanel.h"
#include "Debug/CallStack.h"
#include "Debug/MemoryBackend.h"
#include "Decompiler/MemorySnapshot.h"
#include "Decompiler/Sh2Decompiler.h"
#include "Decompiler/Sh2FunctionFinder.h"
#include "Decompiler/SpecBundle.h"
#include "Disc/PathUtil.h"
#include "FileWrite.h"
#include "Settings.h"
#include "Sha256.h"
#include "SavestateDriver.h"
#include "saturnexplorer/SeHost.h"

#if defined(SE_HAVE_ZLIB)
#include <zlib.h>
#endif

#include <dirent.h>
#include <fcntl.h>
#include <malloc.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <new>
#include <set>
#include <string>
#include <vector>

#if defined(__SANITIZE_ADDRESS__)
#define SE_LF_ASAN 1
extern "C" void __sanitizer_set_death_callback(void (*)(void));
#else
#define SE_LF_ASAN 0
#endif

using sfe::decomp::BiosImage;
using sfe::decomp::BiosLoadResult;
using sfe::decomp::MemorySnapshot;

namespace
{
using Bytes = std::vector<uint8_t>;

// ---------------------------------------------------------------------------------------------
// Allocation accounting. The global operator new is replaced so a case can declare what its
// loader may hold: past the cap an allocation is refused (std::bad_alloc, as an address-space
// limit would), and the refusal is itself recorded as a violation whether or not the loader
// survives it. Nothing here limits memory in any other way, so the run is safe on a shared machine.
// ---------------------------------------------------------------------------------------------
std::atomic<size_t> gLive(0), gPeak(0), gBiggest(0);
std::atomic<size_t> gLiveCap(~size_t(0));      // the live total a case may reach
std::atomic<size_t> gSingleCap(~size_t(0));    // the biggest single request a case may make
std::atomic<bool> gCapHit(false);
std::atomic<bool> gTrack(false);

void* TrackedAlloc(size_t n, bool nothrow)
{
    if (gTrack.load(std::memory_order_relaxed))
    {
        if (n > gBiggest.load(std::memory_order_relaxed)) gBiggest.store(n, std::memory_order_relaxed);
        if (n > gSingleCap.load(std::memory_order_relaxed) ||
            gLive.load(std::memory_order_relaxed) + n > gLiveCap.load(std::memory_order_relaxed))
        {
            gCapHit.store(true, std::memory_order_relaxed);
            if (nothrow) return nullptr;
            throw std::bad_alloc();
        }
    }
    void* p = std::malloc(n ? n : 1);
    if (!p)
    {
        if (nothrow) return nullptr;
        throw std::bad_alloc();
    }
    if (gTrack.load(std::memory_order_relaxed))
    {
        const size_t live = gLive.fetch_add(malloc_usable_size(p), std::memory_order_relaxed) + malloc_usable_size(p);
        if (live > gPeak.load(std::memory_order_relaxed)) gPeak.store(live, std::memory_order_relaxed);
    }
    return p;
}

void TrackedFree(void* p) noexcept
{
    if (!p) return;
    if (gTrack.load(std::memory_order_relaxed))
    {
        const size_t u = malloc_usable_size(p);
        size_t cur = gLive.load(std::memory_order_relaxed);
        gLive.store(cur >= u ? cur - u : 0, std::memory_order_relaxed);
    }
    std::free(p);
}
}  // namespace

void* operator new(size_t n) { return TrackedAlloc(n, false); }
void* operator new[](size_t n) { return TrackedAlloc(n, false); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { return TrackedAlloc(n, true); }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { return TrackedAlloc(n, true); }
void operator delete(void* p) noexcept { TrackedFree(p); }
void operator delete[](void* p) noexcept { TrackedFree(p); }
void operator delete(void* p, size_t) noexcept { TrackedFree(p); }
void operator delete[](void* p, size_t) noexcept { TrackedFree(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { TrackedFree(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { TrackedFree(p); }

namespace
{
// ---------------------------------------------------------------------------------------------
// The record of what happened.
// ---------------------------------------------------------------------------------------------
const int kSlowMs = 5000;   // a case that takes longer is a hang, whatever it was doing

// Violation classes the ledger holds as open: class -> row. Counted and printed, not failed.
// (Emptied as the rows are fixed.)
const std::map<std::string, std::string>& Known()
{
    static const std::map<std::string, std::string> k;
    return k;
}

struct Finding
{
    size_t count = 0;
    std::string firstCase, firstMessage, repro;
};

std::map<std::string, Finding> gFindings;
std::map<std::string, size_t> gCases, gAccepted;
std::string gScratch;                 // the run's directory
std::string gCurModule, gCurCase;     // what the signal handler reports
const Bytes* gCurInput = nullptr;     // saved on a crash
uint64_t gSeed = 9;

std::string Hex(uint64_t v)
{
    char b[24];
    std::snprintf(b, sizeof b, "%llx", static_cast<unsigned long long>(v));
    return b;
}

bool WriteBytes(const std::string& path, const void* data, size_t size)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = size == 0 || std::fwrite(data, 1, size, f) == size;
    return std::fclose(f) == 0 && ok;
}

bool WriteBytes(const std::string& path, const Bytes& b) { return WriteBytes(path, b.data(), b.size()); }

bool WriteText(const std::string& path, const std::string& s) { return WriteBytes(path, s.data(), s.size()); }

bool ReadBytes(const std::string& path, Bytes& out)
{
    std::string s;
    if (!sfe::ReadWholeFile(path, s)) return false;
    out.assign(s.begin(), s.end());
    return true;
}

void MakeDir(const std::string& d) { ::mkdir(d.c_str(), 0755); }

void RemoveTree(const std::string& path)
{
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode))
    {
        if (DIR* d = ::opendir(path.c_str()))
        {
            std::vector<std::string> names;
            while (struct dirent* e = ::readdir(d))
            {
                const std::string n = e->d_name;
                if (n != "." && n != "..") names.push_back(n);
            }
            ::closedir(d);
            for (const std::string& n : names) RemoveTree(path + "/" + n);
        }
        ::rmdir(path.c_str());
    }
    else
    {
        ::unlink(path.c_str());
    }
}

void SaveRepro(const std::string& key, const Bytes* input)
{
    if (!input) return;
    MakeDir(gScratch + "/repro");
    std::string name = key;
    for (char& c : name) if (c == '/') c = '_';
    WriteBytes(gScratch + "/repro/" + name + ".bin", *input);
}

// A violation. Counted per class; the first of each class keeps its case and an input.
void Violation(const std::string& cls, const std::string& message)
{
    const std::string key = gCurModule + "/" + cls;
    Finding& f = gFindings[key];
    if (f.count++ == 0)
    {
        f.firstCase = gCurCase;
        f.firstMessage = message;
        if (gCurInput) { SaveRepro(key, gCurInput); f.repro = "repro/" + key; }
        std::printf("VIOLATION %s: %s\n    case: %s\n", key.c_str(), message.c_str(), gCurCase.c_str());
        std::fflush(stdout);
    }
}

void CrashReport(const char* why)
{
    // Async-signal-unsafe on purpose: the process is ending, and the case is what the reader needs.
    std::fprintf(stderr, "\nCRASH (%s) in %s, case: %s\n", why, gCurModule.c_str(), gCurCase.c_str());
    if (gCurInput) SaveRepro(gCurModule + "/crash", gCurInput);
    std::fflush(stderr);
}

void OnSignal(int sig)
{
    CrashReport(sig == SIGALRM ? "no answer within the watchdog: a hang" : strsignal(sig));
    ::_exit(sig == SIGALRM ? 87 : 86);
}

#if SE_LF_ASAN
void OnSanitizerDeath() { CrashReport("sanitizer"); }
#endif

void InstallHandlers()
{
    struct sigaction sa;
    std::memset(&sa, 0, sizeof sa);
    sa.sa_handler = OnSignal;
    sigemptyset(&sa.sa_mask);
    for (int s : { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGALRM }) ::sigaction(s, &sa, nullptr);
#if SE_LF_ASAN
    __sanitizer_set_death_callback(OnSanitizerDeath);
#endif
}

// ---------------------------------------------------------------------------------------------
// Random numbers and mutations.
// ---------------------------------------------------------------------------------------------
struct Rng
{
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull) { Next(); Next(); }
    uint64_t Next()
    {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return s * 0x2545F4914F6CDD1Dull;
    }
    uint32_t Below(uint32_t n) { return n ? static_cast<uint32_t>(Next() % n) : 0; }
    bool Chance(uint32_t percent) { return Below(100) < percent; }
};

uint64_t Mix(uint64_t a, uint64_t b, uint64_t c)
{
    uint64_t h = a * 0x9E3779B97F4A7C15ull;
    h ^= b + 0x7F4A7C15ull + (h << 6) + (h >> 2);
    h ^= c * 0xC2B2AE3D27D4EB4Full + (h << 6) + (h >> 2);
    return h;
}

uint64_t Fnv(const void* data, size_t n)
{
    const uint8_t* p = static_cast<const uint8_t*>(data);
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i) h = (h ^ p[i]) * 1099511628211ull;
    return h;
}

// The offsets of an input where a length, a count or an offset lives; a mutation lands there more
// often than a uniform one would, which is what reaches the parsers' checks.
struct Sample
{
    Bytes bytes;
    std::vector<size_t> hot;   // offsets of header fields
    std::vector<size_t> cuts;  // offsets where a section ends / a field starts (truncation points)
};

const uint32_t kInteresting[] = { 0u, 1u, 2u, 3u, 4u, 0x1Fu, 0x20u, 0x7Fu, 0x80u, 0xFFu, 0x100u, 0x7FFFu, 0x8000u, 0xFFFFu, 0x10000u,
                                  0x7FFFFFu, 0x80000u, 0x80001u, 0xFFFFFFu, 0x100000u, 0x100001u, 0x7FFFFFFFu, 0x80000000u,
                                  0xFFFFFFF0u, 0xFFFFFFFEu, 0xFFFFFFFFu, 0x3FFFFFFFu, 0x40000000u, 0x04000000u, 0x04000001u };

// One mutation, chosen and applied by 'rng'; 'desc' says which, so a case can be replayed.
Bytes Mutate(const Sample& base, Rng& rng, std::string& desc)
{
    Bytes b = base.bytes;
    const size_t n = b.size();
    char d[160];
    const uint32_t kind = rng.Below(11);
    auto hotOrAny = [&]() -> size_t {
        if (n == 0) return 0;
        if (!base.hot.empty() && rng.Chance(65)) return base.hot[rng.Below(static_cast<uint32_t>(base.hot.size()))] % n;
        return rng.Below(static_cast<uint32_t>(n));
    };
    switch (kind)
    {
    case 0:   // truncate at a field boundary, or just past or short of one
    {
        size_t at = 0;
        if (!base.cuts.empty() && rng.Chance(70))
        {
            at = base.cuts[rng.Below(static_cast<uint32_t>(base.cuts.size()))];
            const int delta = static_cast<int>(rng.Below(15)) - 7;
            at = static_cast<size_t>(std::max<long>(0, static_cast<long>(at) + delta));
        }
        else
        {
            at = rng.Below(static_cast<uint32_t>(n + 1));
        }
        if (at < b.size()) b.resize(at);
        std::snprintf(d, sizeof d, "truncate to %zu of %zu", at, n);
        break;
    }
    case 1:   // bit flips
    {
        const int flips = 1 + static_cast<int>(rng.Below(8));
        for (int i = 0; i < flips && n; ++i) b[hotOrAny()] ^= static_cast<uint8_t>(1u << rng.Below(8));
        std::snprintf(d, sizeof d, "%d bit flips", flips);
        break;
    }
    case 2:   // a byte set to an edge value
    {
        const size_t at = hotOrAny();
        static const uint8_t v[] = { 0x00, 0xFF, 0x7F, 0x80, 0x01, 0xFE };
        if (n) b[at] = v[rng.Below(6)];
        std::snprintf(d, sizeof d, "byte %zu := edge", at);
        break;
    }
    case 3:   // a 32-bit field set to an interesting value
    case 4:
    {
        size_t at = hotOrAny();
        if (rng.Chance(70)) at &= ~size_t(3);
        uint32_t v = kInteresting[rng.Below(sizeof kInteresting / sizeof kInteresting[0])];
        if (rng.Chance(25)) v = static_cast<uint32_t>(n) + rng.Below(5) - 2;
        if (rng.Chance(15)) v = static_cast<uint32_t>(n > at ? n - at : 0) + rng.Below(3) - 1;   // exactly the rest, give or take
        for (int i = 0; i < 4 && at + i < n; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * i));
        std::snprintf(d, sizeof d, "u32 at %zu := 0x%08X", at, v);
        break;
    }
    case 5:   // a 16-bit field the other way round (big-endian)
    {
        const size_t at = hotOrAny();
        const uint32_t v = kInteresting[rng.Below(sizeof kInteresting / sizeof kInteresting[0])];
        for (int i = 0; i < 4 && at + i < n; ++i) b[at + i] = static_cast<uint8_t>(v >> (8 * (3 - i)));
        std::snprintf(d, sizeof d, "be32 at %zu := 0x%08X", at, v);
        break;
    }
    case 6:   // insert bytes
    {
        const size_t at = hotOrAny();
        const size_t len = 1 + rng.Below(rng.Chance(20) ? 300 : 24);
        Bytes ins(len);
        for (uint8_t& x : ins) x = static_cast<uint8_t>(rng.Next());
        b.insert(b.begin() + std::min(at, b.size()), ins.begin(), ins.end());
        std::snprintf(d, sizeof d, "insert %zu bytes at %zu", len, at);
        break;
    }
    case 7:   // delete a range
    {
        const size_t at = hotOrAny();
        const size_t len = 1 + rng.Below(rng.Chance(30) ? 4096 : 32);
        if (at < b.size()) b.erase(b.begin() + at, b.begin() + std::min(b.size(), at + len));
        std::snprintf(d, sizeof d, "delete %zu bytes at %zu", len, at);
        break;
    }
    case 8:   // zero a range
    {
        const size_t at = hotOrAny();
        const size_t len = 1 + rng.Below(rng.Chance(30) ? 4096 : 32);
        for (size_t i = at; i < b.size() && i < at + len; ++i) b[i] = 0;
        std::snprintf(d, sizeof d, "zero %zu bytes at %zu", len, at);
        break;
    }
    case 9:   // oversized: append
    {
        const size_t len = rng.Chance(30) ? 1 + rng.Below(200000) : 1 + rng.Below(64);
        const uint8_t fill = static_cast<uint8_t>(rng.Next());
        const bool rnd = rng.Chance(50);
        for (size_t i = 0; i < len; ++i) b.push_back(rnd ? static_cast<uint8_t>(rng.Next()) : fill);
        std::snprintf(d, sizeof d, "append %zu bytes", len);
        break;
    }
    default:  // duplicate a range
    {
        const size_t at = hotOrAny();
        const size_t len = 1 + rng.Below(512);
        if (at < b.size())
        {
            Bytes copy(b.begin() + at, b.begin() + std::min(b.size(), at + len));
            b.insert(b.begin() + at, copy.begin(), copy.end());
        }
        std::snprintf(d, sizeof d, "duplicate %zu bytes at %zu", len, at);
        break;
    }
    }
    desc = d;
    return b;
}

// Two or three mutations on top of each other now and then.
Bytes MutateMany(const Sample& base, Rng& rng, std::string& desc)
{
    std::string d1;
    Bytes b = Mutate(base, rng, d1);
    desc = d1;
    if (rng.Chance(25))
    {
        Sample next;
        next.bytes = b;
        next.hot = base.hot;
        next.cuts = base.cuts;
        std::string d2;
        b = Mutate(next, rng, d2);
        desc += " + " + d2;
    }
    return b;
}

// ---------------------------------------------------------------------------------------------
// A case: its identity (for the report), a watchdog, the allocation scope, the clock.
// ---------------------------------------------------------------------------------------------
struct CaseScope
{
    std::chrono::steady_clock::time_point t0;
    long slowMs;
    explicit CaseScope(const std::string& module, uint64_t index, const std::string& desc, const Bytes* input, size_t liveCap,
                       size_t singleCap, long slowLimitMs = kSlowMs, unsigned watchdogSec = 60)
    {
        slowMs = slowLimitMs;
        gCurModule = module;
        char b[64];
        std::snprintf(b, sizeof b, "seed=%llu index=%llu ", static_cast<unsigned long long>(gSeed), static_cast<unsigned long long>(index));
        gCurCase = std::string(b) + desc;
        gCurInput = input;
        gCases[module]++;
        gLive = 0; gPeak = 0; gBiggest = 0;
        gLiveCap = liveCap; gSingleCap = singleCap; gCapHit = false;
        gTrack = true;
        ::alarm(watchdogSec);
        t0 = std::chrono::steady_clock::now();
    }
    ~CaseScope()
    {
        gTrack = false;
        ::alarm(0);
        const long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        if (ms > slowMs) Violation("HANG", "a case took " + std::to_string(ms) + " ms (limit " + std::to_string(slowMs) + ")");
        if (gCapHit)
            Violation("UNBOUNDED_ALLOC", "the loader asked for more than the " + std::to_string(gLiveCap.load() >> 20) + " MiB (single request " +
                      std::to_string(gSingleCap.load() >> 20) + " MiB) it may hold; the biggest request seen " + std::to_string(gBiggest.load() >> 10) + " KiB");
        gCurInput = nullptr;
    }
};

const size_t kMiB = 1u << 20;

// ---------------------------------------------------------------------------------------------
// Child processes: a case that may hang or end the process is run apart.
// ---------------------------------------------------------------------------------------------
struct ChildResult
{
    bool exited = false;
    int code = -1;
    int signal = 0;
    bool timedOut = false;
    long maxRssKiB = 0;
};

ChildResult RunChild(const std::function<int()>& fn, int timeoutSec)
{
    ChildResult r;
    std::fflush(stdout);
    const pid_t pid = ::fork();
    if (pid == 0)
    {
        ::alarm(static_cast<unsigned>(timeoutSec));
        ::signal(SIGALRM, SIG_DFL);
        for (int s : { SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT }) ::signal(s, SIG_DFL);
        int rc = 3;
        try { rc = fn(); } catch (...) { rc = 4; }
        std::fflush(stdout);
        ::_exit(rc);
    }
    if (pid < 0) return r;
    int status = 0;
    struct rusage ru;
    std::memset(&ru, 0, sizeof ru);
    ::wait4(pid, &status, 0, &ru);
    r.maxRssKiB = ru.ru_maxrss;
    if (WIFEXITED(status)) { r.exited = true; r.code = WEXITSTATUS(status); }
    if (WIFSIGNALED(status))
    {
        r.signal = WTERMSIG(status);
        r.timedOut = r.signal == SIGALRM;
    }
    return r;
}

// ---------------------------------------------------------------------------------------------
// Module: savestates. Builders for valid files of each shape.
// ---------------------------------------------------------------------------------------------
void P32(Bytes& v, uint32_t x)
{
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}

void Pattern(Bytes& v, size_t n, uint32_t salt)
{
    const size_t at = v.size();
    v.resize(at + n);
    for (size_t i = 0; i < n; ++i) v[at + i] = static_cast<uint8_t>((i * 7 + (i >> 9) + salt) & 0xFF);
}

const uint32_t kVram = 0x80000, kCram = 0x1000, kWram = 0x100000;

void YssSection(Sample& s, const char* tag, const Bytes& data)
{
    s.cuts.push_back(s.bytes.size());
    s.hot.push_back(s.bytes.size() + 8);
    s.bytes.insert(s.bytes.end(), tag, tag + 4);
    P32(s.bytes, 1);
    P32(s.bytes, static_cast<uint32_t>(data.size()));
    s.bytes.insert(s.bytes.end(), data.begin(), data.end());
}

Sample MakeYss(bool vdp1, bool vdp2, bool cpus, bool wram)
{
    Sample s;
    s.bytes = { 'Y', 'S', 'S', 1 };
    s.bytes.resize(0x14, 0);
    for (size_t i = 0; i < 0x14; ++i) s.hot.push_back(i);
    if (vdp1) { Bytes d(52, 1); Pattern(d, kVram, 1); YssSection(s, "VDP1", d); }
    if (vdp2) { Bytes d(288, 0); Pattern(d, kVram, 2); d.resize(d.size() + kCram, 3); d.resize(d.size() + 4, 4); YssSection(s, "VDP2", d); }
    if (cpus)
    {
        Bytes d;
        for (uint32_t i = 0; i < 23; ++i) P32(d, 0x06000000u + i * 0x111);
        d.resize(100, 0);
        YssSection(s, "MSH2", d);
        YssSection(s, "SSH2", d);
    }
    if (wram) { Bytes d(0x10000, 5); Pattern(d, kWram, 6); Pattern(d, kWram, 7); d.resize(d.size() + 64, 8); YssSection(s, "OTHR", d); }
    s.cuts.push_back(s.bytes.size());
    return s;
}

void MdfnField(Bytes& sec, const char* name, const Bytes& payload)
{
    sec.push_back(static_cast<uint8_t>(std::strlen(name)));
    sec.insert(sec.end(), name, name + std::strlen(name));
    P32(sec, static_cast<uint32_t>(payload.size()));
    sec.insert(sec.end(), payload.begin(), payload.end());
}

void MdfnSection(Sample& s, const char* name, const Bytes& sec)
{
    s.cuts.push_back(s.bytes.size());
    const size_t at = s.bytes.size();
    s.bytes.resize(at + 32, 0);
    std::memcpy(&s.bytes[at], name, std::strlen(name));
    s.hot.push_back(s.bytes.size());
    P32(s.bytes, static_cast<uint32_t>(sec.size()));
    s.bytes.insert(s.bytes.end(), sec.begin(), sec.end());
}

Sample MakeMdfn(bool vdp, bool cpus, bool wram, bool bigEndian, uint32_t previewW = 0, uint32_t previewH = 0)
{
    Sample s;
    s.bytes = { 'M', 'D', 'F', 'N', 'S', 'V', 'S', 'T' };
    s.bytes.resize(32, 0);
    if (bigEndian) s.bytes[23] = 0x80;
    for (int i = 0; i < 4; ++i) { s.bytes[24 + i] = static_cast<uint8_t>(previewW >> (8 * i)); s.bytes[28 + i] = static_cast<uint8_t>(previewH >> (8 * i)); }
    for (size_t i = 8; i < 32; ++i) s.hot.push_back(i);
    s.bytes.resize(32 + static_cast<size_t>(previewW) * previewH * 3, 0x55);
    if (vdp)
    {
        Bytes v1; { Bytes vr; Pattern(vr, kVram, 1); MdfnField(v1, "VRAM", vr); MdfnField(v1, "TVMR", Bytes{ 1, 0 }); MdfnField(v1, "FBCR", Bytes{ 3, 0 }); }
        MdfnSection(s, "VDP1", v1);
        Bytes v2;
        { Bytes regs(0x200, 0); MdfnField(v2, "RawRegs", regs); }
        { Bytes vr; Pattern(vr, kVram, 2); MdfnField(v2, "VRAM", vr); }
        { Bytes cr; Pattern(cr, kCram, 3); MdfnField(v2, "CRAM", cr); }
        MdfnSection(s, "VDP2", v2);
    }
    if (wram)
    {
        Bytes m;
        { Bytes l; Pattern(l, kWram, 5); MdfnField(m, "WorkRAML", l); }
        { Bytes h; Pattern(h, kWram, 6); MdfnField(m, "WorkRAMH", h); }
        MdfnSection(s, "MAIN", m);
    }
    if (cpus)
    {
        for (const char* name : { "SH2-M", "SH2-S" })
        {
            Bytes c;
            { Bytes r; for (uint32_t i = 0; i < 16; ++i) P32(r, 0x80000000u | (i * 0x01010101u)); MdfnField(c, "R", r); }
            { Bytes pc; P32(pc, 0x06004000u); MdfnField(c, "PC", pc); }
            { Bytes x; for (uint32_t i = 0; i < 3; ++i) P32(x, 0xF0000000u + i); MdfnField(c, "CtrlRegs", x); }
            { Bytes x; for (uint32_t i = 0; i < 3; ++i) P32(x, 0xE0000000u + i); MdfnField(c, "SysRegs", x); }
            MdfnSection(s, name, c);
        }
    }
    s.cuts.push_back(s.bytes.size());
    return s;
}

Bytes Gzip(const Bytes& raw, int level)
{
#if defined(SE_HAVE_ZLIB)
    z_stream zs;
    std::memset(&zs, 0, sizeof zs);
    deflateInit2(&zs, level, Z_DEFLATED, 16 + MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    Bytes gz(deflateBound(&zs, static_cast<uLong>(raw.size())) + 64);
    zs.next_in = const_cast<Bytef*>(raw.data());
    zs.avail_in = static_cast<uInt>(raw.size());
    zs.next_out = gz.data();
    zs.avail_out = static_cast<uInt>(gz.size());
    deflate(&zs, Z_FINISH);
    gz.resize(zs.total_out);
    deflateEnd(&zs);
    return gz;
#else
    (void)level;
    return raw;
#endif
}

Sample MakeDump(uint32_t base, uint32_t from, uint32_t to)
{
    Sample s;
    s.bytes.resize(to - from);
    for (size_t i = 0; i < s.bytes.size(); ++i) s.bytes[i] = static_cast<uint8_t>((i * 13 + (i >> 8) + (i >> 16)) & 0xFF);
    (void)base;
    for (uint32_t a : { 0x00200000u, 0x00300000u, 0x05C00000u, 0x05C00018u, 0x05D00000u, 0x05D00018u, 0x05E00000u, 0x05E80000u,
                        0x05F00000u, 0x05F01000u, 0x05F80000u, 0x05F80200u, 0x06000000u, 0x06100000u })
        if (a >= from && a <= to) s.cuts.push_back(a - from);
    return s;
}

// What an opened source holds, read through its own callbacks.
struct Opened
{
    se_result code = SE_OK;
    bool ok() const { return code == SE_OK; }
    se_data_source ds;
    uint64_t lwram = 0, hwram = 0, vdp1 = 0, vdp2 = 0, cram = 0, v1regs = 0, v2regs = 0;
    size_t lwramN = 0, hwramN = 0, vdp1N = 0, vdp2N = 0, cramN = 0, v1regsN = 0, v2regsN = 0;
    bool sh2[2] = { false, false };
    Opened() { std::memset(&ds, 0, sizeof ds); }
};

size_t Probe(size_t (*cb)(void*, uint32_t, void*, size_t), void* user, uint32_t off, size_t want, Bytes& buf, uint64_t& digest)
{
    buf.assign(want, 0);
    const size_t got = cb(user, off, buf.data(), want);
    digest = Fnv(buf.data(), std::min(got, want));
    return got;
}

void Describe(Opened& o)
{
    Bytes buf;
    const se_data_source& d = o.ds;
    if (d.read_vdp1_vram) o.vdp1N = Probe(d.read_vdp1_vram, d.user, 0, 2 * kVram, buf, o.vdp1);
    if (d.read_vdp2_vram) o.vdp2N = Probe(d.read_vdp2_vram, d.user, 0, 2 * kVram, buf, o.vdp2);
    if (d.read_cram) o.cramN = Probe(d.read_cram, d.user, 0, 2 * kCram, buf, o.cram);
    if (d.read_main_ram)
    {
        buf.assign(2 * kWram, 0);
        o.lwramN = d.read_main_ram(d.user, 0x00200000u, buf.data(), buf.size());
        o.lwram = Fnv(buf.data(), o.lwramN);
        buf.assign(2 * kWram, 0);
        o.hwramN = d.read_main_ram(d.user, 0x06000000u, buf.data(), buf.size());
        o.hwram = Fnv(buf.data(), o.hwramN);
    }
    uint8_t reg[0x400];
    if (d.read_vdp1_reg) { uint64_t h = 1469598103934665603ull; for (uint32_t r = 0; r < 0x18; r += 2) { const uint16_t v = d.read_vdp1_reg(d.user, r); h = (h ^ v) * 1099511628211ull; } o.v1regs = h; o.v1regsN = 0x18; }
    if (d.read_vdp2_reg) { uint64_t h = 1469598103934665603ull; for (uint32_t r = 0; r < 0x200; r += 2) { const uint16_t v = d.read_vdp2_reg(d.user, r); h = (h ^ v) * 1099511628211ull; } o.v2regs = h; o.v2regsN = 0x200; }
    (void)reg;
    if (d.read_sh2_regs)
        for (int c = 0; c < 2; ++c) { se_sh2_regs r; std::memset(&r, 0, sizeof r); o.sh2[c] = d.read_sh2_regs(d.user, c, &r) != 0; }
}

bool AllZero(const void* p, size_t n)
{
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) if (b[i]) return false;
    return true;
}

// The invariants of an opened (or refused) source. 'tag' names the opener in the report.
void CheckSource(const char* tag, Opened& o, bool deep, bool partialOk = false)
{
    if (!o.ok())
    {
        if (!AllZero(&o.ds, sizeof o.ds)) Violation("OUT_NOT_ZEROED", std::string(tag) + ": a refused file left its output filled in");
        if (o.code > 0 || o.code < SE_ERR_NO_MEMORY) Violation("ERROR_CODE", std::string(tag) + ": refused with an unknown result " + std::to_string(o.code));
        return;
    }
    const se_data_source& d = o.ds;
    if (d.abi_version != SE_ABI_VERSION) Violation("ABI", std::string(tag) + ": abi_version is not the current one");
    if (!d.close) Violation("NO_CLOSE", std::string(tag) + ": an opened source with no close");
    struct Cap { uint32_t bit; bool present; const char* name; };
    const Cap caps[] = {
        { SE_CAP_VDP1_VRAM, d.read_vdp1_vram != nullptr, "vdp1 vram" }, { SE_CAP_VDP2_VRAM, d.read_vdp2_vram != nullptr, "vdp2 vram" },
        { SE_CAP_CRAM, d.read_cram != nullptr, "cram" }, { SE_CAP_MAIN_RAM, d.read_main_ram != nullptr, "main ram" },
        { SE_CAP_VDP1_REGS, d.read_vdp1_reg != nullptr, "vdp1 regs" }, { SE_CAP_VDP2_REGS, d.read_vdp2_reg != nullptr, "vdp2 regs" },
        { SE_CAP_SH2_REGS, d.read_sh2_regs != nullptr, "sh2 regs" } };
    bool any = false;
    for (const Cap& c : caps)
    {
        const bool bit = (d.capabilities & c.bit) != 0;
        any = any || bit;
        if (bit != c.present) Violation("CAPS", std::string(tag) + ": the capability bit and the callback for " + c.name + " disagree");
    }
    if (!any) Violation("EMPTY_OPEN", std::string(tag) + ": opened a source with no region at all");

    // I3: a region is whole or absent. The callbacks clamp, so the count they return is the region's size.
    Describe(o);
    auto whole = [&](const char* what, size_t got, size_t full, bool present) {
        if (present && got != full && got != 0 && !partialOk)
            Violation("PARTIAL_REGION", std::string(tag) + ": " + what + " holds " + std::to_string(got) + " of " + std::to_string(full) + " bytes and is offered as a region");
        if (present && got == 0)
            Violation("EMPTY_REGION", std::string(tag) + ": " + what + " is offered as a region and holds nothing");
    };
    whole("vdp1 vram", o.vdp1N, kVram, d.read_vdp1_vram != nullptr);
    whole("vdp2 vram", o.vdp2N, kVram, d.read_vdp2_vram != nullptr);
    whole("cram", o.cramN, kCram, d.read_cram != nullptr);
    if (d.read_main_ram)
    {
        whole("low work ram", o.lwramN, kWram, o.lwramN != 0);
        whole("high work ram", o.hwramN, kWram, o.hwramN != 0);
        if (o.lwramN == 0 && o.hwramN == 0) Violation("EMPTY_REGION", std::string(tag) + ": main ram is offered and neither work ram holds anything");
    }

    if (!deep) return;
    // The decompiler's view: the core's snapshot of the source, then MemorySnapshot::Capture over it. A work
    // RAM region the snapshot holds is whole and is the source's bytes; one that is not whole is absent.
    se_config cfg;
    cfg.abi_version = SE_ABI_VERSION;
    cfg.reserved = 0;
    se_context* ctx = se_create(&o.ds, &cfg);
    if (!ctx) return;   // the core refused the source: it closes nothing, the caller does
    se_begin_frame(ctx);
    sfe::ContextBackend backend(&ctx);
    se_sh2_regs regs[2];
    std::memset(regs, 0, sizeof regs);
    const bool have[2] = { false, false };
    std::shared_ptr<const MemorySnapshot> snap = MemorySnapshot::Capture(backend, regs, have, 1, 1, 1, nullptr);
    if (!snap)
    {
        Violation("SNAPSHOT_NULL", std::string(tag) + ": Capture returned nothing");
    }
    else
    {
        const struct { const char* name; uint32_t base; size_t got; uint64_t digest; } work[] = {
            { "LWRAM", MemorySnapshot::kLwramBase, o.lwramN, o.lwram }, { "HWRAM", MemorySnapshot::kHwramBase, o.hwramN, o.hwram } };
        for (const auto& w : work)
        {
            const sfe::decomp::MemoryRegion* r = snap->RegionAt(w.base);
            if (r)
            {
                if (r->bytes.size() != MemorySnapshot::kWorkRamSize)
                    Violation("SNAPSHOT_PARTIAL", std::string(tag) + ": the snapshot's " + w.name + " is " + std::to_string(r->bytes.size()) + " bytes");
                else if (w.got != kWram || Fnv(r->bytes.data(), r->bytes.size()) != w.digest)
                    Violation("SNAPSHOT_DIFFERS", std::string(tag) + ": the snapshot's " + w.name + " is not the source's bytes");
            }
            else if (w.got == kWram)
            {
                Violation("SNAPSHOT_MISSING", std::string(tag) + ": a whole " + w.name + " in the source is missing from the snapshot");
            }
        }
    }
    se_destroy(ctx);   // closes the source
    o.ds.close = nullptr;
}

void CloseOpened(Opened& o)
{
    if (o.ok() && o.ds.close) o.ds.close(o.ds.user);
    o.ds.close = nullptr;
}

struct SavestateKind
{
    const char* name;
    std::vector<Sample> samples;
};

std::vector<SavestateKind> g_ssKinds;

void BuildSavestateSamples()
{
    SavestateKind yss{ "yss", {} };
    yss.samples.push_back(MakeYss(true, true, true, true));
    yss.samples.push_back(MakeYss(false, true, false, false));
    yss.samples.push_back(MakeYss(false, false, true, true));
    SavestateKind mdfn{ "mdfn", {} };
    mdfn.samples.push_back(MakeMdfn(true, true, true, false));
    mdfn.samples.push_back(MakeMdfn(true, true, true, true));
    mdfn.samples.push_back(MakeMdfn(false, true, true, false, 8, 6));
    mdfn.samples.push_back(MakeMdfn(true, false, false, false));
    SavestateKind gz{ "mdfn.gz", {} };
    gz.samples.push_back(MakeMdfn(true, true, true, false));
    SavestateKind rawgz{ "yss.gz", {} };
    rawgz.samples.push_back(MakeYss(true, true, true, true));
    g_ssKinds = { yss, mdfn, gz, rawgz };
}

// The base address a dump is opened at, and what a dump of each shape holds.
struct DumpShape { uint32_t from, to, base; };
const DumpShape kDumps[] = {
    { 0x06000000u, 0x06100000u, 0x06000000u },   // high work RAM alone: the shape of hwram007.bin
    { 0x00200000u, 0x00300000u, 0x00200000u },   // low work RAM alone
    { 0x05C00000u, 0x05F80200u, 0x05C00000u },   // the video blocks
    { 0x00200000u, 0x06100000u, 0x00200000u },   // both work RAMs and what lies between (over the cap: refused)
};

void RunSavestateCase(uint64_t index, bool deepEvery)
{
    Rng rng(Mix(gSeed, 1, index));
    // Choose a family, a sample, a mutation, an entry point.
    const uint32_t fam = rng.Below(100);
    std::string desc;
    Bytes input;
    uint32_t base = 0;
    const char* kind = "";
    Sample sample;
    bool isDump = false;
    if (fam < 70)
    {
        const SavestateKind& k = g_ssKinds[rng.Below(static_cast<uint32_t>(g_ssKinds.size()))];
        const Sample& s0 = k.samples[rng.Below(static_cast<uint32_t>(k.samples.size()))];
        kind = k.name;
        if (std::strstr(k.name, ".gz"))
        {
            // Mutate the structure, then compress it (the inflated parser sees the damage); or damage the stream itself.
            if (rng.Chance(60))
            {
                std::string d;
                Bytes raw = MutateMany(s0, rng, d);
                input = Gzip(raw, 1);
                desc = "inner: " + d + ", then gzip";
            }
            else
            {
                Sample gzs;
                gzs.bytes = Gzip(s0.bytes, 1);
                gzs.hot = { 0, 1, 2, 3, 4, 8, 9 };
                for (size_t i = 0; i < 12; ++i) gzs.hot.push_back(gzs.bytes.size() - 1 - i);
                std::string d;
                input = MutateMany(gzs, rng, d);
                desc = "stream: " + d;
            }
        }
        else
        {
            input = MutateMany(s0, rng, desc);
        }
    }
    else if (fam < 95)
    {
        isDump = true;
        const DumpShape& ds = kDumps[rng.Below(sizeof kDumps / sizeof kDumps[0])];
        static std::map<int, Sample> cache;
        const int idx = static_cast<int>(&ds - kDumps);
        if (!cache.count(idx)) cache[idx] = MakeDump(ds.base, ds.from, ds.to);
        kind = "dump";
        input = MutateMany(cache[idx], rng, desc);
        base = rng.Chance(75) ? ds.base : kInteresting[rng.Below(sizeof kInteresting / sizeof kInteresting[0])];
        if (rng.Chance(10)) base = 0x06000000u + rng.Below(5) - 2;
        char b[48];
        std::snprintf(b, sizeof b, " (base 0x%08X)", base);
        desc += b;
    }
    else
    {
        kind = "emptyish";
        input.assign(rng.Below(80), 0);
        for (uint8_t& x : input) x = static_cast<uint8_t>(rng.Next());
        if (rng.Chance(50) && input.size() >= 8) std::memcpy(input.data(), rng.Chance(50) ? "MDFNSVST" : "YSS\x01\0\0\0\0", 8);
        desc = "short header";
    }
    CaseScope scope(std::string("savestate"), index, std::string(kind) + ": " + desc, &input, 700 * kMiB, 300 * kMiB);

    // The buffer opener.
    Opened a;
    if (isDump)
        a.code = se_savestate_open_full_dump_buffer(input.data(), input.size(), base, &a.ds);
    else
        a.code = se_savestate_open_buffer(input.data(), input.size(), &a.ds);
    gAccepted["savestate"] += a.ok();
    const bool deep = a.ok() && (index % 6 == 0 || deepEvery);
    // A linear dump may hold part of a region (a 64 KiB piece of work RAM opens, by design: SavestateShapeTests); the snapshot
    // below is where a part of a region must not pass for the whole.
    CheckSource(isDump ? "full_dump_buffer" : "open_buffer", a, deep, isDump);

    // The path opener must say the same (same bytes through the file layer), every few cases.
    if (index % 5 == 0 && input.size() < 40 * kMiB)
    {
        const std::string path = gScratch + "/savestate/case.bin";
        if (WriteBytes(path, input))
        {
            Opened b;
            if (isDump)
                b.code = se_savestate_open_full_dump(path.c_str(), base, &b.ds);
            else
                b.code = se_savestate_open(path.c_str(), &b.ds);
            CheckSource(isDump ? "full_dump" : "open", b, false, isDump);
            if (a.ok() != b.ok() && !(isDump && input.empty()))
                Violation("PATH_BUFFER_DISAGREE", std::string("the path opener and the buffer opener disagree on the same bytes (buffer ") +
                          (a.ok() ? "opens" : "refuses") + ", path " + (b.ok() ? "opens" : "refuses") + ")");
            else if (a.ok() && b.ok())
            {
                Opened x = a, y = b;
                if (x.ds.close != nullptr || true)
                {
                    // a was closed by a deep check, so compare against a fresh buffer open
                    Opened fresh;
                    fresh.code = isDump ? se_savestate_open_full_dump_buffer(input.data(), input.size(), base, &fresh.ds)
                                        : se_savestate_open_buffer(input.data(), input.size(), &fresh.ds);
                    if (fresh.ok())
                    {
                        Describe(fresh);
                        Describe(b);
                        if (fresh.lwram != b.lwram || fresh.hwram != b.hwram || fresh.vdp1 != b.vdp1 || fresh.vdp2 != b.vdp2 || fresh.cram != b.cram ||
                            fresh.lwramN != b.lwramN || fresh.hwramN != b.hwramN)
                            Violation("PATH_BUFFER_DIFFER", "the path opener and the buffer opener hold different bytes for the same input");
                        CloseOpened(fresh);
                    }
                }
                (void)x; (void)y;
            }
            CloseOpened(b);
            ::unlink(path.c_str());
        }
    }
    CloseOpened(a);
}

// Pins for the shapes random mutation reaches too rarely: a .yss cut inside the header of a section that follows a whole one
// (the later sections are what the cut lost), and one whose section size is a 32-bit number that wraps a 32-bit size_t.
void RunSavestatePins()
{
    int n = 0;
    const Sample full = MakeYss(true, true, true, false);
    for (size_t cut : full.cuts)
    {
        if (cut == 0 || cut >= full.bytes.size()) continue;
        for (size_t k = 1; k <= 11; ++k)
        {
            if (cut + k >= full.bytes.size()) continue;
            Bytes in(full.bytes.begin(), full.bytes.begin() + cut + k);
            CaseScope scope("savestate.pins", n++, "yss cut " + std::to_string(k) + " bytes into a section header at " + std::to_string(cut), &in, 700 * kMiB, 300 * kMiB);
            Opened o;
            o.code = se_savestate_open_buffer(in.data(), in.size(), &o.ds);
            if (o.ok()) Violation("YSS_TRUNCATED_HEADER_ACCEPTED", "a .yss ending " + std::to_string(k) + " bytes into a section header opened as a whole state");
            CloseOpened(o);
        }
    }
    // Sizes a 64-bit size_t compares correctly and a 32-bit one wraps: the section must still be refused.
    for (uint32_t size : { 0xFFFFFFF0u, 0xFFFFFFFFu, 0xFFFFFF80u, 0x80000000u })
    {
        Sample s = MakeYss(false, true, false, false);
        s.bytes.insert(s.bytes.end(), { 'M', 'S', 'H', '2' });
        P32(s.bytes, 1);
        P32(s.bytes, size);
        s.bytes.resize(s.bytes.size() + 8, 0);
        CaseScope scope("savestate.pins", n++, "yss section of size " + Hex(size), &s.bytes, 700 * kMiB, 300 * kMiB);
        Opened o;
        o.code = se_savestate_open_buffer(s.bytes.data(), s.bytes.size(), &o.ds);
        if (o.ok()) Violation("YSS_OVERSIZED_SECTION_ACCEPTED", "a .yss section of " + Hex(size) + " bytes in a file of " + std::to_string(s.bytes.size()) + " opened");
        CloseOpened(o);
    }
}

// A directory of region files, damaged file by file.
void RunRegionDirCase(uint64_t index)
{
    Rng rng(Mix(gSeed, 2, index));
    const std::string dir = gScratch + "/savestate/regions";
    RemoveTree(dir);
    MakeDir(dir);
    struct F { const char* name; size_t size; };
    const F files[] = { { "vdp1_vram.bin", kVram }, { "vdp2_vram.bin", kVram }, { "cram.bin", kCram }, { "wram_low.bin", kWram },
                        { "wram_high.bin", kWram }, { "vdp1_regs.bin", 0x18 }, { "vdp2_regs.bin", 0x200 } };
    std::string desc = "regions:";
    Bytes keep;
    for (const F& f : files)
    {
        if (rng.Chance(30)) continue;   // absent
        Sample s;
        Pattern(s.bytes, f.size, static_cast<uint32_t>(f.size));
        s.cuts = { f.size / 2, f.size - 1, f.size };
        std::string d = "ok";
        Bytes b = s.bytes;
        if (rng.Chance(45)) b = MutateMany(s, rng, d);
        desc += std::string(" ") + f.name + "(" + d + ")";
        const std::string path = dir + "/" + f.name;
        const uint32_t shape = rng.Below(20);
        if (shape == 0) { MakeDir(path); desc += "[dir]"; }
        else if (shape == 1) { ::symlink("/nonexistent/target", path.c_str()); desc += "[dangling]"; }
        else if (shape == 2) { ::symlink(f.name, path.c_str()); desc += "[loop]"; }
        else WriteBytes(path, b);
        keep = b;
    }
    CaseScope scope("regiondir", index, desc, &keep, 700 * kMiB, 300 * kMiB);
    Opened a;
    a.code = se_savestate_open_region_dir(dir.c_str(), &a.ds);
    gAccepted["regiondir"] += a.ok();
    CheckSource("region_dir", a, index % 4 == 0);
    CloseOpened(a);
}

// ---------------------------------------------------------------------------------------------
// Module: the BIOS loader.
// ---------------------------------------------------------------------------------------------
void CheckBios(const std::string& path, const Bytes& onDisk, bool fileIsRegular, const char* what)
{
    BiosLoadResult r = sfe::decomp::LoadBiosImageFile(path);
    if (r.image && !r.error.empty()) Violation("BOTH", std::string(what) + ": an image and an error");
    if (!r.image && r.error.empty()) Violation("NO_ERROR", std::string(what) + ": no image and no error");
    if (!r.image)
    {
        if (r.error.find(path) == std::string::npos) Violation("ERROR_NO_PATH", std::string(what) + ": the error does not name the file: " + r.error);
        // A rejected file must be a file the rules reject.
        if (fileIsRegular && !onDisk.empty() && onDisk.size() <= sfe::decomp::kMaxBiosImageBytes)
            Violation("REJECTED_VALID", std::string(what) + ": a regular file of " + std::to_string(onDisk.size()) + " bytes was refused: " + r.error);
        return;
    }
    if (r.image->bytes.size() > sfe::decomp::kMaxBiosImageBytes) Violation("OVERSIZE_ACCEPTED", std::string(what) + ": an image of " + std::to_string(r.image->bytes.size()) + " bytes was accepted");
    if (r.image->bytes.empty()) Violation("EMPTY_ACCEPTED", std::string(what) + ": an empty image was accepted");
    if (fileIsRegular && r.image->bytes != onDisk) Violation("BYTES_DIFFER", std::string(what) + ": the image is not the file's bytes");
    BiosLoadResult again = sfe::decomp::LoadBiosImageFile(path);
    if (!again.image || again.image->contentHash != r.image->contentHash) Violation("HASH_UNSTABLE", std::string(what) + ": the same file hashed twice gives two hashes");
    const std::shared_ptr<const BiosImage> direct = BiosImage::Make(r.image->bytes);
    if (direct->contentHash != r.image->contentHash) Violation("HASH_DIFFERS", std::string(what) + ": the hash is not BiosImage::Make's");
}

Sample BiosSample(uint32_t size, uint32_t salt)
{
    Sample s;
    Pattern(s.bytes, size, salt);
    s.hot = { 0, 1, 2, 3, size / 2, size ? size - 1 : 0 };
    s.cuts = { 0, 1, 2, size / 4, size / 2, size - 1, size };
    return s;
}

void RunBiosCase(uint64_t index)
{
    Rng rng(Mix(gSeed, 3, index));
    static const Sample samples[] = { BiosSample(0x80000, 1), BiosSample(0x40000, 2), BiosSample(4096, 3), BiosSample(2, 4) };
    const Sample& s = samples[rng.Below(4)];
    std::string desc;
    Bytes input = MutateMany(s, rng, desc);
    // The loader's contract is a file of at most 512 KiB: allow it twice that, no more, plus slack for the strings it builds.
    CaseScope scope("bios", index, desc, &input, 3 * sfe::decomp::kMaxBiosImageBytes + (1 << 20), 2 * sfe::decomp::kMaxBiosImageBytes + (1 << 20));
    const std::string path = gScratch + "/bios/bios.bin";
    WriteBytes(path, input);
    CheckBios(path, input, true, "regular file");
    gAccepted["bios"] += sfe::decomp::LoadBiosImageFile(path).image != nullptr;
    ::unlink(path.c_str());
}

// ---------------------------------------------------------------------------------------------
// Text loaders: settings.ini, saturn_functions.txt / symbol maps, saturn_function_bounds.txt.
// ---------------------------------------------------------------------------------------------
std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string Lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::vector<std::string> Lines(const std::string& text)
{
    std::vector<std::string> out;
    size_t p = 0;
    while (p <= text.size())
    {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string l = text.substr(p, e - p);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        if (!(e == text.size() && l.empty())) out.push_back(l);
        p = e + 1;
    }
    return out;
}

bool IsHex(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

// The symbol-map line as the documentation gives it: "<hex-addr> <name>", the address 1 to 8 hex digits with an optional 0x,
// then white space, then a name; blank lines and lines opening with ; # // are comments.
enum class LineKind { Entry, Comment, TooLong, Signed, NotHex, NoName };

LineKind ParseSymbolRef(const std::string& line, uint32_t& addr, std::string& name)
{
    size_t b = 0;
    while (b < line.size() && std::isspace(static_cast<unsigned char>(line[b]))) ++b;
    if (b >= line.size()) return LineKind::Comment;
    if (line[b] == ';' || line[b] == '#' || (line[b] == '/' && b + 1 < line.size() && line[b + 1] == '/')) return LineKind::Comment;
    size_t e = b;
    while (e < line.size() && !std::isspace(static_cast<unsigned char>(line[e]))) ++e;
    std::string tok = line.substr(b, e - b);
    if (tok[0] == '-' || tok[0] == '+') return LineKind::Signed;
    if (tok.size() > 2 && tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X')) tok = tok.substr(2);
    for (char c : tok) if (!IsHex(c)) return LineKind::NotHex;
    if (tok.size() > 8) return LineKind::TooLong;
    addr = static_cast<uint32_t>(std::strtoul(tok.c_str(), nullptr, 16));
    name = Trim(line.substr(e));
    return name.empty() ? LineKind::NoName : LineKind::Entry;
}

// Entries of the bounds file: the first token is an address (as above), anything after white space is a remark.
LineKind ParseBoundsRef(const std::string& line, uint32_t& addr)
{
    std::string name;
    LineKind k = ParseSymbolRef(line, addr, name);
    return k == LineKind::NoName ? LineKind::Entry : k;
}

const char* const kMalformedLines[] = {
    "100000000 big_address", "FFFFFFFFFFFFFFFF all_ones", "0000000106004000 sixteen", "-1 negative", "+6004000 plus", "0x name_after_bare_prefix",
    "12g name", "06004000name_glued", "Dead function in the old build", "add it later", "beef", "06004000", "0x06004000", "  06004000\t\tname with spaces   ",
    "06004000 ", ";06004000 commented", "// 06004000 slashes", "# 06004000 hash", "\xEF\xBB\xBF" "06004000 bom", "06004000 na\xFFme", "06004000 \tx",
    "0X0600A000 upper_prefix", "6004000 short", "00000000 zero_address", "FFFFFFFF last_address", "26004000 cache_through_mirror", "06100000 hwram_mirror",
    "4000 tiny_address", "0600 400 split", "06004000 name\x01with\x02" "controls" };

std::string MakeNamesText(Rng& rng)
{
    std::string t = rng.Chance(50) ? "; names\n" : "";
    const int n = 1 + rng.Below(12);
    for (int i = 0; i < n; ++i)
    {
        char b[96];
        std::snprintf(b, sizeof b, rng.Chance(30) ? "0x%08X sub_%d%s" : "%08x sub_%d%s", 0x06000000u + rng.Below(0x100000) * 2, i, rng.Chance(20) ? "\r\n" : "\n");
        t += b;
    }
    return t;
}

// A text mutated: bytes, then lines from the dictionary of malformed ones.
std::string MutateText(const std::string& base, Rng& rng, std::string& desc, const std::vector<std::string>& dict)
{
    Sample s;
    s.bytes.assign(base.begin(), base.end());
    for (size_t i = 0; i < s.bytes.size(); ++i)
        if (s.bytes[i] == '\n' || i % 9 == 0) { s.hot.push_back(i); s.cuts.push_back(i); }
    std::string d;
    std::string out;
    if (rng.Chance(75))
    {
        Bytes b = MutateMany(s, rng, d);
        out.assign(b.begin(), b.end());
    }
    else
    {
        out = base;
        d = "dictionary only";
    }
    if (!dict.empty() && rng.Chance(60))
    {
        const int k = 1 + rng.Below(3);
        for (int i = 0; i < k; ++i)
        {
            const std::string& l = dict[rng.Below(static_cast<uint32_t>(dict.size()))];
            const size_t at = rng.Chance(50) ? out.size() : rng.Below(static_cast<uint32_t>(out.size() + 1));
            out.insert(at, (at == 0 || out[at - 1] == '\n' ? "" : "\n") + l + "\n");
            d += " + line \"" + l.substr(0, 24) + "\"";
        }
    }
    desc = d;
    return out;
}

std::vector<std::string> DictLines()
{
    std::vector<std::string> v;
    for (const char* l : kMalformedLines) v.push_back(l);
    return v;
}

// cwd for the loaders that read a fixed relative name.
struct InDir
{
    std::string old;
    explicit InDir(const std::string& dir)
    {
        char b[4096];
        old = ::getcwd(b, sizeof b) ? b : ".";
        if (::chdir(dir.c_str()) != 0) std::printf("chdir %s failed\n", dir.c_str());
    }
    ~InDir() { if (::chdir(old.c_str()) != 0) {} }
};

struct Guarded
{
    // Runs fn, turning an escaped exception into a violation (an uncaught one ends the application).
    template <class F> static void Run(const char* what, F fn)
    {
        try { fn(); }
        catch (const std::bad_alloc&) { Violation("BAD_ALLOC_ESCAPED", std::string(what) + ": std::bad_alloc left the loader (nothing in the application catches it)"); }
        catch (const std::exception& e) { Violation("EXCEPTION_ESCAPED", std::string(what) + ": " + e.what()); }
    }
};

void RunNamesCase(uint64_t index)
{
    Rng rng(Mix(gSeed, 4, index));
    std::string desc;
    const std::string base = MakeNamesText(rng);
    const std::string text = MutateText(base, rng, desc, DictLines());
    Bytes keep(text.begin(), text.end());
    CaseScope scope("names", index, desc, &keep, 64 * kMiB, 32 * kMiB);
    const std::string dir = gScratch + "/names";
    InDir in(dir);
    const bool viaImport = rng.Chance(40);
    WriteText(viaImport ? dir + "/saturn_symbols.txt" : dir + "/saturn_functions.txt", text);
    sfe::FunctionNames names;
    size_t imported = 0;
    Guarded::Run("FunctionNames", [&] {
        if (viaImport) imported = names.Import("saturn_symbols.txt"); else names.Load();
    });
    gAccepted["names"] += !names.All().empty();

    std::map<uint32_t, std::string> ref;
    LineKind firstBad = LineKind::Entry;
    size_t refEntries = 0;
    for (const std::string& l : Lines(text))
    {
        uint32_t a = 0;
        std::string n;
        const LineKind k = ParseSymbolRef(l, a, n);
        if (k == LineKind::Entry) { ref[a] = n; ++refEntries; }
        else if (k != LineKind::Comment && k != LineKind::NoName && firstBad == LineKind::Entry) firstBad = k;
    }
    if (viaImport && imported != refEntries && firstBad == LineKind::Entry)
        Violation("IMPORT_COUNT", "Import reported " + std::to_string(imported) + " entries, the lines hold " + std::to_string(refEntries));
    if (names.All() != ref)
    {
        const char* cls = firstBad == LineKind::TooLong ? "ADDR_TRUNCATED" : firstBad == LineKind::Signed ? "SIGNED_ADDR" : firstBad == LineKind::NotHex ? "ACCEPTS_MALFORMED" : "SET_DIFFERS";
        // The product may only have accepted more than the reference (a malformed line read as an entry), never less.
        bool lessThanRef = false;
        for (const auto& kv : ref) { auto it = names.All().find(kv.first); if (it == names.All().end() || it->second != kv.second) lessThanRef = true; }
        if (lessThanRef && firstBad == LineKind::Entry) cls = "DROPS_VALID";
        Violation(cls, "the entries loaded (" + std::to_string(names.All().size()) + ") are not the well-formed lines' (" + std::to_string(ref.size()) + ")");
    }
    // Save then Load must give back what was loaded.
    Guarded::Run("FunctionNames::Save", [&] {
        names.Save();
        sfe::FunctionNames again;
        again.Load();
        if (again.All() != names.All()) Violation("ROUNDTRIP", "Save then Load does not give back the names");
    });
    ::unlink((dir + "/saturn_functions.txt").c_str());
    ::unlink((dir + "/saturn_symbols.txt").c_str());
}

void RunCommentsCase(uint64_t index)
{
    Rng rng(Mix(gSeed, 9, index));
    std::string desc;
    const std::string base = MakeNamesText(rng);
    const std::string text = MutateText(base, rng, desc, DictLines());
    Bytes keep(text.begin(), text.end());
    CaseScope scope("comments", index, desc, &keep, 64 * kMiB, 32 * kMiB);
    const std::string dir = gScratch + "/names";
    InDir in(dir);
    WriteText(dir + "/assembly_comments.txt", text);
    sfe::AssemblyPanel panel;
    Guarded::Run("AssemblyPanel::LoadComments", [&] { panel.LoadComments(); });
    gAccepted["comments"] += !panel.UserComments().empty();
    std::map<uint32_t, std::string> got(panel.UserComments().begin(), panel.UserComments().end()), ref;
    LineKind firstBad = LineKind::Entry;
    for (const std::string& l : Lines(text))
    {
        uint32_t a = 0;
        std::string n;
        const LineKind k = ParseSymbolRef(l, a, n);
        if (k == LineKind::Entry) ref[a] = n;
        else if (k != LineKind::Comment && k != LineKind::NoName && firstBad == LineKind::Entry) firstBad = k;
    }
    if (got != ref)
    {
        const char* cls = firstBad == LineKind::TooLong ? "ADDR_TRUNCATED" : firstBad == LineKind::Signed ? "SIGNED_ADDR" : firstBad == LineKind::NotHex ? "ACCEPTS_MALFORMED" : "SET_DIFFERS";
        Violation(cls, "the notes loaded (" + std::to_string(got.size()) + ") are not the well-formed lines' (" + std::to_string(ref.size()) + ")");
    }
    Guarded::Run("AssemblyPanel::SaveComments", [&] {
        panel.SaveComments();
        sfe::AssemblyPanel again;
        again.LoadComments();
        std::map<uint32_t, std::string> back(again.UserComments().begin(), again.UserComments().end());
        if (back != got) Violation("ROUNDTRIP", "Save then Load does not give back the notes");
    });
    ::unlink((dir + "/assembly_comments.txt").c_str());
}

void RunBoundsCase(uint64_t index)
{
    Rng rng(Mix(gSeed, 5, index));
    std::string desc;
    std::string base = "; Function entries set by hand\n";
    const int n = rng.Below(10);
    for (int i = 0; i < n; ++i) { char b[32]; std::snprintf(b, sizeof b, "%08X\n", 0x06000000u + rng.Below(0x100000) * 2); base += b; }
    const std::string text = MutateText(base, rng, desc, DictLines());
    Bytes keep(text.begin(), text.end());
    CaseScope scope("bounds", index, desc, &keep, 64 * kMiB, 32 * kMiB);
    const std::string path = gScratch + "/bounds/saturn_function_bounds.txt";
    WriteText(path, text);
    sfe::decomp::FunctionBounds fb;
    bool ok = true;
    Guarded::Run("FunctionBounds::Load", [&] { ok = fb.Load(path); });
    gAccepted["bounds"] += !fb.Entries().empty();
    if (!ok) Violation("REJECTED_READABLE", "Load refused a file it could read");

    std::set<uint32_t> ref;
    LineKind firstBad = LineKind::Entry;
    for (const std::string& l : Lines(text))
    {
        uint32_t a = 0;
        const LineKind k = ParseBoundsRef(l, a);
        if (k == LineKind::Entry) { uint32_t c = 0; if (sfe::CanonicalAddress(a, c)) ref.insert(c); }
        else if (k != LineKind::Comment && firstBad == LineKind::Entry) firstBad = k;
    }
    if (fb.Entries() != ref)
    {
        const char* cls = firstBad == LineKind::TooLong ? "ADDR_TRUNCATED" : firstBad == LineKind::Signed ? "SIGNED_ADDR" : firstBad == LineKind::NotHex ? "PROSE_AS_ADDRESS" : "SET_DIFFERS";
        Violation(cls, "the entries loaded (" + std::to_string(fb.Entries().size()) + ") are not the well-formed lines' (" + std::to_string(ref.size()) + ")");
    }
    Guarded::Run("FunctionBounds::Save", [&] {
        const std::string out = gScratch + "/bounds/out.txt";
        if (!fb.Save(out)) { Violation("SAVE_FAILED", "Save failed in a writable folder"); return; }
        sfe::decomp::FunctionBounds again;
        if (!again.Load(out) || again.Entries() != fb.Entries()) Violation("ROUNDTRIP", "Save then Load does not give back the entries");
    });
}

// settings.ini: the probed keys are the ones the application reads from [decompiler] and a few more.
void RunSettingsCase(uint64_t index)
{
    Rng rng(Mix(gSeed, 6, index));
    std::string base = "[panels]\nassembly = 1\n\n[data]\ndir = /games/one\n\n[decompiler]\nautorefresh = 1\nvolatile = 0x26001000-0x26001003, 25A01000\nbios = /home/u/bios.bin\n\n[callstack]\nsplit = 120.5\n";
    std::vector<std::string> dict = { "[decompiler]", "bios =", "bios = " + std::string(300, 'x'), "volatile = zz", "autorefresh = maybe", "= novalue", "[", "]", "[]", "[a]b]", "k = a = b", "\xEF\xBB\xBF[decompiler]",
                                      "bios = /a/b\r", "split = nan", "split = 1e999", "split = -0", "split = 0x10", "volatile = " + std::string(5000, '1') };
    std::string desc;
    const std::string text = MutateText(base, rng, desc, dict);
    Bytes keep(text.begin(), text.end());
    CaseScope scope("settings", index, desc, &keep, 64 * kMiB, 32 * kMiB);
    const std::string cfg = gScratch + "/cfg/SaturnExplorer";
    WriteText(cfg + "/settings.ini", text);
    sfe::Settings s;
    Guarded::Run("Settings::Load", [&] { s.Load(); });

    // The reference reading of the documented format.
    std::map<std::string, std::map<std::string, std::string>> ref;
    std::string section;
    for (const std::string& raw : Lines(text))
    {
        const std::string t = Trim(raw);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']') { section = Lower(Trim(t.substr(1, t.size() - 2))); continue; }
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Lower(Trim(t.substr(0, eq)));
        if (!key.empty()) ref[section][key] = Trim(t.substr(eq + 1));
    }
    gAccepted["settings"] += !ref.empty();
    bool differs = false;
    for (const auto& sec : ref)
        for (const auto& kv : sec.second)
            if (!s.Has(sec.first, kv.first) || s.Get(sec.first, kv.first) != kv.second) differs = true;
    for (const char* probe : { "assembly", "dir", "autorefresh", "volatile", "bios", "split" })
        for (const char* secName : { "panels", "data", "decompiler", "callstack", "" })
            if (s.Has(secName, probe) && !(ref.count(secName) && ref[secName].count(probe))) differs = true;
    if (differs) Violation("DIFFERS_FROM_FORMAT", "Settings read something other than what the format's lines say");

    // The numbers: a value that is not a number keeps the default.
    const float f = s.GetFloat("callstack", "split", 77.0f);
    if (f != f) Violation("NAN_VALUE", "GetFloat returned NaN from the file");
    // Save, then read again: what the file said survives.
    Guarded::Run("Settings::Save", [&] {
        std::string err;
        if (!s.Save(&err)) { Violation("SAVE_FAILED", "Save failed in a writable folder: " + err); return; }
        sfe::Settings again;
        again.Load();
        for (const auto& sec : ref)
            for (const auto& kv : sec.second)
                if (again.Get(sec.first, kv.first) != kv.second) { Violation("ROUNDTRIP", "[" + sec.first + "] " + kv.first + " changed over Save and Load"); return; }
    });
}

// Settings values the application writes: a path or a list with a line break in it must not turn into another key.
void RunSettingsValueCases()
{
    const std::string cfg = gScratch + "/cfg/SaturnExplorer";
    struct V { const char* what; std::string value; };
    const V values[] = {
        { "path with a line break", "/home/u/bios\nvolatile = 0x0-0xFFFFFFFF" },
        { "path with a carriage return", "/home/u/bios\rx" },
        { "path with leading blanks", "  /home/u/bios.bin" },
        { "path with a trailing blank", "/home/u/bios.bin " },
        { "list of two lines", "0x26001000-0x26001003\n25A01000" },
        { "value that opens like a comment", ";x" },
    };
    int n = 0;
    for (const V& v : values)
    {
        CaseScope scope("settings.values", n++, v.what, nullptr, 64 * kMiB, 32 * kMiB);
        sfe::Settings w;
        w.Set("decompiler", "bios", v.value);
        w.Set("decompiler", "autorefresh", "1");
        std::string err;
        const bool saved = w.Save(&err);
        sfe::Settings r;
        r.Load();
        std::string flat = v.value;
        for (char& c : flat) if (c == '\n') c = ' ';
        const bool same = r.Get("decompiler", "bios") == v.value;
        const bool injected = r.Has("decompiler", "volatile");
        if (saved && injected) Violation("VALUE_INJECTS_KEY", std::string(v.what) + ": the saved value became another key (volatile) on the next start");
        else if (saved && !same && Trim(flat) == r.Get("decompiler", "bios")) {}   // a line feed written as a space: a value that fails visibly
        else if (saved && !same && Trim(v.value) == v.value) Violation("VALUE_CHANGED", std::string(v.what) + ": read back as another value");
        // blanks round trip by design? the file format trims; the application's paths do not start or end with blanks.
    }
}

// An oversized file: a loader declares a ceiling and must not hold more than a small multiple of it.
void RunOversizeCases()
{
    const size_t big = 48 * kMiB;
    struct T { const char* module; std::string file; std::function<void()> run; };
    const std::string base = gScratch + "/oversize";
    MakeDir(base);
    auto sparse = [&](const std::string& path, bool newlines) {
        FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) return;
        if (newlines)
        {
            std::string line(1023, 'a');
            line += '\n';
            for (size_t i = 0; i < big / line.size(); ++i) std::fwrite(line.data(), 1, line.size(), f);
        }
        else if (::ftruncate(::fileno(f), static_cast<off_t>(big)) != 0) {}
        std::fclose(f);
    };
    const std::string names = base + "/saturn_functions.txt", bounds = base + "/bounds.txt", bios = base + "/bios.bin";
    {
        const std::string ini = gScratch + "/cfg/SaturnExplorer/settings.ini";
        for (int variant = 0; variant < 2; ++variant)
        {
            const bool newlines = variant == 1;
            const char* shape = newlines ? "48 MiB of 1 KiB lines" : "48 MiB without a line end";
            {
                sparse(ini, newlines);
                CaseScope scope("oversize.settings", variant, shape, nullptr, 32 * kMiB, 24 * kMiB);
                sfe::Settings s;
                Guarded::Run("Settings::Load", [&] {
                    std::string err;
                    const bool ok = s.Load(&err);
                    if (ok) Violation("OVERSIZE_ACCEPTED", "a settings file of 48 MiB was read");
                    else if (err.empty()) Violation("NO_ERROR", "a settings file was refused without an error");
                });
            }
            {
                sparse(names, newlines);
                InDir in(base);
                CaseScope scope("oversize.names", variant, shape, nullptr, 32 * kMiB, 24 * kMiB);
                sfe::FunctionNames fn;
                Guarded::Run("FunctionNames::Load", [&] {
                    std::string err;
                    const bool ok = fn.Load(&err);
                    if (ok) Violation("OVERSIZE_ACCEPTED", "a names file of 48 MiB was read");
                    else if (err.empty()) Violation("NO_ERROR", "a names file was refused without an error");
                });
            }
            {
                sparse(base + "/assembly_comments.txt", newlines);
                InDir in(base);
                CaseScope scope("oversize.comments", variant, shape, nullptr, 32 * kMiB, 24 * kMiB);
                sfe::AssemblyPanel panel;
                Guarded::Run("AssemblyPanel::LoadComments", [&] {
                    std::string err;
                    const bool ok = panel.LoadComments(&err);
                    if (ok) Violation("OVERSIZE_ACCEPTED", "a comments file of 48 MiB was read");
                    else if (err.empty()) Violation("NO_ERROR", "a comments file was refused without an error");
                });
            }
            {
                sparse(bounds, newlines);
                CaseScope scope("oversize.bounds", variant, shape, nullptr, 32 * kMiB, 24 * kMiB);
                sfe::decomp::FunctionBounds fb;
                Guarded::Run("FunctionBounds::Load", [&] {
                    std::string err;
                    const bool ok = fb.Load(bounds, &err);
                    if (ok) Violation("OVERSIZE_ACCEPTED", "a bounds file of 48 MiB was read");
                    else if (err.empty()) Violation("NO_ERROR", "a bounds file was refused without an error");
                });
            }
        }
        sparse(bios, false);
        CaseScope scope("oversize.bios", 0, "48 MiB file", nullptr, 3 * sfe::decomp::kMaxBiosImageBytes + (1 << 20), 2 * sfe::decomp::kMaxBiosImageBytes + (1 << 20));
        Guarded::Run("LoadBiosImageFile", [&] {
            BiosLoadResult r = sfe::decomp::LoadBiosImageFile(bios);
            if (r.image) Violation("OVERSIZE_ACCEPTED", "an image of 48 MiB was accepted");
            if (!r.image && r.error.empty()) Violation("NO_ERROR", "refused without an error");
        });
    }
    ::unlink((base + "/assembly_comments.txt").c_str());
    ::unlink(names.c_str()); ::unlink(bounds.c_str()); ::unlink(bios.c_str()); ::unlink((gScratch + "/cfg/SaturnExplorer/settings.ini").c_str());
}

// ---------------------------------------------------------------------------------------------
// ParseAddressRanges against an independent reading of "a-b, c d\ne".
// ---------------------------------------------------------------------------------------------
bool RefHex(const std::string& t, uint32_t& v)
{
    std::string d = t;
    if (d.size() > 2 && d[0] == '0' && (d[1] == 'x' || d[1] == 'X')) d = d.substr(2);
    if (d.empty() || d.size() > 8) return false;
    uint64_t x = 0;
    for (char c : d) { if (!IsHex(c)) return false; x = x * 16 + (std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::tolower(c) - 'a' + 10); }
    v = static_cast<uint32_t>(x);
    return true;
}

bool RefRanges(const std::string& in, std::vector<std::pair<uint32_t, uint32_t>>& out)
{
    out.clear();
    // Drop blanks next to a dash, then split on commas and blanks.
    std::string t;
    for (size_t i = 0; i < in.size(); ++i)
    {
        if (std::isspace(static_cast<unsigned char>(in[i])))
        {
            size_t j = i;
            while (j < in.size() && std::isspace(static_cast<unsigned char>(in[j]))) ++j;
            const bool before = !t.empty() && t.back() == '-';
            const bool after = j < in.size() && in[j] == '-';
            if (before || after) { i = j - 1; continue; }
        }
        t += in[i];
    }
    std::vector<std::string> entries;
    std::string cur;
    for (char c : t)
    {
        if (c == ',' || std::isspace(static_cast<unsigned char>(c))) { if (!cur.empty()) entries.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) entries.push_back(cur);
    for (const std::string& e : entries)
    {
        uint32_t lo = 0, hi = 0;
        const size_t dash = e.find('-');
        bool ok = dash == std::string::npos ? RefHex(e, lo) && RefHex(e, hi) : RefHex(e.substr(0, dash), lo) && RefHex(e.substr(dash + 1), hi);
        if (!ok || hi < lo) { out.clear(); return false; }
        out.emplace_back(lo, hi);
    }
    return true;
}

void RunRangesCase(uint64_t index)
{
    Rng rng(Mix(gSeed, 7, index));
    const char* pieces[] = { "0x26001000", "25A01000", "-", " - ", ",", " ", "\n", "\t", "0X1", "0x", "FFFFFFFF", "100000000", "g", "0", "ffffffff-00000000", "6000000-6000fff",
                              "--", ",,", "0x0x1", "1-2-3", "\xC0\x80", "\xEF\xBB\xBF" };
    std::string text;
    const int n = rng.Below(14);
    for (int i = 0; i < n; ++i) text += pieces[rng.Below(sizeof pieces / sizeof pieces[0])];
    if (rng.Chance(15)) text += std::string(rng.Below(2047), rng.Chance(50) ? ' ' : ',');   // the panel caps the list at 2047 characters
    if (text.size() > 2047) text.resize(2047);
    std::string desc = "text of " + std::to_string(text.size()) + " characters";
    Bytes keep(text.begin(), text.end());
    CaseScope scope("ranges", index, desc, &keep, 64 * kMiB, 32 * kMiB);
    std::vector<std::pair<uint32_t, uint32_t>> got, want;
    std::string error;
    const bool ok = sfe::decomp::ParseAddressRanges(text, got, error);
    const bool refOk = RefRanges(text, want);
    gAccepted["ranges"] += ok;
    if (ok != refOk) Violation("VERDICT_DIFFERS", std::string("accepted=") + (ok ? "yes" : "no") + " where the reading of the format says " + (refOk ? "yes" : "no"));
    else if (ok && got != want) Violation("RANGES_DIFFER", "the same text parses to other ranges");
    if (!ok && error.empty()) Violation("NO_ERROR", "refused without saying which entry");
    if (!ok && !got.empty()) Violation("PARTIAL_RESULT", "refused but left ranges in the output");
}

// ---------------------------------------------------------------------------------------------
// SpecBundle: the language files the engine reads, materialised over a damaged directory.
// ---------------------------------------------------------------------------------------------
std::string PublishedDir(const std::string& cfg)
{
    return cfg + "/decompiler/" + sfe::decomp::SpecBundleHash(sfe::decomp::Sh2SpecFiles());
}

// Does 'dir' hold exactly the embedded files? Read with a bound: a file that is not what it should be may be endless.
bool DirIsExact(const std::string& dir, std::string& why)
{
    for (const sfe::decomp::SpecFileData& f : sfe::decomp::Sh2SpecFiles())
    {
        Bytes b;
        FILE* fp = std::fopen((dir + "/" + f.name).c_str(), "rb");
        if (!fp) { why = f.name + " is missing"; return false; }
        b.resize(f.size + 1);
        const size_t got = std::fread(b.data(), 1, b.size(), fp);
        std::fclose(fp);
        if (got != f.size || std::memcmp(b.data(), f.data, f.size) != 0) { why = f.name + " differs"; return false; }
    }
    return true;
}

// Damage the published directory. 'dangerous' is set for the shapes that block or never end when read.
std::string Damage(const std::string& dir, Rng& rng, bool& dangerous, std::string& sentinelPath, Bytes& sentinel)
{
    const std::vector<sfe::decomp::SpecFileData>& files = sfe::decomp::Sh2SpecFiles();
    const std::string victim = dir + "/" + files[rng.Below(static_cast<uint32_t>(files.size()))].name;
    const uint32_t k = rng.Below(16);
    Bytes b;
    ReadBytes(victim, b);
    switch (k)
    {
    case 0: ::unlink(victim.c_str()); return "delete a file";
    case 1: if (!b.empty()) b.resize(rng.Below(static_cast<uint32_t>(b.size()))); WriteBytes(victim, b); return "truncate a file";
    case 2: if (!b.empty()) b[rng.Below(static_cast<uint32_t>(b.size()))] ^= 1u << rng.Below(8); WriteBytes(victim, b); return "flip a bit";
    case 3: b.insert(b.end(), 16, 0); WriteBytes(victim, b); return "append to a file";
    case 4: ::unlink(victim.c_str()); MakeDir(victim); return "a directory in place of a file";
    case 5:
    {
        sentinelPath = gScratch + "/spec/sentinel";
        sentinel.assign(64, 0xAB);
        WriteBytes(sentinelPath, sentinel);
        ::unlink(victim.c_str());
        ::symlink(sentinelPath.c_str(), victim.c_str());
        return "a link to a file outside, in place of a file";
    }
    case 6: ::unlink(victim.c_str()); ::symlink("/nonexistent/x", victim.c_str()); return "a dangling link in place of a file";
    case 7:
    {
        // A valid copy reached through a link is still the right bytes.
        const std::string copy = gScratch + "/spec/copy";
        WriteBytes(copy, b);
        ::unlink(victim.c_str());
        ::symlink(copy.c_str(), victim.c_str());
        return "a link to a valid copy";
    }
    case 8: { Bytes m; ReadBytes(dir + "/manifest.json", m); if (!m.empty()) m[rng.Below(static_cast<uint32_t>(m.size()))] ^= 0x20; WriteBytes(dir + "/manifest.json", m); return "damage the manifest"; }
    case 9: ::unlink((dir + "/manifest.json").c_str()); return "delete the manifest";
    case 10: { WriteText(dir + "/stray.txt", "x"); return "a stray file"; }
    case 11:
    {
        sentinelPath = gScratch + "/spec/sentinel_dir";
        RemoveTree(sentinelPath);
        MakeDir(sentinelPath);
        WriteText(sentinelPath + "/precious.txt", "keep");
        sentinel.assign(4, 'k');
        RemoveTree(dir);
        ::symlink(sentinelPath.c_str(), dir.c_str());
        return "the directory is a link to somebody's directory";
    }
    case 12: RemoveTree(dir); WriteText(dir, "a file"); return "a file in place of the directory";
    case 13:
    {
        ::unlink(victim.c_str());
        ::symlink("/dev/zero", victim.c_str());
        dangerous = true;
        return "a link to /dev/zero in place of a file";
    }
    case 14:
    {
        ::unlink(victim.c_str());
        ::mkfifo(victim.c_str(), 0600);
        dangerous = true;
        return "a FIFO in place of a file";
    }
    default:
    {
        // a staging directory left by a dead process
        const std::string stale = dir + ".tmp-1";
        MakeDir(stale);
        WriteText(stale + "/x", "x");
        return "a stale staging directory";
    }
    }
}

void RunSpecBundleCase(uint64_t index)
{
    Rng rng(Mix(gSeed, 8, index));
    const std::string root = gScratch + "/spec";
    RemoveTree(root);
    MakeDir(root);
    const std::string cfg = root + "/cfg";
    const std::string dir = PublishedDir(cfg);
    bool dangerous = false;
    std::string desc, sentinelPath;
    Bytes sentinel;
    {
        sfe::decomp::SpecBundleResult first = sfe::decomp::MaterialiseSh2SpecBundle(cfg);
        if (!first.ok) { Violation("FIRST_EXTRACT", "a clean extraction failed: " + first.error); return; }
    }
    const int damages = 1 + rng.Below(2);
    for (int i = 0; i < damages; ++i)
    {
        if (i) desc += ", ";
        desc += Damage(dir, rng, dangerous, sentinelPath, sentinel);
        if (desc.find("directory is a link") != std::string::npos || desc.find("a file in place of the directory") != std::string::npos) break;
    }
    auto verdict = [&](const sfe::decomp::SpecBundleResult& r) -> int {
        if (r.ok)
        {
            std::string why;
            if (!DirIsExact(r.dir, why)) { Violation("OK_BUT_WRONG", "reported ok and handed the engine a directory where " + why); return 6; }
            if (!sentinelPath.empty() && sentinelPath.find("sentinel_dir") != std::string::npos)
            {
                struct stat st;
                if (::stat((sentinelPath + "/precious.txt").c_str(), &st) != 0) { Violation("LINK_FOLLOWED", "the repair deleted the contents of the directory a link pointed to"); return 7; }
            }
            if (!sentinelPath.empty() && sentinelPath.find("sentinel") != std::string::npos && sentinelPath.find("sentinel_dir") == std::string::npos)
            {
                Bytes now;
                if (!ReadBytes(sentinelPath, now) || now != sentinel) { Violation("LINK_FOLLOWED", "the repair wrote through a link into a file outside the directory"); return 7; }
            }
            return 0;
        }
        if (r.error.find("Decompiler unavailable") == std::string::npos) { Violation("ERROR_TEXT", "refused with: " + r.error); return 8; }
        return 5;
    };
    CaseScope scope("specbundle", index, desc, nullptr, dangerous ? 128 * kMiB : 512 * kMiB, dangerous ? 96 * kMiB : 256 * kMiB);
    if (dangerous)
    {
        // Run apart: it may block, or read for ever.
        ChildResult c = RunChild([&] {
            gTrack = true; gLiveCap = 128 * kMiB; gSingleCap = 96 * kMiB; gCapHit = false;
            try { sfe::decomp::SpecBundleResult r = sfe::decomp::MaterialiseSh2SpecBundle(cfg); const int v = verdict(r); return gCapHit ? 9 : v; }
            catch (const std::bad_alloc&) { return 9; }
        }, 8);
        if (c.timedOut) Violation("HANG", "materialising over: " + desc + " did not return in 8 s");
        else if (c.code == 9) Violation("UNBOUNDED_ALLOC", "materialising over: " + desc + " read without bound");
        else if (!c.exited) Violation("CRASH", "materialising over: " + desc + " ended with signal " + std::to_string(c.signal));
        else if (c.code == 6 || c.code == 7 || c.code == 8) Violation("CHILD_VIOLATION", "see the child's line above");
        gAccepted["specbundle"]++;
        return;
    }
    sfe::decomp::SpecBundleResult r;
    Guarded::Run("MaterialiseSh2SpecBundle", [&] { r = sfe::decomp::MaterialiseSh2SpecBundle(cfg); });
    gAccepted["specbundle"] += r.ok;
    verdict(r);
    // Nothing left of our own staging.
    if (DIR* d = ::opendir((cfg + "/decompiler").c_str()))
    {
        while (struct dirent* e = ::readdir(d))
            if (std::strstr(e->d_name, ".tmp-") && std::strstr(e->d_name, std::to_string(::getpid()).c_str()))
                Violation("STAGING_LEFT", std::string("the staging directory ") + e->d_name + " was left behind");
        ::closedir(d);
    }
}

// ---------------------------------------------------------------------------------------------
// Links and reopened files: the BIOS path.
// ---------------------------------------------------------------------------------------------
void RunLinkCases()
{
    const std::string root = gScratch + "/links";
    RemoveTree(root);
    MakeDir(root);
    int n = 0;
    Bytes good;
    Pattern(good, 0x80000, 9);
    const std::string real = root + "/real.bin";
    WriteBytes(real, good);

    auto expectLoads = [&](const std::string& path, const char* what) {
        CaseScope scope("links", n++, what, nullptr, 8 * kMiB, 4 * kMiB);
        BiosLoadResult r = sfe::decomp::LoadBiosImageFile(path);
        if (!r.image) Violation("LINK_REFUSED", std::string(what) + ": " + r.error);
        else if (r.image->bytes != good) Violation("BYTES_DIFFER", std::string(what) + ": not the target's bytes");
    };
    auto expectRefused = [&](const std::string& path, const char* what) {
        CaseScope scope("links", n++, what, nullptr, 8 * kMiB, 4 * kMiB);
        Guarded::Run(what, [&] {
            BiosLoadResult r = sfe::decomp::LoadBiosImageFile(path);
            if (r.image) Violation("ACCEPTED", std::string(what) + ": accepted (" + std::to_string(r.image->bytes.size()) + " bytes)");
            else if (r.error.empty()) Violation("NO_ERROR", std::string(what) + ": refused without an error");
        });
    };
    ::symlink(real.c_str(), (root + "/link.bin").c_str());
    ::symlink((root + "/link.bin").c_str(), (root + "/link2.bin").c_str());
    expectLoads(root + "/link.bin", "a link to the file");
    expectLoads(root + "/link2.bin", "a link to a link");
    ::symlink("/nonexistent/bios", (root + "/dangling.bin").c_str());
    expectRefused(root + "/dangling.bin", "a dangling link");
    ::symlink("loop.bin", (root + "/loop.bin").c_str());
    expectRefused(root + "/loop.bin", "a link to itself");
    MakeDir(root + "/dir.bin");
    expectRefused(root + "/dir.bin", "a directory");
    ::symlink(root.c_str(), (root + "/dirlink.bin").c_str());
    expectRefused(root + "/dirlink.bin", "a link to a directory");
    expectRefused(real + "/", "a file path with a trailing slash");
    expectRefused(root + "/missing.bin", "a missing file");
    expectRefused("", "an empty path");
    expectRefused("/dev/null", "/dev/null (empty)");
    WriteBytes(root + "/empty.bin", nullptr, 0);
    expectRefused(root + "/empty.bin", "an empty file");
    Bytes over = good;
    over.push_back(0);
    WriteBytes(root + "/over.bin", over);
    expectRefused(root + "/over.bin", "512 KiB and one byte");

    // These may block or never end: each in a child, with the allocation bound set there.
    struct Hostile { const char* what; std::string path; bool fifo; };
    ::mkfifo((root + "/fifo.bin").c_str(), 0600);
    const Hostile hostile[] = { { "/dev/zero", "/dev/zero", false }, { "a link to /dev/zero", "", false }, { "a FIFO nobody writes", root + "/fifo.bin", true } };
    ::symlink("/dev/zero", (root + "/zero.bin").c_str());
    int h = 0;
    for (const Hostile& hs : hostile)
    {
        const std::string path = hs.path.empty() ? root + "/zero.bin" : hs.path;
        CaseScope scope("links", n++, hs.what, nullptr, ~size_t(0), ~size_t(0));
        gTrack = false;
        ChildResult c = RunChild([&] {
            gTrack = true; gLiveCap = 16 * kMiB; gSingleCap = 8 * kMiB; gCapHit = false;
            try
            {
                BiosLoadResult r = sfe::decomp::LoadBiosImageFile(path);
                if (gCapHit) return 9;
                if (r.image) return 6;
                return r.error.empty() ? 7 : 0;
            }
            catch (const std::bad_alloc&) { return 9; }
        }, 6);
        gTrack = true;
        ++h;
        if (c.timedOut) Violation("HANG", std::string(hs.what) + ": no answer in 6 s");
        else if (c.exited && c.code == 9) Violation("UNBOUNDED_ALLOC", std::string(hs.what) + ": read past 16 MiB looking for the end of the file");
        else if (!c.exited) Violation("CRASH", std::string(hs.what) + ": signal " + std::to_string(c.signal));
        else if (c.code == 6) Violation("ACCEPTED", std::string(hs.what) + ": accepted");
        else if (c.code == 7) Violation("NO_ERROR", std::string(hs.what) + ": refused without an error");
    }

    // A file read again: the hash follows the bytes, and a file that went bad is refused (the caller then decides what to keep).
    {
        CaseScope scope("links", n++, "reopened", nullptr, 8 * kMiB, 4 * kMiB);
        const std::string p = root + "/again.bin";
        WriteBytes(p, good);
        BiosLoadResult a = sfe::decomp::LoadBiosImageFile(p);
        WriteBytes(p, good);
        BiosLoadResult same = sfe::decomp::LoadBiosImageFile(p);
        Bytes other = good;
        other[1000] ^= 1;
        WriteBytes(p, other);
        BiosLoadResult diff = sfe::decomp::LoadBiosImageFile(p);
        if (!a.image || !same.image || !diff.image) Violation("REOPEN_REFUSED", "a file rewritten was refused");
        else
        {
            if (a.image->contentHash != same.image->contentHash) Violation("HASH_UNSTABLE", "the same bytes written again hash differently");
            if (a.image->contentHash == diff.image->contentHash) Violation("HASH_BLIND", "one flipped bit left the hash as it was");
        }
        ::unlink(p.c_str());
        BiosLoadResult gone = sfe::decomp::LoadBiosImageFile(p);
        if (gone.image || gone.error.empty()) Violation("REOPEN_GONE", "a deleted file was not refused with an error");
    }

    // Files the application keeps through links: what Save does to a linked settings.ini, bounds file and names file.
    {
        CaseScope scope("links", n++, "saved through a link", nullptr, 64 * kMiB, 32 * kMiB);
        const std::string target = root + "/dotfiles_settings.ini";
        WriteText(target, "[data]\ndir = /old\n");
        const std::string cfg = gScratch + "/cfg/SaturnExplorer";
        ::unlink((cfg + "/settings.ini").c_str());
        ::symlink(target.c_str(), (cfg + "/settings.ini").c_str());
        sfe::Settings s;
        s.Load();
        s.Set("data", "dir", "/new");
        std::string err;
        const bool saved = s.Save(&err);
        Bytes t;
        ReadBytes(target, t);
        const bool stillLink = sfe::IsLink(cfg + "/settings.ini");
        std::printf("note: settings.ini saved through a link: saved=%d, link kept=%d, target holds the new value=%d\n", saved, stillLink,
                    std::string(t.begin(), t.end()).find("/new") != std::string::npos);
        ::unlink((cfg + "/settings.ini").c_str());
    }
}

// ---------------------------------------------------------------------------------------------
// Two instances on one file.
// ---------------------------------------------------------------------------------------------
int EnvInt(const char* name, int def)
{
    const char* v = std::getenv(name);
    return v && *v ? std::atoi(v) : def;
}

std::string Tag(int who, int n, int j) { return "w" + std::to_string(who) + "n" + std::to_string(n) + "j" + std::to_string(j); }

struct Barrier
{
    int fds[2];
    Barrier() { if (::pipe(fds) != 0) fds[0] = fds[1] = -1; }
    void Release(int n) { for (int i = 0; i < n; ++i) { char c = 1; if (::write(fds[1], &c, 1) != 1) {} } }
    void Wait() { char c; if (::read(fds[0], &c, 1) != 1) {} }
};

// Runs 'writers' + 'readers' processes together; each returns the number of torn reads / failures it saw.
struct Procs
{
    std::vector<pid_t> pids;
    void Spawn(const std::function<int()>& fn)
    {
        std::fflush(stdout);
        const pid_t pid = ::fork();
        if (pid == 0)
        {
            ::signal(SIGALRM, SIG_DFL);
            ::alarm(120);
            int rc = 3;
            try { rc = fn(); } catch (...) { rc = 4; }
            std::fflush(stdout);
            ::_exit(rc);
        }
        pids.push_back(pid);
    }
    // Sum of the exit codes (each process returns its violation count), 1000 for a crash.
    int Join()
    {
        int total = 0;
        for (pid_t p : pids)
        {
            int st = 0;
            ::waitpid(p, &st, 0);
            if (WIFEXITED(st)) total += WEXITSTATUS(st);
            else total += 1000;
        }
        pids.clear();
        return total;
    }
};

void RunConcurrentCases()
{
    const int procs = EnvInt("SE_LOADFUZZ_PROCS", 3);
    const int rounds = EnvInt("SE_LOADFUZZ_ROUNDS", 12);
    const std::string root = gScratch + "/concurrent";
    RemoveTree(root);
    MakeDir(root);
    MakeDir(root + "/names");
    const int kEntries = 2000;
    int n = 0;

    // --- settings.ini ---
    {
        CaseScope scope("concurrent.settings", n++, "writers and readers on settings.ini", nullptr, ~size_t(0), ~size_t(0), 1L << 40, 600);
        gTrack = false;
        {
            sfe::Settings s;
            s.Set("test", "who", "0");
            s.Set("test", "n", "0");
            for (int j = 0; j < kEntries; ++j) s.Set("test", "k" + std::to_string(j), Tag(0, 0, j));
            s.Save();
        }
        Barrier gate;
        Procs ps;
        for (int w = 0; w < procs; ++w)
            ps.Spawn([&, w] {
                gate.Wait();
                for (int i = 1; i <= rounds; ++i)
                {
                    sfe::Settings s;
                    s.Set("test", "who", std::to_string(w));
                    s.Set("test", "n", std::to_string(i));
                    for (int j = 0; j < kEntries; ++j) s.Set("test", "k" + std::to_string(j), Tag(w, i, j));
                    s.Save();
                }
                return 0;
            });
        for (int r = 0; r < 2; ++r)
            ps.Spawn([&] {
                gate.Wait();
                int torn = 0;
                for (int i = 0; i < rounds * 6; ++i)
                {
                    sfe::Settings s;
                    s.Load();
                    const int who = std::atoi(s.Get("test", "who", "-1").c_str()), nn = std::atoi(s.Get("test", "n", "-1").c_str());
                    if (who < 0 || nn < 0) { ++torn; continue; }
                    for (int j = 0; j < kEntries; ++j) if (s.Get("test", "k" + std::to_string(j)) != Tag(who, nn, j)) { ++torn; break; }
                }
                return torn > 100 ? 100 : torn;
            });
        gate.Release(procs + 2);
        const int torn = ps.Join();
        gTrack = true;
        if (torn) Violation("TORN_READ", std::to_string(torn) + " reads of settings.ini saw a file that is none of the writers' (or a writer died)");
    }

    // --- saturn_function_bounds.txt ---
    {
        CaseScope scope("concurrent.bounds", n++, "writers and readers on the bounds file", nullptr, ~size_t(0), ~size_t(0), 1L << 40, 600);
        gTrack = false;
        const std::string path = root + "/bounds.txt";
        { sfe::decomp::FunctionBounds fb; for (int j = 0; j < kEntries; ++j) fb.SetEntry(0x06000000u + 2 * j); fb.Save(path); }
        Barrier gate;
        Procs ps;
        for (int w = 0; w < procs; ++w)
            ps.Spawn([&, w] {
                gate.Wait();
                for (int i = 1; i <= rounds; ++i)
                {
                    sfe::decomp::FunctionBounds fb;
                    const uint32_t base = 0x06000000u + static_cast<uint32_t>((w * rounds + i) * 0x2000);
                    for (int j = 0; j < kEntries; ++j) fb.SetEntry(base + 2 * j);
                    fb.Save(path);
                }
                return 0;
            });
        for (int r = 0; r < 2; ++r)
            ps.Spawn([&] {
                gate.Wait();
                int torn = 0;
                for (int i = 0; i < rounds * 6; ++i)
                {
                    sfe::decomp::FunctionBounds fb;
                    if (!fb.Load(path)) { ++torn; continue; }
                    if (fb.Entries().size() != static_cast<size_t>(kEntries)) { ++torn; continue; }
                    const uint32_t first = *fb.Entries().begin();
                    if (*fb.Entries().rbegin() != first + 2 * (kEntries - 1)) ++torn;
                }
                return torn > 100 ? 100 : torn;
            });
        gate.Release(procs + 2);
        const int torn = ps.Join();
        gTrack = true;
        if (torn) Violation("TORN_READ", std::to_string(torn) + " reads of the bounds file saw a file that is none of the writers'");
    }

    // --- saturn_functions.txt ---
    {
        CaseScope scope("concurrent.names", n++, "writers and readers on saturn_functions.txt", nullptr, ~size_t(0), ~size_t(0), 1L << 40, 600);
        gTrack = false;
        const std::string dir = root + "/names";
        {
            InDir in(dir);
            sfe::FunctionNames fn;
            for (int j = 0; j < kEntries; ++j) fn.Rename(0x06000000u + 2 * j, Tag(0, 0, j));
            fn.Save();
        }
        Barrier gate;
        Procs ps;
        for (int w = 0; w < procs; ++w)
            ps.Spawn([&, w] {
                InDir in(dir);
                gate.Wait();
                for (int i = 1; i <= rounds; ++i)
                {
                    sfe::FunctionNames fn;
                    for (int j = 0; j < kEntries; ++j) fn.Rename(0x06000000u + 2 * j, Tag(w, i, j));
                    fn.Save();
                }
                return 0;
            });
        for (int r = 0; r < 2; ++r)
            ps.Spawn([&] {
                InDir in(dir);
                gate.Wait();
                int torn = 0;
                for (int i = 0; i < rounds * 6; ++i)
                {
                    sfe::FunctionNames fn;
                    fn.Load();
                    const std::map<uint32_t, std::string>& all = fn.All();
                    if (all.size() != static_cast<size_t>(kEntries)) { ++torn; continue; }
                    // all one writer's: the same w and n in every name
                    const std::string first = all.begin()->second;
                    const size_t jpos = first.find('j');
                    const std::string prefix = first.substr(0, jpos);
                    int j = 0;
                    for (const auto& kv : all) if (kv.second != prefix + "j" + std::to_string(j++)) { ++torn; break; }
                }
                return torn > 100 ? 100 : torn;
            });
        gate.Release(procs + 2);
        const int torn = ps.Join();
        gTrack = true;
        if (torn) Violation("TORN_READ", std::to_string(torn) + " reads of saturn_functions.txt saw a file that is none of the writers' (the file is truncated, then written, in place)");
    }

    // --- assembly_comments.txt ---
    {
        CaseScope scope("concurrent.comments", n++, "writers and readers on assembly_comments.txt", nullptr, ~size_t(0), ~size_t(0), 1L << 40, 600);
        gTrack = false;
        const std::string dir = root + "/names";
        ::unlink((dir + "/assembly_comments.txt").c_str());
        Barrier gate;
        Procs ps;
        {
            InDir in(dir);
            // a first complete file, so a reader never starts on a missing one
            std::string t;
            for (int j = 0; j < kEntries; ++j) { char b[96]; std::snprintf(b, sizeof b, "%x %s\n", 0x06000000u + 2 * j, Tag(0, 0, j).c_str()); t += b; }
            WriteText(dir + "/assembly_comments.txt", t);
        }
        // The panel has no setter, so the writers save the list they loaded, over and over: every version of the file is the
        // same whole list, and a reader that finds any other length has read a file in the middle of being written.
        for (int w = 0; w < procs; ++w)
            ps.Spawn([&] {
                InDir in(dir);
                gate.Wait();
                sfe::AssemblyPanel panel;
                panel.LoadComments();
                for (int i = 1; i <= rounds * 4; ++i) panel.SaveComments();
                return 0;
            });
        for (int r = 0; r < 2; ++r)
            ps.Spawn([&] {
                InDir in(dir);
                gate.Wait();
                int torn = 0;
                for (int i = 0; i < rounds * 6; ++i)
                {
                    sfe::AssemblyPanel panel;
                    panel.LoadComments();
                    if (panel.UserComments().size() != static_cast<size_t>(kEntries)) ++torn;
                }
                return torn > 100 ? 100 : torn;
            });
        gate.Release(procs + 2);
        const int torn = ps.Join();
        gTrack = true;
        if (torn) Violation("TORN_READ", std::to_string(torn) + " reads of assembly_comments.txt saw a file that is none of the writers' (it is truncated, then written, in place)");
    }

    // --- the BIOS file replaced under readers (rename, as an editor or a copy tool that stages does) ---
    {
        CaseScope scope("concurrent.bios", n++, "a BIOS file replaced while it is read", nullptr, ~size_t(0), ~size_t(0), 1L << 40, 600);
        gTrack = false;
        const std::string path = root + "/bios.bin";
        Bytes a, b;
        Pattern(a, 0x80000, 1);
        Pattern(b, 0x80000, 2);
        WriteBytes(path, a);
        Barrier gate;
        Procs ps;
        ps.Spawn([&] {
            gate.Wait();
            std::string err;
            for (int i = 0; i < rounds * 3; ++i)
            {
                const Bytes& v = (i & 1) ? b : a;
                sfe::WriteFileAtomically(path, v.data(), v.size(), err);
            }
            return 0;
        });
        for (int r = 0; r < 2; ++r)
            ps.Spawn([&] {
                gate.Wait();
                int bad = 0;
                for (int i = 0; i < rounds * 2; ++i)
                {
                    BiosLoadResult res = sfe::decomp::LoadBiosImageFile(path);
                    if (!res.image || (res.image->bytes != a && res.image->bytes != b)) ++bad;
                }
                return bad > 100 ? 100 : bad;
            });
        gate.Release(3);
        const int bad = ps.Join();
        gTrack = true;
        if (bad) Violation("TORN_READ", std::to_string(bad) + " loads of a replaced BIOS file saw neither version whole");
    }

    // --- the spec bundle: instances starting together on a config directory ---
    for (int variant = 0; variant < 3; ++variant)
    {
        CaseScope scope("concurrent.specbundle", n++, variant == 0 ? "empty directory" : variant == 1 ? "a damaged published copy" : "a stale copy and a staging directory", nullptr, ~size_t(0), ~size_t(0), 1L << 40, 600);
        gTrack = false;
        const std::string cfg = root + "/spec" + std::to_string(variant);
        RemoveTree(cfg);
        if (variant > 0)
        {
            sfe::decomp::MaterialiseSh2SpecBundle(cfg);
            const std::string dir = PublishedDir(cfg);
            Bytes b;
            const std::string f = dir + "/" + sfe::decomp::Sh2SpecFiles()[1].name;
            ReadBytes(f, b);
            if (!b.empty()) b[b.size() / 2] ^= 0x40;
            WriteBytes(f, b);
            if (variant == 2) { MakeDir(dir + ".tmp-7"); WriteText(dir + ".tmp-7/x", "x"); }
        }
        Barrier gate;
        Procs ps;
        const int count = procs + 1;
        for (int w = 0; w < count; ++w)
            ps.Spawn([&] {
                gate.Wait();
                sfe::decomp::SpecBundleResult r = sfe::decomp::MaterialiseSh2SpecBundle(cfg);
                std::string why;
                if (!r.ok) return 1;
                return DirIsExact(r.dir, why) ? 0 : 1;
            });
        gate.Release(count);
        const int bad = ps.Join();
        gTrack = true;
        if (bad) Violation("SPEC_INSTANCES", std::to_string(bad) + " of " + std::to_string(count) + " instances starting together got no verified language directory (" + (variant == 0 ? "empty" : variant == 1 ? "damaged" : "stale") + ")");
    }
}

// ---------------------------------------------------------------------------------------------
// The run.
// ---------------------------------------------------------------------------------------------
struct Module
{
    const char* name;
    int defaultCases;
    std::function<void(uint64_t)> run;
};

}  // namespace

int main(int argc, char** argv)
{
    InstallHandlers();
    gSeed = static_cast<uint64_t>(EnvInt("SE_LOADFUZZ_SEED", 9));
    const int perModule = EnvInt("SE_LOADFUZZ_CASES", 160);
    const uint64_t from = static_cast<uint64_t>(EnvInt("SE_LOADFUZZ_FROM", 0));
    const char* only = std::getenv("SE_LOADFUZZ_ONLY");
    const char* oneCase = std::getenv("SE_LOADFUZZ_CASE");

    std::string root = argc > 1 ? argv[1] : "";
    if (root.empty())
    {
        const char* t = std::getenv("TMPDIR");
        std::string tmpl = std::string(t && *t ? t : "/tmp") + "/se_loaderfuzz_XXXXXX";
        std::vector<char> buf(tmpl.begin(), tmpl.end());
        buf.push_back(0);
        if (!::mkdtemp(buf.data())) { std::printf("no scratch directory\n"); return 2; }
        root = buf.data();
    }
    else
    {
        RemoveTree(root + "/repro");
        MakeDir(root);
    }
    gScratch = root;
    for (const char* d : { "savestate", "bios", "bounds", "names", "spec", "cfg", "cfg/SaturnExplorer", "concurrent", "links", "oversize" }) MakeDir(gScratch + "/" + d);
    ::setenv("XDG_CONFIG_HOME", (gScratch + "/cfg").c_str(), 1);
    ::setenv("APPDATA", (gScratch + "/cfg").c_str(), 1);

    BuildSavestateSamples();

    const std::vector<Module> modules = {
        { "savestate", 150, [](uint64_t i) { RunSavestateCase(i, false); } },
        { "regiondir", 30, RunRegionDirCase },
        { "bios", 200, RunBiosCase },
        { "settings", 150, RunSettingsCase },
        { "names", 250, RunNamesCase },
        { "bounds", 250, RunBoundsCase },
        { "comments", 150, RunCommentsCase },
        { "ranges", 800, RunRangesCase },
        { "specbundle", 40, RunSpecBundleCase },
    };
    const auto t0 = std::chrono::steady_clock::now();
    for (const Module& m : modules)
    {
        if (only && std::strcmp(only, m.name) != 0) continue;
        const uint64_t count = static_cast<uint64_t>(perModule) * m.defaultCases / 160;
        if (oneCase)
        {
            m.run(static_cast<uint64_t>(std::atoll(oneCase)));
            continue;
        }
        for (uint64_t i = from; i < from + count; ++i) m.run(i);
        std::printf("%-12s %6llu cases (%llu..%llu), %zu accepted\n", m.name, static_cast<unsigned long long>(count),
                    static_cast<unsigned long long>(from), static_cast<unsigned long long>(from + count - 1), gAccepted[m.name]);
        std::fflush(stdout);
    }
    auto fixed = [&](const char* name) { return !only || std::strcmp(only, name) == 0; };
    // The directed cases (pins, oversize files, links, concurrent instances) run once, with the first piece of a long run.
    if (!oneCase && from == 0 && !std::getenv("SE_LOADFUZZ_NOFIXED"))
    {
        if (fixed("savestate")) RunSavestatePins();
        if (fixed("settings")) RunSettingsValueCases();
        if (fixed("oversize")) RunOversizeCases();
        if (fixed("links")) RunLinkCases();
        if (fixed("concurrent")) RunConcurrentCases();
    }
    const long secs = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - t0).count();

    // Report.
    int unexpected = 0;
    std::printf("\n%-34s %8s  %s\n", "violation class", "count", "ledger row / first case");
    std::set<std::string> seen;
    for (const auto& kv : gFindings)
    {
        seen.insert(kv.first);
        const auto k = Known().find(kv.first);
        std::printf("%-34s %8zu  %s\n", kv.first.c_str(), kv.second.count, k == Known().end() ? "NEW" : k->second.c_str());
        if (k == Known().end()) ++unexpected;
    }
    for (const auto& k : Known())
        if (!seen.count(k.first)) std::printf("%-34s %8d  %s listed as open, not seen: remove it if the fix has landed\n", k.first.c_str(), 0, k.second.c_str());
    size_t total = 0;
    for (const auto& kv : gCases) total += kv.second;
    std::printf("\nLoaderFuzzTests: %zu cases in %ld s, %d violation classes not in the ledger (seed %llu)\n", total, secs, unexpected, static_cast<unsigned long long>(gSeed));
    return unexpected ? 1 : 0;
}
