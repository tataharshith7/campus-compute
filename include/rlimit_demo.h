/*
 * rlimit_demo.h - FR7: process resource limit demonstration.
 *
 * Owned by the "threads, IPC and process modules" thread (src/rlimit_demo.c).
 */
#ifndef RLIMIT_DEMO_H
#define RLIMIT_DEMO_H

/*
 * In a forked child: print the current limit, lower it with setrlimit()
 * (RLIMIT_CPU 1 s soft / 2 s hard), run a busy loop, and let the limit
 * take effect. The parent waitpid()s, reports the signal (SIGXCPU) and
 * explains the observed effect. Optionally also demonstrates a second limit
 * (e.g. RLIMIT_NOFILE).
 *
 * Returns 0 if the limit was applied and its effect observed, -1 otherwise.
 */
int rlimit_demo(void);

#endif
