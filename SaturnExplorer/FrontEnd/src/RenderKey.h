// RenderKey — what a rendered image was drawn from, so a caller can tell whether drawing it
// again could produce anything different.
//
// The compositor is a CPU pass over the whole frame, and the viewers show its output through
// a texture upload. A static dump, a paused game or a halted breakpoint presents the same
// snapshot every UI frame, so re-running both every frame is pure repetition. An image is a
// function of the context's derived state (se_derive_serial moves on every capture and every
// in-place edit; it is unique across contexts, so a context created at a freed one's address
// cannot match) and of the render options; a key holding those two things is enough to know.
#pragma once

#include <cstdint>

#include "saturnexplorer/SaturnExplorer.h"

namespace sfe
{

class RenderKey
{
public:
    // Remember what 'ctx' and 'opts' look like now.
    void Set(se_context* ctx, const se_render_opts& opts)
    {
        mCtx = ctx;
        mSerial = ctx ? se_derive_serial(ctx) : 0;
        mOpts = opts;
        mValid = ctx != nullptr;
    }

    // True when an image drawn at Set() time is still what a render would produce now.
    bool Matches(se_context* ctx, const se_render_opts& opts) const
    {
        return mValid && ctx == mCtx && se_derive_serial(ctx) == mSerial && Same(mOpts, opts);
    }

    // Forget it: the pixels it described are gone, or were never drawn.
    void Invalidate() { mValid = false; }
    bool Valid() const { return mValid; }

private:
    // Field by field: the struct has padding, whose bytes a memcmp would compare.
    static bool Same(const se_render_opts& a, const se_render_opts& b)
    {
        for (int i = 0; i < SE_LAYER_COUNT; ++i)
            if (a.show_layer[i] != b.show_layer[i]) return false;
        return a.show_vdp1_sprites == b.show_vdp1_sprites &&
               a.show_wireframe == b.show_wireframe &&
               a.show_bounding_boxes == b.show_bounding_boxes &&
               a.show_object_numbers == b.show_object_numbers &&
               a.show_window == b.show_window &&
               a.show_color_calculation == b.show_color_calculation &&
               a.show_shadow_highlight == b.show_shadow_highlight &&
               a.highlight_command == b.highlight_command &&
               a.transparent_background == b.transparent_background &&
               a.show_tile_grid == b.show_tile_grid;
    }

    se_context*    mCtx = nullptr;
    uint64_t       mSerial = 0;
    se_render_opts mOpts = {};
    bool           mValid = false;
};

}  // namespace sfe
