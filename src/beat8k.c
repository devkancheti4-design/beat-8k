/* beat8k.c -- see beat.h. The beat solver for the edai 300-problem challenge, built to need almost no memory.

   It gives the same answers as the table-based solver it came from (challengers/beat in edai's history), but keeps
   no tables. Instead of remembering every piece of every candidate tree while one pass over the formulas fills them
   in, it makes many passes, each over a small batch of pieces that lives on the stack; a formula is worked out only
   at the examples a piece needs; and the answers are worked out in one last pass, for the winning leaves only.
   No static data, no heap, no C library. Portable C99 plus the GCC/Clang builtins __builtin_*_overflow, _ctz and
   _popcount. */
#include "beat.h"
#pragma STDC FP_CONTRACT OFF

typedef int64_t i64;
typedef uint64_t u64;
typedef bt_int val;                      /* a formula's value: 64 bits, or 32 with BT_VAL32 */
#define NOINLINE __attribute__((noinline)) /* a function with a large frame keeps it to itself, and gives it back */

#define MAXE   BT_MAX_EXAMPLES
#define MAXP   (BT_MAX_EXAMPLES + BT_MAX_QUESTIONS)
#define ONE    65536                     /* one bit, in the Q16 fixed point every length is kept in */
#define MAXCOND (54 + MAXE - 1)          /* x % m == r for m = 2..10 (54), and a cut in each gap between examples */
#ifndef BT_BATCH                         /* pieces examined per pass: the most one side of a split can need */
#define BT_BATCH (MAXE > 12 ? 128 : 96)
#endif

/* constant tables: on an AVR they are read from flash, so that they take no RAM */
#ifdef __AVR__
#include <avr/pgmspace.h>
#define ROM PROGMEM
#define ROM8(t, i) ((int8_t)pgm_read_byte(&(t)[i]))
#define ROMU8(t, i) pgm_read_byte(&(t)[i])
#define ROM32(t, i) ((int32_t)pgm_read_dword(&(t)[i]))
#define ROMF(t, i) pgm_read_float(&(t)[i])
#else
#define ROM
#define ROM8(t, i) ((t)[i])
#define ROMU8(t, i) ((t)[i])
#define ROM32(t, i) ((t)[i])
#define ROMF(t, i) ((t)[i])
#endif

#ifdef BT_VAL32
#define LIMIT 1073741823L                /* a formula whose value passes 2^30 - 1 anywhere is dropped */
typedef uint32_t uval;
typedef int32_t dif;                     /* a difference of two values or two outputs: fits, as both are within LIMIT */
static int vmul(val a, val b, val *r) { i64 t = (i64)a * b; if (t > LIMIT || t < -LIMIT) return 0; *r = (val)t; return 1; }
static int vadd(val a, val b, val *r) { val t = a + b; if (t > LIMIT || t < -LIMIT) return 0; *r = t; return 1; }
static int vsub(val a, val b, val *r) { val t = a - b; if (t > LIMIT || t < -LIMIT) return 0; *r = t; return 1; }
#define DIFF(a, b) ((dif)((a) - (b)))
#define MULO(a, b, r) (*(r) = (i64)(a) * (i64)(b), 0)
#define ADDO(a, b, r) (*(r) = (i64)(a) + (i64)(b), 0)
#define SUBO(a, b, r) (*(r) = (i64)(a) - (i64)(b), 0)
#else
#define LIMIT 1000000000000LL            /* a formula whose value passes 1e12 anywhere is dropped */
typedef uint64_t uval;
typedef int64_t dif;
static int vmul(val a, val b, val *r) { return !__builtin_mul_overflow(a, b, r); }
static int vadd(val a, val b, val *r) { return !__builtin_add_overflow(a, b, r); }
static int vsub(val a, val b, val *r) { return !__builtin_sub_overflow(a, b, r); }
#define DIFF(a, b) ((i64)((u64)(a) - (u64)(b)))
#define MULO(a, b, r) __builtin_mul_overflow(a, b, r)
#define ADDO(a, b, r) __builtin_add_overflow(a, b, r)
#define SUBO(a, b, r) __builtin_sub_overflow(a, b, r)
#endif

/* an example mask has at most 16 bits: scan and count it in an unsigned int (16 bits on an AVR) */
#ifdef __AVR__
static uint8_t CTZ(uint16_t e)
{
    uint8_t n = 0, lo = (uint8_t)e;
    if (!lo) { n = 8; lo = (uint8_t)(e >> 8); }
    if (!(lo & 0x0F)) { n += 4; lo >>= 4; }
    if (!(lo & 3)) { n += 2; lo >>= 2; }
    if (!(lo & 1)) n++;
    return n;
}
#else
#define CTZ(e) __builtin_ctz((unsigned)(e))
#endif
#define POPC(e) __builtin_popcount((unsigned)(e))
/* ================================================================ fixed-point lengths */

/* floor(log2(n) * 65536) for n >= 1, by integer squaring: the same on every machine. A number that fits 32 bits
   (nearly always) takes the 32-bit path: the same squarings, without 64-bit shifts. */
static int32_t lg(u64 n)
{
    int ip; int32_t f = 0;
    if (n <= 0xFFFFFFFFul) {
        uint32_t v = (uint32_t)n, m;
        ip = 31; while (!(v & 0x80000000ul)) { v <<= 1; ip--; }
        m = v >> 1;                                               /* Q30, in [1, 2) */
        for (int i = 0; i < 16; i++) {
            u64 t = (u64)m * m;
            m = ((uint32_t)(t >> 32) << 2) | (uint8_t)((uint8_t)((uint32_t)t >> 24) >> 6);   /* t >> 30 */
            f <<= 1;
            if (m & 0x80000000ul) { m >>= 1; f |= 1; }
        }
        return (int32_t)ip * ONE + f;
    }
    ip = 0;
    while (ip < 63 && (n >> (ip + 1)) != 0) ip++;
    u64 m = ip >= 30 ? n >> (ip - 30) : n << (30 - ip);          /* Q30, in [1, 2) */
    for (int i = 0; i < 16; i++) {
        m = (m * m) >> 30; f <<= 1;
        if (m >= (1ULL << 31)) { m >>= 1; f |= 1; }
    }
    return (int32_t)ip * ONE + f;
}

static u64 uabs(i64 v) { return v < 0 ? (u64)0 - (u64)v : (u64)v; }

/* length of a rational p/q (q > 0): 1 + 2 log2(|p| + 1) + 2 log2(q) bits */
static int32_t numbits(i64 p, i64 q) { return ONE + 2 * lg(uabs(p) + 1) + 2 * lg((u64)q); }

/* log2 of how many formulas of cost <= c there are: lg of 1, 41, 554, 1842, 17512, 61017, 85089 */
static const int32_t FBITS[8] ROM = {0, 351112, 597278, 710872, 923799, 1041820, 1073262, 1073262};

/* the largest formula cost a piece may use, given how many examples it has beyond its coefficients */
static int budget(int checks) { return checks < 1 ? -1 : checks == 1 ? 2 : checks == 2 ? 4 : 99; }

static u64 gcd(u64 a, u64 b) { while (b) { u64 t = a % b; a = b; b = t; } return a; }

/* ================================================================ the formula library */

enum { U_ABS, U_SQ, U_CUBE, U_DIGSUM, U_REV, U_POPC, U_NDIG, U_SIGN, U_NEG,
       U_MOD, U_CMOD, U_FDIV, U_CDIV, U_AND, U_OR, U_XOR, U_SHR, U_SHL, U_NONE };
#define NUOP 284                         /* one-step operations */
#define NNW  146                         /* the first 146 can be built on; the rest are bit masks with 16..62 */
typedef struct { uint8_t kind, cost; int16_t k; } uop;

/* operation number o, in a fixed order (no table: the list is worked out from the number) */
static uop uop_at(int o)
{
    static const uint8_t KS[18] ROM = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 16, 20, 25, 32, 50, 64, 100};
    uop u; u.k = 0; u.cost = 2;
    if (o < 9) u.kind = (uint8_t)o;
    else if (o < 81) { o -= 9; u.kind = (uint8_t)(U_MOD + (o & 3)); u.k = ROMU8(KS, o >> 2); }
    else if (o < 138) { o -= 81; u.kind = (uint8_t)(U_AND + o % 3); o /= 3; u.k = (int16_t)(o < 15 ? o + 1 : (32 << (o - 15)) - 1); }
    else if (o < NNW) { o -= 138; u.kind = (uint8_t)(U_SHR + (o & 1)); u.k = (int16_t)((o >> 1) + 1); }
    else { o -= NNW; u.kind = (uint8_t)(U_AND + o % 3); o /= 3; u.k = (int16_t)(o < 15 ? o + 16 : o + 17); u.cost = 3; return u; }
    if (u.kind == U_ABS || u.kind == U_SQ || u.kind == U_SIGN || u.kind == U_NEG || u.kind == U_MOD || u.kind == U_FDIV) u.cost = 1;
    return u;
}
/* may another operation be applied on top of a*x + b after this one? */
static int inner(int kind) { return !(kind == U_DIGSUM || kind == U_REV || kind == U_POPC || kind == U_NDIG || kind == U_SIGN || kind == U_NEG || kind == U_SHL); }

#define UABS(v) ((v) < 0 ? (uval)0 - (uval)(v) : (uval)(v))
static val fdivv(val a, val k) { val q = a / k; if (a % k != 0 && a < 0) q--; return q; }
static val fmodv(val a, val k) { val r = a % k; return r < 0 ? r + k : r; }

/* one unary operation on one value; returns 0 when the result does not fit */
static int apply_u(uop u, val v, val *out)
{
    val r; int k = u.k;
    switch (u.kind) {
    case U_ABS: r = v < 0 ? -v : v; break;
    case U_SQ: if (!vmul(v, v, &r)) return 0; break;
    case U_CUBE: if (!vmul(v, v, &r) || !vmul(r, v, &r)) return 0; break;
    case U_DIGSUM: { uval a = UABS(v); r = 0; do { r += (val)(a % 10); a /= 10; } while (a); } break;
    case U_REV: { uval a = UABS(v); r = 0;
        do { if (!vmul(r, 10, &r) || !vadd(r, (val)(a % 10), &r)) return 0; a /= 10; } while (a);
        if (v < 0) r = -r; } break;
    case U_POPC: { uint32_t w = (uint32_t)(u64)v; r = 0; while (w) { r += w & 1u; w >>= 1; } } break;
    case U_NDIG: { uval a = UABS(v); r = 0; do { r++; a /= 10; } while (a); } break;
    case U_SIGN: r = (v > 0) - (v < 0); break;
    case U_NEG: r = -v; break;
#ifdef BT_VAL32
    case U_MOD: case U_CMOD: case U_FDIV: case U_CDIV: {
        val q, m;                                /* quotient and remainder, truncated towards zero */
        if (v == (int16_t)v) { q = (int16_t)v / (int16_t)k; m = (int16_t)v % (int16_t)k; }   /* 16 bits when it fits: far cheaper */
        else { q = v / k; m = v % k; }
        r = u.kind == U_CMOD ? m : u.kind == U_CDIV ? q : u.kind == U_MOD ? (m < 0 ? m + k : m) : (m != 0 && v < 0 ? q - 1 : q);
    } break;
#else
    case U_MOD: if (v == (int32_t)v) { int32_t w = (int32_t)v % k; r = w < 0 ? w + k : w; } else r = fmodv(v, k); break;
    case U_CMOD: r = v == (int32_t)v ? (i64)((int32_t)v % k) : v % k; break;
    case U_FDIV: if (v == (int32_t)v) { int32_t w = (int32_t)v / k; if ((int32_t)v % k != 0 && v < 0) w--; r = w; } else r = fdivv(v, k); break;
    case U_CDIV: r = v == (int32_t)v ? (i64)((int32_t)v / k) : v / k; break;
#endif
    case U_AND: r = v & (val)k; break;
    case U_OR: r = v | (val)k; break;
    case U_XOR: r = v ^ (val)k; break;
    case U_SHR: r = v >= 0 ? v >> k : -((-(v + 1)) >> k) - 1; break;
    case U_SHL: if (!vmul(v, (val)1 << k, &r)) return 0; break;
    default: return 0;
    }
    *out = r; return 1;
}

