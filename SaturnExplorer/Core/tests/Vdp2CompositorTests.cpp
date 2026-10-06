#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "FakeVdpSource.h"
#include "saturnexplorer/SeHost.h"

namespace
{
using se_test::PutBE16;
using se_test::SetReg;
using se_test::State;

int gFailures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

State MakeNbg3State()
{
    State state;
    // A 4x2 VDP1 system clip establishes the composited frame dimensions.
    se_test::WriteSystemClip(state, 4, 2);
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

// (Re)size the VDP1 VRAM and re-establish the 4x2 composited frame.
void ResizeVdp1(State& state, uint32_t vdp1Size)
{
    state.vdp1.assign(vdp1Size, 0);
    se_test::WriteSystemClip(state, 4, 2);
}

// A 4x2 frame with a single VDP1 normal sprite that fills it: an 8x2 RGB555 white
// (opaque) texture, drawn with the given CMDPMOD. VDP2 backgrounds are disabled so only
// the sprite (over the backdrop) is visible.
State MakeSpriteState(uint16_t pmod)
{
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0000);   // BGON off — only the sprite draws
    ResizeVdp1(state, 0x120);
    PutBE16(state.vdp1, 0x20, 0x0000);          // CMDCTRL: normal sprite (comm 0), JP next
    PutBE16(state.vdp1, 0x40, 0x8000);          // draw-end terminator (END is its own command)
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
    se_context* context = se_test::CreateContext(state);
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

// A source that brackets its captures is read between begin_capture and end_capture -- every
// callback of the capture, and the frame number, inside the pair -- and the frame number the
// host sees afterwards is the one captured, not whatever the source has since reached.
namespace {
struct BracketState
{
    int  begins = 0, ends = 0;
    int  depth = 0;
    bool readOutsidePair = false;
    uint64_t liveFrame = 10;   // what the source is "now" -- moves on after each capture
};
BracketState* gBracket = nullptr;
void Note() { if (gBracket->depth == 0) gBracket->readOutsidePair = true; }
}  // namespace

void TestCaptureIsBracketedAndFrameNumberIsTheCapturedOne()
{
    State state = MakeNbg3State();
    se_data_source source = se_test::MakeSource(state);
    BracketState bracket;
    gBracket = &bracket;
    static State* sInner = nullptr;
    sInner = &state;
    source.capabilities |= SE_CAP_FRAME_STEP;
    source.user = &state;
    source.begin_capture = [](void*) { ++gBracket->begins; ++gBracket->depth; };
    source.end_capture   = [](void*) { ++gBracket->ends; --gBracket->depth; };
    source.read_vdp1_vram = [](void*, uint32_t o, void* d, size_t n)
    {
        Note();
        const std::vector<uint8_t>& v = sInner->vdp1;
        if (o >= v.size()) return size_t(0);
        const size_t c = std::min(n, v.size() - o);
        std::memcpy(d, v.data() + o, c);
        return c;
    };
    source.frame_number = [](void*) -> uint64_t { Note(); return gBracket->liveFrame; };
    source.frame_step = [](void*, int32_t) { return 0; };
    source.frame_pause = [](void*) { return 0; };
    se_context* ctx = se_test::CreateContext(source);
    CHECK(ctx != nullptr);

    CHECK(se_begin_frame(ctx) == SE_OK);
    CHECK(bracket.begins == 1 && bracket.ends == 1 && bracket.depth == 0);   // one pair per capture
    CHECK(!bracket.readOutsidePair);
    bracket.readOutsidePair = false;
    CHECK(se_frame_number(ctx) == 10);

    bracket.liveFrame = 99;                       // the source moves on...
    CHECK(se_frame_number(ctx) == 10);            // ...the captured snapshot has not
    CHECK(se_begin_frame(ctx) == SE_OK);
    CHECK(se_frame_number(ctx) == 99);
    CHECK(bracket.begins == 2 && bracket.ends == 2);
    se_destroy(ctx);
    gBracket = nullptr;
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

// An identity rotation parameter set at 'base' (a word address): screen (x,y) maps
// straight to plane (x,y). Fields are read as (dword >> 6), so values are stored << 6;
// kx/ky are read as-is in .16.
void WriteIdentityRotParam(std::vector<uint8_t>& vram, uint32_t base)
{
    PutRotDword(vram, base + 0x08, 1024u << 6);   // DYst (per line)
    PutRotDword(vram, base + 0x0A, 1024u << 6);   // DX   (per dot)
    PutRotDword(vram, base + 0x0E, 1024u << 6);   // M[0] (A)
    PutRotDword(vram, base + 0x16, 1024u << 6);   // M[4] (E)
    PutRotDword(vram, base + 0x26, 0x10000u);     // kx = 1.0
    PutRotDword(vram, base + 0x28, 0x10000u);     // ky = 1.0
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

    WriteIdentityRotParam(state.vdp2, 0x8000);

    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsWhite(pixels, x, y));
}


// RBG0 with both rotation parameter sets in play. Shared setup: identity rotations for A
// and B, A's plane drawing white character 1 and B's plane drawing red character 2.
State MakeTwoParamRotState()
{
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0010);   // BGON: RBG0 only
    SetReg(state, 0x02A, 0x0000);   // CHCTLB: RBG0 16-colour, 8x8 cells
    SetReg(state, 0x038, 0x8000);   // PNCR: one-word pattern names
    SetReg(state, 0x03E, 0x0000);   // MPOFR: map offset 0 for both sets
    SetReg(state, 0x050, 0x0001);   // MPABRA: param-A plane 0 = map number 1
    SetReg(state, 0x060, 0x0002);   // MPABRB: param-B plane 0 = map number 2
    SetReg(state, 0x03A, 0x0000);   // PLSZ: 1x1 pages, screen-over = repeat (both sets)
    SetReg(state, 0x0FC, 0x0001);   // PRIR: RBG0 priority 1
    SetReg(state, 0x0BE, 0x8000);   // RPTAL: parameter table A at word 0x8000 (B at +0x40)
    WriteIdentityRotParam(state.vdp2, 0x8000);
    WriteIdentityRotParam(state.vdp2, 0x8040);

    // Map number 2's pattern-name table names character 2, whose cell is solid index 2.
    PutBE16(state.vdp2, 0x4000, 0x0002);
    std::fill(state.vdp2.begin() + 0x40, state.vdp2.begin() + 0x60, 0x22);
    PutBE16(state.cram, 4, 0x001F);   // index 2 = red
    return state;
}

void TestRotationParamWindow()
{
    // RPMD 3 picks the rotation parameter set per dot from the rotation parameter window
    // (WCTLD low byte), which is how a game splits the screen at a horizon. Window 0
    // covers dots 0-1, so those sample parameter set B (red) and dots 2-3 sample A
    // (white). The window is geometry rather than masking, so it applies even with
    // window display switched off.
    State state = MakeTwoParamRotState();
    SetReg(state, 0x0B0, 0x0003);   // RPMD: parameter set per rotation parameter window
    SetReg(state, 0x0C0, 0);        // WPSX0 (halved when not hi-res: dots 0..1)
    SetReg(state, 0x0C4, 3);        // WPEX0
    SetReg(state, 0x0C2, 0);        // WPSY0
    SetReg(state, 0x0C6, 1);        // WPEY0
    SetReg(state, 0x0D6, 0x0002);   // WCTLD: rotation parameter window = W0, inside

    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
    {
        CHECK(IsRed(pixels, 0, y));     // inside the window -> parameter set B
        CHECK(IsRed(pixels, 1, y));
        CHECK(IsWhite(pixels, 2, y));   // outside -> parameter set A
        CHECK(IsWhite(pixels, 3, y));
    }
}

void TestRotationCoeffTableOffset()
{
    // KTAOF supplies the coefficient table's high address bits (A18-A16), separately per
    // parameter set. With RPMD 1 (set B only) and KTAOF's B field = 1, B's table starts a
    // whole 64K-word block in; the coefficient stored there is 1.0, so the identity
    // rotation stands and B's red plane fills the frame. A transparent coefficient sits
    // at the un-offset address, so ignoring KTAOF blanks the layer instead.
    State state = MakeTwoParamRotState();
    SetReg(state, 0x0B0, 0x0001);   // RPMD: parameter set B
    SetReg(state, 0x0B4, 0x0100);   // KTCTL: B enables a 32-bit coefficient table, mode 0
    SetReg(state, 0x0B6, 0x0100);   // KTAOF: B's table offset = 1

    // KAst/DKAst/DKAx are all zero, so every dot reads the one entry at the table base.
    PutRotDword(state.vdp2, 0x00000, 0x80000000u);   // un-offset entry: transparent
    PutRotDword(state.vdp2, 0x20000, 0x00010000u);   // KTAOF'd entry: kx = ky = 1.0

    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsRed(pixels, x, y));
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

void TestBitmapRgb888()
{
    // NBG0 in 32bpp RGB888 bitmap mode (like the Sonic Team movie): each pixel is a
    // 32-bit big-endian word stored [code][B][G][R] — the MSB byte carries opacity and
    // the colour's low 24 bits are 0xBBGGRR. Write one opaque pixel and confirm the
    // channels are not swapped (a red/blue swap was the movie-playback bug).
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0001);   // BGON: NBG0 only
    SetReg(state, 0x028, 0x0042);   // CHCTLA: N0BMEN + colour number 4 (RGB888), 512x256
    SetReg(state, 0x03C, 0x0000);   // MPOFN: bitmap base 0
    SetReg(state, 0x0F8, 0x0001);   // PRINA: NBG0 priority 1
    // Pixel (2,1): byte offset (1*512 + 2) * 4 = 0x808. Bytes 0x80,0x11,0x22,0x33 ->
    // opaque, B=0x11, G=0x22, R=0x33.
    state.vdp2[0x808] = 0x80; state.vdp2[0x809] = 0x11;
    state.vdp2[0x80A] = 0x22; state.vdp2[0x80B] = 0x33;
    const std::vector<uint8_t> pixels = Render(state, false);
    CHECK(IsColor(pixels, 2, 1, 0x33, 0x22, 0x11));   // R=0x33, G=0x22, B=0x11
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

// Regression: a draw-end command (CMDCTRL bit 15) must not be drawn. Real lists end
// with CMDCTRL=0x8000, whose low nibble reads as 0x0 -- exactly a normal textured
// sprite -- so a builder that only masks the jump bits (bits 12-14) treats the
// terminator as a sprite and paints a quad from its leftover words. Panzer Dragoon
// Saga hit this: a 32x32 black square in the middle of the frame. Here the terminator
// sits where a red polygon already drew, so if it were drawn the frame would change.
void TestDrawEndNotDrawn()
{
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0000);   // BGON off — only the polygon draws
    ResizeVdp1(state, 0x120);       // room for the terminator's leftover texture
    PutBE16(state.vdp1, 0x20, 0x0004);   // polygon (comm 4), JP next
    PutBE16(state.vdp1, 0x26, 0x001F);   // CMDCOLR: red
    PutBE16(state.vdp1, 0x2C, 0); PutBE16(state.vdp1, 0x2E, 0);   // A
    PutBE16(state.vdp1, 0x30, 4); PutBE16(state.vdp1, 0x32, 0);   // B
    PutBE16(state.vdp1, 0x34, 4); PutBE16(state.vdp1, 0x36, 2);   // C
    PutBE16(state.vdp1, 0x38, 0); PutBE16(state.vdp1, 0x3A, 2);   // D
    // Terminator carrying a full-frame quad + an opaque texture in its leftover words.
    PutBE16(state.vdp1, 0x40, 0x8000);   // CMDCTRL: END (low nibble 0 == normal sprite)
    PutBE16(state.vdp1, 0x44, 0x0028 | 0x0040);   // CMDPMOD: RGB555 + SPD (opaque)
    PutBE16(state.vdp1, 0x48, 0x100 / 8);         // CMDSRCA
    PutBE16(state.vdp1, 0x4A, (1 << 8) | 2);      // CMDSIZE: 8x2
    PutBE16(state.vdp1, 0x4C, 0); PutBE16(state.vdp1, 0x4E, 0);
    for (uint32_t i = 0; i < 16; ++i)             // black, MSB set = opaque
        PutBE16(state.vdp1, 0x100 + i * 2, 0x8000);

    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsRed(pixels, x, y));   // the terminator must not have painted over it
}

