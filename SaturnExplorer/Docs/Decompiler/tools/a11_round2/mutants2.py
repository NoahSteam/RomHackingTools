"""Area 11 round 2: the mutants added for the code changed since round 1 (master a64678c3 to b68bfb60).

Each is (id, file, old text, new text, should, escalate, what it simulates); the file's old text must occur once, or, with
the ALL prefix, at least once (every occurrence is replaced). The row each one reverts is named in the text; P1 and P2 rows
are marked, since a survivor that reverts a P1 fix is area 11's P2.

  H7-  area 7's mapping in Sh2Decompiler.cpp (the value rule, the join constants, the number-reaches-store rule, the case labels)
  O-   the mapping oracle's own checks (MappingOracle.h) blinded one at a time: does any fixture witness the check?
  S-   StoreMerge.h (SaveStore, MergeIntoText)          T-  Settings::SaveTo
  L-   FileLock (FileWrite.cpp)                         N-  the loaders' line and file readers (AddressNameLine.h, FileRead.h)
  P-   the decompiler panel's entry box and pin rules (DecompilerPanel.cpp)
"""


def round2_mutants(SRC, DEC, MO, PAN, ALL, A9ALL, PANEL, ENGINE_ESCALATE):
    MAP = ["unit", "gen", "corpus"]
    A9 = A9ALL
    SM = SRC + "StoreMerge.h"
    SET = SRC + "Settings.cpp"
    FW = SRC + "FileWrite.cpp"
    ANL = SRC + "AddressNameLine.h"
    FR = SRC + "FileRead.h"
    m = [
        # ---- area 7: the mapping ----
        ("H7-storeshows", DEC, "    if (want.times.count(r.time)) return true;\n    const auto at = flow.ops.find(r.time);",
         "    return true;\n    const auto at = flow.ops.find(r.time);", MAP, [],
         "A7-23 (P2) reverted: every store of the variable is taken to show the statement's value"),
        ("H7-take-noshow", DEC, "                if (!r || !shows(r)) return false;", "                if (!r) return false;", MAP, [],
         "A7-23 (P2): the store search's take() no longer asks the value (the statement's own store still does)"),
        ("H7-own-shows", DEC, "            if (own && shows(own))\n", "            if (own)\n", MAP, [],
         "A7-23 (b) (P2): the store at the statement's own instruction taken whatever it stored"),
        ("H7-joinconst-off", DEC, "        AddJoinConstantTokens(r.lines, printed, *fd, flow);", "        (void)0;", MAP, [],
         "A7-22 (P2) reverted: a number merged at a join stays at the join's first instruction"),
        ("H7-into-null", DEC, "        const FlowVn* into = registerStorage ? &storage : nullptr;", "        const FlowVn* into = nullptr;", MAP, [],
         "A7-26 (P2) reverted: an instruction making the number into ANOTHER register is the join constant's maker"),
        ("H7-reaches-reg", DEC, "            if (!InstructionMakesNumber(flow, insn, numberValue, numberSize, &v)) return false;",
         "            if (!InstructionMakesNumber(flow, insn, numberValue, numberSize)) return false;", MAP, [],
         "A7-28 (a) (P2): the number-reaches-store rule ignores which register the instruction makes the number into"),
        ("H7-reaches-any", DEC, "            for (const FlowOp* w : writers)\n                if (w->insn == insn) return true;\n            return false;",
         "            return true;", MAP, [],
         "A7-28 (P2): the number-reaches-store rule takes any instruction making the number, not the store's last writer"),
        ("H7-numreach-off", DEC, "        if (numberCopy && !slotMaker && storedKnown && !flow.callInsns.count(at))",
         "        if (false && numberCopy && !slotMaker && storedKnown && !flow.callInsns.count(at))", MAP, [],
         "A7-28 (P2) reverted: the stores the number reaches are not looked for"),
        ("H7-case-opref", DEC, "                if (elem == kCaseLabel && op->getParent() && op->getParent()->contains(op->getAddr()))",
         "                if (false && elem == kCaseLabel && op->getParent() && op->getParent()->contains(op->getAddr()))", MAP, [],
         "A7-24 (P3) reverted: a case label names Ghidra's opref, the case's first op"),
        ("H7-case-dest", DEC, "            if (caseDest) t.addr = caseDest;", "            (void)caseDest;", MAP, [],
         "A7-27 (P3) reverted: a case that is only a branch or a nop names the exit or nothing"),
        ("H7-case-inner", DEC, "        const auto own = switchAtIndent.find(indentOfLabel);",
         "        const auto own = switchAtIndent.empty() ? switchAtIndent.end() : std::prev(switchAtIndent.end());", MAP, [],
         "A7-29 (P3) reverted (near enough): a label's table is the innermost switch's, not its own indent's"),
        # ---- the mapping oracle's own checks blinded (test code; the suite's damaged-copy pins are their witnesses) ----
        ("O-storeValueOther", MO, "++a.storeValueOther;", "(void)0;", ["unit", "gen"], [], "the oracle's A7-23 check never counts"),
        ("O-storeAfterUse", MO, "++a.storeAfterUse;", "(void)0;", ["unit", "gen"], [], "the oracle's A7-32 check never counts"),
        ("O-copyMergeAtBranch", MO, "++a.copyMergeAtBranch;", "(void)0;", ["unit", "gen"], [], "the oracle's A7-21 check never counts"),
        ("O-numberStoreOther", MO, "++a.numberStoreOther;", "(void)0;", ["unit", "gen"], [], "the oracle's A7-28 check never counts"),
        ("O-constAtJoin", MO, ALL + "++a.constAtJoin;", "(void)0;", ["unit", "gen"], [], "the oracle's A7-22/A7-26 check never counts (3 sites)"),
        ("O-caseOtherEntry", MO, "++a.caseOtherEntry;", "(void)0;", ["unit", "gen"], [], "the oracle's A7-29 check never counts"),
        ("O-backWrong", MO, ALL + "++a.backWrong;", "(void)0;", ["unit", "gen"], [], "the oracle's way-back check never counts (2 sites)"),
        ("O-frameStoreMissing", MO, "++a.frameStoreMissing;", "(void)0;", ["unit", "gen"], [], "the oracle's A7-11 check never counts"),
        ("O-accessAtBranch", MO, "++a.accessAtBranch;", "(void)0;", ["unit", "gen"], [], "the oracle's A7-10 slot check never counts"),
        # ---- area 9: one save for the user's files (StoreMerge.h) ----
        ("S-unchanged", SM, "    if (!StoreChanged(base, cur)) return true;", "", A9, [],
         "A9-20 (P1): an instance that changed nothing saves anyway (the file rewritten as it is)"),
        ("S-refused", SM, "    if (!refusedWhy.empty())\n        return fail(", "    if (false)\n        return fail(", A9, [],
         "A9-19 (P1) reverted: a file the loader refused is written over by the save"),
        ("S-lockheld", SM, "    if (!lock.Held())\n        return fail(std::string(what)", "    if (false)\n        return fail(std::string(what)", A9, [],
         "A9-33 reverted: a store saves without its lock"),
        ("S-nodisk", SM, "MergeIntoText(disk, base, cur, classify, format, header)", "MergeIntoText(std::string(), base, cur, classify, format, header)", A9, [],
         "A9-20 (P1) reverted: the save writes this session's view, not the file as it is now"),
        ("S-dropother", SM, "        out += line;\n        out += '\\n';\n    }\n    for (uint32_t a : set)",
         "        if (classify(line, a)) { out += line; out += '\\n'; }\n    }\n    for (uint32_t a : set)", A9, [],
         "A9-21 (P2) reverted: a line that is no entry (a comment, a skipped line) is erased by a save"),
        ("S-nodelete", SM, "            if (deleted.count(a)) continue;\n", "", A9, [], "a deleted entry comes back at the save"),
        ("S-cap", SM, "    if (text.size() > maxBytes)\n        return fail(", "    if (false)\n        return fail(", A9, [],
         "a save larger than the next start reads is written (A9-27's ceiling)"),
        # ---- area 9: Settings::SaveTo ----
        ("T-unchanged", SET, "    if (changes.empty()) return true;", "", A9, [], "A9-20 (P1): settings unchanged are saved anyway"),
        ("T-refused", SET, "    if (same && !mRefusedWhy.empty())", "    if (false)", A9, [], "A9-19 (P1) reverted: a refused settings file is replaced"),
        ("T-lockheld", SET, "    if (!lock.Held())\n        return fail(\"Settings: not saved, \"", "    if (false)\n        return fail(\"Settings: not saved, \"", A9, [],
         "A9-33 reverted: settings saved without the lock"),
        ("T-nodisk", SET, "    for (const std::string& line : SplitLines(disk))", "    for (const std::string& line : SplitLines(std::string()))", A9, [],
         "A9-20 (P1)/A9-34 reverted: the settings file written from memory, the other instance's keys and the comments lost"),
        ("T-oneline-cr", SET, "        for (char& c : v) if (c == '\\n' || c == '\\r') c = ' ';", "        for (char& c : v) if (c == '\\n') c = ' ';", A9, [],
         "A9-12/A9-25: a CR in a value starts a setting of its own at the next start"),
        # ---- area 9: FileLock ----
        ("L-noflock", FW, "        const int rc = ::flock(fd, LOCK_EX | LOCK_NB);", "        const int rc = 0;", A9, [],
         "the lock is never taken (two instances' saves interleave: A9-20's guard)"),
        ("L-noretry", FW, "        if (!retry) break;\n        if (expired()) { mTimedOut = wait; break; }\n        struct timespec",
         "        break;\n        if (expired()) { mTimedOut = wait; break; }\n        struct timespec", A9, [],
         "A9-40/A9-52: a held lock is not waited for at all"),
        ("L-nobound", FW, "        if (expired()) { mTimedOut = wait; break; }\n        struct timespec",
         "        if (false) { mTimedOut = wait; break; }\n        struct timespec", A9, [],
         "A9-40 reverted: the wait for a held lock has no bound (a stuck holder freezes the caller)"),
        ("L-exitwait", FW, "    if (gClosing && wait)\n", "    if (false && gClosing && wait)\n", A9, [],
         "A9-46/A9-53 reverted: no longer and shared wait at exit"),
        ("L-sharedshort", FW, "        mSharedShort = budgetMs + 500 < kExitWaitMs;", "        mSharedShort = false;", A9, [],
         "A9-58: the refusal at exit does not say an earlier store used the shared time"),
        ("L-why-holds", FW, "    if (mError == 0) return \"another process holds \" + mPath;", "", A9, [],
         "A9-47: a no-wait lock someone holds is reported with errno 0's text"),
        # ---- area 9: the loaders' readers ----
        ("N-addr-letters", ANL, "    if (!prefixed && e - d < 8 && !digit) return false;", "", A9, [],
         "A9-18/A9-41 reverted: a word of the letters a-f (\"add\", \"Dead\") is an address"),
        ("N-addr-remark", ANL, "    if (!prefixed && e - d < 8 && (strict || a < 0x80000u)) return false;", "", A9, [],
         "A9-45 reverted: a remark opening with a number (\"2026 trace\") is an entry; the bounds file's strict rule gone"),
        ("N-split-cr", ANL, "        if (text[i] != '\\n' && text[i] != '\\r') continue;", "        if (text[i] != '\\n') continue;", A9, [],
         "A9-25 reverted: a lone CR does not end a line"),
        ("N-bom", ANL, "    size_t b = text.compare(0, 3, \"\\xEF\\xBB\\xBF\") == 0 ? 3 : 0;", "    size_t b = 0;", A9, [],
         "A9-34: a UTF-8 byte-order mark is part of the first line"),
        ("N-clean", ANL, "    for (char& c : s) if (c == '\\n' || c == '\\r') c = ' ';", "    (void)0;", A9, [],
         "A9-31/A9-36 reverted: a name or note with a line end is saved as two lines"),
        ("N-fifo", FR, "    const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);", "    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);", A9, [],
         "A9-24 reverted: the open can block on a FIFO swapped in after the stat"),
        ("N-resize", FR, "    if (static_cast<unsigned long long>(st2.st_size) > maxBytes) { ::close(fd); return CappedRead::TooLarge; }", "", A9, [],
         "the size is not checked again on the descriptor (a link swapped to a large file after the stat; the read cap remains)"),
        # ---- area 8: the entry box and the pin ----
        ("P-a842-shown", PAN, "        if (mBoxShowsShown && mShownValid && t.kind == TargetKind::Entry)\n",
         "        if (false && mBoxShowsShown && mShownValid && t.kind == TargetKind::Entry)\n", PANEL, [],
         "A8-42 reverted: untouched panel text asks the address it says, not the output shown"),
        ("P-a836-origin", PAN, "        if ((!mBoxShowsShown || mShownValid) && t.kind == TargetKind::Entry && t.address == v)",
         "        if (false)", PANEL, [], "A8-36 (P2) reverted: Enter on the panel's text makes a pinned guess a user-set entry"),
        ("P-a846-undo", PAN, "        if (mBoxCleanValid && !typedOut && std::strcmp(mEntryBuf, mBoxCleanText) == 0)",
         "        if (false && mBoxCleanValid && !typedOut && std::strcmp(mEntryBuf, mBoxCleanText) == 0)", PANEL, [],
         "A8-46 reverted: an edit taken back leaves the text the user's"),
        ("P-a846-typedout", PAN, "        if (mBoxCleanValid && !typedOut && std::strcmp(mEntryBuf, mBoxCleanText) == 0)",
         "        if (mBoxCleanValid && std::strcmp(mEntryBuf, mBoxCleanText) == 0)", PANEL, [],
         "A8-46's limit: text typed out to the panel's text is taken for an edit taken back"),
        ("P-a842-rewrite", PAN, "            if (shownText) std::snprintf(mEntryBuf, sizeof mEntryBuf, \"%08X\", t.address);",
         "            (void)shownText;", PANEL, [], "A8-42: after Enter on untouched text the box keeps an address other than what was asked"),
        ("P-a850-sync", PAN, "            // The refused text stays in the box with the refusal; the sync an install left pending waits for the refusal to go (A8-50, A8-20).\n            mSyncEntryBox = false;\n",
         "", PANEL, [], "A8-50 reverted: a refused entry's text is overwritten by the install's sync"),
        ("P-askagain", PAN, "            // failure, the old function would otherwise stay on screen with nothing coming).\n            mAskAgain = true;",
         "            // failure, the old function would otherwise stay on screen with nothing coming).\n", PANEL, [],
         "Enter on a typed entry is not a request when the question was asked before (after a Stop)"),
        ("P-sync-active", PAN, "    if (mSyncEntryBox && !mEntryActive && mShownValid)", "    if (mSyncEntryBox && mShownValid)", PANEL, [],
         "the install writes the box while the user edits it (the draft lost)"),
        ("P-pin-typed", PAN, "                t.origin = EntryOrigin::Pinned;", "                t.origin = EntryOrigin::Typed;", PANEL, [],
         "A8-30 (P2) reverted: Follow PC off pins the finder's guess as a user-set entry"),
        ("P-pin-noresolve", PAN, "        if (mTarget.origin == EntryOrigin::Pinned)\n            ResolvePin(inputs, canonical);",
         "        if (false)\n            ResolvePin(inputs, canonical);", PANEL, [],
         "A8-37 (P2) reverted: a pinned entry is never asked of the finder again (settled at 100)"),
        ("P-nothingpin", PAN, "    if (mTarget.kind == TargetKind::Entry && mTarget.origin == EntryOrigin::NothingToPin)",
         "    if (false && mTarget.kind == TargetKind::Entry && mTarget.origin == EntryOrigin::NothingToPin)", PANEL, [],
         "A8-38 reverted: Follow PC off with nothing found decompiles 0x00000000 as a user-set entry"),
    ]
    return m
