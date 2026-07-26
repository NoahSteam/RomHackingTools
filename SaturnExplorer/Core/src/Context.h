// Context — the C++ core object behind the opaque se_context* handle. The C ABI
// shim in HostAbi.cpp is a thin translation layer over this class; all real
// logic lives on the C++ side (ARCHITECTURE.md §3, the hybrid boundary).
#pragma once

#include <algorithm>
#include <cstring>
#include <vector>

#include "saturnexplorer/SeHost.h"
#include "HardwareSnapshot.h"
#include "Vdp1Parser.h"
#include "GeometryBuilder.h"
#include "Vdp1Rasterizer.h"
#include "Vdp2Compositor.h"
#include "Vdp1Color.h"
#include "ByteOrder.h"

namespace se
{

class Context
{
public:
    Context(const se_data_source& dataSource, const se_config& config)
        : mDs(dataSource), mCfg(config)
    {
    }

    ~Context()
    {
        if (mDs.close)
        {
            mDs.close(mDs.user);
        }
    }

    // Snapshot state for the current frame, then parse commands + build geometry.
    se_result BeginFrame()
    {
        if (!mSnapshot.Capture(mDs))
        {
            return SE_ERR_NO_DATA;
        }
        Vdp1Parser::Parse(mSnapshot.Vdp1Vram(), mCommands);
        GeometryBuilder::Build(mSnapshot.Vdp1Vram(), mScene);
        ApplyDisplayResolution();
        ResolveSpritePriorities();
        BuildVramRegions();
        return SE_OK;
    }

    // --- Query surface. ---

    size_t CommandCount() const { return mCommands.size(); }

    se_result GetCommand(size_t index, se_command* out) const
    {
        if (index >= mCommands.size())
        {
            return SE_ERR_OUT_OF_RANGE;
        }
        *out = mCommands[index];
        return SE_OK;
    }

    size_t SpriteCount() const { return mScene.sprites.size(); }

    se_result GetSprite2d(size_t index, se_sprite_2d* out) const
    {
        if (index >= mScene.sprites.size())
        {
            return SE_ERR_OUT_OF_RANGE;
        }
        *out = mScene.sprites[index];
        return SE_OK;
    }

    se_result GetSprite3d(size_t index, se_sprite_3d* out) const
    {
        if (index >= mScene.sprites3d.size())
        {
            return SE_ERR_OUT_OF_RANGE;
        }
        *out = mScene.sprites3d[index];
        return SE_OK;
    }

    // Render the composited 2D frame into a scene-sized image. VDP1 sprites and
    // VDP2 NBG screens interleave by priority: for each priority level 0..7 the
    // VDP2 layers at that priority are drawn, then the VDP1 sprites at that
    // priority (sprites in front of same-priority NBGs, per hardware). Each
    // sprite's priority is resolved from its color data + the sprite-priority
    // registers (ResolveSpritePriorities). Empty pixels get an opaque backdrop.
    // See ARCHITECTURE.md §7.
    se_result RenderFrame(const se_render_opts& opts, se_image* out, size_t* needed)
    {
        const int w = mScene.screenWidth;
        const int h = mScene.screenHeight;
        return FillImage(static_cast<uint32_t>(w), static_cast<uint32_t>(h), out, needed, [&]
        {
            const size_t n = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
            mRenderBuffer.assign(n, 0);   // transparent; layers composite over it
            // Lay the real VDP2 back screen first, so it's the opaque surface the
            // priority-ordered layers (and color calculation) composite over. A no-op
            // without VDP2 regs — FillBackdrop below still covers that case.
            Vdp2Compositor::RenderBackScreen(mSnapshot, w, h, mRenderBuffer);
            for (int p = 0; p <= 7; ++p)
            {
                if (p >= 1)   // NBG priority 0 = not displayed
                {
                    Vdp2Compositor::Render(mSnapshot, opts, w, h, mRenderBuffer, p, p, false);
                }
                Vdp1Rasterizer::Render(mScene, mSnapshot.Vdp1Vram(), mSnapshot.Cram(),
                                       mSnapshot.CramMode(), opts, mRenderBuffer, p, p, false);
            }
            FillBackdrop();
        });
    }

