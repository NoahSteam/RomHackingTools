#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "saturnexplorer/SeHost.h"

namespace
{
int gFailures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

struct State
{
    std::vector<uint8_t> vdp1 = std::vector<uint8_t>(0x40);
    std::vector<uint8_t> vdp2 = std::vector<uint8_t>(512 * 1024);
    std::vector<uint8_t> cram = std::vector<uint8_t>(4 * 1024);
    uint16_t regs[0x90] = {};
};

void PutBE16(std::vector<uint8_t>& memory, uint32_t address, uint16_t value)
{
    memory[address] = static_cast<uint8_t>(value >> 8);
    memory[address + 1] = static_cast<uint8_t>(value);
}

size_t Copy(const std::vector<uint8_t>& source, uint32_t offset, void* dst, size_t size)
{
    if (offset >= source.size()) return 0;
    const size_t count = std::min(size, source.size() - offset);
    std::memcpy(dst, source.data() + offset, count);
    return count;
}

size_t ReadVdp1(void* user, uint32_t offset, void* dst, size_t size)
{
    return Copy(static_cast<State*>(user)->vdp1, offset, dst, size);
}

size_t ReadVdp2(void* user, uint32_t offset, void* dst, size_t size)
{
    return Copy(static_cast<State*>(user)->vdp2, offset, dst, size);
}

size_t ReadCram(void* user, uint32_t offset, void* dst, size_t size)
{
    return Copy(static_cast<State*>(user)->cram, offset, dst, size);
}

uint16_t ReadVdp2Reg(void* user, uint32_t offset)
{
    const State* state = static_cast<State*>(user);
    return (offset >> 1) < 0x90 ? state->regs[offset >> 1] : 0;
}

void SetReg(State& state, uint32_t offset, uint16_t value)
{
    state.regs[offset >> 1] = value;
}

State MakeNbg3State()
{
    State state;
    // A 4x2 VDP1 system clip establishes the composited frame dimensions.
    PutBE16(state.vdp1, 0x00, 0x0009);
    PutBE16(state.vdp1, 0x14, 3);
    PutBE16(state.vdp1, 0x16, 1);
    PutBE16(state.vdp1, 0x20, 0x8000);

    // NBG3, 16-color 8x8 cells, one-word pattern names. Plane A's name table
    // starts at byte 0x2000 and points to solid-white character number 1.
    SetReg(state, 0x020, 0x0008);  // BGON
    SetReg(state, 0x036, 0x8000);  // PNCN3: one-word
    SetReg(state, 0x04C, 0x0001);  // MPABN3: plane A map number 1
    SetReg(state, 0x0FA, 0x0100);  // PRINB: NBG3 priority 1
    PutBE16(state.vdp2, 0x2000, 0x0001);
    std::fill(state.vdp2.begin() + 0x20, state.vdp2.begin() + 0x40, 0x11);
    PutBE16(state.cram, 2, 0x7FFF);
    return state;
}

// (Re)size the VDP1 VRAM and write the system-clip command that establishes the 4x2
// composited frame (lower-right corner = 3,1).
void WriteSystemClip(State& state, uint32_t vdp1Size)
{
    state.vdp1.assign(vdp1Size, 0);
    PutBE16(state.vdp1, 0x00, 0x0009);
    PutBE16(state.vdp1, 0x14, 3);
    PutBE16(state.vdp1, 0x16, 1);
}

// A 4x2 frame with a single VDP1 normal sprite that fills it: an 8x2 RGB555 white
// (opaque) texture, drawn with the given CMDPMOD. VDP2 backgrounds are disabled so only
// the sprite (over the backdrop) is visible.
State MakeSpriteState(uint16_t pmod)
{
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0000);   // BGON off — only the sprite draws
    WriteSystemClip(state, 0x120);
    PutBE16(state.vdp1, 0x20, 0x8000);          // CMDCTRL: normal sprite (comm 0) + END
    PutBE16(state.vdp1, 0x24, pmod);            // CMDPMOD
    PutBE16(state.vdp1, 0x28, 0x100 / 8);       // CMDSRCA: texture at byte 0x100
    PutBE16(state.vdp1, 0x2A, (1 << 8) | 2);    // CMDSIZE: 8 wide, 2 tall
    PutBE16(state.vdp1, 0x2C, 0);               // CMDXA
    PutBE16(state.vdp1, 0x2E, 0);               // CMDYA
    for (uint32_t i = 0; i < 16; ++i)           // 8x2 RGB555 white, MSB set = opaque
        PutBE16(state.vdp1, 0x100 + i * 2, 0xFFFF);
    return state;
}

