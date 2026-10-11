// Cancellation spike (PLAN.md A4). With patches/0001-cancel-flag.patch applied, proves that a
// decompilation already running on the engine thread stops when another thread sets the flag
// during flow following and during actions, measures the phases that have no check-point
// (print, teardown), and checks the engine is usable afterwards. Exits non-zero on any failure.
#include "libdecomp.hh"
#include "loadimage.hh"
#include "sleigh_arch.hh"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
using namespace ghidra;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; printf("  FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

class MemImage : public LoadImage {
  uint32_t base; std::vector<uint8_t> bytes;
public:
  MemImage(uint32_t b, std::vector<uint8_t> v) : LoadImage("mem"), base(b), bytes(std::move(v)) {}
  void loadFill(uint1* ptr, int4 size, const Address& addr) override {
    uint32_t a = (uint32_t)addr.getOffset();
    if (a < base || a >= base + bytes.size()) throw DataUnavailError("unmapped");
    int4 n = std::min<int4>(size, (int4)(base + bytes.size() - a));
    memcpy(ptr, bytes.data() + (a - base), n); if (n < size) memset(ptr + n, 0, size - n);
  }
  std::string getArchType() const override { return "mem"; }
  void adjustVma(long) override {}
};
class Arch : public SleighArchitecture {
  LoadImage* img;
  void buildLoader(DocumentStorage&) override { collectSpecFiles(*errorstream); loader = img; }
  void resolveArchitecture() override { archid = getTarget(); SleighArchitecture::resolveArchitecture(); }
public:
  Arch(LoadImage* i, std::ostream* e) : SleighArchitecture("mem", "SuperH:BE:32:SH-2:default", e), img(i) {}
};

// ---- fixtures -----------------------------------------------------------------------------
struct Fixture { std::string name; uint32_t base; std::vector<uint8_t> bytes; };
static void put(std::vector<uint8_t>& v, uint16_t w) { v.push_back(w >> 8); v.push_back(w & 0xFF); }
static void put32(std::vector<uint8_t>& v, uint32_t w) { put(v, w >> 16); put(v, w & 0xFFFF); }
// N diamonds: mov.l @r4,r1 ; add #1,r1 ; mov.l r1,@r4 ; cmp/pl r1 ; bt .+4 ; add #2,r2
static Fixture diamonds(int n) {
  Fixture f{"diamonds", 0x06010000, {}};
  for (int i = 0; i < n; ++i) for (uint16_t w : {0x6142, 0x7101, 0x2412, 0x4115, 0x8900, 0x7202}) put(f.bytes, w);
  put(f.bytes, 0x000B); put(f.bytes, 0x0009); return f;
}
// N x (mov.l @r4,r1 ; add #1,r1 ; mov.l r1,@r4): one straight-line fallthru run, no branches
static Fixture straight(int n) {
  Fixture f{"straight", 0x06030000, {}};
  for (int i = 0; i < n; ++i) for (uint16_t w : {0x6142, 0x7101, 0x2412}) put(f.bytes, w);
  put(f.bytes, 0x000B); put(f.bytes, 0x0009); return f;
}
// GCC-style switch: bounds check, shll2, mova table, mov.l @(r0,r1),r3, jmp @r3, table of N longs, N cases
static Fixture jumptable(int n, int k) {
  Fixture f{"jumptable", 0x06020000, {}};
  const uint32_t table = f.base + 0x14, deflt = table + 4 * n, cases = deflt + 6, caseSize = 6 * k + 6;
  put(f.bytes, 0x6143);                       // 00 mov r4,r1
  put(f.bytes, 0xE200 | (uint8_t)(n - 1));    // 02 mov #n-1,r2
  put(f.bytes, 0x3126);                       // 04 cmp/hi r2,r1
  uint32_t disp = (deflt - (f.base + 0x06 + 4)) / 2;  if (disp > 0x7F) throw std::runtime_error("bt disp");
  put(f.bytes, 0x8900 | disp);                // 06 bt default
  put(f.bytes, 0x4108);                       // 08 shll2 r1
  put(f.bytes, 0xC702);                       // 0A mova table,r0  -> (0x0A&~3)+4+2*4 = 0x14
  put(f.bytes, 0x031E);                       // 0C mov.l @(r0,r1),r3
  put(f.bytes, 0x432B);                       // 0E jmp @r3
  put(f.bytes, 0x0009); put(f.bytes, 0x0009); // 10 nop (slot), 12 nop (align)
  for (int i = 0; i < n; ++i) put32(f.bytes, cases + caseSize * i);
  put(f.bytes, 0xE0FF); put(f.bytes, 0x000B); put(f.bytes, 0x0009);   // default: mov #-1,r0 ; rts ; nop
  for (int i = 0; i < n; ++i) {
    for (int j = 0; j < k; ++j) for (uint16_t w : {0x6142, 0x7101, 0x2412}) put(f.bytes, w);
    put(f.bytes, 0xE000 | (uint8_t)i); put(f.bytes, 0x000B); put(f.bytes, 0x0009);
  }
  return f;
}

// ---- one decompilation with a published phase ------------------------------------------------
enum Phase { P_NONE = 0, P_INIT, P_FLOW, P_ACTIONS, P_PRINT, P_TEARDOWN, P_DONE };
static const char* phaseName(int p) { static const char* n[] = {"none", "init", "followFlow", "actions", "print", "teardown", "done"}; return n[p]; }
struct PhaseSignal {
  std::mutex m; std::condition_variable cv; std::atomic<int> phase{P_NONE};
  void set(int p) { { std::lock_guard<std::mutex> l(m); phase.store(p); } cv.notify_all(); }
  void waitFor(int p) { std::unique_lock<std::mutex> l(m); cv.wait(l, [&] { return phase.load() >= p; }); }
};
struct Outcome {
  bool cancelled = false, done = false; int caughtIn = P_NONE; std::string text, error;
  double phaseMs[P_DONE + 1] = {0}; Clock::time_point caughtAt{}; int flagPhase = -1;
};
static Outcome run(const Fixture& fx, std::atomic<bool>* flag, PhaseSignal& sig) {
  Outcome o; int cur = P_INIT; auto t = Clock::now();
  auto enter = [&](int p) { auto now = Clock::now(); o.phaseMs[cur] += ms(t, now); t = now; cur = p; sig.set(p); };
  sig.set(P_INIT);
  {
    std::ostringstream errs;
    Arch arch(new MemImage(fx.base, fx.bytes), &errs);
    arch.cancelRequested = flag;
    DocumentStorage store; arch.init(store);
    arch.max_instructions = 2000000;
    Address entry(arch.getDefaultCodeSpace(), fx.base);
    Funcdata* fd = arch.symboltab->getGlobalScope()->addFunction(entry, fx.name)->getFunction();
    try {
      enter(P_FLOW);
      fd->followFlow(Address(entry.getSpace(), 0), Address(entry.getSpace(), entry.getSpace()->getHighest()));
      enter(P_ACTIONS);
      arch.allacts.getCurrent()->reset(*fd);
      arch.allacts.getCurrent()->perform(*fd);
      enter(P_PRINT);
      std::ostringstream out; arch.print->setOutputStream(&out); arch.print->docFunction(fd); o.text = out.str();
      o.done = true;
    } catch (CancelError&) { o.cancelled = true; o.caughtIn = cur; o.caughtAt = Clock::now(); }
    catch (LowlevelError& e) { o.error = e.explain; }
    enter(P_TEARDOWN);
  } // Architecture, Funcdata and the abandoned analysis are destroyed here, on this thread
  enter(P_DONE);
  return o;
}

// Cancel once 'phase' has been reached plus 'fraction' of that phase's baseline duration.
static Outcome cancelIn(const Fixture& fx, int phase, double fraction, const Outcome& base, std::atomic<bool>& flag) {
  flag.store(false);
  PhaseSignal sig; Outcome o;
  std::thread worker([&] { o = run(fx, &flag, sig); });
  sig.waitFor(phase);
  if (fraction > 0) std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(base.phaseMs[phase] * fraction));
  int before = sig.phase.load();
  auto flagSet = Clock::now(); flag.store(true);
  int after = sig.phase.load();          // if before == after, the flag was set while that phase ran
  worker.join();
  auto joined = Clock::now();
  o.flagPhase = (before == after) ? before : -1;
  printf("  cancel in %-10s +%3.0f%%: flag set during %-10s caught in %-15s flag->caught %7.1f ms  flag->joined %7.1f ms  teardown %6.1f ms  %s\n",
         phaseName(phase), fraction * 100, o.flagPhase < 0 ? "(boundary)" : phaseName(o.flagPhase), o.cancelled ? phaseName(o.caughtIn) : "(not cancelled)",
         o.cancelled ? ms(flagSet, o.caughtAt) : -1.0, ms(flagSet, joined), o.phaseMs[P_TEARDOWN], o.error.c_str());
  return o;
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: cancel_spike <specdir> [diamonds] [straight] [jumptable-cases<=20]\n"); return 2; }
  int nd = argc > 2 ? atoi(argv[2]) : 1000, ns = argc > 3 ? atoi(argv[3]) : 5000, nj = argc > 4 ? atoi(argv[4]) : 16;
  startDecompilerLibrary(std::vector<std::string>{argv[1]});
  std::atomic<bool> flag{false};

  for (const Fixture& fx : { diamonds(nd), straight(ns), jumptable(nj, 300) }) {
    printf("== %s: %zu bytes\n", fx.name.c_str(), fx.bytes.size());
    PhaseSignal sig; flag.store(false);
    Outcome base = run(fx, &flag, sig);
    printf("  baseline: done=%d err='%s' text=%zu bytes | init %.0f  followFlow %.0f  actions %.0f  print %.1f  teardown %.1f ms\n",
           base.done, base.error.c_str(), base.text.size(), base.phaseMs[P_INIT], base.phaseMs[P_FLOW], base.phaseMs[P_ACTIONS], base.phaseMs[P_PRINT], base.phaseMs[P_TEARDOWN]);
    CHECK(base.done && base.error.empty() && base.text.size() > 100, "%s: baseline did not produce output", fx.name.c_str());
    if (fx.name == "jumptable") {
      bool sw = base.text.find("switch") != std::string::npos;
      printf("  jump table recovered as switch: %s\n", sw ? "yes" : "no");
      CHECK(sw, "jumptable: switch not recovered");
    }
    if (!base.done) continue;

    // Cancellation requested after the phase has begun must be caught in that phase.
    int provenIn[P_DONE + 1] = {0};
    for (int ph : {P_FLOW, P_ACTIONS}) for (double frac : {0.0, 0.5}) {
      Outcome o = cancelIn(fx, ph, frac, base, flag);
      CHECK(o.cancelled, "%s: cancellation requested in %s was never honoured", fx.name.c_str(), phaseName(ph));
      if (o.flagPhase >= 0) {
        CHECK(o.caughtIn == o.flagPhase, "%s: flag set during %s but caught in %s", fx.name.c_str(), phaseName(o.flagPhase), phaseName(o.caughtIn));
        if (o.caughtIn == o.flagPhase) provenIn[o.flagPhase]++;
      }
    }
    CHECK(provenIn[P_FLOW] > 0, "%s: no run proved cancellation inside followFlow", fx.name.c_str());
    CHECK(provenIn[P_ACTIONS] > 0, "%s: no run proved cancellation inside actions", fx.name.c_str());
    // Printing has no check-point: the request must complete normally, and the time is the latency floor there.
    { Outcome o = cancelIn(fx, P_PRINT, 0.0, base, flag);
      CHECK(!o.cancelled && o.done && o.text == base.text, "%s: cancel during print changed the outcome", fx.name.c_str()); }

    // Engine reuse after cancellation on the same process-global translator.
    PhaseSignal sig2; flag.store(false);
    Outcome again = run(fx, &flag, sig2);
    CHECK(again.done && again.text == base.text, "%s: rerun after cancellation differs from baseline", fx.name.c_str());
    printf("  rerun after cancellation: done=%d identical=%d\n", again.done, again.text == base.text);
  }

  // An unrelated small function after all of the above.
  {
    Fixture small{"callee", 0x06005000, {0x60, 0x42, 0x00, 0x0B, 0x70, 0x02}};
    PhaseSignal sig; flag.store(false);
    Outcome o = run(small, &flag, sig);
    bool ok = o.done && o.text.find("return *param_1 + 2;") != std::string::npos;
    CHECK(ok, "small function after cancellations is wrong: %s", o.text.c_str());
    printf("== unrelated small function afterwards: %s\n", ok ? "correct" : "WRONG");
  }
  printf("%s (%d failures)\n", failures ? "CANCEL SPIKE FAILED" : "cancel spike passed", failures);
  return failures ? 1 : 0;
}
