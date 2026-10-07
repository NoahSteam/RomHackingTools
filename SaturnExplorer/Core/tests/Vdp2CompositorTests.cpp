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
    // PRISA..PRISD: every sprite priority number maps to priority 1. A sprite pixel whose number
    // maps to priority 0 is not displayed, so a fixture that draws one has to say where it goes.
    for (uint32_t reg = 0x0F0; reg <= 0x0F6; reg += 2) SetReg(state, reg, 0x0101);
    // SPCTL: type 0 with SPCLMD set, so a framebuffer word with its MSB set is an RGB colour and one
    // with it clear is a palette code. (SPCLMD clear makes every word a palette code.)
    SetReg(state, 0x0E0, 0x0020);
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
    // sprite is drawn at half luminance. VDP1 halves the 5-bit channels (31 -> 15), so the
    // result is (123,123,123), not the (127,127,127) that halving the expanded byte gives.
    State state = MakeSpriteState(0x0028 | 0x0040 | 0x0002);
    const std::vector<uint8_t> pixels = Render(state, false);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColor(pixels, x, y, 123, 123, 123));
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
    PutBE16(state.vdp1, 0x26, 0x801F);   // CMDCOLR: red
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
    PutBE16(state.vdp1, 0x26, 0x801F);   // CMDCOLR: red (RGB555)
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
    PutBE16(state.vdp1, 0x26, 0x801F);   // CMDCOLR: red
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
    PutBE16(state.vdp1, 0x46, 0x801F);   // CMDCOLR: red
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
    PutBE16(state.vdp1, 0x26, 0x801F);   // CMDCOLR: red
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

// VDP1 half-transparency averages a pixel with the VDP1 FRAMEBUFFER pixel under it, never with a
// VDP2 layer: VDP2 only ever sees the finished framebuffer. A half-transparent sprite drawn over
// untouched framebuffer therefore comes out as itself, and the VDP2 layers above it colour-
// calculate against that.
//
// Layout, bottom to top: back screen (blue, priority 0) < half-transparent red sprite (priority 1)
// < NBG3 (white, priority 4, colour calc on, ratio 15).
//
// Correct: the sprite is plain red; NBG3 then blends 16/32 with it, giving (255,127,127).
// The old behaviour averaged the sprite with the back screen first, giving (191,127,191).
void TestHalfTransparentSpriteDoesNotBlendWithVdp2()
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
            CHECK(IsColor(pixels, x, y, 255, 127, 127));
            CHECK(!IsColor(pixels, x, y, 191, 127, 191));
        }
}

// Review 5334449098: the four-layer case, a sprite between two colour-calculating VDP2 layers.
//
// Stack: NBG3 (prio 5, cc) > sprite (prio 3) > NBG2 (prio 2, cc) > back screen.
// A column keeps its top two contributions, so NBG3 blends with the sprite directly under it and
// NBG2 -- third in the stack -- takes no part, whatever its own cc-enable says. Blending
// second-with-third is extended colour calculation (3-layer, roadmap C6), which the mixer does
// not implement.
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
            // NBG3 white blends 1:1 with the red sprite directly beneath it.
            CHECK(IsColor(pixels, x, y, 255, 127, 127));
            // Nothing of NBG2 (green) or the back screen (blue) shows through.
            CHECK(!IsColor(pixels, x, y, 191, 191, 127));
        }
}

// ---- VDP1 framebuffer semantics ---------------------------------------------------------------
//
// Review round S. VDP1 draws its commands into a framebuffer in list order; VDP2 only ever sees
// the finished framebuffer. These fixtures use frames of their own size, so they render through
// RenderSized rather than the 4x2 Render above.

// Render a width x height frame; 'extra' sets any render options a case needs beyond the layers
// and sprites (window, colour calculation, shadow).
template <typename Setup>
std::vector<uint8_t> RenderWith(State& state, int width, int height, Setup&& extra)
{
    se_context* context = se_test::CreateContext(state);
    CHECK(context != nullptr);
    CHECK(se_begin_frame(context) == SE_OK);
    se_render_opts options = {};
    for (int i = 0; i < SE_LAYER_COUNT; ++i) options.show_layer[i] = 1;
    options.show_vdp1_sprites = 1;
    extra(options);
    se_image image = {};
    size_t needed = 0;
    CHECK(se_render_frame(context, &options, &image, &needed) == SE_OK);
    std::vector<uint8_t> pixels(needed);
    image.pixels = pixels.data();
    image.capacity = pixels.size();
    CHECK(se_render_frame(context, &options, &image, &needed) == SE_OK);
    CHECK(image.width == static_cast<uint32_t>(width));
    CHECK(image.height == static_cast<uint32_t>(height));
    se_destroy(context);
    return pixels;
}

std::vector<uint8_t> RenderSized(State& state, int width, int height)
{
    return RenderWith(state, width, height, [](se_render_opts&) {});
}

