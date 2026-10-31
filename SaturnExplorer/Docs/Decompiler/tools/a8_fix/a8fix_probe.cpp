// Area 8, fixer round (A8-15, A8-17 to A8-23): the instruments that are not in the trust test's default run. Not part of the repository
// build: build_probe.sh compiles it with the flags of the trust test's own target.
//
// The round 2 instruments (tools/a8_round2) became the trust test itself: the Decompile operation (`t-contain`), the status-row check, the
// stop-and-interleave sequence generator (`MakeProgram(seed, mix, concat)`), the width runs, the real allocation-failure sweep and the
// directed pins for A8-17 to A8-21. What is left here is what is too long for the default run, or is only a way to look:
//
//   a8fix_probe <scratch dir> long <mix> <first seed> <programs> [concat]   `concat` generated programs end to end (300 to 450 operations),
//                                                                           at the width SE_TRUST_WIDTH names (default 1250 px)
//   a8fix_probe <scratch dir> follow <first seed> <ops>                      the Follow PC rig, seeds not used before
//   a8fix_probe <scratch dir> oom [stride]                                   the allocation-failure sweep at any stride (1 tries every allocation)
//   a8fix_probe <scratch dir> directed                                       D1 to D3 (Stop and Decompile, Follow PC and the entry box, Stop on an answer
//                                                                           already waiting) and D5 (a rename the engine could not apply)
//   a8fix_probe <scratch dir> script "<script>" <world>                      one script (area 1's words plus t-contain <fn>), every step printed
#define main trust_original_main
#include "DecompilerTrustTests.cpp"
#undef main

namespace
{

int RunLong(int mix, uint64_t first, int programs, int concat)
{
    Oracle oracle;
    int failed = 0;
    const auto t0 = std::chrono::steady_clock::now();
    const uint64_t before = gT.steps;
    for (int i = 0; i < programs; ++i)
    {
        const uint64_t seed = first + static_cast<uint64_t>(i);
        const Program p = MakeProgram(seed, mix, concat);
        if (std::getenv("SE_PROBE_VERBOSE"))
        {
            const bool ok = RunTrust(oracle, p, seed, true);
            std::printf("long mix %d seed %llu: %s\n", mix, static_cast<unsigned long long>(seed), ok ? "ok" : "FAILED");
            return ok ? 0 : 1;
        }
        if (!RunTrust(oracle, p, seed, false))
        {
            ++failed;
            std::printf("FAILED: long mix %d seed %llu concat %d (%zu operations)\n", mix, static_cast<unsigned long long>(seed), concat, p.ops.size());
        }
    }
    std::printf("status line checks: %llu\n", static_cast<unsigned long long>(gStatusChecks));
    std::printf("long mix %d: seeds %llu..%llu, concat %d, %.0f px: %d programs, %d failed, %llu operations, %.1f s\n", mix, static_cast<unsigned long long>(first),
                static_cast<unsigned long long>(first + programs - 1), concat, gRigWinW, programs, failed, static_cast<unsigned long long>(gT.steps - before),
                dts::MsSince(t0) / 1000.0);
    return failed;
}

int gDirectedBad = 0;

void Report(const char* what, bool ok, const std::string& detail = "")
{
    std::printf("probe %-100s %s\n", what, ok ? "ok" : "MISMATCH");
    if (!ok)
    {
        ++gDirectedBad;
        if (!detail.empty()) std::printf("%s\n", detail.c_str());
    }
}

const std::string kDoubt = "guessed from the previous rts; no symbol, call or user entry confirms it";

// D1. Stop, then "Decompile" on the same function: it asks again (A8-18), so the Stop is over and the output is replaced.
void D1()
{
    const uint32_t cEntry = Defs()[FnC].entry;
    MiniRig m;
    m.panel.ShowFunctionContaining(0, cEntry + 4);
    m.Idle(3);
    CHECK(m.Settle());
    m.gate.Close();
    EditMini(m);
    m.Idle(3);
    CHECK(WaitHeldOrBusy(m));
    m.Idle(2);
    CHECK(ClickMini(m, "stop"));
    m.gate.Open();
    CHECK(m.Settle());
    trust::TrueState t;
    t.inputsDiffer = true;
    t.stopped = true;
    t.entryDoubt = kDoubt;
    std::string err = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, true);
    Report("D1a after Stop: the Stopped badge and the grey note", err.empty(), err);
    const uint64_t sub = m.panel.Submissions();
    m.panel.ShowFunctionContaining(0, cEntry + 4);
    m.Idle(4);
    CHECK(m.Settle());
    t = trust::TrueState();
    t.entryDoubt = kDoubt;
    err = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, true);
    Report("D1b Decompile on the same function after a Stop asks again, and the output is current", m.panel.Submissions() != sub && !m.panel.ShownStale() && err.empty(), err);
}

