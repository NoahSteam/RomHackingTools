#!/usr/bin/env python3
"""Area 11 round 1: the mutation harness.

usage: mutate.py --root <repo root> --build <build dir> --rev <dir of patch_mutants.py output> --out <log dir>
                 [--only id,id,...] [--group patch|host|env|all] [--minutes 20] [--list]
                 [--hwram <hwram007.bin>]

Applies one mutant at a time to the tree, builds only the test targets the oracles need (ninja,
-j2, under the machine's rules), runs the oracles that should see it, then puts the tree back
(`git checkout` of the files it touched) and checks `git status` is clean before the next one.
A mutant is:
  patch:NNNN   vendored patch NNNN reverted (the hunks patch_mutants.py found revertible alone),
               the spec patches through se-regen-sla (sh-2.sla and Sh2SpecData.h are restored too);
  host         a one-expression change to the host, the panel, the runner or a loader (MUTANTS);
  env          an oracle's own "check checked" mode (SE_GENERATED_MUTATE, SE_GAME_MAPPING_MUTATE):
               no rebuild, the run must fail.
Each mutant names its oracles: 'should' (the oracles whose class it is; each is run and scored),
then 'escalate' (run in order only while nothing has killed it, cheapest first). A run that exits
non-zero (not 77), crashes or times out kills it; one that fails to build is "stillborn" and not
scored. Results go to <out>/results.tsv (one row per mutant and oracle) and <out>/<id>.<oracle>.log.
--minutes stops before starting a mutant once that much time has gone (the 20-minute pieces).
"""
import argparse
import json
import os
import re
import resource
import subprocess
import sys
import time

# ---- oracles ---------------------------------------------------------------------------------------
# name: (target, argv after the binary's path, env, timeout seconds)
ORACLES = {
    "unit": ("SaturnExplorerSh2DecompilerTests", ["{scratch}"], {}, 300),
    "gen": ("SaturnExplorerSh2GeneratedTests", ["{scratch}"], {}, 400),
    "pc": ("SaturnExplorerSh2GeneratedTests", ["{scratch}"], {"SE_GENERATED_PRINTEDC": "only"}, 600),
    "engine": ("SaturnExplorerSh2DecompilerEngineTests", ["{scratch}"], {}, 300),
    "decode": ("SaturnExplorerSh2DecodeTests", ["{scratch}"], {}, 400),
    "corpus": ("SaturnExplorerSh2DecompilerTests", ["{scratch}"], {"SE_GAME_HWRAM": "{hwram}"}, 900),
    "fixture": ("SaturnExplorerSh2DecompilerFixtureTests", ["{scratch}"], {}, 300),
    "fault": ("SaturnExplorerDecompilerFaultTests", ["{scratch}"], {"SE_FAULT_JOBS": "2"}, 900),
    "runner": ("SaturnExplorerDecompilerRunnerTests", ["{scratch}"], {}, 400),
    "model": ("SaturnExplorerDecompilerModelTests", ["{scratch}"], {}, 400),
    "trust": ("SaturnExplorerDecompilerTrustTests", ["{scratch}"], {}, 600),
    "panel": ("SaturnExplorerDecompilerPanelTests", ["{scratch}"], {}, 300),
    "fuzz": ("SaturnExplorerLoaderFuzzTests", ["{scratch}"], {}, 400),
}

ENGINE_ESCALATE = ["unit", "gen", "engine", "pc", "decode", "corpus"]
HOST_ESCALATE = ["unit", "gen", "pc", "corpus"]

SRC = "SaturnExplorer/FrontEnd/src/"
VEN = "SaturnExplorer/FrontEnd/third_party/ghidra-decompiler/"
DEC = SRC + "Decompiler/Sh2Decompiler.cpp"

