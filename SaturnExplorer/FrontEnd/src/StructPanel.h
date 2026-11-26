// StructPanel -- the "Structs" window: persisted struct views over emulator memory, shown as a
// tree (fields, array elements, nested structs, lazily followed pointers) or, for an array of
// structs, as a table with one element per row. Reads and writes go through IMemoryBackend only;
// the definitions and their text format live in Debug/StructDefs.h (the UI-free model).
//
// Persistence is one text file in the work folder (saturn_structs.txt). A file that failed to
// load is never overwritten: its text is held for the user to fix, and saving is refused until
// text that parses has been applied (see SaveSession).
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui.h"

#include "Debug/BreakpointManager.h"
#include "Debug/MemoryBackend.h"
#include "Debug/StructDefs.h"
#include "Debug/WatchList.h"
#include "Platform/IPlatform.h"

namespace sfe
{

class StructPanel
{
public:
    // "Add to Watch" on a scalar leaf. WatchPanel has a public AddWatch(name, expr, type), so the
    // App forwards this request to it; the panel itself holds no WatchPanel reference.
    struct WatchRequest
    {
        bool        pending = false;
        std::string name;
        std::string expression;
        WatchType   type = WatchType::U32;
    };

    // Draw the "Structs" window. 'dt' drives the ~10 Hz refresh and the change tint. 'outHexJump'
    // receives an address for "View in Memory" (the App reveals it in the Hex Editor, as for
    // Watch); 'outAddWatch' receives an "Add to Watch" request (check .pending afterwards).
    void Draw(IMemoryBackend& backend, IExpressionResolver& resolver, IPlatform& platform,
              BreakpointManager& bps, float dt, uint32_t* outHexJump = nullptr,
              WatchRequest* outAddWatch = nullptr);

    // Session persistence, mirroring WatchPanel::LoadSession/SaveSession. LoadSession returns
    // false (with the reason) when the file exists but could not be used; the panel then holds
    // its text and SaveSession refuses until the user applies text that parses. SaveSession
    // returns true when the file is current or there was legitimately nothing to write, and
    // false (with the reason) when it was refused or the write failed.
    bool LoadSession(std::string* error = nullptr);
    bool SaveSession(std::string* error = nullptr);
    // True when the library or the views changed since the last load or successful save.
    bool NeedsSave() const { return mDirty; }

    // Add a view of the first defined struct at 'addr' (used by Watch's "View as Struct...").
    // With no struct defined yet the Definitions editor opens and the view is created as soon as
    // text that defines a struct is applied.
    void RequestViewAt(uint32_t addr, const std::string& label);

    // --- Definitions / import / export, public so tests and the App can drive them ---
    // Replace the struct definitions with 'text'. Views are kept (their ids are remapped); a view
    // whose struct is gone is kept as a broken row. Returns false, changing nothing, when the text
    // does not parse; the errors are then in DefinitionErrors().
    bool ApplyDefinitions(const std::string& text);
    // Import REPLACES the whole library (structs and views) with 'text', like Watch's import
    // replaces its list. Rejected without any change when the text does not parse.
    bool ImportText(const std::string& text, std::string* message = nullptr);
    // The full text that is saved and exported: Emit() plus one "//!broken view ..." comment per
    // broken view, so a view whose struct was deleted survives a restart instead of vanishing.
    std::string FullText() const;

    const StructLibrary& Library() const { return mLib; }
    const std::vector<ParseError>& DefinitionErrors() const { return mDefErrors; }
    const std::string& DefinitionsText() const { return mDefText; }
    bool LoadFailed() const { return mLoadFailed; }
    size_t BrokenViewCount() const { return mBroken.size(); }
    const std::string& Notice() const { return mNotice; }
    bool NoticeIsError() const { return mNoticeError; }
    bool IsEditing() const { return mEdit.active; }
    const std::string& EditError() const { return mEdit.err; }

