// Feasibility spike: decompile SH-2 bytes sitting at real Saturn addresses with Ghidra's
// native C++ decompiler, fed from an in-memory image (the shape IMemoryBackend produces),
// with no Java, no Ghidra install, no subprocess.
#include "libdecomp.hh"
#include "loadimage.hh"
#include "sleigh_arch.hh"
#include "printc.hh"
#include "xml.hh"
#include <set>
#include <functional>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <vector>
#include <chrono>
using namespace ghidra;

// --- A LoadImage over captured Saturn regions ---------------------------------------------
// Mirrors what SaturnExplorer's ContextBackend sees: LWRAM at 0x00200000 and HWRAM at
// 0x06000000, both 1 MiB, nothing else. Addresses fold like the Saturn bus: bits 29-31
// select cache-through/purge mirrors of the same physical bytes, and HWRAM repeats every
// 1 MiB across 0x06000000-0x07FFFFFF. Anything unmapped (BIOS, cartridge, on-chip) throws
// DataUnavailError, which is how the decompiler learns to stop following flow there.
struct Region { uint32_t base, size; std::vector<uint8_t> bytes; };
class SaturnLoadImage : public LoadImage {
  std::vector<Region> regions;
public:
  RangeList ro;   // literal-pool words (and anything else) the decompiler may fold to constants
  void getReadonly(RangeList& list) const override { list = ro; }
  SaturnLoadImage() : LoadImage("saturn-capture") {}
  void add(uint32_t base, std::vector<uint8_t> b) { regions.push_back({base, (uint32_t)b.size(), std::move(b)}); }
  static uint32_t fold(uint32_t a) {
    a &= 0x1FFFFFFFu;                                   // drop cache-control bits 29-31
    if (a >= 0x06000000u && a < 0x08000000u) a = 0x06000000u | (a & 0x000FFFFFu); // HWRAM mirrors
    return a;
  }
  const Region* find(uint32_t a) const {
    for (auto& r : regions) if (a >= r.base && a < r.base + r.size) return &r;
    return nullptr;
  }
  void loadFill(uint1* ptr, int4 size, const Address& addr) override {
    uint32_t a = (uint32_t)addr.getOffset();
    int4 done = 0;
    while (done < size) {
      uint32_t fa = fold(a + done);
      const Region* r = find(fa);
      if (!r) {
        if (done == 0) {
          std::ostringstream e; e << "Unable to load " << size << " bytes at " << std::hex << a;
          throw DataUnavailError(e.str());
        }
        memset(ptr + done, 0, size - done);             // tail off the end of a region
        return;
      }
      uint32_t off = fa - r->base;
      int4 n = std::min<int4>(size - done, (int4)(r->size - off));
      memcpy(ptr + done, r->bytes.data() + off, n);
      done += n;
    }
  }
  std::string getArchType() const override { return "saturn"; }
  void adjustVma(long) override {}
};

// --- The Architecture: SLEIGH SH-2 + our loader, nothing else -------------------------------
class SaturnArchitecture : public SleighArchitecture {
  SaturnLoadImage* img;
  void buildLoader(DocumentStorage&) override { collectSpecFiles(*errorstream); loader = img; }
  void resolveArchitecture() override { archid = getTarget(); SleighArchitecture::resolveArchitecture(); }
  void postSpecFile() override { Architecture::postSpecFile(); }
public:
  SaturnArchitecture(SaturnLoadImage* image, std::ostream* err)
    : SleighArchitecture("saturn", "SuperH:BE:32:SH-2:default", err), img(image) {}
};

struct AsmCollector : public AssemblyEmit {
  std::vector<std::string> lines;
  void dump(const Address& addr, const std::string& mnem, const std::string& body) override {
    std::ostringstream s; s << std::hex << std::uppercase << addr.getOffset() << "  " << mnem << " " << body;
    lines.push_back(s.str());
  }
};

