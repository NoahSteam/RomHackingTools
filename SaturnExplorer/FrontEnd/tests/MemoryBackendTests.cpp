// ContextBackend's write policy and source identity, against a real core context: a read-only
// backend must refuse an edit that was already in flight, a refusal from the emulator must reach
// the caller as "not written", and SourceId must move whenever the data behind an address does.
#include <cstdint>
#include <iostream>
#include <vector>

#include "Debug/MemoryBackend.h"
#include "FakeVdpSource.h"

using namespace sfe;

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what)
{
    if (ok) return;
    std::cerr << "FAIL: " << what << '\n';
    ++gFailures;
}

constexpr uint32_t kVdp1 = 0x05C00000u;

size_t gAccept = 2;
size_t AcceptSome(void*, se_vram_kind, uint32_t, const void*, size_t size)
{
    return gAccept < size ? gAccept : size;
}

se_context* Make(se_test::State& st, bool withSink)
{
    se_data_source ds = se_test::MakeSource(st);
    ds.capabilities |= SE_CAP_MEM_WRITE;
    if (withSink) ds.write_vram = AcceptSome;
    se_context* c = se_test::CreateContext(ds);
    if (c) se_begin_frame(c);
    return c;
}

uint8_t ReadByte(ContextBackend& b, uint32_t addr)
{
    return b.ReadMemoryBatch({ { addr, 1 } })[0].bytes[0];
}
}  // namespace

int main()
{
    const uint8_t ab[2] = { 0xAB, 0xCD };

    // Read-only is a write policy, not just a hint for the UI.
    {
        se_test::State st;
        se_context* ctx = Make(st, false);
        ContextBackend b(&ctx);
        Check(b.CanWrite(kVdp1), "writable by default");
        b.SetReadOnly(true);
        Check(!b.CanWrite(kVdp1), "SetReadOnly turns CanWrite off");
        Check(b.WriteMemory(kVdp1, ab, 2) == 0, "a read-only backend refuses an edit already in flight");
        Check(ReadByte(b, kVdp1) == 0x00, "and the data is untouched");
        b.SetReadOnly(false);
        Check(b.WriteMemory(kVdp1, ab, 2) == 2, "writes work again once it is writable");
        Check(ReadByte(b, kVdp1) == 0xAB, "and land");
        se_destroy(ctx);
    }

    // What the emulator refused is not reported as written, and is not shown.
    {
        se_test::State st;
        se_context* ctx = Make(st, true);
        ContextBackend b(&ctx);
        gAccept = 0;
        Check(b.WriteMemory(kVdp1, ab, 2) == 0, "a rejected write reports 0 bytes");
        Check(ReadByte(b, kVdp1) == 0x00, "and the view still shows the old byte");
        gAccept = 1;
        Check(b.WriteMemory(kVdp1, ab, 2) == 1, "a partial write reports what was accepted");
        Check(ReadByte(b, kVdp1) == 0xAB && ReadByte(b, kVdp1 + 1) == 0x00,
              "and only that byte changed");
        se_destroy(ctx);
    }

    // SourceId follows the context, and NoteSourceChanged covers a reused address / reloaded frame.
    {
        se_test::State st;
        se_context* a = Make(st, false);
        ContextBackend b(&a);
        const uint64_t idA = b.SourceId();
        Check(idA != 0, "a connected backend has an id");
        Check(b.SourceId() == idA, "stable while nothing changes");
        b.NoteSourceChanged();
        Check(b.SourceId() != idA, "NoteSourceChanged moves it");
        const uint64_t idA2 = b.SourceId();
        se_context* c2 = Make(st, false);
        se_context* saved = a;
        a = c2;
        Check(b.SourceId() != idA2, "a different context is a different source");
        a = saved;
        se_destroy(c2);
        se_destroy(a);
        a = nullptr;
        Check(b.SourceId() == 0, "no context, no source");
    }

    if (gFailures) { std::cerr << gFailures << " check(s) failed\n"; return 1; }
    std::cout << "MemoryBackendTests passed\n";
    return 0;
}
