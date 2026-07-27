// se-render — headless golden-frame harness (roadmap E1).
//
// Loads a Saturn Explorer memory dump (.sedump / "SEMDUMP1", the format the app's
// File > Save Memory Dump writes), renders the VDP2+VDP1 composite through the core, and
// either writes the frame to a PPM or diffs it against a reference PPM — reporting the
// exact per-pixel mismatch count and (optionally) writing a diff image. This is what lets
// rendering changes be validated against a captured reference instead of by eye.
//
//   se-render dump.sedump --out frame.ppm
//   se-render dump.sedump --reference golden.ppm [--diff diff.ppm] [--tolerance N]
//
// Exit status is non-zero when a reference is given and the mismatch count exceeds the
// tolerance, so it drops straight into a CI check. PPM (binary P6) is used so the tool
// needs no image-library dependency; the reference frame is produced elsewhere (the
// emulator's displayed framebuffer — see roadmap E1/E3).

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

#include "saturnexplorer/SeHost.h"

namespace
{
uint32_t ReadLE32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24); }

// One named region carved out of the dump's section table.
struct Section { std::string name; std::vector<uint8_t> bytes; };

struct Dump
{
    std::vector<Section> sections;
    const std::vector<uint8_t>* find(const char* name) const
    {
        for (const Section& s : sections)
            if (s.name == name) return &s.bytes;
        return nullptr;
    }
};

bool LoadDump(const char* path, Dump& out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "se-render: cannot open %s\n", path); return false; }
    std::vector<uint8_t> f((std::istreambuf_iterator<char>(in)), {});
    if (f.size() < 16 || std::memcmp(f.data(), "SEMDUMP1", 8) != 0)
    {
        std::fprintf(stderr, "se-render: %s is not a SEMDUMP1 file\n", path);
        return false;
    }
    const uint32_t count = ReadLE32(&f[12]);
    size_t off = 16;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (off + 32 > f.size()) return false;
        char name[17] = {};
        std::memcpy(name, &f[off], 16);
        const uint32_t size = ReadLE32(&f[off + 20]);
        const uint32_t foff = ReadLE32(&f[off + 24]);
        if (foff + size > f.size()) return false;
        out.sections.push_back({ name, std::vector<uint8_t>(f.begin() + foff, f.begin() + foff + size) });
        off += 32;
    }
    return true;
}

// Read a big-endian 16-bit register from a section's hardware-offset image.
uint16_t Reg(const std::vector<uint8_t>* v, uint32_t hw)
{
    if (!v || hw + 1 >= v->size()) return 0;
    return static_cast<uint16_t>(((*v)[hw] << 8) | (*v)[hw + 1]);
}

size_t ReadRegion(const std::vector<uint8_t>* v, uint32_t off, void* dst, size_t n)
{
    if (!v || off >= v->size()) return 0;
    const size_t c = std::min(n, v->size() - off);
    std::memcpy(dst, v->data() + off, c);
    return c;
}

// --- PPM (binary P6) I/O ---
bool WritePpm(const char* path, const uint8_t* rgba, uint32_t w, uint32_t h)
{
    std::ofstream o(path, std::ios::binary);
    if (!o) { std::fprintf(stderr, "se-render: cannot write %s\n", path); return false; }
    o << "P6\n" << w << ' ' << h << "\n255\n";
    std::vector<uint8_t> rgb(static_cast<size_t>(w) * h * 3);
    for (size_t i = 0, n = static_cast<size_t>(w) * h; i < n; ++i)
    {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }
    o.write(reinterpret_cast<const char*>(rgb.data()), rgb.size());
    return static_cast<bool>(o);
}

bool ReadPpm(const char* path, std::vector<uint8_t>& rgb, uint32_t& w, uint32_t& h)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) { std::fprintf(stderr, "se-render: cannot open reference %s\n", path); return false; }
    std::string magic; int maxv = 0;
    in >> magic >> w >> h >> maxv;
    if (magic != "P6" || maxv != 255) { std::fprintf(stderr, "se-render: %s is not a P6/255 PPM\n", path); return false; }
    in.get();  // consume the single whitespace after the header
    rgb.resize(static_cast<size_t>(w) * h * 3);
    in.read(reinterpret_cast<char*>(rgb.data()), rgb.size());
    return static_cast<bool>(in);
}
}  // namespace

