// V3 benchmark client. See BenchClient.h.
//
// Timing: every row is stamped with the steady-clock time at which THIS process drained it from
// the driver (the driver stamps nothing), so a row's time is accurate to one drain period. The
// window edges are drain instants, and the analyzer reads the newest frame at each edge, so the
// quantisation does not bias the rate.
#include "BenchClient.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>

#if defined(__linux__)
#include <unistd.h>
#endif

#include "LiveDriver.h"
#include "SeLiveProtocol.h"
#include "SePerfCounters.h"
#include "saturnexplorer/SaturnExplorer.h"

namespace v3
{
namespace
{
using Clock = std::chrono::steady_clock;

struct Row
{
    const char* kind;        // "frame", "gap" (a lost range with no frame), "probe"
    uint64_t    sampleSeq;
    int64_t     frameNo;     // -1 when the record carried no identity
    uint32_t    timeline;
    uint32_t    mode;
    uint64_t    gapCount;
    uint64_t    evictCount;
    int64_t     arrivalNs;
    uint64_t    newestSeq;
};

struct CpuSample { int64_t atNs = -1; long long ticks = -1; };

int64_t NowNs(Clock::time_point t0)
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - t0).count();
}

void SleepMs(int64_t ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// Sleeps in short steps so a long wait is not a single uninterruptible call.
void SleepUntilNs(Clock::time_point t0, int64_t targetNs)
{
    for (;;)
    {
        const int64_t left = targetNs - NowNs(t0);
        if (left <= 0) return;
        SleepMs(left / 1000000 + 1 < 200 ? left / 1000000 + 1 : 200);
    }
}

bool OpenWithRetry(const BenchOptions& o, se_data_source* ds, Clock::time_point t0)
{
    const int64_t limit = NowNs(t0) + static_cast<int64_t>(o.connectTimeoutS) * 1000000000ll;
    for (;;)
    {
        if (se_live_open(o.endpoint.empty() ? nullptr : o.endpoint.c_str(), ds) == SE_OK) return true;
        if (NowNs(t0) > limit) return false;
        SleepMs(20);
    }
}

void Close(se_data_source& ds)
{
    if (ds.close) ds.close(ds.user);
    ds = se_data_source{};
}

uint32_t CapsOf(const se_data_source& ds)
{
    uint32_t caps = 0;
    se_live_poke_info(&ds, nullptr, nullptr, nullptr, nullptr, &caps);
    return caps;
}

bool WaitVersion(const se_data_source& ds, Clock::time_point t0, int seconds)
{
    const int64_t limit = NowNs(t0) + static_cast<int64_t>(seconds) * 1000000000ll;
    while (se_live_server_version(&ds) == 0)
    {
        if (NowNs(t0) > limit) return false;
        SleepMs(2);
    }
    return true;
}

Row ToRow(const se_live_perf_frame& f, int64_t arrivalNs)
{
    Row r;
    r.kind = f.gap_only ? "gap" : "frame";
    r.sampleSeq = f.sample_seq;
    r.frameNo = (f.valid & SE_PERF_VALID_IDENTITY) ? static_cast<int64_t>(f.frame_no) : -1;
    r.timeline = (f.valid & SE_PERF_VALID_IDENTITY) ? f.timeline_id : 0;
    r.mode = (f.valid & SE_PERF_VALID_MODE) ? f.mode : f.flags;
    r.gapCount = f.gap_count;
    r.evictCount = f.evict_count;
    r.arrivalNs = arrivalNs;
    r.newestSeq = f.newest_seq;
    return r;
}

CpuSample SampleCpu(long pid, Clock::time_point t0)
{
    CpuSample s;
    if (pid <= 0) return s;
    long hz = 0;
    s.ticks = ReadProcessCpuTicks(pid, &hz);
    s.atNs = NowNs(t0);
    return s;
}

struct Summary
{
    std::vector<std::pair<std::string, std::string>> kv;
    void Put(const std::string& k, const std::string& v) { kv.emplace_back(k, v); }
    void Put(const std::string& k, long long v) { kv.emplace_back(k, std::to_string(v)); }
    void Put(const std::string& k, unsigned long long v) { kv.emplace_back(k, std::to_string(v)); }
    void Put(const std::string& k, double v)
    {
        char b[64];
        std::snprintf(b, sizeof b, "%.6f", v);
        kv.emplace_back(k, b);
    }
    std::string Text() const
    {
        std::string s;
        for (const auto& p : kv) s += p.first + "=" + p.second + "\n";
        return s;
    }
};

bool WriteCsv(const std::string& path, const std::vector<Row>& rows)
{
    if (path.empty()) return true;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << "kind,sample_seq,frame_no,timeline_id,mode,gap_count,evict_count,arrival_ns,newest_seq\n";
    for (const Row& r : rows)
        f << r.kind << ',' << r.sampleSeq << ',' << r.frameNo << ',' << r.timeline << ',' << r.mode << ','
          << r.gapCount << ',' << r.evictCount << ',' << r.arrivalNs << ',' << r.newestSeq << '\n';
    return static_cast<bool>(f);
}

bool WriteText(const std::string& path, const std::string& text)
{
    if (path.empty()) return true;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    f << text;
    return static_cast<bool>(f);
}

// One short connection: the first frame PUBLISHED AFTER this connection attached, then hang up.
// A real emulator publishes perf records only while somebody is attached, so the sequence stands still between
// probes and a new connection's first reply carries the newest publication: the previous connection's last
// frame, from long ago. Its sample_seq equals the newest_seq that reply reports. The edge is therefore the
// first identified record with sample_seq strictly after that baseline (a signed difference, so a wrap of the
// 64-bit counter would not matter). A first reply with no record still reports newest_seq (a gap-only item, or
// 0 when nothing was ever published, in which case every record is fresh). A baseline of 0 on a record (a
// server that does not report it) falls back to that record's own sample_seq, i.e. it is treated as stale.
// Returns false when no fresh record arrived within the connect timeout (a paused emulator publishes none).
bool Probe(const BenchOptions& o, Clock::time_point t0, std::vector<Row>& rows, CpuSample* cpu,
           int64_t* holdNs, std::FILE* out, uint64_t* inconsistent = nullptr)
{
    se_data_source ds{};
    if (!OpenWithRetry(o, &ds, t0)) { std::fprintf(out, "error: cannot connect\n"); return false; }
    se_live_set_rewind_enabled(&ds, 0);   // as in the stream configs: rewind is held off
    const int64_t limit = NowNs(t0) + static_cast<int64_t>(o.connectTimeoutS) * 1000000000ll;
    se_live_perf_frame b[16];
    bool got = false, haveBase = false;
    uint64_t base = 0;
    int64_t sampledNs = 0;
    while (!got && NowNs(t0) < limit)
    {
        const uint32_t n = se_live_drain_perf(&ds, b, 16);
        const int64_t at = NowNs(t0);
        for (uint32_t i = 0; i < n; ++i)
        {
            if (!haveBase)
            {
                base = b[i].newest_seq ? b[i].newest_seq : (b[i].gap_only ? 0 : b[i].sample_seq);
                haveBase = true;
            }
            if (b[i].gap_only || !(b[i].valid & SE_PERF_VALID_IDENTITY)) continue;
            if (static_cast<int64_t>(b[i].sample_seq - base) <= 0) continue;   // published before we attached
            // Frames published while attached are one per frame: consecutive sequence numbers (nothing lost
            // between them) carry consecutive frame numbers on one timeline. A batch that breaks this did not
            // come from a live stream, and no speed may be computed from it.
            if (inconsistent && got && rows.back().gapCount == 0 && b[i].gap_count == 0)
            {
                const Row& prev = rows.back();
                if (b[i].sample_seq == prev.sampleSeq + 1 &&
                    (static_cast<int64_t>(b[i].frame_no) != prev.frameNo + 1 || b[i].timeline_id != prev.timeline))
                    ++*inconsistent;
            }
            Row r = ToRow(b[i], at);
            r.kind = "probe";
            if (got) rows.pop_back();
            rows.push_back(r);
            got = true;
            sampledNs = at;
        }
        if (!got) SleepMs(1);
    }
    if (!got)
        std::fprintf(out, "error: no frame published after the connection attached within %d s (is the emulator paused? "
                          "a stale frame from an earlier connection is not accepted)\n", o.connectTimeoutS);
    if (got && cpu) *cpu = SampleCpu(o.pid, t0);
    // The first reply is the handshake's; the REW 0 request goes out behind it. Hanging up before it was
    // sent would leave the server capturing a savestate per frame for the whole (short) connection, so
    // wait for the second reply, which is REW's. Bounded: a server that never answers must not hang a probe.
    for (int i = 0; got && i < 500; ++i)
    {
        se_live_perf_status st{};
        if (se_live_perf_status_get(&ds, &st) && st.replies >= 2) break;
        SleepMs(1);
    }
    Close(ds);
    if (holdNs) *holdNs = NowNs(t0) - sampledNs;
    return got;
}

int RunLoadSlot(const BenchOptions& o, std::FILE* out)
{
    const Clock::time_point t0 = Clock::now();
    if (o.loadSlot < 0) { std::fprintf(out, "error: --load-slot needs a slot index\n"); return kError; }
    se_data_source ds{};
    if (!OpenWithRetry(o, &ds, t0)) { std::fprintf(out, "error: cannot connect\n"); return kError; }
    se_config cfg;
    cfg.abi_version = SE_ABI_VERSION;
    cfg.reserved = 0;
    se_context* ctx = se_create(&ds, &cfg);
    if (!ctx) { Close(ds); std::fprintf(out, "error: se_create failed\n"); return kError; }
    se_live_set_rewind_enabled(&ds, 0);   // no per-frame savestates while the scene is being loaded
    int rc = kError;
    uint32_t done0 = 0, failed0 = 0;
    // A load is aimed at the session this thread last captured, so capture until there is one.
    bool ready = false;
    for (int i = 0; i < 5000 && !ready; ++i)
    {
        se_begin_frame(ctx);
        ready = se_live_captured_generation(&ds) != 0 && se_live_restore_state(&ds, &done0, &failed0) != 0;
        if (!ready) SleepMs(2);
    }
    if (!ready) std::fprintf(out, "error: no snapshot from the emulator (or a server older than v19)\n");
    else
    {
        uint32_t rc0 = 1;
        for (int i = 0; i < 5000 && rc0 != 0; ++i)
        {
            se_begin_frame(ctx);
            rc0 = static_cast<uint32_t>(se_live_emu_load_slot(&ds, static_cast<uint32_t>(o.loadSlot)));
            if (rc0 != 0) SleepMs(2);
        }
        if (rc0 != 0) std::fprintf(out, "error: the load request was refused\n");
        else
        {
            const int64_t limit = NowNs(t0) + static_cast<int64_t>(o.connectTimeoutS) * 1000000000ll;
            for (;;)
            {
                se_begin_frame(ctx);
                uint32_t d = 0, f = 0;
                se_live_restore_state(&ds, &d, &f);
                if (f > failed0) { std::fprintf(out, "error: the emulator refused the slot load\n"); break; }
                if (d > done0) { std::fprintf(out, "slot %d loaded\n", o.loadSlot); rc = kOk; break; }
                if (NowNs(t0) > limit) { std::fprintf(out, "error: the load was not applied in time\n"); break; }
                SleepMs(5);
            }
        }
    }
    se_destroy(ctx);
    return rc;
}

}  // namespace

