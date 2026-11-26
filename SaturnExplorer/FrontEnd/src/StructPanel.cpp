#include "Theme.h"
#include "saturnexplorer/SeFilePath.h"   // UTF-8 paths on Windows
#include "StructPanel.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "Debug/WriteDestText.h"
#include "FileWrite.h"
#include "PanelWidgets.h"
#include "WorkFiles.h"

namespace sfe
{

namespace
{
const char*    kStructsFile = "saturn_structs.txt";
const char*    kBrokenTag = "//!broken ";
constexpr uint32_t kReadChunk = 0x10000;       // ReadOne caps one request at 64 KiB
constexpr int      kMaxPointerHops = 8;
constexpr size_t   kMaxTableLeaves = 64;
constexpr uint32_t kMaxTreeElements = 256;     // tree rows cannot be clipped: open rows vary in height
constexpr size_t   kMaxFallbackRanges = 4096;
constexpr float    kHighlightSecs = 1.0f;
constexpr size_t   kMaxRecordedRows = 6000;

constexpr ImU32 kUpCol      = IM_COL32(40, 150, 70, 255);
constexpr ImU32 kDownCol    = IM_COL32(175, 55, 55, 255);
constexpr ImU32 kNeutralCol = IM_COL32(170, 150, 45, 255);

std::string Hex8(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

// Mirror bits 31-29 select cached / cache-through views of the same bytes; folding them keeps
// the pointer-cycle check from being fooled by 0x06... vs 0x26... spellings of one address.
uint32_t Canon(uint32_t a) { return a & 0x1FFFFFFFu; }

std::string Trimmed(const std::string& s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

std::string SanitizeLabel(const std::string& in)
{
    std::string s;
    for (char c : in)
        s += ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') ? c : '_';
    if (s.empty()) s = "view";
    if (s[0] >= '0' && s[0] <= '9') s = "v" + s;
    return s;
}

WatchType WatchTypeFor(FieldKind k)
{
    switch (k)
    {
    case FieldKind::U8:  return WatchType::U8;
    case FieldKind::S8:  return WatchType::S8;
    case FieldKind::U16: return WatchType::U16;
    case FieldKind::S16: return WatchType::S16;
    case FieldKind::S32: return WatchType::S32;
    case FieldKind::Fixed16_16: return WatchType::S32;   // Watch has no fixed type; raw s32
    case FieldKind::Pointer: return WatchType::Pointer;
    case FieldKind::RGB555:  return WatchType::RGB555;
    default: return WatchType::U32;
    }
}

void CopyStr(char* dst, size_t cap, const std::string& s)
{
    const size_t n = (std::min)(s.size(), cap - 1);
    std::memcpy(dst, s.data(), n);
    dst[n] = '\0';
}

int StringResize(ImGuiInputTextCallbackData* d)
{
    if (d->EventFlag == ImGuiInputTextFlags_CallbackResize)
    {
        std::string* s = static_cast<std::string*>(d->UserData);
        s->resize((size_t)d->BufTextLen);
        d->Buf = const_cast<char*>(s->c_str());
    }
    return 0;
}

// Scalar byte ranges of a struct (arrays whole, nested structs element by element), for the
// per-field fallback read. Capped so a pathological layout cannot explode the request list.
void CollectRanges(const StructLibrary& lib, const std::string& name, uint32_t base,
                   std::vector<std::pair<uint32_t, uint32_t>>& out, int depth = 0)
{
    const StructDef* def = lib.Find(name);
    const StructLayout* lay = lib.Layout(name);
    if (!def || !lay || depth > 16) return;
    for (size_t i = 0; i < def->fields.size() && out.size() < kMaxFallbackRanges; ++i)
    {
        const FieldDef& f = def->fields[i];
        const FieldLayout& fl = lay->fields[i];
        if (f.kind == FieldKind::Struct)
        {
            for (uint32_t k = 0; k < f.count && out.size() < kMaxFallbackRanges; ++k)
                CollectRanges(lib, f.structName, base + fl.offset + k * fl.elemSize, out, depth + 1);
        }
        else
            out.push_back({ base + fl.offset, fl.size });
    }
}

// "//!broken view label = Type[count] @ expr;" lines -> broken views. Lenient on purpose: a line
// that does not parse is left alone (it is only a comment to the model).
struct BrokenLine { std::string label, structName, expression; uint32_t count = 1; };

std::vector<BrokenLine> ExtractBroken(const std::string& text)
{
    std::vector<BrokenLine> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line))
    {
        line = Trimmed(line);
        if (line.compare(0, std::strlen(kBrokenTag), kBrokenTag) != 0) continue;
        std::string rest = Trimmed(line.substr(std::strlen(kBrokenTag)));
        if (rest.compare(0, 5, "view ") != 0) continue;
        rest = rest.substr(5);
        const size_t eq = rest.find('=');
        const size_t lb = rest.find('[', eq == std::string::npos ? 0 : eq);
        const size_t rb = rest.find(']', lb == std::string::npos ? 0 : lb);
        const size_t at = rest.find('@', rb == std::string::npos ? 0 : rb);
        if (eq == std::string::npos || lb == std::string::npos || rb == std::string::npos || at == std::string::npos)
            continue;
        BrokenLine b;
        b.label = Trimmed(rest.substr(0, eq));
        b.structName = Trimmed(rest.substr(eq + 1, lb - eq - 1));
        b.count = (uint32_t)std::strtoul(rest.substr(lb + 1, rb - lb - 1).c_str(), nullptr, 0);
        std::string e = Trimmed(rest.substr(at + 1));
        if (!e.empty() && e.back() == ';') e.pop_back();
        b.expression = Trimmed(e);
        if (b.label.empty() || b.structName.empty() || b.expression.empty() || b.count == 0) continue;
        out.push_back(std::move(b));
    }
    return out;
}
}  // namespace

bool StructPanel::Instance::Has(uint32_t off, uint32_t n) const
{
    if (!haveData || off > size || n > size - off) return false;
    if (allValid) return true;
    if (valid.size() != size) return false;
    for (uint32_t i = 0; i < n; ++i)
        if (!valid[off + i]) return false;
    return true;
}

// ---------------------------------------------------------------- model plumbing

void StructPanel::SetNotice(const std::string& text, bool error, float seconds)
{
    mNotice = text;
    mNoticeError = error;
    mNoticeTime = seconds;
}

std::string StructPanel::DefsOnlyText(const StructLibrary& lib) const
{
    // Emit() is canonical: a view always starts a line with "view ", and "view" is a reserved word,
    // so no struct line can be mistaken for one.
    std::istringstream in(lib.Emit());
    std::string line, out;
    while (std::getline(in, line))
    {
        if (line.compare(0, 5, "view ") == 0) continue;
        out += line + "\n";
    }
    while (out.size() > 1 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n') out.pop_back();
    return out;
}

std::string StructPanel::FullText() const
{
    std::string t = mLib.Emit();
    if (!mBroken.empty() && !t.empty() && t.back() != '\n') t += "\n";
    for (const BrokenView& b : mBroken)
        t += std::string(kBrokenTag) + "view " + b.label + " = " + b.structName + "[" +
             std::to_string(b.count) + "] @ " + b.expression + ";\n";
    return t;
}

void StructPanel::ReplaceLibrary(StructLibrary&& next, std::vector<BrokenView>&& broken,
                                 const std::unordered_map<uint64_t, uint64_t>& idMap)
{
    std::unordered_map<uint64_t, ViewUi> ui;
    for (const auto& kv : idMap)
    {
        auto it = mViewUi.find(kv.first);
        if (it != mViewUi.end()) ui[kv.second] = std::move(it->second);
    }
    mViewUi = std::move(ui);
    mLib = std::move(next);
    ++mLibEpoch;
    mBroken = std::move(broken);
    mInstances.clear();
    mLeafCache.clear();
    mLeafTruncated.clear();
    ++mEpoch;
    CancelEdit();
    mDirty = true;
    mLoadState = LoadState::Loaded;
    mLoadFailed = false;
    mLoadError.clear();
    mHeldText.clear();
    mDefText = DefsOnlyText(mLib);
    mDefDirty = false;
    mDefErrors.clear();
    mSelectedStruct = -1;
    FlushPendingView();
}

bool StructPanel::ApplyDefinitions(const std::string& text)
{
    std::vector<ParseError> errs;
    StructLibrary tmp;
    if (!tmp.Parse(text, errs))
    {
        mDefErrors = errs;
        return false;
    }
    // Rebuild from the canonical definitions so the existing views can go in first, in order,
    // ahead of any view lines that came in with the pasted text.
    StructLibrary next;
    std::vector<ParseError> errs2;
    if (!next.Parse(DefsOnlyText(tmp), errs2))
    {
        mDefErrors = errs2;
        return false;
    }

    std::unordered_map<uint64_t, uint64_t> idMap;
    std::vector<BrokenView> broken;
    auto carry = [&](const std::string& label, const std::string& st, uint32_t count,
                     const std::string& expr, uint64_t oldId, bool oldIsBroken, const std::string& prevReason)
    {
        std::string why;
        if (next.Find(st))
        {
            std::string err;
            const uint64_t id = next.AddView(label, st, count, expr, &err);
            if (id)
            {
                if (!oldIsBroken) idMap[oldId] = id;
                return;
            }
            why = err;
        }
        else
            why = "struct '" + st + "' is not defined";
        BrokenView b;
        b.id = oldIsBroken && oldId ? oldId : mNextBrokenId++;
        b.label = label; b.structName = st; b.expression = expr; b.count = count;
        b.reason = why.empty() ? prevReason : why;
        broken.push_back(std::move(b));
    };
    for (const StructView& v : mLib.Views()) carry(v.label, v.structName, v.count, v.expression, v.id, false, "");
    for (const BrokenView& b : mBroken) carry(b.label, b.structName, b.count, b.expression, b.id, true, b.reason);
    for (const BrokenLine& b : ExtractBroken(text)) carry(b.label, b.structName, b.count, b.expression, 0, true, "");
    for (const StructView& v : tmp.Views())
    {
        std::string err;
        if (!next.AddView(v.label, v.structName, v.count, v.expression, &err))
        {
            BrokenView b;
            b.id = mNextBrokenId++;
            b.label = v.label; b.structName = v.structName; b.expression = v.expression;
            b.count = v.count; b.reason = err;
            broken.push_back(std::move(b));
        }
    }
    ReplaceLibrary(std::move(next), std::move(broken), idMap);
    return true;
}

bool StructPanel::ImportText(const std::string& text, std::string* message)
{
    std::vector<ParseError> errs;
    StructLibrary tmp;
    if (!tmp.Parse(text, errs))
    {
        mDefErrors = errs;
        if (message)
            *message = "Import failed: " + (errs.empty() ? std::string("invalid file")
                       : (errs[0].line ? "line " + std::to_string(errs[0].line) + ": " : std::string()) + errs[0].message);
        return false;
    }
    std::vector<BrokenView> broken;
    for (const BrokenLine& b : ExtractBroken(text))
    {
        BrokenView v;
        v.id = mNextBrokenId++;
        v.label = b.label; v.structName = b.structName; v.expression = b.expression; v.count = b.count;
        v.reason = "struct '" + b.structName + "' is not defined";
        broken.push_back(std::move(v));
    }
    const size_t structs = tmp.Structs().size(), views = tmp.Views().size();
    ReplaceLibrary(std::move(tmp), std::move(broken), {});
    if (message)
        *message = "Imported " + std::to_string(structs) + " structs and " + std::to_string(views) +
                   " views (replaced the previous library)";
    return true;
}

void StructPanel::FlushPendingView()
{
    if (!mPendingView || mLib.Structs().empty()) return;
    mPendingView = false;
    AddDefaultView(mPendingLabel, Hex8(mPendingAddr), mLib.Structs().front().name);
}

void StructPanel::AddDefaultView(const std::string& labelIn, const std::string& expr, const std::string& structName)
{
    const std::string base = SanitizeLabel(labelIn);
    std::string err;
    for (int i = 0; i < 40; ++i)
    {
        const std::string label = i == 0 ? base : base + "_" + std::to_string(i + 1);
        if (mLib.AddView(label, structName, 1, expr, &err))
        {
            mDirty = true;
            return;
        }
    }
    SetNotice("Could not add the view: " + err, true);
}

void StructPanel::RequestViewAt(uint32_t addr, const std::string& label)
{
    mFocusWindow = true;
    if (mLib.Structs().empty())
    {
        mPendingView = true;
        mPendingAddr = addr;
        mPendingLabel = label.empty() ? "view" : label;
        mShowDefs = true;
        if (!mDefDirty && !mLoadFailed) mDefText = DefsOnlyText(mLib);
        SetNotice("Define a struct first; the view at " + Hex8(addr) + " is added once you Apply.", false, 12.0f);
        return;
    }
    AddDefaultView(label.empty() ? "view" : label, Hex8(addr), mLib.Structs().front().name);
}

StructPanel::ViewUi& StructPanel::UiFor(const StructView& v)
{
    ViewUi& ui = mViewUi[v.id];
    if (!ui.labelActive && v.label != ui.label) CopyStr(ui.label, sizeof ui.label, v.label);
    if (!ui.exprActive && v.expression != ui.expr) CopyStr(ui.expr, sizeof ui.expr, v.expression);
    if (ui.count != (int)v.count && ui.count != 0) ui.count = (int)v.count;
    return ui;
}

void StructPanel::ResolveView(const StructView& v, ViewUi& ui)
{
    // Re-resolve once per refresh epoch as well as on a text change: a resolver may follow memory.
    if (ui.resolvedEpoch == mEpoch && ui.resolvedExpr == v.expression) return;
    ui.resolvedEpoch = mEpoch;
    ui.resolvedExpr = v.expression;
    uint32_t a = 0;
    std::string err;
    ui.resolved = mResolver && mResolver->Resolve(v.expression, a, err);
    ui.addr = ui.resolved ? a : 0;
    ui.error = ui.resolved ? std::string() : err;
}

// ---------------------------------------------------------------- persistence

bool StructPanel::LoadSession(std::string* error)
{
    const std::string path = WorkFilePath(kStructsFile);
    mFileExisted = FileOrDirectoryExists(path);
    if (!mFileExisted)
    {
        mLoadState = LoadState::Loaded;      // nothing to lose: the first save may create it
        return true;
    }
    std::string text;
    {
        std::ifstream f(se::NativePath(path).c_str(), std::ios::binary);
        if (!f)
        {
            mLoadState = LoadState::Failed;
            mLoadFailed = true;
            mLoadError = std::string(kStructsFile) + " could not be read; it will not be overwritten.";
            mHeldText.clear();
            if (error) *error = mLoadError;
            return false;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
    std::string msg;
    if (ImportText(text, &msg))
    {
        mDirty = false;
        return true;
    }
    mLoadState = LoadState::Failed;
    mLoadFailed = true;
    mHeldText = text;
    mDefText = text;
    mDefDirty = true;
    mLoadError = std::string(kStructsFile) + " could not be loaded (" + msg + "); it will not be overwritten "
                 "until you Apply definitions that parse.";
    if (error) *error = mLoadError;
    return false;
}

bool StructPanel::SaveSession(std::string* error)
{
    auto refuse = [&](const std::string& why)
    {
        if (error) *error = why;
        return false;
    };
    if (mLoadState == LoadState::Failed)
        return refuse("The struct definitions: not saved, " + std::string(kStructsFile) +
                      " failed to load and is kept as it is. Fix it in Definitions and Apply.");
    const std::string path = WorkFilePath(kStructsFile);
    const bool empty = mLib.Structs().empty() && mLib.Views().empty() && mBroken.empty();
    if (mLoadState == LoadState::NotLoaded)
    {
        // Never loaded: whatever is on disk is unknown to us, so an empty (or any) library must
        // not replace it. Writing is only safe when there is no file at all.
        if (FileOrDirectoryExists(path))
            return refuse("The struct definitions: not saved, " + std::string(kStructsFile) +
                          " exists but was never loaded.");
        if (empty) return true;
    }
    else if (empty && !mFileExisted)
        return true;                          // nothing to write, and no file to empty
    if (!mDirty && mFileExisted) return true;
    const std::string text = FullText();
    std::string why;
    if (!WriteFileAtomically(path, text.data(), text.size(), why))
        return refuse("The struct definitions: not saved, " + why);
    mFileExisted = true;
    mDirty = false;
    return true;
}

void StructPanel::DoImport(IPlatform& platform)
{
    std::string path;
    if (!platform.OpenFileDialog(path)) return;
    std::ifstream f(se::NativePath(path).c_str(), std::ios::binary);
    if (!f) { SetNotice("Import failed: cannot open file.", true); return; }
    std::stringstream ss;
    ss << f.rdbuf();
    std::string msg;
    const bool ok = ImportText(ss.str(), &msg);
    SetNotice(msg, !ok);
}

void StructPanel::DoExport(IPlatform& platform)
{
    const std::string text = FullText();
    const SaveOutcome r = platform.SaveFile(kStructsFile, text.data(), text.size());
    if (r == SaveOutcome::Saved) SetNotice("Exported " + std::to_string(mLib.Structs().size()) + " structs and " +
                                           std::to_string(mLib.Views().size()) + " views", false, 4.0f);
    else if (r == SaveOutcome::Failed) SetNotice(std::string("Could not write ") + kStructsFile, true);
}

// ---------------------------------------------------------------- reads

StructPanel::Instance& StructPanel::Acquire(const std::string& key, const std::string& structName,
                                            uint32_t addr, uint32_t size)
{
    Instance& in = mInstances[key];
    if (in.structName != structName || in.addr != addr || in.size != size)
    {
        in = Instance();
        in.structName = structName;
        in.addr = addr;
        in.size = size;
    }
    in.lastUsed = mFrame;
    return in;
}

void StructPanel::Refresh(Instance& inst)
{
    if (inst.epoch == mEpoch || !mBe || !mBe->Connected()) return;
    ReadInstances({ &inst });
}

void StructPanel::CommitRead(Instance& inst, std::vector<uint8_t>&& bytes, std::vector<uint8_t>&& valid,
                             bool allValid)
{
    if (inst.haveData && inst.bytes.size() == bytes.size())
    {
        bool changed = false;
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            const bool ov = inst.allValid || inst.valid[i];
            const bool nv = allValid || valid[i];
            if (ov && nv && inst.bytes[i] != bytes[i])
            {
                if (inst.changedAt.empty()) inst.changedAt.assign(bytes.size(), -1.0e9f);
                inst.changedAt[i] = (float)mClock;
                changed = true;
            }
        }
        if (changed) inst.prev = inst.bytes;
    }
    inst.bytes = std::move(bytes);
    inst.valid = allValid ? std::vector<uint8_t>() : std::move(valid);
    inst.allValid = allValid;
    inst.anyValid = allValid;
    if (!allValid)
        for (uint8_t b : inst.valid) if (b) { inst.anyValid = true; break; }
    inst.haveData = true;
    inst.epoch = mEpoch;
}

void StructPanel::ReadInstances(const std::vector<Instance*>& list)
{
    if (list.empty() || !mBe) return;
    struct Piece { size_t inst; uint32_t off, n; };
    std::vector<MemoryReadRequest> reqs;
    std::vector<Piece> pieces;
    for (size_t i = 0; i < list.size(); ++i)
        for (uint32_t off = 0; off < list[i]->size; off += kReadChunk)
        {
            const uint32_t n = (std::min)(kReadChunk, list[i]->size - off);
            reqs.push_back({ list[i]->addr + off, n });
            pieces.push_back({ i, off, n });
        }
    std::vector<std::vector<uint8_t>> bytes(list.size());
    std::vector<char> ok(list.size(), 1);
    for (size_t i = 0; i < list.size(); ++i) bytes[i].assign(list[i]->size, 0);
    if (!reqs.empty())
    {
        const std::vector<MemoryReadResult> res = mBe->ReadMemoryBatch(reqs);
        for (size_t p = 0; p < pieces.size(); ++p)
        {
            if (p < res.size() && res[p].success && res[p].bytes.size() == pieces[p].n)
                std::memcpy(bytes[pieces[p].inst].data() + pieces[p].off, res[p].bytes.data(), pieces[p].n);
            else
                ok[pieces[p].inst] = 0;
        }
    }

    // Whole-struct read failed (a struct straddling a region edge, say): read it field by field so
    // only the genuinely unreadable fields show "Unavailable".
    std::vector<std::vector<uint8_t>> valid(list.size());
    std::vector<MemoryReadRequest> freqs;
    struct FPiece { size_t inst; uint32_t off, n; };
    std::vector<FPiece> fpieces;
    for (size_t i = 0; i < list.size(); ++i)
    {
        if (ok[i]) continue;
        valid[i].assign(list[i]->size, 0);
        std::vector<std::pair<uint32_t, uint32_t>> ranges;
        CollectRanges(mLib, list[i]->structName, 0, ranges);
        for (const auto& r : ranges)
            for (uint32_t o = 0; o < r.second; o += kReadChunk)
            {
                const uint32_t n = (std::min)(kReadChunk, r.second - o);
                if (r.first + o + n > list[i]->size) continue;
                freqs.push_back({ list[i]->addr + r.first + o, n });
                fpieces.push_back({ i, r.first + o, n });
            }
    }
    if (!freqs.empty())
    {
        const std::vector<MemoryReadResult> res = mBe->ReadMemoryBatch(freqs);
        for (size_t p = 0; p < fpieces.size() && p < res.size(); ++p)
            if (res[p].success && res[p].bytes.size() == fpieces[p].n)
            {
                std::memcpy(bytes[fpieces[p].inst].data() + fpieces[p].off, res[p].bytes.data(), fpieces[p].n);
                std::memset(valid[fpieces[p].inst].data() + fpieces[p].off, 1, fpieces[p].n);
            }
    }
    for (size_t i = 0; i < list.size(); ++i)
        CommitRead(*list[i], std::move(bytes[i]), std::move(valid[i]), ok[i] != 0);
}

// ---------------------------------------------------------------- editing

void StructPanel::BeginEdit(const LeafCtx& c)
{
    if (!c.have) return;
    if (!mBe || !mBe->CanWrite(c.addr))
    {
        const std::string why = mBe ? mBe->WriteRefusal(c.addr) : std::string();
        SetNotice(why.empty() ? "Writing is not available at " + Hex8(c.addr) + "." : why, true);
        return;
    }
    mEdit = EditState();
    mEdit.active = true;
    mEdit.viewId = c.viewId;
    mEdit.key = *c.key;
    mEdit.addr = c.addr;
    mEdit.field = c.field;
    mEdit.focusPending = true;
    mEdit.sourceId = mBe->SourceId();
    CopyStr(mEdit.buf, sizeof mEdit.buf, c.sv.editText);
}

void StructPanel::CancelEdit()
{
    mEdit = EditState();
}

void StructPanel::CommitEdit()
{
    const uint32_t size = FieldKindSize(mEdit.field.kind);
    uint8_t out[4] = {};
    std::string err;
    if (!EncodeField(mEdit.field, mEdit.field.fmt, mEdit.buf, out, err))
    {
        // Stay in edit mode so the text can be fixed; nothing was written.
        mEdit.err = err;
        mEdit.focusPending = true;
        mEdit.hadFocus = false;
        mEdit.idleFrames = 0;
        SetNotice("Cannot write: " + err, true);
        return;
    }
    const uint32_t addr = mEdit.addr;
    if (mBe->SourceId() != mEdit.sourceId)
    {
        CancelEdit();
        SetNotice("Edit dropped: the memory source changed.", true);
        return;
    }
    std::string result;
    bool error = false;
    if (!mBe->CanWrite(addr))
    {
        const std::string why = mBe->WriteRefusal(addr);
        result = why.empty() ? "Write refused at " + Hex8(addr) + "." : why;
        error = true;
    }
    else
    {
        const size_t n = mBe->WriteMemory(addr, out, size);
        if (n == size)
            result = "Wrote " + std::to_string(size) + " byte" + (size == 1 ? "" : "s") + " at " + Hex8(addr) +
                     ": " + WriteDestText(mBe->WriteDestination(addr));
        else
        {
            const std::string why = mBe->WriteRefusal(addr);
            result = n == 0 ? (why.empty() ? "Write refused at " + Hex8(addr) + "." : why)
                            : "Partially written " + std::to_string(n) + "/" + std::to_string(size) + " at " +
                              Hex8(addr) + (why.empty() ? "" : ": " + why);
            error = true;
        }
    }
    CancelEdit();
    ++mEpoch;                                 // re-read at once so the cell shows what memory now holds
    SetNotice(result, error);
}

// ---------------------------------------------------------------- drawing

void StructPanel::EnsureViewsTable()
{
    if (mInTable) return;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV;
    if (!ImGui::BeginTable("views", 5, flags)) return;
    ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthStretch, 0.28f);
    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthStretch, 0.20f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthStretch, 0.18f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.28f);
    ImGui::TableSetupColumn("...", ImGuiTableColumnFlags_WidthFixed, 30.0f);
    if (!mHeaderDone) { ImGui::TableHeadersRow(); mHeaderDone = true; }
    mInTable = true;
}

void StructPanel::EndViewsTable()
{
    if (!mInTable) return;
    ImGui::EndTable();
    mInTable = false;
}

bool StructPanel::BeginRow(const char* label, bool hasChildren, bool noPushOnOpen, bool defaultOpen, RowRecord* rec)
{
    EnsureViewsTable();
    // Every row is at least a frame tall, so an edit box replacing a text cell cannot grow it.
    ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetFrameHeight());
    ImGui::TableSetColumnIndex(0);
    rec->rowY = ImGui::GetCursorScreenPos().y;
    rec->label = label;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap |
                               ImGuiTreeNodeFlags_OpenOnArrow;
    if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    else if (noPushOnOpen) flags |= ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (hasChildren && (defaultOpen || mExpandAll)) ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    return ImGui::TreeNodeEx(label, flags);
}

