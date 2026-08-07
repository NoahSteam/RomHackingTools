// MemoryBackend — the debugger's view of emulator memory, isolated from any
// specific emulator. Panels (Watch, later Assembly/Hex) depend only on this
// interface; the concrete backend reads from an se_context (which the live driver
// or a savestate fills), so no Yabause-specific code reaches the UI widgets.
//
// Reads are expressed as a batch so a backend can coalesce them; the current
// ContextBackend serves them synchronously from the already-captured snapshot (a
// fast local copy — it never blocks on I/O). Note the return is synchronous: a
// remote/async backend would change this seam to a request-id + poll (a breaking
// change, not additive), so today's signature is not yet async-ready.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"

namespace sfe
{

struct MemoryReadRequest
{
    uint32_t address = 0;
    uint32_t size = 0;
};

struct MemoryReadResult
{
    bool                 success = false;
    std::vector<uint8_t> bytes;      // big-endian Saturn bytes, 'size' long on success
    std::string          error;      // human-readable reason when !success
};

class IMemoryBackend
{
public:
    virtual ~IMemoryBackend() = default;

    // True when a source is loaded and reads can succeed. Panels show a
    // "disconnected" state otherwise instead of spamming errors.
    virtual bool Connected() const = 0;

    // Read each request; results are parallel to 'requests'. Never throws; a
    // failed read yields {success=false, error=...}. Bytes are raw Saturn memory
    // (big-endian) so callers interpret multi-byte values big-endian.
    virtual std::vector<MemoryReadResult> ReadMemoryBatch(
        const std::vector<MemoryReadRequest>& requests) = 0;

    // True when memory at 'address' can be edited (a writable region is loaded).
    virtual bool CanWrite(uint32_t address) const { (void)address; return false; }

    // Write 'bytes' (big-endian Saturn bytes) at 'address'. Returns the number
    // written (0 if the region isn't writable). Default: no-op read-only backend.
    virtual size_t WriteMemory(uint32_t address, const uint8_t* bytes, size_t size)
    { (void)address; (void)bytes; (void)size; return 0; }
};

// Backend over an se_context. Holds a pointer-to-pointer so it always follows the
// app's current context (live snapshot, or a paused scrub frame) without re-wiring.
class ContextBackend : public IMemoryBackend
{
public:
    explicit ContextBackend(se_context** contextSlot) : mContext(contextSlot) {}

    bool Connected() const override { return mContext && *mContext; }
    std::vector<MemoryReadResult> ReadMemoryBatch(
        const std::vector<MemoryReadRequest>& requests) override;
    bool CanWrite(uint32_t address) const override;
    size_t WriteMemory(uint32_t address, const uint8_t* bytes, size_t size) override;

    // Force the backend read-only regardless of the context (e.g. while scrubbing a recorded
    // frame on a server that can't rewind, so edits that would go nowhere are disabled).
    void SetReadOnly(bool readOnly) { mForceReadOnly = readOnly; }

    // Map a CPU-visible Saturn address (mirror bits normalized) to a captured
    // region and read 'size' bytes. Public so hover/operand previews can reuse it.
    MemoryReadResult ReadOne(uint32_t address, uint32_t size) const;

private:
    se_context** mContext = nullptr;
    bool         mForceReadOnly = false;
};

}  // namespace sfe
