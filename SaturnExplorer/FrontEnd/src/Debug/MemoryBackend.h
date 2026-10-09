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

    // Identifies what the backend is currently serving. It changes whenever the bytes behind an
    // address become different data -- another source, another scrubbed frame -- so a panel
    // holding an edit in flight can tell that the edit's target is gone. 0 when not connected.
    virtual uint64_t SourceId() const { return 0; }

    // True when memory at 'address' can be edited (a writable region is loaded).
    virtual bool CanWrite(uint32_t address) const { (void)address; return false; }

    // Write 'bytes' (big-endian Saturn bytes) at 'address'. Returns the number
    // written (0 if the region isn't writable). Default: no-op read-only backend.
    virtual size_t WriteMemory(uint32_t address, const uint8_t* bytes, size_t size)
    { (void)address; (void)bytes; (void)size; return 0; }

    // Why an edit at 'address' is not accepted -- for the UI to say, instead of swallowing the
    // refusal. Meaningful once CanWrite(address) is false or a WriteMemory returned short; empty
    // when the backend has nothing to add.
    virtual std::string WriteRefusal(uint32_t address) const { (void)address; return std::string(); }

    // Whether an accepted edit at 'address' is also handed to the source behind the backend (a
    // live emulator's poke queue, a scrubbed frame's replay list) rather than changing only the
    // loaded snapshot. Lets the UI say "sent to the emulator" or "this view only".
    virtual bool WriteReachesSource(uint32_t address) const { (void)address; return false; }
};

// Backend over an se_context. Holds a pointer-to-pointer so it always follows the
// app's current context (live snapshot, or a paused scrub frame) without re-wiring.
// Read a whole region through 'backend' into 'out'. Chunked, because a single read is capped
// (ContextBackend::ReadOne caps a request at 64 KiB) -- but the whole region lands in one
// buffer, so a caller scanning for a byte sequence still finds a match that straddles a chunk
// boundary. Returns false and clears 'out' if any chunk fails, so a partially read region is
// never mistaken for a complete one: "nothing found here" and "nothing was looked at" are
// different answers and callers have to be able to tell them apart.
// Inline so a caller needs only this header and the IMemoryBackend interface -- the pure
// scanners and their unit tests then link neither MemoryBackend.cpp nor the core behind it.
inline bool ReadRegionBytes(IMemoryBackend& backend, uint32_t base, uint32_t size,
                            std::vector<uint8_t>& out)
{
    out.clear();
    if (size == 0) return false;
    const uint32_t kChunk = 0x10000;   // ReadOne caps a single request at 64 KiB
    out.reserve(size);
    for (uint32_t off = 0; off < size; off += kChunk)
    {
        const uint32_t n = (size - off < kChunk) ? (size - off) : kChunk;
        std::vector<MemoryReadRequest> req{ { base + off, n } };
        std::vector<MemoryReadResult> res = backend.ReadMemoryBatch(req);
        if (res.empty() || !res[0].success || res[0].bytes.size() != n)
        {
            out.clear();
            return false;
        }
        out.insert(out.end(), res[0].bytes.begin(), res[0].bytes.end());
    }
    return true;
}

class ContextBackend : public IMemoryBackend
{
public:
    explicit ContextBackend(se_context** contextSlot) : mContext(contextSlot) {}

    bool Connected() const override { return mContext && *mContext; }
    std::vector<MemoryReadResult> ReadMemoryBatch(
        const std::vector<MemoryReadRequest>& requests) override;
    uint64_t SourceId() const override;
    bool CanWrite(uint32_t address) const override;
    size_t WriteMemory(uint32_t address, const uint8_t* bytes, size_t size) override;
    std::string WriteRefusal(uint32_t address) const override;
    bool WriteReachesSource(uint32_t address) const override;

    // Force the backend read-only regardless of the context (e.g. while scrubbing a recorded
    // frame on a server that can't rewind, so edits that would go nowhere are disabled). 'why' is
    // what WriteRefusal tells the user; it must outlive the call (a string literal).
    void SetReadOnly(bool readOnly, const char* why = nullptr)
    {
        mForceReadOnly = readOnly;
        mReadOnlyWhy = why;
    }

    // The VDP register windows are served through the snapshot's register setters, which never
    // reach an emulator or a replay: an edit there changes this view and is gone on the next
    // capture. That is a fair scratchpad on a loaded dump or savestate, so the windows are
    // editable there; on a live emulator or a scrubbed frame they are not offered at all.
    void SetRegistersReadOnly(bool readOnly) { mRegistersReadOnly = readOnly; }

    // Call when the data behind the context changes without the context pointer changing
    // (a different scrubbed frame loaded in place, or a destroyed context's address reused), so
    // SourceId() moves and a panel drops its in-flight edit.
    void NoteSourceChanged() { ++mGeneration; }

    // Map a CPU-visible Saturn address (mirror bits normalized) to a captured
    // region and read 'size' bytes. Public so hover/operand previews can reuse it.
    MemoryReadResult ReadOne(uint32_t address, uint32_t size) const;

private:
    se_context** mContext = nullptr;
    bool         mForceReadOnly = false;
    const char*  mReadOnlyWhy = nullptr;
    bool         mRegistersReadOnly = false;
    uint64_t     mGeneration = 0;
};

}  // namespace sfe