bool StructPanel::MenuItemRec(const char* label, bool enabled)
{
    const bool r = ImGui::MenuItem(label, nullptr, false, enabled);
    if (mRecord)
    {
        MenuItemRecord m;
        m.label = label;
        m.min = ImGui::GetItemRectMin();
        m.max = ImGui::GetItemRectMax();
        m.enabled = enabled;
        mMenuItems.push_back(m);
    }
    return r;
}

void StructPanel::ContainerMenuBody(uint32_t addr)
{
    if (MenuItemRec("Copy Address", true)) ImGui::SetClipboardText(Hex8(addr).c_str());
    if (MenuItemRec("View in Memory", true) && mHexOut) *mHexOut = addr;
}

void StructPanel::LeafMenuBody(LeafCtx& c)
{
    const bool canWrite = c.have && mBe && mBe->CanWrite(c.addr);
    if (MenuItemRec("Edit Value", canWrite)) BeginEdit(c);
    if (c.have && mBe && !canWrite && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        const std::string why = mBe->WriteRefusal(c.addr);
        ImGui::SetTooltip("%s", why.empty() ? "Writing is not available here." : why.c_str());
    }
    if (MenuItemRec("Copy Address", true)) ImGui::SetClipboardText(Hex8(c.addr).c_str());
    if (MenuItemRec("Copy Value", c.have)) ImGui::SetClipboardText(c.sv.text.c_str());
    ImGui::Separator();
    // Memory watchpoints take 1, 2 or 4 bytes; every scalar element is one of those.
    const bool bpOk = mBps && (c.size == 1 || c.size == 2 || c.size == 4);
    if (MenuItemRec("Break on Read", bpOk)) mBps->AddMemory(c.addr, c.size, BpKind::MemRead);
    if (MenuItemRec("Break on Write", bpOk)) mBps->AddMemory(c.addr, c.size, BpKind::MemWrite);
    if (MenuItemRec("Break on Read or Write", bpOk)) mBps->AddMemory(c.addr, c.size, BpKind::MemReadWrite);
    ImGui::Separator();
    if (MenuItemRec("View in Memory", true) && mHexOut) *mHexOut = c.addr;
    const bool ptrToStruct = c.have && c.field.kind == FieldKind::Pointer && !c.field.structName.empty() &&
                             c.sv.pointerTarget != 0 && mLib.Find(c.field.structName);
    if (MenuItemRec("Open Pointer Target as New View", ptrToStruct))
        AddDefaultView(c.field.name.empty() ? "target" : c.field.name, Hex8(c.sv.pointerTarget), c.field.structName);
    if (MenuItemRec("Add to Watch", mWatchOut != nullptr))
    {
        mWatchOut->pending = true;
        mWatchOut->name = c.field.name;
        mWatchOut->expression = Hex8(c.addr);
        mWatchOut->type = WatchTypeFor(c.field.kind);
    }
}