bool IsColorAt(const std::vector<uint8_t>& pixels, int width, int x, int y,
               uint8_t r, uint8_t g, uint8_t b)
{
    const size_t o = static_cast<size_t>(y * width + x) * 4;
    return pixels[o] == r && pixels[o + 1] == g && pixels[o + 2] == b;
}

// A frame with no VDP2 layers over a blue back screen, 'width' x 'height'.
State MakeBlueBackState(int width, int height)
{
    State state = MakeNbg3State();
    SetReg(state, 0x020, 0x0000);   // BGON off
    SetReg(state, 0x0AC, 0x0000);   // back-screen table at VDP2 0x200
    SetReg(state, 0x0AE, 0x0100);
    PutBE16(state.vdp2, 0x200, 0x7C00);   // blue
    ResizeVdp1(state, 0x400);
    se_test::WriteSystemClip(state, width, height);
    return state;
}

// A polygon over the inclusive pixel box (x0,y0)-(x1,y1) at command address 'cmd'.
void PutPolygon(State& state, uint32_t cmd, uint16_t color, uint16_t pmod,
                int x0, int y0, int x1, int y1)
{
    PutBE16(state.vdp1, cmd + 0x00, 0x0004);
    PutBE16(state.vdp1, cmd + 0x04, pmod);
    PutBE16(state.vdp1, cmd + 0x06, color);
    PutBE16(state.vdp1, cmd + 0x0C, static_cast<uint16_t>(x0));
    PutBE16(state.vdp1, cmd + 0x0E, static_cast<uint16_t>(y0));
    PutBE16(state.vdp1, cmd + 0x10, static_cast<uint16_t>(x1));
    PutBE16(state.vdp1, cmd + 0x12, static_cast<uint16_t>(y0));
    PutBE16(state.vdp1, cmd + 0x14, static_cast<uint16_t>(x1));
    PutBE16(state.vdp1, cmd + 0x16, static_cast<uint16_t>(y1));
    PutBE16(state.vdp1, cmd + 0x18, static_cast<uint16_t>(x0));
    PutBE16(state.vdp1, cmd + 0x1A, static_cast<uint16_t>(y1));
}

// VDP1 resolves overlap in command order, before VDP2 priority has any say. A later pixel replaces
// an earlier one however the two rank: the first polygon's priority number maps to 7 and the second
// one's to 1, and the second still wins because it was drawn last.
void TestLaterVdp1CommandReplacesEarlierRegardlessOfPriority()
{
    State state = MakeBlueBackState(4, 2);
    SetReg(state, 0x0F0, 0x0107);   // PRISA: number 0 -> priority 7, number 1 -> priority 1
    PutBE16(state.cram, 31 * 2, 0x001F);   // red
    PutBE16(state.cram, 32 * 2, 0x03E0);   // green
    PutPolygon(state, 0x20, 0x001F, 0, 0, 0, 3, 1);   // palette code: number 0, colour 31 (red)
    PutPolygon(state, 0x40, 0x4020, 0, 0, 0, 3, 1);   // palette code: number 1, colour 32 (green)
    PutBE16(state.vdp1, 0x60, 0x8000);
    const std::vector<uint8_t> pixels = RenderSized(state, 4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColorAt(pixels, 4, x, y, 0, 255, 0));
}

// A sprite pixel whose priority maps to 0 is not displayed.
void TestSpritePriorityZeroIsSuppressed()
{
    State state = MakeBlueBackState(4, 2);
    SetReg(state, 0x0F0, 0x0100);   // PRISA: number 0 -> priority 0 (an RGB word is always number 0)
    PutPolygon(state, 0x20, 0x801F, 0, 0, 0, 3, 1);   // red RGB
    PutBE16(state.vdp1, 0x40, 0x8000);
    const std::vector<uint8_t> pixels = RenderSized(state, 4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColorAt(pixels, 4, x, y, 0, 0, 255));
}

// A normal sprite is 'width' x 'height' pixels. Its corners are inclusive pixel indices, so the far
// corner is at x + width - 1; using x + width drew an extra row and column.
void TestNormalSpriteCoversExactlyItsSize()
{
    State state = MakeBlueBackState(16, 4);
    PutBE16(state.vdp1, 0x20, 0x0000);                 // normal sprite
    PutBE16(state.vdp1, 0x24, 0x0028 | 0x0040);        // RGB555 + SPD
    PutBE16(state.vdp1, 0x28, 0x100 / 8);              // CMDSRCA
    PutBE16(state.vdp1, 0x2A, (1 << 8) | 1);           // 8 x 1
    PutBE16(state.vdp1, 0x2C, 2);                      // at (2,1)
    PutBE16(state.vdp1, 0x2E, 1);
    PutBE16(state.vdp1, 0x40, 0x8000);
    for (uint32_t i = 0; i < 8; ++i) PutBE16(state.vdp1, 0x100 + i * 2, 0x801F);
    const std::vector<uint8_t> pixels = RenderSized(state, 16, 4);
    int red = 0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 16; ++x)
        {
            const bool isRed = IsColorAt(pixels, 16, x, y, 255, 0, 0);
            red += isRed ? 1 : 0;
            CHECK(isRed == (y == 1 && x >= 2 && x <= 9));
        }
    CHECK(red == 8);
}

