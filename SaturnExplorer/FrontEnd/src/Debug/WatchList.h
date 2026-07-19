// WatchList — the persistent + runtime model behind the Watch Window. Holds the
// user's watch entries, resolves their address expressions, formats their values
// (big-endian, per the user-chosen type), and serializes to/from JSON. No ImGui
// here; the panel renders this and the ContextBackend feeds it bytes.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Debug/MemoryBackend.h"

namespace sfe
{

enum class WatchType
{
    U8, S8, U16, S16, U32, S32, RGB555, Pointer
};

// Type metadata (kept in one place so the dropdown, byte size, and breakpoint size
// all agree).
const char*  WatchTypeName(WatchType t);            // "U16"
uint32_t     WatchTypeSize(WatchType t);            // bytes read (1/2/4)
bool         WatchTypeFromName(const char* s, WatchType& out);
extern const WatchType kAllWatchTypes[8];

// One watch — the persisted fields only. The transient runtime state (resolved
// address, last value, change highlight) lives in the panel's per-row scratch.
struct WatchEntry
{
    uint64_t    id = 0;              // stable, for stale-response matching
    std::string name;
    std::string expression = "0x06000000";
    WatchType   type = WatchType::U16;
    bool        enabled = true;
};

// Address-expression resolver seam: today "0xADDR [+|- N]"; later symbols/pointers.
class IExpressionResolver
{
public:
    virtual ~IExpressionResolver() = default;
    // Returns true and sets 'outAddr' on success; false with 'outError' otherwise.
    virtual bool Resolve(const std::string& expr, uint32_t& outAddr,
                         std::string& outError) = 0;
};

// Default resolver: a base hex address plus an optional single +/- offset
// (hex with 0x, decimal otherwise). No scripting, no side effects.
class SimpleExpressionResolver : public IExpressionResolver
{
public:
    bool Resolve(const std::string& expr, uint32_t& outAddr,
                 std::string& outError) override;
};

// Normalize a pure-address token to 0xXXXXXXXX (upper). Non-address expressions
// (containing +/-) are returned trimmed but otherwise unchanged.
std::string NormalizeExpression(const std::string& expr);

// The formatted value of a watch, ready for the table cell.
struct WatchValue
{
    bool        valid = false;      // false -> show 'text' (e.g. "Invalid address")
    std::string text;               // "7FFF", "-128", "R31 G14 B28", "-> 0x06025040"
    bool        hasSwatch = false;  // RGB555: draw a colour chip
    uint8_t     r = 0, g = 0, b = 0;
    bool        isPointer = false;
    uint32_t    pointerTarget = 0;
    bool        pointerSuspicious = false;   // outside known Saturn ranges
    // For the change highlight: a signed magnitude and whether up/down is meaningful.
    long long   numeric = 0;
    bool        numericMeaningful = false;
};

// Interpret 'bytes' (big-endian, WatchTypeSize(type) long) as 'type'.
WatchValue FormatWatchValue(WatchType type, const MemoryReadResult& mem);

// Is 'addr' inside a plausible Saturn CPU region (for the pointer sanity check)?
bool IsPlausibleSaturnAddress(uint32_t addr);

class WatchList
{
public:
    std::vector<WatchEntry>& Entries() { return mEntries; }
    const std::vector<WatchEntry>& Entries() const { return mEntries; }

    WatchEntry& Add(const std::string& name, const std::string& expr,
                    WatchType type, bool enabled);
    void RemoveAt(size_t i);
    void Clear() { mEntries.clear(); }

    std::string ToJson() const;
    // Replaces the list on success. Skipped/invalid entries are appended to
    // 'errors' (one line each) but do not abort the import. Returns imported count,
    // or -1 if the document itself is unparseable / wrong version.
    int FromJson(const std::string& json, std::vector<std::string>& errors);

private:
    std::vector<WatchEntry> mEntries;
    uint64_t                mNextId = 1;
};

}  // namespace sfe
