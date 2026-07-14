#include "hardware_snapshot.h"

namespace se {

namespace {

// Read `size` bytes from a Seam A bulk reader into `dst`, in chunks, tolerating
// short reads. Returns total bytes copied.
size_t ReadAll(size_t (*reader)(void*, uint32_t, void*, size_t), void* user,
               uint32_t base, uint8_t* dst, size_t size) {
    if (!reader) return 0;
    size_t done = 0;
    while (done < size) {
        size_t got = reader(user, base + static_cast<uint32_t>(done),
                            dst + done, size - done);
        if (got == 0) break;  // driver reported no progress; stop.
        done += got;
    }
    return done;
}

}  // namespace

bool HardwareSnapshot::Capture(const se_data_source& ds) {
    valid_ = false;
    vdp1_vram_.clear();
    vdp2_vram_.clear();
    cram_.clear();

    if (ds.capabilities & SE_CAP_VDP1_VRAM) {
        vdp1_vram_.resize(kVdp1VramSize);
        size_t got = ReadAll(ds.read_vdp1_vram, ds.user, 0,
                             vdp1_vram_.data(), vdp1_vram_.size());
        vdp1_vram_.resize(got);
    }
    if (ds.capabilities & SE_CAP_VDP2_VRAM) {
        vdp2_vram_.resize(kVdp2VramSize);
        size_t got = ReadAll(ds.read_vdp2_vram, ds.user, 0,
                             vdp2_vram_.data(), vdp2_vram_.size());
        vdp2_vram_.resize(got);
    }
    if (ds.capabilities & SE_CAP_CRAM) {
        cram_.resize(kCramSize);
        size_t got = ReadAll(ds.read_cram, ds.user, 0, cram_.data(), cram_.size());
        cram_.resize(got);
    }

    valid_ = !vdp1_vram_.empty();
    return valid_;
}

}  // namespace se