void TestPolygon()
{
    // VDP1 untextured polygon (command 4): a solid red quad covering the 4x2 frame.
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0000);   // BGON off — only the polygon draws
    ResizeVdp1(state, 0x60);
    PutBE16(state.vdp1, 0x20, 0x0004);   // CMDCTRL: polygon (comm 4), JP next
    PutBE16(state.vdp1, 0x40, 0x8000);   // draw-end terminator
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
    // VDP1 line (command 6): a red segment from (0,0) to (3,0) along the top row.
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0000);   // BGON off
    ResizeVdp1(state, 0x60);
    PutBE16(state.vdp1, 0x20, 0x0006);   // CMDCTRL: line (comm 6), JP next
    PutBE16(state.vdp1, 0x40, 0x8000);   // draw-end terminator
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
    ResizeVdp1(state, 0x80);
    PutBE16(state.vdp1, 0x20, 0x0008);   // user clip command (comm 8), JP next
    PutBE16(state.vdp1, 0x2C, 1);        // clip X0 = 1
    PutBE16(state.vdp1, 0x2E, 0);        // clip Y0 = 0
    PutBE16(state.vdp1, 0x34, 2);        // clip X1 = 2
    PutBE16(state.vdp1, 0x36, 1);        // clip Y1 = 1
    PutBE16(state.vdp1, 0x40, 0x0004);   // polygon (comm 4), JP next
    PutBE16(state.vdp1, 0x60, 0x8000);   // draw-end terminator
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
    ResizeVdp1(state, 0x60);
    PutBE16(state.vdp1, 0x20, 0x0004);   // polygon (comm 4), JP next
    PutBE16(state.vdp1, 0x40, 0x8000);   // draw-end terminator
    PutBE16(state.vdp1, 0x24, 0x0400);   // CMDPMOD: user-clip enable, mode inside, no comm 8
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

