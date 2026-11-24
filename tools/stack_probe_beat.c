/* Measures how much stack edai's calls really use on this machine (macOS arm64, a shipped target).
   Each call runs on a thread whose stack we allocate and fill with a pattern; afterwards we find the deepest byte that
   changed. The library's own share is that depth minus the depth of an empty call through the same path.
   Input: the same line format as native_solve. Output per line: bytes used by the library call, then the problem kind. */
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "beat.h"


#define STACK_SIZE (256 * 1024)
#define PAT 0xA5
static unsigned char *stk;

typedef struct { int seq, n, nq; bt_int x[64], y[64], q[8]; int empty; } job_t;
static job_t job;
static volatile uintptr_t sp_at_call;

__attribute__((noinline)) static int32_t call_it(void) {
    int dummy; sp_at_call = (uintptr_t)&dummy;
    bt_answer a;
    if (job.empty) return 0;
#ifdef BT_NO_SEQ
    if (job.seq) return 0;
#else
    if (job.seq) return bt_solve_sequence(job.x, job.n, &a);
#endif
    return bt_solve(job.x, job.y, job.n, job.q, job.nq, &a);
}
static void *thread_main(void *arg) { (void)arg; call_it(); return NULL; }

static size_t run_once(void) {
    memset(stk, PAT, STACK_SIZE);
    pthread_attr_t at; pthread_attr_init(&at);
    pthread_attr_setstack(&at, stk, STACK_SIZE);
    pthread_t th; pthread_create(&th, &at, thread_main, NULL); pthread_join(th, NULL);
    size_t i = 0; while (i < STACK_SIZE && stk[i] == PAT) i++;      /* stack grows down: lowest dirtied byte */
    return STACK_SIZE - i;                                             /* bytes from the top that were touched */
}

int main(void) {
    stk = aligned_alloc(16384, STACK_SIZE);

    job.empty = 1; size_t base = run_once();                          /* thread start-up + call_it frame */
    fprintf(stderr, "baseline (thread start + wrapper) %zu bytes\n", base);
    char kind[4]; size_t worst = 0;
    while (scanf("%3s", kind) == 1) {
        memset(&job, 0, sizeof job);
        scanf("%d", &job.n);
        if (kind[0] == 'R') {
            int64_t t_;
            for (int i = 0; i < job.n; i++) { scanf("%" SCNd64, &t_); job.x[i] = (bt_int)t_; }
            for (int i = 0; i < job.n; i++) { scanf("%" SCNd64, &t_); job.y[i] = (bt_int)t_; }
            scanf("%d", &job.nq); for (int i = 0; i < job.nq; i++) { scanf("%" SCNd64, &t_); job.q[i] = (bt_int)t_; }
        } else { int64_t t_; job.seq = 1; for (int i = 0; i < job.n; i++) { scanf("%" SCNd64, &t_); job.x[i] = (bt_int)t_; } }
    
        size_t used = run_once(), lib = used > base ? used - base : 0;
        if (lib > worst) worst = lib;
        printf("%zu %c\n", lib, kind[0]);
    }
    fprintf(stderr, "worst stack use: %zu bytes\n", worst);
    return 0;
}