std::vector<uint8_t> Render(State& state, bool showWindow, bool colorCalc = false)
{
    se_data_source source = {};
    source.abi_version = SE_ABI_VERSION;
    source.capabilities = SE_CAP_VDP1_VRAM | SE_CAP_VDP2_VRAM |
                          SE_CAP_CRAM | SE_CAP_VDP2_REGS;
    source.user = &state;
    source.read_vdp1_vram = ReadVdp1;
    source.read_vdp2_vram = ReadVdp2;
    source.read_cram = ReadCram;
    source.read_vdp2_reg = ReadVdp2Reg;
    se_config config = {};
    config.abi_version = SE_ABI_VERSION;
    se_context* context = se_create(&source, &config);
    CHECK(context != nullptr);
    CHECK(se_begin_frame(context) == SE_OK);

    se_render_opts options = {};
    for (int i = 0; i < SE_LAYER_COUNT; ++i) options.show_layer[i] = 1;
    options.show_vdp1_sprites = 1;
    options.show_window = showWindow ? 1 : 0;
    options.show_color_calculation = colorCalc ? 1 : 0;
    se_image image = {};
    size_t needed = 0;
    CHECK(se_render_frame(context, &options, &image, &needed) == SE_OK);
    std::vector<uint8_t> pixels(needed);
    image.pixels = pixels.data();
    image.capacity = pixels.size();
    CHECK(se_render_frame(context, &options, &image, &needed) == SE_OK);
    CHECK(image.width == 4);
    CHECK(image.height == 2);
    se_destroy(context);
    return pixels;
}

bool IsColor(const std::vector<uint8_t>& pixels, int x, int y,
             uint8_t r, uint8_t g, uint8_t b)
{
    const size_t offset = static_cast<size_t>(y * 4 + x) * 4;
    return pixels[offset] == r && pixels[offset + 1] == g &&
           pixels[offset + 2] == b && pixels[offset + 3] == 255;
}

bool IsWhite(const std::vector<uint8_t>& pixels, int x, int y)
{
    return IsColor(pixels, x, y, 255, 255, 255);
}

bool IsRed(const std::vector<uint8_t>& pixels, int x, int y)
{
    return IsColor(pixels, x, y, 255, 0, 0);
}

void TestRectangularWindow()
{
    State state = MakeNbg3State();
    // In normal resolution, horizontal window coordinates are in half-dots.
    // Area=outside means only x=1..2, y=0..1 remains visible.
    SetReg(state, 0x0C0, 2);
    SetReg(state, 0x0C4, 4);
    SetReg(state, 0x0C2, 0);
    SetReg(state, 0x0C6, 1);
    SetReg(state, 0x0D2, 0x0300);  // NBG3 W0 outside + enable

    const std::vector<uint8_t> clipped = Render(state, true);
    for (int y = 0; y < 2; ++y)
    {
        CHECK(!IsWhite(clipped, 0, y));
        CHECK(IsWhite(clipped, 1, y));
        CHECK(IsWhite(clipped, 2, y));
        CHECK(!IsWhite(clipped, 3, y));
    }

    const std::vector<uint8_t> unclipped = Render(state, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsWhite(unclipped, x, y));
}

