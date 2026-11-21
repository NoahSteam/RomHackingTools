#!/usr/bin/env python3
"""Area 10 round 4: revert a patch's effect (all of its code hunks, or chosen hunks = one route) in the vendored tree,
build, run the tests the README names for it, restore, and record which test functions failed.

usage: mut.py <repo root> <build dir> <hunk dir (patch_mutants.py output)> <out dir> <jobs.json> [--only id,id]

jobs.json (a job may carry "edits": [[path under cpp/, old text, new text]] with "hunks": "edits" or [] for a hand-made revert):
           [{"id": "0009", "patch": "0009", "hunks": "code" | [3, 4], "named": ["TestHardwareVolatility"],
             "runs": ["unit", "gen", "pc", "engine", "decode", "fault"]}]
  hunks "code": every hunk that reverts alone, except those in .hh files (a header hunk is the patch's interface; the
  host uses it, and a header change rebuilds the whole engine), as area 11 round 2's harness did.
A mutant's tests run beside the next mutant's build (binaries are copied first). A test run is one process under
ulimit -v 3500000; the oracles after the first run only while the named tests have not failed.
Results: <out>/results.jsonl (one line per mutant), <out>/<id>.<oracle>.log.
"""
import concurrent.futures as cf
import json
import os
import re
import resource
import shutil
import subprocess
import sys
import time

ORACLES = {
    "unit": ("SaturnExplorerSh2DecompilerTests", {}, "Sh2DecompilerTests.cpp", 600),
    "gen": ("SaturnExplorerSh2GeneratedTests", {}, "Sh2GeneratedTests.cpp", 600),
    "pc": ("SaturnExplorerSh2GeneratedTests", {"SE_GENERATED_PRINTEDC": "only"}, "Sh2GeneratedTests.cpp", 600),
    "engine": ("SaturnExplorerSh2DecompilerEngineTests", {}, "Sh2DecompilerEngineTests.cpp", 600),
    "decode": ("SaturnExplorerSh2DecodeTests", {}, "Sh2DecodeTests.cpp", 1200),
    "sem": ("SaturnExplorerSh2SemanticsTests", {}, "Sh2SemanticsTests.cpp", 1200),
    "fault": ("SaturnExplorerDecompilerFaultTests", {"SE_FAULT_JOBS": "2"}, "DecompilerFaultTests.cpp", 1800),
    "faultfull": ("SaturnExplorerDecompilerFaultTests", {"SE_FAULT_JOBS": "2", "SE_FAULT_FULL": "1"}, "DecompilerFaultTests.cpp", 3000),
    "runner": ("SaturnExplorerDecompilerRunnerTests", {}, "DecompilerRunnerTests.cpp", 600),
    # No "fresh" oracle: SlaFreshnessTests takes <se-sleighc> <scratch>, not the <scratch> run_one passes, and the
    # snapshot holds no se-sleighc, so it would fail on setup and read as a kill. No round-6 job used it.
}
VEN = "SaturnExplorer/FrontEnd/third_party/ghidra-decompiler"
SPECH = "SaturnExplorer/FrontEnd/src/Decompiler/Sh2SpecData.h"


def sh(cmd, **kw):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, **kw)


def lim():
    resource.setrlimit(resource.RLIMIT_AS, (3500000 * 1024, 3500000 * 1024))


def funcmap(src):
    starts = []
    for i, l in enumerate(open(src, encoding="utf-8", errors="replace"), 1):
        m = re.match(r"^(?:static )?(?:void|bool|int|std::string|double) (\w+)\(", l)
        if m:
            starts.append((i, m.group(1)))
    return starts


def func_at(starts, line):
    best = "?"
    for i, n in starts:
        if i <= line:
            best = n
        else:
            break
    return best