# ---- host mutants ------------------------------------------------------------------------------------
# (id, file, old text (must occur exactly once), new text, should, escalate, what it simulates)
MUTANTS = [
    # The access ledger (Accesses).
    ("H-led-width", DEC, "a.size = static_cast<uint8_t>(value->getSize());", "a.size = 4;",
     ["unit", "gen"], ["corpus"], "every ledger entry is a longword"),
    ("H-led-printed", DEC, "a.printed = a.write || !o->getOut() ? 1 : emissions(o->getOut());", "a.printed = 1;",
     ["unit", "gen"], ["corpus"], "an implied read printed at several uses counts once"),
    ("H-led-cond", DEC, "if ((use->code() == ghidra::CPUI_BOOL_AND || use->code() == ghidra::CPUI_BOOL_OR) && use->getSlot(v) == 1)\n                    a.conditionalInPrint = true;",
     "if (false)\n                    a.conditionalInPrint = true;", ["unit", "gen"], ["corpus"], "a read in the right operand of && / || not flagged"),
    ("H-led-repeat", DEC, "if (static_cast<int>(before.size()) < allowed) continue;", "continue;",
     ["unit", "gen"], ["corpus"], "an access copied onto one path is never flagged"),
    ("H-led-insn", DEC, "a.insn = canonicalOf(InstructionOf(o));", "a.insn = canonicalOf(InstructionOf(o)) + 2;",
     ["unit", "gen"], ["corpus"], "the ledger names the next instruction"),
    ("H-led-nowrites", DEC, "        if (!value) continue;\n        a.size", "        if (!value || a.write) continue;\n        a.size",
     ["unit", "gen"], ["corpus"], "writes left out of the ledger"),
    ("H-led-direct", DEC, "            a.direct = true;\n            a.address = static_cast<uint32_t>(o->getIn(1)->getOffset());",
     "            a.direct = false;\n            a.address = static_cast<uint32_t>(o->getIn(1)->getOffset());",
     ["unit", "gen"], ["corpus"], "a volatile register access listed as through a pointer"),
    ("H-led-returned", DEC, "a.returned = !a.write && returnedVns.count(value) != 0;", "a.returned = false;",
     ["unit", "gen"], ["corpus"], "a read whose value is returned not marked"),
    # Conservation: the engine's records as the host reads them.
    ("H-fate-unreach", DEC, "case ghidra::fate_unreachable: f.fate = AccessFate::Unreachable; break;",
     "case ghidra::fate_unreachable: f.fate = AccessFate::FoldedOrdinary; break;",
     ["unit", "gen"], ["corpus"], "an access removed with unreachable code taken for a fold (a loss hidden as conserved)"),
    ("H-fate-switch", DEC, "case ghidra::fate_switch_lookup: f.fate = AccessFate::SwitchLookup; break;",
     "case ghidra::fate_switch_lookup: f.fate = AccessFate::Dead; break;",
     ["unit", "gen"], ["corpus"], "a switch's table read recorded as dead"),
    ("H-fate-frameunread", DEC, "case ghidra::fate_frame_unread: f.fate = AccessFate::FrameUnread; break;",
     "case ghidra::fate_frame_unread: f.fate = AccessFate::FoldedOrdinary; break;",
     ["unit", "gen"], ["corpus"], "the frame ledger's unread writes read as folds (A3-124's loss hidden)"),
    ("H-fate-capped", DEC, "r.frameLedgerCapped = fd->isFrameLedgerCapped();", "r.frameLedgerCapped = false;",
     ["unit", "gen"], ["corpus"], "a frame ledger that hit its bound reported as checked (A3-133 fail-open)"),
    ("H-fate-insn", DEC, "            f.insn = canonicalOf(InstructionOf(rec));\n            f.write = rec.store;",
     "            f.insn = canonicalOf(InstructionOf(rec)) + 2;\n            f.write = rec.store;",
     ["unit", "gen"], ["corpus"], "a record names the next instruction"),
    ("H-removed-unsaid", DEC, "        if (outcome[i] != Removed) continue;\n        // Every register",
     "        continue;\n        // Every register", ["unit", "gen"], ["corpus"], "a hardware access removed as unreachable is not said"),
    # The mapping (tokens, hover text, body).
    ("H-map-origin", DEC, "    if (at.size() == 1)\n    {\n        // The click", "    if (at.size() >= 1)\n    {\n        // The click",
     ["unit", "gen"], ["corpus"], "a line from several instructions described as one (tokunmark in the product)"),
    ("H-map-first", DEC, "        for (const Token& t : l.tokens)\n            if (t.addr && (l.firstAddr == 0 || t.addr < l.firstAddr)) l.firstAddr = t.addr;\n        out.push_back(std::move(l));",
     "        for (const Token& t : l.tokens)\n            if (t.addr && (l.firstAddr == 0 || t.addr > l.firstAddr)) l.firstAddr = t.addr;\n        out.push_back(std::move(l));",
     ["unit", "gen"], ["corpus"], "firstAddr is the highest token address (tokfirst in the product)"),
    ("H-map-slotbody", DEC, "            all.insert(a + 2);\n            next = a + 4;", "            next = a + 4;",
     ["unit", "gen"], ["corpus"], "a delay slot left out of the body ranges"),
    ("H-map-framestore", DEC, "int AddFrameStoreTokens(std::vector<Line>& lines, const PrintedOperations& printed, const ghidra::Funcdata& fd, const FlowRecord& flow)\n{",
     "int AddFrameStoreTokens(std::vector<Line>& lines, const PrintedOperations& printed, const ghidra::Funcdata& fd, const FlowRecord& flow)\n{\n    if (lines.size() < 1000000) return 0;",
     ["unit", "gen"], ["corpus"], "A7-11/A7-19's store tokens never added (tokframe/tokcopy in the product)"),
    # Hardware notes and PointerResolver.
    ("H-note-wo", DEC, "if (read && reg->access == RegisterAccess::WriteOnly)", "if (false && read && reg->access == RegisterAccess::WriteOnly)",
     ["unit"], ["gen", "corpus"], "a read of a write-only register not cautioned"),
    ("H-note-ro", DEC, "if (write && reg->access == RegisterAccess::ReadOnly)", "if (false && write && reg->access == RegisterAccess::ReadOnly)",
     ["unit"], ["gen", "corpus"], "a write of a read-only register not cautioned"),
    ("H-note-reach", DEC, "        const uint32_t reach = AccessAddress(at, size);\n        const std::vector<const HardwareRegister*> regs = HardwareRegistersReached(reach, size, write);",
     "        const uint32_t reach = at;\n        const std::vector<const HardwareRegister*> regs = HardwareRegistersReached(reach, size, write);",
     ["unit"], ["gen", "corpus"], "a misaligned access's registers taken at the address given (A5-25)"),
    ("H-note-narrow", DEC, "for (const NarrowEffect& e : NarrowAccessEffects(at, size, write)) note(insn, e.text, e.caution);",
     "for (const NarrowEffect& e : NarrowAccessEffects(at, size, write)) if (false) note(insn, e.text, e.caution);",
     ["unit"], ["gen", "corpus"], "the width table's cautions never said"),
    ("H-note-cram", DEC, "        if (write && size == 1 && InColorRam(at))\n            note(insn, ColorRamByteNote(at), true);",
     "        if (false && write && size == 1 && InColorRam(at))\n            note(insn, ColorRamByteNote(at), true);",
     ["unit"], ["gen", "corpus"], "a byte write of colour RAM not cautioned (A5-50)"),
    ("H-ptr-sub", DEC, "out = Shift(Walk(base, depth + 1), sub ? 0u - k : k);", "out = Shift(Walk(base, depth + 1), k);",
     ["unit"], ["gen", "corpus"], "PointerResolver adds a subtracted constant"),
    ("H-ptr-phi", DEC, "            for (int i = 0; i < d->numInput(); ++i) Merge(out, Walk(d->getIn(i), depth + 1));\n            break;\n        case ghidra::CPUI_PTRSUB:",
     "            for (int i = 0; i < 1; ++i) Merge(out, Walk(d->getIn(i), depth + 1));\n            break;\n        case ghidra::CPUI_PTRSUB:",
     ["unit"], ["gen", "corpus"], "PointerResolver follows only the first input of a join"),
    ("H-ptr-loop", DEC, "if (budget_ <= 0 || !v.back.empty()) v.others = true;", "if (budget_ <= 0) v.others = true;",
     ["unit"], ["gen", "corpus"], "a pointer round a loop not said to hold other values"),
    # Lending (areas 3 and 4).
    ("H-lend-unknown", DEC, "if (lent.count(t) == 0) return \"it calls \" + Hex8(t) + \", whose signature is not known\";",
     "if (false) return \"it calls \" + Hex8(t) + \", whose signature is not known\";",
     ["gen"], ["unit", "corpus"], "a signature lent though a callee's is unknown"),
    ("H-lend-self", DEC, "if (t == entry || (CanonicalAddress(t, c) && c == entry)) return \"it calls itself\";",
     "if (false) return \"it calls itself\";", ["gen"], ["unit", "corpus"], "a recursive function lent"),
    ("H-lend-kill", DEC, "            effects.emplace_back(arch.translate->getRegister(k.name), ghidra::EffectRecord::killedbycall);",
     "            (void)k;", ["gen"], ["unit", "corpus"], "a callee proved to change r8-r14 lent as keeping them (A3-3)"),
    ("H-lend-extrapop", DEC, "        fp.setExtraPop(proto.effects.spDelta);", "        (void)proto;",
     ["gen"], ["unit", "corpus"], "a callee's r15 distance not applied"),
    # Cancellation inside the host (area 6).
    ("H-cancel-actions", DEC, "auto checkCancel = [&] {\n        if (cancel && cancel->load()) throw ghidra::CancelError(\"cancelled\");",
     "auto checkCancel = [&] {\n        if (false && cancel && cancel->load()) throw ghidra::CancelError(\"cancelled\");",
     ["fault"], ["runner"], "the host's check-points never cancel"),
    # The runner (areas 1 and 6).
    ("H-run-epoch", SRC + "Decompiler/DecompilerRunner.cpp", "if (mDisabled || done.first != mEpoch) continue;", "if (mDisabled) continue;",
     ["runner", "model"], ["fault", "trust"], "a result of an earlier epoch delivered"),
    ("H-run-gen", SRC + "Decompiler/DecompilerRunner.cpp", "if (latest == mLatest.end() || latest->second != done.second.generation) continue;",
     "if (latest == mLatest.end()) continue;", ["runner", "model"], ["fault", "trust"], "a result for a stale generation delivered"),
    ("H-run-toolarge", SRC + "Decompiler/DecompilerRunner.cpp", "if (job.epoch == mEpoch) mTooLarge.insert(job.memoKey);", "mTooLarge.insert(job.memoKey);",
     ["runner", "model"], ["fault", "trust"], "an out-of-budget memo of an earlier epoch kept"),
    # The panel's trust indicators (area 8).
    ("H-pan-volatile", SRC + "DecompilerPanel.cpp", "           mShownVolatileHash != mVolatileHash ||   // decompiled under another volatile list\n",
     "", ["trust"], ["panel", "model"], "output decompiled under another volatile list not stale"),
    ("H-pan-names", SRC + "DecompilerPanel.cpp", "return ShownOutdatedByView() || mShownNamesGeneration != mNamesGeneration ||",
     "return ShownOutdatedByView() ||", ["trust"], ["panel", "model"], "output from before a rename not stale"),
    ("H-pan-disabled", SRC + "DecompilerPanel.cpp", "    if (mRunnerDisabled) return StaleReason::Disabled;\n", "",
     ["trust"], ["panel", "model"], "the stale reason does not say the runner is disabled"),
    ("H-pan-truncated", SRC + "DecompilerPanel.cpp", "    if (mShown.truncated)\n    {\n        std::snprintf(b, sizeof b, \"Flows into",
     "    if (false)\n    {\n        std::snprintf(b, sizeof b, \"Flows into", ["trust"], ["panel"], "a truncated function not said"),
    ("H-pan-unsupported", SRC + "DecompilerPanel.cpp", "    if (mShown.unsupported)\n    {\n        std::snprintf(b, sizeof b, \"PC-relative",
     "    if (false)\n    {\n        std::snprintf(b, sizeof b, \"PC-relative", ["trust"], ["panel"], "an unsupported function not said"),
    ("H-pan-copymarks", SRC + "DecompilerPanel.cpp", "    std::string out;\n    if (ShownStale())\n        out += comment(",
     "    std::string out;\n    if (false)\n        out += comment(", ["trust"], ["panel"], "Copy drops the stale mark (A8-21)"),
    # Loaders (area 9).
    ("H-load-empty", SRC + "Decompiler/BiosImageFile.cpp", "    if (bytes.empty())\n", "    if (false)\n",
     ["fuzz"], [], "an empty BIOS file accepted"),
    ("H-load-toolarge", SRC + "Decompiler/BiosImageFile.cpp",
     "case CappedRead::TooLarge: r.error = path + \" is larger than a Saturn BIOS (512 KiB); not used\"; return r;",
     "case CappedRead::TooLarge: break;", ["fuzz"], [], "a file larger than a BIOS accepted, cut at the ceiling"),
    ("H-load-hexlen", SRC + "Decompiler/AddressRanges.cpp", "if (d.empty() || d.size() > 8) return false;", "if (d.empty()) return false;",
     ["fuzz"], [], "a volatile address of nine digits wraps"),
    ("H-load-inverted", SRC + "Decompiler/AddressRanges.cpp", "if (!ok || hi < lo)", "if (!ok)",
     ["fuzz"], [], "an inverted volatile range accepted"),
    # Hand-made reverts of the patches no hunk of which reverts alone (later patches rewrote the lines).
    ("V-0019-ldc", VEN + "processors/SuperH/superh.sinc", "    local value:4 = rm_08_11 & 0x000003F3;", "    local value:4 = rm_08_11 & 0x0FFF0FFF;",
     ["decode", "unit"], ENGINE_ESCALATE, "patch 0019 reverted in ldc Rm,SR: bits the SH-2 does not have kept"),
    ("V-0019-rte", VEN + "processors/SuperH/superh.sinc", "    _sr:4 = *r15 & 0x000003F3;", "    _sr:4 = *r15 & 0x000063F3;",
     ["decode", "unit"], ENGINE_ESCALATE, "patch 0019 reverted in rte: SH-2A bits 13 and 14 restored"),
    ("V-0071", VEN + "cpp/cast.cc", "  if (exttype2 == NO_PROMOTION && !othervn->isConstant() && othervn->getSize() < promoteSize) {",
     "  if (false && exttype2 == NO_PROMOTION && !othervn->isConstant() && othervn->getSize() < promoteSize) {",
     ["pc", "unit"], ENGINE_ESCALATE, "patches 0071 and 0077 reverted: a narrow named value against a constant of the other sign uncast"),
    ("H-load-partial", SRC + "Decompiler/AddressRanges.cpp", "            error = \"\\\"\" + entry + \"\\\" is not an address or a range first-last\";\n            out.clear();",
     "            error = \"\\\"\" + entry + \"\\\" is not an address or a range first-last\";", ["fuzz"], [], "a rejected list leaves its first entries"),
]