enum { B_ADD, B_SUB, B_MUL, B_MIN, B_MAX, B_AND, B_OR, B_XOR, NBOP };
static int apply_b(int op, val a, val b, val *out)
{
    switch (op) {
    case B_ADD: return vadd(a, b, out);
    case B_SUB: return vsub(a, b, out);
    case B_MUL: return vmul(a, b, out);
    case B_MIN: *out = a < b ? a : b; return 1;
    case B_MAX: *out = a > b ? a : b; return 1;
    case B_AND: *out = a & b; return 1;
    case B_OR: *out = a | b; return 1;
    default: *out = a ^ b; return 1;
    }
}

/* ================================================================ the problem, and one batch of pieces */

/* A piece is a set of examples that one formula must fit. Eight bytes each. */
typedef struct {
    uint16_t e;                         /* which examples */
    uint8_t fl;                         /* which questions, and the flags below */
    int8_t fc;                          /* cost of the best formula so far; 127 none; -1 a constant */
    int32_t c;                          /* its length (Q16) */
} piece;
enum { F_QM = 3, F_SAME = 4, F_PEEL = 8, F_K2 = 16, F_SMALL = 32, F_PEELOK = 64, F_DONE = 128 };
/* F_SAME: begins with the same three examples as the piece before it;  F_PEEL: the best is the square of a formula;
   F_K2: two examples;  F_SMALL: outputs small enough that cross products fit in 64 bits;
   F_PEELOK: every output is a perfect square (and they are not all tiny);  F_DONE: answered, in the last pass */

/* the best tree so far: its length and its leaves (at most four), kept so the last pass can work out the answers */
typedef struct { int32_t c; uint8_t ok, by, cap, nleaf; piece leaf[4]; } cand;

/* A formula under test: out(A [, B]), where A and B are x itself or a one-step formula of x, or (A only) a*x + b;
   or max(x, k), min(x, k). It is never stored as a list of values: each value is worked out when first needed. */
typedef struct {
    uop ua, ub, uo;                     /* kind U_NONE when absent */
    int8_t la, lb;                      /* A = la * x + lb when la != 0 */
    int8_t bop;                         /* the binary operation on A and B, or -1 */
    int8_t mm, mk;                      /* 1: max(x, mk); 2: min(x, mk) */
} formula;

typedef struct {
    const bt_int *x, *y, *q;            /* the caller's arrays: nothing is copied */
    piece *P;                           /* the batch */
    val w[MAXP];                        /* the formula under test, at the points worked out so far */
    val wa[MAXP];                       /* its inner step A, likewise (it stays the same for many formulas) */
    uint8_t known[MAXP], knowna[MAXP];  /* which points of w and of wa are worked out */
    i64 pred[2];                        /* the answers, filled in by the last pass */
    uint16_t CE[MAXCOND];               /* each condition: which examples satisfy it */
    uint8_t CQ[(MAXCOND + 3) / 4];      /* and which questions (two bits each) */
    uint32_t rep[5], c1[5];             /* one-step formulas of x: which are distinct, and which cost 1 */
    int32_t lgm;                        /* log2 of how many conditions there are */
    int32_t limit;                      /* the longest rule that would be accepted: (n - 2) log2(spread of the outputs) */
    uint16_t sq;                        /* which outputs are perfect squares */
    uint8_t ne, nq, np, ncond, npiece, n3, cap, replay, left, dead;   /* n3: the pieces of three or more examples */
#ifdef BT_DEEP
    piece ds[7];                        /* the deep search's sides, as the two-piece search found them */
#endif
} ctx;

/* point p: an example's x, or a question */
static val XP(const ctx *C, int p) { return p < C->ne ? C->x[p] : C->q[p - C->ne]; }

/* the formula's value at point p; 0 when it does not fit there */
static int feval(ctx *C, const formula *f, int p, val *out)
{
    val a = XP(C, p), b, r;
    if (f->mm) { *out = f->mm == 1 ? (a > f->mk ? a : f->mk) : (a < f->mk ? a : f->mk); return 1; }
    if (f->la) {
#ifdef BT_VAL32
        if (!vmul(f->la, a, &a) || !vadd(a, f->lb, &a)) return 0;
#else
        a = (val)((u64)(i64)f->la * (u64)a + (u64)(i64)f->lb);
#endif
    } else if (f->ua.kind != U_NONE) {                               /* always fits: see build_l1 */
        if (!C->knowna[p]) { apply_u(f->ua, a, &C->wa[p]); C->knowna[p] = 1; }
        a = C->wa[p];
    }
    if (f->bop >= 0) {
        b = XP(C, p); if (f->ub.kind != U_NONE) apply_u(f->ub, b, &b);
        if (!apply_b(f->bop, a, b, &r)) return 0;
    } else if (f->uo.kind != U_NONE) { if (!apply_u(f->uo, a, &r)) return 0; }
    else { *out = a; return 1; }
    if (r > LIMIT || r < -LIMIT) return 0;
    *out = r; return 1;
}

/* make sure the formula's value at point p is in w; 0 (and the formula is dead) when it does not fit there.
   The usual case, a value already worked out, costs one test and no call. */
static NOINLINE int get_slow(ctx *C, const formula *f, int p)
{
    if (!feval(C, f, p, &C->w[p])) { C->dead = 1; return 0; }
    C->known[p] = 1;
    return 1;
}
#define get(C, f, p) ((C)->known[p] || get_slow(C, f, p))
static int fill(ctx *C, const formula *f) { for (int p = 0; p < C->np; p++) if (!get(C, f, p)) return 0; return 1; }
static void forget(uint8_t *known, int np) { for (int p = 0; p < np; p++) known[p] = 0; }

static int is_square(val y)
{
    if (y < 0) return 0;
    uval n = (uval)y, r = 0, bit = (uval)1 << (sizeof(uval) * 8 - 2);
    while (bit > n) bit >>= 2;
    while (bit) { if (n >= r + bit) { n -= r + bit; r = (r >> 1) + bit; } else r >>= 1; bit >>= 2; }
    return n == 0;
}

#define REP(C, j) ((C)->rep[(j) >> 5] >> ((j) & 31) & 1)
#define L1COST(C, j) (((C)->c1[(j) >> 5] >> ((j) & 31) & 1) ? 1 : 2)

/* which of the first 146 one-step formulas of x are distinct on this problem's points (valid at every point, and
   not x itself), and the smallest cost in each group of equal ones */
static void build_l1(ctx *C)
{
    val *v = C->w;
    for (int w = 0; w < 5; w++) C->rep[w] = C->c1[w] = 0;
    for (int o = 0; o < NNW; o++) {
        uop u = uop_at(o);
        int ok = 1, same = 1;
        for (int i = 0; i < C->np && ok; i++) { ok = apply_u(u, XP(C, i), &v[i]) && v[i] <= LIMIT && v[i] >= -LIMIT; if (ok && v[i] != XP(C, i)) same = 0; }
        if (!ok || same) continue;
        int at = o;
        for (int j = 0; j < o && at == o; j++) {
            if (!REP(C, j)) continue;
            uop w = uop_at(j); int eq = 1;
            for (int i = 0; i < C->np && eq; i++) { val t = 0; apply_u(w, XP(C, i), &t); eq = t == v[i]; }
            if (eq) at = j;
        }
        if (at == o) C->rep[o >> 5] |= 1ul << (o & 31);
        if (u.cost == 1) C->c1[at >> 5] |= 1ul << (at & 31);
    }
}

static void piece_init(const ctx *C, piece *p, uint32_t e, int qm)
{
    const bt_int *Y = C->y;
    int k = POPC(e), first = CTZ(e), same = 1, small = 1, okp = 1, n4 = 0, big = 0;
    p->e = (uint16_t)e; p->fl = (uint8_t)qm; p->fc = 127; p->c = 0x7fffffff;
    for (uint32_t m = e; m; m &= m - 1) {
        int i = CTZ(m);
        if (Y[i] != Y[first]) same = 0;
#ifndef BT_VAL32
        if (Y[i] <= -(1LL << 21) || Y[i] >= (1LL << 21)) small = 0;
#endif
        if (!(C->sq >> i & 1)) okp = 0;
        if (Y[i] >= 16) n4++;
        if (Y[i] >= 100) big = 1;
    }
    if (same && k >= 2) { p->fc = -1; p->c = ONE + numbits(Y[first], 1); }      /* a constant piece */
    if (k == 2) p->fl |= F_K2;
    if (small) p->fl |= F_SMALL;
    if (okp && big && n4 >= 2 && k >= 2) p->fl |= F_PEELOK;
}

static int find(const ctx *C, uint32_t e, int qm)
{
    for (int i = 0; i < C->npiece; i++) if (C->P[i].e == e && (C->P[i].fl & F_QM) == qm) return i;
    return -1;
}

/* puts a piece in the batch if it is not there; 0 when the batch is full */
static int add(ctx *C, uint32_t e, int qm)
{
    if (find(C, e, qm) >= 0) return 1;
    if (C->npiece >= BT_BATCH) return 0;
    piece_init(C, &C->P[C->npiece++], e, qm);
    return 1;
}

/* is this formula better than the piece's best so far? A plain formula wins a tie with a squared one. */
static int better(const piece *p, int32_t c, int fc, int peel)
{
    if (p->fc == 127) return 1;
    if (!peel == !(p->fl & F_PEEL)) return c < p->c || (c == p->c && fc < p->fc);
    return peel ? c < p->c : c <= p->c;
}

static void take(ctx *C, piece *p, int32_t c, int fc, int peel, const i64 *pr)
{
    if (C->replay) {                    /* the last pass: this is the formula that won; keep its answers */
        if (c != p->c || fc != p->fc || !peel != !(p->fl & F_PEEL)) return;
        for (int s = 0; s < C->nq; s++) if (p->fl >> s & 1) C->pred[s] = pr[s];
        p->fl |= F_DONE; C->left--;
        return;
    }
    p->c = c; p->fc = (int8_t)fc; p->fl = (uint8_t)(peel ? p->fl | F_PEEL : p->fl & ~F_PEEL);
}

