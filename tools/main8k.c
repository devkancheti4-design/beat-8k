/* Runs the 300 problems of problems.h through beat8k and prints one line per problem, in the form score.py reads.
   ./beat [first [count]] */
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "beat.h"
#include "problems.h"

static void cents(char *b, int64_t v) { snprintf(b, 32, "%s%lld.%02lld", v < 0 ? "-" : "", (long long)(llabs(v) / 100), (long long)(llabs(v) % 100)); }

int main(int argc, char **argv)
{
    int first = argc > 1 ? atoi(argv[1]) : 0, count = argc > 2 ? atoi(argv[2]) : BT_NPROBLEMS;
    static const char *BY[] = {"CANNOT", "RULE", "PIECES", "SEQ", "TREND"};
    for (int i = first; i < first + count && i < BT_NPROBLEMS; i++) {
        const bt_problem *p = &BT_PROBLEMS[i];
        bt_int x[16], y[16], q[2]; bt_answer a;
        for (int j = 0; j < p->n; j++) { x[j] = p->x[j]; y[j] = p->y[j]; }
        q[0] = p->q[0]; q[1] = p->q[1];
        clock_t t0 = clock();
#ifdef BT_NO_SEQ
        int rc = 0; if (p->seq) { a.by = BT_CANNOT; a.bits = 0; } else rc = bt_solve(x, y, p->n, q, p->nq, &a);
#else
        int rc = p->seq ? bt_solve_sequence(x, p->n, &a) : bt_solve(x, y, p->n, q, p->nq, &a);
#endif
        double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
        char s0[32], s1[32];
        if (rc) printf("%s ERROR\n", p->id);
        else if (a.by == BT_CANNOT) printf("%s CANNOT  # %s bits=%d ms=%.0f\n", p->id, BY[a.by], a.bits, ms);
        else if (a.by == BT_TREND) { cents(s0, a.value[0]); cents(s1, a.value[1]); printf("%s %s,%s  # %s s=%d ms=%.0f\n", p->id, s0, s1, BY[a.by], a.bits, ms); }
        else printf("%s %lld,%lld  # %s bits=%d ms=%.0f\n", p->id, (long long)a.value[0], (long long)a.value[1], BY[a.by], a.bits, ms);
        fflush(stdout);
    }
    return 0;
}