    // Render the exploded 3D view from 'camera' into a viewport-sized image.
    se_result Render3D(const se_camera3d& camera, const se_render_opts& opts,
                       se_image* out, size_t* needed)
    {
        return FillImage(camera.viewport_width, camera.viewport_height, out, needed, [&]
        {
            Vdp1Rasterizer::Render3D(mScene, mSnapshot.Vdp1Vram(), mSnapshot.Cram(),
                                     mSnapshot.CramMode(), camera, opts, mRenderBuffer,
                                     mDepthBuffer);
        });
    }

    // Decode a texture straight out of VDP1 VRAM into an RGBA image (index-0 and
    // RGB-code-0 left transparent, so the viewer can show what is see-through).
    se_result DecodeTexture(const se_texture_ref& ref, se_image* out, size_t* needed)
    {
        if (ref.width == 0 || ref.height == 0)
        {
            return SE_ERR_INVALID_ARG;
        }
        return FillImage(ref.width, ref.height, out, needed, [&]
        {
            const std::vector<uint8_t>& vram = mSnapshot.Vdp1Vram();
            const std::vector<uint8_t>& cram = mSnapshot.Cram();
            const se_cram_mode cm = mSnapshot.CramMode();
            const uint32_t w = ref.width;
            const uint32_t h = ref.height;
            mRenderBuffer.assign(static_cast<size_t>(w) * h * 4, 0);
            for (uint32_t y = 0; y < h; ++y)
            {
                for (uint32_t x = 0; x < w; ++x)
                {
                    const Rgba c = DecodeTexel(vram, cram, cm, ref.color_mode,
                                               ref.vram_address, static_cast<uint16_t>(w),
                                               static_cast<int>(x), static_cast<int>(y),
                                               ref.palette_bank, ref.clut_address, false);
                    const size_t o = (static_cast<size_t>(y) * w + x) * 4;
                    mRenderBuffer[o + 0] = c.r;
                    mRenderBuffer[o + 1] = c.g;
                    mRenderBuffer[o + 2] = c.b;
                    mRenderBuffer[o + 3] = c.a;
                }
            }
        });
    }

    // Decode a 16-entry VDP1 color-lookup table (LUT mode) at 'clutAddress' into a
    // palette. Each entry is either a literal RGB555 color or, with bit 15 set, a
    // VDP2 color-bank code resolved through CRAM (same rule as the texel decoder).
    se_result DecodePalette(uint32_t clutAddress, se_palette* out) const
    {
        const std::vector<uint8_t>& vram = mSnapshot.Vdp1Vram();
        const std::vector<uint8_t>& cram = mSnapshot.Cram();
        const se_cram_mode cm = mSnapshot.CramMode();

        out->clut_address = clutAddress;
        out->mode = cm;
        out->count = 16;
        for (uint16_t i = 0; i < 16; ++i)
        {
            const uint16_t raw = ReadBE16(vram, clutAddress + i * 2);
            const Rgba c = (raw & 0x8000) ? CramColor(cram, cm, raw & 0x7FF)
                                          : Rgb555ToRgba(raw);
            se_palette_entry& e = out->entries[i];
            e.r = c.r;
            e.g = c.g;
            e.b = c.b;
            e.a = 255;
            e.raw = raw;
        }
        return SE_OK;
    }

    // Sprites that read the same texture as 'ref' (matched by VDP1 VRAM source
    // address). Fills up to 'max' into 'out' and returns the TOTAL match count, so
    // a caller can grow its buffer if the count exceeds 'max'. Pass out == nullptr
    // to only count.
    size_t ReferencesOfTexture(const se_texture_ref& ref, se_reference* out, size_t max) const
    {
        const uint32_t addr = ref.vram_address;
        return CollectReferences(out, max, [addr](const se_command& c)
        {
            return c.texture_address == addr;
        });
    }

    // Sprites that read the same CLUT (LUT-mode palette) at 'clutAddress'.
    size_t ReferencesOfPalette(uint32_t clutAddress, se_reference* out, size_t max) const
    {
        return CollectReferences(out, max, [clutAddress](const se_command& c)
        {
            return c.color_mode == SE_COLOR_LUT_16 && c.clut_address == clutAddress;
        });
    }

