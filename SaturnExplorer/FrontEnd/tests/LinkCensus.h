// The census of step 1 of the link model (Docs/Decompiler/LINK_MODEL.md section 4): what the shadow model (DecompileResult::links and
// ::statements) says next to what the token addresses say, summed over every function the mapping oracle audits (AuditMapping feeds it, so
// the generator's seeds and pins, the fixtures of Sh2DecompilerTests and the game images all count). Off unless SE_LINK_CENSUS is set:
// "1" prints the report when the process ends, anything else is a file to write it to. The report is `CENSUS key value` and `CLASS key count`
// lines, which tools/a7_link1/census_merge.py adds up across the chunks of a long run, and `SAMPLE class | where | text` lines (the first few
// of each class, with the line's lowest address, for the list of disagreements).
#pragma once

#include "Decompiler/Sh2Decompiler.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace dts
{

constexpr const char* kOrigin[3] = { "flow", "clone", "engine" };
constexpr const char* kOpClass[8] = { "STORE", "CALL", "BRANCH", "RETURN", "COPY", "LOAD", "HARDWARE", "OTHER" };
constexpr const char* kKind[8] = { "None", "Store", "Assign", "Branch", "Call", "Return", "Halt", "Label" };
constexpr const char* kHalt[4] = { "baddata", "halt", "unimplemented", "missing" };
constexpr const char* kRole[10] = { "effect", "condition", "value", "placement", "", "", "", "", "", "none" };

class LinkTotals
{
public:
    static LinkTotals& Instance()
    {
        static LinkTotals totals;
        return totals;
    }
    bool Enabled() const { return mEnabled; }
    void SetContext(const std::string& label) { mContext = label; }   // what 'where' says when the oracle's own label is empty

    // One function. 'where' says which (the oracle's label: a pin, a seed, a game function).
    void Add(const sfe::decomp::DecompileResult& r, const std::string& whereGiven)
    {
        const std::string& where = whereGiven.empty() ? mContext : whereGiven;
        mLabelled = !whereGiven.empty();
        using namespace sfe::decomp;
        if (!mEnabled || r.status != DecompileStatus::Ok) return;
        ++mCount["functions"];
        mCount["elapsedMicros"] += static_cast<uint64_t>(r.elapsedMs * 1000.0);
        const LinkCensus& c = r.linkCensus;
        for (int o = 0; o < 3; ++o)
            for (int k = 0; k < 8; ++k) mCount["ops." + std::string(kOrigin[o]) + "." + kOpClass[k]] += c.ops[o][k];
        for (int k = 0; k < 8; ++k) mCount[std::string("statements.") + kKind[k]] += c.statements[k];
        for (int k = 0; k < 4; ++k) mCount[std::string("halts.") + kHalt[k]] += c.halts[k];
        mCount["storeFlow"] += c.storeFlow;
        mCount["storeEngine"] += c.storeEngine;
        mCount["walkProven"] += c.walkProven;
        mCount["walkCandidate"] += c.walkCandidate;
        mCount["walkNone"] += c.walkNone;
        mCount["walkBound"] += c.walkBound;
        mCount["namedStoreLines"] += c.namedStoreLines;
        mCount["lostStoreLines"] += c.lostStoreLines;
        mCount["effectDue"] += c.effectDue;
        mCount["effectGiven"] += c.effectGiven;
        mCount["cloneEffects"] += c.cloneEffects;
        mCount["multiStatementLines"] += c.multiStatementLines;
        mCount["noStatementLines"] += c.noStatementLines;
        mCount["opsEndDisagree"] += c.opsEndDisagree;
        mCount["inlineClones"] += c.inlineClones;
        mCount["overCap"] += c.overCap;
        mCount["linkMicros"] += c.linkMicros;
        mCount["links"] += r.links.size();
        mCount["statements"] += r.statements.size();
        mCount["lines"] += r.lines.size();
        if (c.cloneEffects) Sample("clone-effect", where, "a function with a clone-made effect op");

        // The line: the first proven Effect of the statements that start on it against the lowest address of its tokens.
        for (size_t li = 0; li < r.lines.size(); ++li)
        {
            const Line& l = r.lines[li];
            std::set<uint32_t> oldSet, newSet;
            for (const Token& t : l.tokens)
                if (t.addr) oldSet.insert(t.addr);
            bool effect = false, anyStatement = false;
            uint32_t first = 0;
            StatementKind kind = StatementKind::None;
            for (size_t si = l.statement < 0 ? r.statements.size() : static_cast<size_t>(l.statement); si < r.statements.size() && r.statements[si].firstLine <= li; ++si)
            {
                const Statement& st = r.statements[si];
                if (st.firstLine != li) continue;
                if (!anyStatement) kind = st.kind;
                anyStatement = true;
                for (uint32_t k = st.linkBegin; k < st.linkBegin + st.linkCount && k < r.links.size(); ++k)
                {
                    const InsnLink& link = r.links[k];
                    if (!(link.flags & (kLinkBadData))) newSet.insert(link.addr);
                    if (!effect && link.role == LinkRole::Effect && !(link.flags & kLinkCandidate))
                        effect = true, first = link.addr, kind = st.kind;
                }
            }
            if (!anyStatement) continue;
            ++mCount["linesWithStatement"];
            if (oldSet.empty() && newSet.empty()) continue;
            ++mCount[oldSet == newSet ? "set.equal" : std::includes(oldSet.begin(), oldSet.end(), newSet.begin(), newSet.end()) ? "set.oldHasMore"
                   : std::includes(newSet.begin(), newSet.end(), oldSet.begin(), oldSet.end()) ? "set.newHasMore" : "set.different"];
            if (!effect)
            {
                if (l.firstAddr) Class(std::string("line.no-effect/") + kKind[static_cast<int>(kind)], where, l, li, first);
                continue;
            }
            ++mCount["line.effect"];
            if (l.firstAddr == first) { ++mCount["line.agree"]; continue; }
            // The disagreement: what the old address is to the model.
            std::string rel = "old-not-linked";
            for (const Statement& st : r.statements)
            {
                if (st.firstLine != li) continue;
                for (uint32_t k = st.linkBegin; k < st.linkBegin + st.linkCount && k < r.links.size(); ++k)
                    if (r.links[k].addr == l.firstAddr)
                        rel = r.links[k].role == LinkRole::Placement ? "old-is-placement" : r.links[k].role == LinkRole::Effect ? "old-is-other-effect"
                            : (r.links[k].flags & kLinkCandidate) ? "old-is-candidate-value" : "old-is-value";
            }
            ++mCount["line.disagree"];
            Class(std::string("line.disagree/") + kKind[static_cast<int>(kind)] + "/" + rel + (l.firstAddr < first ? "/below" : l.firstAddr == 0 ? "/none" : "/above"),
                  where, l, li, first);
        }

        // The way back: the first line today's answer offers for an instruction against the first the roles give (Effect, Condition, Value,
        // Placement; text order within a role).
        std::map<uint32_t, std::map<size_t, int>> best;   // instruction -> line -> best role rank
        for (const Statement& st : r.statements)
            for (uint32_t k = st.linkBegin; k < st.linkBegin + st.linkCount && k < r.links.size(); ++k)
            {
                const InsnLink& link = r.links[k];
                if (link.flags & (kLinkBadData | kLinkHalt)) continue;
                const int rank = link.role == LinkRole::Effect ? 0 : link.role == LinkRole::Condition ? 1 : link.role == LinkRole::Placement ? 3 : 2;
                auto at = best[link.addr].emplace(st.firstLine, rank);
                if (!at.second && rank < at.first->second) at.first->second = rank;
            }
        const InstructionLineIndex index(r);
        for (const auto& range : r.body)
            for (uint32_t a = range.first; a < range.second; a += 2)
            {
                const LineLookup old = index.Find(a);
                if (old.kind != LineLookup::Exact || old.lines.empty()) continue;
                ++mCount["insn.exact"];
                const auto b = best.find(a);
                if (b == best.end())
                {
                    ++mCount["insn.unlinked"];
                    continue;
                }
                size_t line = 0;
                int rank = 9;
                for (const auto& lr : b->second)
                    if (lr.second < rank) rank = lr.second, line = lr.first;
                if (old.lines.front() == line) { ++mCount["insn.agree"]; continue; }
                ++mCount["insn.disagree"];
                const bool oldLinked = b->second.count(old.lines.front()) != 0;
                Class(std::string("insn.disagree/new-") + kRole[rank] + (oldLinked ? "/old-first-linked" : "/old-first-unlinked"), where,
                      r.lines[old.lines.front()], old.lines.front(), a);
            }
    }

    void Print(std::FILE* out) const
    {
        for (const auto& kv : mCount) std::fprintf(out, "CENSUS %s %llu\n", kv.first.c_str(), static_cast<unsigned long long>(kv.second));
        for (const auto& kv : mClass) std::fprintf(out, "CLASS %s %llu\n", kv.first.c_str(), static_cast<unsigned long long>(kv.second));
        for (const auto& kv : mSamples)
            for (const std::string& s : kv.second) std::fprintf(out, "SAMPLE %s | %s\n", kv.first.c_str(), s.c_str());
        for (const std::string& s : mAll) std::fprintf(out, "LABELLED %s\n", s.c_str());
    }

private:
    static const size_t kSamples = 6;

    LinkTotals()
    {
        const char* e = std::getenv("SE_LINK_CENSUS");
        mEnabled = e && *e;
        if (mEnabled && std::string(e) != "1") mPath = e;
    }
    ~LinkTotals()
    {
        if (!mEnabled) return;
        std::FILE* out = mPath.empty() ? stdout : std::fopen(mPath.c_str(), "w");
        if (!out) return;
        Print(out);
        if (out != stdout) std::fclose(out);
    }
    void Sample(const std::string& cls, const std::string& where, const std::string& text)
    {
        std::vector<std::string>& v = mSamples[cls];
        if (v.size() < kSamples) v.push_back(where + " | " + text);
    }
    void Class(const std::string& cls, const std::string& where, const sfe::decomp::Line& l, size_t line, uint32_t at)
    {
        ++mClass[cls];
        char b[96];
        std::snprintf(b, sizeof b, "line %zu firstAddr %08X new %08X", line, l.firstAddr, at);
        Sample(cls, where, std::string(b) + " | " + l.text);
        // (a labelled function is a pin or a fixture: every disagreement is listed, for the rows whose pins must hold)
        if (mLabelled && mAll.size() < 50000) mAll.push_back(cls + " | " + where + " | " + std::string(b) + " | " + l.text);
    }

    bool mEnabled = false, mLabelled = false;
    std::vector<std::string> mAll;
    std::string mPath, mContext;
    std::map<std::string, uint64_t> mCount, mClass;
    std::map<std::string, std::vector<std::string>> mSamples;
};

}  // namespace dts