// Half-transparency averages with the VDP1 framebuffer pixel under it, and only when that holds a
// colour. Untouched framebuffer does not, so the polygon replaces it -- the VDP2 layer behind (blue)
// takes no part.
void TestHalfTransparencyIgnoresVdp2Background()
{
    State state = MakeNbg3State();   // NBG3 white, priority 1
    SetReg(state, 0x0AC, 0x0000);
    SetReg(state, 0x0AE, 0x0100);
    PutBE16(state.vdp2, 0x200, 0x7C00);
    ResizeVdp1(state, 0x60);
    PutPolygon(state, 0x20, 0x801F, 0x0003, 0, 0, 3, 1);
    PutBE16(state.vdp1, 0x40, 0x8000);
    const std::vector<uint8_t> pixels = RenderSized(state, 4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColorAt(pixels, 4, x, y, 255, 0, 0));
}

// VDP1 colour arithmetic is on the framebuffer's 5-bit channels: red (31,0,0) averaged with blue
// (0,0,31) is (15,0,15), which displays as (123,0,123) -- not (127,0,127).
void TestHalfTransparencyAveragesFiveBitChannels()
{
    State state = MakeBlueBackState(4, 2);
    PutPolygon(state, 0x20, 0x801F, 0x0000, 0, 0, 3, 1);   // opaque red
    PutPolygon(state, 0x40, 0xFC00, 0x0003, 0, 0, 3, 1);   // half-transparent blue over it
    PutBE16(state.vdp1, 0x60, 0x8000);
    const std::vector<uint8_t> pixels = RenderSized(state, 4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColorAt(pixels, 4, x, y, 123, 0, 123));
}

// Shadow halves the framebuffer pixel under it (in 5-bit channels); over untouched framebuffer it
// draws nothing.
void TestShadowHalvesTheFramebufferPixel()
{
    State state = MakeBlueBackState(4, 2);
    PutPolygon(state, 0x20, 0xFFFF, 0x0000, 0, 0, 1, 1);   // white in columns 0-1
    PutPolygon(state, 0x40, 0x801F, 0x0001, 0, 0, 3, 1);   // shadow across all four
    PutBE16(state.vdp1, 0x60, 0x8000);
    const std::vector<uint8_t> pixels = RenderSized(state, 4, 2);
    for (int y = 0; y < 2; ++y)
    {
        CHECK(IsColorAt(pixels, 4, 0, y, 123, 123, 123));
        CHECK(IsColorAt(pixels, 4, 1, y, 123, 123, 123));
        CHECK(IsColorAt(pixels, 4, 2, y, 0, 0, 255));   // nothing under it: blue back screen
        CHECK(IsColorAt(pixels, 4, 3, y, 0, 0, 255));
    }
}

// An 8x1 texture whose pixels are the nibbles of 'bytes', drawn at (0,0) of an 8x2
// frame with the colour-bank 16 mode. Palette entry 1 is red, entry 15 is green; 'pmod' picks the
// end-code setting.
std::vector<uint8_t> RenderEndCodeRow(const uint8_t bytes[4], uint16_t pmod)
{
    State state = MakeBlueBackState(8, 2);
    PutBE16(state.vdp1, 0x20, 0x0000);
    PutBE16(state.vdp1, 0x24, pmod);
    PutBE16(state.vdp1, 0x28, 0x100 / 8);
    PutBE16(state.vdp1, 0x2A, (1 << 8) | 1);
    PutBE16(state.vdp1, 0x40, 0x8000);
    for (int i = 0; i < 4; ++i) state.vdp1[0x100 + i] = bytes[i];
    PutBE16(state.cram, 1 * 2, 0x801F);
    PutBE16(state.cram, 15 * 2, 0x83E0);
    return RenderSized(state, 8, 2);
}

// With the end code enabled (CMDPMOD bit 7 clear) an end-code texel is not drawn, and a second one
// in a row ends the texture line: F,F,1,1,... draws nothing at all.
void TestEndCodeTerminatesTheTextureRow()
{
    const uint8_t row[4] = { 0xFF, 0x11, 0x11, 0x11 };   // F F 1 1 1 1 1 1
    const std::vector<uint8_t> pixels = RenderEndCodeRow(row, 0x0000);
    for (int x = 0; x < 8; ++x) CHECK(IsColorAt(pixels, 8, x, 0, 0, 0, 255));

    // End code disabled (bit 7 set): F is an ordinary colour (green), the 1s are red.
    const std::vector<uint8_t> disabled = RenderEndCodeRow(row, 0x0080);
    CHECK(IsColorAt(disabled, 8, 0, 0, 0, 255, 0));
    CHECK(IsColorAt(disabled, 8, 1, 0, 0, 255, 0));
    for (int x = 2; x < 8; ++x) CHECK(IsColorAt(disabled, 8, x, 0, 255, 0, 0));
}

