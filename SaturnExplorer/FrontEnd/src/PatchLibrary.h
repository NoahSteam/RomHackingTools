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
    bool changed = false;           // current memory != expected (only changed ones are emitted)
    bool readFailed = false;        // memory could not be read for this location
};

// Can `loc` be represented in the project format, and used to patch anything?
//
// The project text is tab-separated with the label as the line's remainder, so a tab or newline
// in a field silently re-splits the record on reload: a label with a tab shifts every field after
// it, and one with a newline becomes a second, malformed line. `file` is also the path the
// generated script writes to, so a separator in it cannot be substituted away -- it has to be
// refused. And `expected` is the baseline the script compares the target against, so a size other
// than `length` means the comparison is against the wrong number of bytes.
//
// On failure, fills `why` (when given) with a message fit for the UI.
bool PatchLocationValid(const PatchLocation& loc, std::string* why = nullptr);

class PatchLibrary
{
public:
    // Insert `loc`, or update the existing entry at the same (file, fileOffset). Marks dirty
    // when anything actually changed.
    //
    // Returns false and changes nothing when the location cannot be represented (see
    // PatchLocationValid), filling `error` when given. The label is the one field that is
    // repaired rather than refused: it is cosmetic, so a tab or newline in it becomes a space.
    bool AddOrUpdate(const PatchLocation& loc, std::string* error = nullptr);

    // Remove the entry at index `i` (no-op if out of range). Marks dirty.
    void RemoveAt(size_t i);

    const std::vector<PatchLocation>& Entries() const { return mEntries; }
    size_t Count() const { return mEntries.size(); }

    bool Dirty() const { return mDirty; }
    void ClearDirty() { mDirty = false; }

    // Serialize to / parse from the project text. The app pairs Serialize() with the platform
    // save-file dialog; LoadProject reads a chosen file and replaces the current contents
    // (clearing dirty). Deserialize is the in-memory parse used by both LoadProject and tests.
    // A record that does not parse, or whose baseline is not `length` bytes, fails the whole
    // parse rather than being skipped: a project that silently loads with some of its locations
    // missing is worse than one that refuses to load, because the next save writes the loss back.
    std::string Serialize() const;
    bool        Deserialize(const std::string& text, std::string* error = nullptr);
    bool        LoadProject(const std::string& path, std::string* error = nullptr);

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
