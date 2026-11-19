/*
 * workers.c - FR6: worker failure detection with fork() and waitpid().
 *
 * Based on the course lab programs:
 *   - Unit 1 Lab 1 example2.c: an array {5, 10, ..., 40} is split between
 *     processes, each sums its part, the parent combines the sums.
 *   - Unit 2 Lab 3 demo_pipe.c: a child sends its result to the parent through
 *     a pipe (READ_END / WRITE_END, close the unused end).
 *   - Unit 1 notes: waitpid() + WEXITSTATUS to collect a child's status.
 * Each of NUM_WORKERS children sums one chunk and writes the partial sum into
 * its own pipe. The exit status is then free to mean only "success / failure",
 * which is what the parent decodes with WIFEXITED/WEXITSTATUS and
 * WIFSIGNALED/WTERMSIG.
 *
 * In "fail" mode two workers are told to misbehave ON PURPOSE:
 *   worker 2: prints "SIMULATED FAILURE" and exits with code 42
 *   worker 3: prints "SIMULATED CRASH" and calls abort() (SIGABRT)
 * These are labelled as simulated everywhere; they are not real OS faults.
 */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "logger.h"
#include "workers.h"

#define NUM_WORKERS        4
#define READ_END           0
#define WRITE_END          1
#define SIMULATED_EXIT     42

/* What each worker is told to do. */
typedef enum {
    TASK_NORMAL,            /* sum the chunk, exit 0                   */
    TASK_SIMULATED_FAIL,    /* exit(SIMULATED_EXIT) without a result   */
    TASK_SIMULATED_CRASH    /* abort() -> killed by SIGABRT            */
} worker_task;

/* How the parent saw the worker end. */
typedef enum {
    END_OK,
    END_EXIT_CODE,
    END_SIGNAL,
    END_UNKNOWN
} worker_end;

typedef struct {
    pid_t pid;
    int fd;                 /* read end of this worker's pipe */
    worker_task task;
    worker_end end;
    int code;               /* exit code or signal number */
    int partial_sum;
    int got_result;
} worker_info;

/* Same data as example2.c. */
static const int arr[] = { 5, 10, 15, 20, 25, 30, 35, 40 };
#define ARR_LEN ((int)(sizeof arr / sizeof arr[0]))

static const char *task_name(worker_task t)
{
    switch (t) {
    case TASK_NORMAL:          return "normal";
    case TASK_SIMULATED_FAIL:  return "SIMULATED FAILURE (exit 42)";
    case TASK_SIMULATED_CRASH: return "SIMULATED CRASH (abort)";
    }
    return "?";
}

/* Code run by worker number `idx` (0-based) in the child process. Never returns. */
static void worker_main(int idx, worker_task task, int write_fd)
{
    int per = ARR_LEN / NUM_WORKERS;
    int first = idx * per;
    int last = first + per;             /* exclusive */
    int childSum = 0;

    printf("    [worker %d] This is child. Process Id = %d, Parent Process Id = %d, task = %s\n",
           idx + 1, (int)getpid(), (int)getppid(), task_name(task));
    fflush(stdout);

    if (task == TASK_SIMULATED_FAIL) {
        printf("    [worker %d] SIMULATED FAILURE: exiting with code %d on purpose (not a real fault)\n",
               idx + 1, SIMULATED_EXIT);
        fflush(stdout);
        close(write_fd);
        _exit(SIMULATED_EXIT);
    }
    if (task == TASK_SIMULATED_CRASH) {
        printf("    [worker %d] SIMULATED CRASH: calling abort() on purpose (not a real fault)\n", idx + 1);
        fflush(stdout);
        /* Core size limit 0 so the deliberate crash leaves no core file behind. */
        struct rlimit no_core = { 0, 0 };
        setrlimit(RLIMIT_CORE, &no_core);
        close(write_fd);
        abort();
    }

    for (int i = first; i < last; i++) {
        childSum += arr[i];
        printf("    [worker %d] adding %d -> Partial Sum = %d\n", idx + 1, arr[i], childSum);
        fflush(stdout);
        usleep(50000);
    }

    /* Send the result through the pipe, as in demo_pipe.c. */
    ssize_t n = write(write_fd, &childSum, sizeof childSum);
    close(write_fd);
    _exit(n == (ssize_t)sizeof childSum ? 0 : 1);
}