# ---- oracle self-mutations (no rebuild) --------------------------------------------------------------
ENV_MUTANTS = []
for k in "ledger second alias params ret body cases warn preserved claims lent data honour kept tokodd tokoutside tokfirst tokdrop " \
         "tokcall tokunmark tokhover tokslot tokframe tokcopy".split():
    ENV_MUTANTS.append(("E-gen-" + k, {"SE_GENERATED_MUTATE": k, "SE_GENERATED_SEEDS": "300"}, "gen"))
for k in "tokodd tokoutside tokfirst tokdrop tokcall tokunmark tokhover tokslot tokframe tokcopy".split():
    ENV_MUTANTS.append(("E-map-" + k, {"SE_GAME_MAPPING_MUTATE": k}, "corpus"))

# A sensor blinded and the class it is the sensor of brought back (the patch reverted): does any other oracle still see the class?
# (id, patch, host mutant, should, escalate)
COMBOS = []

# The order of the patch sweep (a sample is taken from the front): the patches that fixed P1 rows, then P2 rows, then the
# access ledger and conservation, the allocation-failure and cancel patches, the spec, and the rest in number order.
PRIORITY = ("0068 0072 0076 0066 0065 0063 0060 0058 0057 0056 0054 "
            "0075 0077 0064 0067 0069 0062 0055 0047 0030 "
            "0018 0017 0010 0009 0012 0014 0024 0031 0073 0013 "
            "0001 0023 0032 0040 0003 0004 "
            "0050 0006 0005 0045 0034 0026 0038").split()