    // --- Test observability: what the last frame drew, only collected when enabled ---
    struct RowRecord
    {
        std::string key;        // "v<viewId>", "v1.pos[2]", "v1[3].vel.x", "v1.next->hp"
        std::string label;
        std::string address;
        std::string type;
        std::string value;
        bool        editing = false;
        bool        broken = false;
        float       rowY = 0.0f;                 // screen Y of the row's first cell
        ImVec2      valueMin, valueMax;          // the Value cell
        ImVec2      menuMin, menuMax;            // the "..." button (zero in table cells)
    };
    struct MenuItemRecord
    {
        std::string label;
        ImVec2      min, max;
        bool        enabled = true;
    };
    void SetRecordRows(bool on) { mRecord = on; }
    // Open every tree node the first time it is seen (a test cannot click what it cannot find).
    void SetExpandAll(bool on) { mExpandAll = on; }
    const std::vector<RowRecord>& Rows() const { return mRows; }
    const std::vector<MenuItemRecord>& MenuItems() const { return mMenuItems; }

private:
    // One decoded-from-memory struct instance: a view root, an array element or a pointer target.
    struct Instance
    {
        std::string structName;
        uint32_t    addr = 0;
        uint32_t    size = 0;
        std::vector<uint8_t> bytes;
        std::vector<uint8_t> prev;               // bytes before the latest change
        std::vector<uint8_t> valid;              // per byte; empty when allValid
        std::vector<float>   changedAt;          // per byte, panel clock; empty until a change
        bool        haveData = false;
        bool        allValid = false;
        bool        anyValid = false;
        uint64_t    epoch = 0;                   // refresh epoch of the last read
        uint64_t    lastUsed = 0;

        bool Has(uint32_t off, uint32_t n) const;
    };

    struct ViewUi
    {
        int      tableMode = -1;                 // -1 unset (decided by count), 0 tree, 1 table
        char     label[64] = {};
        char     expr[256] = {};
        int      count = 1;
        bool     labelActive = false;
        bool     exprActive = false;
        std::string resolvedExpr;
        uint64_t resolvedEpoch = 0;
        bool     resolved = false;
        uint32_t addr = 0;
        std::string error;
    };

    struct BrokenView
    {
        uint64_t    id = 0;
        std::string label, structName, expression, reason;
        uint32_t    count = 1;
    };

    struct EditState
    {
        bool        active = false;
        uint64_t    viewId = 0;
        std::string key;
        std::string instKey;
        uint32_t    addr = 0;
        FieldDef    field;
        char        buf[96] = {};
        bool        focusPending = false;
        bool        hadFocus = false;
        int         idleFrames = 0;
        uint64_t    sourceId = 0;
        std::string err;
    };

    // Everything a value cell and its menu need to know about one scalar element.
    struct LeafCtx
    {
        const std::string* key = nullptr;
        const char*   label = "";
        FieldDef      field;                     // one element: count 1, not an array
        uint32_t      addr = 0;
        uint32_t      size = 0;
        bool          have = false;
        StructValue   sv;
        uint64_t      viewId = 0;
        std::string   instKey;
        const Instance* inst = nullptr;
        uint32_t      instOff = 0;
    };

    // --- drawing ---
    void DrawToolbar(IPlatform& platform);
    void DrawViews();
    void DrawDefinitions();
    void DrawViewRoot(const StructView& v);
    void DrawBrokenRow(const BrokenView& b);
    void DrawElementTable(const StructView& v, ViewUi& ui, const StructDef& def, uint32_t elemSize);
    void DrawStructBody(const StructDef& def, const StructLayout& lay, Instance& inst, uint32_t base,
                        const std::string& key, int hops, std::vector<uint32_t>& chain, uint64_t viewId);
    void DrawLeafRow(const char* label, const FieldDef& elem, Instance& inst, uint32_t off,
                     const std::string& key, int hops, std::vector<uint32_t>& chain, uint64_t viewId);
    bool DrawContainerRow(const char* label, const std::string& type, uint32_t addr,
                          const std::string& key, bool defaultOpen);
    void DrawMessageRow(const std::string& label, const std::string& detailTip);
    void ValueCell(LeafCtx& c, RowRecord* rec);
    void EditBox(LeafCtx& c);
    void LeafMenuBody(LeafCtx& c);
    void ContainerMenuBody(uint32_t addr);
    bool MenuItemRec(const char* label, bool enabled);
    void EnsureViewsTable();
    void EndViewsTable();
    bool BeginRow(const char* label, bool hasChildren, bool noPushOnOpen, bool defaultOpen, RowRecord* rec);