// A lone end code is just transparent: the row carries on.
void TestLoneEndCodeIsOnlyTransparent()
{
    const uint8_t row[4] = { 0xF1, 0x11, 0x11, 0x11 };   // F 1 1 1 1 1 1 1
    const std::vector<uint8_t> pixels = RenderEndCodeRow(row, 0x0000);
    CHECK(IsColorAt(pixels, 8, 0, 0, 0, 0, 255));
    for (int x = 1; x < 8; ++x) CHECK(IsColorAt(pixels, 8, x, 0, 255, 0, 0));
}

// A command with JP >= 4 is linked through but not executed, so it must not move the local origin
// either: the polygon after a skipped "local coordinate X=20" stays at X=0.
void TestSkippedStateCommandsChangeNothing()
{
    State state = MakeBlueBackState(4, 2);
    PutBE16(state.vdp1, 0x20, 0x400A);   // local coordinate, JP = skip-next
    PutBE16(state.vdp1, 0x2C, 20);
    PutBE16(state.vdp1, 0x2E, 0);
    PutPolygon(state, 0x40, 0x801F, 0, 0, 0, 3, 1);
    PutBE16(state.vdp1, 0x60, 0x8000);
    const std::vector<uint8_t> skipped = RenderSized(state, 4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColorAt(skipped, 4, x, y, 255, 0, 0));

    // The same command executed does move it off the frame.
    PutBE16(state.vdp1, 0x20, 0x000A);
    const std::vector<uint8_t> moved = RenderSized(state, 4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColorAt(moved, 4, x, y, 0, 0, 255));
}

// In a hi-res mode VDP1 draws at half the display width and every column is doubled at scan-out.
// A user clip of X = 1..1 therefore covers display columns 2 and 3.
void TestHiResUserClipCoversBothDoubledColumns()
{
    State state = MakeBlueBackState(320, 224);
    SetReg(state, 0x000, 0x0002);   // TVMD: HRES = hi-res, 640 wide
    PutBE16(state.vdp1, 0x20, 0x0008);   // user clip: (1,0) - (1,223)
    PutBE16(state.vdp1, 0x2C, 1);
    PutBE16(state.vdp1, 0x2E, 0);
    PutBE16(state.vdp1, 0x34, 1);
    PutBE16(state.vdp1, 0x36, 223);
    PutPolygon(state, 0x40, 0x801F, 0x0400, 0, 0, 5, 2);   // user clipping on, draw inside
    PutBE16(state.vdp1, 0x60, 0x8000);
    const std::vector<uint8_t> pixels = RenderSized(state, 640, 224);
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 6; ++x)
            CHECK(IsColorAt(pixels, 640, x, y, 255, 0, 0) == (x == 2 || x == 3));
}

// Drawing coordinates are 13-bit signed and the local origin 11-bit signed, not 16-bit: 0x1FFF is -1
// and a local 0x07FF is -1. Read as int16 they were 8191 and 2047, which throws the primitive off
// screen.
void TestCoordinatesAreSignExtendedFromTheirHardwareWidth()
{
    State state = MakeBlueBackState(4, 2);
    PutPolygon(state, 0x20, 0x801F, 0, 0x1FFF, 0x1FFF, 3, 1);   // (-1,-1) - (3,1)
    PutBE16(state.vdp1, 0x40, 0x8000);
    const std::vector<uint8_t> drawn = RenderSized(state, 4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColorAt(drawn, 4, x, y, 255, 0, 0));

    State local = MakeBlueBackState(4, 2);
    PutBE16(local.vdp1, 0x20, 0x000A);   // local coordinate (0x07FF, 0x07FF) == (-1,-1)
    PutBE16(local.vdp1, 0x2C, 0x07FF);
    PutBE16(local.vdp1, 0x2E, 0x07FF);
    PutPolygon(local, 0x40, 0x801F, 0, 1, 1, 4, 2);   // + origin -> (0,0) - (3,1)
    PutBE16(local.vdp1, 0x60, 0x8000);
    const std::vector<uint8_t> shifted = RenderSized(local, 4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColorAt(shifted, 4, x, y, 255, 0, 0));
}

// An 8x1 RGB555 texture, drawn at (0,0) of an 8x2 frame with 'pmod'.
std::vector<uint8_t> RenderRgbRow(const uint16_t words[8], uint16_t pmod)
{
    State state = MakeBlueBackState(8, 2);
    PutBE16(state.vdp1, 0x20, 0x0000);
    PutBE16(state.vdp1, 0x24, pmod);
    PutBE16(state.vdp1, 0x28, 0x100 / 8);
    PutBE16(state.vdp1, 0x2A, (1 << 8) | 1);
    PutBE16(state.vdp1, 0x40, 0x8000);
    for (uint32_t i = 0; i < 8; ++i) PutBE16(state.vdp1, 0x100 + i * 2, words[i]);
    return RenderSized(state, 8, 2);
}

