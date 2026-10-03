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
#if !defined(_WIN32)
#include <unistd.h>   // mkdtemp — declared here on macOS/clang; the python e2e block is POSIX-only
#endif

using namespace sfe;

namespace
{
int gFail = 0;
void Check(bool ok, const char* what) { if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; } }

// Run a shell command whose failure is not a test failure (scratch-file cleanup). std::system is
// warn_unused_result, and a (void) cast does not silence that on gcc, so swallow it explicitly.
void BestEffort(const std::string& cmd) { if (std::system(cmd.c_str()) != 0) { /* cleanup only */ } }

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
    // POSIX-only: this leg drives a POSIX shell (mkdtemp, /tmp, `mkdir -p`, `rm -rf`, `>/dev/null`),
    // none of which behave under cmd.exe, so skip it on Windows.
#ifndef _WIN32
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
            Check(std::system(mk.c_str()) == 0, "made the scratch SOUND subfolder");
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

            // --- The baseline check: a target that no longer matches must be refused ---
            // Re-running the same script now finds 0xDEADBEEF where it expected zeroes,
            // which is exactly the shape of running a project against another revision of
            // the game. It must report a mismatch and leave the bytes alone.
            {
                std::ofstream f(target, std::ios::binary | std::ios::in);
                f.seekp(4);
                const char marker[4] = {0x11, 0x22, 0x33, 0x44};
                f.write(marker, sizeof(marker));
            }
            Check(std::system((python + " '" + scriptPath + "' >/dev/null 2>&1").c_str()) != 0,
                  "script fails when the target no longer matches its baseline");
            const std::string untouched = ReadFile(target);
            Check(untouched.size() == 16 && (uint8_t)untouched[4] == 0x11 &&
                      (uint8_t)untouched[7] == 0x44,
                  "a refused patch leaves the file byte-for-byte alone");

            // --force is the documented escape hatch, and it must actually write.
            Check(std::system((python + " '" + scriptPath + "' --force >/dev/null 2>&1").c_str()) == 0,
                  "--force writes over a mismatched baseline");
            const std::string forced = ReadFile(target);
            Check(forced.size() == 16 && (uint8_t)forced[4] == 0xDE && (uint8_t)forced[7] == 0xEF,
                  "--force landed the replacement bytes");

            // --- Containment: a project path climbing out of BASE must be refused ---
            {
                PatchLibrary esc;
                esc.AddOrUpdate(Loc("e", 0x200000, 4, "../escape.bin", 0, {0, 0, 0, 0}));
                MemStub em; em.mem.push_back({0x200000, {0xAA, 0xBB, 0xCC, 0xDD}});
                std::vector<PatchOutcome> eoc;
                const std::string escScript = esc.EmitPython(
                    [&](uint32_t a, uint32_t l, std::vector<uint8_t>& o) { return em.Read(a, l, o); },
                    eoc);
                const std::string escPath = std::string(dir) + "/se_escape.py";
                { std::ofstream f(escPath, std::ios::binary); f << escScript; }
                Check(escScript.find("../escape.bin") != std::string::npos,
                      "the escaping entry really was emitted into the script");
                // The would-be victim sits beside the patch dir, where '..' would reach it.
                const std::string victim = std::string(dir) + "/../escape.bin";
                { std::ofstream f(victim, std::ios::binary); const char z[8] = {0}; f.write(z, 8); }
                Check(std::system((python + " '" + escPath + "' >/dev/null 2>&1").c_str()) != 0,
                      "script refuses a path that escapes the patch directory");
                const std::string still = ReadFile(victim);
                Check(still.size() == 8 && (uint8_t)still[0] == 0x00,
                      "the file outside the patch directory was not written");
                BestEffort("rm -f '" + victim + "'");
            }

            BestEffort("rm -rf '" + std::string(dir) + "'");
        }
    }
#endif  // !_WIN32

    // --- ROM-05: the project format's own rules, enforced on the way in and out. ---
    {
        PatchLibrary lib;
        std::string why;

        // A tab or newline in the file path re-splits the record on reload: the tab becomes a
        // field separator, the newline a second line. It cannot be substituted away either --
        // the path is what the generated script writes to -- so it is refused.
        Check(!lib.AddOrUpdate(Loc("t", 0x200000, 1, "A\tB.BIN", 0, {0x11}), &why),
              "a tab in the file path is refused");
        Check(!why.empty(), "the refusal says why");
        Check(!lib.AddOrUpdate(Loc("t", 0x200000, 1, "A\nB.BIN", 0, {0x11})),
              "a newline in the file path is refused");
        Check(lib.Count() == 0, "a refused location is not stored");

        // The baseline is what the script compares the target against, so a size other than the
        // record's own length compares the wrong number of bytes.
        Check(!lib.AddOrUpdate(Loc("s", 0x200000, 4, "A.BIN", 0, {0x11, 0x22})),
              "a baseline shorter than the length is refused");
        Check(!lib.AddOrUpdate(Loc("s", 0x200000, 0, "A.BIN", 0, {})),
              "a zero-length location is refused");

        // A label is cosmetic, so its whitespace is repaired rather than refused -- but it must
        // still not be able to re-split the line.
        Check(lib.AddOrUpdate(Loc("has\ta tab\nand a newline", 0x200000, 1, "A.BIN", 0, {0x11})),
              "a label with separators is accepted");
        Check(lib.Count() == 1, "and stored");
        Check(lib.Entries()[0].label == "has a tab and a newline", "with the separators replaced");

        // Round-tripping it therefore gives the label back intact, rather than shifting fields.
        PatchLibrary back;
        Check(back.Deserialize(lib.Serialize()), "the sanitized record round-trips");
        Check(back.Count() == 1 && back.Entries()[0].label == "has a tab and a newline",
              "the label survives the round-trip");
        Check(back.Entries()[0].file == "A.BIN", "and so does the file path");
    }
    {
        // Parsing is strict in both directions: a record whose baseline disagrees with its length
        // fails the load rather than being skipped. A project that loads with locations quietly
        // missing is worse than one that refuses, because the next save writes the loss back.
        PatchLibrary lib;
        std::string why;
        Check(!lib.Deserialize("SEPATCH 1\n00200000\t4\t0\t1122\tA.BIN\tshort baseline\n", &why),
              "a baseline that is not <length> bytes fails the parse");
        Check(!why.empty(), "and says which record");
        Check(!lib.Deserialize("SEPATCH 1\n00200000\t2\tA.BIN\tmissing fields\n"),
              "a record with too few fields fails the parse");
        Check(!lib.Deserialize("SEPATCH 1\n00200000\t2\t0\tnothex\tA.BIN\tx\n"),
              "a baseline that is not hex fails the parse");

        // The version is part of the header. A later format that adds a field would otherwise be
        // read here as a record with a stray tab in its label.
        Check(!lib.Deserialize("SEPATCH 2\n"), "a later format version is refused");
        Check(!lib.Deserialize("SEPATCHED\n"), "a header that merely starts with SEPATCH is refused");
        Check(lib.Deserialize("SEPATCH 1\n"), "a header with no records is a valid empty project");
        Check(lib.Count() == 0, "and loads nothing");

        // A CRLF project file still loads: the header check must not trip over the '\r'.
        Check(lib.Deserialize("SEPATCH 1\r\n00200000\t2\t0\t1122\tA.BIN\tok\r\n"),
              "a CRLF project file loads");
        Check(lib.Count() == 1, "and its record is kept");
    }

    if (gFail == 0) std::printf("All PatchLibrary tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