void StructPanel::EditBox(LeafCtx& c)
{
    (void)c;
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (mEdit.focusPending)
    {
        ImGui::SetKeyboardFocusHere();
        mEdit.focusPending = false;
    }
    const bool bad = !mEdit.err.empty();
    if (bad)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
    }
    const bool entered = ImGui::InputText("##edit", mEdit.buf, sizeof mEdit.buf,
                                          ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    const bool active = ImGui::IsItemActive();
    if (bad)
    {
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", mEdit.err.c_str());
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }
    if (entered) CommitEdit();
    else if (active) { mEdit.hadFocus = true; mEdit.idleFrames = 0; }
    else if (mEdit.hadFocus || ++mEdit.idleFrames > 4) CancelEdit();   // Escape / click elsewhere
}

void StructPanel::ValueCell(LeafCtx& c, RowRecord* rec)
{
    const float h = ImGui::GetFrameHeight();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    rec->valueMin = p;
    rec->valueMax = ImVec2(p.x + w, p.y + h);
    ImGui::PushID("val");

    if (mEdit.active && mEdit.viewId == c.viewId && mEdit.addr == c.addr && mEdit.key == *c.key)
    {
        rec->editing = true;
        rec->value = mEdit.buf;
        EditBox(c);
        ImGui::PopID();
        return;
    }

    std::string text = "Unavailable";
    if (c.have)
    {
        text = c.sv.text;
        if (c.field.kind == FieldKind::Pointer && !c.field.structName.empty() && c.sv.pointerTarget == 0)
            text = "null";
    }
    rec->value = text;

    // Change tint, like Watch: up / down / neutral, fading over a second.
    if (c.have && c.inst && !c.inst->changedAt.empty())
    {
        float newest = -1.0e9f;
        for (uint32_t i = 0; i < c.size && c.instOff + i < c.inst->changedAt.size(); ++i)
            newest = (std::max)(newest, c.inst->changedAt[c.instOff + i]);
        const float age = (float)mClock - newest;
        if (age >= 0.0f && age < kHighlightSecs)
        {
            ImU32 col = kNeutralCol;
            const FieldKind k = c.field.kind;
            const bool numeric = k != FieldKind::Pointer && k != FieldKind::RGB555;
            if (numeric && c.inst->prev.size() == c.inst->bytes.size())
            {
                StructValue pv;
                if (DecodeField(c.field, &c.inst->prev[c.instOff], pv))
                    col = c.sv.numeric > pv.numeric ? kUpCol : c.sv.numeric < pv.numeric ? kDownCol : kNeutralCol;
            }
            if (ui::IsLight())
                col = col == kUpCol ? IM_COL32(160, 210, 170, 255) : col == kDownCol ? IM_COL32(235, 175, 175, 255)
                                                                                     : IM_COL32(230, 210, 140, 255);
            const int alpha = (int)((1.0f - age / kHighlightSecs) * 140.0f);
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, (col & 0x00FFFFFFu) | ((ImU32)alpha << 24));
        }
    }

    if (c.have && c.sv.hasSwatch)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(p.x, p.y + 2), ImVec2(p.x + h - 4, p.y + h - 2), IM_COL32(c.sv.r, c.sv.g, c.sv.b, 255));
        dl->AddRect(ImVec2(p.x, p.y + 2), ImVec2(p.x + h - 4, p.y + h - 2), IM_COL32(20, 20, 24, 255));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + h);
    }

    if (!c.have)
        ImGui::PushStyleColor(ImGuiCol_Text, ui::Color(ImVec4(0.8f, 0.55f, 0.35f, 1.0f), ImVec4(0.50f, 0.36f, 0.0f, 1)));
    // The Type cell before this one used AlignTextToFramePadding; a Selectable derives its box from
    // CursorPos + CurrLineTextBaseOffset (see CLAUDE.md), so leaving that offset in place pushes it
    // down and the row grows by the frame padding. Centre the label in the box instead.
    ImGui::GetCurrentWindow()->DC.CurrLineTextBaseOffset = 0.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    const std::string label = text + "##v";
    const bool pressed = ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, h));
    ImGui::PopStyleVar();
    if (!c.have) ImGui::PopStyleColor();

    if (ImGui::IsItemHovered() && c.have)
    {
        if (c.sv.isPointer)
        {
            const uint32_t t = c.sv.pointerTarget;
            if (!IsPlausibleSaturnAddress(t))
                ImGui::SetTooltip("Target 0x%08X is outside known Saturn RAM/VRAM ranges.", t);
            else if (!IsReadableAddress(mBe, t))
                ImGui::SetTooltip("Target 0x%08X is a valid Saturn address, but this source does not capture it.", t);
            else if (!c.sv.tooltip.empty())
                ImGui::SetTooltip("%s", c.sv.tooltip.c_str());
        }
        else if (!c.sv.tooltip.empty())
            ImGui::SetTooltip("%s", c.sv.tooltip.c_str());
    }
    if (pressed && ImGui::IsMouseDoubleClicked(0)) BeginEdit(c);
    if (ImGui::BeginPopupContextItem("ctx"))
    {
        LeafMenuBody(c);
        ImGui::EndPopup();
    }
    ImGui::PopID();
}