void TestLineWindow()
{
    State state = MakeNbg3State();
    SetReg(state, 0x0C2, 0);
    SetReg(state, 0x0C6, 1);
    SetReg(state, 0x0D2, 0x0300);  // NBG3 W0 outside + enable
    SetReg(state, 0x0D8, 0x8001);  // line table word address 0x10000
    SetReg(state, 0x0DA, 0x0000);
    PutBE16(state.vdp2, 0x20000, 0); // line 0: x=0..1 after half-dot conversion
    PutBE16(state.vdp2, 0x20002, 2);
    PutBE16(state.vdp2, 0x20004, 4); // line 1: x=2..3
    PutBE16(state.vdp2, 0x20006, 6);

    const std::vector<uint8_t> pixels = Render(state, true);
    CHECK(IsWhite(pixels, 0, 0));
    CHECK(IsWhite(pixels, 1, 0));
    CHECK(!IsWhite(pixels, 2, 0));
    CHECK(!IsWhite(pixels, 3, 0));
    CHECK(!IsWhite(pixels, 0, 1));
    CHECK(!IsWhite(pixels, 1, 1));
    CHECK(IsWhite(pixels, 2, 1));
    CHECK(IsWhite(pixels, 3, 1));
}

void TestVerticalPlaneSize()
{
    State state = MakeNbg3State();
    SetReg(state, 0x03A, 0x0080);  // NBG3 plane size 10 = 1x2 pages
    SetReg(state, 0x04C, 0x0002);  // aligned plane-A map number
    SetReg(state, 0x096, 512);     // show the plane's lower page
    PutBE16(state.vdp2, 0x6000, 0x0001);
    const std::vector<uint8_t> pixels = Render(state, true);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsWhite(pixels, x, y));
}

void TestTransparentPixelDisable()
{
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0808);  // NBG3 enabled + color-code-zero opaque
    std::fill(state.vdp2.begin() + 0x20, state.vdp2.begin() + 0x40, 0x00);
    PutBE16(state.cram, 0, 0x001F);
    const std::vector<uint8_t> pixels = Render(state, true);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsRed(pixels, x, y));
}
// Store a 32-bit rotation-table field as two big-endian words at 'wordAddr'. Most fields
// are read as (dword >> 6), so the caller pre-shifts; kx/ky are read as-is.
void PutRotDword(std::vector<uint8_t>& vram, uint32_t wordAddr, uint32_t value)
{
    PutBE16(vram, wordAddr * 2, static_cast<uint16_t>(value >> 16));
    PutBE16(vram, (wordAddr + 1) * 2, static_cast<uint16_t>(value));
}

void TestRotationIdentity()
{
    // RBG0 with an identity rotation (unit matrix, kx=ky=1, per-line/per-dot steps of 1)
    // maps screen (x,y) straight to plane (x,y), so plane A's solid-white character 1
    // fills the frame — exactly what the same plane looks like as a plain NBG.
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0010);   // BGON: RBG0 only
    SetReg(state, 0x02A, 0x0000);   // CHCTLB: RBG0 16-colour, 8x8 cells
    SetReg(state, 0x038, 0x8000);   // PNCR: one-word pattern names
    SetReg(state, 0x03E, 0x0000);   // MPOFR: map offset 0
    SetReg(state, 0x050, 0x0001);   // MPABRA: RBG0 param-A plane 0 = map number 1
    SetReg(state, 0x03A, 0x0000);   // PLSZ: 1x1 pages, screen-over = repeat
    SetReg(state, 0x0FC, 0x0001);   // PRIR: RBG0 priority 1
    SetReg(state, 0x0B0, 0x0000);   // RPMD: parameter set A
    SetReg(state, 0x0BE, 0x8000);   // RPTAL: parameter table at word 0x8000

    const uint32_t base = 0x8000;
    // Fields read as (dword >> 6): store value << 6.  M[0]=M[4]=1.0 (1024 in .10),
    // DYst=1.0 (per line), DX=1.0 (per dot); everything else 0.
    PutRotDword(state.vdp2, base + 0x08, 1024u << 6);   // DYst
    PutRotDword(state.vdp2, base + 0x0A, 1024u << 6);   // DX
    PutRotDword(state.vdp2, base + 0x0E, 1024u << 6);   // M[0] (A)
    PutRotDword(state.vdp2, base + 0x16, 1024u << 6);   // M[4] (E)
    // kx/ky are read as-is: 1.0 in .16.
    PutRotDword(state.vdp2, base + 0x26, 0x10000u);     // kx
    PutRotDword(state.vdp2, base + 0x28, 0x10000u);     // ky

    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsWhite(pixels, x, y));
}

