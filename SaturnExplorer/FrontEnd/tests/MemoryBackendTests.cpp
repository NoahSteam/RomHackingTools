// ContextBackend's write policy and source identity, against a real core context: a read-only
// backend must refuse an edit that was already in flight, a refusal from the emulator must reach
// the caller as "not written", and SourceId must move whenever the data behind an address does.
#include <cstdint>
#include <iostream>
#include <vector>

#include "Debug/MemoryBackend.h"
#include "Debug/PokeReport.h"
#include "FakeVdpSource.h"
#include "Vdp1SpriteMove.h"

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

void TestSpriteMove()
{
    // Exercise the actual parser, geometry and memory backend for every drawable
    // command, including all sixteen combinations of scaled-sprite zoom axes.
    std::vector<uint16_t> controls = { 0, 2, 3, 4, 5, 6 };
    for (int zoom = 0; zoom < 16; ++zoom) controls.push_back(static_cast<uint16_t>(1 | (zoom << 8)));
    for (uint16_t ctrl : controls)
    {
        se_test::State st(0x200);
        se_test::WriteSystemClip(st, 320, 224);
        se_test::PutBE16(st.vdp1, 0x20, 0x000A); // local origin must stay intact
        se_test::PutBE16(st.vdp1, 0x2C, 5);
        se_test::PutBE16(st.vdp1, 0x2E, 7);
        se_test::PutBE16(st.vdp1, 0x40, ctrl);
        se_test::PutBE16(st.vdp1, 0x44, 0x00E8); // RGB555, no end/transparent codes
        se_test::PutBE16(st.vdp1, 0x46, 0xFFFF);
        se_test::PutBE16(st.vdp1, 0x48, 0x20);   // texture at byte 0x100
        se_test::PutBE16(st.vdp1, 0x4A, 0x0108); // 8x8
        for (int i = 0; i < 8; ++i)
            se_test::PutBE16(st.vdp1, 0x4C + i * 2, static_cast<uint16_t>(10 + i * 10));
        se_test::PutBE16(st.vdp1, 0x60, 0x8000);
        std::fill(st.vdp1.begin() + 0x100, st.vdp1.end(), 0xFF);
        se_test::SpritesInFront(st);
        se_context* ctx = Make(st, false);
        Check(ctx != nullptr, "sprite move context created");
        if (!ctx) continue;
        se_sprite_2d before {}, after {};
        Check(se_get_sprite_2d(ctx, 0, &before) == SE_OK, "original sprite has geometry");
        auto render = [&]() {
            se_render_opts opts {};
            se_default_render_opts(&opts);
            se_image image {};
            size_t needed = 0;
            Check(se_render_frame(ctx, &opts, &image, &needed) == SE_OK, "movement frame size available");
            std::vector<uint8_t> pixels(needed);
            image.pixels = pixels.data(); image.capacity = pixels.size();
            Check(se_render_frame(ctx, &opts, &image, &needed) == SE_OK, "movement frame renders");
            return pixels;
        };
        if (ctrl == 0)
            Check(render()[(27 * 320 + 15) * 4] == 255, "sprite initially draws at the original position");
        Vdp1CommandBytes original {};
        se_read_vram(ctx, SE_VRAM_KIND_VDP1_VRAM, 0x40, original.data(), original.size());
        const auto moved = MoveVdp1Command(original, 12, -9);
        ContextBackend backend(&ctx);
        const auto serial = se_derive_serial(ctx);
        Check(backend.WriteMemory(kVdp1 + 0x4C, moved.data() + 0x0C, 0x10) == 0x10,
              "sprite movement writes the coordinate block");
        Check(se_derive_serial(ctx) != serial, "movement invalidates the rendered output");
        Check(se_get_sprite_2d(ctx, 0, &after) == SE_OK, "moved sprite has geometry");
        for (int corner = 0; corner < 4; ++corner)
        {
            Check(after.corners[corner].x == before.corners[corner].x + 12,
                  "every rendered corner moves horizontally without changing shape");
            Check(after.corners[corner].y == before.corners[corner].y - 9,
                  "every rendered corner moves vertically without changing shape");
        }
        se_command command {};
        se_get_command(ctx, 2, &command);
        Check(command.x == 22 && command.y == 11, "Command List sees the updated position");
        Vdp1CommandBytes actual {};
        se_read_vram(ctx, SE_VRAM_KIND_VDP1_VRAM, 0x40, actual.data(), actual.size());
        Check(actual == moved, "VDP1 Table sees the exact updated RAM bytes");
        Check(std::equal(original.begin(), original.begin() + 0x0C, actual.begin()) &&
              std::equal(original.begin() + 0x1C, original.end(), actual.begin() + 0x1C),
              "sprite attributes, texture address and shading are preserved");
        if (ctrl == 0)
        {
            const auto pixels = render();
            Check(pixels[(18 * 320 + 27) * 4] == 255, "VDP Output draws the moved sprite");
            Check(pixels[(27 * 320 + 15) * 4] == 0, "VDP Output clears the sprite's previous position");
        }
        se_destroy(ctx);
    }
    Vdp1CommandBytes limits {};
    limits[1] = 4; // polygon
    limits[0x0C] = 0x0F; limits[0x0D] = 0xFE; // A.x = 4094
    limits[0x10] = 0x0F; limits[0x11] = 0xFF; // B.x = 4095
    const auto clamped = MoveVdp1Command(limits, 100, -9000);
    Check(Vdp1MoveCoordinate(clamped, 0x0C) == 4094 &&
          Vdp1MoveCoordinate(clamped, 0x10) == 4095,
          "coordinate limits clamp the entire shape, never individual corners");
    Check(Vdp1MoveCoordinate(clamped, 0x0E) == -4096, "negative coordinate limit is signed 13-bit");
    for (uint16_t ctrl : { 0x8000, 0x4000, 8, 9, 10, 15 })
    {
        Vdp1CommandBytes bytes {};
        bytes[0] = static_cast<uint8_t>(ctrl >> 8); bytes[1] = static_cast<uint8_t>(ctrl);
        Check(MoveVdp1Command(bytes, 10, 20) == bytes, "state, skipped and end commands cannot move");
    }
    Vdp1MoveGesture gesture;
    Check(!gesture.Update(false, true, 50, 60), "mouse motion alone never moves a sprite");
    Check(gesture.Update(true, true, 50, 60) && gesture.active, "Shift starts from the current cursor");
    Check(Vdp1MoveGesture::Delta(70, gesture.startX, 0.5f) == 10,
          "hi-res and scaled views map cursor distance into VDP1 coordinates");
    Check(Vdp1MoveGesture::Delta(50.4f, gesture.startX, 1) == 0 &&
          Vdp1MoveGesture::Delta(50.8f, gesture.startX, 1) == 1,
          "fractional motion accumulates from the anchor without rounding drift");
    gesture.Update(true, false, 70, 80);
    Check(!gesture.active, "clicks, loss of hover or source changes stop movement");
    Check(!gesture.Update(true, true, 70, 80), "holding Shift never restarts a cancelled move");
    gesture.Update(false, true, 70, 80);
    Check(gesture.Update(true, true, 70, 80), "a fresh Shift press starts a new move");
    gesture.Update(false, true, 70, 80);
    Check(!gesture.active, "releasing Shift stops movement");
}
}  // namespace