/* Decode a waitpid() status into the worker record and print one line about it. */
static void decode_status(worker_info *w, int idx, int status)
{
    if (WIFEXITED(status)) {
        w->code = WEXITSTATUS(status);
        w->end = (w->code == 0) ? END_OK : END_EXIT_CODE;
        printf("  [parent] waitpid: worker %d (pid %d) exited normally, exit status %d\n",
               idx + 1, (int)w->pid, w->code);
    } else if (WIFSIGNALED(status)) {
        w->code = WTERMSIG(status);
        w->end = END_SIGNAL;
        printf("  [parent] waitpid: worker %d (pid %d) was killed by signal %d (%s)%s\n",
               idx + 1, (int)w->pid, w->code, strsignal(w->code),
               WCOREDUMP(status) ? ", core dumped" : "");
    } else {
        w->end = END_UNKNOWN;
        printf("  [parent] waitpid: worker %d (pid %d) ended with unexpected status 0x%x\n",
               idx + 1, (int)w->pid, (unsigned)status);
    }
}

/* Did the parent observe exactly what this worker was told to do? */
static int outcome_as_expected(const worker_info *w)
{
    switch (w->task) {
    case TASK_NORMAL:          return w->end == END_OK && w->got_result;
    case TASK_SIMULATED_FAIL:  return w->end == END_EXIT_CODE && w->code == SIMULATED_EXIT;
    case TASK_SIMULATED_CRASH: return w->end == END_SIGNAL && w->code == SIGABRT;
    }
    return 0;
}