    // Decode the CRAM sub-palette a color-bank sprite indexes into. 'colorBank'
    // is CMDCOLR; the color mode fixes how many CRAM entries the bank spans
    // (16/64/128/256). LUT and RGB555 modes have no bank palette.
    se_result DecodeBankPalette(uint16_t colorBank, se_color_mode mode, se_palette* out) const
    {
        uint32_t base;
        uint16_t count;
        switch (mode)
        {
        case SE_COLOR_BANK_16:  base = colorBank & 0xFFF0u; count = 16;  break;
        case SE_COLOR_BANK_64:  base = colorBank & 0xFFC0u; count = 64;  break;
        case SE_COLOR_BANK_128: base = colorBank & 0xFF80u; count = 128; break;
        case SE_COLOR_BANK_256: base = colorBank & 0xFF00u; count = 256; break;
        default:                return SE_ERR_UNSUPPORTED;
        }

        const std::vector<uint8_t>& cram = mSnapshot.Cram();
        const se_cram_mode cm = mSnapshot.CramMode();
        const uint32_t words = (cm == SE_CRAM_RGB888_1024)
                                   ? static_cast<uint32_t>(cram.size() / 4)
                                   : static_cast<uint32_t>(cram.size() / 2);

        out->clut_address = 0;
        out->mode = cm;
        out->count = count;
        for (uint16_t i = 0; i < count; ++i)
        {
            const uint32_t idx = base + i;
            const Rgba c = CramColor(cram, cm, idx);
            se_palette_entry& e = out->entries[i];
            e.r = c.r;
            e.g = c.g;
            e.b = c.b;
            e.a = 255;
            e.raw = (cm == SE_CRAM_RGB888_1024 || words == 0)
                        ? 0
                        : ReadBE16(cram, (idx & (words - 1)) * 2);
        }
        return SE_OK;
    }

    // --- Raw inspection surface (register / VRAM / CRAM viewers). ---

    bool HasVdp1Regs() const { return mSnapshot.HasVdp1Regs(); }
    bool HasVdp2Regs() const { return mSnapshot.HasVdp2Regs(); }
    se_cram_mode CramMode() const { return mSnapshot.CramMode(); }
    uint16_t Vdp1Register(uint32_t hw) const { return mSnapshot.Vdp1Reg(hw); }
    uint16_t Vdp2Register(uint32_t hw) const { return mSnapshot.Vdp2Reg(hw); }

    // Copy raw bytes from a memory region (as the core holds them: Saturn-native
    // big-endian). Returns the number of bytes copied (clamped to the region).
    size_t ReadVram(se_vram_kind kind, uint32_t offset, void* dst, size_t size) const
    {
        const std::vector<uint8_t>* src = nullptr;
        switch (kind)
        {
        case SE_VRAM_KIND_VDP1_VRAM: src = &mSnapshot.Vdp1Vram(); break;
        case SE_VRAM_KIND_VDP2_VRAM: src = &mSnapshot.Vdp2Vram(); break;
        case SE_VRAM_KIND_CRAM:      src = &mSnapshot.Cram();      break;
        case SE_VRAM_KIND_WRAM_LOW:  src = &mSnapshot.WramLow();   break;
        case SE_VRAM_KIND_WRAM_HIGH: src = &mSnapshot.WramHigh();  break;
        case SE_VRAM_KIND_VDP1_FB:   src = &mSnapshot.Vdp1Fb();    break;
        default: return 0;
        }
        if (!dst || offset >= src->size())
        {
            return 0;
        }
        const size_t avail = src->size() - offset;
        const size_t n = size < avail ? size : avail;
        std::memcpy(dst, src->data() + offset, n);
        return n;
    }