// Where a bank palette says it lives in CRAM. DecodeBankPalette reported 0, so nothing
// downstream could tell where a bank palette's bytes were -- the Palette Viewer's jump to the
// Memory panel needs that address. Re-deriving the masks here would only restate the code, so
// each case plants a known colour at the offset the palette reports and checks entry 0 came
// from exactly there.
void TestBankPaletteReportsItsCramAddress()
{
    // RGB555: two bytes an entry, so the fixture's 4 KiB CRAM holds 2048 of them. BANK_16
    // masks the colour bank down to a 16-entry boundary -- 0x1234 -> 0x1230 -- which is past
    // the end of 2048 entries and wraps to 0x230. A bank index runs to 0xFF00, so wrapping is
    // ordinary here rather than a corner case.
    {
        State state = MakeNbg3State();
        const uint32_t expected = 0x230u * 2u;
        state.cram[expected]     = 0x83;   // green, big-endian RGB555
        state.cram[expected + 1] = 0xE0;
        se_context* context = se_test::CreateContext(state);
        CHECK(context != nullptr);
        CHECK(se_begin_frame(context) == SE_OK);
        se_palette pal = {};
        CHECK(se_decode_bank_palette(context, 0x1234, SE_COLOR_BANK_16, &pal) == SE_OK);
        CHECK(pal.clut_address == expected);
        CHECK(pal.count == 16);
        CHECK(pal.entries[0].g > 200 && pal.entries[0].r < 40 && pal.entries[0].b < 40);
        se_destroy(context);
    }

    // Each bank mode masks to its own boundary, so the same colour bank lands somewhere
    // different depending on how wide the palette is.
    {
        State state = MakeNbg3State();
        se_context* context = se_test::CreateContext(state);
        CHECK(context != nullptr);
        CHECK(se_begin_frame(context) == SE_OK);
        se_palette pal = {};
        CHECK(se_decode_bank_palette(context, 0x01FFu, SE_COLOR_BANK_16, &pal) == SE_OK);
        CHECK(pal.clut_address == 0x01F0u * 2u && pal.count == 16);
        CHECK(se_decode_bank_palette(context, 0x01FFu, SE_COLOR_BANK_64, &pal) == SE_OK);
        CHECK(pal.clut_address == 0x01C0u * 2u && pal.count == 64);
        CHECK(se_decode_bank_palette(context, 0x01FFu, SE_COLOR_BANK_128, &pal) == SE_OK);
        CHECK(pal.clut_address == 0x0180u * 2u && pal.count == 128);
        CHECK(se_decode_bank_palette(context, 0x01FFu, SE_COLOR_BANK_256, &pal) == SE_OK);
        CHECK(pal.clut_address == 0x0100u * 2u && pal.count == 256);
        se_destroy(context);
    }

    // RGB888 is four bytes an entry, so the same entry index is twice as far in. Taking the
    // stride from anything but the mode would put this at half the address.
    {
        State state = MakeNbg3State();
        SetReg(state, 0x00E, 0x2000);   // RAMCTL bits 12-13 = 2: RGB888, 1024 colours
        const uint32_t expected = 0x300u * 4u;
        state.cram[expected + 1] = 0x33;   // [pad][R][G][B]
        state.cram[expected + 2] = 0x22;
        state.cram[expected + 3] = 0x11;
        se_context* context = se_test::CreateContext(state);
        CHECK(context != nullptr);
        CHECK(se_begin_frame(context) == SE_OK);
        se_palette pal = {};
        CHECK(se_decode_bank_palette(context, 0x0300u, SE_COLOR_BANK_256, &pal) == SE_OK);
        CHECK(pal.mode == SE_CRAM_RGB888_1024);
        CHECK(pal.clut_address == expected);
        CHECK(pal.entries[0].r == 0x33 && pal.entries[0].g == 0x22 && pal.entries[0].b == 0x11);
        se_destroy(context);
    }
}
}  // namespace

