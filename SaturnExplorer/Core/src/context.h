// Context — the C++ core object behind the opaque se_context* handle. The C ABI
// shim in host_abi.cpp is a thin translation layer over this class; all real
// logic lives on the C++ side (ARCHITECTURE.md §3, the hybrid boundary).
#pragma once

#include <cstring>

#include "saturnexplorer/se_host.h"
#include "hardware_snapshot.h"

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

    // Snapshot state for the current frame.
    se_result BeginFrame()
    {
        return mSnapshot.Capture(mDs) ? SE_OK : SE_ERR_NO_DATA;
    }

    // --- Query surface. M1: parsing/rendering not implemented yet, so these
    // return empty/unimplemented but exercise the boundary end-to-end. ---

    size_t CommandCount() const { return 0; }
    size_t SpriteCount() const { return 0; }
    size_t VramRegionCount() const { return 0; }

    bool HasSnapshot() const { return mSnapshot.Valid(); }

    const se_data_source& DataSource() const { return mDs; }
    const se_config& Config() const { return mCfg; }

private:
    se_data_source   mDs;
    se_config        mCfg;
    HardwareSnapshot mSnapshot;
};

}  // namespace se