    // Write raw big-endian bytes into a region. Updates the snapshot (so the edit
    // shows immediately) and forwards work-RAM writes to the source's
    // write_main_ram (so a live emulator is poked). Returns bytes written.
    size_t WriteVram(se_vram_kind kind, uint32_t offset, const void* src, size_t size)
    {
        const size_t n = mSnapshot.WriteRegion(kind, offset, src, size);
        if (n == 0) return 0;
        if (mDs.write_main_ram &&
            (kind == SE_VRAM_KIND_WRAM_LOW || kind == SE_VRAM_KIND_WRAM_HIGH))
        {
            const uint32_t base = (kind == SE_VRAM_KIND_WRAM_HIGH) ? kWramHighBase
                                                                   : kWramLowBase;
            mDs.write_main_ram(mDs.user, base + offset, src, n);
        }
        return n;
    }

    // The Hex Editor can edit whenever a snapshot is loaded (savestate edits are
    // in-memory; live edits persist through write_main_ram).
    bool CanWrite() const { return mSnapshot.Valid(); }

    // Decode CRAM entries [start, start+count) into RGBA palette entries. Returns
    // the number written (clamped to the CRAM size for the current color mode).
    size_t ReadCramColors(uint16_t start, uint16_t count, se_palette_entry* out) const
    {
        if (!out)
        {
            return 0;
        }
        const std::vector<uint8_t>& cram = mSnapshot.Cram();
        const se_cram_mode cm = mSnapshot.CramMode();
        const uint32_t entries = (cm == SE_CRAM_RGB888_1024)
                                     ? static_cast<uint32_t>(cram.size() / 4)
                                     : static_cast<uint32_t>(cram.size() / 2);
        size_t written = 0;
        for (uint16_t i = 0; i < count; ++i)
        {
            const uint32_t idx = static_cast<uint32_t>(start) + i;
            if (idx >= entries)
            {
                break;
            }
            const Rgba c = CramColor(cram, cm, idx);
            se_palette_entry& e = out[written++];
            e.r = c.r;
            e.g = c.g;
            e.b = c.b;
            e.a = 255;
            e.raw = (cm == SE_CRAM_RGB888_1024) ? 0 : ReadBE16(cram, idx * 2);
        }
        return written;
    }

    // Topmost sprite (last drawn) containing the screen point, if any.
    se_result HitTest(int x, int y, size_t* outCommandIndex) const
    {
        // The click is in display space; sprite corners are in VDP1 space, which is
        // narrower in hi-res (the rasterizer scales sprite X up to display). Map back.
        const float xScale = (mScene.screenWidth > 0)
                                 ? static_cast<float>(mScene.vdp1Width) / mScene.screenWidth : 1.0f;
        const float sx = (x + 0.5f) * xScale;
        for (size_t i = mScene.sprites.size(); i-- > 0; )
        {
            if (PointInSprite(mScene.sprites[i], sx, y + 0.5f))
            {
                *outCommandIndex = mScene.sprites[i].command_index;
                return SE_OK;
            }
        }
        return SE_ERR_NO_DATA;
    }

    // Topmost 3D sprite under the screen point for 'camera' (matches the 3D view).
    se_result HitTest3D(const se_camera3d& camera, int x, int y,
                        size_t* outCommandIndex) const
    {
        uint32_t cmd = 0;
        if (Vdp1Rasterizer::HitTest3D(mScene, camera, x, y, &cmd))
        {
            *outCommandIndex = cmd;
            return SE_OK;
        }
        return SE_ERR_NO_DATA;
    }

    size_t VramRegionCount() const { return mVramRegions.size(); }

    se_result GetVramRegion(size_t index, se_vram_region* out) const
    {
        if (index >= mVramRegions.size())
        {
            return SE_ERR_OUT_OF_RANGE;
        }
        *out = mVramRegions[index];
        return SE_OK;
    }

    bool HasSnapshot() const { return mSnapshot.Valid(); }

    bool HasSh2Regs(int cpu) const { return mSnapshot.HasSh2Regs(cpu); }
    se_result GetSh2Regs(int cpu, se_sh2_regs* out) const
    {
        if (!out || !mSnapshot.HasSh2Regs(cpu))
        {
            return SE_ERR_NO_DATA;
        }
        *out = mSnapshot.Sh2Regs(cpu);
        return SE_OK;
    }

