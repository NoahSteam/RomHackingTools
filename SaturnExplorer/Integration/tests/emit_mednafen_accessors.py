"""Write apply.py's memory accessors out as .inc files, so a C++ test can compile the exact code
apply.py injects into Mednafen instead of a copy of it that could drift.

Each block is written without its `namespace MDFN_IEN_SS { namespace VDPn {` wrapper; the test
re-opens the namespace around the #include with stubs of the statics the code touches.
Usage: emit_mednafen_accessors.py <apply.py> <out dir>"""
import importlib.util
import os
import sys

apply_py, out = sys.argv[1], sys.argv[2]
spec = importlib.util.spec_from_file_location("se_apply", apply_py)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)


def body(block):
    lines = block.strip().splitlines()
    return "\n".join(l for l in lines if not l.startswith("namespace") and l != "}}") + "\n"


def function(block, signature):
    """One top-level function out of a block, from its signature to the closing brace in column 0."""
    start = block.index(signature)
    return block[start:block.index("\n}\n", start) + 3]


os.makedirs(out, exist_ok=True)
files = {
    "vdp1_accessors.inc": body(mod.VDP1_ACCESSORS),
    "vdp1_poke.inc": body(mod.VDP1_POKE_ACCESSORS),
    "vdp2_accessors.inc": body(mod.VDP2_ACCESSORS),
    "vdp2_poke.inc": body(mod.VDP2_POKE_ACCESSORS),
    "ss_poke.inc": function(mod.SS_ACCESSORS, 'extern "C" void SsDbgPokeByte('),
}
for name, text in files.items():
    path = os.path.join(out, name)
    # Only rewrite on change, so an unchanged apply.py does not trigger a rebuild.
    if not os.path.exists(path) or open(path).read() != text:
        with open(path, "w") as f:
            f.write(text)
