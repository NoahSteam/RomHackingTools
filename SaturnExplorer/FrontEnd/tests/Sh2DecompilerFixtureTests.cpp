// Real game code: the Yabause savestate the feasibility spike used (SPIKE_RESULTS.md fixture 2),
// opened through SaturnExplorer's own savestate driver and captured into a MemorySnapshot through
// an IMemoryBackend, exactly the bytes a panel would hand the decompiler.
//
//   2a  the function the master SH-2 is paused in, found from its PC by the rts scan: a busy-wait
//       whose mov.w literal (7000) folds
//   2b  the function containing the master's return address, 0x06031598: 13 literal-pool loads, its
//       callees, and a SLEIGH listing equal to the checked-in, Capstone-verified one
//       (Docs/Decompiler/spike/real_06031598_listing.txt). The C text is asserted on structure,
//       not byte for byte: golden output is pinned to the Ghidra version and reviewed on a bump.
//
// Skips itself (and passes, saying so) when the game data is not in the checkout, like
// YssFixtureTests.
//
// usage: SaturnExplorerSh2DecompilerFixtureTests <scratch dir>
#include "DecompilerTestSupport.h"

#include "Debug/MemoryBackend.h"
#include "Decompiler/Sh2Decompiler.h"
#include "Decompiler/Sh2FunctionFinder.h"
#include "Disc/PathUtil.h"
#include "SavestateDriver.h"

#include <set>
#include <sstream>

using namespace sfe::decomp;
using dts::MsSince;

namespace {

const char* kState = SE_YSS_FIXTURE_DIR "/GS-9169_007.yss";
const char* kListing = SE_DECOMPILER_SPIKE_DIR "/real_06031598_listing.txt";

// IMemoryBackend over a savestate's data source: how ContextBackend would serve work RAM.
class SourceBackend : public sfe::IMemoryBackend
{
public:
    explicit SourceBackend(const se_data_source& ds) : mDs(ds) {}
    bool Connected() const override { return true; }
    std::vector<sfe::MemoryReadResult> ReadMemoryBatch(const std::vector<sfe::MemoryReadRequest>& requests) override
    {
        std::vector<sfe::MemoryReadResult> out;
        for (const auto& q : requests)
        {
            sfe::MemoryReadResult r;
            r.bytes.resize(q.size);
            r.success = mDs.read_main_ram && mDs.read_main_ram(mDs.user, q.address, r.bytes.data(), q.size) == q.size;
            if (!r.success) r.bytes.clear();
            out.push_back(std::move(r));
        }
        return out;
    }

private:
    se_data_source mDs;
};

bool Has(const std::string& text, const char* what) { return text.find(what) != std::string::npos; }

std::vector<std::string> ListingLines(const std::string& text)
{
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l))
    {
        while (!l.empty() && (l.back() == '\r' || l.back() == '\n')) l.pop_back();
        if (!l.empty() && l[0] != '<') lines.push_back(l);   // '<...>' marks where decoding stopped
    }
    return lines;
}

