// Context — the C++ core object behind the opaque se_context* handle. The C ABI
// shim in host_abi.cpp is a thin translation layer over this class; all real
// logic lives on the C++ side (ARCHITECTURE.md §3, the hybrid boundary).
#pragma once

#include <cstring>
#include <vector>

#include "saturnexplorer/se_host.h"
#include "hardware_snapshot.h"
#include "vdp1_parser.h"
#include "geometry_builder.h"
#include "vdp1_rasterizer.h"

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

    // Render the composited frame. Two-call convention: with out->pixels == NULL
    // reports the required byte size in *needed; otherwise fills the buffer.
    se_result RenderFrame(const se_render_opts& opts, se_image* out, size_t* needed)
    {
        const uint32_t w = static_cast<uint32_t>(mScene.screenWidth);
        const uint32_t h = static_cast<uint32_t>(mScene.screenHeight);
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
        Vdp1Rasterizer::Render(mScene, mSnapshot.Vdp1Vram(), mSnapshot.Cram(),
                               mSnapshot.CramMode(), opts, mRenderBuffer);
        std::memcpy(out->pixels, mRenderBuffer.data(),
                    mRenderBuffer.size() < required ? mRenderBuffer.size() : required);
        return SE_OK;
    }

    // Topmost sprite (last drawn) containing the screen point, if any.
    se_result HitTest(int x, int y, size_t* outCommandIndex) const
    {
        for (size_t i = mScene.sprites.size(); i-- > 0; )
        {
            if (PointInSprite(mScene.sprites[i], x + 0.5f, y + 0.5f))
            {
                *outCommandIndex = mScene.sprites[i].command_index;
                return SE_OK;
            }
        }
        return SE_ERR_NO_DATA;
    }

    size_t VramRegionCount() const { return 0; }

    bool HasSnapshot() const { return mSnapshot.Valid(); }

    const se_data_source& DataSource() const { return mDs; }
    const se_config& Config() const { return mCfg; }

private:
    se_data_source          mDs;
    se_config               mCfg;
    HardwareSnapshot        mSnapshot;
    std::vector<se_command> mCommands;
    Vdp1Scene               mScene;
    std::vector<uint8_t>    mRenderBuffer;
};

}  // namespace se
