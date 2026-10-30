#!/usr/bin/env python3
"""Area 10 gate: the vendored Ghidra decompiler is pinned upstream plus patches/, and says so.

    python3 -I check-vendored-tree.py lint     VENDOR_DIR [--tests-dir DIR]
    python3 -I check-vendored-tree.py rebuild  VENDOR_DIR GHIDRA_CHECKOUT WORK_DIR
    python3 -I check-vendored-tree.py check    VENDOR_DIR GHIDRA_CHECKOUT WORK_DIR [--tests-dir DIR]
    python3 -I check-vendored-tree.py selftest VENDOR_DIR GHIDRA_CHECKOUT WORK_DIR

VENDOR_DIR is SaturnExplorer/FrontEnd/third_party/ghidra-decompiler. Exit status 0 when every
check passes, 1 otherwise (the errors are printed, one per line).

lint     The bookkeeping, which needs no upstream checkout:
           * patch names are NNNN-name.patch, strictly increasing, and the numbers missing
             below the highest are exactly the ones patches/README.md lists on its
             "Unused numbers:" line (a "Reserved numbers:" line names numbers branches in
             flight will use: they may be missing or present);
           * every patch has one row in patches/README.md (three cells, none empty: why, and
             what pins it) and one in VENDOR.md, in ascending order, and no row names a patch
             that does not exist;
           * every test function the README names (`TestXxx`) and every test source it names
             (`Sh2DecompilerTests`) exists, when --tests-dir is given;
           * every vendored file a patch changes carries "Modified for SaturnExplorer" in its
             licence header (Apache-2.0 section 4(b)), and no file carries it that no patch
             changes;
           * PINNED_COMMIT in vendor.py is the commit VENDOR.md names.
rebuild  Runs vendor.py on a copy of VENDOR_DIR against GHIDRA_CHECKOUT (which must be at the
         pinned commit; vendor.py refuses otherwise) and fails on any difference between the copy
         and VENDOR_DIR: a file whose content differs, a file only on one side (also under
         processors/, which vendor.py does not clean), or a different executable bit.
         processors/SuperH/sh-2.sla is a build product vendor.py does not make (the SLEIGH
         freshness test covers its content); only its presence is checked. The unified diff goes
         to WORK_DIR/vendor.diff.
check    lint, then rebuild.
selftest Runs `check` on an unmodified copy (must pass) and on copies each carrying one defect
         (must fail, with the expected kind of error), so a gate that has stopped gating is
         itself a red job.
"""
import os
import re
import shutil
import stat
import subprocess
import sys

sys.dont_write_bytecode = True    # nothing here may leave a __pycache__ next to vendor.py

NOTICE = "Modified for SaturnExplorer"
PATCH_RE = re.compile(r"^(\d{4})-[a-z0-9][a-z0-9-]*\.patch$")
# ctest names that are a mode of another test binary (Sh2GeneratedTests with SE_GENERATED_PRINTEDC set)
ALIASES = {"Sh2PrintedCTests": "Sh2GeneratedTests"}
UPSTREAM_CPP = "Ghidra/Features/Decompiler/src/decompile/cpp/"
UPSTREAM_SPEC = "Ghidra/Processors/SuperH/data/languages/"


def split_cells(line):
    """The cells of a markdown table row, splitting on | that is not escaped as \\|."""
    parts = re.split(r"(?<!\\)\|", line.rstrip("\n"))
    return [p.strip() for p in parts[1:-1]] if len(parts) >= 3 else []


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def patch_files(vend):
    d = os.path.join(vend, "patches")
    return sorted(p for p in os.listdir(d) if p.endswith(".patch"))


def local_path(upstream_path):
    if upstream_path.startswith(UPSTREAM_CPP):
        return "cpp/" + upstream_path[len(UPSTREAM_CPP):]
    if upstream_path.startswith(UPSTREAM_SPEC):
        return "processors/SuperH/" + upstream_path[len(UPSTREAM_SPEC):]
    return None


def table_rows(path):
    """patch name -> (position, cells) for the rows that start with a `NNNN-name.patch` cell."""
    rows, order = {}, []
    for line in read(path).split("\n"):
        m = re.match(r"^\| `(\d{4}-[^`]+\.patch)` \|", line)
        if m:
            name = m.group(1)
            if name in rows:
                rows[name].append(split_cells(line))
            else:
                rows[name] = [split_cells(line)]
            order.append(name)
    return rows, order