/* y = F^2 on this piece's examples? */
static void test_peel(ctx *C, const formula *f, piece *p, int fc)
{
    const val *F = C->w;
    for (uint16_t m = p->e; m; m &= (uint16_t)(m - 1)) {
        int j = CTZ(m); i64 s;
        if (!get(C, f, j)) return;
        if (MULO(F[j], F[j], &s) || s != C->y[j]) return;
    }
    int32_t c = ROM32(FBITS, fc + 1 < 7 ? fc + 1 : 7) + ONE;
    if (!C->replay && !better(p, c, fc, 1)) return;
    if (!fill(C, f)) return;                     /* it must be valid at every point to count */
    i64 pr[2] = {0, 0};
    for (int s = 0; s < C->nq; s++) if (p->fl >> s & 1) { if (MULO(F[C->ne + s], F[C->ne + s], &pr[s])) return; }
    take(C, p, c, fc, 1, pr);
}

#ifdef BT_VAL32
/* |v| / d and |v| % d for a small d (at most 12), a byte at a time: the 32-bit build has no 64-bit division */
static u64 div_small(u64 v, uint8_t d, uint8_t *rem)
{
    u64 q = 0; uint16_t r = 0;
    for (int8_t i = 7; i >= 0; i--) { r = (uint16_t)(r << 8 | (uint8_t)(v >> (8 * i))); q = q << 8 | (uint8_t)(r / d); r %= d; }
    *rem = (uint8_t)r;
    return q;
}
static uint32_t gcd32(uint32_t a, uint32_t b) { while (b) { uint32_t t = a % b; a = b; b = t; } return a; }
#endif

/* does y = a F + b hold exactly on this piece's examples, and is it this piece's shortest formula so far? */
static void test(ctx *C, const formula *f, piece *p, int fc)
{
    const val *F = C->w; const bt_int *Y = C->y;
    uint16_t e = p->e;
    int i0 = CTZ(e);
    if (!get(C, f, i0)) return;
    val F0 = F[i0]; bt_int y0 = Y[i0]; dif dF = 0, dy = 0;
    if ((p->fl & F_K2) && fc > 2) {                               /* two examples: only y = F itself may cost more than 2 */
        int j = CTZ(e & (e - 1));
        if (F0 != y0 || !get(C, f, j) || F[j] != Y[j]) return;
    }
    int have = 0;
    for (uint16_t m = e & (uint16_t)(e - 1); m; m &= (uint16_t)(m - 1)) {
        int j = CTZ(m);
        if (!get(C, f, j)) return;
        if (!have) {
            if (F[j] == F0) { if (Y[j] != y0) return; }
            else { dF = DIFF(F[j], F0); dy = DIFF(Y[j], y0); have = 1; }
        }
#ifndef BT_VAL32
        else if (!(p->fl & F_SMALL)) {
            i64 l, r;
            if (__builtin_mul_overflow(DIFF(Y[j], y0), dF, &l) || __builtin_mul_overflow(dy, DIFF(F[j], F0), &r) || l != r) return;
        }
#endif
        else if ((i64)DIFF(Y[j], y0) * dF != (i64)dy * DIFF(F[j], F0)) return;
    }
    if (!have) return;
    i64 B, t;                                    /* b = B / d */
#ifdef BT_VAL32
    uint32_t g = gcd32((uint32_t)(dy < 0 ? -dy : dy), (uint32_t)(dF < 0 ? -dF : dF));
    dif pp = dy / (dif)g, d = dF / (dif)g;
    if (d < 0) { pp = -pp; d = -d; }
    if (d > 12) return;
    B = (i64)d * y0 - (i64)pp * F0; (void)t;
#else
    u64 g = gcd(uabs(dy), uabs(dF));
    i64 pp = dy / (i64)g, d = dF / (i64)g;
    if (d < 0) { pp = -pp; d = -d; }
    if (d > 12) return;
    if (MULO(pp, F0, &t) || MULO(d, y0, &B) || SUBO(B, t, &B)) return;
#endif
    int slope = !(pp == 1 && d == 1), npar = slope + (B != 0);
    if (fc > budget(POPC(e) - npar)) return;
    int32_t c = ROM32(FBITS, fc) + ONE;
    /* its length is at least this (a fitted number takes 3 bits or more; a zero slope, 1): if the piece already has
       a shorter formula, stop before any logarithm */
    if (p->fc != 127 && c + (slope ? (pp ? 3 * ONE : ONE) : 0) + (B != 0 ? 3 * ONE : 0) > p->c) return;
    if (slope) c += numbits(pp, d);
#ifdef BT_VAL32
    if (B != 0) {
        uint8_t r, gb; u64 qb;
        div_small(uabs(B), (uint8_t)d, &r);
        gb = (uint8_t)gcd32((uint32_t)d, r);
        qb = div_small(uabs(B), gb, &r);
        c += numbits(B < 0 ? -(i64)qb : (i64)qb, d / gb);
    }
#else
    if (B != 0) { i64 gb = (i64)gcd(uabs(B), (u64)d); c += numbits(B / gb, d / gb); }
#endif
    if (!C->replay && !better(p, c, fc, 0)) return;
    if (!fill(C, f)) return;                     /* it must be valid at every point to count */
    i64 pr[2] = {0, 0};
    for (int s = 0; s < C->nq; s++) {
        if (!(p->fl >> s & 1)) continue;
        i64 num;
#ifdef BT_VAL32
        uint8_t r; u64 qn;
        num = (i64)pp * F[C->ne + s] + B;
        qn = div_small(uabs(num), (uint8_t)d, &r);
        if (r) return;
        pr[s] = num < 0 ? -(i64)qn : (i64)qn;
#else
        if (MULO(pp, F[C->ne + s], &num) || ADDO(num, B, &num) || num % d) return;
        pr[s] = num / d;
#endif
    }
    take(C, p, c, fc, 0, pr);
}

/* A quick look at three of a piece's examples (its group's first three; two, for a two-example piece): a formula
   that is not affine there cannot fit, and nearly every formula fails right here. Every piece of a group contains
   these examples, so they share the look. Only these examples' values are worked out. */
static int lead_ok(ctx *C, const formula *f, const piece *p, int cost)
{
    const bt_int *Y = C->y; const val *F = C->w;
    uint16_t e = p->e;
    uint8_t a = (uint8_t)CTZ(e); e &= (uint16_t)(e - 1);
    uint8_t b = (uint8_t)CTZ(e);
    if (!get(C, f, a)) return 0;
    val fa = F[a], fb; bt_int ya = Y[a], yb;
    if (p->fl & F_K2) {
        /* two examples fit only y = F itself (any cheap formula), or y = F + b or y = a F (cost 2 at most) */
        if (cost > 2) return fa == ya && get(C, f, b) && F[b] == Y[b];
        if (!get(C, f, b)) return 0;
        fb = F[b]; yb = Y[b];
        if (fb == fa) return 0;                                   /* equal values: no slope (or a contradiction) */
        dif dF = DIFF(fb, fa), dy = DIFF(yb, ya);
        if (dy == dF) return 1;
#ifndef BT_VAL32
        if (!(p->fl & F_SMALL)) return 1;        /* too large to tell quickly: let the full test decide */
#endif
        if ((uint8_t)((uint8_t)ya * (uint8_t)dF) != (uint8_t)((uint8_t)fa * (uint8_t)dy)) return 0;
        return (i64)ya * dF == (i64)fa * dy;
    }
    if (!get(C, f, b)) return 0;
    e &= (uint16_t)(e - 1);
    uint8_t c = (uint8_t)CTZ(e);
    if (!get(C, f, c)) return 0;
    /* (yc - ya)(fb - fa) = (yb - ya)(fc - fa) must hold in the last byte too: four 8-bit subtractions and two 8-bit
       multiplications settle nearly every formula, reading one byte of each number, before any wide arithmetic */
    uint8_t fa8 = (uint8_t)fa, fb8 = (uint8_t)F[b], ya8 = (uint8_t)ya;
    if (fb8 != fa8 || F[b] != fa) {
        if ((uint8_t)((uint8_t)((uint8_t)Y[c] - ya8) * (uint8_t)(fb8 - fa8)) != (uint8_t)((uint8_t)((uint8_t)Y[b] - ya8) * (uint8_t)((uint8_t)F[c] - fa8))) return 0;
    }
    fb = F[b]; yb = Y[b];
    val fc = F[c]; bt_int yc = Y[c];
    if (fb == fa) return yb == ya && (fc != fa || yc == ya);
    dif dF = DIFF(fb, fa), dy = DIFF(yb, ya), dc = DIFF(yc, ya), fd = DIFF(fc, fa);
#ifndef BT_VAL32
    if (!(p->fl & F_SMALL)) { i64 l, r; return !__builtin_mul_overflow(dc, dF, &l) && !__builtin_mul_overflow(dy, fd, &r) && l == r; }
#endif
    return (i64)dc * dF == (i64)dy * fd;
}

/* one formula against every piece of the batch */
static void visit(ctx *C, const formula *f, int cost)
{
    uint8_t ok = 0;
    piece *p = C->P, *end = p + (cost > 4 ? C->n3 : C->npiece);   /* the two-example pieces come last, and use cheap formulas only */
    forget(C->known, C->np); C->dead = 0;
    for (; p != end; p++) {
        uint8_t fl = p->fl;
        if (fl & F_SAME) { if (!ok && !(fl & F_PEELOK)) continue; }  /* its group already failed: nearly every piece ends here */
        else if (fl & F_DONE) { ok = 0; continue; }
        else if ((fl & F_K2) && cost > 4) { ok = 0; continue; }
        else { ok = (uint8_t)lead_ok(C, f, p, cost); if (C->dead) return; if (!ok && !(fl & F_PEELOK)) continue; }
        if (fl & F_DONE) continue;
        uint8_t low = C->cap == 4 || (fl & F_K2);                 /* this piece may only use cheap formulas */
        if (ok && (!low || cost <= 4)) { test(C, f, p, cost); if (C->dead) return; }
        if ((fl & F_PEELOK) && cost <= (low ? 3 : 98)) { test_peel(C, f, p, cost); if (C->dead) return; }
    }
}