// With SPD clear every RGB word below 0x4000 is transparent, not just zero: 0x0001 is not a
// dark-red pixel. 0x4000 itself is not transparent once the end code is disabled -- it is a
// palette code (MSB clear), here CRAM entry 0.
void TestRgbTexelsBelow0x4000AreTransparent()
{
    const uint16_t row[8] = { 0x0001, 0x3FFF, 0x4000, 0x801F, 0x801F, 0x801F, 0x801F, 0x801F };
    const std::vector<uint8_t> pixels = RenderRgbRow(row, 0x0028 | 0x0080);   // RGB555, ECD set
    CHECK(IsColorAt(pixels, 8, 0, 0, 0, 0, 255));
    CHECK(IsColorAt(pixels, 8, 1, 0, 0, 0, 255));
    CHECK(IsColorAt(pixels, 8, 2, 0, 0, 0, 0));   // 0x4000: drawn, as CRAM entry 0 (black)
    CHECK(IsColorAt(pixels, 8, 3, 0, 255, 0, 0));
}

// RGB end codes are every word in 0x4000-0x7FFF (MSB clear, bit 14 set), and the second one met
// ends the line.
void TestRgbEndCodesSpanTheWholeRange()
{
    const uint16_t row[8] = { 0x4001, 0x5000, 0x801F, 0x801F, 0x801F, 0x801F, 0x801F, 0x801F };
    const std::vector<uint8_t> pixels = RenderRgbRow(row, 0x0028);   // end code enabled
    for (int x = 0; x < 8; ++x) CHECK(IsColorAt(pixels, 8, x, 0, 0, 0, 255));
}

// The hardware counts end codes over the line -- the second one met ends it even with colours in
// between, not only two in a row.
void TestEndCodesAreCountedAcrossTheLine()
{
    const uint8_t row[4] = { 0x1F, 0x1F, 0x11, 0x11 };   // 1 F 1 F 1 1 1 1
    const std::vector<uint8_t> pixels = RenderEndCodeRow(row, 0x0000);
    CHECK(IsColorAt(pixels, 8, 0, 0, 255, 0, 0));
    CHECK(IsColorAt(pixels, 8, 1, 0, 0, 0, 255));   // first end code: transparent
    CHECK(IsColorAt(pixels, 8, 2, 0, 255, 0, 0));
    for (int x = 3; x < 8; ++x) CHECK(IsColorAt(pixels, 8, x, 0, 0, 0, 255));   // second ends it
}

// Half-transparency tests the destination's MSB, not what kind of primitive wrote it: over a pixel
// whose MSB is clear (a palette code) the half-transparent pixel simply replaces it.
void TestHalfTransparencyNeedsAnMsbSetDestination()
{
    State state = MakeBlueBackState(4, 2);
    PutPolygon(state, 0x20, 0x001F, 0x0000, 0, 0, 3, 1);   // MSB clear: a palette code
    PutPolygon(state, 0x40, 0x83E0, 0x0003, 0, 0, 3, 1);   // green, half-transparent
    PutBE16(state.vdp1, 0x60, 0x8000);
    const std::vector<uint8_t> pixels = RenderSized(state, 4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColorAt(pixels, 4, x, y, 0, 255, 0));
}

// VDP2 reads the priority (and colour) out of the framebuffer word, so a draw-mode effect has to
// change the word, not just a colour kept beside it. Half-luminance turns 0xC01F into 0xA00F: its
// priority number goes from 3 to 2, which must move it behind the background.
void TestEffectsChangeThePriorityTheWordSelects()
{
    State state = MakeNbg3State();   // NBG3 white
    SetReg(state, 0x0E0, 0x0000);    // SPCTL type 0, SPCLMD clear: every word is a palette code
    SetReg(state, 0x0FA, 0x0300);    // NBG3 priority 3
    SetReg(state, 0x0F2, 0x0601);    // number 2 -> priority 1, number 3 -> priority 6
    PutBE16(state.cram, 0x01F * 2, 0x001F);   // the palette colours either word resolves to
    PutBE16(state.cram, 0x00F * 2, 0x03E0);
    ResizeVdp1(state, 0x400);
    PutPolygon(state, 0x20, 0xC01F, 0x0002, 0, 0, 3, 1);   // half-luminance
    PutBE16(state.vdp1, 0x40, 0x8000);
    const std::vector<uint8_t> pixels = RenderSized(state, 4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColorAt(pixels, 4, x, y, 255, 255, 255));   // behind NBG3 after halving

    // Control: without the effect the word keeps number 3 and sits in front, as its palette colour.
    PutPolygon(state, 0x20, 0xC01F, 0x0000, 0, 0, 3, 1);
    const std::vector<uint8_t> plain = RenderSized(state, 4, 2);
    CHECK(IsColorAt(plain, 4, 0, 0, 255, 0, 0));
}

