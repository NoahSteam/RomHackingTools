// Cancellation spike (PLAN.md A4). Proves that, with patches/0001-cancel-flag.patch applied,
// a decompilation already running on the engine thread stops when another thread sets the
// flag, that the latency is bounded, and that the engine (whose SLEIGH translator is a
// process-global object) is usable again afterwards.
#include "libdecomp.hh"
#include "loadimage.hh"
#include "sleigh_arch.hh"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <sstream>
#include <thread>
#include <vector>
using namespace ghidra;
using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) { return std::chrono::duration<double, std::milli>(b - a).count(); }

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

// N blocks of: mov.l @r4,r1 ; add #1,r1 ; mov.l r1,@r4 ; cmp/pl r1 ; bt .+4 (skips the add #2) ; add #2,r2
// then rts ; nop. Every block is a diamond, so the CFG and the SSA work grow with N.
static std::vector<uint8_t> synth(int blocks) {
  static const uint16_t blk[] = { 0x6142, 0x7101, 0x2412, 0x4115, 0x8900, 0x7202 };
  std::vector<uint8_t> v;
  auto put = [&](uint16_t w) { v.push_back(w >> 8); v.push_back(w & 0xFF); };
  for (int i = 0; i < blocks; ++i) for (uint16_t w : blk) put(w);
  put(0x000B); put(0x0009);
  return v;
}

struct Outcome { std::string phase = "none"; double totalMs = 0, teardownMs = 0; Clock::time_point caughtAt{}; std::string text; bool cancelled = false; std::string error; };

// One decompilation of the synthetic function on the calling thread. 'flag' is the host-owned cancel flag.
static Outcome run(int blocks, std::atomic<bool>* flag, const std::string& specdir, Clock::time_point* started) {
  Outcome o; auto t0 = Clock::now(); if (started) *started = t0;
  std::ostringstream errs;
  Arch arch(new MemImage(0x06010000, synth(blocks)), &errs);
  arch.cancelRequested = flag;
  DocumentStorage store; arch.init(store);
  arch.max_instructions = 2000000;
  Address entry(arch.getDefaultCodeSpace(), 0x06010000);
  Funcdata* fd = arch.symboltab->getGlobalScope()->addFunction(entry, "synth")->getFunction();
  try {
    o.phase = "followFlow";
    fd->followFlow(Address(entry.getSpace(), 0), Address(entry.getSpace(), entry.getSpace()->getHighest()));
    o.phase = "actions";
    arch.allacts.getCurrent()->reset(*fd);
    arch.allacts.getCurrent()->perform(*fd);
    o.phase = "print";
    std::ostringstream out; arch.print->setOutputStream(&out); arch.print->docFunction(fd); o.text = out.str();
    o.phase = "done";
  } catch (CancelError& e) { o.cancelled = true; o.caughtAt = Clock::now(); }
  catch (LowlevelError& e) { o.error = e.explain; }
  o.totalMs = ms(t0, Clock::now());
  return o;
}
static Outcome runAndTearDown(int blocks, std::atomic<bool>* flag, const std::string& specdir, Clock::time_point* started) {
  auto t0 = Clock::now(); Outcome o = run(blocks, flag, specdir, started);   // run()'s locals (Architecture, Funcdata) are destroyed on return
  o.teardownMs = ms(t0, Clock::now()) - o.totalMs; return o;
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: cancel_spike <specdir> [blocks]\n"); return 2; }
  std::string specdir = argv[1]; int blocks = argc > 2 ? atoi(argv[2]) : 3000;
  startDecompilerLibrary(std::vector<std::string>{specdir});
  std::atomic<bool> flag{false};

  // 1. Baseline: how long does the synthetic function take uncancelled?
  Outcome base = runAndTearDown(blocks, &flag, specdir, nullptr);
  printf("baseline  blocks=%d  %.0f ms (+%.0f ms teardown)  phase=%s  cancelled=%d  err=%s  text=%zu bytes\n", blocks, base.totalMs, base.teardownMs, base.phase.c_str(), base.cancelled, base.error.c_str(), base.text.size());

  // 2. Cancel from another thread at several points after the work has started.
  for (double cancelAtMs : { 1.0, 50.0, base.totalMs * 0.5 }) {
    flag.store(false);
    Outcome o; Clock::time_point started{}, flagSet{};
    std::thread worker([&] { o = runAndTearDown(blocks, &flag, specdir, &started); });
    while (started == Clock::time_point{}) std::this_thread::yield();
    std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(cancelAtMs));
    flagSet = Clock::now(); flag.store(true);
    worker.join();
    auto ended = Clock::now();
    double toCatch = o.cancelled ? ms(flagSet, o.caughtAt) : -1;
    printf("cancel@%6.0f ms  -> phase=%-10s cancelled=%d  flag->caught %.1f ms, +teardown %.0f ms, flag->joined %.1f ms  (worker ran %.0f ms)\n",
           cancelAtMs, o.phase.c_str(), o.cancelled, toCatch, o.teardownMs, ms(flagSet, ended), o.totalMs);
  }

  // 3. Engine reuse after cancellation: a fresh Architecture on the same translator must give the baseline result.
  flag.store(false);
  Outcome again = runAndTearDown(blocks, &flag, specdir, nullptr);
  printf("after-cancel rerun: %.0f ms  phase=%s  identical-to-baseline=%d\n", again.totalMs, again.phase.c_str(), again.text == base.text);
  // and a small, different function (the fixture-1 callee shape) to show unrelated work is unaffected
  {
    std::vector<uint8_t> callee = { 0x60, 0x42, 0x00, 0x0B, 0x70, 0x02 };
    std::ostringstream errs; Arch arch(new MemImage(0x06005000, callee), &errs); arch.cancelRequested = &flag;
    DocumentStorage store; arch.init(store);
    Address e(arch.getDefaultCodeSpace(), 0x06005000);
    Funcdata* fd = arch.symboltab->getGlobalScope()->addFunction(e, "callee")->getFunction();
    fd->followFlow(Address(e.getSpace(), 0), Address(e.getSpace(), e.getSpace()->getHighest()));
    arch.allacts.getCurrent()->reset(*fd); arch.allacts.getCurrent()->perform(*fd);
    std::ostringstream out; arch.print->setOutputStream(&out); arch.print->docFunction(fd);
    printf("small function after cancel:%s", out.str().c_str());
  }
  return 0;
}