// A hex edit to VRAM/CRAM/registers must feed straight back into the reconstructed image,
// WITHOUT a re-capture from the source (se_begin_frame) — that's what lets a paused VDP tweak
// show. Edit CRAM + a VDP2 register in place and confirm the render changes.
void TestEditReRenders()
{
    State state = MakeNbg3State();
    se_context* ctx = se_test::CreateContext(state);
    CHECK(ctx != nullptr);
    CHECK(se_begin_frame(ctx) == SE_OK);

    se_render_opts opts = {};
    for (int i = 0; i < SE_LAYER_COUNT; ++i) opts.show_layer[i] = 1;
    opts.show_vdp1_sprites = 1;
    auto render = [&](std::vector<uint8_t>& out) {
        se_image img = {};
        size_t needed = 0;
        se_render_frame(ctx, &opts, &img, &needed);
        out.assign(needed, 0);
        img.pixels = out.data();
        img.capacity = out.size();
        se_render_frame(ctx, &opts, &img, &needed);
    };

    std::vector<uint8_t> before;
    render(before);
    // NBG3 draws from CRAM entry 1 (byte offset 2 = 0x7FFF white). Recolour it to red in place
    // via se_write_vram — no se_begin_frame — and the constructed image must change.
    const uint8_t red555[2] = { 0x00, 0x1F };   // big-endian 0x001F = red only (RGB555)
    CHECK(se_write_vram(ctx, SE_VRAM_KIND_CRAM, 2, red555, 2) == 2);
    std::vector<uint8_t> after;
    render(after);
    CHECK(before != after);   // the in-place CRAM edit re-rendered
    bool sawRed = false;      // and some pixel is now the new red
    for (size_t i = 0; i + 3 < after.size(); i += 4)
        if (after[i] > 200 && after[i + 1] < 60 && after[i + 2] < 60) { sawRed = true; break; }
    CHECK(sawRed);

    // A VDP register edit also re-derives: turning NBG3 off (BGON=0) removes the layer.
    std::vector<uint8_t> withLayer;
    render(withLayer);
    CHECK(se_set_vdp2_register(ctx, 0x020, 0x0000) == 1);   // BGON
    std::vector<uint8_t> noLayer;
    render(noLayer);
    CHECK(withLayer != noLayer);

    se_destroy(ctx);
}

// A VDP-region edit must also be forwarded to the driver's write_vram callback, so a live
// emulator is poked (not just the local snapshot). Install a capturing callback and confirm the
// core hands it the same kind / offset / bytes.
namespace {
struct VramWriteCapture { int calls; se_vram_kind kind; uint32_t offset; size_t size; uint8_t first; };
VramWriteCapture gVramCap;
size_t CaptureWriteVram(void*, se_vram_kind kind, uint32_t offset, const void* src, size_t size)
{
    gVramCap.calls++;
    gVramCap.kind = kind;
    gVramCap.offset = offset;
    gVramCap.size = size;
    gVramCap.first = size ? static_cast<const uint8_t*>(src)[0] : 0;
    return size;
}
}  // namespace

void TestWriteVramForwards()
{
    State state = MakeNbg3State();
    se_data_source source = se_test::MakeSource(state);
    source.capabilities |= SE_CAP_MEM_WRITE;
    source.write_vram = CaptureWriteVram;
    se_context* ctx = se_test::CreateContext(source);
    CHECK(ctx != nullptr);
    CHECK(se_begin_frame(ctx) == SE_OK);

    gVramCap = {};
    const uint8_t bytes[2] = { 0xAB, 0xCD };
    CHECK(se_write_vram(ctx, SE_VRAM_KIND_VDP1_VRAM, 0x10, bytes, 2) == 2);
    CHECK(gVramCap.calls == 1);
    CHECK(gVramCap.kind == SE_VRAM_KIND_VDP1_VRAM);
    CHECK(gVramCap.offset == 0x10u);
    CHECK(gVramCap.size == 2u);
    CHECK(gVramCap.first == 0xAB);

    se_destroy(ctx);
}

