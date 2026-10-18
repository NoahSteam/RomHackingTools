// Area 2 round 12 review: the CircleRange-level fixtures of the ledger rows (build: build_tool.sh fixtures12.cpp <out> <build dir> <SaturnExplorer dir>)
#include "libdecomp.hh"
#include "rangeutil.hh"
#include <cstdio>
using namespace ghidra;
static void show(const char *what, const CircleRange &q) { printf("%s: [%llx,%llx) step %d mask %llx%s\n", what, (unsigned long long)q.getMin(), (unsigned long long)q.getEnd(), q.getStep(), (unsigned long long)q.getMask(), q.isEmpty() ? " EMPTY" : ""); }
int main() {
  // A2-73: intersect of ranges of two masks, the wider one holds only values above the narrower one's mask and ends at the top (right bound 0 or the phase)
  { CircleRange a(0, 0x10, 1, 1), b(0x8000, 0, 2, 1); int rc = a.intersect(b); printf("rc %d ", rc); show("[0,0x10) 1 byte  ^  [0x8000,0) 2 bytes (empty expected)", a); }
  { CircleRange a(0xf8, 0xf4, 1, 4), b(0x7ffb, 0xfffe, 2, 1); int rc = a.intersect(b); printf("rc %d ", rc); show("[f8,f4)/4 1 byte  ^  [7ffb,fffe) 2 bytes (empty expected)", a); }
  { CircleRange a(0x1, 0x9, 1, 8), b(0x801, 0xffff, 2, 1); int rc = a.intersect(b); printf("rc %d ", rc); show("[1,9)/8 1 byte  ^  [801,ffff) 2 bytes (empty expected)", a); }
  // A2-74: setNZMask for a mask whose lowest set bit is 31 or more: the step is an int4 shift
  { CircleRange q; bool ok = q.setNZMask(0x80000000u, 4); printf("setNZMask(0x80000000, 4) -> %d ", ok); show("", q); }
  { CircleRange q; bool ok = q.setNZMask(1ull << 63, 8); printf("setNZMask(1<<63, 8) -> %d ", ok); show("", q); }
  { CircleRange q; bool ok = q.setNZMask(1ull << 40, 8); printf("setNZMask(1<<40, 8) -> %d ", ok); show("", q); }
  // what the negative step does in the intersect that pullBack(usenzmask) makes with it
  { CircleRange nz; nz.setNZMask(0x80000000u, 4);
    { CircleRange r(0, 5, 4, 1); int rc = r.intersect(nz); printf("[0,5) ^ setNZMask(0x80000000): rc %d ", rc); show("", r); }
    { CircleRange r(2, 10, 4, 4); int rc = r.intersect(nz); printf("[2,10)/4 ^ setNZMask(0x80000000): rc %d ", rc); show("", r); }
    { CircleRange r(0x80000000u, 0x80000001u, 4, 1); int rc = r.intersect(nz); printf("[0x80000000,+1) ^ setNZMask(0x80000000): rc %d ", rc); show("", r); }
    { CircleRange r(0, 16, 4, 4); int rc = r.intersect(nz); printf("[0,16)/4 ^ setNZMask(0x80000000): rc %d ", rc); show("", r); } }
  // A2-75: widen leaves the bounds of a strided range in different phases
  { CircleRange a(0x34, 0x44, 1, 16), b(0xfa, 0xf9, 1, 1); a.widen(b, false); show("[34,44)/16 widened to [fa,f9) at the right (left unstable)", a); printf("   contains 0x34: %d (it held it before)\n", (int)a.contains(0x34)); }
  { CircleRange a(0x0e, 0x6e, 1, 8), b(0, 0, 1, 2); a.widen(b, false); show("[e,6e)/8 widened to the evens (left unstable)", a); printf("   contains 0xe: %d\n", (int)a.contains(0xe)); }
  return 0;
}
