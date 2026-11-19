/*
 * workers.h - FR6: worker failure detection with fork() and waitpid().
 *
 * Owned by the "threads, IPC and process modules" thread (src/workers.c).
 */
#ifndef WORKERS_H
#define WORKERS_H

/*
 * Fork several worker children and collect each status with waitpid().
 *
 *   simulate_failure == 0: every worker exits 0 ("normal completion").
 *   simulate_failure != 0: at least one worker exits non-zero and one is
 *                          killed by a signal; both are printed as
 *                          "SIMULATED FAILURE" / "SIMULATED CRASH" so they
 *                          are never mistaken for real OS faults.
 *
 * The parent decodes each status with WIFEXITED/WEXITSTATUS and
 * WIFSIGNALED/WTERMSIG and prints a report. No zombies may remain.
 *
 * Returns 0 if the parent's report matches what each worker was told to do
 * (e.g. the simulated failure WAS detected), -1 otherwise or on error.
 */
int workers_run(int simulate_failure);

#endif