# Patches with a known reason no plain Linux ctest run can see them (A10-10, accepted): run, reported apart.
PATCH_NOTES = {
    "0011": "UBSan vptr only (A10-10)",
    "0016": "LeakSanitizer only (A10-10)",
    "0022": "a 32-bit long only (Windows, A10-10)",
    "0074": "se-sleighc under the sanitizers only (A10-1..3)",
}
PATCH_SHOULD = {
    # allocation failure and cancellation: the fault matrix
    "0001": ["fault"], "0003": ["fault"], "0004": ["fault"], "0016": ["fault"], "0023": ["fault"], "0032": ["fault"],
    "0040": ["fault"], "0002": ["fault", "engine"], "0020": ["engine", "unit"], "0047": ["decode", "unit"],
    "0074": ["engine"], "0022": ["engine", "unit"], "0055": ["unit", "engine"],
}
# the rest by the class of the patch
for _n in "0011 0033 0037 0044 0048 0058 0059 0062 0064 0067 0069 0070 0071 0077 0046 0049 0051 0054".split():
    PATCH_SHOULD[_n] = ["unit", "pc"]           # printed values
for _n in "0030 0053 0056 0057 0060 0061 0063 0065 0066 0068 0072 0076".split():
    PATCH_SHOULD[_n] = ["unit", "gen", "corpus"]   # the frame
