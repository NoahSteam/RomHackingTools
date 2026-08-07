// PatchLibrary — the "known locations" model behind the Patch menu.
//
// Saturn Explorer has no built-in mapping from a Saturn memory address to a byte in a game
// file; the two are discovered by content search (DataSearch). The user finds a memory
// selection inside the game's data files (using surrounding context to disambiguate) and
// *accepts* the match, which records a PatchLocation here: "the `length` bytes of memory at
// `cpuAddr` live at `fileOffset` in `file`". The library is the single source of truth for
// patching — "Apply changes to disc" reads current memory at each location and emits a Python
// script that writes those bytes into the mapped files. The library persists to a project file.
//
// Pure model + text I/O + script emission — no ImGui / App / platform dependency, so it is
// unit-testable on its own.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sfe
{

// One accepted mapping: a memory range and where it lives in a game file (relative to the
// data directory). `expected` is the memory content captured when the location was accepted —
// the on-disc baseline, used to skip unchanged locations and to warn about drift.
struct PatchLocation
{
    std::string          label;        // human label, e.g. "WRAM 0x00250100 (8 bytes)"
    uint32_t             cpuAddr = 0;  // Saturn CPU address of the mapped bytes
    uint32_t             length = 0;   // number of bytes
    std::string          file;         // path RELATIVE to the data dir, '/'-separated
    uint64_t             fileOffset = 0;   // byte offset of the mapped bytes within `file`
    std::vector<uint8_t> expected;     // bytes at accept time (baseline; size == length)
};

// A single location's contribution to a patch run, produced by EmitPython().
struct PatchOutcome
{
    const PatchLocation* location = nullptr;
    std::vector<uint8_t> current;   // current memory bytes (what will be written)
    bool changed = false;           // current != expected (only changed ones are emitted)
    bool readFailed = false;        // memory could not be read for this location
};

class PatchLibrary
{
public:
    // Insert `loc`, or update the existing entry at the same (file, fileOffset). Marks dirty
    // when anything actually changed.
    void AddOrUpdate(const PatchLocation& loc);

    // Remove the entry at index `i` (no-op if out of range). Marks dirty.
    void RemoveAt(size_t i);

    // Drop every entry. Marks dirty only if there was something to clear.
    void Clear();

    const std::vector<PatchLocation>& Entries() const { return mEntries; }
    size_t Count() const { return mEntries.size(); }

    bool Dirty() const { return mDirty; }
    void ClearDirty() { mDirty = false; }

    // Serialize / restore the library as a small text project file. Returns false on I/O error.
    // Save clears the dirty flag; Load replaces the current contents and clears dirty.
    bool SaveProject(const std::string& path) const;
    bool LoadProject(const std::string& path);

    // Serialize to / parse from the project text (exposed for testing without touching disk).
    std::string Serialize() const;
    bool        Deserialize(const std::string& text);

    // Read current memory for each entry via `readMem(cpuAddr, length, out)` (returns true on
    // success). Produces one PatchOutcome per entry and returns the Python patch script that
    // writes the CHANGED entries' current bytes into their files (relative to the script's own
    // directory). Unchanged and read-failed entries are recorded in `outcomes` but excluded
    // from the script. If no entry changed, the script still parses and simply patches nothing.
    std::string EmitPython(
        const std::function<bool(uint32_t addr, uint32_t len, std::vector<uint8_t>& out)>& readMem,
        std::vector<PatchOutcome>& outcomes) const;

private:
    std::vector<PatchLocation> mEntries;
    bool                       mDirty = false;
};

}  // namespace sfe