// A word with its MSB clear is a palette code, whatever drew it: a polygon of 0x4020 is CRAM entry
// 32 (here blue), not the RGB555 colour those bits would spell.
void TestPolygonPaletteCodesAreLookedUpInCram()
{
    State state = MakeBlueBackState(4, 2);
    PutBE16(state.vdp2, 0x200, 0x03E0);   // green back screen
    PutBE16(state.cram, 32 * 2, 0x7C00);  // CRAM 32: blue
    PutPolygon(state, 0x20, 0x4020, 0, 0, 0, 3, 1);
    PutBE16(state.vdp1, 0x40, 0x8000);
    const std::vector<uint8_t> pixels = RenderSized(state, 4, 2);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 4; ++x)
            CHECK(IsColorAt(pixels, 4, x, y, 0, 0, 255));
}

// A one-pixel-thin sprite is drawn, so it has to be selectable: its own corners are a zero-area
// quad, and the hit test has to use the pixels the rasterizer fills instead.
void TestThinSpriteCanBeHitTested()
{
    State state = MakeBlueBackState(16, 4);
    PutBE16(state.vdp1, 0x20, 0x0000);
    PutBE16(state.vdp1, 0x24, 0x0028 | 0x0040);
    PutBE16(state.vdp1, 0x28, 0x100 / 8);
    PutBE16(state.vdp1, 0x2A, (1 << 8) | 1);   // 8 x 1
    PutBE16(state.vdp1, 0x2C, 2);
    PutBE16(state.vdp1, 0x2E, 1);
    PutBE16(state.vdp1, 0x40, 0x8000);
    for (uint32_t i = 0; i < 8; ++i) PutBE16(state.vdp1, 0x100 + i * 2, 0x801F);
    se_context* context = se_test::CreateContext(state);
    CHECK(context != nullptr);
    CHECK(se_begin_frame(context) == SE_OK);
    size_t cmd = 99;
    for (int x = 0; x < 16; ++x)
        for (int y = 0; y < 4; ++y)
        {
            const bool drawn = (y == 1 && x >= 2 && x <= 9);
            CHECK((se_hit_test(context, x, y, &cmd) == SE_OK) == drawn);
        }
    se_destroy(context);
}

// ---- VDP2 sprite colour calculation, shadow, window ---------------------------------------------

// A frame whose only VDP2 content is a blue back screen, with every sprite priority number mapped
// to 1 and SPCTL = 'spctl'.
State MakeSpriteVdp2State(uint16_t spctl)
{
    State state = MakeBlueBackState(4, 2);
    SetReg(state, 0x0E0, spctl);
    return state;
}

// The sprite layer takes part in colour calculation: CCCTL SPCCEN enables it, SPCCCS picks the
// condition on the pixel's priority, and the ratio comes from CCRSx by the pixel's own colour-
// calculation bits. Red over the blue back screen at ratio 15 is (127,0,127).
void TestSpriteColorCalculation()
{
    auto render = [](uint16_t spctl, uint16_t ccctl, uint16_t ccrsa, uint16_t word, uint16_t cramRed)
    {
        State state = MakeSpriteVdp2State(spctl);
        SetReg(state, 0x0EC, ccctl);
        SetReg(state, 0x100, ccrsa);
        PutBE16(state.cram, 31 * 2, cramRed);
        PutPolygon(state, 0x20, word, 0, 0, 0, 3, 1);
        PutBE16(state.vdp1, 0x40, 0x8000);
        return RenderWith(state, 4, 2, [](se_render_opts& o) { o.show_color_calculation = 1; });
    };
    // Type 0, palette-only; condition 0 (priority <= 7): enabled, ratio number 0 = 15.
    CHECK(IsColorAt(render(0x0700, 0x0040, 0x000F, 0x001F, 0x001F), 4, 1, 1, 127, 0, 127));
    // SPCCEN clear: plain red.
    CHECK(IsColorAt(render(0x0700, 0x0000, 0x000F, 0x001F, 0x001F), 4, 1, 1, 255, 0, 0));
    // Condition 1 (priority == 2): the pixel's priority is 1, so no colour calculation.
    CHECK(IsColorAt(render(0x1200, 0x0040, 0x000F, 0x001F, 0x001F), 4, 1, 1, 255, 0, 0));
    // Condition 2 (priority >= 1): enabled.
    CHECK(IsColorAt(render(0x2100, 0x0040, 0x000F, 0x001F, 0x001F), 4, 1, 1, 127, 0, 127));
    // Condition 3 follows the colour's own MSB in CRAM: set -> blended, clear -> plain.
    CHECK(IsColorAt(render(0x3000, 0x0040, 0x000F, 0x001F, 0x801F), 4, 1, 1, 127, 0, 127));
    CHECK(IsColorAt(render(0x3000, 0x0040, 0x000F, 0x001F, 0x001F), 4, 1, 1, 255, 0, 0));
    // The ratio is chosen by the word's colour-calculation bits (11-13 in type 0): number 1 = 16,
    // so the sprite keeps 15/32 of itself and takes 17/32 of the back screen.
    CHECK(IsColorAt(render(0x0700, 0x0040, 0x1000, 0x081F, 0x001F), 4, 1, 1, 119, 0, 135));
}