// D2. Follow PC, a Stop, and what the tooltip says to do: Refresh asks for the function followed; Enter in the box would ask for the one shown.
void D2()
{
    const uint32_t cEntry = Defs()[FnC].entry, aEntry = Defs()[FnA].entry;
    MiniRig m;
    m.kind = DecompViewKind::LivePaused;
    m.regs[0].pc = cEntry + 4;
    m.haveRegs[0] = true;
    m.Idle(3);
    CHECK(m.Settle());
    m.gate.Close();
    m.regs[0].pc = aEntry + 4;
    ++m.frame;
    ++m.serial;
    m.Idle(3);
    CHECK(WaitHeldOrBusy(m));
    m.Idle(2);
    CHECK(ClickMini(m, "stop"));
    m.gate.Open();
    CHECK(m.Settle());
    std::printf("      Follow PC %d, PC in A (%08X), shown C (%08X), entry box '%s', reason %d, tooltip: %s\n", m.panel.FollowPc(), aEntry + 4, cEntry,
                m.panel.EntryText(), static_cast<int>(m.panel.GetStaleReason()), m.panel.StaleTooltip().c_str());
    const std::string tip = m.panel.StaleTooltip();
    Report("D2a the Stopped tooltip under Follow PC names Refresh, not the entry box", tip.find("Enter") == std::string::npos && tip.find("Refresh") != std::string::npos);
    CHECK(ClickMini(m, "refresh"));
    m.Idle(3);
    CHECK(m.Settle());
    const DecompileResult* s = m.panel.Shown();
    Report("D2b Refresh shows the function followed and keeps Follow PC", s && s->requestedEntry == aEntry && m.panel.FollowPc() && !m.panel.ShownStale());
}

// D3. Stop on a request whose answer is already waiting in the runner (the cache-hit and memo paths), and Stop pressed twice.
void D3()
{
    MiniRig m;
    m.panel.ShowFunctionContaining(0, Defs()[FnC].entry + 4);
    m.Idle(3);
    CHECK(m.Settle());
    for (int round = 0; round < 6; ++round)
    {
        m.gate.Close();
        CHECK(ClickMini(m, "refresh"));
        WaitHeldOrBusy(m);
        m.Idle(2);
        ClickMini(m, "stop");
        ClickMini(m, "stop");
        m.gate.Open();
        CHECK(m.Settle());
        trust::TrueState t;
        t.entryDoubt = kDoubt;
        std::string err = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, true);
        if (!err.empty())
        {
            t.stopped = true;   // the answer may have been stopped for good: the grey note is the panel's record of it
            err = Verify(m.panel, t, [&](ImVec2 p) { m.Frame(p, false); }, true);
        }
        Report("D3 Refresh on current output, Stop twice", err.empty(), err);
    }
}

// D5. A rename the engine could not apply (two functions given one name): the engine takes it, with no note.
void D5()
{
    const uint32_t cEntry = Defs()[FnC].entry, aEntry = Defs()[FnA].entry;
    MiniRig m;
    m.names[aEntry] = "alpha";
    m.names[cEntry] = "alpha";
    ++m.serial;
    m.panel.ShowFunctionContaining(0, cEntry);
    m.Idle(3);
    CHECK(m.Settle());
    const DecompileResult* s = m.panel.Shown();
    std::printf("      two functions named alpha, C asked: name on screen '%s', %zu engine notes\n", s ? s->name.c_str() : "-", s ? s->warnings.size() : 0);
    Report("D5 a rename the engine could not apply is amber or classified", s && (s->warnings.empty() || !m.panel.Uncertainties().empty()));
}

