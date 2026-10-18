#!/usr/bin/env python3
# usage: apply_struct.py <cpp dir>   (a tree that nzm_check_r9.patch is applied to)
import sys
d = sys.argv[1]; t = sys.argv[2] if len(sys.argv) > 2 else '.'
s = open(d + '/action.cc').read()
marker = "}\n\n/// Specify the name, group, and properties of the Action"
code = open(t + '/structcheck_action_cc.txt').read()
code2 = open(t + '/exactstress_action_cc.txt').read()
assert marker in s
s = s.replace(marker, code + code2 + marker, 1)
s = s.replace('int4 limit = data.getArch()->maxActionRepeats;', 'int4 limit = std::getenv("SE_REPEAT_LIMIT") ? std::atoi(std::getenv("SE_REPEAT_LIMIT")) : data.getArch()->maxActionRepeats;', 1)
b2 = '      if (nzmchk::structEnabled()) nzmchk::structCheck(data, std::string("rule:") + rl->getName());'
assert b2 in s
s = s.replace(b2, b2 + '\n      nzmchk::exactStress(data);', 1)
a = '''	nzmchk::dumpEach(data, std::string("action:") + getName());
	count_apply += 1;'''
assert a in s
s = s.replace(a, '''	nzmchk::dumpEach(data, std::string("action:") + getName());
	if (nzmchk::structEnabled() && !nzmchk::isCompound(this)) { nzmchk::structCheck(data, std::string("action:") + getName()); if (getName() == "deadcode") nzmchk::modelCheck(data, std::string("action:") + getName()); }
	count_apply += 1;''', 1)
b = '''      nzmchk::dumpEach(data, std::string("rule:") + rl->getName());'''
assert b in s
s = s.replace(b, b + '''
      if (nzmchk::structEnabled()) nzmchk::structCheck(data, std::string("rule:") + rl->getName());''', 1)
a3 = '''      res = apply(data);	// Start or continue action\n#ifdef OPACTION_DEBUG'''
assert a3 in s
s = s.replace(a3, '''      res = apply(data);	// Start or continue action\n      if (nzmchk::structEnabled() && getName() == "deadcode" && res >= 0) nzmchk::modelCheck(data, "action:deadcode(always)");\n#ifdef OPACTION_DEBUG''', 1)
open(d + '/action.cc', 'w').write(s)
f = open(d + '/funcdata_op.cc').read()
f += open(t + '/structcheck_funcdata_op.cc.txt').read()
open(d + '/funcdata_op.cc', 'w').write(f)
h = open(d + '/funcdata.hh').read()
line = '  uintb inputConsumeNeeded(PcodeOp *op,int4 slot) const;'
assert line in h
h = h.replace(line, 'public:\n' + line + '\nprivate:', 1)
open(d + '/funcdata.hh', 'w').write(h)
