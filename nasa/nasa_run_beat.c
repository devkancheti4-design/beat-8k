/* beat8k on windows of NASA spacecraft telemetry, in the place of edai's space/nasa_run.c: the same input and the
   same output lines, so that edai's own scripts (space/scripts) score it.

   Standard input, one window per line (whole numbers):
       <id> R <n> x1 .. xn y1 .. yn <nq> q1 [q2]      a rule y = f(x): n examples, nq questions
       <id> S <n> t1 .. tn                             a sequence: n terms, asks the next two
   Standard output, one line per window:
       <id> by=<AM|SPLITTER|NET|CANNOT> ans=<a,b|CANNOT> [est=<e1,e2>] ns=<nanoseconds>
     AM        an exact rule (a sequence rule, or one formula)      SPLITTER  an exact formula in pieces
     NET       an estimate (edai's scripts call every estimate NET)  CANNOT    no answer

   A sequence is given to bt_solve_sequence. With the environment variable BEAT_FALLBACK=1, a sequence it cannot
   answer is then given to bt_solve as x = 0 .. n-1 with the questions x = n and n + 1: the x -> y solver asked about
   two points beyond its examples, which it was built and tuned to do only between them. */
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "beat.h"

static uint64_t now_ns(void)
{
    struct timespec t;
#ifdef CLOCK_MONOTONIC_RAW
    clock_gettime(CLOCK_MONOTONIC_RAW, &t);
#else
    clock_gettime(CLOCK_MONOTONIC, &t);
#endif
    return (uint64_t)t.tv_sec * 1000000000u + (uint64_t)t.tv_nsec;
}

int main(void)
{
    int fallback = getenv("BEAT_FALLBACK") != NULL;
    char id[64], kind[4]; int n;
    while (scanf("%63s %3s %d", id, kind, &n) == 3) {
        int64_t x[BT_MAX_EXAMPLES], y[BT_MAX_EXAMPLES], q[2] = { 0, 0 };
        int nq = 0, ok = n >= 1 && n <= BT_MAX_EXAMPLES, seq = kind[0] == 'S';
        for (int i = 0; ok && i < n; i++) ok = scanf("%" SCNd64, &x[i]) == 1;
        if (!seq) {
            for (int i = 0; ok && i < n; i++) ok = scanf("%" SCNd64, &y[i]) == 1;
            ok = ok && scanf("%d", &nq) == 1 && nq >= 1 && nq <= 2;
            for (int j = 0; ok && j < nq; j++) ok = scanf("%" SCNd64, &q[j]) == 1;
        }
        if (!ok) { printf("%s error=input\n", id); fflush(stdout); return 2; }
        bt_answer a; int rc;
        uint64_t t0 = now_ns();
        if (seq) {
            rc = bt_solve_sequence(x, n, &a);
            if (!rc && a.by == BT_CANNOT && fallback) {
                for (int i = 0; i < n; i++) { y[i] = x[i]; x[i] = i; }
                q[0] = n; q[1] = n + 1;
                rc = bt_solve(x, y, n, q, 2, &a);
            }
        } else rc = bt_solve(x, y, n, q, nq, &a);
        uint64_t t1 = now_ns();
        if (rc) { printf("%s error=%d\n", id, rc); fflush(stdout); continue; }
        if (a.by == BT_CANNOT) printf("%s by=CANNOT ans=CANNOT", id);
        else if (a.by == BT_TREND) {
            long long e0 = (long long)(a.est[0] < 0 ? a.est[0] - 0.5 : a.est[0] + 0.5), e1 = (long long)(a.est[1] < 0 ? a.est[1] - 0.5 : a.est[1] + 0.5);
            printf("%s by=NET ans=%lld,%lld est=%lld,%lld", id, e0, e1, e0, e1);
        } else printf("%s by=%s ans=%" PRId64 ",%" PRId64, id, a.by == BT_PIECES ? "SPLITTER" : "AM", a.value[0], a.value[1]);
        printf(" ns=%" PRIu64 "\n", t1 - t0);
        fflush(stdout);
    }
    return 0;
}