/* every formula in the library, in a fixed order, at most maxcost */
static void enumerate(ctx *C, int maxcost)
{
    static const int8_t AL[14] ROM = {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, -1, -2, -3};
    formula f;
    if (maxcost < 0) return;
    f.ua.kind = f.ub.kind = f.uo.kind = U_NONE; f.la = f.lb = 0; f.bop = -1; f.mm = f.mk = 0;
#define STOP (C->replay && !C->left)
    /* one piece only (the first pass): stop as soon as it has a formula shorter than any still to come can be.
       After x itself every formula costs 1 or more; after the one-step formulas, 2 or more. */
#define SHORT(k) (!C->replay && C->npiece == 1 && C->P[0].fc != 127 && C->P[0].c < ROM32(FBITS, k) + ONE)
    visit(C, &f, 0);
    if (SHORT(1)) return;
    for (int o = 0; o < NUOP && !STOP; o++) { f.uo = uop_at(o); if (f.uo.cost <= maxcost) visit(C, &f, f.uo.cost); }
    f.uo.kind = U_NONE;
    if (SHORT(2)) return;
    if (maxcost >= 2) for (int k = -20; k <= 20 && !STOP; k++) {
        f.mk = (int8_t)k;
        f.mm = 1; visit(C, &f, 2);
        f.mm = 2; visit(C, &f, 2);
    }
    f.mm = 0;
    for (int ai = 0; ai < 14; ai++) for (int bb = -12; bb <= 12 && !STOP; bb++) {
        f.la = ROM8(AL, ai); f.lb = (int8_t)bb; f.uo.kind = U_NONE;
        if (bb && maxcost >= 2) visit(C, &f, 2);
        for (int o = 0; o < NUOP; o++) {
            uop u = uop_at(o); int c = u.cost + 2 + (bb != 0);
            if (!inner(u.kind) || c > maxcost) continue;
            f.uo = u; visit(C, &f, c);
        }
    }
    f.la = 0; f.lb = 0;
    for (int j = 0; j < NNW && !STOP; j++) {
        if (!REP(C, j)) continue;
        int cj = L1COST(C, j);
        if (cj + 2 > maxcost) continue;
        f.ua = uop_at(j); forget(C->knowna, C->np);
        for (int o = 0; o < NUOP; o++) {
            uop u = uop_at(o); int c = cj + u.cost + 1;
            if (c <= maxcost) { f.uo = u; visit(C, &f, c); }
        }
    }
    f.uo.kind = U_NONE;
    for (int i = -1; i < NNW && !STOP; i++) {
        if (i >= 0 && !REP(C, i)) continue;
        int ca = i < 0 ? 0 : L1COST(C, i);
        if (ca + 2 > maxcost) continue;
        if (i < 0) f.ua.kind = U_NONE; else f.ua = uop_at(i);
        forget(C->knowna, C->np);
        for (int j = i; j < NNW; j++) {
            if (j >= 0 && !REP(C, j)) continue;
            int cb = j < 0 ? 0 : L1COST(C, j);
            if (ca + cb + 2 > maxcost) continue;
            if (j < 0) f.ub.kind = U_NONE; else f.ub = uop_at(j);
            for (int op = 0; op < NBOP; op++) { f.bop = (int8_t)op; visit(C, &f, ca + cb + 2); }
        }
    }
#undef STOP
#undef SHORT
}

/* the order pieces are sorted into before grouping: three or more examples first, fewest first; pairs last */
static int before(const piece *a, const piece *b)
{
    int ka = (a->fl & F_K2) ? 99 : POPC(a->e), kb = (b->fl & F_K2) ? 99 : POPC(b->e);
    return ka != kb ? ka < kb : a->e < b->e;
}

/* one pass over the batch: cap says what its pieces may use (4: cheap formulas only; 99: any), maxcost how costly
   a formula is still worth trying (see worth) */
static void run_pass(ctx *C, int cap, int maxcost)
{
    piece *P = C->P; int n = C->npiece, n3 = 0;
    for (int i = 1; i < n; i++) {
        piece t = P[i]; int j = i - 1;
        while (j >= 0 && before(&t, &P[j])) { P[j + 1] = P[j]; j--; }
        P[j + 1] = t;
    }
    while (n3 < n && !(P[n3].fl & F_K2)) n3++;
    /* groups: the smallest piece left starts one; its first three examples are the group's, and every later piece
       that contains them joins it. One quick look at those three examples then serves the whole group. */
    for (int i = 0; i < n3; ) {
        uint16_t e = P[i].e, t3 = e & (uint16_t)(e - 1); t3 &= (uint16_t)(t3 - 1); t3 = (uint16_t)(e ^ (t3 & (uint16_t)(t3 - 1)));
        int j = i + 1;
        P[i].fl &= (uint8_t)~F_SAME;
        for (int k = i + 1; k < n3; k++) if ((P[k].e & t3) == t3) { piece t = P[k]; P[k] = P[j]; P[j] = t; P[j].fl |= F_SAME; j++; }
        i = j;
    }
    for (int i = n3; i < n; i++) { if (i > n3 && P[i].e == P[i - 1].e) P[i].fl |= F_SAME; else P[i].fl &= (uint8_t)~F_SAME; }
    C->n3 = (uint8_t)n3; C->cap = (uint8_t)cap;
    enumerate(C, maxcost < cap ? maxcost : cap);
}

/* A tree can only matter if it is at most this long: shorter than the best accepted so far, or else within the
   limit. So a leaf can only matter if it leaves room for the rest of its tree, and a formula of cost fc, whose leaf
   is at least FBITS[fc] + 1 bits, only if that fits: the largest such cost (-1: not even x itself; constants still
   count, they need no pass). Nothing that could change the answer is skipped. */
static int32_t room(const ctx *C, const cand *best) { return best->ok && best->c <= C->limit ? best->c - 1 : C->limit; }
static int worth(int32_t leafmax, int top)
{
    int fc = -1;
    while (fc < top && ROM32(FBITS, fc + 1) + ONE <= leafmax) fc++;
    return fc;
}

/* ================================================================ the search over trees of pieces */

#define CQM(C, ci) ((int)((C)->CQ[(ci) >> 2] >> (((ci) & 3) * 2) & 3))    /* the questions that satisfy condition ci */

/* conditions: x % m == r, then x below each gap between the examples (at its middle, or at zero) */
static void cond_set(ctx *C, int ci, uint32_t mk)
{
    C->CE[ci] = (uint16_t)(mk & ((1ul << C->ne) - 1));
    if (!(ci & 3)) C->CQ[ci >> 2] = 0;
    C->CQ[ci >> 2] |= (uint8_t)(((mk >> C->ne) & 3u) << ((ci & 3) * 2));
}
static NOINLINE void conditions(ctx *C)
{
    int n = C->ne, np = C->np, nc = 0, ns = 0;
    val sx[MAXE];
    for (int m = 2; m <= 10; m++) for (int r = 0; r < m; r++) {
        uint32_t mk = 0; for (int i = 0; i < np; i++) if (fmodv(XP(C, i), m) == r) mk |= 1ul << i;
        cond_set(C, nc++, mk);
    }
    for (int i = 0; i < n; i++) { int d = 0; for (int j = 0; j < ns; j++) if (sx[j] == C->x[i]) d = 1; if (!d) sx[ns++] = C->x[i]; }
    for (int i = 1; i < ns; i++) { val t = sx[i]; int j = i - 1; while (j >= 0 && sx[j] > t) { sx[j + 1] = sx[j]; j--; } sx[j + 1] = t; }
    for (int i = 0; i + 1 < ns; i++) {
        i64 t2 = (sx[i] < 0 && sx[i + 1] >= 0) ? -1 : (i64)((u64)sx[i] + (u64)sx[i + 1]);
        uint32_t mk = 0; for (int p = 0; p < np; p++) if ((i64)((u64)XP(C, p) + (u64)XP(C, p)) <= t2) mk |= 1ul << p;
        cond_set(C, nc++, mk);
    }
    C->ncond = (uint8_t)nc; C->lgm = lg((u64)nc);
}

/* a condition that splits the examples into two parts of two or more, and no earlier one splits them the same */
static int top_ok(const ctx *C, int ci, uint32_t *L, uint32_t *R)
{
    uint32_t all = (1ul << C->ne) - 1, l = C->CE[ci], r = all & ~l;
    if (POPC(l) < 2 || POPC(r) < 2) return 0;
    for (int j = 0; j < ci; j++) if (C->CE[j] == l) return 0;
    *L = l; *R = r;
    return 1;
}

static void offer(cand *best, int32_t c, int by, int cap, int nleaf, const piece *l0, const piece *l1, const piece *l2, const piece *l3)
{
    if (best->ok && c >= best->c) return;
    best->ok = 1; best->c = c; best->by = (uint8_t)by; best->cap = (uint8_t)cap; best->nleaf = (uint8_t)nleaf;
    if (l0) best->leaf[0] = *l0;
    if (l1) best->leaf[1] = *l1;
    if (l2) best->leaf[2] = *l2;
    if (l3) best->leaf[3] = *l3;
}

/* two pieces, any formula each */
static void two_pieces(ctx *C, cand *best)
{
    int qa = (1 << C->nq) - 1, ci = 0;
    uint32_t L, R;
    while (ci < C->ncond) {
        int start = ci; C->npiece = 0;
        for (; ci < C->ncond; ci++) {
            if (!top_ok(C, ci, &L, &R)) continue;
            int mark = C->npiece, lq = qa & CQM(C, ci), rq = qa & ~CQM(C, ci);
            if (!add(C, L, lq) || !add(C, R, rq)) { C->npiece = (uint8_t)mark; break; }
        }
        run_pass(C, 99, worth(room(C, best) - C->lgm - 2 * ONE, 6));
        for (int cj = start; cj < ci; cj++) {
            if (!top_ok(C, cj, &L, &R)) continue;
            int lq = qa & CQM(C, cj), rq = qa & ~CQM(C, cj);
            const piece *l = &C->P[find(C, L, lq)], *r = &C->P[find(C, R, rq)];
#ifdef BT_DEEP
            for (int i = 0; i < 7; i++) {
                if (C->ds[i].e == l->e && (C->ds[i].fl & F_QM) == (l->fl & F_QM)) C->ds[i] = *l;
                if (C->ds[i].e == r->e && (C->ds[i].fl & F_QM) == (r->fl & F_QM)) C->ds[i] = *r;
            }
#endif
            if (l->fc == 127 || r->fc == 127) continue;
            offer(best, l->c + r->c + C->lgm + ONE, BT_PIECES, 99, 2, l, r, 0, 0);
        }
    }
}

/* one side of a split, as one piece or two with cheap formulas: its length and its leaves */
typedef struct { int32_t c; uint8_t ok, nleaf; piece leaf[2]; } side;

/* reg: put the pieces this side needs in the batch (0 when they do not fit); otherwise read its result */
static int sub(ctx *C, uint32_t e, int qm, int reg, side *out)
{
    if (reg) { if (!add(C, e, qm)) return 0; }
    else {
        const piece *p = &C->P[find(C, e, qm)];
        out->ok = p->fc != 127; out->c = p->c; out->nleaf = 1; out->leaf[0] = *p;
    }
    for (int ci = 0; ci < C->ncond; ci++) {
        uint32_t L = e & C->CE[ci], R = e & ~C->CE[ci];
        if (POPC(L) < 2 || POPC(R) < 2) continue;
        int dup = 0; for (int j = 0; j < ci && !dup; j++) dup = (e & C->CE[j]) == L;
        if (dup) continue;
        int lq = qm & CQM(C, ci), rq = qm & ~CQM(C, ci);
        if (reg) { if (!add(C, L, lq) || !add(C, R, rq)) return 0; continue; }
        const piece *l = &C->P[find(C, L, lq)], *r = &C->P[find(C, R, rq)];
        if (l->fc == 127 || r->fc == 127) continue;
        int32_t t = l->c + r->c + C->lgm + ONE;
        if (out->ok && t >= out->c) continue;
        out->ok = 1; out->c = t; out->nleaf = 2; out->leaf[0] = *l; out->leaf[1] = *r;
    }
    return 1;
}