// The edit is reported, and shown, only for the bytes the driver accepted. It used to update the
// snapshot first and ignore the driver's answer, so a refused write read back as stored.
namespace {
size_t gAccept = 0;
size_t AcceptSome(void*, se_vram_kind, uint32_t, const void*, size_t size)
{
    return gAccept < size ? gAccept : size;
}
}  // namespace

void TestWriteVramReportsWhatTheDriverTook()
{
    State state = MakeNbg3State();
    se_data_source source = se_test::MakeSource(state);
    source.capabilities |= SE_CAP_MEM_WRITE;
    source.write_vram = AcceptSome;
    se_context* ctx = se_test::CreateContext(source);
    CHECK(ctx != nullptr);
    CHECK(se_begin_frame(ctx) == SE_OK);

    uint8_t before[2] = {};
    se_read_vram(ctx, SE_VRAM_KIND_VDP1_VRAM, 0x10, before, 2);
    const uint8_t bytes[2] = { uint8_t(before[0] ^ 0xFF), uint8_t(before[1] ^ 0xFF) };

    gAccept = 0;   // rejected
    CHECK(se_write_vram(ctx, SE_VRAM_KIND_VDP1_VRAM, 0x10, bytes, 2) == 0);
    uint8_t got[2] = {};
    se_read_vram(ctx, SE_VRAM_KIND_VDP1_VRAM, 0x10, got, 2);
    CHECK(got[0] == before[0] && got[1] == before[1]);   // the view still shows the old bytes

    gAccept = 1;   // partial
    CHECK(se_write_vram(ctx, SE_VRAM_KIND_VDP1_VRAM, 0x10, bytes, 2) == 1);
    se_read_vram(ctx, SE_VRAM_KIND_VDP1_VRAM, 0x10, got, 2);
    CHECK(got[0] == bytes[0] && got[1] == before[1]);    // only the accepted byte landed

    gAccept = 2;   // all of it
    CHECK(se_write_vram(ctx, SE_VRAM_KIND_VDP1_VRAM, 0x10, bytes, 2) == 2);
    se_read_vram(ctx, SE_VRAM_KIND_VDP1_VRAM, 0x10, got, 2);
    CHECK(got[0] == bytes[0] && got[1] == bytes[1]);

    se_destroy(ctx);
}

// Snapshot validity is not VDP1-centric (SNAP-02). It used to be `!vdp1Vram.empty()`, so a
// VDP2-only source -- backgrounds, tiles, palettes, all present -- could not open at all:
// se_begin_frame returned SE_ERR_NO_DATA and nothing downstream ever ran.
void TestVdp2OnlySourceIsValid()
{
    State state;
    se_test::WriteSystemClip(state, 8, 4);
    std::fill(state.vdp2.begin(), state.vdp2.end(), uint8_t(0x5A));

    se_data_source source = se_test::MakeSource(state);
    // Deliberately no VDP1 of any kind, which is the shape a VDP2-only savestate produces.
    source.capabilities &= ~static_cast<uint32_t>(SE_CAP_VDP1_VRAM | SE_CAP_VDP1_REGS |
                                                 SE_CAP_VDP1_FB);
    source.read_vdp1_vram = nullptr;
    source.read_vdp1_reg = nullptr;
    source.read_vdp1_fb = nullptr;

    se_context* context = se_test::CreateContext(source);
    CHECK(context != nullptr);
    if (!context) return;
    CHECK(se_begin_frame(context) == SE_OK);   // the whole point: it opens
    uint8_t probe[16] = {};
    CHECK(se_read_vram(context, SE_VRAM_KIND_VDP2_VRAM, 0, probe, sizeof(probe)) == sizeof(probe));
    CHECK(probe[0] == 0x5A);
    // And a VDP1-shaped query answers "nothing here" rather than failing the context.
    CHECK(se_sprite_count(context) == 0);
    se_destroy(context);
}

// A source that supplies nothing at all is still invalid: the relaxation must not turn
// "capabilities advertised, every read empty" into a usable context.
void TestEmptySourceIsStillInvalid()
{
    State state;
    se_data_source source = se_test::MakeSource(state);
    source.read_vdp1_vram = [](void*, uint32_t, void*, size_t) -> size_t { return 0; };
    source.read_vdp2_vram = [](void*, uint32_t, void*, size_t) -> size_t { return 0; };
    source.read_cram = [](void*, uint32_t, void*, size_t) -> size_t { return 0; };
    source.read_vdp1_fb = [](void*, uint32_t, void*, size_t) -> size_t { return 0; };
    source.read_vdp1_reg = nullptr;
    source.read_vdp2_reg = nullptr;

    se_context* context = se_test::CreateContext(source);
    CHECK(context != nullptr);
    if (!context) return;
    CHECK(se_begin_frame(context) == SE_ERR_NO_DATA);
    se_destroy(context);
}

