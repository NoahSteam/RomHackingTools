#!/bin/bash
# usage: mkcast13.sh <SaturnExplorer dir> <scratch dir>
# Builds a scratch printc.cc object that logs, for every ZEXT, SEXT, SUBPIECE and CAST the printer prints, what the p-code op was and how it was printed
# (SE_CASTLOG=1: `CASTLOG <op> <in size>:<in metatype> <out size>:<out metatype> <style>`, style cast / hidden / func), so that the printed-C oracle's reading of each
# form can be compared with the op the engine means. The vendored tree does not change.
se=$1; out=$2
d=$se/FrontEnd/third_party/ghidra-decompiler/cpp
mkdir -p "$out"
python3 - "$d/printc.cc" "$out/printc_cl.cc" <<'PY'
import sys
s = open(sys.argv[1]).read()
helper = r'''
static const char *seMeta(type_metatype m) {
  switch (m) { case TYPE_VOID: return "void"; case TYPE_UNKNOWN: return "unknown"; case TYPE_INT: return "int"; case TYPE_UINT: return "uint"; case TYPE_BOOL: return "bool";
    case TYPE_CODE: return "code"; case TYPE_FLOAT: return "float"; case TYPE_PTR: return "ptr"; case TYPE_PTRREL: return "ptrrel"; case TYPE_ARRAY: return "array"; case TYPE_STRUCT: return "struct";
    case TYPE_UNION: return "union"; case TYPE_PARTIALSTRUCT: return "partstruct"; case TYPE_PARTIALUNION: return "partunion"; default: return "other"; }
}
static void seCastLog(const char *what, const PcodeOp *op, const char *style) {
  static const bool on = getenv("SE_CASTLOG") != 0;
  if (!on) return;
  const Varnode *in = op->getIn(0), *out = op->getOut();
  fprintf(stderr, "CASTLOG %s %d:%s %d:%s %s\n", what, in->getSize(), seMeta(in->getHighTypeReadFacing(op)->getMetatype()), out->getSize(), seMeta(out->getHighTypeDefFacing()->getMetatype()), style);
}
'''
s = s.replace("namespace ghidra {\n", "#include <cstdio>\n#include <cstdlib>\nnamespace ghidra {\n" + helper, 1)
def patch(name, tag):
    global s
    i = s.index("void PrintC::" + name)
    j = s.index("\n}\n", i)
    body = s[i:j]
    old = """    if (option_hide_exts && castStrategy->isExtensionCastImplied(op,readOp))
      opHiddenFunc(op);
    else
      opTypeCast(op);
  }
  else
    opFunc(op);"""
    new = """    if (option_hide_exts && castStrategy->isExtensionCastImplied(op,readOp)) {
      seCastLog("%s", op, "hidden");
      opHiddenFunc(op);
    }
    else {
      seCastLog("%s", op, "cast");
      opTypeCast(op);
    }
  }
  else {
    seCastLog("%s", op, "func");
    opFunc(op);
  }""" % (tag, tag, tag)
    assert old in body
    s = s[:i] + body.replace(old, new) + s[j:]
patch("opIntZext", "ZEXT")
patch("opIntSext", "SEXT")
s = s.replace("""    opTypeCast(op);
  else
    opFunc(op);
}

void PrintC::opPtradd""", """    { seCastLog("SUBPIECE", op, "cast"); opTypeCast(op); }
  else
    { seCastLog("SUBPIECE", op, "func"); opFunc(op); }
}

void PrintC::opPtradd""", 1)
open(sys.argv[2], 'w').write(s)
PY
g++ -O2 -g -std=gnu++14 -I"$d" -c "$out/printc_cl.cc" -o "$out/printc.cc.o" || exit 1
echo "built $out/printc.cc.o"