void TestBitmapNbg0()
{
    // NBG0 in 8bpp bitmap mode (512x256): pixel (x,y) is the palette index at VRAM byte
    // y*512 + x. Fill the top-left 4x2 with index 1 (white) and expect a white frame.
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0001);   // BGON: NBG0 only
    SetReg(state, 0x028, 0x0012);   // CHCTLA: N0BMEN + 8bpp, bitmap size 512x256
    SetReg(state, 0x02C, 0x0000);   // BMPNA: palette base 0
    SetReg(state, 0x03C, 0x0000);   // MPOFN: bitmap base 0
    SetReg(state, 0x0F8, 0x0001);   // PRINA: NBG0 priority 1
    PutBE16(state.cram, 2, 0x7FFF); // CRAM index 1 = white
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            state.vdp2[y * 512 + x] = 1;
    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsWhite(pixels, x, y));
}

void TestZoomBitmap()
{
    // NBG0 8bpp bitmap with 2x horizontal zoom (X coordinate increment 0x80 = 0.5/dot):
    // screen dots 0,1 sample bitmap pixel 0; dots 2,3 sample pixel 1. Pixel 0 = white,
    // pixel 1 = red -> the frame reads white,white,red,red.
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0001);   // BGON: NBG0
    SetReg(state, 0x028, 0x0012);   // CHCTLA: N0BMEN + 8bpp
    SetReg(state, 0x0F8, 0x0001);   // PRINA: priority 1
    SetReg(state, 0x07A, 0x8000);   // ZMXDN0: fractional 0x80 -> increment 0.5 (2x zoom)
    PutBE16(state.cram, 2, 0x7FFF); // index 1 = white
    PutBE16(state.cram, 4, 0x001F); // index 2 = red
    for (int y = 0; y < 2; ++y)
    {
        state.vdp2[y * 512 + 0] = 1;   // pixel 0 = white
        state.vdp2[y * 512 + 1] = 2;   // pixel 1 = red
    }
    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
    {
        CHECK(IsWhite(pixels, 0, y));
        CHECK(IsWhite(pixels, 1, y));
        CHECK(IsRed(pixels, 2, y));
        CHECK(IsRed(pixels, 3, y));
    }
}

void TestColorOffset()
{
    // NBG3 white with colour offset A of (-128,-128,0) -> (127,127,255).
    State state = MakeNbg3State();
    SetReg(state, 0x110, 0x0008);   // CLOFEN: NBG3 colour offset enabled
    SetReg(state, 0x112, 0x0000);   // CLOFSL: use offset set A
    SetReg(state, 0x114, 0x0180);   // COAR = -128 (9-bit signed)
    SetReg(state, 0x116, 0x0180);   // COAG = -128
    SetReg(state, 0x118, 0x0000);   // COAB = 0
    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColor(pixels, x, y, 127, 127, 255));
}

void TestMosaic()
{
    // NBG0 8bpp bitmap with horizontal mosaic size 2: dots 0,1 show pixel 0 (white),
    // dots 2,3 show pixel 2 (red).
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0001);   // BGON: NBG0
    SetReg(state, 0x028, 0x0012);   // CHCTLA: N0BMEN + 8bpp
    SetReg(state, 0x0F8, 0x0001);   // PRINA: priority 1
    SetReg(state, 0x022, 0x0101);   // MZCTL: N0 mosaic enable + horizontal size 2
    PutBE16(state.cram, 2, 0x7FFF); // index 1 = white
    PutBE16(state.cram, 4, 0x001F); // index 2 = red
    for (int y = 0; y < 2; ++y)
    {
        state.vdp2[y * 512 + 0] = 1;   // pixel 0 = white
        state.vdp2[y * 512 + 2] = 2;   // pixel 2 = red
    }
    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
    {
        CHECK(IsWhite(pixels, 0, y));
        CHECK(IsWhite(pixels, 1, y));
        CHECK(IsRed(pixels, 2, y));
        CHECK(IsRed(pixels, 3, y));
    }
}

