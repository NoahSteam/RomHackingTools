import os
# usage: ENGINE_SRC=<SaturnExplorer dir> python3 dbgpatch2.py   (temporary env-gated prints in the callee walk; SE_DBG_CFW=1 enables; revert with git checkout)
p=os.environ['ENGINE_SRC']+'/FrontEnd/src/Decompiler/Sh2Decompiler.cpp'
s=open(p).read()
old="""        cache[target] = words;
        // A3-86 a: the registers the walked callee stores below"""
new="""        cache[target] = words;
        if (getenv("SE_DBG_CFW")) fprintf(stderr, "DBG CFW target=%08X depth=%d why='%s' undecodable=%d cfs=%08X own=%08X words=%08X budget=%zu steps=%zu\\n", target, mDepth, why.c_str(), (int)callee.mEntryUndecodable, callee.CallerFrameStores(), callee.mOwnFrameWords, words, (size_t)ctx->budget, (size_t)callee.mStepsUsed);
        // A3-86 a: the registers the walked callee stores below"""
assert old in s
s=s.replace(old,new)
old2="""        if (mDepth >= kNestedMaxDepth || ctx->budget == 0)
        {"""
new2="""        if (mDepth >= kNestedMaxDepth || ctx->budget == 0)
        {
            if (getenv("SE_DBG_CFW")) fprintf(stderr, "DBG CFW LIMIT target=%08X depth=%d budget=%zu\\n", target, mDepth, (size_t)ctx->budget);"""
assert old2 in s
s=s.replace(old2,new2)
open(p,'w').write(s)
s=open(p).read()
old3="""    void MapCalleeWords(uint32_t words, const State& s, const Value& sp)
    {
        if (words == 0) return;"""
new3="""    void MapCalleeWords(uint32_t words, const State& s, const Value& sp)
    {
        if (getenv("SE_DBG_CFW") && words != 0) fprintf(stderr, "DBG MAP depth=%d words=%08X spkind=%d spv=%lld pc=%llx\\n", mDepth, words, (int)sp.kind, (long long)sp.v, (unsigned long long)(mDef >> 12));
        if (words == 0) return;"""
assert old3 in s
s=s.replace(old3,new3)
old4="""        const uint32_t words = CalleeFrameWrites(target);
        if (words == 0) return;   // the callee writes none of its caller's frame"""
new4="""        const uint32_t words = CalleeFrameWrites(target);
        if (getenv("SE_DBG_CFW")) fprintf(stderr, "DBG EXPOSE at=%08X target=%08X words=%08X\\n", at, target, words);
        if (words == 0) return;   // the callee writes none of its caller's frame"""
assert old4 in s
s=s.replace(old4,new4)
open(p,'w').write(s)
