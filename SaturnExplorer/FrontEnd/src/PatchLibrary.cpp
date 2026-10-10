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

// Tabs and newlines in a label are replaced rather than refused: the label is cosmetic, and
// losing the exact whitespace of a generated string is not worth failing an accepted match over.
std::string SanitizeLabel(const std::string& label)
{
    std::string out = label;
    for (char& c : out)
        if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    return out;
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

// Parse an entire field as an unsigned number in 'base', no larger than 'max'. strtoul alone
// stops at the first bad character, wraps on overflow and accepts a sign, which turns
// "1junk" or "-1" into a plausible-looking mapping instead of a refused record.
bool ParseUnsigned(const std::string& s, int base, uint64_t max, uint64_t& out)
{
    if (s.empty()) return false;
    uint64_t v = 0;
    for (char c : s)
    {
        const int d = HexNib(c);
        if (d < 0 || d >= base) return false;
        if (v > (max - uint64_t(d)) / uint64_t(base)) return false;
        v = v * uint64_t(base) + uint64_t(d);
    }
    out = v;
    return true;
}
}  // namespace

bool PatchLocationValid(const PatchLocation& loc, std::string* why)
{
    auto fail = [&](const char* m) { if (why) *why = m; return false; };
    if (loc.length == 0) return fail("patch location has zero length");
    if (loc.expected.size() != loc.length)
        return fail("patch baseline is not the same size as the mapped range");
    if (loc.file.empty()) return fail("patch location has no file");
    if (loc.file.find_first_of("\t\r\n") != std::string::npos)
        return fail("patch file path contains a tab or newline, which the project format cannot "
                    "represent");
    // No filesystem path can hold a NUL, and Python refuses to compile a source that does, so
    // the generated script would not run at all.
    if (loc.file.find('\0') != std::string::npos)
        return fail("patch file path contains a NUL byte");
    if (uint64_t(loc.cpuAddr) + loc.length > 0x100000000ull)
        return fail("patch range runs past the end of the address space");
    return true;
}

bool RelativePathUnder(const std::string& root, const std::string& path, std::string& rel)
{
    std::string d = root, p = path;
    for (char& c : d) if (c == '\\') c = '/';
    for (char& c : p) if (c == '\\') c = '/';
    while (!d.empty() && d.back() == '/') d.pop_back();
    if (d.empty() || p.size() <= d.size() + 1 || p.compare(0, d.size(), d) != 0 ||
        p[d.size()] != '/')
        return false;
    rel = p.substr(d.size() + 1);
    // Every component must be a real name: the script refuses "..", "." and empty ones too.
    size_t start = 0;
    for (;;)
    {
        const size_t slash = rel.find('/', start);
        const std::string part = rel.substr(start, slash == std::string::npos ? std::string::npos
                                                                               : slash - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return true;
}

bool PatchLibrary::AddOrUpdate(const PatchLocation& loc_, std::string* error)
{
    if (!PatchLocationValid(loc_, error)) return false;
    PatchLocation loc = loc_;
    loc.label = SanitizeLabel(loc.label);
    for (PatchLocation& e : mEntries)
    {
        if (e.file == loc.file && e.fileOffset == loc.fileOffset)
        {
            // Same disc location: update the mapping in place (keeps the list stable).
            const bool same = e.label == loc.label && e.cpuAddr == loc.cpuAddr &&
                              e.length == loc.length && e.expected == loc.expected;
            if (!same) { e = loc; mDirty = true; }
            return true;
        }
    }
    mEntries.push_back(loc);
    mDirty = true;
    return true;
}

void PatchLibrary::RemoveAt(size_t i)
{
    if (i >= mEntries.size()) return;
    mEntries.erase(mEntries.begin() + static_cast<std::ptrdiff_t>(i));
    mDirty = true;
}

// Text format (tab-separated so spaces in file/label are safe). No field may contain a tab or a
// newline, and the baseline must be exactly <length> bytes -- rules PatchLocationValid enforces
// on the way in, so Serialize cannot emit a line that Deserialize would re-split differently:
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

bool PatchLibrary::Deserialize(const std::string& text, std::string* error)
{
    auto fail = [&](const std::string& m) { if (error) *error = m; return false; };

    std::vector<PatchLocation> parsed;
    std::istringstream is(text);
    std::string line;
    if (!std::getline(is, line)) return fail("project file is empty");
    if (!line.empty() && line.back() == '\r') line.pop_back();
    // The version is part of the header, not decoration: a future format that adds a field would
    // otherwise be read by this parser as a record with a stray tab in its label.
    if (line != "SEPATCH 1") return fail("not a Saturn Explorer patch project (header is \"" +
                                         line + "\", expected \"SEPATCH 1\")");

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
        if (f != 5) return fail("malformed record (expected 6 tab-separated fields): " + line);
        field[5] = line.substr(start);   // label = remainder

        PatchLocation e;
        uint64_t addr = 0, len = 0, off = 0;
        if (!ParseUnsigned(field[0], 16, 0xFFFFFFFFull, addr))
            return fail("record's address is not a 32-bit hex number: " + line);
        if (!ParseUnsigned(field[1], 10, 0xFFFFFFFFull, len))
            return fail("record's length is not a 32-bit decimal number: " + line);
        if (!ParseUnsigned(field[2], 10, 0x7FFFFFFFFFFFFFFFull, off))
            return fail("record's file offset is not a decimal number: " + line);
        e.cpuAddr = static_cast<uint32_t>(addr);
        e.length = static_cast<uint32_t>(len);
        e.fileOffset = off;
        if (!FromHex(field[3], e.expected)) return fail("record's baseline is not hex: " + line);
        e.file = field[4];
        e.label = field[5];
        // The same rule the writer is held to. Chiefly this catches a baseline whose length
        // disagrees with the record's own length field, which the script would then compare
        // against the wrong number of bytes -- reporting a mismatch on an untouched file, or
        // matching on a prefix and writing over more than was captured.
        std::string why;
        if (!PatchLocationValid(e, &why)) return fail(why + ": " + line);
        parsed.push_back(std::move(e));
    }
    mEntries = std::move(parsed);
    mDirty = false;
    return true;
}

bool PatchLibrary::LoadProject(const std::string& path, std::string* error)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
    {
        if (error) *error = "cannot open " + path;
        return false;
    }
    // Read explicitly and check for a stream error before parsing: 'ss << f.rdbuf()' stops
    // quietly at a read error, and a prefix that ends on a record boundary parses cleanly --
    // replacing the project with part of itself and clearing the dirty flag.
    std::string text;
    char buf[64 * 1024];
    for (;;)
    {
        f.read(buf, sizeof buf);
        text.append(buf, size_t(f.gcount()));
        if (f.bad())
        {
            if (error) *error = "read error on " + path;
            return false;
        }
        if (f.eof()) break;
    }
    return Deserialize(text, error);
}

