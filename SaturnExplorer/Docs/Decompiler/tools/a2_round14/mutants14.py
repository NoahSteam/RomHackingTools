MUT = {
 'a': ('ruleaction.cc', [("  return y[0] <= mask / maxx;\n}","  return y[0] <= mask / maxx + 1;\n}")], 'divProductFits (unsigned): one too many'),
 'b': ('ruleaction.cc', [("    return y[0] <= ((uintb)1 << (width - xsize));","    return y[0] <= ((uintb)1 << (width - xsize)) + 1;")], 'divProductFits (signed): one too many'),
 'c': ('ruleaction.cc', [("if (!mvn->isConstant() || (mvn->getOffset() >> bits) != 0) return false;","if (!mvn->isConstant() || (mvn->getOffset() >> (bits + 1)) != 0) return false;")], 'divReadsLowBits: an AND mask one bit wider'),
 'd': ('ruleaction.cc', [("      if (8 * rdop->getOut()->getSize() > bits) return false;","      if (8 * rdop->getOut()->getSize() > bits + 8) return false;")], 'divReadsLowBits: a SUBPIECE one byte wider'),
 'e': ('ruleaction.cc', [("    if (divisor > limit) return 0;","    if (divisor > limit * 2 + 1) return 0;")], 'divisor limit doubled'),
 'f': ('ruleaction.cc', [("  return y[0] <= mask / maxx;\n}","  return y[0] + 1 <= mask / maxx;\n}")], 'divProductFits (unsigned): one too few (a right fold refused)'),
 'g': ('ruleaction.cc', [("    if (8*op->getOut()->getSize() - n != xsize)\n      return (Varnode *)0;","    if (8*op->getOut()->getSize() - n > xsize)\n      return (Varnode *)0;")], 'mismatched pair: the width test loosened'),
 'h': ('ruleaction.cc', [("  if (xsize == 0 || xsize > width) return false;\n","  if (xsize == 0) return false;\n")], 'divProductFits: a numerand wider than the multiply'),
}