void TestSpriteHalfLuminance()
{
    // CMDPMOD: RGB555 (0x28) + SPD (0x40) + color-calc 2 (half-luminance). A white
    // sprite is drawn at half luminance -> (127,127,127) everywhere it covers.
    State state = MakeSpriteState(0x0028 | 0x0040 | 0x0002);
    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColor(pixels, x, y, 127, 127, 127));
}

void TestSpriteMesh()
{
    // CMDPMOD: RGB555 + SPD + mesh (bit 8). The white sprite draws only on the
    // checkerboard where (x+y) is even; the stippled pixels fall through to the backdrop.
    State state = MakeSpriteState(0x0028 | 0x0040 | 0x0100);
    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
        {
            if (((x + y) & 1) == 0) CHECK(IsWhite(pixels, x, y));
            else                    CHECK(!IsWhite(pixels, x, y));
        }
}

void TestPolygon()
{
    // VDP1 untextured polygon (command 3): a solid red quad covering the 4x2 frame.
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0000);   // BGON off — only the polygon draws
    WriteSystemClip(state, 0x40);
    PutBE16(state.vdp1, 0x20, 0x8003);   // CMDCTRL: polygon (comm 3) + END
    PutBE16(state.vdp1, 0x26, 0x001F);   // CMDCOLR: red (RGB555)
    PutBE16(state.vdp1, 0x2C, 0); PutBE16(state.vdp1, 0x2E, 0);   // A = (0,0)
    PutBE16(state.vdp1, 0x30, 4); PutBE16(state.vdp1, 0x32, 0);   // B = (4,0)
    PutBE16(state.vdp1, 0x34, 4); PutBE16(state.vdp1, 0x36, 2);   // C = (4,2)
    PutBE16(state.vdp1, 0x38, 0); PutBE16(state.vdp1, 0x3A, 2);   // D = (0,2)
    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsRed(pixels, x, y));
}

void TestLine()
{
    // VDP1 line (command 5): a red segment from (0,0) to (3,0) along the top row.
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0000);   // BGON off
    WriteSystemClip(state, 0x40);
    PutBE16(state.vdp1, 0x20, 0x8005);   // CMDCTRL: line (comm 5) + END
    PutBE16(state.vdp1, 0x26, 0x001F);   // CMDCOLR: red
    PutBE16(state.vdp1, 0x2C, 0); PutBE16(state.vdp1, 0x2E, 0);   // A = (0,0)
    PutBE16(state.vdp1, 0x30, 3); PutBE16(state.vdp1, 0x32, 0);   // B = (3,0)
    const std::vector<uint8_t> pixels = Render(state, false);
    for (int x = 0; x < 4; ++x)
    {
        CHECK(IsRed(pixels, x, 0));       // the segment
        CHECK(!IsRed(pixels, x, 1));      // row 1 untouched
    }
}

void TestUserClip()
{
    // A full-frame red polygon with user clipping enabled (CMDPMOD bit 10), mode 0
    // (draw inside), clipped to the rect x=1..2 by a preceding user-clip command.
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0000);   // BGON off
    WriteSystemClip(state, 0x60);
    PutBE16(state.vdp1, 0x20, 0x0006);   // user clip command (comm 6), JP next
    PutBE16(state.vdp1, 0x2C, 1);        // clip X0 = 1
    PutBE16(state.vdp1, 0x2E, 0);        // clip Y0 = 0
    PutBE16(state.vdp1, 0x34, 2);        // clip X1 = 2
    PutBE16(state.vdp1, 0x36, 1);        // clip Y1 = 1
    PutBE16(state.vdp1, 0x40, 0x8003);   // polygon (comm 3) + END
    PutBE16(state.vdp1, 0x44, 0x0400);   // CMDPMOD: user clip enable (bit 10), mode inside
    PutBE16(state.vdp1, 0x46, 0x001F);   // CMDCOLR: red
    PutBE16(state.vdp1, 0x4C, 0); PutBE16(state.vdp1, 0x4E, 0);   // A
    PutBE16(state.vdp1, 0x50, 4); PutBE16(state.vdp1, 0x52, 0);   // B
    PutBE16(state.vdp1, 0x54, 4); PutBE16(state.vdp1, 0x56, 2);   // C
    PutBE16(state.vdp1, 0x58, 0); PutBE16(state.vdp1, 0x5A, 2);   // D
    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
        {
            if (x >= 1 && x <= 2) CHECK(IsRed(pixels, x, y));    // inside the clip
            else                  CHECK(!IsRed(pixels, x, y));   // clipped away
        }
}

