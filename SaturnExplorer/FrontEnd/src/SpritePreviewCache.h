#pragma once

#include <unordered_map>
#include <vector>

#include "Platform/IPlatform.h"
#include "saturnexplorer/SeHost.h"

namespace sfe
{
// Only visible rows ask for previews. Content keys survive identical live captures;
// per-snapshot row keys avoid even hashing texture data again on a static UI frame.
class SpritePreviewCache
{
public:
    struct Preview { TextureHandle texture = 0; int width = 0, height = 0; uint64_t used = 0; };

    void BeginFrame(IPlatform& platform, uint64_t frame)
    {
        mFrame = frame;
        // Never destroy textures already referenced by this frame's ImGui draw list.
        while (mPreviews.size() > 256)
        {
            auto oldest = mPreviews.end();
            for (auto it = mPreviews.begin(); it != mPreviews.end(); ++it)
                if (it->second.used != frame &&
                    (oldest == mPreviews.end() || it->second.used < oldest->second.used)) oldest = it;
            if (oldest == mPreviews.end()) break;
            platform.DestroyTexture(oldest->second.texture);
            mPreviews.erase(oldest);
        }
    }

    const Preview* Get(se_context* context, size_t command, IPlatform& platform)
    {
        const uint64_t serial = se_derive_serial(context);
        if (serial != mSerial) { mSerial = serial; mRowKeys.clear(); }
        auto key = mRowKeys.find(command);
        if (key == mRowKeys.end()) key = mRowKeys.emplace(command, se_sprite_preview_key(context, command)).first;
        if (!key->second) return nullptr;
        auto found = mPreviews.find(key->second);
        if (found == mPreviews.end())
        {
            se_image image {};
            size_t needed = 0;
            if (se_render_sprite_preview(context, command, 64, 64, &image, &needed) != SE_OK) return nullptr;
            std::vector<uint8_t> pixels(needed);
            image.pixels = pixels.data(); image.capacity = pixels.size();
            if (se_render_sprite_preview(context, command, 64, 64, &image, &needed) != SE_OK) return nullptr;
            Preview preview;
            preview.width = static_cast<int>(image.width); preview.height = static_cast<int>(image.height);
            preview.texture = platform.CreateTexture(preview.width, preview.height);
            if (!preview.texture) return nullptr;
            platform.UpdateTexture(preview.texture, pixels.data(), preview.width, preview.height);
            found = mPreviews.emplace(key->second, preview).first;
        }
        found->second.used = mFrame;
        return &found->second;
    }

private:
    uint64_t mSerial = 0, mFrame = 0;
    std::unordered_map<size_t, uint64_t> mRowKeys;
    std::unordered_map<uint64_t, Preview> mPreviews;
};
} // namespace sfe