// --- Per-pixel sprite priority (VDP1-02 / VDP2-01) -----------------------------------------
//
// A VDP1 sprite pixel selects one of the eight VDP2 sprite-priority slots with a "priority
// number" encoded in its own framebuffer word, and which bits carry that number depends on the
// SPCTL sprite type. So one sprite can hold pixels at several priorities, and the hardware
// interleaves each of them with the VDP2 layers separately.
//
// The scene below is the smallest one that tells the two models apart: a 4x2 LUT-16 sprite whose
// left half uses a CLUT entry with priority number 0 and whose right half uses one with number 1,
// over an NBG3 layer at a priority between the two. Per-pixel, the left half goes behind the
// background and the right half in front. Per-command -- taking the front-most priority the
// sprite can reach, which is what this used to do -- the whole sprite lands in front and the
// background is hidden where it should show through.
State MakeMixedPrioritySpriteState()
{
    State state = MakeNbg3State();

    // NBG3 at priority 4, filling the frame. MakeNbg3State points its pattern at character 1
    // and puts white in CRAM entry 1; keep that, and give the sprite its own CRAM entries.
    SetReg(state, 0x0FA, 0x0400);   // PRINB: NBG3 priority 4

    // SPCTL: sprite type 0, SPCLMD off. Type 0 reads the priority number from bits 15-14 of the
    // pixel word, and for a LUT sprite the word is the CLUT entry -- whose bit 15 is the
    // "direct RGB" flag. With CRAM-index entries (bit 15 clear) bit 14 alone selects number
    // 0 or 1, which is exactly the two-priority sprite this test needs.
    SetReg(state, 0x0E0, 0x0000);
    // PRISA: priority number 0 -> priority 2 (behind NBG3), number 1 -> priority 6 (in front).
    SetReg(state, 0x0F0, 0x0602);

    // CRAM entries the CLUT points at: 2 = red, 3 = green.
    PutBE16(state.cram, 2 * 2, 0x801F);   // red
    PutBE16(state.cram, 3 * 2, 0x83E0);   // green

    ResizeVdp1(state, 0x200);
    PutBE16(state.vdp1, 0x20, 0x0000);            // CMDCTRL: normal sprite
    PutBE16(state.vdp1, 0x40, 0x8000);            // draw end
    PutBE16(state.vdp1, 0x24, 0x0008 | 0x0040);   // CMDPMOD: LUT-16 colour mode, SPD
    PutBE16(state.vdp1, 0x26, 0x180 / 8);         // CMDCOLR: CLUT at byte 0x180
    PutBE16(state.vdp1, 0x28, 0x100 / 8);         // CMDSRCA: texture at byte 0x100
    PutBE16(state.vdp1, 0x2A, (1 << 8) | 2);      // CMDSIZE: 8 wide, 2 tall
    PutBE16(state.vdp1, 0x2C, 0);                 // CMDXA
    PutBE16(state.vdp1, 0x2E, 0);                 // CMDYA

    // The CLUT: index 1 -> CRAM entry 2 with priority number 0; index 2 -> CRAM entry 3 with
    // bit 14 set, i.e. priority number 1.
    PutBE16(state.vdp1, 0x180 + 1 * 2, 0x0002);
    PutBE16(state.vdp1, 0x180 + 2 * 2, 0x4003);

    // 4bpp texture, 8 wide by 2 tall (the smallest CMDSIZE width is 8). The sprite is drawn at
    // x = 0 and the composited frame is only 4 wide, so screen columns 0-3 show texels 0-3 and
    // the rest is clipped away -- which is why the priority split is at texel 2 and not at the
    // middle of the texture. Texels 0-1 take CLUT index 1 (priority number 0), texels 2-3 take
    // index 2 (number 1); one byte holds two texels.
    for (int row = 0; row < 2; ++row)
    {
        const uint32_t base = 0x100 + static_cast<uint32_t>(row) * 4;   // stride = 8 px / 2
        state.vdp1[base + 0] = 0x11;   // texels 0,1 -> index 1
        state.vdp1[base + 1] = 0x22;   // texels 2,3 -> index 2
        state.vdp1[base + 2] = 0x22;   // clipped off the 4-wide frame
        state.vdp1[base + 3] = 0x22;
    }
    return state;
}

void TestSpritePriorityIsPerPixel()
{
    State state = MakeMixedPrioritySpriteState();
    const std::vector<uint8_t> pixels = Render(state, false);

    // Columns 0-1: priority 2, so NBG3 (priority 4) covers them. White, and specifically not
    // the sprite's own red.
    CHECK(IsColor(pixels, 0, 0, 255, 255, 255));
    CHECK(IsColor(pixels, 1, 0, 255, 255, 255));
    CHECK(IsColor(pixels, 0, 1, 255, 255, 255));
    CHECK(IsColor(pixels, 1, 1, 255, 255, 255));

    // Columns 2-3: priority 6, in front of NBG3. The sprite's green.
    CHECK(IsColor(pixels, 2, 0, 0, 255, 0));
    CHECK(IsColor(pixels, 3, 0, 0, 255, 0));
    CHECK(IsColor(pixels, 2, 1, 0, 255, 0));
    CHECK(IsColor(pixels, 3, 1, 0, 255, 0));
}

// The per-command summary se_sprite_2d::priority still reports the front-most priority the
// sprite's pixels reach, which is what the Command List panel shows. It is no longer what
// composites the frame -- that is the point above -- so this pins the two apart.
void TestSpritePrioritySummaryIsTheFrontMost()
{
    State state = MakeMixedPrioritySpriteState();
    se_context* context = se_test::CreateContext(state);
    CHECK(context != nullptr);
    CHECK(se_begin_frame(context) == SE_OK);
    CHECK(se_sprite_count(context) == 1);
    se_sprite_2d sprite = {};
    CHECK(se_get_sprite_2d(context, 0, &sprite) == SE_OK);
    CHECK(sprite.priority == 6);
    se_destroy(context);
}

