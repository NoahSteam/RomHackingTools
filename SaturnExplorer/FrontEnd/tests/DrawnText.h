// DrawnText — read back the text a window DREW, from ImGui's draw lists, without a renderer.
//
// A panel test that asks the panel what it says ("StaleTooltip()", "LineTooltip()") checks the panel's own
// account of what it drew. This reads the glyphs: ImGui turns every character of a string into one textured
// quad in the window's draw list, whose texture coordinates are the glyph's cell in the font atlas. The atlas
// knows the code point of each cell, so the quads of a frame can be turned back into the strings, colours and
// positions that were drawn (a space has no quad; it is a gap). What a widget drew, and how bright, is then a
// fact of the frame, not of the panel's state functions.
//
// Limits: a glyph outside the clip rectangle is not emitted (text wider than its window is cut), and the atlas
// is the one the frame was drawn with (read right after the frame, before the next NewFrame). Both are why the
// callers compare with 'a prefix of' where a line can be cut, and why a glyph this cannot name is reported as
// '\x01' and counted in Run::unknown.
#pragma once

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

namespace sfe
{
namespace drawn
{

struct Run
{
    std::string text;       // UTF-8, spaces put in where the gap between glyphs is wide enough for one
    ImU32 col = 0;
    float x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    int glyphs = 0;
    int unknown = 0;        // glyphs whose cell the atlas does not know
    float Alpha() const { return static_cast<float>((col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f; }
    ImVec4 Colour() const { return ImGui::ColorConvertU32ToFloat4(col); }
};

inline std::string Utf8(unsigned cp)
{
    std::string s;
    if (cp < 0x80) s += static_cast<char>(cp);
    else if (cp < 0x800) { s += static_cast<char>(0xC0 | (cp >> 6)); s += static_cast<char>(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000)
    {
        s += static_cast<char>(0xE0 | (cp >> 12));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        s += static_cast<char>(0xF0 | (cp >> 18));
        s += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        s += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        s += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return s;
}

// The atlas cell -> code point map, from every baked size the atlas holds.
class Atlas
{
public:
    Atlas()
    {
        ImFontAtlas* atlas = ImGui::GetIO().Fonts;
        if (!atlas || !atlas->Builder) return;
        ImFontAtlasBuilder* b = atlas->Builder;
        for (int i = 0; i < b->BakedPool.Size; ++i)
        {
            const ImFontBaked* baked = &b->BakedPool[i];
            for (int g = 0; g < baked->Glyphs.Size; ++g)
            {
                const ImFontGlyph& gl = baked->Glyphs[g];
                if (!gl.Visible) continue;
                mCell[Key(gl.U0, gl.V0, gl.U1, gl.V1)] = gl.Codepoint;
            }
        }
    }
    // 0: unknown
    unsigned Find(float u0, float v0, float u1, float v1) const
    {
        auto it = mCell.find(Key(u0, v0, u1, v1));
        return it == mCell.end() ? 0u : it->second;
    }

private:
    typedef std::vector<uint32_t> KeyT;
    static KeyT Key(float u0, float v0, float u1, float v1)
    {
        KeyT k(4);
        std::memcpy(&k[0], &u0, 4);
        std::memcpy(&k[1], &v0, 4);
        std::memcpy(&k[2], &u1, 4);
        std::memcpy(&k[3], &v1, 4);
        return k;
    }
    std::map<KeyT, unsigned> mCell;
};

struct Glyph
{
    float x0, y0, x1, y1, u0, v0, u1, v1;
    ImU32 col;
};

// Every textured (non-solid) quad of a draw list, in the order it was drawn.
inline std::vector<Glyph> Glyphs(const ImDrawList* dl)
{
    std::vector<Glyph> out;
    if (!dl) return out;
    const ImVec2 white = dl->_Data ? dl->_Data->TexUvWhitePixel : ImVec2(0, 0);
    const ImDrawIdx* idx = dl->IdxBuffer.Data;
    const int n = dl->IdxBuffer.Size;
    for (int i = 0; i + 6 <= n;)
    {
        const bool quad = idx[i + 1] == idx[i] + 1 && idx[i + 2] == idx[i] + 2 && idx[i + 3] == idx[i] &&
                          idx[i + 4] == idx[i] + 2 && idx[i + 5] == idx[i] + 3;
        if (!quad)
        {
            i += 3;   // a triangle of a path or a line: not text
            continue;
        }
        const ImDrawVert& a = dl->VtxBuffer[idx[i]];
        const ImDrawVert& c = dl->VtxBuffer[idx[i + 2]];
        i += 6;
        if (a.uv.x == c.uv.x || a.uv.y == c.uv.y) continue;   // a solid rectangle samples one pixel
        if (a.uv.x == white.x && a.uv.y == white.y) continue;
        Glyph g;
        g.x0 = a.pos.x;
        g.y0 = a.pos.y;
        g.x1 = c.pos.x;
        g.y1 = c.pos.y;
        g.u0 = a.uv.x;
        g.v0 = a.uv.y;
        g.u1 = c.uv.x;
        g.v1 = c.uv.y;
        g.col = a.col;
        out.push_back(g);
    }
    return out;
}

// The strings a draw list drew. A run is the glyphs of one call: they follow each other left to right on one
// line in one colour. A glyph's quad is a pixel or two wider than the glyph (the atlas is oversampled), so neighbours
// overlap by up to that much, a space leaves a gap of about two pixels, and the gap between two widgets on a line is the
// style's item spacing (8) less the overhang: 'splitGap' lies between the two. A step back by more than a few pixels is
// another string.
inline std::vector<Run> Runs(const ImDrawList* dl, float splitGap = 5.0f, float spaceGap = 1.4f)
{
    std::vector<Run> runs;
    const Atlas atlas;
    const std::vector<Glyph> gl = Glyphs(dl);
    Run cur;
    bool open = false;
    float prevRight = 0, prevMidY = 0;
    for (const Glyph& g : gl)
    {
        const float midY = (g.y0 + g.y1) * 0.5f;
        const unsigned cp = atlas.Find(g.u0, g.v0, g.u1, g.v1);
        bool split = !open;
        if (open)
        {
            const float gap = g.x0 - prevRight;
            split = g.col != cur.col || gap > splitGap || gap < -8.0f || (midY - prevMidY > 9.0f) || (prevMidY - midY > 9.0f);
        }
        if (split)
        {
            if (open) runs.push_back(cur);
            cur = Run();
            cur.col = g.col;
            cur.x0 = g.x0;
            cur.y0 = g.y0;
            cur.y1 = g.y1;
            open = true;
        }
        else if (g.x0 - prevRight > spaceGap)
            cur.text += ' ';
        if (cp) cur.text += Utf8(cp);
        else
        {
            cur.text += '\x01';
            ++cur.unknown;
        }
        ++cur.glyphs;
        cur.x1 = g.x1;
        cur.y0 = cur.y0 < g.y0 ? cur.y0 : g.y0;
        cur.y1 = cur.y1 > g.y1 ? cur.y1 : g.y1;
        prevRight = g.x1;
        prevMidY = midY;
    }
    if (open) runs.push_back(cur);
    return runs;
}

// Runs of a window drawn directly in it (not of its child windows).
inline std::vector<Run> WindowRuns(const char* name)
{
    ImGuiWindow* w = ImGui::FindWindowByName(name);
    return w ? Runs(w->DrawList) : std::vector<Run>();
}

// The first child window of the window (the code list), by walking the window list.
inline ImGuiWindow* ChildOf(const char* name)
{
    ImGuiWindow* parent = ImGui::FindWindowByName(name);
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (ImGuiWindow* w : g.Windows)
        if (w->ParentWindow == parent && w->Active && (w->Flags & ImGuiWindowFlags_ChildWindow)) return w;
    return nullptr;
}

inline std::string Squash(const std::string& s)
{
    std::string out;
    for (char c : s)
        if (c != ' ' && c != '\n' && c != '\t' && c != '\x01') out += c;
    return out;
}

// All the tooltip windows that are up, drawn text joined top to bottom.
inline std::string TooltipText(bool* found = nullptr)
{
    std::string text;
    bool any = false;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (ImGuiWindow* w : g.Windows)
        if ((w->Flags & ImGuiWindowFlags_Tooltip) && w->Active)
        {
            any = true;
            std::vector<Run> rs = Runs(w->DrawList);
            for (const Run& r : rs) text += r.text + "\n";
        }
    if (found) *found = any;
    return text;
}

}  // namespace drawn
}  // namespace sfe