int main(int argc, char** argv)
{
    const char* input = nullptr;
    const char* outPath = nullptr;
    const char* refPath = nullptr;
    const char* diffPath = nullptr;
    int tolerance = 0;   // allowed differing pixels before failing
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : nullptr; };
        if (a == "--out") outPath = next();
        else if (a == "--reference") refPath = next();
        else if (a == "--diff") diffPath = next();
        else if (a == "--tolerance") { const char* t = next(); tolerance = t ? std::atoi(t) : 0; }
        else if (!input) input = argv[i];
        else { std::fprintf(stderr, "se-render: unexpected argument %s\n", a.c_str()); return 2; }
    }
    if (!input)
    {
        std::fprintf(stderr,
            "usage: se-render <dump.sedump> [--out frame.ppm]\n"
            "                 [--reference golden.ppm [--diff diff.ppm] [--tolerance N]]\n");
        return 2;
    }

    Dump dump;
    if (!LoadDump(input, dump)) return 2;

    se_data_source src {};
    src.abi_version = SE_ABI_VERSION;
    src.user = &dump;
    uint32_t caps = 0;
    auto bind = [&](const char* name, uint32_t cap) -> const std::vector<uint8_t>* {
        const std::vector<uint8_t>* v = dump.find(name);
        if (v) caps |= cap;
        return v;
    };
    static const std::vector<uint8_t>* gVdp1  = bind("VDP1_VRAM", SE_CAP_VDP1_VRAM);
    static const std::vector<uint8_t>* gVdp2  = bind("VDP2_VRAM", SE_CAP_VDP2_VRAM);
    static const std::vector<uint8_t>* gCram  = bind("CRAM",      SE_CAP_CRAM);
    static const std::vector<uint8_t>* gV1r   = bind("VDP1_REGS", SE_CAP_VDP1_REGS);
    static const std::vector<uint8_t>* gV2r   = bind("VDP2_REGS", SE_CAP_VDP2_REGS);
    static const std::vector<uint8_t>* gFb    = bind("VDP1_FB",   SE_CAP_VDP1_FB);
    src.capabilities = caps;
    src.read_vdp1_vram = [](void*, uint32_t o, void* d, size_t n) { return ReadRegion(gVdp1, o, d, n); };
    src.read_vdp2_vram = [](void*, uint32_t o, void* d, size_t n) { return ReadRegion(gVdp2, o, d, n); };
    src.read_cram      = [](void*, uint32_t o, void* d, size_t n) { return ReadRegion(gCram, o, d, n); };
    src.read_vdp1_fb   = [](void*, uint32_t o, void* d, size_t n) { return ReadRegion(gFb,   o, d, n); };
    src.read_vdp1_reg  = [](void*, uint32_t hw) { return Reg(gV1r, hw); };
    src.read_vdp2_reg  = [](void*, uint32_t hw) { return Reg(gV2r, hw); };

    se_config cfg {};
    cfg.abi_version = SE_ABI_VERSION;
    se_context* ctx = se_create(&src, &cfg);
    if (!ctx) { std::fprintf(stderr, "se-render: se_create failed\n"); return 2; }
    se_begin_frame(ctx);

    se_render_opts opts {};
    for (int i = 0; i < SE_LAYER_COUNT; ++i) opts.show_layer[i] = 1;
    opts.show_vdp1_sprites = 1;
    se_image img {};
    size_t need = 0;
    se_render_frame(ctx, &opts, &img, &need);
    std::vector<uint8_t> px(need);
    img.pixels = px.data();
    img.capacity = px.size();
    if (se_render_frame(ctx, &opts, &img, &need) != SE_OK)
    {
        std::fprintf(stderr, "se-render: render failed\n");
        se_destroy(ctx);
        return 2;
    }
    std::printf("rendered %u x %u\n", img.width, img.height);
    se_destroy(ctx);

    if (outPath && !WritePpm(outPath, px.data(), img.width, img.height)) return 2;

    int rc = 0;
    if (refPath)
    {
        std::vector<uint8_t> ref; uint32_t rw = 0, rh = 0;
        if (!ReadPpm(refPath, ref, rw, rh)) return 2;
        if (rw != img.width || rh != img.height)
        {
            std::printf("MISMATCH: dimensions render=%ux%u reference=%ux%u\n",
                        img.width, img.height, rw, rh);
            return 1;
        }
        std::vector<uint8_t> diff(static_cast<size_t>(rw) * rh * 4, 0);
        size_t mismatches = 0;
        int maxDelta = 0;
        for (size_t i = 0, n = static_cast<size_t>(rw) * rh; i < n; ++i)
        {
            int d = 0;
            for (int c = 0; c < 3; ++c)
                d = std::max(d, std::abs(int(px[i * 4 + c]) - int(ref[i * 3 + c])));
            maxDelta = std::max(maxDelta, d);
            if (d != 0)
            {
                ++mismatches;
                diff[i * 4 + 0] = 255; diff[i * 4 + 3] = 255;   // flag differing pixels red
            }
        }
        std::printf("diff: %zu / %u mismatched pixels (max channel delta %d)\n",
                    mismatches, rw * rh, maxDelta);
        if (diffPath) WritePpm(diffPath, diff.data(), rw, rh);
        rc = (mismatches > static_cast<size_t>(tolerance)) ? 1 : 0;
    }
    return rc;
}