bool StructPanel::DrawContainerRow(const char* label, const std::string& type, uint32_t addr,
                                   const std::string& key, bool defaultOpen)
{
    RowRecord rec;
    rec.key = key;
    const bool open = BeginRow(label, true, false, defaultOpen, &rec);
    ImGui::PushID(label);
    rec.address = Hex8(addr);
    rec.type = type;
    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rec.address.c_str());
    ImGui::TableSetColumnIndex(2);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(type.c_str());
    ImGui::TableSetColumnIndex(4);
    if (ImGui::SmallButton("...")) ImGui::OpenPopup("rowmenu");
    if (ImGui::BeginPopup("rowmenu")) { ContainerMenuBody(addr); ImGui::EndPopup(); }
    ImGui::PopID();
    if (mRecord && mRows.size() < kMaxRecordedRows) mRows.push_back(rec);
    return open;
}

void StructPanel::DrawMessageRow(const std::string& label, const std::string& tip)
{
    RowRecord rec;
    rec.key = label;
    BeginRow(label.c_str(), false, true, false, &rec);
    if (!tip.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
    if (mRecord && mRows.size() < kMaxRecordedRows) mRows.push_back(rec);
}

void StructPanel::DrawLeafRow(const char* label, const FieldDef& elem, Instance& inst, uint32_t off,
                              const std::string& key, int hops, std::vector<uint32_t>& chain, uint64_t viewId)
{
    LeafCtx c;
    c.key = &key;
    c.label = label;
    c.field = elem;
    c.addr = inst.addr + off;
    c.size = FieldKindSize(elem.kind);
    c.viewId = viewId;
    c.inst = &inst;
    c.instOff = off;
    c.have = inst.Has(off, c.size) && DecodeField(elem, &inst.bytes[off], c.sv) && c.sv.valid;

    const StructDef* target = elem.kind == FieldKind::Pointer && !elem.structName.empty() ? mLib.Find(elem.structName) : nullptr;
    const bool expandable = target && c.have && c.sv.pointerTarget != 0;

    RowRecord rec;
    rec.key = key;
    const bool open = BeginRow(label, expandable, false, false, &rec);
    ImGui::PushID(label);
    rec.address = Hex8(c.addr);
    rec.type = FieldTypeText(elem);
    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rec.address.c_str());
    ImGui::TableSetColumnIndex(2);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(rec.type.c_str());
    ImGui::TableSetColumnIndex(3);
    ValueCell(c, &rec);
    ImGui::TableSetColumnIndex(4);
    if (ImGui::SmallButton("...")) ImGui::OpenPopup("rowmenu");
    rec.menuMin = ImGui::GetItemRectMin();
    rec.menuMax = ImGui::GetItemRectMax();
    if (ImGui::BeginPopup("rowmenu")) { LeafMenuBody(c); ImGui::EndPopup(); }
    ImGui::PopID();
    if (mRecord && mRows.size() < kMaxRecordedRows) mRows.push_back(rec);

    if (!expandable || !open) return;

    const uint32_t t = c.sv.pointerTarget;
    if (hops >= kMaxPointerHops)
        DrawMessageRow("(depth limit " + std::to_string(kMaxPointerHops) + ")", "Pointers are followed at most 8 levels deep.");
    else if (std::find(chain.begin(), chain.end(), Canon(t)) != chain.end())
        DrawMessageRow("(shown above)", "This address is already displayed higher up in this chain.");
    else
    {
        const StructLayout* tl = mLib.Layout(elem.structName);
        const std::string tkey = key + "->";
        Instance& ti = Acquire(tkey, elem.structName, t, tl ? tl->size : 0);
        Refresh(ti);
        if (!tl || !ti.anyValid)
        {
            std::string tip;
            if (!IsPlausibleSaturnAddress(t))
                tip = "Target " + Hex8(t) + " is outside known Saturn RAM/VRAM ranges.";
            else
                tip = "Target " + Hex8(t) + " is a valid Saturn address, but this source does not capture it.";
            DrawMessageRow("Unavailable", tip);
        }
        else
        {
            chain.push_back(Canon(t));
            DrawStructBody(*target, *tl, ti, 0, tkey, hops + 1, chain, viewId);
            chain.pop_back();
        }
    }
    ImGui::TreePop();
}