/* up to three or four pieces, cheap formulas: unit u is side (u & 1) of condition u >> 1 */
static void four_pieces(ctx *C, cand *best)
{
    int qa = (1 << C->nq) - 1, u = 0, nu = 2 * C->ncond;
    uint32_t L, R;
    side left, right; left.ok = 0;
    while (u < nu) {
        int32_t leafmax = room(C, best) - 2 * C->lgm - 4 * ONE;   /* two more leaves and two splits at least */
        if (leafmax < ONE) return;                                /* no tree of three pieces could matter any more */
        int start = u; C->npiece = 0;
        for (; u < nu; u++) {
            int ci = u >> 1;
            if (!top_ok(C, ci, &L, &R)) { u |= 1; continue; }
            int mark = C->npiece, lq = qa & CQM(C, ci), rq = qa & ~CQM(C, ci);
            if (!sub(C, (u & 1) ? R : L, (u & 1) ? rq : lq, 1, 0)) {
                if (!mark) u++;                                   /* cannot happen (see BT_BATCH): skip, never loop */
                else C->npiece = (uint8_t)mark;
                break;
            }
        }
        run_pass(C, 4, worth(leafmax, 4));
        for (int w = start; w < u; w++) {
            int ci = w >> 1;
            if (!top_ok(C, ci, &L, &R)) { w |= 1; continue; }
            int lq = qa & CQM(C, ci), rq = qa & ~CQM(C, ci);
            if (!(w & 1)) { sub(C, L, lq, 0, &left); continue; }
            sub(C, R, rq, 0, &right);
            if (!left.ok || !right.ok) continue;
            offer(best, left.c + right.c + C->lgm + ONE, BT_PIECES, 4, left.nleaf + right.nleaf, &left.leaf[0],
                  left.nleaf == 2 ? &left.leaf[1] : &right.leaf[0],
                  left.nleaf == 2 ? &right.leaf[0] : (right.nleaf == 2 ? &right.leaf[1] : 0),
                  left.nleaf == 2 && right.nleaf == 2 ? &right.leaf[1] : 0);
        }
    }
}

/* the whole search: one formula for everything, then trees. The batch lives here and is gone when this returns. */
static NOINLINE void search(ctx *C, cand *best)
{
    piece P[BT_BATCH];
    C->P = P; C->npiece = 0; C->replay = 0;
    add(C, (1ul << C->ne) - 1, (1 << C->nq) - 1);
    run_pass(C, 99, 99);
    if (P[0].fc != 127) offer(best, P[0].c, BT_RULE, 99, 1, &P[0], 0, 0, 0);
    if (best->ok && best->c <= 8 * ONE) return;
    conditions(C);
    two_pieces(C, best);
    if (C->ne >= 8) four_pieces(C, best);
}

/* the last pass: run the formulas again until each winning leaf's formula comes by, and take its answers */
static NOINLINE void replay(ctx *C, cand *b)
{
    int maxfc = 0;
    C->P = b->leaf; C->npiece = b->nleaf; C->replay = 1; C->left = 0; C->pred[0] = C->pred[1] = 0;
    for (int i = 0; i < b->nleaf; i++) {
        piece *p = &b->leaf[i];
        p->fl &= (uint8_t)~(F_SAME | F_DONE);
        if (p->fc == -1) { for (int s = 0; s < C->nq; s++) if (p->fl >> s & 1) C->pred[s] = C->y[CTZ(p->e)]; p->fl |= F_DONE; }
        else { C->left++; if (p->fc > maxfc) maxfc = p->fc; }
    }
    C->cap = b->cap; C->n3 = b->nleaf;
    if (C->left) enumerate(C, maxfc);
}

#ifdef BT_DEEP
/* ================================================================ the deep search
   Chains of up to three operations on x, each with its own constant -- such as ((x & 35) ^ 3) % 7 -- then a*f + b.
   The table version keeps every two-step formula (19 MB); here each chain is simply tried in turn, and the third
   step is worked out only at the examples that can still rule it out. Python integer semantics: // and % floor,
   >> floors. */

enum { D_ADD, D_MUL, D_AND, D_OR, D_XOR, D_MOD, D_FDIV, D_SHR, D_SHL, D_SQ, D_ABS, D_NEG };
#define NDOP 317
#define DOPBITS 544495L                  /* lg(317) */
typedef struct { uint8_t kind; int8_t c; } dop;