def lint(vend, tests_dir=None):
    errs = []
    patches = patch_files(vend)
    nums = []
    for p in patches:
        m = PATCH_RE.match(p)
        if not m:
            errs.append("patches/%s: not named NNNN-name.patch" % p)
            continue
        nums.append(int(m.group(1)))
    if len(set(nums)) != len(nums):
        errs.append("patches/: a patch number is used twice")
    readme_path = os.path.join(vend, "patches", "README.md")
    vendor_md = os.path.join(vend, "VENDOR.md")
    readme = read(readme_path)

    # numbering: the gaps below the highest number are declared
    m = re.search(r"^Unused numbers: ([0-9, ]+)\.?\s*$", readme, re.M)
    declared = set(int(x) for x in re.findall(r"\d{4}", m.group(1))) if m else set()
    if not m:
        errs.append('patches/README.md: no "Unused numbers: NNNN, NNNN." line')
    mr = re.search(r"^Reserved numbers: ([0-9, ]+)\.?\s*$", readme, re.M)
    reserved = set(int(x) for x in re.findall(r"\d{4}", mr.group(1))) if mr else set()
    if nums:
        gaps = set(range(1, max(nums) + 1)) - set(nums)
        for g in sorted(gaps - declared - reserved):
            errs.append("patches/: number %04d is missing and README.md does not list it under Unused numbers" % g)
        for g in sorted(declared - gaps):
            errs.append("patches/README.md: %04d is listed as unused but a patch uses it (or it is above the highest)" % g)

    # the two tables
    for label, path, ncells in (("patches/README.md", readme_path, 3), ("VENDOR.md", vendor_md, 2)):
        rows, order = table_rows(path)
        for p in patches:
            if p not in rows:
                errs.append("%s: no row for %s" % (label, p))
            elif len(rows[p]) > 1:
                errs.append("%s: %d rows for %s" % (label, len(rows[p]), p))
        for name in rows:
            if name not in patches:
                errs.append("%s: row for %s, which is not in patches/" % (label, name))
        nums_in_order = [int(n[:4]) for n in order]
        if nums_in_order != sorted(nums_in_order):
            bad = [order[i] for i in range(1, len(order)) if order[i] < order[i - 1]]
            errs.append("%s: rows out of order (first out of place: %s)" % (label, ", ".join(b[:4] for b in bad)))
        for name, rl in rows.items():
            for cells in rl:
                if len(cells) != ncells:
                    errs.append("%s: row %s has %d cells, the table has %d" % (label, name[:4], len(cells), ncells))
                elif any(not c for c in cells):
                    errs.append("%s: row %s has an empty cell" % (label, name[:4]))
                elif label.endswith("README.md"):
                    if not re.search(r"`[A-Za-z0-9_./-]+`", cells[2]):
                        errs.append("%s: row %s names nothing in backticks as what pins it" % (label, name[:4]))

    # named tests exist
    if tests_dir:
        blob, sources = "", set()
        for fn in os.listdir(tests_dir):
            fp = os.path.join(tests_dir, fn)
            if os.path.isfile(fp):
                blob += read(fp) + "\n"
                sources.add(os.path.splitext(fn)[0])
        rows, _ = table_rows(readme_path)
        for name, rl in rows.items():
            for cells in rl:
                if len(cells) != 3:
                    continue
                for ident in sorted(set(re.findall(r"`((?:Test|Check)[A-Za-z0-9_]+)`", cells[2]))):
                    if re.search(r"\b%s\b" % re.escape(ident), blob) is None:
                        errs.append("patches/README.md: row %s names test %s, which is in no file of %s" % (name[:4], ident, tests_dir))
                for src in sorted(set(re.findall(r"`([A-Za-z0-9]+Tests)`", cells[2]))):
                    base = src[len("SaturnExplorer"):] if src.startswith("SaturnExplorer") else src
                    base = ALIASES.get(base, base)
                    if base not in sources:
                        errs.append("patches/README.md: row %s names %s, which has no source in %s" % (name[:4], src, tests_dir))

    # the notice
    touched = set()
    for p in patches:
        for line in read(os.path.join(vend, "patches", p)).split("\n"):
            m = re.match(r"^diff --git a/(\S+) b/(\S+)$", line)
            if m:
                lp = local_path(m.group(2))
                if lp is None:
                    errs.append("patches/%s: changes %s, outside the two upstream directories vendor.py copies" % (p, m.group(2)))
                else:
                    touched.add(lp)
    for lp in sorted(touched):
        fp = os.path.join(vend, lp)
        if not os.path.isfile(fp):
            errs.append("%s: changed by a patch and missing" % lp)
        elif NOTICE not in read(fp)[:4096]:
            errs.append('%s: changed by a patch and carries no "%s" notice in its header' % (lp, NOTICE))
    for sub in ("cpp", "processors/SuperH"):
        d = os.path.join(vend, sub)
        if not os.path.isdir(d):
            errs.append("%s: missing" % sub)
            continue
        for fn in sorted(os.listdir(d)):
            fp = os.path.join(d, fn)
            if os.path.isfile(fp) and not fn.endswith(".sla") and (sub + "/" + fn) not in touched:
                if NOTICE in read(fp)[:4096]:
                    errs.append('%s/%s: carries a "%s" notice, but no patch changes it' % (sub, fn, NOTICE))

    # the pin
    vp = read(os.path.join(vend, "vendor.py"))
    m = re.search(r'^PINNED_COMMIT = "([0-9a-f]{40})"', vp, re.M)
    if not m:
        errs.append("vendor.py: no PINNED_COMMIT")
    elif m.group(1) not in read(vendor_md):
        errs.append("VENDOR.md does not name the pinned commit %s" % m.group(1))
    return errs