void Run(const std::string& config)
{
    se_data_source ds{};
    const auto t0 = std::chrono::steady_clock::now();
    CHECK(se_savestate_open(kState, &ds) == SE_OK);
    se_sh2_regs regs[2] = {};
    bool have[2] = { false, false };
    for (int cpu = 0; cpu < 2; ++cpu)
        have[cpu] = ds.read_sh2_regs && ds.read_sh2_regs(ds.user, cpu, &regs[cpu]) != 0;
    SourceBackend backend(ds);
    auto snap = MemorySnapshot::Capture(backend, regs, have, 1, 1, 0);
    if (ds.close) ds.close(ds.user);
    std::printf("open + capture: %.1f ms, master pc=%08X pr=%08X\n", MsSince(t0), regs[0].pc, regs[0].pr);
    CHECK(snap->regions.size() == 2);
    CHECK(have[0] && regs[0].pc == 0x0603055Au && regs[0].pr == 0x0603168Au);

    // 2a: from the PC to its function, then decompiled.
    FinderHints none;
    const EntryGuess pcGuess = FindFunctionContaining(*snap, regs[0].pc, none);
    std::printf("function containing pc: %08X (%s)\n", pcGuess.entry, EntrySourceName(pcGuess.source));
    CHECK(pcGuess.ok && pcGuess.entry == 0x06030548u && pcGuess.source == EntrySource::RtsScan);

    Sh2Decompiler d(config);
    DecompileRequest q;
    q.entry = 0x06030548u;
    DecompileResult a = d.Decompile(snap, q);
    std::printf("2a: %.1f ms\n%s", a.elapsedMs, a.Text().c_str());
    CHECK(a.status == DecompileStatus::Ok);
    CHECK(Has(a.Text(), "param_1 * 7000"));
    CHECK(a.literalPools == std::vector<uint32_t>{ 0x06030566u });
    CHECK(!a.truncated && !a.unsupported);

    // 2b.
    const EntryGuess prGuess = FindFunctionContaining(*snap, regs[0].pr, none);
    std::printf("function containing pr: %08X (%s)\n", prGuess.entry, EntrySourceName(prGuess.source));
    CHECK(prGuess.ok && prGuess.entry == 0x06031598u);
    q.entry = 0x06031598u;
    DecompileResult b = d.Decompile(snap, q);
    std::printf("2b: %.1f ms, %u body bytes, %u PC-relative loads from %zu literal words, %zu callees\n%s",
                b.elapsedMs, b.bodyBytes, b.literalLoads, b.literalPools.size(), b.callees.size(), b.Text().c_str());
    CHECK(b.status == DecompileStatus::Ok);
    CHECK(b.bodyBytes == 286);
    // The spike's "13 literal pools" counted loads; two of the eleven words are each read twice.
    CHECK(b.literalLoads == 13);
    CHECK(b.literalPools.size() == 11);
    const std::vector<uint32_t> callees = { 0x060198DEu, 0x06030548u, 0x060305B0u, 0x06030DB0u,
                                            0x06031168u, 0x06031340u, 0x060313D0u, 0x06031510u };
    CHECK(b.callees == callees);
    const std::string c = b.Text();
    CHECK(Has(c, "int4 func_0x06031598(int4 param_1,int4 param_2,int4 param_3,int4 param_4)"));
    CHECK(Has(c, "func_0x06031510(param_1,0)"));
    CHECK(Has(c, "func_0x06030548(1);"));
    // The literal-pool loop bound 20000 is recovered as a do-while. (Area 3 round 11 fix: the fail-closed A3-83 rule records
    // storesBelow for a store through a laundered pointer in a callee here -- a caller-saved argument register it then cannot
    // prove preserved -- so the loop counter and the post-call reads renumber and show extraout_r4; the bound is unchanged. The
    // pin no longer names the counter, only the recovered bound and the loop shape.)
    CHECK(Has(c, "< 20000);") && Has(c, "do {"));
    CHECK(!Has(c, "(*(code *)"));   // every literal-pool call became a named one
    // The callees' names navigate: each func_ token on a call line carries its target.
    std::set<uint32_t> tokenTargets;
    for (const Line& l : b.lines)
        for (const Token& t : l.tokens)
            if (t.kind == TokenKind::FuncName && t.target) tokenTargets.insert(t.target);
    CHECK(std::vector<uint32_t>(tokenTargets.begin(), tokenTargets.end()) == callees);

    // The SLEIGH listing equals the Capstone-verified one, instruction for instruction.
    std::string file;
    CHECK(sfe::ReadWholeFile(kListing, file));
    const std::vector<std::string> expected = ListingLines(file);
    const std::vector<std::string> got = ListingLines([&] {
        std::string s;
        for (const std::string& l : d.Disassemble(snap, 0x06031598u, static_cast<int>(expected.size()))) s += l + "\n";
        return s;
    }());
    std::printf("listing: %zu instructions, %zu expected\n", got.size(), expected.size());
    CHECK(expected.size() == 143);
    CHECK(got == expected);
    for (size_t i = 0; i < got.size() && i < expected.size(); ++i)
        if (got[i] != expected[i])
        {
            std::printf("  first difference at line %zu: got '%s', expected '%s'\n", i, got[i].c_str(), expected[i].c_str());
            break;
        }

    // The cache-through mirror decompiles to the same text.
    q.entry = 0x26031598u;
    CHECK(d.Decompile(snap, q).Text() == c);
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::printf("usage: %s <scratch dir>\n", argv[0]);
        return 2;
    }
    dts::PrepareProcess();
    const auto start = std::chrono::steady_clock::now();
    if (!sfe::PathEntryExists(kState))
    {
        std::printf("SKIPPED: %s is not in this checkout (game data absent)\n", kState);
        return 0;
    }
    const std::string scratch = argv[1];
    dts::RemoveTree(scratch);
    CHECK(sfe::MakeDirectory(scratch));
    const std::string config = dts::Join(scratch, "config");
    CHECK(sfe::MakeDirectory(config));
    try
    {
        RUN(Run(config));
    }
    catch (std::exception& e)
    {
        std::printf("FATAL: uncaught std::exception: %s\n", e.what());
        return 3;
    }
    dts::RemoveTree(scratch);
    return dts::Finish("Sh2DecompilerFixtureTests", start);
}
