/* beat.h -- beat8k: a small solver for the edai 300-problem challenge that needs about 2 KB of memory.

   It answers a problem from its own examples only:
     - a hidden integer rule x -> y: the shortest exact formula (or formula in pieces) that fits every example,
       accepted only when it is much shorter than the examples it explains;
     - a sequence: an exact linear recurrence (with a polynomial term) or an alternation of two or three steps;
     - otherwise an estimate from an ensemble of smooth curve fits, or CANNOT when no curve predicts the examples it
       did not see better than their average.

   No static data, no heap, no C library: all working memory is on the stack of the call. Rules are found with
   integer arithmetic only; the estimates use only + - * / and the file's own exp and sqrt, so every IEEE-754
   machine gives the same answers, provided the compiler does not fuse multiply-adds (-ffp-contract=off).

   Build options:
     -DBT_DEEP             also search chains of three operations (slower, more rules found)
     -DBT_VAL32            32-bit values: inputs are int32 (|x|, |q| < 2^26, |y| <= 2^30), for 8- and 32-bit chips
     -DBT_FLOAT            estimates in single precision (for chips without 64-bit floating point)
     -DBT_NO_SEQ           leave out sequences
     -DBT_MAX_EXAMPLES=12  fewer examples at most (default 16): smaller batches */
#ifndef BEAT_H
#define BEAT_H
#include <stdint.h>

#ifndef BT_MAX_EXAMPLES
#define BT_MAX_EXAMPLES 16
#endif
#define BT_MAX_QUESTIONS 2

#ifdef BT_VAL32
typedef int32_t bt_int;
#else
typedef int64_t bt_int;
#endif
#ifdef BT_FLOAT
typedef float bt_real;
#else
typedef double bt_real;
#endif

#define BT_CANNOT 0
#define BT_RULE   1      /* one exact formula */
#define BT_PIECES 2      /* an exact formula in pieces */
#define BT_SEQ    3      /* an exact sequence rule */
#define BT_TREND  4      /* an estimate (est[] holds it, value[] holds it rounded to cents) */

typedef struct {
    int32_t by;          /* BT_RULE, BT_PIECES, BT_SEQ, BT_TREND or BT_CANNOT */
    int32_t bits;        /* rule length in 1/65536 bits, or the trend score x 1000 */
    int64_t value[2];    /* exact answers; for a trend, the estimate x 100, rounded */
    bt_real est[2];      /* a trend's estimate */
} bt_answer;

/* each returns 0, or -1 for bad sizes or (with BT_VAL32) inputs out of range */
int bt_solve(const bt_int *x, const bt_int *y, int n, const bt_int *q, int nq, bt_answer *out);       /* rule, else estimate, else CANNOT */
int bt_solve_rule(const bt_int *x, const bt_int *y, int n, const bt_int *q, int nq, bt_answer *out);  /* rule or CANNOT */
int bt_solve_sequence(const bt_int *t, int n, bt_answer *out);                                         /* the next two terms */

#endif