def tree_files(root):
    out = {}
    for dp, dn, fn in os.walk(root):
        for f in fn:
            full = os.path.join(dp, f)
            out[os.path.relpath(full, root)] = full
    return out


ROOT_FILES = {".gitattributes", "CMakeLists.txt", "LICENSE", "NOTICE", "SaturnExplorerGhidraDecomp.vcxproj", "VENDOR.md",
              "sources.cmake", "vendor.py"}


def _spec_files(vend):
    """SPEC_FILES of the vendor.py in vend, read without importing it."""
    m = re.search(r"^SPEC_FILES = \[([^\]]*)\]", read(os.path.join(vend, "vendor.py")), re.M)
    return re.findall(r'"([^"]+)"', m.group(1)) if m else []


def rebuild(vend, ghidra, work):
    errs = []
    os.makedirs(work, exist_ok=True)
    rebuilt = os.path.join(work, "rebuilt")
    if os.path.exists(rebuilt):
        shutil.rmtree(rebuilt)
    shutil.copytree(vend, rebuilt, symlinks=True)
    r = subprocess.run([sys.executable, "-I", os.path.join(rebuilt, "vendor.py"), ghidra],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if r.returncode != 0:
        errs.append("vendor.py failed (exit %d):\n%s" % (r.returncode, r.stdout.decode(errors="replace")[-4000:]))
        return errs
    diff_path = os.path.join(work, "vendor.diff")
    d = subprocess.run(["diff", "-r", "-u", "-x", "sh-2.sla", rebuilt, vend], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    with open(diff_path, "wb") as f:
        f.write(d.stdout)
    if d.returncode != 0:
        errs.append("the committed tree differs from vendor.py's output (left side of %s is the rebuilt tree):\n%s"
                    % (diff_path, d.stdout.decode(errors="replace")[:6000]))
    # vendor.py replaces cpp/ and the five spec files but cleans nothing else, so a file left or
    # added anywhere else survives into the copy and the diff cannot see it: list what may exist.
    spec_files = _spec_files(rebuilt)
    allowed = {"processors": None, "cpp": None, "patches": None}
    for name in sorted(os.listdir(vend)):
        full = os.path.join(vend, name)
        if os.path.isdir(full):
            if name not in allowed:
                errs.append("%s/: not part of the vendored tree" % name)
        elif name not in ROOT_FILES:
            errs.append("%s: not part of the vendored tree (a file the vendor directory may hold is listed in ROOT_FILES of check-vendored-tree.py)" % name)
    sup = os.path.join(vend, "processors")
    if os.path.isdir(sup):
        if sorted(os.listdir(sup)) != ["SuperH"]:
            errs.append("processors/: holds %s, expected only SuperH" % sorted(os.listdir(sup)))
        else:
            have = sorted(os.listdir(os.path.join(sup, "SuperH")))
            want = sorted(list(spec_files) + ["sh-2.sla"])
            for f in sorted(set(have) - set(want)):
                errs.append("processors/SuperH/%s: not one of the files vendor.py copies (or sh-2.sla)" % f)
            for f in sorted(set(want) - set(have)):
                errs.append("processors/SuperH/%s: missing" % f)
    for f in sorted(os.listdir(os.path.join(vend, "patches"))):
        if f != "README.md" and not PATCH_RE.match(f):
            errs.append("patches/%s: neither a patch nor the README" % f)
    a, b = tree_files(rebuilt), tree_files(vend)
    for rel in sorted(set(a) | set(b)):
        if rel.endswith("processors/SuperH/sh-2.sla") or rel == os.path.join("processors", "SuperH", "sh-2.sla"):
            if rel not in b or os.path.getsize(b[rel]) == 0:
                errs.append("%s: missing or empty" % rel)
            continue
        if rel in a and rel in b:
            ma, mb = os.lstat(a[rel]).st_mode, os.lstat(b[rel]).st_mode
            if stat.S_ISLNK(mb):
                errs.append("%s: is a symbolic link" % rel)
            elif (ma & 0o111 != 0) != (mb & 0o111 != 0):
                errs.append("%s: executable bit differs from vendor.py's output" % rel)
    return errs


def check(vend, ghidra, work, tests_dir=None):
    return lint(vend, tests_dir) + rebuild(vend, ghidra, work)


# ---------------------------------------------------------------------------------------------
# selftest

def _edit(path, fn):
    s = read(path)
    t = fn(s)
    assert t != s, "mutation changed nothing in %s" % path
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(t)


def _drop_row(path, patch_prefix):
    _edit(path, lambda s: "".join(l for l in s.splitlines(True) if not l.startswith("| `%s-" % patch_prefix)))


def mutations(v):
    """(name, mutate(copy_dir), substring every mutated copy's errors must contain)."""
    last = patch_files(v)[-1]
    first = patch_files(v)[0]
    lastn = last[:4]

    def p(*a):
        return os.path.join(*a)

    def edit_vendored_file(d):
        _edit(p(d, "cpp", "action.cc"), lambda s: s + "\n// hand edit\n")

    def drop_last_patch(d):
        os.remove(p(d, "patches", last))

    def change_a_patch_line(d):
        # the last hunk of the last patch: change one added line (the patch still applies)
        def g(s):
            lines = s.split("\n")
            for k in range(len(lines) - 1, 0, -1):
                if lines[k].startswith("+") and not lines[k].startswith("+++") and lines[k].strip() != "+":
                    lines[k] = lines[k] + " // x"
                    return "\n".join(lines)
            raise AssertionError("no added line")
        _edit(p(d, "patches", last), g)

    def extra_cpp(d):
        with open(p(d, "cpp", "zz_extra.cc"), "w") as f:
            f.write("int x;\n")

    def extra_spec(d):
        with open(p(d, "processors", "SuperH", "zz_extra.sinc"), "w") as f:
            f.write("# x\n")

    def drop_readme_row(d):
        _drop_row(p(d, "patches", "README.md"), first[:4])

    def drop_vendor_row(d):
        _drop_row(p(d, "VENDOR.md"), first[:4])

    def empty_pinned(d):
        def f(s):
            out = []
            for l in s.splitlines(True):
                if l.startswith("| `%s-" % first[:4]):
                    cells = re.split(r"(?<!\\)\|", l.rstrip("\n"))
                    cells[3] = " "
                    l = "|".join(cells) + "\n"
                out.append(l)
            return "".join(out)
        _edit(p(d, "patches", "README.md"), f)

    def two_cell_row(d):
        def f(s):
            out = []
            for l in s.splitlines(True):
                if l.startswith("| `%s-" % lastn):
                    cells = re.split(r"(?<!\\)\|", l.rstrip("\n"))
                    l = "|".join(cells[:3]) + "|\n"
                out.append(l)
            return "".join(out)
        _edit(p(d, "patches", "README.md"), f)

    def strip_notice(d):
        _edit(p(d, "cpp", "action.cc"), lambda s: re.sub(r"\n \* Modified for SaturnExplorer[^\n]*", "", s))

    def stale_notice(d):
        # a file no patch changes claims to be modified
        _edit(p(d, "cpp", "xml.cc"), lambda s: s.replace(" */", " *\n * Modified for SaturnExplorer: hand edit.\n */", 1))

    def edit_sources_cmake(d):
        _edit(p(d, "sources.cmake"), lambda s: s + "# hand edit\n")

    def renumber_gap(d):
        new = "%04d" % (int(lastn) + 2) + last[4:]
        os.rename(p(d, "patches", last), p(d, "patches", new))
        for f in (p(d, "patches", "README.md"), p(d, "VENDOR.md")):
            _edit(f, lambda s: s.replace(last, new))

    def used_number_listed_unused(d):
        _edit(p(d, "patches", "README.md"), lambda s: re.sub(r"(Unused numbers: )", r"\g<1>0001, ", s, count=1))

    def exec_bit(d):
        os.chmod(p(d, "cpp", "action.cc"), 0o755)

    def pin_mismatch(d):
        _edit(p(d, "VENDOR.md"), lambda s: s.replace(re.search(r'PINNED_COMMIT = "([0-9a-f]{40})"', read(p(d, "vendor.py"))).group(1), "0" * 40))

    return [
        ("a vendored file edited by hand", edit_vendored_file, "differs from vendor.py"),
        ("the last patch removed", drop_last_patch, "differs from vendor.py"),
        ("a line of the last patch changed", change_a_patch_line, "differs from vendor.py"),
        ("an extra file in cpp/", extra_cpp, "zz_extra.cc"),
        ("an extra file in processors/SuperH/", extra_spec, "zz_extra.sinc"),
        ("a stale sources.cmake", edit_sources_cmake, "differs from vendor.py"),
        ("the notice removed from a patched file", strip_notice, "carries no \"%s\" notice" % NOTICE),
        ("a notice on a file no patch changes", stale_notice, "no patch changes it"),
        ("the README row of a patch removed", drop_readme_row, "patches/README.md: no row for"),
        ("the VENDOR.md row of a patch removed", drop_vendor_row, "VENDOR.md: no row for"),
        ("an empty Pinned by cell", empty_pinned, "empty cell"),
        ("a README row with two cells", two_cell_row, "has 2 cells"),
        ("a patch number skipped without being declared", renumber_gap, "does not list it under Unused numbers"),
        ("a used number declared unused", used_number_listed_unused, "listed as unused"),
        ("an executable bit changed", exec_bit, "executable bit differs"),
        ("VENDOR.md names another commit", pin_mismatch, "does not name the pinned commit"),
    ]


def selftest(vend, ghidra, work, tests_dir=None):
    bad = []
    if os.path.exists(work):
        shutil.rmtree(work)
    os.makedirs(work)
    ctrl = os.path.join(work, "control")
    shutil.copytree(vend, os.path.join(ctrl, "v"), symlinks=True)
    errs = check(os.path.join(ctrl, "v"), ghidra, os.path.join(ctrl, "w"), tests_dir)
    if errs:
        bad.append("control (an unmodified copy) fails:\n  " + "\n  ".join(e.split("\n")[0] for e in errs))
    else:
        print("ok    control: an unmodified copy passes")
    for i, (name, mutate, expect) in enumerate(mutations(vend)):
        base = os.path.join(work, "m%02d" % i)
        copy = os.path.join(base, "v")
        shutil.copytree(vend, copy, symlinks=True)
        mutate(copy)
        errs = check(copy, ghidra, os.path.join(base, "w"), tests_dir)
        if not errs:
            bad.append("%s: the check PASSED a tree with this defect" % name)
        elif not any(expect in e for e in errs):
            bad.append("%s: failed, but not with %r; first error: %s" % (name, expect, errs[0].split("\n")[0][:200]))
        else:
            print("ok    %s: rejected (%r)" % (name, expect))
        shutil.rmtree(base)
    for b in bad:
        print("FAIL  " + b)
    return bad


def main(argv):
    if len(argv) < 3 or argv[1] not in ("lint", "rebuild", "check", "selftest"):
        sys.stderr.write(__doc__)
        return 2
    args, tests_dir = [], None
    it = iter(argv[2:])
    for a in it:
        if a == "--tests-dir":
            tests_dir = next(it)
        else:
            args.append(a)
    cmd = argv[1]
    vend = os.path.abspath(args[0])
    if cmd == "lint":
        errs = lint(vend, tests_dir)
    elif cmd == "rebuild":
        errs = rebuild(vend, os.path.abspath(args[1]), os.path.abspath(args[2]))
    elif cmd == "check":
        errs = check(vend, os.path.abspath(args[1]), os.path.abspath(args[2]), tests_dir)
    else:
        errs = selftest(vend, os.path.abspath(args[1]), os.path.abspath(args[2]), tests_dir)
    for e in errs:
        print("error: " + e)
    if not errs:
        print("vendored tree: ok (%s)" % cmd)
    return 1 if errs else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