    const se_data_source& DataSource() const { return mDs; }
    const se_config& Config() const { return mCfg; }

private:
    // Set the frame dimensions from VDP2 TVMD (HRES/VRES), the authoritative display
    // resolution, when VDP2 registers are present. GeometryBuilder only sees VDP1 and
    // defaults to 320x224 (overridden solely by a VDP1 system-clip command), so a pure
    // VDP2 scene — or one whose clip differs from the TV mode — otherwise renders at the
    // wrong width and looks horizontally stretched. The *full* dot count matters, not just
    // the 320-vs-352 base: the compositor samples one background column per output column,
    // so rendering fewer columns than the mode has crops the field of view (e.g. showing
    // only the left half of a 704-wide hi-res screen, stretched 2x to fill the frame).
    void ApplyDisplayResolution()
    {
        if (!mSnapshot.HasVdp2Regs())
        {
            return;
        }
        const uint16_t tvmd = mSnapshot.Vdp2Reg(0x000);
        const uint32_t hres = tvmd & 0x7;
        const bool hiRes = (hres & 0x2) != 0;   // 640/704 — VDP1 draws at half this width
        // The VDP1 system clip is authoritative for the display in normal-res scenes (and
        // it's what the compositor tests use as a fixture). In hi-res, though, the clip is
        // the *half-width* VDP1 area (e.g. 352) while VDP2 scans out at the full TVMD dot
        // count (704), so the TVMD width must win or the backgrounds render half the
        // field of view. mScene.vdp1Width keeps the VDP1 coordinate space either way.
        if (mScene.hasSystemClip && !hiRes)
        {
            return;
        }
        int w = (hres & 0x1) ? 352 : 320;   // HRES bit 0: 352 vs 320 base
        if (hiRes) w *= 2;                  // HRES bit 1: hi-res (640 / 704)
        static const int kVRes[4] = { 224, 240, 256, 256 };
        int h = kVRes[(tvmd >> 4) & 0x3];   // VRES bits 4-5
        if (((tvmd >> 6) & 0x3) == 0x3) h *= 2;   // LSMD: double-density interlace
        mScene.screenWidth = w;
        mScene.screenHeight = h;
    }

    // Walk the drawable sprites (the same filter and object-number order
    // GeometryBuilder uses) and, for each one matching 'match', emit an
    // se_reference. Writes at most 'max' entries; returns the total match count.
    template <typename Match>
    size_t CollectReferences(se_reference* out, size_t max, Match&& match) const
    {
        size_t total = 0;
        uint32_t objectNumber = 0;
        for (const se_command& c : mCommands)
        {
            const bool textured = (c.type == SE_CMD_NORMAL_SPRITE ||
                                   c.type == SE_CMD_SCALED_SPRITE ||
                                   c.type == SE_CMD_DISTORTED_SPRITE);
            if (!textured || c.status == SE_CMDSTAT_SKIP || c.width == 0 || c.height == 0)
            {
                continue;   // not a drawn sprite; no object number consumed
            }
            const uint32_t objNum = objectNumber++;
            if (!match(c))
            {
                continue;
            }
            if (out && total < max)
            {
                se_reference& r = out[total];
                r.command_index = c.index;
                r.object_number = objNum;
                r.x = c.x;
                r.y = c.y;
                r.width = c.width;
                r.height = c.height;
            }
            ++total;
        }
        return total;
    }

    // Bytes a texture occupies in VDP1 VRAM, from its pixel size and color mode.
    static uint32_t TextureByteSize(const se_command& c)
    {
        const uint32_t pixels = static_cast<uint32_t>(c.width) * c.height;
        switch (c.color_mode)
        {
        case SE_COLOR_BANK_16:
        case SE_COLOR_LUT_16:   return pixels / 2;   // 4 bpp
        case SE_COLOR_RGB555:   return pixels * 2;   // 16 bpp
        default:                return pixels;       // 8 bpp bank modes
        }
    }

