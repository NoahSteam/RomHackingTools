#include "PatchLibrary.h"

#include <cstdio>
#include <fstream>
#include <sstream>

namespace sfe
{

namespace
{
// lower-case hex of a byte buffer.
std::string ToHex(const std::vector<uint8_t>& b)
{
    static const char* k = "0123456789abcdef";
    std::string s;
    s.reserve(b.size() * 2);
    for (uint8_t v : b) { s.push_back(k[v >> 4]); s.push_back(k[v & 0xF]); }
    return s;
}

int HexNib(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// Parse an even-length hex string into bytes; returns false on any bad/odd input.
bool FromHex(const std::string& s, std::vector<uint8_t>& out)
{
    if (s.size() % 2 != 0) return false;
    out.clear();
    out.reserve(s.size() / 2);
    for (size_t i = 0; i < s.size(); i += 2)
    {
        const int hi = HexNib(s[i]), lo = HexNib(s[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

// Escape a string for embedding inside a Python double-quoted literal.
std::string PyStr(const std::string& s)
{
    std::string o = "\"";
    for (char c : s)
    {
        if (c == '\\' || c == '"') { o.push_back('\\'); o.push_back(c); }
        else if (c == '\n') o += "\\n";
        else if (c == '\r') o += "\\r";
        else o.push_back(c);
    }
    o.push_back('"');
    return o;
}
}  // namespace

void PatchLibrary::AddOrUpdate(const PatchLocation& loc)
{
    for (PatchLocation& e : mEntries)
    {
        if (e.file == loc.file && e.fileOffset == loc.fileOffset)
        {
            // Same disc location: update the mapping in place (keeps the list stable).
            const bool same = e.label == loc.label && e.cpuAddr == loc.cpuAddr &&
                              e.length == loc.length && e.expected == loc.expected;
            if (!same) { e = loc; mDirty = true; }
            return;
        }
    }
    mEntries.push_back(loc);
    mDirty = true;
}

void PatchLibrary::RemoveAt(size_t i)
{
    if (i >= mEntries.size()) return;
    mEntries.erase(mEntries.begin() + static_cast<std::ptrdiff_t>(i));
    mDirty = true;
}

void PatchLibrary::Clear()
{
    if (mEntries.empty()) return;
    mEntries.clear();
    mDirty = true;
}

// Text format (tab-separated so spaces in file/label are safe; no field may contain a tab):
//   SEPATCH 1
//   <addrHex>\t<length>\t<offset>\t<expectedHex>\t<file>\t<label>
std::string PatchLibrary::Serialize() const
{
    std::ostringstream os;
    os << "SEPATCH 1\n";
    for (const PatchLocation& e : mEntries)
    {
        char head[64];
        std::snprintf(head, sizeof(head), "%08x\t%u\t%llu\t", e.cpuAddr, e.length,
                      static_cast<unsigned long long>(e.fileOffset));
        os << head << ToHex(e.expected) << '\t' << e.file << '\t' << e.label << '\n';
    }
    return os.str();
}

bool PatchLibrary::Deserialize(const std::string& text)
{
    std::vector<PatchLocation> parsed;
    std::istringstream is(text);
    std::string line;
    if (!std::getline(is, line)) return false;
    if (line.rfind("SEPATCH", 0) != 0) return false;   // header required

    while (std::getline(is, line))
    {
        if (line.empty()) continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        // Split into 6 tab-separated fields; the last (label) may itself be empty.
        std::string field[6];
        size_t start = 0;
        int f = 0;
        for (; f < 5; ++f)
        {
            const size_t tab = line.find('\t', start);
            if (tab == std::string::npos) break;
            field[f] = line.substr(start, tab - start);
            start = tab + 1;
        }
        if (f != 5) continue;            // malformed line: skip
        field[5] = line.substr(start);   // label = remainder

        PatchLocation e;
        e.cpuAddr = static_cast<uint32_t>(std::strtoul(field[0].c_str(), nullptr, 16));
        e.length = static_cast<uint32_t>(std::strtoul(field[1].c_str(), nullptr, 10));
        e.fileOffset = static_cast<uint64_t>(std::strtoull(field[2].c_str(), nullptr, 10));
        if (!FromHex(field[3], e.expected)) continue;
        e.file = field[4];
        e.label = field[5];
        parsed.push_back(std::move(e));
    }
    mEntries = std::move(parsed);
    mDirty = false;
    return true;
}

bool PatchLibrary::SaveProject(const std::string& path) const
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    const std::string text = Serialize();
    f.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!f) return false;
    const_cast<PatchLibrary*>(this)->mDirty = false;
    return true;
}

bool PatchLibrary::LoadProject(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    return Deserialize(ss.str());
}

std::string PatchLibrary::EmitPython(
    const std::function<bool(uint32_t, uint32_t, std::vector<uint8_t>&)>& readMem,
    std::vector<PatchOutcome>& outcomes) const
{
    outcomes.clear();
    outcomes.reserve(mEntries.size());

    std::ostringstream body;   // the PATCHES table entries (changed only)
    for (const PatchLocation& e : mEntries)
    {
        PatchOutcome oc;
        oc.location = &e;
        if (!readMem || !readMem(e.cpuAddr, e.length, oc.current) || oc.current.size() != e.length)
        {
            oc.readFailed = true;
            outcomes.push_back(std::move(oc));
            continue;
        }
        oc.changed = (oc.current != e.expected);
        if (oc.changed)
        {
            body << "    (" << PyStr(e.file) << ", "
                 << static_cast<unsigned long long>(e.fileOffset) << ", "
                 << PyStr(ToHex(oc.current)) << "),\n";
        }
        outcomes.push_back(std::move(oc));
    }

    std::ostringstream os;
    os <<
        "#!/usr/bin/env python3\n"
        "# Generated by Saturn Explorer - patches game data files with edited memory.\n"
        "# Do not hand-edit; regenerate from the app (Patch > Apply changes to disc).\n"
        "import os, sys\n\n"
        "BASE = os.path.dirname(os.path.abspath(__file__))\n\n"
        "# (relative_path, byte_offset, hex_bytes)\n"
        "PATCHES = [\n" << body.str() << "]\n\n"
        "def main():\n"
        "    ok = 0\n"
        "    for rel, off, hexb in PATCHES:\n"
        "        data = bytes.fromhex(hexb)\n"
        "        path = os.path.join(BASE, *rel.split('/'))\n"
        "        try:\n"
        "            with open(path, 'r+b') as f:\n"
        "                f.seek(off)\n"
        "                f.write(data)\n"
        "            print('patched %s @ %d (%d bytes)' % (rel, off, len(data)))\n"
        "            ok += 1\n"
        "        except OSError as e:\n"
        "            print('FAILED %s: %s' % (rel, e), file=sys.stderr)\n"
        "    print('%d file(s) patched' % ok)\n"
        "    return 0\n\n"
        "if __name__ == '__main__':\n"
        "    sys.exit(main())\n";
    return os.str();
}

}  // namespace sfe