// The per-command summary has to consider every priority number the sprite's pixels can select,
// not a representative word or two. The slots map numbers to priorities in any order, so the
// front-most priority is not the one the largest number picks.
//
// Sprite type 0xA reads the number from bits 7-6, and a 256-colour bank sprite supplies bits 7-0
// from the texel index -- so all four numbers are reachable. Here number 2 carries priority 7 and
// the other three carry 1. Probing the bank with a zero index (which is what this used to do)
// sees only number 0 and reports 1.
void TestPrioritySummaryConsidersEveryReachableNumber()
{
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0000);   // no backgrounds; this test only reads the summary
    SetReg(state, 0x0E0, 0x000A);   // SPCTL: sprite type A, SPCLMD off
    SetReg(state, 0x0F0, 0x0101);   // PRISA: number 0 -> 1, number 1 -> 1
    SetReg(state, 0x0F2, 0x0107);   // PRISB: number 2 -> 7, number 3 -> 1

    ResizeVdp1(state, 0x400);
    PutBE16(state.vdp1, 0x20, 0x0000);            // CMDCTRL: normal sprite
    PutBE16(state.vdp1, 0x40, 0x8000);            // draw end
    PutBE16(state.vdp1, 0x24, 0x0020 | 0x0040);   // CMDPMOD: 256-colour bank, SPD
    PutBE16(state.vdp1, 0x26, 0x0200);            // CMDCOLR: colour bank
    PutBE16(state.vdp1, 0x28, 0x100 / 8);         // CMDSRCA
    PutBE16(state.vdp1, 0x2A, (1 << 8) | 2);      // CMDSIZE: 8 wide, 2 tall
    for (uint32_t i = 0; i < 16; ++i) state.vdp1[0x100 + i] = 0x01;   // 8bpp, index 1 throughout

    se_context* context = se_test::CreateContext(state);
    CHECK(context != nullptr);
    CHECK(se_begin_frame(context) == SE_OK);
    CHECK(se_sprite_count(context) == 1);
    se_sprite_2d sprite = {};
    CHECK(se_get_sprite_2d(context, 0, &sprite) == SE_OK);
    CHECK(sprite.priority == 7);
    se_destroy(context);
}

// A half-transparent sprite pixel BETWEEN two VDP2 contributions, the higher of which does colour
// calculation. This is the case the per-pixel priority change made ordinary, and it is where
// blending against the top of the column instead of against what is under the sprite goes wrong:
// the sprite would carry the higher layer's colour, and that layer then colour-calculates against
// the sprite as its second contribution -- blending itself in twice.
//
// Layout, bottom to top: back screen (blue, priority 0) < half-transparent red sprite (priority 1)
// < NBG3 (white, priority 4, colour calc on, ratio 15).
//
// Correct: the sprite halves against the BACK SCREEN, so (255,0,0) over (0,0,255) is (127,0,127);
// NBG3 then blends 16/32 with that, giving (191,127,191).
// The bug gave the sprite (191,63,127) -- halved against NBG3-over-back-screen -- and a final
// (223,159,191).
void TestHalfTransparentSpriteBlendsAgainstWhatIsBelowIt()
{
    State state = MakeNbg3State();
    SetReg(state, 0x0FA, 0x0400);   // PRINB: NBG3 priority 4
    SetReg(state, 0x0AC, 0x0000);   // BKTAU / BKTAL: back-screen table at VDP2 0x200
    SetReg(state, 0x0AE, 0x0100);
    PutBE16(state.vdp2, 0x200, 0x7C00);   // back screen: RGB555 blue
    SetReg(state, 0x0EC, 0x0008);   // CCCTL: NBG3 colour-calc enable
    SetReg(state, 0x10A, 0x0F00);   // CCRNB: NBG3 ratio 15

    // SPCTL type 0, SPCLMD off; priority number 0 -> priority 1, so every sprite pixel is at 1.
    SetReg(state, 0x0E0, 0x0000);
    SetReg(state, 0x0F0, 0x0001);

    PutBE16(state.cram, 2 * 2, 0x801F);   // CRAM entry 2: red

    ResizeVdp1(state, 0x200);
    PutBE16(state.vdp1, 0x20, 0x0000);                     // CMDCTRL: normal sprite
    PutBE16(state.vdp1, 0x40, 0x8000);                     // draw end
    PutBE16(state.vdp1, 0x24, 0x0008 | 0x0040 | 0x0003);   // LUT-16, SPD, half-transparency
    PutBE16(state.vdp1, 0x26, 0x180 / 8);                  // CMDCOLR: CLUT at 0x180
    PutBE16(state.vdp1, 0x28, 0x100 / 8);                  // CMDSRCA
    PutBE16(state.vdp1, 0x2A, (1 << 8) | 2);               // CMDSIZE: 8x2
    PutBE16(state.vdp1, 0x180 + 1 * 2, 0x0002);            // CLUT index 1 -> CRAM 2, number 0
    for (int row = 0; row < 2; ++row)
        for (int b = 0; b < 4; ++b) state.vdp1[0x100 + row * 4 + b] = 0x11;

    const std::vector<uint8_t> pixels = Render(state, false, true);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
        {
            // NBG3 (prio 4, cc on at ratio 15) over the half-transparent sprite (prio 1) over
            // the blue back screen (prio 0): NBG3 white blends 1:1 with the sprite's own blend of
            // red over blue.
            CHECK(IsColor(pixels, x, y, 191, 127, 191));
            // The pre-fix answer: the sprite blended against NBG3 -- which is above it -- and
            // NBG3 then colour-calculated against that, mixing itself in twice.
            CHECK(!IsColor(pixels, x, y, 223, 159, 191));
        }
}