    // Classify VDP1 VRAM into the regions each drawable command references — its
    // command table, texture, CLUT (LUT mode), and gouraud table — for the VRAM
    // map. Distinct (address, kind) pairs are listed once, sorted by address.
    void BuildVramRegions()
    {
        mVramRegions.clear();
        auto add = [this](uint32_t addr, uint32_t size, se_vram_region_kind kind, uint32_t ref)
        {
            for (const se_vram_region& r : mVramRegions)
            {
                if (r.address == addr && r.kind == kind)
                {
                    return;  // already listed
                }
            }
            se_vram_region reg {};
            reg.address = addr;
            reg.size = size;
            reg.kind = kind;
            reg.ref_index = ref;
            mVramRegions.push_back(reg);
        };

        for (const se_command& c : mCommands)
        {
            const bool textured = (c.type == SE_CMD_NORMAL_SPRITE ||
                                   c.type == SE_CMD_SCALED_SPRITE ||
                                   c.type == SE_CMD_DISTORTED_SPRITE);
            if (!textured || c.status == SE_CMDSTAT_SKIP)
            {
                continue;
            }
            add(c.table_address, 0x20, SE_VRAM_CMD_TABLE, c.index);
            add(c.texture_address, TextureByteSize(c), SE_VRAM_TEXTURE, c.index);
            if (c.color_mode == SE_COLOR_LUT_16)
            {
                add(c.clut_address, 0x20, SE_VRAM_CLUT, c.index);   // 16 entries x 2 bytes
            }
            if (c.gouraud)
            {
                add(c.gouraud_table, 0x08, SE_VRAM_GOURAUD, c.index);  // 4 entries x 2 bytes
            }
        }

        std::sort(mVramRegions.begin(), mVramRegions.end(),
                  [](const se_vram_region& a, const se_vram_region& b)
        {
            return a.address < b.address;
        });
    }

    // Fill any still-transparent pixel of the finished frame (no sprite or NBG
    // covered it) with an opaque backdrop, so the result is a complete frame.
    void FillBackdrop()
    {
        constexpr uint8_t kBackdrop[3] = { 8, 8, 12 };
        for (size_t o = 0; o < mRenderBuffer.size(); o += 4)
        {
            if (mRenderBuffer[o + 3])
            {
                continue;
            }
            mRenderBuffer[o + 0] = kBackdrop[0];
            mRenderBuffer[o + 1] = kBackdrop[1];
            mRenderBuffer[o + 2] = kBackdrop[2];
            mRenderBuffer[o + 3] = 255;
        }
    }

    // Priority NUMBER encoded in a 16-bit sprite pixel for SPCTL sprite type
    // 'type' (VDP1 manual; mirrors Yabause Vdp1GetSpritePixelInfo). A direct-RGB
    // pixel (MSB set, mixed-color mode) carries no number and uses slot 0.
    static int SpritePriorityNumber(uint16_t px, int type, bool spclmd)
    {
        if (spclmd && (px & 0x8000))
        {
            return 0;   // RGB pixel: priority number 0
        }
        switch (type)
        {
        case 0x0: return (px >> 14) & 0x3;
        case 0x1: return (px >> 13) & 0x7;
        case 0x2: return (px >> 14) & 0x1;
        case 0x3: return (px >> 13) & 0x3;
        case 0x4: return (px >> 13) & 0x3;
        case 0x5: case 0x6: case 0x7: return (px >> 12) & 0x7;
        case 0x8: case 0x9: return (px >> 7) & 0x1;
        case 0xA:           return (px >> 6) & 0x3;
        case 0xC: case 0xD: return (px >> 7) & 0x1;
        case 0xE:           return (px >> 6) & 0x3;
        default:            return 0;   // types B, F: no priority bits
        }
    }