void TestUserClipDefaultUnbounded()
{
    // A polygon with user clipping ENABLED (CMDPMOD bit 10) but NO user-clip command in
    // the frame — like NiGHTS' "PRESS START" sprite, which relies on a clip rect set in an
    // earlier frame. The default rect must be unbounded so the sprite still draws.
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0000);   // BGON off
    WriteSystemClip(state, 0x40);
    PutBE16(state.vdp1, 0x20, 0x8003);   // polygon (comm 3) + END
    PutBE16(state.vdp1, 0x24, 0x0400);   // CMDPMOD: user-clip enable, mode inside, no comm 6
    PutBE16(state.vdp1, 0x26, 0x001F);   // CMDCOLR: red
    PutBE16(state.vdp1, 0x2C, 0); PutBE16(state.vdp1, 0x2E, 0);   // A
    PutBE16(state.vdp1, 0x30, 4); PutBE16(state.vdp1, 0x32, 0);   // B
    PutBE16(state.vdp1, 0x34, 4); PutBE16(state.vdp1, 0x36, 2);   // C
    PutBE16(state.vdp1, 0x38, 0); PutBE16(state.vdp1, 0x3A, 2);   // D
    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsRed(pixels, x, y));   // not clipped away
}

void TestBackScreen()
{
    // With NBG3 disabled, the whole frame is the BKTA back-screen colour. Point BKTA
    // at a VRAM word holding RGB555 red and expect an opaque red frame — not the
    // hardcoded fallback backdrop.
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0000);   // BGON: all screens off
    SetReg(state, 0x0AC, 0x0000);   // BKTAU: single colour, high address 0
    SetReg(state, 0x0AE, 0x0100);   // BKTAL: word address 0x100 (byte 0x200)
    PutBE16(state.vdp2, 0x200, 0x001F);  // RGB555 red
    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColor(pixels, x, y, 255, 0, 0));
}

void TestColorCalc()
{
    // NBG3 (white, priority 1) with color calculation enabled, ratio 15, composited
    // over a blue back screen. Expect a half-blend: R,G = (255*16)>>5 = 127, B = 255.
    State state = MakeNbg3State();
    SetReg(state, 0x0AC, 0x0000);
    SetReg(state, 0x0AE, 0x0100);
    PutBE16(state.vdp2, 0x200, 0x7C00);  // RGB555 blue back screen
    SetReg(state, 0x0EC, 0x0008);        // CCCTL: N3 color-calc enable
    SetReg(state, 0x10A, 0x0F00);        // CCRNB: N3 ratio = 15 (high byte)

    const std::vector<uint8_t> blended = Render(state, false, true);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColor(blended, x, y, 127, 127, 255));

    // With the color-calc option off, NBG3 overwrites opaquely (stays white).
    const std::vector<uint8_t> opaque = Render(state, false, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsWhite(opaque, x, y));
}
}  // namespace

int main()
{
    TestRectangularWindow();
    TestLineWindow();
    TestVerticalPlaneSize();
    TestTransparentPixelDisable();
    TestBackScreen();
    TestColorCalc();
    TestRotationIdentity();
    TestBitmapNbg0();
    TestZoomBitmap();
    TestColorOffset();
    TestMosaic();
    TestSpriteHalfLuminance();
    TestSpriteMesh();
    TestPolygon();
    TestLine();
    TestUserClip();
    TestUserClipDefaultUnbounded();
    if (gFailures != 0)
    {
        std::cerr << gFailures << " VDP2 compositor check(s) failed\n";
        return 1;
    }
    std::cout << "VDP2 compositor tests passed\n";
    return 0;
}
