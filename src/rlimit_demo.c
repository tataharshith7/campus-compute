/*
 * rlimit_demo.c - FR7: process resource limits with setrlimit().
 *
 * Each demo runs in a fork()ed child (Unit 1 Lab 1 style: pid > 0 parent,
 * pid == 0 child) so the limit only affects that child, never the main
 * program. The parent collects the result with waitpid() and explains it.
 *
 * Demo 1, RLIMIT_CPU: soft limit 1 s, hard limit 2 s. The child spins on the
 * CPU; when it has used 1 s of CPU time the kernel sends SIGXCPU, whose
 * default action terminates the process.
 *
 * Demo 2, RLIMIT_NOFILE: the child lowers its open-file limit to 8 and opens
 * /dev/null until open() fails with EMFILE. Nothing is created on disk.
 */
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "logger.h"
#include "rlimit_demo.h"

#define CPU_SOFT_SECONDS   1
#define CPU_HARD_SECONDS   2
#define NOFILE_LIMIT       8
#define NOFILE_EXIT_BASE   100   /* child exits with 100 + number of files it could open */

/* Print an rlimit value, which may be "unlimited". */
static void print_limit(const char *label, const struct rlimit *rl)
{
    char soft[32], hard[32];
    if (rl->rlim_cur == RLIM_INFINITY)
        snprintf(soft, sizeof soft, "unlimited");
    else
        snprintf(soft, sizeof soft, "%llu", (unsigned long long)rl->rlim_cur);
    if (rl->rlim_max == RLIM_INFINITY)
        snprintf(hard, sizeof hard, "unlimited");
    else
        snprintf(hard, sizeof hard, "%llu", (unsigned long long)rl->rlim_max);
    printf("    [child pid %d] %s: soft = %s, hard = %s\n", (int)getpid(), label, soft, hard);
}

static double timeval_seconds(struct timeval tv)
{
    return (double)tv.tv_sec + (double)tv.tv_usec / 1e6;
}

/* Child for demo 1: lower RLIMIT_CPU, then burn CPU until the kernel stops us. */
static void cpu_limit_child(void)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_CPU, &rl) != 0) {
        perror("    getrlimit(RLIMIT_CPU)");
        _exit(1);
    }
    print_limit("RLIMIT_CPU before", &rl);

    rl.rlim_cur = CPU_SOFT_SECONDS;
    if (rl.rlim_max == RLIM_INFINITY || rl.rlim_max > CPU_HARD_SECONDS)
        rl.rlim_max = CPU_HARD_SECONDS;    /* lowering the hard limit needs no privilege */
    if (setrlimit(RLIMIT_CPU, &rl) != 0) {
        perror("    setrlimit(RLIMIT_CPU)");
        _exit(1);
    }
    getrlimit(RLIMIT_CPU, &rl);
    print_limit("RLIMIT_CPU after ", &rl);
    printf("    [child pid %d] now running a CPU-bound loop with no end condition...\n", (int)getpid());
    fflush(stdout);

    volatile unsigned long spins = 0;      /* volatile: the loop must not be optimised away */
    for (;;)
        spins++;
}

/* Child for demo 2: lower RLIMIT_NOFILE and count how many files we can still open. */
static void nofile_limit_child(void)
{
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) != 0) {
        perror("    getrlimit(RLIMIT_NOFILE)");
        _exit(1);
    }
    print_limit("RLIMIT_NOFILE before", &rl);

    rl.rlim_cur = NOFILE_LIMIT;
    if (setrlimit(RLIMIT_NOFILE, &rl) != 0) {
        perror("    setrlimit(RLIMIT_NOFILE)");
        _exit(1);
    }
    getrlimit(RLIMIT_NOFILE, &rl);
    print_limit("RLIMIT_NOFILE after ", &rl);

    int fds[NOFILE_LIMIT + 1];
    int opened = 0;
    while (opened <= NOFILE_LIMIT) {
        int fd = open("/dev/null", O_RDONLY);
        if (fd < 0) {
            printf("    [child pid %d] open() #%d failed: %s (errno %s)\n", (int)getpid(),
                   opened + 1, strerror(errno), errno == EMFILE ? "EMFILE" : "other");
            break;
        }
        fds[opened++] = fd;
        printf("    [child pid %d] opened /dev/null as fd %d\n", (int)getpid(), fd);
    }
    fflush(stdout);
    for (int i = 0; i < opened; i++)
        close(fds[i]);
    _exit(NOFILE_EXIT_BASE + opened);
}