int main()
{
    TestSpriteMove();
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
        Check(b.WriteDestination(kVdp1) == IMemoryBackend::WriteDest::ViewOnly,
              "a sink alone is not an emulator: a savestate's work-RAM sink edits its own copy");
        b.SetEditSink(IMemoryBackend::WriteDest::Emulator);
        Check(b.WriteDestination(kVdp1) == IMemoryBackend::WriteDest::Emulator, "a live source's sink reaches the emulator");
        Check(b.WriteDestination(0x05F80000u) == IMemoryBackend::WriteDest::ViewOnly, "a register edit never does");
        b.SetEditSink(IMemoryBackend::WriteDest::Staged);
        Check(b.WriteDestination(kVdp1) == IMemoryBackend::WriteDest::Staged,
              "on a scrubbed frame the sink is the replay list: staged, not sent");
        b.SetEditSink(IMemoryBackend::WriteDest::ViewOnly);
        b.SetReadOnly(true, "state load in flight");
        Check(b.WriteRefusal(kVdp1) == "state load in flight", "a forced read-only carries the reason it was given");
        b.SetReadOnly(false);
        Check(b.WriteRefusal(0x05F00000u).find("VDP writer") != std::string::npos,
              "a refused CRAM edit says the emulator needs the VDP writer");
        Check(b.WriteRefusal(0x04000000u).find("not in a captured region") != std::string::npos,
              "an unmapped address says so");
        // Without a VDP writer, CRAM and the frame buffer are not offered -- and an edit already in
        // flight is refused too -- while VRAM stays writable.
        const uint8_t one = 0x11;
        Check(b.CanWrite(0x05F00000u) && b.CanWrite(0x05C80000u), "CRAM and the FB are editable by default");
        b.SetVdpWindowsWritable(false);
        Check(!b.CanWrite(0x05F00000u) && !b.CanWrite(0x05C80000u), "not without a VDP writer");
        Check(b.WriteMemory(0x05F00000u, &one, 1) == 0, "and a write there is refused, as CanWrite says");
        Check(b.WriteRefusal(0x05F00000u).find("VDP writer") != std::string::npos, "with the reason");
        Check(b.CanWrite(kVdp1), "VRAM is unaffected");
        b.SetVdpWindowsWritable(true);
        se_destroy(ctx);

        se_test::State st2;
        se_context* snap = Make(st2, false);
        ContextBackend b2(&snap);
        b2.SetEditSink(IMemoryBackend::WriteDest::Emulator);
        Check(b2.WriteDestination(kVdp1) == IMemoryBackend::WriteDest::ViewOnly, "with no sink an edit changes the snapshot only");
        se_destroy(snap);
        ContextBackend none(nullptr);
        Check(!none.WriteRefusal(kVdp1).empty() && none.WriteDestination(kVdp1) == IMemoryBackend::WriteDest::ViewOnly,
              "no source: refused, and says so");
    }

    // What the user is told when an emulator edit may not have landed. An edit whose reply was lost
    // may well have been applied, so it is reported as uncertain -- never as "never reached".
    {
        Check(DescribePokeLoss(0, 0, 0).empty(), "nothing lost, nothing said");
        const std::string unsure = DescribePokeLoss(0, 0, 1);
        Check(unsure.find("could not be confirmed") != std::string::npos, "an unconfirmed edit says it is unconfirmed");
        Check(unsure.find("may or may not") != std::string::npos, "and that the emulator may have it");
        Check(unsure.find("never reached") == std::string::npos && unsure.find("does not have") == std::string::npos,
              "and does not claim it is missing");
        const std::string sure = DescribePokeLoss(1, 2, 0);
        Check(sure.find("2 never reached") != std::string::npos && sure.find("1 could not be written") != std::string::npos &&
                  sure.find("does not have it") != std::string::npos, "definite losses are said as such");
        const std::string both = DescribePokeLoss(0, 1, 1);
        Check(both.find("1 never reached") != std::string::npos && both.find("1 could not be confirmed") != std::string::npos &&
                  both.find("apart from the unconfirmed") != std::string::npos, "a mix keeps the two apart");
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