long long ReadProcessCpuTicks(long pid, long* ticksPerSecond)
{
#if defined(__linux__)
    if (ticksPerSecond) *ticksPerSecond = sysconf(_SC_CLK_TCK);
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!f || !std::getline(f, line)) return -1;
    const size_t close = line.rfind(')');   // the command name may contain spaces
    if (close == std::string::npos) return -1;
    std::istringstream rest(line.substr(close + 1));
    std::string tok;
    long long utime = 0, stime = 0;
    for (int field = 3; rest >> tok; ++field)   // first token after ')' is field 3 (state)
    {
        if (field == 14) utime = std::atoll(tok.c_str());
        if (field == 15) { stime = std::atoll(tok.c_str()); return utime + stime; }
    }
    return -1;
#else
    (void)pid;
    if (ticksPerSecond) *ticksPerSecond = 0;
    return -1;
#endif
}

int RunBench(const BenchOptions& o, std::FILE* out)
{
    if (o.mode == Mode::LoadSlot) return RunLoadSlot(o, out);

    const Clock::time_point t0 = Clock::now();
    std::vector<Row> rows;
    rows.reserve(1u << 16);
    const int64_t warmNs = static_cast<int64_t>(o.warmupS * 1e9);
    const int64_t durNs = static_cast<int64_t>(o.durationS * 1e9);
    CpuSample cpuStart, cpuEnd;
    int64_t windowStartNs = -1, windowEndNs = -1;
    int64_t holdStartNs = 0, holdEndNs = 0;
    uint64_t extraProbes = 0, probeInconsistent = 0;
    int64_t windowFrames = -1;
    uint32_t caps = 0, version = 0;
    uint64_t statusServerLost = 0, statusDriverEvicted = 0;
    bool haveStatus = false;
    std::vector<std::string> invalid;

    int64_t originFrame = -1, openFrame = -1;
    uint64_t warmProbes = 0;
    int64_t holdWarmNs = 0;
    size_t startIdx = 0;   // bracket: the row of the probe that opens the window
    if (o.mode == Mode::Bracket)
    {
        if (o.warmupFrames > 0)
        {
            // Warm-up in frames, with the stream's meaning (see the stream branch): the origin is the first frame
            // published after the client attached, the window opens at the first observation that is at least
            // warmupFrames past it, and the seconds option is not used. The emulator publishes only while a
            // client is attached and the client stays disconnected between probes, so the progress is read with
            // short probes: one for the origin, then, after a sleep sized from the frame rate seen so far, one
            // more, until the count is reached. That probe is the window's start probe (no extra connection).
            // Granularity: the window opens within (the last sleep + one probe's latency) of frames after the
            // target, in frames at most the distance between the last two probes' frame numbers; each sleep is
            // 90 percent of the estimated remaining time (never under kMinSpacingNs), so that shrinks as the
            // target nears. Bounds: timeoutS for the whole warm-up, kMaxWarmProbes probes, and a probe that
            // sees no fresh frame fails after connectTimeoutS.
            const int64_t kMinSpacingNs = 20000000, kFirstSleepNs = 50000000, kStalledSleepNs = 250000000;
            const uint64_t kMaxWarmProbes = 64;
            const int64_t deadlineNs = NowNs(t0) + static_cast<int64_t>(o.timeoutS * 1e9);
            CpuSample cpuTmp;
            int64_t holdTmp = 0;
            if (!Probe(o, t0, rows, &cpuTmp, &holdTmp, out, &probeInconsistent)) return kError;
            const Row org = rows.back();
            originFrame = org.frameNo;
            uint64_t probes = 1;
            for (;;)
            {
                const Row cur = rows.back();
                if (probes > 1 && cur.frameNo - org.frameNo >= o.warmupFrames)
                {
                    cpuStart = cpuTmp;
                    holdStartNs = holdTmp;
                    break;
                }
                holdWarmNs += holdTmp;
                if (probes >= kMaxWarmProbes || NowNs(t0) >= deadlineNs)
                {
                    std::fprintf(out, "error: the warm-up of %lld frames was not reached (origin frame %lld, newest %lld, %llu probes, %.0f s allowed)\n",
                                 static_cast<long long>(o.warmupFrames), static_cast<long long>(org.frameNo),
                                 static_cast<long long>(cur.frameNo), static_cast<unsigned long long>(probes), o.timeoutS);
                    return kError;
                }
                int64_t sleepNs = kFirstSleepNs;
                const int64_t dt = cur.arrivalNs - org.arrivalNs, df = cur.frameNo - org.frameNo;
                if (probes > 1)
                    sleepNs = (df > 0 && dt > 0)
                        ? static_cast<int64_t>(0.9 * static_cast<double>(o.warmupFrames - df) * static_cast<double>(dt) / static_cast<double>(df))
                        : kStalledSleepNs;
                sleepNs = (std::max)(sleepNs, kMinSpacingNs);
                SleepUntilNs(t0, (std::min)(cur.arrivalNs + sleepNs, deadlineNs));
                if (!Probe(o, t0, rows, &cpuTmp, &holdTmp, out, &probeInconsistent)) return kError;
                ++probes;
                if (rows.back().timeline != org.timeline)
                {
                    std::fprintf(out, "error: the timeline changed during the warm-up (a state load?); load the slot before the bracket starts\n");
                    return kError;
                }
            }
            warmProbes = probes - 1;   // the origin probe and the ones after it, not the window's start probe
        }
        else
        {
            SleepUntilNs(t0, warmNs);
            if (!Probe(o, t0, rows, &cpuStart, &holdStartNs, out, &probeInconsistent)) return kError;
            originFrame = rows.back().frameNo;
        }
        startIdx = rows.size() - 1;
        openFrame = rows.back().frameNo;
        windowStartNs = rows.back().arrivalNs;
        SleepUntilNs(t0, windowStartNs + durNs);
        const int64_t startFrame = rows.back().frameNo;
        for (;;)
        {
            if (!Probe(o, t0, rows, &cpuEnd, &holdEndNs, out, &probeInconsistent)) return kError;
            windowEndNs = rows.back().arrivalNs;
            if (o.frames <= 0 || rows.back().frameNo - startFrame >= o.frames) break;
            if (NowNs(t0) > static_cast<int64_t>(o.timeoutS * 1e9))
            {
                std::fprintf(out, "error: %lld frames not reached within %.0f s\n", static_cast<long long>(o.frames), o.timeoutS);
                return kError;
            }
            ++extraProbes;   // too few frames yet: look again (each look is a short connection, kept in the CSV)
            SleepMs(250);
        }
    }
    else
    {
        se_data_source ds{};
        if (!OpenWithRetry(o, &ds, t0)) { std::fprintf(out, "error: cannot connect\n"); return kError; }
        se_live_set_rewind_enabled(&ds, 0);   // rewind held off, as the plan's V3 requires
        if (!WaitVersion(ds, t0, 10)) { Close(ds); std::fprintf(out, "error: no reply from the emulator\n"); return kError; }
        version = se_live_server_version(&ds);
        // Capabilities arrive with the first reply; wait for the word to be non-zero.
        for (int i = 0; i < 500 && (caps = CapsOf(ds)) == 0; ++i) SleepMs(2);
        if (!(caps & SE_LIVE_CAP_PROFILER)) { Close(ds); std::fprintf(out, "error: the server has no profiler (caps 0x%x)\n", caps); return kError; }
        if (o.functions)
        {
            if (!(caps & SE_LIVE_CAP_PROF_FUNCTIONS)) { Close(ds); std::fprintf(out, "error: attribution requested but the server lacks SE_LIVE_CAP_PROF_FUNCTIONS\n"); return kError; }
            se_live_set_profiler(&ds, SE_LIVE_PROF_FUNCTIONS);
        }
        else
            se_live_set_profiler(&ds, 0);
        const int64_t startNs = NowNs(t0);   // the measured schedule starts when the stream is up
        const int64_t wsTarget = startNs + warmNs;
        const bool byCount = o.frames > 0 || o.warmupFrames > 0;
        const int64_t timeoutNs = static_cast<int64_t>((byCount ? o.timeoutS : o.warmupS + o.durationS + o.timeoutS) * 1e9);
        const uint32_t wantMode0 = o.functions ? SE_LIVE_PROF_FUNCTIONS : 0u;
        // The warm-up origin is the first frame published AFTER this connection attached. The first reply carries
        // the newest publication of an earlier connection (the emulator publishes only while somebody is attached),
        // so counting from it would credit the warm-up with frames that passed while nobody was connected. Same
        // rule as Probe.
        bool haveBase = false;
        uint64_t base = 0;
        int64_t newest = -1, firstSeen = -1, startFrame = -1;
        uint32_t newestMode = 0;
        se_live_perf_frame b[64];
        for (;;)
        {
            SleepMs(o.drainMs);
            uint32_t n;
            const int64_t at = NowNs(t0);
            bool mixedBatch = false;   // a frame in this drain ran in another mode than requested
            while ((n = se_live_drain_perf(&ds, b, 64)) != 0)
                for (uint32_t i = 0; i < n; ++i)
                {
                    rows.push_back(ToRow(b[i], at));
                    const Row& r = rows.back();
                    if (!haveBase)
                    {
                        base = b[i].newest_seq ? b[i].newest_seq : (b[i].gap_only ? 0 : b[i].sample_seq);
                        haveBase = true;
                    }
                    if (r.frameNo >= 0 && std::strcmp(r.kind, "gap") != 0)
                    {
                        if (r.mode != wantMode0) mixedBatch = true;
                        if (firstSeen < 0 && static_cast<int64_t>(b[i].sample_seq - base) > 0) firstSeen = r.frameNo;
                        newest = r.frameNo;
                        newestMode = r.mode;
                    }
                }
            // The window opens once the warm-up is over AND the requested attribution mode is the one the newest
            // frame ran in: the request is applied at a frame boundary some frames after it is sent, and how many
            // depends on the host, so a fixed warm-up cannot promise it.
            const bool warmDone = o.warmupFrames > 0 ? (firstSeen >= 0 && newest - firstSeen >= o.warmupFrames) : at >= wsTarget;
            // All rows drained together share one arrival time, so a drain that still holds a frame of the old mode
            // cannot open the window: that frame would be counted inside it.
            if (windowStartNs < 0 && warmDone && newest >= 0 && newestMode == wantMode0 && !mixedBatch)
            {
                windowStartNs = at;
                startFrame = newest;
                originFrame = firstSeen;
                openFrame = newest;
                cpuStart = SampleCpu(o.pid, t0);
            }
            if (windowStartNs >= 0 && (o.frames > 0 ? newest - startFrame >= o.frames : at >= windowStartNs + durNs))
            {
                windowEndNs = at;
                windowFrames = newest - startFrame;
                cpuEnd = SampleCpu(o.pid, t0);
                break;
            }
            if (at > timeoutNs)
            {
                Close(ds);
                std::fprintf(out, "error: the measured window %s within %.0f s (newest frame %lld, mode %u, wanted %u)\n",
                             windowStartNs < 0 ? "did not open" : "did not reach its frame count",
                             byCount ? o.timeoutS : o.warmupS + o.durationS + o.timeoutS, static_cast<long long>(newest),
                             newestMode, wantMode0);
                return kError;
            }
        }
        se_live_perf_status st{};
        haveStatus = se_live_perf_status_get(&ds, &st) != 0;
        statusServerLost = st.server_lost;
        statusDriverEvicted = st.driver_evicted;
        Close(ds);
    }

    // ---- validity, from the rows ----
    uint64_t lostServerWin = 0, lostDriverWin = 0, lostServerAll = 0, lostDriverAll = 0;
    uint64_t seqMissing = 0, modeMismatch = 0, timelineChanges = 0, noIdentity = 0, windowRows = 0;
    const uint32_t wantMode = o.functions ? SE_LIVE_PROF_FUNCTIONS : 0u;
    uint64_t prevSeq = 0;
    bool prevInWin = false;
    int64_t firstFrame = -1, lastFrame = -1;
    uint32_t timeline = 0;
    bool haveTimeline = false;
    for (const Row& r : rows)
    {
        lostServerAll += r.gapCount;
        lostDriverAll += r.evictCount;
        const bool inWin = r.arrivalNs >= windowStartNs && r.arrivalNs <= windowEndNs;
        if (inWin) { lostServerWin += r.gapCount; lostDriverWin += r.evictCount; }
        if (std::strcmp(r.kind, "gap") == 0) continue;
        if (inWin)
        {
            ++windowRows;
            if (r.frameNo < 0) ++noIdentity;
            if (o.mode == Mode::Stream && r.mode != wantMode) ++modeMismatch;
            if (r.frameNo >= 0)
            {
                if (firstFrame < 0) firstFrame = r.frameNo;
                lastFrame = r.frameNo;
                if (!haveTimeline) { timeline = r.timeline; haveTimeline = true; }
                else if (r.timeline != timeline) ++timelineChanges;
            }
            if (o.mode == Mode::Stream && prevInWin && r.sampleSeq > prevSeq + 1) seqMissing += r.sampleSeq - prevSeq - 1;
        }
        prevSeq = r.sampleSeq;
        prevInWin = inWin;
    }
    if (lostServerWin || lostDriverWin) invalid.push_back("samples lost in the measured window");
    if (seqMissing) invalid.push_back("sample_seq not consecutive in the measured window");
    if (modeMismatch) invalid.push_back("attribution mode of some window frames differs from the request");
    if (timelineChanges) invalid.push_back("timeline changed inside the measured window");
    if (noIdentity) invalid.push_back("records without frame identity");
    if (o.mode == Mode::Stream && windowRows < 2) invalid.push_back("fewer than two frames in the window");
    if (o.mode == Mode::Bracket && windowRows < 2) invalid.push_back("a bracket probe did not read a frame");
    if (o.mode == Mode::Bracket && probeInconsistent)
        invalid.push_back("a bracket probe read frames whose numbers do not follow their sample sequence");
    if (o.mode == Mode::Bracket && windowRows >= 2)
    {
        // The two edges are fresh records of one emulator run: later in publication order AND in frame number.
        // (The sample sequence advances only while somebody is attached, so its distance says nothing about
        // how many frames passed between the probes; the order is all it can tell.)
        size_t first = startIdx, last = 0;
        for (size_t i = 0; i < rows.size(); ++i) if (!std::strcmp(rows[i].kind, "probe")) last = i;
        if (first < last && (rows[last].sampleSeq <= rows[first].sampleSeq || rows[last].frameNo <= rows[first].frameNo))
            invalid.push_back("the bracket's end probe is not later than its start probe");
    }
    if (haveStatus && (statusServerLost != lostServerAll || statusDriverEvicted != lostDriverAll))
        invalid.push_back("per-row loss counts disagree with the driver's own totals");

    Summary s;
    s.Put("tag", o.tag);
    s.Put("mode", o.mode == Mode::Bracket ? "bracket" : "stream");
    s.Put("attribution_requested", static_cast<long long>(o.functions ? 1 : 0));
    s.Put("server_version", static_cast<long long>(version));
    s.Put("caps", static_cast<long long>(caps));
    s.Put("warmup_s", o.warmupS);
    s.Put("duration_s", o.durationS);
    s.Put("drain_ms", static_cast<long long>(o.drainMs));
    s.Put("window_start_ns", static_cast<long long>(windowStartNs));
    s.Put("window_end_ns", static_cast<long long>(windowEndNs));
    if (o.mode == Mode::Bracket) windowFrames = lastFrame - firstFrame;
    s.Put("window_frames", static_cast<long long>(windowFrames));
    s.Put("frames_requested", static_cast<long long>(o.frames));
    s.Put("bracket_probe_inconsistent", static_cast<unsigned long long>(probeInconsistent));
    s.Put("bracket_extra_probes", static_cast<unsigned long long>(extraProbes));
    // Warm-up record. origin = first frame published after the client attached; open = the frame the window
    // opened at. With a frame warm-up, open - origin >= warmup_frames_requested; the excess is the granularity
    // (one drain for the stream, the last probe interval for the bracket). bracket_warmup_probes counts the
    // short connections made before the window's start probe (origin probe included), probe_hold_warmup_ms
    // their summed hold: the perturbation a frame warm-up costs configuration (a).
    s.Put("warmup_frames_requested", static_cast<long long>(o.warmupFrames));
    s.Put("window_origin_frame", static_cast<long long>(originFrame));
    s.Put("window_open_frame", static_cast<long long>(openFrame));
    s.Put("bracket_warmup_probes", static_cast<unsigned long long>(warmProbes));
    s.Put("probe_hold_warmup_ms", static_cast<double>(holdWarmNs) / 1e6);
    s.Put("window_rows", static_cast<unsigned long long>(windowRows));
    s.Put("window_first_frame", static_cast<long long>(firstFrame));
    s.Put("window_last_frame", static_cast<long long>(lastFrame));
    s.Put("lost_server_ring_window", static_cast<unsigned long long>(lostServerWin));
    s.Put("lost_client_queue_window", static_cast<unsigned long long>(lostDriverWin));
    s.Put("lost_server_ring_total", static_cast<unsigned long long>(lostServerAll));
    s.Put("lost_client_queue_total", static_cast<unsigned long long>(lostDriverAll));
    s.Put("driver_status_server_lost", static_cast<unsigned long long>(statusServerLost));
    s.Put("driver_status_client_evicted", static_cast<unsigned long long>(statusDriverEvicted));
    s.Put("seq_missing_window", static_cast<unsigned long long>(seqMissing));
    s.Put("mode_mismatch_window", static_cast<unsigned long long>(modeMismatch));
    s.Put("timeline_changes_window", static_cast<unsigned long long>(timelineChanges));
    s.Put("probe_hold_start_ms", static_cast<double>(holdStartNs) / 1e6);
    s.Put("probe_hold_end_ms", static_cast<double>(holdEndNs) / 1e6);
    s.Put("emulator_pid", static_cast<long long>(o.pid));
    if (o.pid > 0)
    {
        long hz = 0;
        ReadProcessCpuTicks(o.pid, &hz);
        s.Put("cpu_ticks_per_s", static_cast<long long>(hz));
    }
    s.Put("cpu_start_at_ns", static_cast<long long>(cpuStart.atNs));
    s.Put("cpu_start_ticks", static_cast<long long>(cpuStart.ticks));
    s.Put("cpu_end_at_ns", static_cast<long long>(cpuEnd.atNs));
    s.Put("cpu_end_ticks", static_cast<long long>(cpuEnd.ticks));
    s.Put("valid", static_cast<long long>(invalid.empty() ? 1 : 0));
    for (const std::string& why : invalid) s.Put("invalid_reason", why);

    const bool csvOk = WriteCsv(o.csvPath, rows);
    const std::string text = s.Text();
    const bool sumOk = WriteText(o.summaryPath, text);
    std::fputs(text.c_str(), out);
    std::fprintf(out, "LOSS server_ring=%llu client_queue=%llu (window), %llu / %llu (whole run)%s\n",
                 static_cast<unsigned long long>(lostServerWin), static_cast<unsigned long long>(lostDriverWin),
                 static_cast<unsigned long long>(lostServerAll), static_cast<unsigned long long>(lostDriverAll),
                 invalid.empty() ? "" : "  => RUN INVALID");
    if (!csvOk || !sumOk) { std::fprintf(out, "error: could not write the output files\n"); return kError; }
    return invalid.empty() ? kOk : kInvalid;
}
}  // namespace v3
