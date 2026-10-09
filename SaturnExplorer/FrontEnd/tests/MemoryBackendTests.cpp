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

    // The VDP register windows are a snapshot-level scratchpad: fine on a loaded dump, never
    // offered on a source whose edits are meant to reach an emulator or a replay (they would go
    // nowhere and vanish on the next capture).
    {
        constexpr uint32_t kVdp2Reg = 0x05F80000u;
        se_test::State st;
        se_context* ctx = Make(st, true);
        ContextBackend b(&ctx);
        const uint8_t reg[2] = { 0x12, 0x34 };
        Check(b.CanWrite(kVdp2Reg), "registers are editable by default (a dump or savestate)");
        Check(b.WriteMemory(kVdp2Reg, reg, 2) == 2, "and the edit lands");
        b.SetRegistersReadOnly(true);
        Check(!b.CanWrite(kVdp2Reg) && !b.CanWrite(0x05D00000u), "marked read-only, neither register window is writable");
        Check(b.WriteMemory(kVdp2Reg, reg, 2) == 0, "and an edit already in flight is refused");
        Check(b.CanWrite(kVdp1), "memory is unaffected");
        Check(!b.WriteRefusal(kVdp2Reg).empty(), "the refusal has a reason the UI can show");
        Check(b.WriteRefusal(kVdp2Reg).find("live emulator") != std::string::npos, "that says why");
        se_destroy(ctx);
    }

    // Reasons for a refusal, and whether an accepted edit goes anywhere beyond the snapshot.
    {
        se_test::State st;
        se_context* ctx = Make(st, true);
        ContextBackend b(&ctx);
        Check(b.WriteReachesSource(kVdp1), "a region with a sink reaches the source");
        Check(!b.WriteReachesSource(0x05F80000u), "a register edit never does");
        b.SetReadOnly(true, "state load in flight");
        Check(b.WriteRefusal(kVdp1) == "state load in flight", "a forced read-only carries the reason it was given");
        b.SetReadOnly(false);
        Check(b.WriteRefusal(0x05F00000u).find("VDP writer") != std::string::npos,
              "a refused CRAM edit says the emulator needs the VDP writer");
        Check(b.WriteRefusal(0x04000000u).find("not in a captured region") != std::string::npos,
              "an unmapped address says so");
        se_destroy(ctx);

        se_test::State st2;
        se_context* snap = Make(st2, false);
        ContextBackend b2(&snap);
        Check(!b2.WriteReachesSource(kVdp1), "with no sink an edit changes the snapshot only");
        se_destroy(snap);
        ContextBackend none(nullptr);
        Check(!none.WriteRefusal(kVdp1).empty() && !none.WriteReachesSource(kVdp1), "no source: refused, and says so");
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