int workers_run(int simulate_failure)
{
    worker_info w[NUM_WORKERS];
    int created = 0;
    int rc = 0;

    printf("\n=== Worker processes: fork() + waitpid() (%s mode) ===\n",
           simulate_failure ? "simulated failure" : "normal");
    printf("Task: sum the array {5, 10, 15, 20, 25, 30, 35, 40} split across %d worker processes.\n",
           NUM_WORKERS);
    printf("Each worker sends its partial sum to the parent through a pipe; the exit status\n"
           "reports success or failure. This is parent. Process Id = %d\n", (int)getpid());
    if (simulate_failure)
        printf("NOTE: workers 2 and 3 are told to fail ON PURPOSE. These are SIMULATED\n"
               "      failures for the demonstration, not real operating-system faults.\n");
    printf("\n");

    for (int i = 0; i < NUM_WORKERS; i++) {
        w[i] = (worker_info){ .pid = -1, .fd = -1, .task = TASK_NORMAL, .end = END_UNKNOWN };
        if (simulate_failure && i == 1)
            w[i].task = TASK_SIMULATED_FAIL;
        if (simulate_failure && i == 2)
            w[i].task = TASK_SIMULATED_CRASH;

        int fd[2];
        if (pipe(fd) == -1) {
            perror("error: pipe");
            rc = -1;
            break;
        }

        fflush(stdout);     /* otherwise the child would repeat buffered output */
        pid_t p1 = fork();

        if (p1 > 0) //parent process
        {
            close(fd[WRITE_END]);       /* parent only reads */
            w[i].pid = p1;
            w[i].fd = fd[READ_END];
            created++;
            printf("  [parent] created worker %d: pid %d (%s)\n", i + 1, (int)p1, task_name(w[i].task));
            logger_log("workers", "forked worker %d pid %d task=%s", i + 1, (int)p1, task_name(w[i].task));
        }
        else if (p1 == 0) // child process
        {
            close(fd[READ_END]);        /* child only writes */
            for (int k = 0; k < i; k++) /* do not keep siblings' pipes open */
                close(w[k].fd);
            worker_main(i, w[i].task, fd[WRITE_END]);
        }
        else //fork failed
        {
            perror("error: fork");
            close(fd[READ_END]);
            close(fd[WRITE_END]);
            rc = -1;
            break;
        }
    }

    printf("\n  [parent] waiting for %d workers with waitpid()...\n", created);
    fflush(stdout);

    for (int i = 0; i < created; i++) {
        int status = 0;
        pid_t r;
        do {
            r = waitpid(w[i].pid, &status, 0);
        } while (r < 0 && errno == EINTR);
        if (r < 0) {
            fprintf(stderr, "error: waitpid(%d): %s\n", (int)w[i].pid, strerror(errno));
            rc = -1;
            close(w[i].fd);
            continue;
        }
        decode_status(&w[i], i, status);

        /* Read the result; a failed worker closed the pipe without writing (read returns 0). */
        ssize_t n = read(w[i].fd, &w[i].partial_sum, sizeof w[i].partial_sum);
        w[i].got_result = (n == (ssize_t)sizeof w[i].partial_sum);
        close(w[i].fd);
    }

    /* Report. */
    int total = 0, ok_count = 0, failed_count = 0;
    printf("\n  %-8s %-8s %-30s %-34s %s\n", "Worker", "PID", "Told to", "Parent detected", "Result");
    for (int i = 0; i < created; i++) {
        char detected[64];
        switch (w[i].end) {
        case END_OK:        snprintf(detected, sizeof detected, "completed (exit 0)"); break;
        case END_EXIT_CODE: snprintf(detected, sizeof detected, "FAILED (exit code %d)", w[i].code); break;
        case END_SIGNAL:    snprintf(detected, sizeof detected, "CRASHED (signal %d: %s)", w[i].code,
                                     strsignal(w[i].code)); break;
        default:            snprintf(detected, sizeof detected, "unknown"); break;
        }
        char result[32];
        if (w[i].got_result)
            snprintf(result, sizeof result, "partial sum %d", w[i].partial_sum);
        else
            snprintf(result, sizeof result, "no result");

        printf("  %-8d %-8d %-30s %-34s %s%s\n", i + 1, (int)w[i].pid, task_name(w[i].task),
               detected, result, w[i].task != TASK_NORMAL ? "  [SIMULATED]" : "");

        if (w[i].end == END_OK && w[i].got_result) {
            total += w[i].partial_sum;
            ok_count++;
        } else {
            failed_count++;
        }
        if (!outcome_as_expected(&w[i]))
            rc = -1;
        logger_log("workers", "worker %d pid %d: %s%s", i + 1, (int)w[i].pid, detected,
                   w[i].task != TASK_NORMAL ? " [SIMULATED]" : "");
    }

    int expected_total = 0;
    for (int i = 0; i < ARR_LEN; i++)
        expected_total += arr[i];

    printf("\n  Workers completed normally = %d, failed/crashed = %d\n", ok_count, failed_count);
    if (failed_count == 0)
        printf("  Total Array Sum = %d (expected %d)\n", total, expected_total);
    else
        printf("  Partial total from successful workers = %d (full array sum would be %d;\n"
               "  the chunks of failed workers are missing and would need to be re-run)\n",
               total, expected_total);
    if (created == NUM_WORKERS && failed_count == 0 && total != expected_total)
        rc = -1;

    /* No zombies: every child has been reaped, so waitpid(-1, ..., WNOHANG) finds none. */
    errno = 0;
    if (waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD)
        printf("  Zombie check: no child processes left (all reaped by waitpid)\n");
    else
        printf("  Zombie check: unexpected child process still present\n");

    if (simulate_failure)
        printf("RESULT: %s\n", rc == 0 ? "PASS - the parent correctly detected both SIMULATED failures"
                                       : "FAIL - the parent's report does not match what the workers did");
    else
        printf("RESULT: %s\n", rc == 0 ? "PASS - all workers completed normally"
                                       : "FAIL - see report above");
    return rc;
}
