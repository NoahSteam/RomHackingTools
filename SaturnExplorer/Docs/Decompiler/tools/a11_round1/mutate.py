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
    "0074": ["engine"], "0022": ["engine", "unit"],
}


def sh(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)


def limit():
    resource.setrlimit(resource.RLIMIT_AS, (3500000 * 1024, 3500000 * 1024))


class Harness:
    def __init__(self, a):
        self.root, self.build, self.rev, self.out = a.root, a.build, a.rev, a.out
        self.hwram = a.hwram
        os.makedirs(self.out, exist_ok=True)
        self.results = open(os.path.join(self.out, "results.tsv"), "a")
        self.t0 = time.time()

    def clean(self):
        r = sh(["git", "status", "--porcelain", "--", "SaturnExplorer/FrontEnd", "SaturnExplorer/CMakeLists.txt"], cwd=self.root)
        return r.stdout.decode().strip() == ""

    def restore(self, paths):
        if paths:
            sh(["git", "checkout", "--"] + sorted(set(paths)), cwd=self.root)
        if not self.clean():
            # anything else the mutant touched (a regenerated spec file)
            sh(["git", "checkout", "--", "SaturnExplorer/FrontEnd"], cwd=self.root)
        if not self.clean():
            sys.exit("the tree is not clean after a restore: stop")

    def ninja(self, targets, log):
        r = sh(["ninja", "-C", self.build, "-j2"] + sorted(set(targets)))
        with open(log, "ab") as f:
            f.write(r.stdout)
        return r.returncode == 0

    def run_oracle(self, mid, name, extra_env=None):
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
        if r.returncode == 0:
            verdict = "survived"
        elif r.returncode == 77:
            verdict = "skipped"
        elif r.returncode == 124:
            verdict = "killed-timeout"
        else:
            verdict = "killed"
        why = ""
        for line in out.splitlines():
            if re.search(r"FAIL|FINDING|VIOLATION|CHECK|Assertion|FATAL|failed:", line) and "0 failed" not in line:
                why = line.strip()[:200]
                break
        self.results.write("%s\t%s\t%s\t%.0f\t%s\n" % (mid, name, verdict, dt, why.replace("\t", " ")))
        self.results.flush()
        print("  %-8s %-14s %5.0fs %s" % (name, verdict, dt, why[:120]), flush=True)
        return verdict.startswith("killed")

    def score(self, mid, should, escalate, targets_of):
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
        open(full, "w", encoding="utf-8").write(text.replace(old, new))
        try:
            self.score(mid, should, escalate, None)
        finally:
            self.restore([path])

    def patch(self, num):
        hunks = sorted(f for f in os.listdir(self.rev) if f.startswith(num + ".h") and f.endswith(".rev.patch"))
        mid = "patch:" + num
        if not hunks:
            print("%s: no hunk reverts alone" % mid, flush=True)
            self.results.write("%s\t-\tnot-applied\t0\tno hunk reverts alone (later patches rewrote it)\n" % mid)
            return
        touched, spec, applied = set(), False, 0
        for h in hunks:
            p = os.path.join(self.rev, h)
            ctx = open(p + ".ctx").read().strip() if os.path.exists(p + ".ctx") else "-C3"
            r = sh(["git", "apply", "-R", "--recount", ctx, p], cwd=self.root)
            if r.returncode != 0:
                continue   # a hunk an earlier hunk of the same patch already moved
            applied += 1
            for line in open(p):
                if line.startswith("+++ b/"):
                    touched.add(line[6:].strip())
                    spec |= "/processors/" in line
        print("%s: %d of %d hunks reverted%s %s" % (mid, applied, len(hunks), " (spec)" if spec else "", PATCH_NOTES.get(num, "")), flush=True)
        try:
            if spec:
                r = sh(["ninja", "-C", self.build, "-j2", "se-regen-sla"])
                if r.returncode != 0:
                    self.results.write("%s\t-\tstillborn\t0\tse-regen-sla failed\n" % mid)
                    return
            should = PATCH_SHOULD.get(num, ["unit", "gen"])
            self.score(mid, should, ENGINE_ESCALATE + (["fault"] if num in PATCH_SHOULD else []), None)
        finally:
            self.restore(list(touched) + ["SaturnExplorer/FrontEnd/src/Decompiler/Sh2SpecData.h",
                                          "SaturnExplorer/FrontEnd/third_party/ghidra-decompiler/processors/SuperH/sh-2.sla"])

    def env(self, m):
        mid, env, oracle = m
        print("%s: %s" % (mid, env), flush=True)
        if oracle == "corpus" and not self.hwram:
            return
        self.ninja([ORACLES[oracle][0]], os.path.join(self.out, "env.build.log"))
        if not self.run_oracle(mid, oracle, env):
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
    if a.group in ("all", "patch"):
        todo += [("patch", "patch:" + n, n) for n in patches]
    if a.only:
        want = set(a.only.split(","))
        todo = [t for t in todo if t[1] in want]
    if a.list:
        for t in todo:
            print(t[1])
        return
    h = Harness(a)
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
