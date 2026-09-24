# 01 — ABI & Lifecycle

## ABI-01 — High — `ReadBE16` overflow can bypass bounds checking

`Core/src/ByteOrder.h` checks `off + 1 >= mem.size()`. Because `off` is `uint32_t`, `UINT32_MAX + 1` wraps to zero and a non-empty vector can pass the check before `mem[off]` is read.

This is reachable from public texture/palette decode APIs that accept raw addresses.

**Fix:** use `if (off >= mem.size() || mem.size() - off < 2) return 0;` and centralize checked-span reads.

## ABI-02 — Medium — C++ exceptions may cross the C ABI

Several exported `extern "C"` entry points call code that allocates STL containers without a catch boundary. An OOM can therefore unwind across the C seam.

**Fix:** no-throw wrappers around exported entry points, convert `std::bad_alloc` to a result code, and impose practical byte budgets.

## ABI-03 — Medium — public search/history ABI is stale

ROM search/history functions remain exposed at the core boundary while current implementations live in frontend systems or are stubs.

**Fix:** implement them behind the core ABI or formally deprecate/remove them in the next ABI revision.
