// Unit tests for PatchLibrary: AddOrUpdate merge semantics, project text round-trip, and
// EmitPython — including running the emitted script with the real python3 interpreter against
// a scratch data directory and asserting the bytes actually land at the right offsets.
#include "PatchLibrary.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; } }

PatchLocation Loc(const char* label, uint32_t addr, uint32_t len, const char* file,
                  uint64_t off, std::vector<uint8_t> expected)
{
    PatchLocation e;
    e.label = label; e.cpuAddr = addr; e.length = len; e.file = file; e.fileOffset = off;
    e.expected = std::move(expected);
    return e;
}

// A readMem that serves bytes from a fixed map keyed by address, or fails for unknown addrs.
struct MemStub
{
    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> mem;
    bool Read(uint32_t addr, uint32_t len, std::vector<uint8_t>& out) const
    {
        for (const auto& kv : mem)
            if (kv.first == addr && kv.second.size() == len) { out = kv.second; return true; }
        return false;
    }
};

std::string ReadFile(const std::string& p)
{
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}
}  // namespace

int main()
{
    // --- AddOrUpdate: dedup/merge by (file, offset) ---
    {
        PatchLibrary lib;
        lib.AddOrUpdate(Loc("a", 0x200000, 2, "A.BIN", 10, {0x11, 0x22}));
        lib.AddOrUpdate(Loc("b", 0x200100, 4, "B.BIN", 20, {0x01, 0x02, 0x03, 0x04}));
        Check(lib.Count() == 2, "two distinct locations added");
        Check(lib.Dirty(), "library dirty after adds");

        // Same (file, offset) -> update in place, not a new entry.
        lib.AddOrUpdate(Loc("a2", 0x200000, 3, "A.BIN", 10, {0xAA, 0xBB, 0xCC}));
        Check(lib.Count() == 2, "same (file,offset) updates in place");
        Check(lib.Entries()[0].length == 3 && lib.Entries()[0].label == "a2",
              "updated entry reflects new mapping");

        // Idempotent re-add of an identical entry does not re-dirty.
        lib.ClearDirty();
        lib.AddOrUpdate(Loc("a2", 0x200000, 3, "A.BIN", 10, {0xAA, 0xBB, 0xCC}));
        Check(!lib.Dirty(), "identical re-add is not a change");

        // Different offset in the same file is a distinct entry.
        lib.AddOrUpdate(Loc("a3", 0x200000, 3, "A.BIN", 99, {0xAA, 0xBB, 0xCC}));
        Check(lib.Count() == 3, "different offset in same file is distinct");
    }

    // --- Project text round-trip ---
    {
        PatchLibrary lib;
        lib.AddOrUpdate(Loc("WRAM 0x00250100 (2 bytes)", 0x250100, 2, "SOUND/BGM01.PCM", 4128, {0xDE, 0xAD}));
        lib.AddOrUpdate(Loc("has spaces in label", 0x260000, 1, "DATA/FILE WITH SPACE.BIN", 0, {0x7F}));
        const std::string text = lib.Serialize();

        PatchLibrary lib2;
        Check(lib2.Deserialize(text), "deserialize accepts serialized text");
        Check(!lib2.Dirty(), "deserialize clears dirty");
        Check(lib2.Count() == 2, "round-trip preserves entry count");
        const PatchLocation& e = lib2.Entries()[0];
        Check(e.cpuAddr == 0x250100 && e.length == 2 && e.fileOffset == 4128 &&
              e.file == "SOUND/BGM01.PCM" && e.expected == std::vector<uint8_t>({0xDE, 0xAD}) &&
              e.label == "WRAM 0x00250100 (2 bytes)", "round-trip preserves fields");
        Check(lib2.Entries()[1].file == "DATA/FILE WITH SPACE.BIN" &&
              lib2.Entries()[1].label == "has spaces in label", "spaces in file/label survive");
        // A garbage payload is rejected (no header).
        PatchLibrary lib3;
        Check(!lib3.Deserialize("not a project\n"), "missing header rejected");
    }

    // --- EmitPython: only changed entries emitted; outcomes classify each ---
    {
        PatchLibrary lib;
        lib.AddOrUpdate(Loc("chg", 0x200000, 2, "A.BIN", 4, {0x11, 0x22}));   // will change
        lib.AddOrUpdate(Loc("same", 0x200100, 2, "B.BIN", 0, {0x33, 0x44}));  // unchanged
        lib.AddOrUpdate(Loc("gone", 0x200200, 2, "C.BIN", 0, {0x55, 0x66}));  // read fails

        MemStub mem;
        mem.mem.push_back({0x200000, {0xAB, 0xCD}});   // differs from expected -> changed
        mem.mem.push_back({0x200100, {0x33, 0x44}});   // equals expected -> unchanged
        // 0x200200 absent -> read fails

        std::vector<PatchOutcome> oc;
        const std::string py = lib.EmitPython(
            [&](uint32_t a, uint32_t l, std::vector<uint8_t>& o) { return mem.Read(a, l, o); }, oc);

        Check(oc.size() == 3, "one outcome per entry");
        Check(oc[0].changed && !oc[0].readFailed, "entry 0 classified changed");
        Check(!oc[1].changed && !oc[1].readFailed, "entry 1 classified unchanged");
        Check(oc[2].readFailed, "entry 2 classified read-failed");
        // Only the changed entry's new bytes appear in the table.
        Check(py.find("\"abcd\"") != std::string::npos, "changed entry emits current bytes");
        Check(py.find("\"A.BIN\"") != std::string::npos, "changed entry emits its file");
        Check(py.find("B.BIN") == std::string::npos, "unchanged entry not emitted");
        Check(py.find("C.BIN") == std::string::npos, "read-failed entry not emitted");
    }

    // --- End-to-end: run the emitted script with real python3 and check the file is patched ---
    {
        const char* py3 = std::getenv("SE_PYTHON");
        std::string python = py3 ? py3 : "python3";
        // Probe python availability; skip this leg (don't fail) if absent.
        std::string probe = python + " --version >/dev/null 2>&1";
        if (std::system(probe.c_str()) != 0)
        {
            std::printf("(skip python e2e: no %s interpreter)\n", python.c_str());
        }
        else
        {
            // Scratch data dir with a subfolder + a file to patch.
            char tmpl[] = "/tmp/se_patch_testXXXXXX";
            const char* dir = mkdtemp(tmpl);
            Check(dir != nullptr, "made scratch data dir");
            const std::string sub = std::string(dir) + "/SOUND";
            std::string mk = "mkdir -p '" + sub + "'";
            std::system(mk.c_str());
            const std::string target = sub + "/BGM01.PCM";
            {
                std::ofstream f(target, std::ios::binary);
                const char zeros[16] = {0};
                f.write(zeros, sizeof(zeros));   // 16 zero bytes
            }

            PatchLibrary lib;
            // Map memory 0x200000 (4 bytes) -> SOUND/BGM01.PCM @ offset 4. Baseline all-zero.
            lib.AddOrUpdate(Loc("t", 0x200000, 4, "SOUND/BGM01.PCM", 4, {0, 0, 0, 0}));
            MemStub mem; mem.mem.push_back({0x200000, {0xDE, 0xAD, 0xBE, 0xEF}});
            std::vector<PatchOutcome> oc;
            const std::string script = lib.EmitPython(
                [&](uint32_t a, uint32_t l, std::vector<uint8_t>& o) { return mem.Read(a, l, o); }, oc);

            const std::string scriptPath = std::string(dir) + "/se_patch.py";
            { std::ofstream f(scriptPath, std::ios::binary); f << script; }

            std::string run = python + " '" + scriptPath + "' >/dev/null 2>&1";
            Check(std::system(run.c_str()) == 0, "python patch script runs cleanly");

            const std::string patched = ReadFile(target);
            Check(patched.size() == 16, "patched file keeps its size");
            const bool bytesOk = patched.size() == 16 &&
                (uint8_t)patched[4] == 0xDE && (uint8_t)patched[5] == 0xAD &&
                (uint8_t)patched[6] == 0xBE && (uint8_t)patched[7] == 0xEF &&
                (uint8_t)patched[0] == 0x00 && (uint8_t)patched[8] == 0x00;
            Check(bytesOk, "edited bytes landed at the mapped offset, neighbors untouched");

            std::string rm = "rm -rf '" + std::string(dir) + "'";
            std::system(rm.c_str());
        }
    }

    if (gFail == 0) std::printf("All PatchLibrary tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