void StructPanel::DrawStructBody(const StructDef& def, const StructLayout& lay, Instance& inst, uint32_t base,
                                 const std::string& key, int hops, std::vector<uint32_t>& chain, uint64_t viewId)
{
    for (size_t i = 0; i < def.fields.size(); ++i)
    {
        const FieldDef& f = def.fields[i];
        const FieldLayout& fl = lay.fields[i];
        const uint32_t off = base + fl.offset;
        const std::string fkey = key + "." + f.name;
        FieldDef elem = f;
        elem.count = 1;
        elem.isArray = false;

        if (f.kind == FieldKind::Struct)
        {
            const StructDef* sd = mLib.Find(f.structName);
            const StructLayout* sl = mLib.Layout(f.structName);
            if (!sd || !sl) continue;
            if (!f.isArray)
            {
                if (DrawContainerRow(f.name.c_str(), f.structName, inst.addr + off, fkey, false))
                {
                    DrawStructBody(*sd, *sl, inst, off, fkey, hops, chain, viewId);
                    ImGui::TreePop();
                }
            }
            else if (DrawContainerRow(f.name.c_str(), FieldTypeText(f), inst.addr + off, fkey, false))
            {
                const uint32_t n = (std::min)(f.count, kMaxTreeElements);
                for (uint32_t k = 0; k < n; ++k)
                {
                    const std::string label = "[" + std::to_string(k) + "]";
                    const std::string ekey = fkey + label;
                    if (DrawContainerRow(label.c_str(), f.structName, inst.addr + off + k * fl.elemSize, ekey, false))
                    {
                        DrawStructBody(*sd, *sl, inst, off + k * fl.elemSize, ekey, hops, chain, viewId);
                        ImGui::TreePop();
                    }
                }
                if (f.count > n) DrawMessageRow("... " + std::to_string(f.count - n) + " more elements not shown", "");
                ImGui::TreePop();
            }
            continue;
        }

        if (!f.isArray)
        {
            DrawLeafRow(f.name.c_str(), elem, inst, off, fkey, hops, chain, viewId);
            continue;
        }
        if (!DrawContainerRow(f.name.c_str(), FieldTypeText(f), inst.addr + off, fkey, false)) continue;
        const bool variableRows = f.kind == FieldKind::Pointer && !f.structName.empty();
        if (variableRows)
        {
            const uint32_t n = (std::min)(f.count, kMaxTreeElements);
            for (uint32_t k = 0; k < n; ++k)
            {
                const std::string label = "[" + std::to_string(k) + "]";
                DrawLeafRow(label.c_str(), elem, inst, off + k * fl.elemSize, fkey + label, hops, chain, viewId);
            }
            if (f.count > n) DrawMessageRow("... " + std::to_string(f.count - n) + " more elements not shown", "");
        }
        else
        {
            ImGuiListClipper clipper;
            clipper.Begin((int)f.count);
            while (clipper.Step())
                for (int k = clipper.DisplayStart; k < clipper.DisplayEnd; ++k)
                {
                    const std::string label = "[" + std::to_string(k) + "]";
                    DrawLeafRow(label.c_str(), elem, inst, off + (uint32_t)k * fl.elemSize, fkey + label, hops, chain, viewId);
                }
        }
        ImGui::TreePop();
    }
}