static int run_cpu_demo(void)
{
    printf("Demo 1: RLIMIT_CPU (soft %d s, hard %d s) in a child process\n",
           CPU_SOFT_SECONDS, CPU_HARD_SECONDS);

    struct rusage before, after;
    getrusage(RUSAGE_CHILDREN, &before);

    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) // child process
    {
        cpu_limit_child();
    }
    else if (pid < 0) //fork failed
    {
        perror("error: fork");
        return -1;
    }

    // parent process
    int status = 0;
    pid_t r;
    do {
        r = waitpid(pid, &status, 0);
    } while (r < 0 && errno == EINTR);
    if (r < 0) {
        perror("error: waitpid");
        return -1;
    }
    getrusage(RUSAGE_CHILDREN, &after);
    double cpu = (timeval_seconds(after.ru_utime) - timeval_seconds(before.ru_utime)) +
                 (timeval_seconds(after.ru_stime) - timeval_seconds(before.ru_stime));

    int ok = 0;
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("  [parent] waitpid: child pid %d was killed by signal %d (%s)\n",
               (int)pid, sig, strsignal(sig));
        printf("  [parent] CPU time used by the child: %.2f s\n", cpu);
        if (sig == SIGXCPU) {
            printf("  Observed effect: the kernel sent SIGXCPU once the child used its %d s soft CPU limit,\n"
                   "  so the endless loop was stopped instead of running forever.\n", CPU_SOFT_SECONDS);
            ok = 1;
        } else if (sig == SIGKILL) {
            printf("  Observed effect: the child reached the %d s hard CPU limit and was killed with SIGKILL.\n",
                   CPU_HARD_SECONDS);
            ok = 1;
        }
    } else if (WIFEXITED(status)) {
        printf("  [parent] child pid %d exited with status %d (limit could not be applied)\n",
               (int)pid, WEXITSTATUS(status));
    }
    logger_log("rlimit", "RLIMIT_CPU child pid %d: %s after %.2f s CPU", (int)pid,
               WIFSIGNALED(status) ? strsignal(WTERMSIG(status)) : "exited", cpu);
    return ok ? 0 : -1;
}

static int run_nofile_demo(void)
{
    printf("\nDemo 2: RLIMIT_NOFILE (max %d open file descriptors) in a child process\n", NOFILE_LIMIT);

    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) // child process
    {
        nofile_limit_child();
    }
    else if (pid < 0) //fork failed
    {
        perror("error: fork");
        return -1;
    }

    // parent process
    int status = 0;
    pid_t r;
    do {
        r = waitpid(pid, &status, 0);
    } while (r < 0 && errno == EINTR);
    if (r < 0) {
        perror("error: waitpid");
        return -1;
    }

    int ok = 0;
    if (WIFEXITED(status) && WEXITSTATUS(status) >= NOFILE_EXIT_BASE) {
        int opened = WEXITSTATUS(status) - NOFILE_EXIT_BASE;
        printf("  [parent] waitpid: child pid %d exited normally; it could open %d more files\n",
               (int)pid, opened);
        /* Descriptor numbers 0..LIMIT-1 are allowed; the ones not handed out were already in use. */
        printf("  Observed effect: %d descriptors were already open (stdin/stdout/stderr plus inherited\n"
               "  ones such as the log file), so only %d more fit under the limit of %d; the next\n"
               "  open() was refused with EMFILE.\n",
               NOFILE_LIMIT - opened, opened, NOFILE_LIMIT);
        ok = (opened < NOFILE_LIMIT);
        logger_log("rlimit", "RLIMIT_NOFILE=%d child pid %d opened %d files before EMFILE",
                   NOFILE_LIMIT, (int)pid, opened);
    } else {
        printf("  [parent] child pid %d did not finish the RLIMIT_NOFILE demo as expected\n", (int)pid);
    }
    return ok ? 0 : -1;
}

int rlimit_demo(void)
{
    printf("\n=== Resource limits with setrlimit() ===\n");
    printf("Limits are set inside child processes only; the main program keeps its own limits.\n\n");

    int cpu_rc = run_cpu_demo();
    int nofile_rc = run_nofile_demo();

    errno = 0;
    if (waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD)
        printf("\n  Cleanup check: both limited children were reaped (no zombies)\n");

    int rc = (cpu_rc == 0 && nofile_rc == 0) ? 0 : -1;
    printf("RESULT: %s\n", rc == 0 ? "PASS - limits applied and their effect observed"
                                   : "FAIL - a limit did not behave as expected");
    return rc;
}
