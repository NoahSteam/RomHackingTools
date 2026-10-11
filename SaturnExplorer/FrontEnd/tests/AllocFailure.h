// Allocation-failure injection for the decompiler tests: replaces the global operator new of the
// test executable (include this header in exactly one of its translation units) so that the
// Nth allocation from now throws std::bad_alloc, once. Lets a test prove that an out-of-memory
// anywhere it can reach is reported rather than ending the process.
#pragma once

#include <atomic>
#include <cstdlib>
#include <new>

namespace dts
{

// MSVC's Debug STL allocates its iterator-debugging proxies even inside noexcept operations
// (container moves), so a failure injected there is std::terminate by design of that library,
// not a defect of the code under test. The injection cases skip themselves in that build; the
// Windows Release run covers them.
#if defined(_MSC_VER) && defined(_DEBUG)
const bool kInjectionUsable = false;
#else
const bool kInjectionUsable = true;
#endif

// -1: off. N >= 0: the allocation after N more succeed throws, and injection turns off.
inline std::atomic<long>& FailAfter()
{
    static std::atomic<long> n(-1);
    return n;
}

// Allocations counted while > 0 is wanted (for sizing a sweep); reset by the test.
inline std::atomic<long>& AllocCount()
{
    static std::atomic<long> n(0);
    return n;
}

inline void* Allocate(std::size_t size)
{
    AllocCount().fetch_add(1, std::memory_order_relaxed);
    long k = FailAfter().load();
    while (k >= 0)
    {
        if (FailAfter().compare_exchange_weak(k, k - 1))
        {
            if (k == 0)
            {
                FailAfter() = -1;
                throw std::bad_alloc();
            }
            break;
        }
    }
    void* p = std::malloc(size ? size : 1);
    if (!p) throw std::bad_alloc();
    return p;
}

}  // namespace dts

void* operator new(std::size_t size) { return dts::Allocate(size); }
void* operator new[](std::size_t size) { return dts::Allocate(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