static std::vector<uint8_t> readFile(const char* p) {
  std::ifstream f(p, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

int main(int argc, char** argv) {
  // usage: spike <specdir> <func-addr-hex> [<base-hex>=<file>]... [--asm N]
  if (argc < 3) { fprintf(stderr, "usage: %s <specdir> <func> base=file...\n", argv[0]); return 2; }
  std::string specdir = argv[1];
  uint32_t func = (uint32_t)strtoul(argv[2], nullptr, 16);
  auto* img = new SaturnLoadImage();          // owned by the Architecture (deleted in ~Architecture)
  int asmCount = 0; bool xmlMarkup = false, autoRo = false, dumpPcode = false; std::vector<std::string> roArgs; std::vector<uint32_t> known;
  for (int i = 3; i < argc; ++i) {
    if (!strcmp(argv[i], "--asm") && i + 1 < argc) { asmCount = atoi(argv[++i]); continue; }
    if (!strcmp(argv[i], "--xml")) { xmlMarkup = true; continue; }
    if (!strcmp(argv[i], "--auto-ro")) { autoRo = true; continue; }
    if (!strcmp(argv[i], "--pcode")) { dumpPcode = true; continue; }
    if (!strcmp(argv[i], "--known") && i + 1 < argc) { known.push_back(strtoul(argv[++i], nullptr, 16)); continue; }
    if (!strcmp(argv[i], "--ro") && i + 1 < argc) { roArgs.push_back(argv[++i]); continue; }
    std::string a = argv[i]; auto eq = a.find('=');
    uint32_t base = (uint32_t)strtoul(a.substr(0, eq).c_str(), nullptr, 16);
    auto bytes = readFile(a.substr(eq + 1).c_str());
    printf("; region %08X..%08X from %s\n", base, base + (uint32_t)bytes.size() - 1, a.substr(eq + 1).c_str());
    img->add(base, std::move(bytes));
  }

  auto t0 = std::chrono::steady_clock::now();
  startDecompilerLibrary(std::vector<std::string>{specdir});   // registers .ldefs found in specdir
  std::ostringstream errs;
  SaturnArchitecture arch(img, &errs);
  DocumentStorage store;
  arch.readonlypropagate = true;   // Ghidra GUI default ("readonly" option on); lets literal pools fold to constants
  try { arch.init(store); }
  catch (LowlevelError& e) { fprintf(stderr, "init failed: %s\n%s\n", e.explain.c_str(), errs.str().c_str()); return 1; }
  auto t1 = std::chrono::steady_clock::now();
  printf("; language %s loaded in %lld ms\n", arch.getTarget().c_str(),
         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

  Address entry(arch.getDefaultCodeSpace(), func);
  for (auto& r : roArgs) {   // "base:size" hex; applied after init via the same path fillinReadOnlyFromLoader uses
    auto c = r.find(':'); uint32_t b = strtoul(r.substr(0, c).c_str(), nullptr, 16), n = strtoul(r.substr(c + 1).c_str(), nullptr, 16);
    arch.symboltab->setPropertyRange(Varnode::readonly, Range(entry.getSpace(), b, b + n - 1));
    printf("; read-only: %08X..%08X\n", b, b + n - 1);
  }
  if (asmCount > 0) {
    printf("; --- SLEIGH disassembly from %08X ---\n", func);
    AsmCollector em; Address a = entry;
    for (int i = 0; i < asmCount; ++i) {
      try { int4 len = arch.translate->printAssembly(em, a); a = a + len; }
      catch (LowlevelError& e) { printf(";   <%s>\n", e.explain.c_str()); break; }
    }
    for (auto& l : em.lines) printf(";   %s\n", l.c_str());
  }

  for (uint32_t k : known) {
    Address ka(entry.getSpace(), k); std::string kn; arch.nameFunction(ka, kn);
    Funcdata* kfd = arch.symboltab->getGlobalScope()->addFunction(ka, kn)->getFunction();
    try { Address lo(ka.getSpace(), 0), hi(ka.getSpace(), ka.getSpace()->getHighest()); kfd->followFlow(lo, hi); }
    catch (LowlevelError& e) { printf("; known %08X: %s\n", k, e.explain.c_str()); }
    printf("; pre-registered callee %s\n", kn.c_str());
  }
  std::string name; arch.nameFunction(entry, name);
  Funcdata* fd = arch.symboltab->getGlobalScope()->addFunction(entry, name)->getFunction();
  try {
    Address lo(entry.getSpace(), 0), hi(entry.getSpace(), entry.getSpace()->getHighest());
    fd->followFlow(lo, hi);
  } catch (LowlevelError& e) { printf("; followFlow: %s\n", e.explain.c_str()); }
  if (fd->hasNoCode()) { printf("; no code at %08X (memory not captured)\n", func); return 3; }
  printf("; function %s: %d bytes of body%s%s\n", fd->getName().c_str(), fd->getSize(),
         fd->hasBadData() ? ", flows into uncaptured memory" : "",
         fd->hasUnimplemented() ? ", has unimplemented p-code" : "");
  if (dumpPcode) {
    printf("; --- raw p-code ---\n");
    for (auto it = fd->beginOpMain(); it != fd->endOpMain(); ++it) { std::ostringstream o; it->second->printRaw(o); printf(";   %08X  %s\n", (uint32_t)it->second->getAddr().getOffset(), o.str().c_str()); }
  }
  // Literal pools. SLEIGH emits "tmp = #addr; Rn = *(ram, tmp)" for mov.l/mov.w @(disp,PC) and
  // raw-stage varnodes carry no def links yet, so track constants per instruction: walk that
  // instruction's ops in order, fold any op whose inputs are known constants, and when a LOAD
  // reads through a known-constant pointer, that pointer is a literal-pool word.
  if (autoRo) {
    int nro = 0;
    std::map<std::pair<uintb,int4>, uintb> consts;   // (unique offset, size) -> value, per instruction
    Address cur;
    for (auto it = fd->beginOpMain(); it != fd->endOpMain(); ++it) {
      const PcodeOp* op = it->second;
      if (op->getAddr() != cur) { cur = op->getAddr(); consts.clear(); }
      auto valueOf = [&](const Varnode* vn, uintb& v) {
        if (vn->isConstant()) { v = vn->getOffset(); return true; }
        auto f = consts.find({vn->getOffset(), vn->getSize()});
        if (vn->getSpace()->getType() == IPTR_INTERNAL && f != consts.end()) { v = f->second; return true; }
        return false;
      };
      uintb a, b;
      if (op->code() == CPUI_LOAD) {
        if (valueOf(op->getIn(1), a)) {
          int4 n = op->getOut()->getSize();
          arch.symboltab->setPropertyRange(Varnode::readonly, Range(entry.getSpace(), (uintb)(uint32_t)a, (uintb)(uint32_t)a + n - 1));
          ++nro;
        }
        continue;
      }
      const Varnode* out = op->getOut();
      if (out == nullptr || out->getSpace()->getType() != IPTR_INTERNAL) continue;
      try {
        if (op->numInput() == 1 && valueOf(op->getIn(0), a))
          consts[{out->getOffset(), out->getSize()}] = op->getOpcode()->evaluateUnary(out->getSize(), op->getIn(0)->getSize(), a);
        else if (op->numInput() == 2 && valueOf(op->getIn(0), a) && valueOf(op->getIn(1), b))
          consts[{out->getOffset(), out->getSize()}] = op->getOpcode()->evaluateBinary(out->getSize(), op->getIn(0)->getSize(), a, b);
      } catch (LowlevelError&) {}
    }
    printf("; literal pool: %d PC-relative loads marked read-only from raw p-code\n", nro);
  }
  auto t2 = std::chrono::steady_clock::now();
  int res = -1;
  try {
    arch.allacts.getCurrent()->reset(*fd);
    res = arch.allacts.getCurrent()->perform(*fd);
  } catch (LowlevelError& e) { printf("; decompile error: %s\n", e.explain.c_str()); return 4; }
  auto t3 = std::chrono::steady_clock::now();
  printf("; decompiled (result %d) in %lld ms\n", res,
         (long long)std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count());

  std::ostringstream out;
  arch.print->setOutputStream(&out);
  arch.print->docFunction(fd);
  printf("%s\n", out.str().c_str());

  // Markup pass: the same output with each token tagged by the p-code op's address --
  // this is what a pseudocode->assembly navigation needs.
  std::ostringstream xml;
  arch.print->setOutputStream(&xml);
  arch.print->setMarkup(true);
  if (xmlMarkup) arch.print->setPackedOutput(false);
  arch.print->docFunction(fd);
  arch.print->setMarkup(false);
  std::string x = xml.str();
  printf("; markup bytes: %zu\n", x.size());
  if (xmlMarkup) {
    std::map<uintm, uint32_t> opAddr;   // op "time" -> instruction address
    for (auto it = fd->beginOpMain(); it != fd->endOpMain(); ++it) opAddr[it->first.getTime()] = (uint32_t)it->second->getAddr().getOffset();
    std::istringstream in(x); Document* doc = xml_tree(in);
    std::vector<std::pair<std::string, std::set<uint32_t>>> lines(1);
    std::function<void(const Element*)> walk = [&](const Element* el) {
      const std::string& nm = el->getName();
      if (nm == "break") { lines.emplace_back(); int ind = atoi(el->getAttributeValue("indent").c_str()); lines.back().first.assign(ind, ' '); }
      else if (nm == "syntax" || nm == "op" || nm == "variable" || nm == "funcname" || nm == "type" || nm == "comment" || nm == "label" || nm == "field") {
        lines.back().first += el->getContent();
        if (nm != "syntax") { for (int i = 0; i < el->getNumAttributes(); ++i) if (el->getAttributeName(i) == "opref") {
          auto f = opAddr.find((uintm)strtoull(el->getAttributeValue(i).c_str(), nullptr, 0)); if (f != opAddr.end()) lines.back().second.insert(f->second); } }
      }
      for (const Element* c : el->getChildren()) walk(c);
    };
    walk(doc->getRoot()); delete doc;
    printf("; --- pseudocode lines with the SH-2 addresses their tokens came from ---\n");
    for (auto& l : lines) { if (l.first.empty()) continue; std::string as; for (uint32_t a : l.second) { char b[16]; snprintf(b, 16, "%08X ", a); as += b; }
      printf("%-50s ; %s\n", l.first.c_str(), as.c_str()); }
  }
  if (!errs.str().empty()) printf("; warnings: %s\n", errs.str().c_str());
  return 0;
}