// D12. Where the toolbar's controls end at the default layout's widths (not a trust indicator, but Stop is the control "decompiling..." offers).
void D12()
{
    for (float w : { 537.0f, 672.0f, 806.0f, 1000.0f })
    {
        gMiniWinW = w;
        MiniRig m;
        m.kind = DecompViewKind::LiveRunning;
        m.regs[0].pc = Defs()[FnC].entry + 4;
        m.haveRegs[0] = true;
        m.Idle(3);
        CHECK(m.Settle());
        m.gate.Close();
        ++m.frame;
        ++m.serial;
        ClickMini(m, "refresh");
        WaitHeldOrBusy(m);
        m.Idle(3);
        std::printf("      panel %4.0f px:", w);
        for (const char* name : { "cpu", "Open Registers", "follow", "entry", "refresh", "stop" })
        {
            ImVec2 a, b;
            if (m.panel.ItemRect(name, a, b)) std::printf("  %s %.0f..%.0f%s", name, a.x, b.x, b.x > w ? " (OUT)" : "");
            else std::printf("  %s -", name);
        }
        std::printf("\n");
        m.gate.Open();
        CHECK(m.Settle());
    }
    gMiniWinW = 1250.0f;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        std::printf("usage: %s <scratch dir> long|follow|directed|oom|script ...\n", argv[0]);
        return 2;
    }
    dts::PrepareProcess();
    const std::string scratch = argv[1];
    dts::RemoveTree(scratch);
    CHECK(sfe::MakeDirectory(scratch));
    gConfig = dts::Join(scratch, "config");
    CHECK(sfe::MakeDirectory(gConfig));
    gHover = std::getenv("SE_TRUST_NOHOVER") == nullptr;
    if (const char* w = std::getenv("SE_TRUST_WIDTH"))
        if (*w) gRigWinW = static_cast<float>(std::atof(w));
    const std::string mode = argv[2];
    int bad = 0;
    try
    {
        if (mode == "long")
        {
            const int mix = argc > 3 ? std::atoi(argv[3]) : 7;
            const uint64_t first = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 1;
            const int programs = argc > 5 ? std::atoi(argv[5]) : 10;
            const int concat = argc > 6 ? std::atoi(argv[6]) : 4;
            bad = RunLong(mix, first, programs, concat);
        }
        else if (mode == "follow")
        {
            const uint64_t first = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 1;
            const uint64_t want = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 5000;
            uint64_t ops = 0;
            int seeds = 0;
            for (uint64_t seed = first; ops < want; ++seed, ++seeds)
                if (!RunFollow(seed, false, ops))
                {
                    ++bad;
                    std::printf("FAILED: follow seed %llu\n", static_cast<unsigned long long>(seed));
                }
            std::printf("follow: seeds from %llu, %d sequences, %llu operations, %d failed\n", static_cast<unsigned long long>(first), seeds,
                        static_cast<unsigned long long>(ops), bad);
        }
        else if (mode == "directed")
        {
            D1();
            D2();
            D3();
            D5();
            D12();
            bad = gDirectedBad;
        }
        else if (mode == "oom")
        {
#ifndef _WIN32
            int cells = 0, failedCells = 0;
            bad = RunOomSweep(argc > 3 ? std::atol(argv[3]) : 7, cells, failedCells);
#endif
        }
        else if (mode == "script")
        {
            Program p;
            std::string error;
            if (argc < 5 || !ParseScript(argv[3], argv[4], 0, p, error))
            {
                std::printf("cannot parse: %s\n", error.c_str());
                return 2;
            }
            Oracle oracle;
            bad = RunTrust(oracle, p, 0, true) ? 0 : 1;
            std::printf("script: %s\n", bad ? "FAILED" : "ok");
        }
    }
    catch (std::exception& e)
    {
        std::printf("FATAL: %s\n", e.what());
        return 3;
    }
    dts::RemoveTree(scratch);
    return bad ? 1 : 0;
}