    // Resolve each VDP1 sprite's priority (0..7) from the sprite-priority
    // registers (PRISA..PRISD indexed by the pixel's priority number) and its
    // color data. Modeled per command (one priority per sprite) using the
    // front-most priority its pixels reach — the common case; per-pixel sprite
    // priority isn't modeled. Sprites default to 0 when VDP2 regs are absent.
    void ResolveSpritePriorities()
    {
        if (!mSnapshot.HasVdp2Regs())
        {
            for (se_sprite_2d& s : mScene.sprites) s.priority = 0;
            return;
        }
        const uint16_t spctl = mSnapshot.Vdp2Reg(0x0E0);
        const int type = spctl & 0xF;
        const bool spclmd = (spctl & 0x20) != 0;
        const uint16_t prisa = mSnapshot.Vdp2Reg(0x0F0);
        const uint16_t prisb = mSnapshot.Vdp2Reg(0x0F2);
        const uint16_t prisc = mSnapshot.Vdp2Reg(0x0F4);
        const uint16_t prisd = mSnapshot.Vdp2Reg(0x0F6);
        const uint8_t pt[8] = {
            static_cast<uint8_t>(prisa & 0x7), static_cast<uint8_t>((prisa >> 8) & 0x7),
            static_cast<uint8_t>(prisb & 0x7), static_cast<uint8_t>((prisb >> 8) & 0x7),
            static_cast<uint8_t>(prisc & 0x7), static_cast<uint8_t>((prisc >> 8) & 0x7),
            static_cast<uint8_t>(prisd & 0x7), static_cast<uint8_t>((prisd >> 8) & 0x7) };
        const std::vector<uint8_t>& vram = mSnapshot.Vdp1Vram();
        for (se_sprite_2d& s : mScene.sprites)
        {
            int best = 0;
            bool found = false;
            auto consider = [&](uint16_t px)
            {
                const int prio = pt[SpritePriorityNumber(px, type, spclmd) & 0x7];
                if (!found || prio > best) { best = prio; found = true; }
            };
            if (s.texture.color_mode == SE_COLOR_LUT_16)
            {
                for (int p = 1; p < 16; ++p)   // pixel = CLUT entry; skip index 0
                {
                    consider(ReadBE16(vram, s.texture.clut_address + p * 2));
                }
            }
            else if (s.texture.color_mode == SE_COLOR_RGB555)
            {
                consider(0x8000);   // direct-RGB sprite
            }
            else
            {
                consider(s.texture.palette_bank);   // color-bank: bits from CMDCOLR
            }
            s.priority = static_cast<uint8_t>(found ? best : 0);
        }
    }

    // Largest image dimension the core will allocate for. Comfortably above any
    // real Saturn framebuffer, texture, or host viewport, but bounds the buffer
    // so a caller-supplied size (texture ref, camera viewport, a crafted scene
    // clip) can't request a multi-gigabyte allocation whose bad_alloc would then
    // escape across the C ABI. 16384^2 * 4 = 1 GiB worst case.
    static constexpr uint32_t kMaxImageDim = 16384;

    // Shared image size-negotiation for the render entry points. Two-call
    // convention: with out->pixels == NULL, report the required byte size in
    // *needed; otherwise run 'render' (which fills mRenderBuffer with exactly
    // w*h*4 bytes) and copy it out.
    template <typename Render>
    se_result FillImage(uint32_t w, uint32_t h, se_image* out, size_t* needed, Render&& render)
    {
        if (w == 0 || h == 0 || w > kMaxImageDim || h > kMaxImageDim)
        {
            if (needed)
            {
                *needed = 0;
            }
            return SE_ERR_INVALID_ARG;
        }
        const size_t required = static_cast<size_t>(w) * h * 4;
        if (needed)
        {
            *needed = required;
        }
        out->width = w;
        out->height = h;
        out->stride = w * 4;
        out->format = SE_PIXFMT_RGBA8888;
        if (!out->pixels)
        {
            return SE_OK;  // size query only
        }
        if (out->capacity < required)
        {
            return SE_ERR_BUFFER_TOO_SMALL;
        }
        render();
        std::memcpy(out->pixels, mRenderBuffer.data(), required);
        return SE_OK;
    }

    se_data_source          mDs;
    se_config               mCfg;
    HardwareSnapshot        mSnapshot;
    std::vector<se_command> mCommands;
    Vdp1Scene               mScene;
    std::vector<uint8_t>    mRenderBuffer;
    std::vector<uint8_t>    mBgBuffer;      // VDP2 NBG composite, under the sprites
    std::vector<float>      mDepthBuffer;
    std::vector<se_vram_region> mVramRegions;
};

}  // namespace se
