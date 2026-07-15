#include "hardware_snapshot.h"

namespace se
{

namespace
{

// Read `size` bytes from a Seam A bulk reader into `dst`, in chunks, tolerating
// short reads. Returns total bytes copied.
size_t ReadAll(size_t (*reader)(void*, uint32_t, void*, size_t), void* user,
               uint32_t base, uint8_t* dst, size_t size)
{
    if (!reader)
    {
        return 0;
    }

    size_t done = 0;
    while (done < size)
    {
        size_t got = reader(user, base + static_cast<uint32_t>(done),
                            dst + done, size - done);
        if (got == 0)
        {
            break;  // driver reported no progress; stop.
        }
        done += got;
    }
    return done;
}

}  // namespace

bool HardwareSnapshot::Capture(const se_data_source& dataSource)
{
    mbValid = false;
    mVdp1Vram.clear();
    mVdp2Vram.clear();
    mCram.clear();

    if (dataSource.capabilities & SE_CAP_VDP1_VRAM)
    {
        mVdp1Vram.resize(kVdp1VramSize);
        size_t got = ReadAll(dataSource.read_vdp1_vram, dataSource.user, 0,
                             mVdp1Vram.data(), mVdp1Vram.size());
        mVdp1Vram.resize(got);
    }
    if (dataSource.capabilities & SE_CAP_VDP2_VRAM)
    {
        mVdp2Vram.resize(kVdp2VramSize);
        size_t got = ReadAll(dataSource.read_vdp2_vram, dataSource.user, 0,
                             mVdp2Vram.data(), mVdp2Vram.size());
        mVdp2Vram.resize(got);
    }
    if (dataSource.capabilities & SE_CAP_CRAM)
    {
        mCram.resize(kCramSize);
        size_t got = ReadAll(dataSource.read_cram, dataSource.user, 0,
                             mCram.data(), mCram.size());
        mCram.resize(got);
    }

    // CRAM color mode from VDP2 RAMCTL (offset 0x0E), bits 12-11.
    mCramMode = SE_CRAM_RGB555_1024;
    if ((dataSource.capabilities & SE_CAP_VDP2_REGS) && dataSource.read_vdp2_reg)
    {
        const uint16_t ramctl = dataSource.read_vdp2_reg(dataSource.user, 0x0E);
        switch ((ramctl >> 12) & 0x3)
        {
        case 1:  mCramMode = SE_CRAM_RGB555_2048; break;
        case 2:  mCramMode = SE_CRAM_RGB888_1024; break;
        default: mCramMode = SE_CRAM_RGB555_1024; break;
        }
    }

    mbValid = !mVdp1Vram.empty();
    return mbValid;
}

}  // namespace se
