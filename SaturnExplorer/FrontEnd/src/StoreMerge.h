// StoreMerge -- what the save of a file the user also edits by hand, or that another instance writes, must do (A9-19, A9-20,
// A9-21): apply THIS session's changes (adds, edits, deletes since the file was loaded) on top of what the file holds now, and
// touch nothing else in it. The names, the notes and the function starts used to be written whole from memory, so a second
// instance that changed nothing undid the first one's work at its exit, a line the loader skipped or a comment was erased, and a
// file the loader refused was replaced by what the session held (nothing).
//
// The pieces: the lines of the file as it is now are kept as written; an entry the session deleted is dropped; an entry it
// edited is rewritten in place; an entry it added goes at the end. Nothing changed means nothing is written.
//
// Header only and free of the application's other modules, so every store can use it.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "AddressNameLine.h"
#include "FileRead.h"
#include "FileWrite.h"

namespace sfe
{

using AddressMap = std::map<uint32_t, std::string>;

// True when 'cur' differs from 'base' (a different set of addresses, or a different value at one).
inline bool StoreChanged(const AddressMap& base, const AddressMap& cur) { return base != cur; }

// The file's text with this session's changes applied. 'classify' says whether a line is an entry and at which address (the
// store's own reading of its lines); 'format' writes one entry; 'header' is the first line of a file that has no lines yet.
inline std::string MergeIntoText(const std::string& disk, const AddressMap& base, const AddressMap& cur,
                                 const std::function<bool(const std::string&, uint32_t&)>& classify,
                                 const std::function<std::string(uint32_t, const std::string&)>& format, const std::string& header)
{
    std::set<uint32_t> deleted, set;
    for (const auto& kv : base)
        if (!cur.count(kv.first)) deleted.insert(kv.first);
    for (const auto& kv : cur)
    {
        auto it = base.find(kv.first);
        if (it == base.end() || it->second != kv.second) set.insert(kv.first);
    }
    const std::vector<std::string> lines = SplitLines(disk);
    std::string out;
    if (lines.empty()) out += header;
    std::set<uint32_t> written;
    for (const std::string& line : lines)
    {
        uint32_t a = 0;
        if (classify(line, a))
        {
            if (deleted.count(a)) continue;
            if (set.count(a))
            {
                if (written.insert(a).second) out += format(a, cur.at(a));   // the first line of the address; a repeat is dropped
                continue;
            }
        }
        out += line;
        out += '\n';
    }
    for (uint32_t a : set)
        if (written.insert(a).second) out += format(a, cur.at(a));
    return out;
}

// The one save of a store: refused for a file the loader refused; nothing written when nothing changed; otherwise under the
// file's lock the file is read again, this session's changes are applied and the result is written atomically. On success the
// session's view becomes the new base. False, with 'error', and the file left as it was, on every refusal.
inline bool SaveStore(const std::string& path, const char* what, const std::string& refusedWhy, size_t maxBytes, AddressMap& base,
                      const AddressMap& cur, const std::function<bool(const std::string&, uint32_t&)>& classify,
                      const std::function<std::string(uint32_t, const std::string&)>& format, const std::string& header,
                      std::string* error)
{
    auto fail = [&](const std::string& why) { if (error) *error = why; return false; };
    if (!StoreChanged(base, cur)) return true;   // an instance that changed nothing writes nothing (A9-20)
    if (!refusedWhy.empty())
        return fail(std::string(what) + ": not saved, " + refusedWhy + " when it was loaded, and a save would replace it; "
                    "this session's changes to it are kept in memory only");
    // The lock file lives beside the data: a lock on the data file itself would be dropped by the rename that replaces it.
    FileLock lock(path + ".lock");
    std::string disk;
    switch (ReadFileCapped(path, maxBytes, disk))
    {
    case CappedRead::Ok: break;
    case CappedRead::Missing: disk.clear(); break;
    case CappedRead::TooLarge:
        return fail(std::string(what) + ": not saved, " + path + " is now larger than " + std::to_string(maxBytes >> 20) + " MiB and would not be read at the next start; the file is left as it is");
    case CappedRead::NotRegular:
        return fail(std::string(what) + ": not saved, " + path + " is not a regular file; it is left as it is");
    case CappedRead::Unreadable:
        return fail(std::string(what) + ": not saved, " + path + " cannot be read; it is left as it is");
    }
    const std::string text = MergeIntoText(disk, base, cur, classify, format, header);
    if (text.size() > maxBytes)
        return fail(std::string(what) + ": not saved, it would be " + std::to_string(text.size()) + " bytes, over the " + std::to_string(maxBytes >> 20) +
                    " MiB that is read back at the next start, which would refuse " + path + "; trim it or move entries to another file");
    std::string why;
    if (!WriteFileAtomically(path, text.data(), text.size(), why)) return fail(path + ": " + why);
    base = cur;
    return true;
}

}  // namespace sfe