for _n in "0005 0006 0007 0008 0019 0025 0026 0034 0038 0045 0050".split():
    PATCH_SHOULD[_n] = ["decode", "unit", "gen"]   # the spec
for _n in "0009 0010 0012 0013 0014 0017 0018 0024 0031 0073 0075".split():
    PATCH_SHOULD[_n] = ["unit", "gen", "corpus"]   # accesses and conservation
FAULT_PATCHES = set("0001 0002 0003 0004 0016 0023 0032 0040".split())


def sh(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)


def limit():
    resource.setrlimit(resource.RLIMIT_AS, (3500000 * 1024, 3500000 * 1024))


class Harness:
    def __init__(self, a):
        self.root, self.build, self.rev, self.out = (os.path.abspath(x) for x in (a.root, a.build, a.rev, a.out))
        self.hwram = os.path.abspath(a.hwram) if a.hwram else ""
        os.makedirs(self.out, exist_ok=True)
        self.results = open(os.path.join(self.out, "results.tsv"), "a")
        self.t0 = time.time()

    def clean(self):
        r = sh(["git", "status", "--porcelain", "--", "SaturnExplorer/FrontEnd", "SaturnExplorer/CMakeLists.txt"], cwd=self.root)
        return r.stdout.decode().strip() == ""

    def mark(self, paths):
        with open(os.path.join(self.out, "applied.json"), "w") as f:
            json.dump(sorted(set(paths)), f)

    def recover(self):
        m = os.path.join(self.out, "applied.json")
        if os.path.exists(m):
            paths = json.load(open(m))
            print("a mutant was left applied by an interrupted piece: restoring %s" % " ".join(paths), flush=True)
            sh(["git", "checkout", "--"] + paths, cwd=self.root)
            os.remove(m)

    def restore(self, paths):
        m = os.path.join(self.out, "applied.json")
        if os.path.exists(m):
            os.remove(m)
        if paths:
            sh(["git", "checkout", "--"] + sorted(set(paths)), cwd=self.root)
        if not self.clean():
            # anything else the mutant touched (a regenerated spec file)
            sh(["git", "checkout", "--", "SaturnExplorer/FrontEnd"], cwd=self.root)
        if not self.clean():
            sys.exit("the tree is not clean after a restore: stop")

    def ninja(self, targets, log):
        # the machine's rule: -j2, or -j1 when less than 4000 MB is available (two agents share it)
        avail = 0
        for line in open("/proc/meminfo"):
            if line.startswith("MemAvailable:"):
                avail = int(line.split()[1]) // 1024
        r = sh(["ninja", "-C", self.build, "-j2" if avail >= 4000 else "-j1"] + sorted(set(targets)))
        with open(log, "ab") as f:
            f.write(r.stdout)
        return r.returncode == 0

    def run_oracle(self, mid, name, extra_env=None, inverted=False):
        target, argv, env, tmo = ORACLES[name]
        scratch = os.path.join(self.build, "a11_scratch_" + name)
        e = dict(os.environ)
        for k, v in env.items():
            e[k] = v.format(hwram=self.hwram)
        if extra_env:
            e.update(extra_env)
        cmd = ["timeout", str(tmo), os.path.join(self.build, "bin", target)] + [x.format(scratch=scratch) for x in argv]
        t = time.time()
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=e, preexec_fn=limit, cwd=self.build)
        dt = time.time() - t
        out = r.stdout.decode(errors="replace")
        with open(os.path.join(self.out, "%s.%s.log" % (mid, name)), "w") as f:
            f.write(out)
        if inverted:
            # SE_GENERATED_MUTATE: the run passes only when the check reported the damage on some seed.
            m = re.search(r"mutation check \((\S+)\): (\d+) of", out)
            verdict = "killed" if r.returncode == 0 and m and int(m.group(2)) > 0 else \
                      "error" if r.returncode in (126, 127) or not m else "survived"
        elif r.returncode in (126, 127) or "usage:" in out[:200]:
            verdict = "error"
        elif r.returncode == 0:
            verdict = "survived"
        elif r.returncode == 77:
            verdict = "skipped"
        elif r.returncode == 124:
            verdict = "killed-timeout"
        else:
            verdict = "killed"
        why = ""
        for line in out.splitlines():
            if re.search(r"FAIL|FINDING|VIOLATION|CHECK|Assertion|FATAL|failed:|mutation check", line) and "0 failed" not in line:
                why = line.strip()[:200]
                break
        self.results.write("%s\t%s\t%s\t%.0f\t%s\n" % (mid, name, verdict, dt, why.replace("\t", " ")))
        self.results.flush()
        print("  %-8s %-14s %5.0fs %s" % (name, verdict, dt, why[:120]), flush=True)
        return verdict.startswith("killed")

    def score(self, mid, should, escalate, targets_of):
        # the mutant as applied, for reading afterwards
        with open(os.path.join(self.out, mid.replace(":", "_") + ".diff"), "wb") as f:
            f.write(sh(["git", "diff", "--", "SaturnExplorer/FrontEnd", ":(exclude)*Sh2SpecData.h", ":(exclude)*.sla"], cwd=self.root).stdout)
        killed = False
        for o in should:
            if not self.ninja([ORACLES[o][0]], os.path.join(self.out, mid + ".build.log")):
                self.results.write("%s\t%s\tstillborn\t0\tbuild failed\n" % (mid, o))
                print("  stillborn (build failed)", flush=True)
                return
            killed |= self.run_oracle(mid, o)
        for o in escalate:
            if killed:
                break
            if o in should:
                continue
            if o == "corpus" and not self.hwram:
                continue
            if not self.ninja([ORACLES[o][0]], os.path.join(self.out, mid + ".build.log")):
                self.results.write("%s\t%s\tstillborn\t0\tbuild failed\n" % (mid, o))
                return
            killed |= self.run_oracle(mid, o)
        if not killed:
            self.results.write("%s\t*\tSURVIVED\t0\t\n" % mid)
            print("  SURVIVED", flush=True)

    # -- the three kinds --
    def host(self, m):
        mid, path, old, new, should, escalate, what = m
        full = os.path.join(self.root, path)
        text = open(full, encoding="utf-8").read()
        if text.count(old) != 1:
            print("%s: the text occurs %d times: not applied" % (mid, text.count(old)), flush=True)
            self.results.write("%s\t-\tnot-applied\t0\ttext occurs %d times\n" % (mid, text.count(old)))
            return
        print("%s: %s" % (mid, what), flush=True)
        self.mark([path])
        open(full, "w", encoding="utf-8").write(text.replace(old, new))
        try:
            if "/processors/" in path and sh(["ninja", "-C", self.build, "-j2", "se-regen-sla"]).returncode != 0:
                self.results.write("%s\t-\tstillborn\t0\tse-regen-sla failed\n" % mid)
                return
            self.score(mid, should, escalate, None)
        finally:
            self.restore([path])

    def patch(self, num, combo=None):
        """combo: (id, host mutant id, should, escalate): the patch reverted and a host mutant applied together."""
        hunks = sorted(f for f in os.listdir(self.rev) if f.startswith(num + ".h") and f.endswith(".rev.patch"))
        mid = combo[0] if combo else "patch:" + num
        if not hunks:
            print("%s: no hunk reverts alone" % mid, flush=True)
            self.results.write("%s\t-\tnot-applied\t0\tno hunk reverts alone (later patches rewrote it)\n" % mid)
            return
        touched, spec, applied = set(), False, 0
        self.mark(["SaturnExplorer/FrontEnd/third_party/ghidra-decompiler", "SaturnExplorer/FrontEnd/src/Decompiler/Sh2SpecData.h"])
        skipped_hh = 0
        for h in hunks:
            p = os.path.join(self.rev, h)
            # Header hunks stay: they are the patch's interface (the host uses it, so reverting them does not build) and a
            # header change rebuilds the whole engine. The mutant is the patch's behaviour, its .cc and spec hunks.
            if any(l.startswith("+++ b/") and l.rstrip().endswith(".hh") for l in open(p)):
                skipped_hh += 1
                continue
            ctx = open(p + ".ctx").read().strip() if os.path.exists(p + ".ctx") else "-C3"
            r = sh(["git", "apply", "-R", "--recount", ctx, p], cwd=self.root)
            if r.returncode != 0:
                continue   # a hunk an earlier hunk of the same patch already moved
            applied += 1
            for line in open(p):
                if line.startswith("+++ b/"):
                    touched.add(line[6:].strip())
                    spec |= "/processors/" in line
        print("%s: %d of %d hunks reverted (%d header hunks kept)%s %s" % (mid, applied, len(hunks), skipped_hh, " (spec)" if spec else "",
                                                                        PATCH_NOTES.get(num, "")), flush=True)
        if not applied:
            self.results.write("%s\t-\tnot-applied\t0\tonly header hunks revert alone\n" % mid)
            self.restore([])
            return
        if combo:
            hm = [m for m in MUTANTS if m[0] == combo[1]][0]
            full = os.path.join(self.root, hm[1])
            text = open(full, encoding="utf-8").read()
            self.mark(["SaturnExplorer/FrontEnd/third_party/ghidra-decompiler", "SaturnExplorer/FrontEnd/src/Decompiler/Sh2SpecData.h", hm[1]])
            open(full, "w", encoding="utf-8").write(text.replace(hm[2], hm[3]))
            touched.add(hm[1])
            print("  with %s: %s" % (hm[0], hm[6]), flush=True)
        try:
            if spec:
                r = sh(["ninja", "-C", self.build, "-j2", "se-regen-sla"])
                if r.returncode != 0:
                    self.results.write("%s\t-\tstillborn\t0\tse-regen-sla failed\n" % mid)
                    return
            if combo:
                self.score(mid, combo[2], combo[3], None)
            else:
                should = PATCH_SHOULD.get(num, ["unit", "gen"])
                self.score(mid, should, ENGINE_ESCALATE + (["fault"] if num in FAULT_PATCHES else []), None)
        finally:
            self.restore(list(touched) + ["SaturnExplorer/FrontEnd/src/Decompiler/Sh2SpecData.h",
                                          "SaturnExplorer/FrontEnd/third_party/ghidra-decompiler/processors/SuperH/sh-2.sla"])

    def combo(self, m):
        self.patch(m[1], (m[0], m[2], m[3], m[4]))

    def env(self, m):
        mid, env, oracle = m
        print("%s: %s" % (mid, env), flush=True)
        if oracle == "corpus" and not self.hwram:
            return
        self.ninja([ORACLES[oracle][0]], os.path.join(self.out, "env.build.log"))
        if not self.run_oracle(mid, oracle, env, inverted="SE_GENERATED_MUTATE" in env):
            self.results.write("%s\t*\tSURVIVED\t0\t\n" % mid)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", required=True)
    ap.add_argument("--build", required=True)
    ap.add_argument("--rev", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--hwram", default="")
    ap.add_argument("--only", default="")
    ap.add_argument("--group", default="all")
    ap.add_argument("--minutes", type=float, default=20)
    ap.add_argument("--list", action="store_true")
    a = ap.parse_args()
    patches = sorted({f[:4] for f in os.listdir(a.rev) if f.endswith(".rev.patch")} |
                     {f[:4] for f in os.listdir(os.path.join(a.root, "SaturnExplorer/FrontEnd/third_party/ghidra-decompiler/patches")) if f.endswith(".patch")})
    todo = []
    if a.group in ("all", "env"):
        todo += [("env", m[0], m) for m in ENV_MUTANTS]
    if a.group in ("all", "host"):
        todo += [("host", m[0], m) for m in MUTANTS]
    if a.group in ("all", "combo"):
        todo += [("combo", m[0], m) for m in COMBOS]
    if a.group in ("all", "patch"):
        first = [n for n in PRIORITY if n in patches]
        todo += [("patch", "patch:" + n, n) for n in first + [n for n in patches if n not in first]]
    if a.only:
        want = set(a.only.split(","))
        todo = [t for t in todo if t[1] in want]
    if a.list:
        for t in todo:
            print(t[1])
        return
    # Resumable: a mutant with a row in results.tsv is done (a piece cut by the clock, or a restart, goes on from the next).
    done = set()
    rpath = os.path.join(a.out, "results.tsv")
    if os.path.exists(rpath):
        done = {line.split("\t")[0] for line in open(rpath) if line.strip()}
    todo = [t for t in todo if t[1] not in done]
    if not todo:
        print("nothing left")
        return
    h = Harness(a)
    h.recover()
    if not h.clean():
        sys.exit("the tree is not clean: stop")
    for kind, mid, m in todo:
        if (time.time() - h.t0) / 60 > a.minutes:
            print("stopping: %.0f minutes gone; next would be %s" % ((time.time() - h.t0) / 60, mid), flush=True)
            break
        getattr(h, kind)(m)
    print("piece done in %.1f minutes" % ((time.time() - h.t0) / 60))


if __name__ == "__main__":
    main()