void StructPanel::DrawElementTable(const StructView& v, ViewUi& ui, const StructDef& def, uint32_t elemSize)
{
    (void)def;
    if (mLeafCacheEpoch != mLibEpoch) { mLeafCache.clear(); mLeafTruncated.clear(); mLeafCacheEpoch = mLibEpoch; }
    std::vector<LeafField>& leaves = mLeafCache[v.structName];
    if (leaves.empty())
    {
        bool trunc = false;
        mLib.FlattenLeaves(v.structName, kMaxTableLeaves, leaves, trunc);
        mLeafTruncated[v.structName] = trunc;
    }
    if (mLeafTruncated[v.structName])
        ImGui::TextDisabled("Showing the first %d fields of each element; use the Tree view for the rest.", (int)kMaxTableLeaves);

    const ImGuiStyle& st = ImGui::GetStyle();
    const float rowH = ImGui::GetFrameHeight() + st.CellPadding.y * 2.0f;
    const int shown = (int)(std::min)(v.count, 12u);
    const float height = (shown + 1) * rowH + st.ScrollbarSize + 4.0f;
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollX |
                                  ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
    if (!ImGui::BeginTable("##elems", 2 + (int)leaves.size(), flags, ImVec2(0.0f, height))) return;
    ImGui::TableSetupScrollFreeze(2, 1);          // index and address stay while the leaves scroll
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 56.0f);
    ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 92.0f);
    for (const LeafField& lf : leaves)
        ImGui::TableSetupColumn(lf.path.c_str(), ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin((int)v.count);
    while (clipper.Step())
    {
        // One batched read for the rows that are actually on screen and stale.
        std::vector<Instance*> stale;
        std::vector<Instance*> rows;
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            Instance& in = Acquire("v" + std::to_string(v.id) + "#" + std::to_string(i), v.structName,
                                   ui.addr + (uint32_t)i * elemSize, elemSize);
            rows.push_back(&in);
            if (in.epoch != mEpoch && mBe->Connected()) stale.push_back(&in);
        }
        ReadInstances(stale);
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            Instance& in = *rows[(size_t)(i - clipper.DisplayStart)];
            const std::string ekey = "v" + std::to_string(v.id) + "[" + std::to_string(i) + "]";
            ImGui::TableNextRow(ImGuiTableRowFlags_None, ImGui::GetFrameHeight());
            ImGui::PushID(i);
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%d", i);
            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            ImGui::Text("0x%08X", in.addr);
            for (size_t li = 0; li < leaves.size(); ++li)
            {
                const LeafField& lf = leaves[li];
                ImGui::TableSetColumnIndex(2 + (int)li);
                ImGui::PushID((int)li);
                LeafCtx c;
                const std::string key = ekey + "." + lf.path;
                c.key = &key;
                c.label = lf.path.c_str();
                c.field = lf.field;
                c.field.name = lf.path;
                c.addr = in.addr + lf.offset;
                c.size = FieldKindSize(lf.field.kind);
                c.viewId = v.id;
                c.inst = &in;
                c.instOff = lf.offset;
                c.have = in.Has(lf.offset, c.size) && DecodeField(lf.field, &in.bytes[lf.offset], c.sv) && c.sv.valid;
                RowRecord rec;
                rec.key = key;
                rec.label = lf.path;
                rec.rowY = ImGui::GetCursorScreenPos().y;
                rec.address = Hex8(c.addr);
                rec.type = FieldTypeText(lf.field);
                ValueCell(c, &rec);
                if (mRecord && mRows.size() < kMaxRecordedRows) mRows.push_back(rec);
                ImGui::PopID();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

void StructPanel::DrawBrokenRow(const BrokenView& b)
{
    RowRecord rec;
    rec.key = "broken" + std::to_string(b.id);
    rec.broken = true;
    BeginRow(b.label.c_str(), false, true, false, &rec);
    ImGui::PushID((int)(b.id & 0x7FFFFFFF));
    ImGui::TableSetColumnIndex(1);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(b.expression.c_str());
    rec.address = b.expression;
    ImGui::TableSetColumnIndex(2);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", b.structName.c_str());
    rec.type = b.structName;
    ImGui::TableSetColumnIndex(3);
    ImGui::AlignTextToFramePadding();
    rec.value = "Broken view: " + b.reason;
    ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", rec.value.c_str());
    ImGui::TableSetColumnIndex(4);
    bool remove = false;
    if (ImGui::SmallButton("...")) ImGui::OpenPopup("rowmenu");
    if (ImGui::BeginPopup("rowmenu"))
    {
        // Repair by pointing the view at a struct that exists.
        if (ImGui::BeginMenu("Use struct", !mLib.Structs().empty()))
        {
            for (const StructDef& s : mLib.Structs())
                if (ImGui::MenuItem(s.name.c_str()))
                {
                    std::string err;
                    if (mLib.AddView(b.label, s.name, b.count, b.expression, &err))
                    {
                        mDirty = true;
                        remove = true;
                    }
                    else
                        SetNotice("Cannot use " + s.name + ": " + err, true);
                }
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Delete View")) remove = true;
        ImGui::EndPopup();
    }
    ImGui::PopID();
    if (mRecord && mRows.size() < kMaxRecordedRows) mRows.push_back(rec);
    if (remove)
    {
        mBroken.erase(std::remove_if(mBroken.begin(), mBroken.end(),
                                     [&](const BrokenView& x) { return x.id == b.id; }), mBroken.end());
        mDirty = true;
    }
}

void StructPanel::DrawViewRoot(const StructView& v)
{
    ViewUi& ui = UiFor(v);
    const StructDef* def = mLib.Find(v.structName);
    const StructLayout* lay = mLib.Layout(v.structName);
    ResolveView(v, ui);
    const bool tableMode = v.count > 1 && (ui.tableMode < 0 ? v.count > 16 : ui.tableMode == 1);

    RowRecord rec;
    rec.key = "v" + std::to_string(v.id);
    EnsureViewsTable();                           // begun outside the PushID scope it will be ended outside of
    ImGui::PushID((int)v.id);
    const bool open = BeginRow("##root", true, tableMode, true, &rec);
    rec.label = v.label;

    // Field column: the label, editable in place.
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##label", ui.label, sizeof ui.label, ImGuiInputTextFlags_CharsNoBlank);
    ui.labelActive = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        std::string err;
        if (mLib.UpdateView(v.id, ui.label, v.structName, v.count, v.expression, &err)) mDirty = true;
        else { SetNotice("Label: " + err, true); CopyStr(ui.label, sizeof ui.label, v.label); }
    }

    // Address column: the expression.
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##expr", ui.expr, sizeof ui.expr, ImGuiInputTextFlags_CharsNoBlank);
    ui.exprActive = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        std::string err;
        const std::string norm = NormalizeExpression(ui.expr);
        if (mLib.UpdateView(v.id, v.label, v.structName, v.count, norm, &err)) { mDirty = true; ui.resolvedExpr.clear(); }
        else { SetNotice("Address: " + err, true); CopyStr(ui.expr, sizeof ui.expr, v.expression); }
    }
    rec.address = v.expression;

    // Type column: the struct, by combo.
    ImGui::TableSetColumnIndex(2);
    {
        std::vector<const char*> names;
        for (const StructDef& s : mLib.Structs()) names.push_back(s.name.c_str());
        const float w = (std::min)(ComboWidth(names.data(), (int)names.size()), ImGui::GetContentRegionAvail().x);
        ImGui::SetNextItemWidth((std::max)(w, 40.0f));
        if (ImGui::BeginCombo("##struct", v.structName.c_str()))
        {
            for (const StructDef& s : mLib.Structs())
                if (ImGui::Selectable(s.name.c_str(), s.name == v.structName) && s.name != v.structName)
                {
                    std::string err;
                    if (mLib.UpdateView(v.id, v.label, s.name, v.count, v.expression, &err)) { mDirty = true; ++mEpoch; }
                    else SetNotice("Struct: " + err, true);
                }
            ImGui::EndCombo();
        }
        rec.type = v.structName;
    }

    // Value column: element count and, for an array, the Tree/Table toggle.
    ImGui::TableSetColumnIndex(3);
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputInt("##count", &ui.count, 0, 0);
    if (ImGui::IsItemDeactivatedAfterEdit())
    {
        std::string err;
        const int n = (std::max)(1, ui.count);
        if (mLib.UpdateView(v.id, v.label, v.structName, (uint32_t)n, v.expression, &err)) mDirty = true;
        else { SetNotice("Count: " + err, true); ui.count = (int)v.count; }
    }
    if (v.count > 1)
    {
        ImGui::SameLine();
        if (ImGui::SmallButton(tableMode ? "Table" : "Tree")) ui.tableMode = tableMode ? 0 : 1;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Switch between the tree and the one-element-per-row table.");
    }
    rec.value = std::to_string(v.count);

    ImGui::TableSetColumnIndex(4);
    bool deleteMe = false;
    if (ImGui::SmallButton("...")) ImGui::OpenPopup("rowmenu");
    rec.menuMin = ImGui::GetItemRectMin();
    rec.menuMax = ImGui::GetItemRectMax();
    if (ImGui::BeginPopup("rowmenu"))
    {
        ContainerMenuBody(ui.addr);
        if (v.count > 1 && ImGui::MenuItem(tableMode ? "Show as Tree" : "Show as Table")) ui.tableMode = tableMode ? 0 : 1;
        if (ImGui::MenuItem("Delete View")) deleteMe = true;
        ImGui::EndPopup();
    }
    if (mRecord && mRows.size() < kMaxRecordedRows) mRows.push_back(rec);

    bool elementTable = false;
    if (open && def && lay)
    {
        if (!ui.resolved)
            DrawMessageRow("Invalid address", ui.error);
        else if (lay->size == 0)
            DrawMessageRow("Empty struct", "");
        else if (v.count == 1)
        {
            Instance& inst = Acquire(rec.key, v.structName, ui.addr, lay->size);
            Refresh(inst);
            std::vector<uint32_t> chain{ Canon(ui.addr) };
            DrawStructBody(*def, *lay, inst, 0, rec.key, 0, chain, v.id);
        }
        else if (tableMode)
        {
            elementTable = true;                  // drawn after the PushID scope closes
        }
        else
        {
            const uint32_t n = (std::min)(v.count, kMaxTreeElements);
            for (uint32_t k = 0; k < n; ++k)
            {
                const std::string label = "[" + std::to_string(k) + "]";
                const std::string ekey = rec.key + label;
                const uint32_t addr = ui.addr + k * lay->size;
                if (DrawContainerRow(label.c_str(), v.structName, addr, ekey, false))
                {
                    Instance& inst = Acquire(ekey, v.structName, addr, lay->size);
                    Refresh(inst);
                    std::vector<uint32_t> chain{ Canon(addr) };
                    DrawStructBody(*def, *lay, inst, 0, ekey, 0, chain, v.id);
                    ImGui::TreePop();
                }
            }
            if (v.count > n) DrawMessageRow("... " + std::to_string(v.count - n) + " more elements; use the Table view", "");
        }
    }
    if (open && !tableMode) ImGui::TreePop();
    ImGui::PopID();
    if (elementTable)
    {
        EndViewsTable();                          // the element table is its own scrolling table
        ImGui::PushID((int)v.id);
        DrawElementTable(v, ui, *def, lay->size);
        ImGui::PopID();
    }

    if (deleteMe)
    {
        mLib.RemoveView(v.id);
        mViewUi.erase(v.id);
        mDirty = true;
        if (mEdit.active && mEdit.viewId == v.id) CancelEdit();
    }
}

void StructPanel::DrawViews()
{
    mInTable = false;
    mHeaderDone = false;
    // A copy: rows edit the library (label, count, delete) while this loop runs.
    const std::vector<StructView> views = mLib.Views();
    EnsureViewsTable();
    for (const StructView& v : views) DrawViewRoot(v);
    const std::vector<BrokenView> broken = mBroken;
    for (const BrokenView& b : broken) DrawBrokenRow(b);
    EndViewsTable();
}

void StructPanel::DrawDefinitions()
{
    const float lineH = ImGui::GetTextLineHeightWithSpacing();
    const float btnH = ImGui::GetFrameHeightWithSpacing();
    const size_t nerr = (std::min)(mDefErrors.size(), (size_t)5);
    const float errH = (float)(nerr + 1) * lineH + 8.0f;

    ImGui::BeginChild("##deflist", ImVec2(220.0f, 0.0f), true);
    ImGui::TextDisabled("Structs");
    for (size_t i = 0; i < mLib.Structs().size(); ++i)
    {
        const StructDef& s = mLib.Structs()[i];
        char buf[160];
        std::snprintf(buf, sizeof buf, "%s  (0x%X)", s.name.c_str(), s.size);
        if (ImGui::Selectable(buf, (int)i == mSelectedStruct)) mSelectedStruct = (int)i;
    }
    if (mSelectedStruct >= 0 && (size_t)mSelectedStruct < mLib.Structs().size())
    {
        const StructDef& s = mLib.Structs()[(size_t)mSelectedStruct];
        const StructLayout* l = mLib.Layout(s.name);
        ImGui::Separator();
        if (l)
            for (size_t i = 0; i < s.fields.size(); ++i)
                ImGui::TextDisabled("%03X %s %s", l->fields[i].offset, FieldTypeText(s.fields[i]).c_str(),
                                    s.fields[i].name.c_str());
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginGroup();
    const float editorH = (std::max)(80.0f, ImGui::GetContentRegionAvail().y - errH - btnH - 8.0f);
    if (ImGui::InputTextMultiline("##defs", const_cast<char*>(mDefText.c_str()), mDefText.capacity() + 1,
                                  ImVec2(-FLT_MIN, editorH), ImGuiInputTextFlags_CallbackResize, StringResize, &mDefText))
        mDefDirty = true;
    if (ImGui::Button("Apply"))
    {
        if (ApplyDefinitions(mDefText))
            SetNotice("Applied " + std::to_string(mLib.Structs().size()) + " struct definitions.", false, 4.0f);
        else
            SetNotice("Definitions not applied: " + std::to_string(mDefErrors.size()) + " error(s). The library is unchanged.", true);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Replaces the struct definitions if the text parses; views are kept.\nView lines in the text are added as views.");
    ImGui::SameLine();
    if (ImGui::Button("Revert"))
    {
        mDefText = mLoadFailed ? mHeldText : DefsOnlyText(mLib);
        mDefDirty = mLoadFailed;
        mDefErrors.clear();
    }
    ImGui::BeginChild("##errs", ImVec2(0.0f, errH), true);
    if (mDefErrors.empty()) ImGui::TextDisabled("No errors");
    for (const ParseError& e : mDefErrors)
    {
        if (e.line > 0)
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "Line %d: %s", e.line, e.message.c_str());
        else
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f), "%s", e.message.c_str());
    }
    ImGui::EndChild();
    ImGui::EndGroup();
}