    // --- reads ---
    Instance& Acquire(const std::string& key, const std::string& structName, uint32_t addr, uint32_t size);
    void ReadInstances(const std::vector<Instance*>& list);
    void CommitRead(Instance& inst, std::vector<uint8_t>&& bytes, std::vector<uint8_t>&& valid, bool allValid);
    void Refresh(Instance& inst);               // read now if stale

    // --- editing ---
    void BeginEdit(const LeafCtx& c);
    void CancelEdit();
    void CommitEdit();

    // --- model plumbing ---
    ViewUi& UiFor(const StructView& v);
    void ResolveView(const StructView& v, ViewUi& ui);
    void AddDefaultView(const std::string& label, const std::string& expr, const std::string& structName);
    void SetNotice(const std::string& text, bool error, float seconds = 8.0f);
    std::string DefsOnlyText(const StructLibrary& lib) const;
    void ReplaceLibrary(StructLibrary&& next, std::vector<BrokenView>&& broken,
                        const std::unordered_map<uint64_t, uint64_t>& idMap);
    void HealBroken();
    void DoImport(IPlatform& platform);
    void DoExport(IPlatform& platform);
    void FlushPendingView();

    StructLibrary mLib;
    uint64_t      mLibEpoch = 1;                 // bumps when the library object is replaced
    std::vector<BrokenView> mBroken;
    uint64_t      mNextBrokenId = (1ull << 40);

    std::unordered_map<uint64_t, ViewUi>     mViewUi;
    std::unordered_map<std::string, Instance> mInstances;
    std::unordered_map<std::string, std::vector<LeafField>> mLeafCache;   // struct + epoch
    uint64_t mLeafCacheEpoch = 0;
    std::unordered_map<std::string, bool> mLeafTruncated;

    // Frame context (valid only inside Draw).
    IMemoryBackend*      mBe = nullptr;
    IExpressionResolver* mResolver = nullptr;
    BreakpointManager*   mBps = nullptr;
    uint32_t*            mHexOut = nullptr;
    WatchRequest*        mWatchOut = nullptr;
    bool                 mInTable = false;
    bool                 mHeaderDone = false;

    uint64_t mEpoch = 1;
    uint64_t mFrame = 0;
    uint64_t mLastSource = ~0ull;
    double   mClock = 0.0;
    float    mRefreshAccum = 0.0f;
    float    mRefreshHz = 10.0f;

    EditState mEdit;

    // Definitions editor.
    bool        mShowDefs = false;
    std::string mDefText;
    bool        mDefDirty = false;
    std::vector<ParseError> mDefErrors;
    int         mSelectedStruct = -1;

    // Persistence.
    enum class LoadState { NotLoaded, Loaded, Failed };
    LoadState   mLoadState = LoadState::NotLoaded;
    bool        mLoadFailed = false;
    std::string mLoadError;
    std::string mHeldText;                       // the unusable file's text, kept for the user
    bool        mFileExisted = false;
    bool        mDirty = false;

    std::string mNotice;
    bool        mNoticeError = false;
    float       mNoticeTime = 0.0f;

    bool        mPendingView = false;            // RequestViewAt before any struct existed
    uint32_t    mPendingAddr = 0;
    std::string mPendingLabel;
    bool        mFocusWindow = false;

    // Test observability.
    bool        mRecord = false;
    bool        mExpandAll = false;
    std::vector<RowRecord>      mRows;
    std::vector<MenuItemRecord> mMenuItems;
};

}  // namespace sfe