static dop dop_at(int o)
{
    static const int8_t MC[14] = {-3, -2, -1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    dop d; d.c = 0;
    if (o < 40) { d.kind = D_ADD; d.c = (int8_t)(o < 20 ? o - 20 : o - 19); }
    else if (o < 54) { d.kind = D_MUL; d.c = MC[o - 40]; }
    else if (o < 243) { o -= 54; d.kind = (uint8_t)(D_AND + o % 3); d.c = (int8_t)(o / 3 + 1); }
    else if (o < 305) { o -= 243; d.kind = (o & 1) ? D_FDIV : D_MOD; d.c = (int8_t)((o >> 1) + 2); }
    else if (o < 310) { d.kind = D_SHR; d.c = (int8_t)(o - 304); }
    else if (o < 314) { d.kind = D_SHL; d.c = (int8_t)(o - 309); }
    else d.kind = (uint8_t)(D_SQ + (o - 314));
    return d;
}

static int dapply(dop o, val v, val *r)
{
    val c = o.c;
    switch (o.kind) {
    case D_ADD: *r = v + c; break;
    case D_MUL: if (!vmul(v, c, r)) return 0; break;
    case D_AND: *r = v & c; break;
    case D_OR: *r = v | c; break;
    case D_XOR: *r = v ^ c; break;
    case D_MOD: *r = fmodv(v, c); break;
    case D_FDIV: *r = fdivv(v, c); break;
    case D_SHR: *r = v >= 0 ? v >> c : -((-(v + 1)) >> c) - 1; break;
    case D_SHL: if (!vmul(v, (val)1 << c, r)) return 0; break;
    case D_SQ: if (!vmul(v, v, r)) return 0; break;
    case D_ABS: *r = v < 0 ? -v : v; break;
    default: *r = -v; break;
    }
    return *r <= LIMIT && *r >= -LIMIT;
}

/* one step on x (src 0) or on a list of values, at every point; 0 if it does not fit somewhere */
static int dvec(const ctx *C, dop o, const val *src, val *dst)
{
    for (int i = 0; i < C->np; i++) if (!dapply(o, src ? src[i] : XP(C, i), &dst[i])) return 0;
    return 1;
}
static int veq(const ctx *C, const val *a, const val *b) { for (int i = 0; i < C->np; i++) if (a[i] != (b ? b[i] : XP(C, i))) return 0; return 1; }

/* a target: a set of examples (all of them, or one side of x<0, x odd, x % 3) that one chain must fit */
typedef struct { i64 pred[2]; int32_t c; uint16_t e; uint8_t qm, k, ok, dead; } dtarget;
#define DT_MAX 8

static i64 wmul(i64 a, i64 b) { return (i64)((u64)a * (u64)b); }
static i64 wadd(i64 a, i64 b) { return (i64)((u64)a + (u64)b); }

/* a of y = a F + b, from two examples with different F; 0 if a is not a whole number or is 0 */
static int slope(i64 dF, i64 dy, i64 *a)
{
    if (dF == -1) *a = DIFF(0, dy); else { if (dy % dF) return 0; *a = dy / dF; }
    return *a != 0;
}

/* y = a F + b exactly on a target's examples, a and b integers, a != 0: its length, or -1 */
static int32_t deep_fit(const ctx *C, const dtarget *t, const val *F, i64 *pa, i64 *pb)
{
    const bt_int *Y = C->y;
    int i0 = CTZ(t->e), i1 = -1;
    for (uint32_t m = t->e & (t->e - 1ul); m; m &= m - 1) if (F[CTZ(m)] != F[i0]) { i1 = CTZ(m); break; }
    if (i1 < 0) return -1;
    i64 a, b;
    if (!slope((i64)F[i1] - F[i0], DIFF(Y[i1], Y[i0]), &a)) return -1;
    b = DIFF(Y[i0], wmul(a, F[i0]));
    for (uint32_t m = t->e; m; m &= m - 1) { int j = CTZ(m); i64 u; if (MULO(a, F[j], &u) || wadd(u, b) != Y[j]) return -1; }
    *pa = a; *pb = b;
    return (a == 1 ? 0 : numbits(a, 1)) + (b == 0 ? 0 : numbits(b, 1));
}

static void deep_offer(const ctx *C, dtarget *t, const val *F, int depth)
{
    i64 a, b;
    int32_t c = deep_fit(C, t, F, &a, &b);
    if (c < 0) return;
    c += depth * DOPBITS + ONE;
    if (t->ok && c >= t->c) return;
    t->ok = 1; t->c = c;
    for (int s = 0; s < C->nq; s++) t->pred[s] = (t->qm >> s & 1) ? wadd(wmul(a, F[C->ne + s]), b) : 0;
}

/* The third step on the two-step formula v, for the targets that have no chain yet. A chain is worked out only at
   the examples a target needs, and thrown away at the first that cannot fit; it is checked at every point (it must
   be valid at all of them) only once it fits a target. Returns the targets still open. */
static NOINLINE int deep_third(const ctx *C, dtarget *T, int open, const val *v)
{
    const bt_int *Y = C->y;
    int live = 0;
    for (int t = 0; t < DT_MAX; t++) {
        if (!(open >> t & 1)) continue;
        int ok = 1;                                              /* equal values must have equal outputs */
        for (uint32_t m = T[t].e; m && ok; m &= m - 1) {
            int i = CTZ(m);
            for (uint32_t r = m & (m - 1); r; r &= r - 1) { int j = CTZ(r); if (v[i] == v[j] && Y[i] != Y[j]) { ok = 0; break; } }
        }
        if (ok) live |= 1 << t;
    }
    if (!live) return open;
    for (int o = 54; o < NDOP - 1 && live; o++) {                 /* a last +c, *c, << or negation adds nothing to a*f + b */
        if (o >= 310 && o < 314) continue;
        dop op = dop_at(o);
        val w[MAXP]; uint32_t have = 0; int bad = 0;
#define GET(p) do { if (!(have >> (p) & 1)) { if (!dapply(op, v[p], &w[p])) { bad = 1; } have |= 1ul << (p); } } while (0)
        for (int t = 0; t < DT_MAX && !bad; t++) {
            if (!(live >> t & 1)) continue;
            uint32_t e = T[t].e;
            int i0 = CTZ(e), i1 = -1, fail = 0;
            GET(i0); if (bad) break;
            for (uint32_t m = e & (e - 1); m; m &= m - 1) {
                int j = CTZ(m);
                GET(j); if (bad) break;
                if (w[j] != w[i0]) { i1 = j; break; }
                if (Y[j] != Y[i0]) { fail = 1; break; }
            }
            if (bad) break;
            if (fail || i1 < 0) continue;
            i64 a, b;
            if (!slope((i64)w[i1] - w[i0], DIFF(Y[i1], Y[i0]), &a)) continue;
            b = DIFF(Y[i0], wmul(a, w[i0]));
            for (uint32_t m = e; m; m &= m - 1) {
                int j = CTZ(m); i64 u;
                GET(j); if (bad) break;
                if (MULO(a, w[j], &u) || wadd(u, b) != Y[j]) { fail = 1; break; }
            }
            if (bad) break;
            if (fail) continue;
            for (int p = 0; p < C->np && !bad; p++) GET(p);       /* it fits: now it must be valid everywhere */
            if (bad) break;
            T[t].ok = 1; T[t].c = (a == 1 ? 0 : numbits(a, 1)) + (b == 0 ? 0 : numbits(b, 1)) + 3 * DOPBITS + ONE;
            for (int s = 0; s < C->nq; s++) T[t].pred[s] = (T[t].qm >> s & 1) ? wadd(wmul(a, w[C->ne + s]), b) : 0;
            live &= ~(1 << t); open &= ~(1 << t);
        }
#undef GET
    }
    return open;
}

static NOINLINE void deep_run(const ctx *C, dtarget *T, int nt)
{
    val v1[MAXP], v2[MAXP];
    uint32_t rep[10];                                             /* the one-step formulas that are distinct */
    for (int i = 0; i < 10; i++) rep[i] = 0;
    for (int o = 0; o < NDOP; o++) {
        if (!dvec(C, dop_at(o), 0, v1)) continue;
        for (int t = 0; t < nt; t++) deep_offer(C, &T[t], v1, 1);
        int dup = 0;
        for (int j = 0; j < o && !dup; j++) {
            if (!(rep[j >> 5] >> (j & 31) & 1)) continue;
            dop w = dop_at(j); int eq = 1;
            for (int i = 0; i < C->np && eq; i++) { val r = 0; dapply(w, XP(C, i), &r); eq = r == v1[i]; }
            dup = eq;
        }
        if (!dup) rep[o >> 5] |= 1ul << (o & 31);
    }
    for (int o1 = 0; o1 < NDOP; o1++) {
        if (!(rep[o1 >> 5] >> (o1 & 31) & 1)) continue;
        dvec(C, dop_at(o1), 0, v1);
        for (int o2 = 0; o2 < NDOP; o2++) {
            if (!dvec(C, dop_at(o2), v1, v2)) continue;
            for (int t = 0; t < nt; t++) deep_offer(C, &T[t], v2, 2);
        }
    }
    int open = 0;
    for (int t = 0; t < nt; t++) if (!T[t].ok && T[t].k >= 4 && !T[t].dead) open |= 1 << t;
    for (int o1 = 0; o1 < NDOP && open; o1++) {
        if (!(rep[o1 >> 5] >> (o1 & 31) & 1)) continue;
        dvec(C, dop_at(o1), 0, v1);
        if (veq(C, v1, 0)) continue;                              /* its chains are one step shorter: already tried */
        for (int o2 = 0; o2 < NDOP && open; o2++) {
            if (!dvec(C, dop_at(o2), v1, v2) || veq(C, v2, v1) || veq(C, v2, 0)) continue;
            open = deep_third(C, T, open, v2);
        }
    }
}

static void dt_set(const ctx *C, dtarget *t, uint32_t e, int qm)
{
    t->e = (uint16_t)e; t->qm = (uint8_t)qm; t->k = (uint8_t)POPC(e); t->ok = 0; t->c = 0x7fffffff;
    t->pred[0] = t->pred[1] = 0; t->dead = 1;
    for (uint32_t m = e; m; m &= m - 1) if (C->y[CTZ(m)] != C->y[CTZ(e)]) t->dead = 0;
}

/* The deep search, after the ordinary one: one chain for everything, or the sides of x<0, of x odd, or the three
   classes of x % 3, each side the shorter of its chain and its ordinary formula. Returns 1 when it gives the answer
   (written to out). The targets live here, not in the caller, so the batch and they never share the stack. */
static NOINLINE int deep_phase(ctx *C, cand *best, int32_t *limit, int32_t lim2, bt_answer *out)
{
    uint32_t alle = (1ul << C->ne) - 1;
    dtarget T[DT_MAX];
    int8_t ti[7]; int32_t sc[7]; uint8_t sok[7];
    int nt = 0, dsel = -1;                        /* which deep answer won: 0 one chain, 1/2 a pair of sides, 3 the three classes */
    dt_set(C, &T[nt++], alle, (1 << C->nq) - 1);
    for (int i = 0; i < 7; i++) {
        uint32_t e = C->ds[i].e;
        if (POPC(e) >= 3 && e != alle) { ti[i] = (int8_t)nt; dt_set(C, &T[nt++], e, C->ds[i].fl & F_QM); } else ti[i] = -1;
    }
    deep_run(C, T, nt);
    int32_t bc = best->c; int okb = best->ok && best->c <= *limit;
    if (T[0].ok && T[0].c <= lim2 && (!okb || T[0].c < bc)) { bc = T[0].c; okb = 1; dsel = 0; }
    for (int i = 0; i < 7; i++) {
        uint32_t e = C->ds[i].e;
        sok[i] = 0; sc[i] = 0x7fffffff;
        if (POPC(e) < 2 || e == alle) continue;
        if (C->ds[i].fc != 127) { sok[i] = 1; sc[i] = C->ds[i].c; }
        if (ti[i] >= 0 && T[ti[i]].ok && T[ti[i]].c < sc[i]) { sok[i] = 2; sc[i] = T[ti[i]].c; }
    }
    for (int i = 0; i < 4; i += 2) if (sok[i] && sok[i + 1]) {
        int32_t c = sc[i] + sc[i + 1] + C->lgm + ONE;
        if (c <= lim2 && (!okb || c < bc)) { bc = c; okb = 1; dsel = 1 + i / 2; }
    }
    if (sok[4] && sok[5] && sok[6]) {
        int32_t c = sc[4] + sc[5] + C->lgm + ONE + sc[6] + C->lgm + ONE;
        if (c <= lim2 && (!okb || c < bc)) { bc = c; okb = 1; dsel = 3; }
    }
    if (okb && dsel < 0) *limit = best->c;
    if (!okb || dsel < 0) return 0;
    /* the answer comes from the deep search: gather its sides; ordinary ones go through the last pass */
    int first = dsel == 0 ? -1 : dsel == 1 ? 0 : dsel == 2 ? 2 : 4, cnt = dsel == 0 ? 0 : dsel == 3 ? 3 : 2;
    best->ok = 1; best->c = bc; best->by = dsel == 0 ? BT_RULE : BT_PIECES; best->cap = 99; best->nleaf = 0;
    for (int i = first; i < first + cnt; i++) if (sok[i] == 1) best->leaf[best->nleaf++] = C->ds[i];
    replay(C, best);
    if (dsel == 0) { C->pred[0] = T[0].pred[0]; C->pred[1] = T[0].pred[1]; }
    for (int i = first; i < first + cnt; i++) if (sok[i] == 2)
        for (int s = 0; s < C->nq; s++) if (T[ti[i]].qm >> s & 1) C->pred[s] = T[ti[i]].pred[s];
    out->by = best->by; out->bits = best->c; out->value[0] = C->pred[0]; out->value[1] = C->pred[1];
    return 1;
}
#endif

int bt_solve_rule(const bt_int *x, const bt_int *y, int n, const bt_int *q, int nq, bt_answer *out)
{
    if (!x || !y || !out || n < 2 || n > BT_MAX_EXAMPLES || nq < 1 || nq > BT_MAX_QUESTIONS || !q) return -1;
    out->by = BT_CANNOT; out->bits = 0; out->value[0] = out->value[1] = 0; out->est[0] = out->est[1] = 0;
    ctx C; cand best;
    C.x = x; C.y = y; C.q = q; C.ne = (uint8_t)n; C.nq = (uint8_t)nq; C.np = (uint8_t)(n + nq); C.sq = 0; C.ncond = 0; C.lgm = 0;
    for (int i = 0; i < n; i++) {
#ifdef BT_VAL32
        if (x[i] >= (1L << 26) || x[i] <= -(1L << 26) || y[i] > LIMIT || y[i] < -LIMIT) return -1;
#endif
        if (is_square(y[i])) C.sq |= (uint16_t)(1u << i);
    }
#ifdef BT_VAL32
    for (int s = 0; s < nq; s++) if (q[s] >= (1L << 26) || q[s] <= -(1L << 26)) return -1;
#endif
    build_l1(&C);
    best.ok = 0; best.c = 0; best.by = BT_CANNOT; best.nleaf = 0; best.cap = 99;
#ifdef BT_DEEP
    {   /* the sides the deep search joins: x<0 and not, x odd and not, and each class of x % 3 */
        uint32_t alle = (1ul << n) - 1, masks[7] = {0, 0, 0, 0, 0, 0, 0}; int allq = (1 << nq) - 1;
        for (int p = 0; p < C.np; p++) {
            if (XP(&C, p) < 0) masks[0] |= 1ul << p; else masks[1] |= 1ul << p;
            if (fmodv(XP(&C, p), 2)) masks[2] |= 1ul << p; else masks[3] |= 1ul << p;
            masks[4 + fmodv(XP(&C, p), 3)] |= 1ul << p;
        }
        for (int i = 0; i < 7; i++) { C.ds[i].e = (uint16_t)(masks[i] & alle); C.ds[i].fl = (uint8_t)(allq & (int)((masks[i] >> n) & 3u)); C.ds[i].fc = 127; C.ds[i].c = 0x7fffffff; }
    }
#endif
    bt_int lo = y[0], hi = y[0];
    for (int i = 1; i < n; i++) { if (y[i] < lo) lo = y[i]; if (y[i] > hi) hi = y[i]; }
    u64 span = (u64)hi - (u64)lo + 2; if (span < 32) span = 32;
    int32_t limit = (int32_t)(n - 2) * lg(span);
    C.limit = limit;
    search(&C, &best);
#ifndef BT_DEEP
    if (!best.ok) return 0;
#endif
    if (best.ok && best.by == BT_RULE && best.c <= 8 * ONE) limit = best.c;      /* a very short formula needs no check */
#ifdef BT_DEEP
    if (!best.ok || best.c > limit || !(best.by == BT_RULE && best.c <= 8 * ONE))
        if (deep_phase(&C, &best, &limit, C.limit, out)) return 0;
#endif
    if (!best.ok || best.c > limit) return 0;
    replay(&C, &best);
    out->by = best.by; out->bits = best.c; out->value[0] = C.pred[0]; out->value[1] = C.pred[1];
    return 0;
}

/* ================================================================ estimates (trends) and noise */

typedef bt_real real;
#define RC(x) ((real)(x))

#ifdef BT_FLOAT
static real dsqrt(real x)
{
    if (!(x > 0)) return 0;
    union { float d; uint32_t u; } v; v.d = x;
    v.u = (v.u >> 1) + 0x1FC00000ul;                               /* a first guess from the exponent */
    real r = v.d;
    for (int i = 0; i < 4; i++) r = RC(0.5) * (r + x / r);
    return r;
}

static real dexp(real t)
{
    if (t < -80) return 0;
    if (t > 80) t = 80;
    const real LN2HI = RC(0.693359375), LN2LO = RC(-2.12194440e-4), INVLN2 = RC(1.44269504);
    real kf = t * INVLN2;
    int k = (int)(kf < 0 ? kf - RC(0.5) : kf + RC(0.5));
    real r = (t - k * LN2HI) - k * LN2LO;                         /* |r| <= 0.35 */
    real s = 1;
    for (int i = 10; i >= 1; i--) s = 1 + s * r / i;              /* Taylor series, Horner form */
    union { float d; uint32_t u; } p; p.u = (uint32_t)(k + 127) << 23;
    return s * p.d;
}
#else
static real dsqrt(real x)
{
    if (!(x > 0)) return 0;
    union { double d; u64 u; } v; v.d = x;
    v.u = (v.u >> 1) + 0x1FF8000000000000ULL;                      /* a first guess from the exponent */
    real r = v.d;
    for (int i = 0; i < 6; i++) r = 0.5 * (r + x / r);
    return r;
}

static real dexp(real t)
{
    if (t < -700) return 0;
    if (t > 700) t = 700;
    const double LN2HI = 6.93147180369123816490e-01, LN2LO = 1.90821492927058770002e-10, INVLN2 = 1.44269504088896338700e+00;
    double kf = t * INVLN2;
    int k = (int)(kf < 0 ? kf - 0.5 : kf + 0.5);
    double r = (t - k * LN2HI) - k * LN2LO;                       /* |r| <= 0.35 */
    double s = 1.0;
    for (int i = 16; i >= 1; i--) s = 1.0 + s * r / i;           /* Taylor series, Horner form */
    union { double d; u64 u; } p; p.u = (u64)(k + 1023) << 52;
    return s * p.d;
}
#endif

/* The examples as the fits see them: the caller's integers, with one example left out (skip >= 0) or none (-1).
   Nothing is copied: example k of the m = n - 1 that remain is read where it lies. */
typedef struct { const bt_int *x, *y; int m, skip; } pts;
static real PX(const pts *P, int k) { return (real)P->x[k < P->skip || P->skip < 0 ? k : k + 1]; }
static real PY(const pts *P, int k) { return (real)P->y[k < P->skip || P->skip < 0 ? k : k + 1]; }

static real meanx(const pts *P) { real s = 0; for (int i = 0; i < P->m; i++) s += PX(P, i); return s / P->m; }
static real meany(const pts *P) { real s = 0; for (int i = 0; i < P->m; i++) s += PY(P, i); return s / P->m; }

/* solve A z = b (size m, row-major, overwritten), partial pivoting; returns 0 if singular */
static int gauss(real *A, real *b, int m)
{
    for (int c = 0; c < m; c++) {
        int p = c; real best = A[c * m + c] < 0 ? -A[c * m + c] : A[c * m + c];
        for (int r = c + 1; r < m; r++) { real v = A[r * m + c] < 0 ? -A[r * m + c] : A[r * m + c]; if (v > best) { best = v; p = r; } }
        if (best == 0) return 0;
        if (p != c) { for (int j = 0; j < m; j++) { real t = A[c * m + j]; A[c * m + j] = A[p * m + j]; A[p * m + j] = t; } real t = b[c]; b[c] = b[p]; b[p] = t; }
        for (int r = c + 1; r < m; r++) {
            real f = A[r * m + c] / A[c * m + c];
            if (f == 0) continue;
            for (int j = c; j < m; j++) A[r * m + j] -= f * A[c * m + j];
            b[r] -= f * b[c];
        }
    }
    for (int c = m - 1; c >= 0; c--) {
        real s = b[c];
        for (int j = c + 1; j < m; j++) s -= A[c * m + j] * b[j];
        b[c] = s / A[c * m + c];
    }
    return 1;
}

#define NMODEL 24

/* a polynomial of degree d in scaled x, fitted to the examples and read at the nq points q */
static NOINLINE void model_poly(int d, const pts *P, const real *q, int nq, real *out)
{
    int n = P->m, m = d + 1;
    real mu = meanx(P), s = 0, A[25], b[5];
    for (int i = 0; i < n; i++) s += (PX(P, i) - mu) * (PX(P, i) - mu);
    s = dsqrt(s / n); if (s == 0) s = 1;
    for (int r = 0; r < m; r++) {
        b[r] = 0;
        for (int i = 0; i < n; i++) { real z = (PX(P, i) - mu) / s, p = 1; for (int e = 0; e < r; e++) p *= z; b[r] += p * PY(P, i); }
        for (int c = 0; c < m; c++) {
            real t = 0;
            for (int i = 0; i < n; i++) { real z = (PX(P, i) - mu) / s, p = 1; for (int e = 0; e < r + c; e++) p *= z; t += p; }
            A[r * m + c] = t;
        }
    }
    if (!gauss(A, b, m)) { for (int j = 0; j < nq; j++) out[j] = meany(P); return; }
    for (int j = 0; j < nq; j++) { real zq = (q[j] - mu) / s, v = 0; for (int r = m - 1; r >= 0; r--) v = v * zq + b[r]; out[j] = v; }
}

/* a Gaussian process around a straight line. Its matrix is symmetric, so only the lower half is kept (packed),
   and it is solved in place as L D L': 136 numbers for 16 examples, where a full matrix needs 256. */
#define TRI(i, j) ((i) * ((i) + 1) / 2 + (j))
static NOINLINE void model_gp(int g, const pts *P, const real *q, int nq, real *out)
{
    static const real GLS[5] ROM = {15, 25, 40, 60, 100}, GNZ[4] ROM = {RC(0.05), RC(0.2), RC(0.5), RC(1.0)};
    int n = P->m, ok = 1;
#if defined(__AVR__) && !defined(BT_FLOAT)
#error "on an AVR, build with BT_FLOAT"
#endif
    real ls = ROMF(GLS, g / 4), nz = ROMF(GNZ, g % 4);
    real mx = meanx(P), my = meany(P), sxx = 0, sxy = 0;
    for (int i = 0; i < n; i++) { sxx += (PX(P, i) - mx) * (PX(P, i) - mx); sxy += (PX(P, i) - mx) * (PY(P, i) - my); }
    real sl = sxx > 0 ? sxy / sxx : 0, ic = my - sl * mx;
    real r[BT_MAX_EXAMPLES], K[BT_MAX_EXAMPLES * (BT_MAX_EXAMPLES + 1) / 2];
    real mr = 0, sc = 0;
    for (int i = 0; i < n; i++) { r[i] = PY(P, i) - (sl * PX(P, i) + ic); mr += r[i]; }
    mr /= n;
    for (int i = 0; i < n; i++) sc += (r[i] - mr) * (r[i] - mr);
    sc = dsqrt(sc / n); if (sc == 0) sc = 1;
    for (int i = 0; i < n; i++) {
        r[i] /= sc;
        for (int j = 0; j <= i; j++) { real d = (PX(P, i) - PX(P, j)) / ls; K[TRI(i, j)] = dexp(RC(-0.5) * d * d) + (i == j ? nz : 0); }
    }
    for (int j = 0; j < n && ok; j++) {                            /* K = L D L', in place: D on the diagonal */
        real dj = K[TRI(j, j)];
        for (int k = 0; k < j; k++) dj -= K[TRI(j, k)] * K[TRI(j, k)] * K[TRI(k, k)];
        if (!(dj > 0)) { ok = 0; break; }
        K[TRI(j, j)] = dj;
        for (int i = j + 1; i < n; i++) {
            real v = K[TRI(i, j)];
            for (int k = 0; k < j; k++) v -= K[TRI(i, k)] * K[TRI(j, k)] * K[TRI(k, k)];
            K[TRI(i, j)] = v / dj;
        }
    }
    if (ok) {
        for (int i = 0; i < n; i++) for (int k = 0; k < i; k++) r[i] -= K[TRI(i, k)] * r[k];
        for (int i = 0; i < n; i++) r[i] /= K[TRI(i, i)];
        for (int i = n - 1; i >= 0; i--) for (int k = i + 1; k < n; k++) r[i] -= K[TRI(k, i)] * r[k];
    }
    for (int j = 0; j < nq; j++) {
        real v = 0;
        if (ok) for (int i = 0; i < n; i++) { real d = (q[j] - PX(P, i)) / ls; v += dexp(RC(-0.5) * d * d) * r[i]; }
        out[j] = sl * q[j] + ic + sc * v;
    }
}

/* model mi fitted to the examples, predicted at the nq points q */
static void model(int mi, const pts *P, const real *q, int nq, real *out)
{
    if (mi < 4) model_poly(mi + 1, P, q, nq, out); else model_gp(mi - 4, P, q, nq, out);
}

/* leave-one-out error of model mi over all examples; *inner gets the same over the interior examples only (all but
   one example at the smallest x and one at the largest), where predicting a hidden example is interpolation */
static real loo(int mi, const bt_int *x, const bt_int *y, int n, real *inner)
{
    real s = 0, si = 0, p;
    int imin = 0, imax = 0, ni = 0;
    for (int i = 1; i < n; i++) { if (x[i] < x[imin]) imin = i; if (x[i] >= x[imax]) imax = i; }
    for (int i = 0; i < n; i++) {
        pts P; P.x = x; P.y = y; P.m = n - 1; P.skip = i;
        real qi = (real)x[i], yi = (real)y[i];
        model(mi, &P, &qi, 1, &p);
        s += (p - yi) * (p - yi);
        if (i != imin && i != imax) { si += (p - yi) * (p - yi); ni++; }
    }
    *inner = ni ? dsqrt(si / ni) : dsqrt(s / n);
    return dsqrt(s / n);
}

/* the most interior error, over the spread of the readings, that a real trend shows: the middle of the gap between
   trends and noise measured on edai's six sets of problems (8 examples: 0.445 vs 0.452; 12: 0.307 vs 0.537) */
static real inner_cut(int n)
{
    if (n <= 8) return RC(0.4485);
    if (n >= 12) return RC(0.406);
    return RC(0.4485) + (n - 8) * (RC(0.406) - RC(0.4485)) / 4;
}

static NOINLINE void trend(const bt_int *xi, const bt_int *yi, int n, const bt_int *qi, int nq, bt_answer *out)
{
    real q[2], sc[10], best_inner = RC(1e30), first = 0; uint8_t id[10]; int nk = 0;
    for (int j = 0; j < nq; j++) q[j] = (real)qi[j];
    for (int m = 0; m < NMODEL; m++) {                             /* the ten models that predict hidden examples best */
        real inner, s = loo(m, xi, yi, n, &inner);
        if (inner < best_inner) best_inner = inner;
        int j = nk;
        while (j > 0 && s < sc[j - 1]) j--;
        if (j >= 10) continue;
        for (int k = nk < 10 ? nk : 9; k > j; k--) { sc[k] = sc[k - 1]; id[k] = id[k - 1]; }
        sc[j] = s; id[j] = (uint8_t)m;
        if (nk < 10) nk++;
    }
    first = sc[0];
    pts P; P.x = xi; P.y = yi; P.m = n; P.skip = -1;
    real my = meany(&P), sy = 0;
    for (int i = 0; i < n; i++) sy += ((real)yi[i] - my) * ((real)yi[i] - my);
    sy = dsqrt(sy / n); if (sy == 0) sy = 1;
    /* a trend or noise? With 5 or more examples, judge by the interior error alone: the questions lie between the
       examples, and leaving out an end example asks a model to extrapolate, which inflates the error of real curves */
    real score = n >= 5 ? best_inner / sy : first / sy;
    out->bits = (int32_t)(score * 1000 + RC(0.5));
    if (!(score < (n >= 5 ? inner_cut(n) : RC(0.6)))) return;
    real num[2] = {0, 0}, den = 0, p[2];
    for (int t = 0; t < 10; t++) {
        real w = 1 / (sc[t] * sc[t] + RC(1e-9));
        model(id[t], &P, q, nq, p);
        for (int j = 0; j < nq; j++) num[j] += w * p[j];
        den += w;
    }
    out->by = BT_TREND;
    for (int j = 0; j < nq; j++) {
        real e = num[j] / den; out->est[j] = e;
        real c = e * 100;
#ifdef BT_VAL32
        out->value[j] = c >= RC(2e9) ? 2000000000L : c <= RC(-2e9) ? -2000000000L : (int32_t)(c < 0 ? -(-c + RC(0.5)) : c + RC(0.5));
#else
        out->value[j] = (int64_t)(c < 0 ? -(-c + RC(0.5)) : c + RC(0.5));
#endif
    }
}

/* rule first; if none, an estimate or CANNOT */
int bt_solve(const bt_int *x, const bt_int *y, int n, const bt_int *q, int nq, bt_answer *out)
{
    int rc = bt_solve_rule(x, y, n, q, nq, out);
    if (rc || out->by != BT_CANNOT) return rc;
    trend(x, y, n, q, nq, out);
    return 0;
}

#ifndef BT_NO_SEQ
/* ================================================================ sequences (exact rationals) */

typedef struct { i64 n, d; } rat;
static rat mk(i64 n, i64 d, int *ovf)
{
    rat r; (void)ovf; if (d < 0) { n = -n; d = -d; }
    i64 g = (i64)gcd(uabs(n), (u64)d); if (g > 1) { n /= g; d /= g; }
    if (n == 0) d = 1;
    r.n = n; r.d = d; return r;
}
static rat rI(i64 v) { rat r; r.n = v; r.d = 1; return r; }
static rat rmul(rat a, rat b, int *ovf)
{
    i64 g1 = (i64)gcd(uabs(a.n), (u64)b.d), g2 = (i64)gcd(uabs(b.n), (u64)a.d), n, d;
    if (g1 < 1) g1 = 1; if (g2 < 1) g2 = 1;
    if (__builtin_mul_overflow(a.n / g1, b.n / g2, &n) || __builtin_mul_overflow(a.d / g2, b.d / g1, &d)) { *ovf = 1; return rI(0); }
    return mk(n, d, ovf);
}
static rat radd(rat a, rat b, int *ovf)
{
    i64 g = (i64)gcd((u64)a.d, (u64)b.d), l, x, y, n;
    if (__builtin_mul_overflow(a.d / g, b.d, &l) || __builtin_mul_overflow(a.n, b.d / g, &x) ||
        __builtin_mul_overflow(b.n, a.d / g, &y) || __builtin_add_overflow(x, y, &n)) { *ovf = 1; return rI(0); }
    return mk(n, l, ovf);
}
static rat rneg(rat a) { a.n = -a.n; return a; }
static rat rdiv(rat a, rat b, int *ovf) { rat i = mk(b.d, b.n, ovf); return rmul(a, i, ovf); }

#define SQ_COL 7                         /* at most 6 unknowns and the right-hand side */

/* One equation of a recurrence of the given order with a polynomial of npoly terms, for term k:
   t[k-1] .. t[k-order], 1, k, k^2 | t[k]. The equations are never stored: each is made again when needed. */
static void seq_row(const bt_int *t, int order, int npoly, int k, rat *row)
{
    for (int i = 1; i <= order; i++) row[i - 1] = rI(t[k - i]);
    i64 pw = 1; for (int d = 0; d < npoly; d++) { row[order + d] = rI(pw); pw *= k; }
    row[order + npoly] = rI(t[k]);
}

/* row -= f * pivot, for every column, where f is row's entry in the pivot's column (nothing to do when it is 0) */
static void seq_reduce(rat *row, const rat *pivot, int col, int ncol, int *ovf)
{
    if (row[col].n == 0) return;
    rat f = row[col];
    for (int j = 0; j <= ncol; j++) row[j] = radd(row[j], rneg(rmul(f, pivot[j], ovf)), ovf);
}

/* Gauss-Jordan over the rationals, giving exactly what elimination on the whole stored matrix gives (the same
   operations on the same numbers), but keeping only the pivot rows: at most 6 rows of 7 numbers. Any other row is
   made again from the sequence and reduced by the pivots found so far whenever it is looked at.
   0 if inconsistent, under-determined, or a number did not fit. */
static NOINLINE int solve_exact(const bt_int *t, int order, int npoly, int rows, rat *sol)
{
    int ncol = order + npoly, r = 0, ovf = 0;
    rat S[SQ_COL - 1][SQ_COL], row[SQ_COL];
    uint8_t perm[BT_MAX_EXAMPLES], piv[SQ_COL - 1];
    for (int i = 0; i < rows; i++) perm[i] = (uint8_t)i;
    for (int c = 0; c < ncol; c++) {
        int p = -1;
        for (int i = r; i < rows && p < 0; i++) {                 /* the first row, in the current order, not 0 in column c */
            seq_row(t, order, npoly, order + perm[i], row);
            for (int j = 0; j < r; j++) seq_reduce(row, S[j], piv[j], ncol, &ovf);
            if (row[c].n != 0) p = i;
        }
        if (p < 0) continue;
        uint8_t tp = perm[r]; perm[r] = perm[p]; perm[p] = tp;
        rat inv = row[c];
        for (int j = 0; j <= ncol; j++) S[r][j] = rdiv(row[j], inv, &ovf);
        piv[r++] = (uint8_t)c;
        if (ovf) return 0;
    }
    for (int i = r; i < rows; i++) {                              /* the rows left over must have come out as 0 = 0 */
        seq_row(t, order, npoly, order + perm[i], row);
        for (int j = 0; j < r; j++) seq_reduce(row, S[j], piv[j], ncol, &ovf);
        if (row[ncol].n != 0) return 0;
    }
    if (r < ncol) return 0;
    for (int i = 0; i < r; i++) {                                 /* each pivot row, cleared of the later pivots' columns */
        for (int j = 0; j <= ncol; j++) row[j] = S[i][j];
        for (int k = i + 1; k < r; k++) seq_reduce(row, S[k], piv[k], ncol, &ovf);
        sol[piv[i]] = row[ncol];
    }
    return !ovf;
}

int bt_solve_sequence(const bt_int *t, int n, bt_answer *out)
{
    if (!t || !out || n < 3 || n > BT_MAX_EXAMPLES) return -1;
    out->by = BT_CANNOT; out->bits = 0; out->value[0] = out->value[1] = 0; out->est[0] = out->est[1] = 0;
    int bestc = 1 << 30; i64 bv[2] = {0, 0};
    for (int order = 0; order < 4; order++) for (int npoly = 0; npoly < 4; npoly++) {
        int npar = order + npoly, eqs = n - order, ovf = 0;
        if (npar == 0 || eqs - npar < 2) continue;
        rat sol[SQ_COL - 1], nx[2];
        if (!solve_exact(t, order, npoly, eqs, sol)) continue;
        for (int k = n; k < n + 2; k++) {                          /* the next two terms */
            rat v = rI(0);
            for (int i = 1; i <= order; i++) v = radd(v, rmul(sol[i - 1], k - i < n ? rI(t[k - i]) : nx[k - i - n], &ovf), &ovf);
            i64 pw = 1; for (int d = 0; d < npoly; d++) { v = radd(v, rmul(sol[order + d], rI(pw), &ovf), &ovf); pw *= k; }
            nx[k - n] = v;
        }
        if (ovf || nx[0].d != 1 || nx[1].d != 1) continue;
        int cost = 2 * npar; for (int i = 0; i < npar; i++) if (sol[i].d != 1) cost += 2;
        if (cost < bestc) { bestc = cost; bv[0] = nx[0].n; bv[1] = nx[1].n; }
    }
    for (int per = 2; per <= 3; per++) {                           /* alternating steps a*t + b */
        rat A[3], Bb[3]; int ok = 1, ovf = 0;
        static const int8_t an[14] = {-5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 1, -1, 1}, ad[14] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 3};
        for (int ph = 0; ph < per && ok; ph++) {
            int np = 0, k0 = 0;
            for (int k = 1; k < n; k++) if (k % per == ph) { if (!np) k0 = k; np++; }
            if (np < 2) { ok = 0; break; }
            int found = 0;
            for (int ai = 0; ai < 14 && !found; ai++) {
                rat a = mk(an[ai], ad[ai], &ovf);
                rat b = radd(rI(t[k0]), rneg(rmul(a, rI(t[k0 - 1]), &ovf)), &ovf);
                int all = 1;
                for (int k = k0; k < n && all; k += per) { rat v = radd(rmul(a, rI(t[k - 1]), &ovf), b, &ovf); all = v.n == t[k] && v.d == 1; }
                if (all && !ovf) { A[ph] = a; Bb[ph] = b; found = 1; }
            }
            if (!found) ok = 0;
        }
        if (!ok) continue;
        rat cur = rI(t[n - 1]), nx[2];
        for (int k = n; k < n + 2; k++) { cur = radd(rmul(A[k % per], cur, &ovf), Bb[k % per], &ovf); nx[k - n] = cur; }
        if (ovf || nx[0].d != 1 || nx[1].d != 1) continue;
        if (3 * per < bestc) { bestc = 3 * per; bv[0] = nx[0].n; bv[1] = nx[1].n; }
    }
    if (bestc < (1 << 30)) { out->by = BT_SEQ; out->bits = bestc; out->value[0] = bv[0]; out->value[1] = bv[1]; }
    return 0;
}
#endif
