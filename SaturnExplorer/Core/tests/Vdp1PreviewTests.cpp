#include <iostream>
#include "FakeVdpSource.h"
#include "GeometryBuilder.h"
#include "Vdp1Rasterizer.h"

int main()
{
    for (int size : { 64, 512, 2048 })
    {
        se_test::State state(0x100);
        se_test::WriteSystemClip(state, size, size);
        se_test::PutBE16(state.vdp1, 0x20, 4);
        se_test::PutBE16(state.vdp1, 0x24, 0x40);
        se_test::PutBE16(state.vdp1, 0x26, 0xFFFF);
        se_test::PutBE16(state.vdp1, 0x30, size - 1);
        se_test::PutBE16(state.vdp1, 0x34, size - 1);
        se_test::PutBE16(state.vdp1, 0x36, size - 1);
        se_test::PutBE16(state.vdp1, 0x3A, size - 1);
        se_test::PutBE16(state.vdp1, 0x40, 0x8000);
        se::Vdp1Scene scene;
        se::GeometryBuilder::Build(state.vdp1, scene);
        se::SpritePriorityTable prios;
        se::MixState mix;
        std::vector<uint8_t> pixels;
        const size_t work = se::Vdp1Rasterizer::RenderSpritePreview(scene, 0, state.vdp1, state.cram,
            SE_CRAM_RGB555_1024, prios, mix, 0, 0, 0, size, size, 64, 64, pixels);
        // A deterministic work limit, rather than a timing assertion: the native
        // quad area must never determine preview allocations or raster work.
        if (!work || work > 2 * 64 * 64 * 16 || pixels.size() != 64 * 64 * 4)
        {
            std::cerr << "Unbounded preview work for " << size << ": " << work << '\n';
            return 1;
        }
    }
    std::cout << "VDP1 preview work-budget tests passed\n";
}