// Review 5334449098: the four-layer case, where the contribution below the sprite is itself a
// colour-calculating VDP2 layer with something under it.
//
// Stack: NBG3 (prio 5, cc) > half-transparent sprite (prio 3) > NBG2 (prio 2, cc) > back screen.
// A column keeps its top two contributions, so the back screen is already evicted by the time the
// sprite arrives, and the sprite blends against NBG2's own colour rather than NBG2 blended with
// the back screen.
//
// That is the model, not a shortfall of ResolveBelow. Standard VDP2 colour calculation blends the
// top contribution with the one immediately below it, so a layer's cc-enable does nothing while it
// is third in the stack -- NBG2 is below the sprite here, so it never blends with the back screen.
// ResolveColumn has the same property from the other side: it blends the top against the *raw*
// second, never a resolved one. Blending second-with-third is extended colour calculation
// (3-layer, roadmap C6), which the mixer does not implement anywhere; giving the sprite path a
// third retained contribution would make it the only place that did.
void TestSpriteBetweenTwoColorCalcLayers()
{
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x000C);   // BGON: NBG2 + NBG3
    SetReg(state, 0x034, 0x8000);   // PNCN2: one-word
    SetReg(state, 0x048, 0x0002);   // MPABN2: plane A map number 2 -> name table at 0x4000
    SetReg(state, 0x0FA, 0x0502);   // PRINB: NBG3 priority 5, NBG2 priority 2
    SetReg(state, 0x0AC, 0x0000);   // back-screen table at VDP2 0x200
    SetReg(state, 0x0AE, 0x0100);
    PutBE16(state.vdp2, 0x200, 0x7C00);   // back screen: blue
    SetReg(state, 0x0EC, 0x000C);   // CCCTL: colour calc on for both NBG2 and NBG3
    SetReg(state, 0x10A, 0x0F0F);   // CCRNB: ratio 15 for both

    PutBE16(state.vdp2, 0x4000, 0x0002);   // NBG2 plane A -> character 2
    std::fill(state.vdp2.begin() + 0x40, state.vdp2.begin() + 0x60, 0x33);   // index 3
    PutBE16(state.cram, 3 * 2, 0x03E0);    // CRAM entry 3: green (NBG2)
    PutBE16(state.cram, 2 * 2, 0x801F);    // CRAM entry 2: red (sprite)

    // SPCTL type 0; priority number 0 -> PRISA low byte, so every sprite pixel is at priority 3.
    SetReg(state, 0x0E0, 0x0000);
    SetReg(state, 0x0F0, 0x0003);

    ResizeVdp1(state, 0x200);
    PutBE16(state.vdp1, 0x20, 0x0000);                     // CMDCTRL: normal sprite
    PutBE16(state.vdp1, 0x40, 0x8000);                     // draw end
    PutBE16(state.vdp1, 0x24, 0x0008 | 0x0040 | 0x0003);   // LUT-16, SPD, half-transparency
    PutBE16(state.vdp1, 0x26, 0x180 / 8);                  // CMDCOLR: CLUT at 0x180
    PutBE16(state.vdp1, 0x28, 0x100 / 8);                  // CMDSRCA
    PutBE16(state.vdp1, 0x2A, (1 << 8) | 2);               // CMDSIZE: 8x2
    PutBE16(state.vdp1, 0x180 + 1 * 2, 0x0002);            // CLUT index 1 -> CRAM 2, number 0
    for (int row = 0; row < 2; ++row)
        for (int b = 0; b < 4; ++b) state.vdp1[0x100 + row * 4 + b] = 0x11;

    const std::vector<uint8_t> pixels = Render(state, false, true);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
        {
            // Sprite red halved against NBG2 green is (127,127,0); NBG3 white then blends 1:1.
            CHECK(IsColor(pixels, x, y, 191, 191, 127));
            // What a resolved-second (3-layer) model would give: green blended with the blue back
            // screen first. Pinned so C6 has to change this test deliberately.
            CHECK(!IsColor(pixels, x, y, 191, 159, 159));
        }
}

int main()
{
    TestSpriteBetweenTwoColorCalcLayers();
    TestHalfTransparentSpriteBlendsAgainstWhatIsBelowIt();
    TestPrioritySummaryConsidersEveryReachableNumber();
    TestSpritePriorityIsPerPixel();
    TestSpritePrioritySummaryIsTheFrontMost();
    TestVdp2OnlySourceIsValid();
    TestEmptySourceIsStillInvalid();
    TestEditReRenders();
    TestWriteVramForwards();
    TestWriteVramReportsWhatTheDriverTook();
    TestCaptureIsBracketedAndFrameNumberIsTheCapturedOne();
    TestRectangularWindow();
    TestLineWindow();
    TestVerticalPlaneSize();
    TestTransparentPixelDisable();
    TestBackScreen();
    TestColorCalc();
    TestRotationIdentity();
    TestRotationParamWindow();
    TestRotationCoeffTableOffset();
    TestBitmapNbg0();
    TestBitmapRgb888();
    TestZoomBitmap();
    TestColorOffset();
    TestMosaic();
    TestSpriteHalfLuminance();
    TestSpriteMesh();
    TestDrawEndNotDrawn();
    TestPolygon();
    TestLine();
    TestUserClip();
    TestUserClipDefaultUnbounded();
    TestBankPaletteReportsItsCramAddress();
    if (gFailures != 0)
    {
        std::cerr << gFailures << " VDP2 compositor check(s) failed\n";
        return 1;
    }
    std::cout << "VDP2 compositor tests passed\n";
    return 0;
}
