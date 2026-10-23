// Area 2 round 13: the printed-C oracle's engine reading against C and against the printer, cast by cast (see castmodel13.sh).
#include "cxxheader.inc"
#include <cstdio>
#include <vector>

static u64 Mk(int n) { return n >= 8 ? ~0ull : ((1ull << (8 * n)) - 1); }
static s64 Sx(u64 v, int n) { if (n >= 8) return (s64)v; u64 sign = 1ull << (8 * n - 1); return (s64)((v ^ sign) - sign); }

// C's conversion of a value of M bytes (signed or not) to an integer type of N bytes (signed or not), as a 64-bit pattern masked to N bytes
static u64 CCast(u64 v, int M, bool U /*source signed*/, int N)
{
    u64 wide = U ? (u64)Sx(v, M) : (v & Mk(M));
    return wide & Mk(N);
}

template <int N, bool S, int M, bool U> int Cell(std::vector<u64> const& vals, bool printerSaysZeroExt)
{
    int diffC = 0, diffP = 0;
    for (u64 v : vals)
    {
        T<M, U> x((u64)(v & Mk(M)));
        T<N, S> y = T<N, S>(x);                       // the explicit cast of the reading
        const u64 got = y.Zext();
        const u64 c = CCast(v & Mk(M), M, U, N);
        const u64 p = printerSaysZeroExt ? ((v & Mk(M)) & Mk(N)) : c;
        if (got != c) ++diffC;
        if (got != p) ++diffP;
    }
    if (diffC || diffP) std::printf("cast (%sint%d) of a %s %d-byte value: differs from C in %d of %zu values, from the printer's meaning in %d\n", S ? "" : "u", N, U ? "signed" : "unsigned", M, diffC, vals.size(), diffP);
    return diffC + diffP;
}

template <int N, bool S, int M> int PieceCell(std::vector<u64> const& vals)
{
    int diffZ = 0, diffS = 0;
    for (u64 v : vals)
    {
        Piece<M> x((u64)(v & Mk(M)));
        T<N, S> y = T<N, S>(x);
        const u64 got = y.Zext();
        const u64 z = (v & Mk(M)) & Mk(N), sx = (u64)Sx(v & Mk(M), M) & Mk(N);
        if (got != z) ++diffZ;
        if (got != sx) ++diffS;
    }
    const char* reading = diffZ == 0 && diffS == 0 ? "both (the value has no high bit set)" : diffZ == 0 ? "zero extension" : diffS == 0 ? "sign extension" : "neither";
    std::printf("cast (%sint%d) of a CONCAT piece of %d bytes: read as %s\n", S ? "" : "u", N, M, reading);
    return 0;
}

#define CELLS_M(M, U) \
    Cell<2, true, M, U>(vals, false); Cell<2, false, M, U>(vals, false); Cell<4, true, M, U>(vals, false); Cell<4, false, M, U>(vals, false); Cell<8, true, M, U>(vals, false); Cell<8, false, M, U>(vals, false);

int main()
{
    std::vector<u64> vals = { 0, 1, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff, 0x7fffffffull, 0x80000000ull, 0xffffffffull, 0x7fffffffffffull, 0x800000000000ull, 0xffffffffffffull, 0x7fffffffffffffffull, 0x8000000000000000ull, ~0ull };
    std::printf("-- scalar casts: the cells where the reading is not C's conversion --\n");
    Cell<2, true, 1, true>(vals, false); Cell<2, false, 1, true>(vals, false); Cell<4, true, 1, true>(vals, false); Cell<4, false, 1, true>(vals, false); Cell<8, true, 1, true>(vals, false); Cell<8, false, 1, true>(vals, false);
    Cell<2, true, 1, false>(vals, false); Cell<4, true, 1, false>(vals, false); Cell<4, false, 1, false>(vals, false); Cell<8, true, 1, false>(vals, false); Cell<8, false, 1, false>(vals, false);
    Cell<4, true, 2, true>(vals, false); Cell<4, false, 2, true>(vals, false); Cell<8, true, 2, true>(vals, false); Cell<8, false, 2, true>(vals, false);
    Cell<4, true, 2, false>(vals, false); Cell<4, false, 2, false>(vals, false); Cell<8, true, 2, false>(vals, false); Cell<8, false, 2, false>(vals, false);
    Cell<8, true, 4, true>(vals, false); Cell<8, false, 4, true>(vals, false); Cell<8, true, 4, false>(vals, false); Cell<8, false, 4, false>(vals, false);
    Cell<8, false, 3, true>(vals, false); Cell<8, false, 3, false>(vals, false);
    std::printf("-- pieces: the reading of (T)CONCATab by size and target --\n");
    PieceCell<4, false, 2>(vals); PieceCell<4, true, 2>(vals); PieceCell<8, false, 2>(vals); PieceCell<8, true, 2>(vals);
    PieceCell<8, false, 3>(vals); PieceCell<8, true, 3>(vals); PieceCell<8, false, 4>(vals); PieceCell<8, true, 4>(vals);
    PieceCell<8, false, 5>(vals); PieceCell<8, true, 5>(vals); PieceCell<8, false, 6>(vals); PieceCell<8, true, 6>(vals); PieceCell<8, false, 7>(vals); PieceCell<8, true, 7>(vals);
    return 0;
}
