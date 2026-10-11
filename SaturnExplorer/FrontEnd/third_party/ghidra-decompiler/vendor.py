#!/usr/bin/env python3
"""Copy Ghidra's native decompiler and the SuperH SLEIGH spec into this directory.

    python3 -I vendor.py <ghidra-checkout> [--allow-other-commit]

The checkout only needs the decompiler sources and the SuperH languages (a sparse clone is
enough; see VENDOR.md). Everything under cpp/ and the five spec files are replaced, the
patches in patches/ are applied in name order, and sources.cmake is rewritten. Any patch
that does not apply exactly stops the script with a non-zero exit and leaves this directory
untouched, because a silently skipped patch would ship an engine without the behaviour the
host relies on (cancellation, PLAN.md A4).

processors/SuperH/sh-2.sla is not touched: it is a build product of the .slaspec, and the
freshness test fails until it is regenerated with the se-regen-sla target.
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

PINNED_COMMIT = "918d44ebc2962a53d23eeee52b8f44c7816e7815"   # Ghidra 12.3 (DEV), master

HERE = os.path.dirname(os.path.abspath(__file__))
UPSTREAM_CPP = "Ghidra/Features/Decompiler/src/decompile/cpp"
UPSTREAM_SPEC = "Ghidra/Processors/SuperH/data/languages"

# The Makefile's CORE + DECCORE + SLEIGH groups, plus the SleighArchitecture glue and the
# library entry point. Not the ghidra_* files (the Java-process bridge), BFD, the XML/raw
# load images, the console or the unit tests.
CORE = "xml marshal space float address pcoderaw translate opcodes metatype globalcontext"
DECCORE = ("capability architecture options graph cover block cast typeop database cpool "
           "comment stringmanage modelrules fspec action loadimage grammar varnode op type "
           "variable varmap jumptable emulate emulateutil flow userop expression multiprecision "
           "funcdata funcdata_block funcdata_op funcdata_varnode unionresolve pcodeinject "
           "heritage prefersplit rangeutil ruleaction subflow blockaction merge double "
           "transform constseq bitfield coreaction condexe override dynamic crc32 prettyprint "
           "printlanguage printc printjava memstate opbehavior paramid signature")
SLEIGH = ("sleigh pcodeparse pcodecompile sleighbase slghsymbol slghpatexpress slghpattern "
          "semantics context slaformat compression filemanage")
GLUE = "sleigh_arch inject_sleigh libdecomp"
# The SLEIGH compiler (the parser and scanner are generated upstream and checked in there,
# so no bison or flex is needed here).
SLACOMP = "slgh_compile slghparse slghscan"

SPEC_FILES = ["sh-2.slaspec", "superh.sinc", "superh.pspec", "superh.cspec", "superh.ldefs"]

INCLUDE_RE = re.compile(r'^\s*#\s*include\s+"([^"]+)"', re.M)


def fail(msg):
    sys.stderr.write("vendor.py: error: " + msg + "\n")
    sys.exit(1)


def names(group):
    return group.split()


def header_closure(src_dir, cc_files):
    """Every local header the given sources reach, so only what is compiled is vendored."""
    seen, todo = set(), list(cc_files)
    while todo:
        name = todo.pop()
        path = os.path.join(src_dir, name)
        if not os.path.isfile(path):
            fail("%s is included but missing from %s" % (name, src_dir))
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for inc in INCLUDE_RE.findall(f.read()):
                if inc not in seen and os.path.isfile(os.path.join(src_dir, inc)):
                    seen.add(inc)
                    todo.append(inc)
    return sorted(seen)


def git_head(tree):
    try:
        out = subprocess.run(["git", "-C", tree, "rev-parse", "HEAD"], check=True,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    except (OSError, subprocess.CalledProcessError):
        return None
    return out.stdout.decode().strip()


def apply_patches(stage):
    patches = sorted(p for p in os.listdir(os.path.join(HERE, "patches")) if p.endswith(".patch"))
    env = dict(os.environ)
    # The stage is a plain directory; stop git from discovering an enclosing repository and
    # resolving the patch paths against that instead.
    env["GIT_CEILING_DIRECTORIES"] = os.path.dirname(stage)
    for p in patches:
        path = os.path.join(HERE, "patches", p)
        # --check first so a partial application can never be left behind, then for real.
        for extra in (["--check"], []):
            r = subprocess.run(["git", "apply", "--verbose"] + extra + [path], cwd=stage, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if r.returncode != 0:
                fail("patch %s does not apply:\n%s" % (p, r.stdout.decode(errors="replace")))
        print("applied %s" % p, flush=True)
    return patches


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("--")]
    if len(args) != 1:
        fail("usage: vendor.py <ghidra-checkout> [--allow-other-commit]")
    tree = os.path.abspath(args[0])
    head = git_head(tree)
    if head != PINNED_COMMIT and "--allow-other-commit" not in argv:
        fail("%s is at %s, not the pinned %s. Bumping Ghidra is a deliberate act: update "
             "PINNED_COMMIT and VENDOR.md, then follow VENDOR.md's bump checklist."
             % (tree, head or "<not a git checkout>", PINNED_COMMIT))

    src_cpp = os.path.join(tree, UPSTREAM_CPP)
    src_spec = os.path.join(tree, UPSTREAM_SPEC)
    lib_cc = [n + ".cc" for g in (CORE, DECCORE, SLEIGH, GLUE) for n in names(g)]
    comp_cc = [n + ".cc" for n in names(SLACOMP)]
    headers = header_closure(src_cpp, lib_cc + comp_cc)

    with tempfile.TemporaryDirectory() as tmp:
        stage = os.path.join(tmp, "stage")
        os.makedirs(os.path.join(stage, UPSTREAM_CPP))
        os.makedirs(os.path.join(stage, UPSTREAM_SPEC))
        for f in lib_cc + comp_cc + headers:
            shutil.copy2(os.path.join(src_cpp, f), os.path.join(stage, UPSTREAM_CPP, f))
        for f in SPEC_FILES:
            shutil.copy2(os.path.join(src_spec, f), os.path.join(stage, UPSTREAM_SPEC, f))
        applied = apply_patches(stage)

        # Only now, with every patch applied, replace the vendored tree.
        dst_cpp = os.path.join(HERE, "cpp")
        if os.path.isdir(dst_cpp):
            shutil.rmtree(dst_cpp)
        shutil.copytree(os.path.join(stage, UPSTREAM_CPP), dst_cpp)
        dst_spec = os.path.join(HERE, "processors", "SuperH")
        os.makedirs(dst_spec, exist_ok=True)
        for f in SPEC_FILES:
            shutil.copy2(os.path.join(stage, UPSTREAM_SPEC, f), os.path.join(dst_spec, f))
    for f in ("LICENSE", "NOTICE"):
        shutil.copy2(os.path.join(tree, f), os.path.join(HERE, f))

    with open(os.path.join(HERE, "sources.cmake"), "w", newline="\n") as f:
        f.write("# Generated by vendor.py from Ghidra %s -- do not edit.\n" % (head or "?"))
        f.write("set(SE_GHIDRA_DECOMP_SOURCES\n")
        for n in lib_cc:
            f.write("    cpp/%s\n" % n)
        f.write(")\nset(SE_GHIDRA_SLEIGHC_SOURCES\n")
        for n in comp_cc:
            f.write("    cpp/%s\n" % n)
        f.write(")\n")

    print("vendored %d library sources, %d compiler sources, %d headers, %d spec files; "
          "patches: %s" % (len(lib_cc), len(comp_cc), len(headers), len(SPEC_FILES),
                           ", ".join(applied) or "none"))
    print("next: build se-regen-sla, then run SaturnExplorerSlaFreshnessTests (VENDOR.md)")


if __name__ == "__main__":
    main(sys.argv)
