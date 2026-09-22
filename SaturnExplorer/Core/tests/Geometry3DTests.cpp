// 3D View orientation. The world-space and camera conventions the view is built on are
// documented on se_sprite_3d and se_camera3d in SeTypes.h; these tests pin them, because
// getting either wrong is invisible to every other test and glaring to the user.
//
// Two independent things can break. The layer stack can face the wrong way, which the
// head-on tests catch by requiring the 3D view to reproduce the 2D composite — the
// authoritative "what the Saturn drew" picture. Or the orbit can run backwards while the
// stacking stays right, which only TestOrbitSense catches.

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

#include "FakeVdpSource.h"
#include "saturnexplorer/SeHost.h"

namespace
{
using se_test::PutBE16;
using se_test::State;

int gFailures = 0;

void Check(bool condition, const char* expression, int line)
{
    if (condition) return;
    std::cerr << "CHECK failed at line " << line << ": " << expression << '\n';
    ++gFailures;
}

#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

const int kFrameWidth  = 160;
const int kFrameHeight = 120;
const size_t kVdp1Size = 0x20000;   // command table plus room for full-frame textures

// Saturn RGB555 is 1BBBBBGGGGGRRRRR — the MSB marks the pixel opaque.
const uint16_t kRed   = 0x801F;
const uint16_t kGreen = 0x83E0;
const uint16_t kBlue  = 0xFC00;
const uint16_t kWhite = 0xFFFF;

// One opaque RGB555 "normal sprite" of a single flat colour, at (x,y), w x h pixels.
void AddSprite(State& state, uint32_t cmd, uint32_t texture, uint16_t color,
               int x, int y, int w, int h)
{
    PutBE16(state.vdp1, cmd + 0x00, 0x0000);                        // CMDCTRL: normal sprite
    PutBE16(state.vdp1, cmd + 0x04, 0x0028 | 0x0040);               // CMDPMOD: RGB555, SPD
    PutBE16(state.vdp1, cmd + 0x08, static_cast<uint16_t>(texture / 8));
    PutBE16(state.vdp1, cmd + 0x0A, static_cast<uint16_t>(((w / 8) << 8) | h));
    PutBE16(state.vdp1, cmd + 0x0C, static_cast<uint16_t>(x));
    PutBE16(state.vdp1, cmd + 0x0E, static_cast<uint16_t>(y));
    for (int i = 0; i < w * h; ++i)
    {
        PutBE16(state.vdp1, texture + static_cast<uint32_t>(i) * 2, color);
    }
}

// A frame whose four sprites pin all three axes at once: a red backdrop, a blue square up
// and to the left, a green square down and to the right, and a white square drawn *over*
// the blue one. The white square is only visible when the layer stack faces the camera the
// right way round, and blue vs green only land correctly when X is not mirrored.
State MakeScene()
{
    State state(kVdp1Size);
    se_test::WriteSystemClip(state, kFrameWidth, kFrameHeight);

    const uint32_t backdrop = 0x1000;
    const uint32_t patch = backdrop + kFrameWidth * kFrameHeight * 2;
    AddSprite(state, 0x20, backdrop, kRed, 0, 0, kFrameWidth, kFrameHeight);
    AddSprite(state, 0x40, patch, kBlue, 16, 24, 32, 32);
    AddSprite(state, 0x60, patch + 32 * 32 * 2, kGreen, 112, 64, 32, 32);
    AddSprite(state, 0x80, patch + 32 * 32 * 4, kWhite, 24, 32, 16, 16);
    PutBE16(state.vdp1, 0xA0, 0x8000);   // draw end
    return state;
}

// A frame built to measure parallax rather than layout: a backdrop, four invisible
// backdrop-coloured squares stacked in the middle purely to push the layer counter up, and
// a white marker on top of them. The marker ends up centred on screen but well in front of
// Z == 0, so orbiting slides it across the frame while a Z == 0 sprite would barely move.
State MakeParallaxScene()
{
    State state(kVdp1Size);
    se_test::WriteSystemClip(state, kFrameWidth, kFrameHeight);

    const uint32_t backdrop = 0x1000;
    const uint32_t marker = backdrop + kFrameWidth * kFrameHeight * 2;
    const int x = kFrameWidth / 2 - 8;
    const int y = kFrameHeight / 2 - 8;
    AddSprite(state, 0x20, backdrop, kRed, 0, 0, kFrameWidth, kFrameHeight);
    for (uint32_t i = 0; i < 4; ++i)
    {
        AddSprite(state, 0x40 + i * 0x20, marker + i * 16 * 16 * 2, kRed, x, y, 16, 16);
    }
    AddSprite(state, 0xC0, marker + 4 * 16 * 16 * 2, kWhite, x, y, 16, 16);
    PutBE16(state.vdp1, 0xE0, 0x8000);
    return state;
}

se_context* Open(State& state)
{
    se_context* context = se_test::CreateContext(state);
    CHECK(context != nullptr);
    CHECK(se_begin_frame(context) == SE_OK);
    return context;
}

// fov == distance makes the Z == 0 plane project 1:1 with the composite; the layers in
// front of it are magnified by a few percent, which the centroid tolerances absorb.
se_camera3d Camera(float yaw, float pitch)
{
    se_camera3d camera = {};
    camera.yaw = yaw;
    camera.pitch = pitch;
    camera.distance = 300.0f;
    camera.fov = 300.0f;
    camera.viewport_width = kFrameWidth;
    camera.viewport_height = kFrameHeight;
    return camera;
}

struct Image
{
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Render with every layer on, through the ABI's two-call size convention. A null 'camera'
// asks for the 2D composite and a non-null one for the 3D view, so the two pictures under
// comparison come out of the same code here and can only differ inside the core.
Image Render(se_context* context, const se_camera3d* camera)
{
    se_render_opts options = {};
    for (int i = 0; i < SE_LAYER_COUNT; ++i) options.show_layer[i] = 1;
    options.show_vdp1_sprites = 1;

    se_image image = {};
    size_t needed = 0;
    auto render = [&]()
    {
        return camera ? se_render_3d(context, camera, &options, &image, &needed)
                      : se_render_frame(context, &options, &image, &needed);
    };
    CHECK(render() == SE_OK);
    Image out;
    out.pixels.resize(needed);
    image.pixels = out.pixels.data();
    image.capacity = out.pixels.size();
    CHECK(render() == SE_OK);
    out.width = image.width;
    out.height = image.height;
    return out;
}

// Where the pixels of one flat colour sit, as a count and a centre of mass.
struct Blob
{
    size_t count = 0;
    float x = 0.0f;
    float y = 0.0f;
};

Blob Find(const Image& image, uint8_t r, uint8_t g, uint8_t b)
{
    Blob blob;
    double sx = 0.0, sy = 0.0;
    for (uint32_t y = 0; y < image.height; ++y)
    {
        for (uint32_t x = 0; x < image.width; ++x)
        {
            const size_t o = (static_cast<size_t>(y) * image.width + x) * 4;
            if (image.pixels[o] == r && image.pixels[o + 1] == g && image.pixels[o + 2] == b)
            {
                ++blob.count;
                sx += x;
                sy += y;
            }
        }
    }
    if (blob.count != 0)
    {
        blob.x = static_cast<float>(sx / blob.count);
        blob.y = static_cast<float>(sy / blob.count);
    }
    return blob;
}

bool Near(float a, float b, float tolerance) { return std::fabs(a - b) <= tolerance; }

// The world the core hands out, checked at the seam rather than through a render.
void TestWorldSpaceIsRightHanded()
{
    State state = MakeScene();
    se_context* context = Open(state);

    CHECK(se_sprite_count(context) == 4);
    se_sprite_3d backdrop = {}, blue = {}, green = {}, white = {};
    CHECK(se_get_sprite_3d(context, 0, &backdrop) == SE_OK);
    CHECK(se_get_sprite_3d(context, 1, &blue) == SE_OK);
    CHECK(se_get_sprite_3d(context, 2, &green) == SE_OK);
    CHECK(se_get_sprite_3d(context, 3, &white) == SE_OK);

    CHECK(blue.corners[0].x < green.corners[0].x);      // X runs right, as on screen
    CHECK(blue.corners[0].y > green.corners[0].y);      // Y runs up, opposite to screen Y
    CHECK(backdrop.corners[0].z < blue.corners[0].z);   // drawn later => nearer the viewer
    CHECK(blue.corners[0].z < white.corners[0].z);

    se_destroy(context);
}

// Seen head-on the 3D view must be the composite: same sprites, same side of the frame,
// same one on top. A handedness flip mirrors X so blue and green swap sides; an inverted
// depth axis hides the white square behind the backdrop.
void TestFrontViewMatchesComposite()
{
    State state = MakeScene();
    se_context* context = Open(state);

    const Image composite = Render(context, nullptr);
    CHECK(composite.width == static_cast<uint32_t>(kFrameWidth));
    CHECK(composite.height == static_cast<uint32_t>(kFrameHeight));
    const se_camera3d front = Camera(0.0f, 0.0f);
    const Image view = Render(context, &front);
    CHECK(view.width == composite.width);
    CHECK(view.height == composite.height);

    const uint8_t colors[3][3] = { { 0, 0, 255 }, { 0, 255, 0 }, { 255, 255, 255 } };
    for (int i = 0; i < 3; ++i)
    {
        const Blob want = Find(composite, colors[i][0], colors[i][1], colors[i][2]);
        const Blob got = Find(view, colors[i][0], colors[i][1], colors[i][2]);
        CHECK(want.count > 100);   // the composite is the ground truth
        CHECK(got.count > 100);    // ... and the 3D view shows the same sprite
        CHECK(Near(got.x, want.x, 4.0f));
        CHECK(Near(got.y, want.y, 4.0f));
    }

    se_destroy(context);
}

// Orbiting must not lose the ordering: from an oblique angle the topmost sprite is still
// in front and the sprites still sit on their own side of the frame. This says nothing
// about which way the scene spins — TestOrbitSense is what pins that.
void TestObliqueViewKeepsOrder()
{
    State state = MakeScene();
    se_context* context = Open(state);

    const se_camera3d oblique = Camera(0.3f, 0.2f);
    const Image view = Render(context, &oblique);
    const Blob white = Find(view, 255, 255, 255);
    const Blob blue = Find(view, 0, 0, 255);
    const Blob green = Find(view, 0, 255, 0);
    CHECK(white.count > 100);
    CHECK(blue.count > 100);
    CHECK(green.count > 100);
    CHECK(blue.x < green.x);   // blue stays left of green
    CHECK(blue.y < green.y);   // and above it

    se_destroy(context);
}

// Which way the scene spins. se_camera3d's yaw/pitch have a documented sense and the 3D
// View panel wires drag-right to yaw+ and drag-down to pitch+ on the strength of it, so a
// sign flip here is a user-visible regression even when the depth order is right. It is
// also invisible to the tests above: at their angles a layer shifts a few pixels, far less
// than the sprites are apart. Hence a scene whose only moving part is a marker sitting well
// in front of the backdrop, and angles big enough to be unambiguous.
void TestOrbitSense()
{
    State state = MakeParallaxScene();
    se_context* context = Open(state);

    const se_camera3d rest = Camera(0.0f, 0.0f);
    const se_camera3d yaw = Camera(0.8f, 0.0f);
    const se_camera3d pitch = Camera(0.0f, 0.8f);
    const Blob atRest = Find(Render(context, &rest), 255, 255, 255);
    const Blob yawed = Find(Render(context, &yaw), 255, 255, 255);
    const Blob pitched = Find(Render(context, &pitch), 255, 255, 255);
    CHECK(atRest.count > 100);
    CHECK(yawed.count > 100);
    CHECK(pitched.count > 100);

    // At rest the marker is centred over the backdrop it floats above.
    CHECK(Near(atRest.x, kFrameWidth * 0.5f, 2.0f));
    CHECK(Near(atRest.y, kFrameHeight * 0.5f, 2.0f));

    // yaw > 0 swings the +X side of the scene away, so what stands in front of the backdrop
    // slides right; pitch > 0 tips the +Y side towards the viewer, so it slides down.
    CHECK(yawed.x > atRest.x + 10.0f);
    CHECK(Near(yawed.y, atRest.y, 2.0f));
    CHECK(pitched.y > atRest.y + 10.0f);
    CHECK(Near(pitched.x, atRest.x, 2.0f));

    se_destroy(context);
}

}  // namespace

int main()
{
    TestWorldSpaceIsRightHanded();
    TestFrontViewMatchesComposite();
    TestObliqueViewKeepsOrder();
    TestOrbitSense();
    if (gFailures != 0)
    {
        std::cerr << gFailures << " check(s) failed\n";
        return 1;
    }
    std::cout << "Geometry3DTests: all checks passed\n";
    return 0;
}