class H:
    def __init__(self, root, build, hunks, out):
        self.root, self.build, self.hunks, self.out = root, build, hunks, out
        os.makedirs(out, exist_ok=True)
        self.pool = cf.ThreadPoolExecutor(max_workers=1)
        self.futs = []

    def clean(self):
        return sh(["git", "status", "--porcelain", "--", "SaturnExplorer/FrontEnd/third_party", SPECH], cwd=self.root).stdout.decode().strip() == ""

    def restore(self):
        sh(["git", "checkout", "--", VEN, SPECH], cwd=self.root)
        if not self.clean():
            sys.exit("tree not clean after restore")

    def build_targets(self, targets, log):
        r = sh(["cmake", "--build", self.build, "-j2", "--target"] + sorted(targets))
        open(log, "ab").write(r.stdout)
        return r.returncode == 0

    def apply(self, num, sel):
        files = sorted(f for f in os.listdir(self.hunks) if f.startswith(num + ".h") and f.endswith(".rev.patch"))
        applied, spec = [], False
        for f in files:
            k = int(f[6:8])
            p = os.path.join(self.hunks, f)
            txt = open(p).read()
            hh = any(l.startswith("+++ b/") and l.rstrip().endswith(".hh") for l in txt.splitlines())
            if sel == "code":
                if hh:
                    continue
            elif k not in sel:
                continue
            ctx = open(p + ".ctx").read().strip() if os.path.exists(p + ".ctx") else "-C3"
            r = sh(["git", "apply", "-R", "--recount", ctx, p], cwd=self.root)
            if r.returncode == 0:
                applied.append(k)
                spec |= "/processors/" in txt
        return applied, spec

    def run_one(self, snap, jid, o, named):
        tgt, env, srcname, tmo = ORACLES[o]
        e = dict(os.environ)
        e.update(env)
        scr = os.path.join(snap, "scr_" + o)
        t = time.time()
        try:
            r = subprocess.run(["timeout", str(tmo), os.path.join(snap, tgt), scr], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, env=e, preexec_fn=lim, cwd=snap)
            rc, out = r.returncode, r.stdout.decode(errors="replace")
        except Exception as ex:
            rc, out = -1, str(ex)
        dt = time.time() - t
        open(os.path.join(self.out, "%s.%s.log" % (jid, o)), "w").write(out)
        sp = os.path.join(snap, srcname)
        starts = funcmap(sp) if os.path.exists(sp) else []
        fns = {}
        for m in re.finditer(r"CHECK failed at (?:\S*[/\\])?(\S+?\.cpp):(\d+)", out):
            if m.group(1) == srcname:
                n = func_at(starts, int(m.group(2)))
            else:
                n = m.group(1)
            fns[n] = fns.get(n, 0) + 1
        for m in re.finditer(r"CHECK failed at line (\d+)", out):
            n = func_at(starts, int(m.group(1)))
            fns[n] = fns.get(n, 0) + 1
        findings = re.findall(r"FINDING \[(\w[\w-]*)\] (.{0,60})", out)
        canary = re.findall(r"CANARY FAILURE.{0,60}", out)
        return {"oracle": o, "rc": rc, "secs": round(dt), "fns": fns, "findings": findings[:12], "nfindings": len(findings),
                "canary": canary[:3], "first": next((l[:160] for l in out.splitlines() if re.search(r"CHECK failed|FAIL|FATAL|ERROR|runtime error|Sanitizer", l)), "")}

    def tests(self, snap, job):
        res = []
        named = job.get("named", [])
        hit = False
        for o in job["runs"]:
            r = self.run_one(snap, job["id"], o, named)
            res.append(r)
            blob = json.dumps([r["fns"], r["findings"], r["canary"], r["first"]])
            if r["rc"] not in (0, 77) and any(n in blob for n in named):
                hit = True
                break
            if r["rc"] not in (0, 77) and not named:   # a per-hunk group (jobs_gen.py): one failing oracle kills it
                break
        rec = {"id": job["id"], "applied": job.get("applied"), "named": named, "named_hit": hit, "runs": res}
        open(os.path.join(self.out, "results.jsonl"), "a").write(json.dumps(rec) + "\n")
        print("%s: applied %s named_hit=%s %s" % (job["id"], job.get("applied"), hit,
              " ".join("%s:%s(rc%d,%s)" % (r["oracle"], "FAIL" if r["rc"] not in (0, 77) else "ok", r["rc"],
                                           ",".join("%s=%d" % kv for kv in list(r["fns"].items())[:6])) for r in res)), flush=True)
        shutil.rmtree(snap, ignore_errors=True)

    def job(self, job):
        jid, num = job["id"], job["patch"]
        sel = job.get("hunks", "code")
        applied, spec = self.apply(num, sel) if sel not in ("edits", "diff") else ([], False)
        if sel == "diff":   # round 6: a whole-tree mutant given as a diff (the stack-escape family without its dependants, replay.py)
            r = sh(["git", "apply", "--recount", job["diff"]], cwd=self.root)
            if r.returncode == 0:
                applied.append("diff:" + os.path.basename(job["diff"]))
                spec = "processors/" in open(job["diff"]).read()
            else:
                print("%s: diff does not apply: %s" % (jid, r.stdout.decode()[:200]), flush=True)
        for path, old, new in job.get("edits", []):   # a hand-made revert: text of a file under cpp/ or processors/
            full = os.path.join(self.root, VEN, path)
            txt = open(full, encoding="utf-8").read()
            if txt.count(old) == 1:
                open(full, "w", encoding="utf-8").write(txt.replace(old, new))
                applied.append("edit:" + path)
                spec |= path.startswith("processors/")
            else:
                print("%s: edit text occurs %d times in %s" % (jid, txt.count(old), path), flush=True)
        job["applied"] = applied
        if not applied:
            sh(["git", "checkout", "--", VEN, SPECH], cwd=self.root)
            open(os.path.join(self.out, "results.jsonl"), "a").write(json.dumps({"id": jid, "applied": [], "note": "nothing reverts"}) + "\n")
            print("%s: nothing reverts" % jid, flush=True)
            return
        log = os.path.join(self.out, jid + ".build.log")
        snap = None
        try:
            if spec and not self.build_targets(["se-regen-sla"], log):
                open(os.path.join(self.out, "results.jsonl"), "a").write(json.dumps({"id": jid, "applied": applied, "note": "regen failed"}) + "\n")
                return
            targets = {ORACLES[o][0] for o in job["runs"]}
            if not self.build_targets(targets, log):
                open(os.path.join(self.out, "results.jsonl"), "a").write(json.dumps({"id": jid, "applied": applied, "note": "build failed"}) + "\n")
                print("%s: build failed" % jid, flush=True)
                return
            snap = os.path.join(self.build, "snap_" + jid)
            shutil.rmtree(snap, ignore_errors=True)
            os.makedirs(snap)
            for t in targets:
                shutil.copy(os.path.join(self.build, "bin", t), snap)
            for o in job["runs"]:
                s = os.path.join(self.root, "SaturnExplorer/FrontEnd/tests", ORACLES[o][2])
                if os.path.exists(s):
                    shutil.copy(s, snap)
        finally:
            self.restore()
        if snap:
            self.futs.append(self.pool.submit(self.tests, snap, job))
            # one test run at a time: wait while a run is still going before the next build starts its own
            while sum(1 for f in self.futs if not f.done()) > 1:
                time.sleep(2)


def main():
    root, build, hunks, out, jobs = (os.path.abspath(x) for x in sys.argv[1:6])
    only = sys.argv[sys.argv.index("--only") + 1].split(",") if "--only" in sys.argv else None
    h = H(root, build, hunks, out)
    h.restore()
    for job in json.load(open(jobs)):
        if only and job["id"] not in only:
            continue
        h.job(job)
    for f in h.futs:
        f.result()


if __name__ == "__main__":
    main()