void StructPanel::DrawToolbar(IPlatform& platform)
{
    const bool noStructs = mLib.Structs().empty();
    ImGui::BeginDisabled(noStructs || mShowDefs);
    if (ImGui::Button("+ View"))
        AddDefaultView("view" + std::to_string(mLib.Views().size() + 1), "0x06000000", mLib.Structs().front().name);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(noStructs ? "Define a struct first (Definitions...)." : "Add a view of a struct at an address.");
    ImGui::SameLine();
    if (ImGui::Button(mShowDefs ? "Views" : "Definitions..."))
    {
        mShowDefs = !mShowDefs;
        if (mShowDefs && !mDefDirty && !mLoadFailed) mDefText = DefsOnlyText(mLib);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(mLoadFailed);
    if (ImGui::Button("Import")) DoImport(platform);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(mLoadFailed ? "Fix and Apply the definitions text first."
                                      : "Load structs and views from a text file. Replaces ALL current structs and views.");
    ImGui::SameLine();
    if (ImGui::Button("Export")) DoExport(platform);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Save the structs and views as a text file.");
    ImGui::SameLine();
    if (!mBe->Connected()) ImGui::TextDisabled("(disconnected)");
    else ImGui::TextDisabled("%d views", (int)mLib.Views().size());

    if (mLoadFailed)
        ImGui::TextColored(ui::Color(ImVec4(0.9f, 0.45f, 0.4f, 1), ImVec4(0.65f, 0.11f, 0.14f, 1)), "%s", mLoadError.c_str());
    if (mEdit.active)
    {
        // On the toolbar line so entering edit mode never shifts the rows (a click target) down.
        ImGui::SameLine();
        ImGui::TextDisabled("Editing %s: %s", Hex8(mEdit.addr).c_str(), WriteDestText(mBe->WriteDestination(mEdit.addr)));
    }
    if (!mNotice.empty())
    {
        if (mNoticeError) ImGui::TextColored(ui::Color(ImVec4(0.9f, 0.45f, 0.4f, 1), ImVec4(0.65f, 0.11f, 0.14f, 1)), "%s", mNotice.c_str());
        else ImGui::TextWrapped("%s", mNotice.c_str());
    }
}

void StructPanel::Draw(IMemoryBackend& backend, IExpressionResolver& resolver, IPlatform& platform,
                       BreakpointManager& bps, float dt, uint32_t* outHexJump, WatchRequest* outAddWatch)
{
    mBe = &backend;
    mResolver = &resolver;
    mBps = &bps;
    mHexOut = outHexJump;
    mWatchOut = outAddWatch;
    ++mFrame;
    mClock += dt;

    const uint64_t src = backend.SourceId();
    if (src != mLastSource)
    {
        // The bytes behind every address are different data now: nothing cached or in flight is valid.
        mLastSource = src;
        mInstances.clear();
        ++mEpoch;
        if (mEdit.active)
        {
            CancelEdit();
            SetNotice("Edit dropped: the memory source changed.", true);
        }
    }
    mRefreshAccum += dt;
    const float period = mRefreshHz > 0.0f ? 1.0f / mRefreshHz : 0.1f;
    if (mRefreshAccum >= period) { mRefreshAccum = 0.0f; ++mEpoch; }
    if (mNoticeTime > 0.0f) { mNoticeTime -= dt; if (mNoticeTime <= 0.0f) mNotice.clear(); }

    if (mFocusWindow) { ImGui::SetNextWindowFocus(); mFocusWindow = false; }
    mRows.clear();
    mMenuItems.clear();
    if (ImGui::Begin("Structs"))
    {
        DrawToolbar(platform);
        if (mShowDefs) DrawDefinitions();
        else DrawViews();
    }
    ImGui::End();

    if ((mFrame & 63) == 0)
        for (auto it = mInstances.begin(); it != mInstances.end();)
            it = (it->second.lastUsed + 240 < mFrame) ? mInstances.erase(it) : std::next(it);

    mBe = nullptr; mResolver = nullptr; mBps = nullptr; mHexOut = nullptr; mWatchOut = nullptr;
}

}  // namespace sfe