// MSB-on only sets the destination's MSB. In a sprite type with a shadow bit (2-7) that makes the
// pixel a sprite shadow, which darkens itself: red drawn first, then MSB-on over it, is half red.
void TestMsbOnShadowsTheSpriteItself()
{
    State state = MakeSpriteVdp2State(0x0002);   // type 2, palette-only
    PutBE16(state.cram, 31 * 2, 0x001F);
    PutPolygon(state, 0x20, 0x001F, 0x0000, 0, 0, 3, 1);   // red
    PutPolygon(state, 0x40, 0x0000, 0x8000, 0, 0, 1, 1);   // MSB-on over the left two columns
    PutBE16(state.vdp1, 0x60, 0x8000);
    const std::vector<uint8_t> pixels =
        RenderWith(state, 4, 2, [](se_render_opts& o) { o.show_shadow_highlight = 1; });
    for (int y = 0; y < 2; ++y)
    {
        CHECK(IsColorAt(pixels, 4, 0, y, 127, 0, 0));
        CHECK(IsColorAt(pixels, 4, 1, y, 127, 0, 0));
        CHECK(IsColorAt(pixels, 4, 2, y, 255, 0, 0));
        CHECK(IsColorAt(pixels, 4, 3, y, 255, 0, 0));
    }
    // Shadows off: the MSB still changes nothing visible.
    const std::vector<uint8_t> off = RenderSized(state, 4, 2);
    CHECK(IsColorAt(off, 4, 0, 0, 255, 0, 0));
}

// MSB-on over empty framebuffer is a bare MSB: a transparent shadow. With TPSDSL set it darkens
// the layers whose SDCTL enable is set (here the back screen); without TPSDSL it is transparent.
void TestTransparentShadowNeedsTpsdAndTheLayerEnable()
{
    auto render = [](uint16_t sdctl)
    {
        State state = MakeSpriteVdp2State(0x0002);
        SetReg(state, 0x0E2, sdctl);
        PutPolygon(state, 0x20, 0x0000, 0x8000, 0, 0, 1, 1);   // bare MSB over the left columns
        PutBE16(state.vdp1, 0x40, 0x8000);
        return RenderWith(state, 4, 2, [](se_render_opts& o) { o.show_shadow_highlight = 1; });
    };
    const std::vector<uint8_t> shadowed = render(0x0120);   // TPSDSL + BKSDEN
    CHECK(IsColorAt(shadowed, 4, 0, 0, 0, 0, 127));
    CHECK(IsColorAt(shadowed, 4, 2, 0, 0, 0, 255));
    CHECK(IsColorAt(render(0x0020), 4, 0, 0, 0, 0, 255));   // TPSDSL clear: transparent
    CHECK(IsColorAt(render(0x0100), 4, 0, 0, 0, 0, 255));   // back screen not enabled
}

// A normal shadow is a pixel whose dot-colour bits are all ones but the lowest (0x7FE in type 0).
// It draws no colour; the layer under it is darkened.
void TestNormalShadowDarkensTheLayerUnderIt()
{
    State state = MakeSpriteVdp2State(0x0000);
    SetReg(state, 0x0E2, 0x0020);   // BKSDEN
    PutPolygon(state, 0x20, 0x07FE, 0x0000, 0, 0, 1, 1);
    PutBE16(state.vdp1, 0x40, 0x8000);
    const std::vector<uint8_t> pixels =
        RenderWith(state, 4, 2, [](se_render_opts& o) { o.show_shadow_highlight = 1; });
    CHECK(IsColorAt(pixels, 4, 0, 0, 0, 0, 127));
    CHECK(IsColorAt(pixels, 4, 3, 0, 0, 0, 255));
}

