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

std::vector<uint8_t> Render(State& state, bool showWindow)
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
    options.show_layer[SE_LAYER_NBG3] = 1;
    options.show_window = showWindow ? 1 : 0;
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

bool IsWhite(const std::vector<uint8_t>& pixels, int x, int y)
{
    const size_t offset = static_cast<size_t>(y * 4 + x) * 4;
    return pixels[offset] == 255 && pixels[offset + 1] == 255 &&
           pixels[offset + 2] == 255 && pixels[offset + 3] == 255;
}

bool IsRed(const std::vector<uint8_t>& pixels, int x, int y)
{
    const size_t offset = static_cast<size_t>(y * 4 + x) * 4;
    return pixels[offset] == 255 && pixels[offset + 1] == 0 &&
           pixels[offset + 2] == 0 && pixels[offset + 3] == 255;
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
}  // namespace

int main()
{
    TestRectangularWindow();
    TestLineWindow();
    TestVerticalPlaneSize();
    TestTransparentPixelDisable();
    if (gFailures != 0)
    {
        std::cerr << gFailures << " VDP2 compositor check(s) failed\n";
        return 1;
    }
    std::cout << "VDP2 compositor tests passed\n";
    return 0;
}