std::string PatchLibrary::EmitPython(
    const std::function<bool(uint32_t, uint32_t, std::vector<uint8_t>&)>& readMem,
    std::vector<PatchOutcome>& outcomes) const
{
    outcomes.clear();
    outcomes.reserve(mEntries.size());

    std::ostringstream body;   // the PATCHES table entries (changed only)
    std::vector<uint8_t> current;   // reused scratch for each entry's memory read
    for (const PatchLocation& e : mEntries)
    {
        PatchOutcome oc;
        oc.location = &e;
        if (!readMem || !readMem(e.cpuAddr, e.length, current) || current.size() != e.length)
        {
            oc.readFailed = true;
            outcomes.push_back(oc);
            continue;
        }
        oc.changed = (current != e.expected);
        if (oc.changed)
        {
            // The baseline goes into the script alongside the replacement. Without it the
            // script is just "write these bytes at this offset", which silently corrupts a
            // different revision of the game whose data sits at a different offset.
            body << "    (" << PyStr(e.file) << ", "
                 << static_cast<unsigned long long>(e.fileOffset) << ", "
                 << PyStr(ToHex(e.expected)) << ", "
                 << PyStr(ToHex(current)) << "),\n";
        }
        outcomes.push_back(oc);
    }

    std::ostringstream os;
    os <<
        "#!/usr/bin/env python3\n"
        "# Generated by Saturn Explorer - patches game data files with edited memory.\n"
        "# Do not hand-edit; regenerate from the app (Patch > Apply changes to disc).\n"
        "import os, stat, sys\n\n"
        "BASE = os.path.dirname(os.path.abspath(__file__))\n"
        "# Open files relative to a directory descriptor without following links where the\n"
        "# platform allows it (POSIX); see open_target().\n"
        "HAVE_DIRFD = (hasattr(os, 'O_NOFOLLOW') and hasattr(os, 'O_DIRECTORY')\n"
        "              and os.open in os.supports_dir_fd)\n\n"
        "# (relative_path, byte_offset, expected_hex, replacement_hex)\n"
        "# expected_hex is what the bytes were when the patch was captured. Each write is\n"
        "# refused unless the target still matches, so running this against a different\n"
        "# revision of the game reports a mismatch instead of corrupting it at a stale\n"
        "# offset. --force skips that check; it is not the default for a reason.\n"
        "PATCHES = [\n" << body.str() << "]\n\n"
        "def final_path(fd):\n"
        "    # The path of the file an open descriptor really refers to, as the OS reports it for\n"
        "    # the handle. Asking the handle (not re-walking a name) is what makes the answer about\n"
        "    # the object that will be written. Raises where it cannot be determined, so callers\n"
        "    # fail closed.\n"
        "    if os.name != 'nt':\n"
        "        raise OSError('cannot verify an opened file on this platform')\n"
        "    import ctypes, msvcrt\n"
        "    from ctypes import wintypes\n"
        "    k32 = ctypes.WinDLL('kernel32', use_last_error=True)\n"
        "    k32.GetFinalPathNameByHandleW.argtypes = [\n"
        "        wintypes.HANDLE, wintypes.LPWSTR, wintypes.DWORD, wintypes.DWORD]\n"
        "    k32.GetFinalPathNameByHandleW.restype = wintypes.DWORD\n"
        "    buf = ctypes.create_unicode_buffer(32768)\n"
        "    n = k32.GetFinalPathNameByHandleW(msvcrt.get_osfhandle(fd), buf, len(buf), 0)\n"
        "    if n == 0 or n >= len(buf):\n"
        "        raise OSError('GetFinalPathNameByHandleW failed')\n"
        "    return buf.value\n"
        "\n"
        "def plain(path):\n"
        "    # Drop the \\\\?\\ / \\\\?\\UNC\\ prefix and fold case so two spellings of one path compare equal.\n"
        "    if path.startswith('\\\\\\\\?\\\\UNC\\\\'):\n"
        "        path = '\\\\\\\\' + path[8:]\n"
        "    elif path.startswith('\\\\\\\\?\\\\'):\n"
        "        path = path[4:]\n"
        "    return os.path.normcase(os.path.normpath(path))\n"
        "\n"
        "def open_target(rel):\n"
        "    # Open rel under BASE for reading and writing and return (fd, identity). Nothing here\n"
        "    # keeps a path: the descriptor is what is checked and what is written through, so\n"
        "    # replacing a file or one of its parent directories afterwards changes nothing.\n"
        "    parts = rel.split('/')\n"
        "    for p in parts:\n"
        "        if p in ('', '.', '..') or os.sep in p or (os.altsep and os.altsep in p):\n"
        "            raise ValueError('path escapes the patch directory')\n"
        "    flags = getattr(os, 'O_BINARY', 0)\n"
        "    if HAVE_DIRFD:\n"
        "        # Walk one component at a time from a descriptor for BASE, refusing every symlink\n"
        "        # (O_NOFOLLOW) so no link, existing or swapped in later, can redirect the open.\n"
        "        dirfd = os.open(os.path.realpath(BASE), os.O_RDONLY | os.O_DIRECTORY)\n"
        "        try:\n"
        "            for part in parts[:-1]:\n"
        "                nxt = os.open(part, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW, dir_fd=dirfd)\n"
        "                os.close(dirfd)\n"
        "                dirfd = nxt\n"
        "            fd = os.open(parts[-1], os.O_RDWR | os.O_NOFOLLOW | flags, dir_fd=dirfd)\n"
        "        finally:\n"
        "            os.close(dirfd)\n"
        "    else:\n"
        "        # No dir_fd/O_NOFOLLOW here (Windows). Open the name, then judge the object the\n"
        "        # handle refers to rather than the name: whatever a swapped parent or file led to,\n"
        "        # the opened file's own final path must lie under BASE, or it is closed unwritten.\n"
        "        # The CRT open shares read/write but not delete, so the checked file and its\n"
        "        # parents cannot be renamed away while the handle is held. Any failure to verify\n"
        "        # refuses the file (fails closed).\n"
        "        root = plain(os.path.realpath(BASE))\n"
        "        fd = os.open(os.path.join(root, *parts), os.O_RDWR | flags)\n"
        "        try:\n"
        "            path = plain(final_path(fd))\n"
        "            if path == root or os.path.commonpath([root, path]) != root:\n"
        "                raise ValueError('path escapes the patch directory')\n"
        "        except BaseException:\n"
        "            os.close(fd)\n"
        "            raise\n"
        "    st = os.fstat(fd)\n"
        "    if not stat.S_ISREG(st.st_mode):\n"
        "        os.close(fd)\n"
        "        raise ValueError('not a regular file')\n"
        "    return fd, (st.st_dev, st.st_ino)\n"
        "\n"
        "def read_at(fd, off, n):\n"
        "    os.lseek(fd, off, os.SEEK_SET)\n"
        "    chunks = []\n"
        "    while n > 0:\n"
        "        b = os.read(fd, n)\n"
        "        if not b:\n"
        "            break\n"
        "        chunks.append(b)\n"
        "        n -= len(b)\n"
        "    return b''.join(chunks)\n"
        "\n"
        "def write_at(fd, off, data):\n"
        "    os.lseek(fd, off, os.SEEK_SET)\n"
        "    view = memoryview(data)\n"
        "    while len(view):\n"
        "        view = view[os.write(fd, view):]\n"
        "\n"
        "def main():\n"
        "    force = '--force' in sys.argv[1:]\n"
        "    by_rel = {}   # rel -> (fd, identity)\n"
        "    fds = {}      # identity -> fd; one handle per real file, held until the script exits\n"
        "    try:\n"
        "        return patch_all(force, by_rel, fds)\n"
        "    finally:\n"
        "        for fd in fds.values():\n"
        "            os.close(fd)\n"
        "\n"
        "def patch_all(force, by_rel, fds):\n"
        "    skipped = 0\n"
        "    failed = 0\n"
        "    # Pass 1 judges every patch against the files as they are now, before anything is\n"
        "    # written. Checking each one just before writing it would let an earlier patch change\n"
        "    # the baseline a later, overlapping one expects, and leave the file half patched.\n"
        "    # The files stay open from here through pass 2, so what is checked is what is written.\n"
        "    plan = []   # (rel, ident, fd, off, data)\n"
        "    for rel, off, want_hex, new_hex in PATCHES:\n"
        "        want = bytes.fromhex(want_hex)\n"
        "        data = bytes.fromhex(new_hex)\n"
        "        try:\n"
        "            if rel not in by_rel:\n"
        "                fd, ident = open_target(rel)\n"
        "                if ident in fds:\n"
        "                    os.close(fd)\n"
        "                    fd = fds[ident]\n"
        "                else:\n"
        "                    fds[ident] = fd\n"
        "                by_rel[rel] = (fd, ident)\n"
        "            fd, ident = by_rel[rel]\n"
        "            have = read_at(fd, off, len(want))\n"
        "        except (OSError, ValueError) as e:\n"
        "            print('FAILED %s: %s' % (rel, e), file=sys.stderr)\n"
        "            failed += 1\n"
        "            continue\n"
        "        if have != want and not force:\n"
        "            print('SKIPPED %s @ %d: expected %s, found %s'\n"
        "                  % (rel, off, want.hex(), have.hex()), file=sys.stderr)\n"
        "            skipped += 1\n"
        "            continue\n"
        "        plan.append((rel, ident, fd, off, data))\n"
        "    # Patches that overlap must agree on the bytes they share (the same edit reached\n"
        "    # through two mappings); disagreeing ones have no right answer, so write nothing.\n"
        "    conflict = False\n"
        "    live = {}   # file identity -> earlier patches whose range may still reach the next one\n"
        "    for rel, ident, fd, off, data in sorted(plan, key=lambda p: (p[1], p[3])):\n"
        "        active = [q for q in live.get(ident, []) if q[0] + len(q[1]) > off]\n"
        "        for qoff, qdata in active:\n"
        "            lo = off\n"
        "            hi = min(off + len(data), qoff + len(qdata))\n"
        "            if data[lo - off:hi - off] != qdata[lo - qoff:hi - qoff]:\n"
        "                print('CONFLICT %s: patches at %d and %d overlap with different bytes'\n"
        "                      % (rel, qoff, off), file=sys.stderr)\n"
        "                conflict = True\n"
        "        active.append((off, data))\n"
        "        live[ident] = active\n"
        "    if conflict:\n"
        "        print('No file was modified.', file=sys.stderr)\n"
        "        return 1\n"
        "    ok = 0\n"
        "    for rel, ident, fd, off, data in plan:\n"
        "        try:\n"
        "            write_at(fd, off, data)\n"
        "            print('patched %s @ %d (%d bytes)' % (rel, off, len(data)))\n"
        "            ok += 1\n"
        "        except OSError as e:\n"
        "            print('FAILED %s: %s' % (rel, e), file=sys.stderr)\n"
        "            failed += 1\n"
        "    print('%d file(s) patched, %d skipped, %d failed' % (ok, skipped, failed))\n"
        "    if skipped and not force:\n"
        "        print('Re-run with --force to write anyway.', file=sys.stderr)\n"
        "    return 1 if (skipped or failed) else 0\n"
        "\n"
        "if __name__ == '__main__':\n"
        "    sys.exit(main())\n";
    return os.str();
}

}  // namespace sfe