// The sprite window: with SPWINEN a type 2-7 sprite's shadow bit is a window bit instead. NBG3's
// window control selects it (SW enable, inside), so NBG3 is cut away exactly where the sprite layer
// carries the bit -- even though that sprite pixel itself is priority 0 and invisible.
void TestSpriteWindowCutsLayersWhereTheShadowBitIsSet()
{
    State state = MakeNbg3State();   // NBG3 white, priority 1
    SetReg(state, 0x0E0, 0x0012);   // type 2, SPWINEN
    SetReg(state, 0x0F0, 0x0100);   // PRISA: number 0 -> priority 0 (the window sprite is invisible)
    SetReg(state, 0x0AC, 0x0000);
    SetReg(state, 0x0AE, 0x0100);
    PutBE16(state.vdp2, 0x200, 0x7C00);   // blue back screen
    SetReg(state, 0x0D2, 0x2000);   // WCTLB high byte: NBG3 -- sprite window enabled, area bit clear
    ResizeVdp1(state, 0x400);
    PutPolygon(state, 0x20, 0x8001, 0x0000, 0, 0, 1, 1);   // sd = 1: window bit on the left columns
    PutBE16(state.vdp1, 0x40, 0x8000);
    const std::vector<uint8_t> pixels =
        RenderWith(state, 4, 2, [](se_render_opts& o) { o.show_window = 1; });
    for (int y = 0; y < 2; ++y)
    {
        CHECK(IsColorAt(pixels, 4, 0, y, 0, 0, 255));
        CHECK(IsColorAt(pixels, 4, 1, y, 0, 0, 255));
        CHECK(IsColorAt(pixels, 4, 2, y, 255, 255, 255));
        CHECK(IsColorAt(pixels, 4, 3, y, 255, 255, 255));
    }
}

// SPD is documented as "set to 1 for polygons". With it clear the hardware still decides
// transparency, by reading a texel at address -1 -- the last word of VDP1 VRAM -- so a polygon with
// SPD clear draws only if that word is not the colour mode's transparent (or end) code.
void TestPolygonWithSpdClearFollowsTheLastVramWord()
{
    auto render = [](uint16_t pmod, uint16_t lastWord)
    {
        State state = MakeBlueBackState(4, 2);
        state.vdp1.assign(0x80000, 0);
        se_test::WriteSystemClip(state, 4, 2);
        PutPolygon(state, 0x20, 0x801F, pmod, 0, 0, 3, 1);
        PutBE16(state.vdp1, 0x40, 0x8000);
        PutBE16(state.vdp1, 0x7FFFE, lastWord);
        return RenderSized(state, 4, 2);
    };
    CHECK(IsColorAt(render(0x0000, 0x0000), 4, 1, 1, 0, 0, 255));   // transparent code: not drawn
    CHECK(IsColorAt(render(0x0000, 0x0001), 4, 1, 1, 255, 0, 0));   // other data: drawn
    CHECK(IsColorAt(render(0x0040, 0x0000), 4, 1, 1, 255, 0, 0));   // SPD set: always drawn
    CHECK(IsColorAt(render(0x0040, 0x000F), 4, 1, 1, 0, 0, 255));   // end code (ECD clear): not drawn
    CHECK(IsColorAt(render(0x00C0, 0x000F), 4, 1, 1, 255, 0, 0));   // ECD and SPD set: drawn
    // A VRAM image too short to hold that word cannot say, so the polygon is drawn.
    State shortVram = MakeBlueBackState(4, 2);
    PutPolygon(shortVram, 0x20, 0x801F, 0x0000, 0, 0, 3, 1);
    PutBE16(shortVram.vdp1, 0x40, 0x8000);
    CHECK(IsColorAt(RenderSized(shortVram, 4, 2), 4, 1, 1, 255, 0, 0));
}

int main()
{
    TestSpriteBetweenTwoColorCalcLayers();
    TestHalfTransparentSpriteDoesNotBlendWithVdp2();
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
    TestLaterVdp1CommandReplacesEarlierRegardlessOfPriority();
    TestSpritePriorityZeroIsSuppressed();
    TestNormalSpriteCoversExactlyItsSize();
    TestHalfTransparencyIgnoresVdp2Background();
    TestHalfTransparencyAveragesFiveBitChannels();
    TestShadowHalvesTheFramebufferPixel();
    TestEndCodeTerminatesTheTextureRow();
    TestLoneEndCodeIsOnlyTransparent();
    TestSkippedStateCommandsChangeNothing();
    TestHiResUserClipCoversBothDoubledColumns();
    TestCoordinatesAreSignExtendedFromTheirHardwareWidth();
    TestRgbTexelsBelow0x4000AreTransparent();
    TestRgbEndCodesSpanTheWholeRange();
    TestEndCodesAreCountedAcrossTheLine();
    TestHalfTransparencyNeedsAnMsbSetDestination();
    TestEffectsChangeThePriorityTheWordSelects();
    TestPolygonPaletteCodesAreLookedUpInCram();
    TestThinSpriteCanBeHitTested();
    TestSpriteColorCalculation();
    TestMsbOnShadowsTheSpriteItself();
    TestTransparentShadowNeedsTpsdAndTheLayerEnable();
    TestNormalShadowDarkensTheLayerUnderIt();
    TestSpriteWindowCutsLayersWhereTheShadowBitIsSet();
    TestPolygonWithSpdClearFollowsTheLastVramWord();
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
