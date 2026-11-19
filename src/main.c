/*
 * main.c - FR8: command-line interface for CampusCompute.
 *
 * Usage: ./campuscompute <command> [arguments]   (run 'help' for the list)
 *
 * Each command runs one feature on its own, so every feature is
 * independently testable. 'demo' runs them all and prints a summary.
 */
#include <stdio.h>
#include <string.h>

#include "banker.h"
#include "common.h"
#include "ipc.h"
#include "jobs.h"
#include "logger.h"
#include "rlimit_demo.h"
#include "scheduler.h"
#include "workers.h"

static void print_usage(void)
{
    printf("CampusCompute - job scheduling and resource management prototype\n\n");
    printf("Usage: ./campuscompute <command> [arguments]\n\n");
    printf("Scheduling (FR1, FR2); [file] defaults to %s:\n", DEFAULT_JOBS_FILE);
    printf("  jobs     [file]              load, validate and show the job table\n");
    printf("  fcfs     [file]              First Come First Served\n");
    printf("  rr       [file] [quantum]    Round Robin (default quantum %d)\n", DEFAULT_RR_QUANTUM);
    printf("  priority [file]              non-preemptive Priority (higher number = higher priority)\n");
    printf("  compare  [file] [quantum]    run all three on the same jobs and compare\n\n");
    printf("Other features:\n");
    printf("  logger  [producers] [events] pthread producer-consumer logger (FR3), default 3 5\n");
    printf("  ipc                          shared memory + semaphores between processes (FR4)\n");
    printf("  banker  <file>               Banker's safety check (FR5), e.g. %s\n", DEFAULT_BANKER_SAFE);
    printf("  workers normal|fail          fork/waitpid workers, simulated failure (FR6)\n");
    printf("  rlimit                       setrlimit demonstration (FR7)\n");
    printf("  status                       show which CampusCompute IPC objects exist\n");
    printf("  cleanup                      remove only CampusCompute's own IPC objects\n");
    printf("  demo                         run everything and print a summary\n");
    printf("  help                         show this message\n");
}

/* Print an error and the usage text; the caller returns EXIT_USAGE. */
static int usage_error(const char *fmt, const char *arg)
{
    fprintf(stderr, "error: ");
    fprintf(stderr, fmt, arg);
    fprintf(stderr, "\n\n");
    print_usage();
    return EXIT_USAGE;
}

/* Strict positive-int argument, e.g. a quantum. Returns 0 on success. */
static int positive_arg(const char *what, const char *s, int *out)
{
    if (parse_int(s, out) != 0 || *out <= 0) {
        fprintf(stderr, "error: %s must be a positive integer (got '%s')\n\n", what, s);
        print_usage();
        return -1;
    }
    return 0;
}

/* ---------- scheduling commands ---------- */

static int cmd_schedule(const char *cmd, int argc, char **argv)
{
    const char *file = DEFAULT_JOBS_FILE;
    int quantum = DEFAULT_RR_QUANTUM;
    int nargs = argc - 1;               /* arguments after the command */
    char **args = argv + 1;
    JobSet set;
    static ScheduleResult res;

    int is_rr = strcmp(cmd, "rr") == 0 || strcmp(cmd, "compare") == 0;
    int max_args = is_rr ? 2 : 1;
    if (strcmp(cmd, "jobs") == 0)
        max_args = 1;
    if (nargs > max_args)
        return usage_error("too many arguments for '%s'", cmd);

    if (is_rr) {
        int n;
        if (nargs == 1 && parse_int(args[0], &n) == 0) {
            /* a lone number is the quantum */
            if (positive_arg("time quantum", args[0], &quantum) != 0)
                return EXIT_USAGE;
        } else {
            if (nargs >= 1)
                file = args[0];
            if (nargs == 2 && positive_arg("time quantum", args[1], &quantum) != 0)
                return EXIT_USAGE;
        }
    } else if (nargs == 1) {
        file = args[0];
    }

    if (jobs_load(file, &set) != 0)
        return EXIT_USAGE;

    if (strcmp(cmd, "jobs") == 0) {
        printf("\n=== Job table: %s ===\n", file);
        jobs_print(&set);
        return EXIT_OK;
    }
    if (strcmp(cmd, "compare") == 0)
        return schedule_compare(&set, quantum) == 0 ? EXIT_OK : EXIT_FAIL;

    Algorithm alg = strcmp(cmd, "fcfs") == 0 ? ALG_FCFS
                  : strcmp(cmd, "rr") == 0   ? ALG_RR : ALG_PRIORITY;
    if (schedule_run(alg, &set, quantum, &res) != 0)
        return EXIT_FAIL;
    schedule_print(&set, &res);
    return EXIT_OK;
}

/* ---------- other commands ---------- */

static int cmd_logger(int argc, char **argv)
{
    int producers = 3, events = 5;
    if (argc > 3)
        return usage_error("too many arguments for '%s'", argv[0]);
    if (argc >= 2 && positive_arg("number of producers", argv[1], &producers) != 0)
        return EXIT_USAGE;
    if (argc == 3 && positive_arg("events per producer", argv[2], &events) != 0)
        return EXIT_USAGE;
    return logger_demo(producers, events, 4, DEFAULT_LOG_FILE) == 0 ? EXIT_OK : EXIT_FAIL;
}

static int cmd_ipc(void)
{
    JobSet set;
    if (jobs_load(DEFAULT_JOBS_FILE, &set) != 0)
        return EXIT_FAIL;
    return ipc_demo(&set) == 0 ? EXIT_OK : EXIT_FAIL;
}

static int cmd_banker(int argc, char **argv)
{
    if (argc != 2)
        return usage_error("'banker' needs exactly one state file (try %s)", DEFAULT_BANKER_SAFE);
    int r = banker_run_file(argv[1]);
    return r < 0 ? EXIT_USAGE : EXIT_OK;   /* safe and unsafe are both valid results */
}

static int cmd_workers(int argc, char **argv)
{
    if (argc != 2)
        return usage_error("'workers' needs 'normal' or 'fail'%s", "");
    if (strcmp(argv[1], "normal") == 0)
        return workers_run(0) == 0 ? EXIT_OK : EXIT_FAIL;
    if (strcmp(argv[1], "fail") == 0)
        return workers_run(1) == 0 ? EXIT_OK : EXIT_FAIL;
    return usage_error("unknown workers mode '%s' (use normal or fail)", argv[1]);
}

/* ---------- demo ---------- */

#define MAX_STEPS 16
static const char *step_name[MAX_STEPS];
static int step_ok[MAX_STEPS];
static int nsteps;

static void record(const char *name, int ok)
{
    step_name[nsteps] = name;
    step_ok[nsteps] = ok;
    nsteps++;
}

static int cmd_demo(void)
{
    JobSet set;
    ScheduleResult *res;
    static ScheduleResult r;
    res = &r;

    printf("##### CampusCompute integrated demonstration #####\n");

    int ok = jobs_load(DEFAULT_JOBS_FILE, &set) == 0;
    if (ok) {
        printf("\n=== FR1: job table ===\n");
        jobs_print(&set);
    }
    record("FR1 job management", ok);

    ok = ok && schedule_run(ALG_FCFS, &set, DEFAULT_RR_QUANTUM, res) == 0;
    if (ok) schedule_print(&set, res);
    record("FR2 FCFS", ok);
    ok = ok && schedule_run(ALG_RR, &set, DEFAULT_RR_QUANTUM, res) == 0;
    if (ok) schedule_print(&set, res);
    record("FR2 Round Robin", ok);
    ok = ok && schedule_run(ALG_PRIORITY, &set, DEFAULT_RR_QUANTUM, res) == 0;
    if (ok) schedule_print(&set, res);
    record("FR2 Priority", ok);

    record("FR5 Banker safe state detected", banker_run_file(DEFAULT_BANKER_SAFE) == 1);
    record("FR5 Banker unsafe state detected", banker_run_file(DEFAULT_BANKER_UNSAFE) == 0);

    /* The logger demo owns the logger thread while it runs: stop ours first. */
    logger_stop();
    record("FR3 producer-consumer logger", logger_demo(3, 5, 4, DEFAULT_LOG_FILE) == 0);
    logger_start(DEFAULT_LOG_FILE, 16, 0, 0);

    record("FR4 shared memory + semaphores", ok && ipc_demo(&set) == 0);
    record("FR6 workers (normal)", workers_run(0) == 0);
    record("FR6 workers (simulated failure detected)", workers_run(1) == 0);
    record("FR7 resource limit", rlimit_demo() == 0);

    printf("\n=== FR7: cleanup check ===\n");
    record("FR7 no IPC objects left", ipc_status() == 0);

    int failed = 0;
    printf("\n=============== DEMO SUMMARY ===============\n");
    for (int i = 0; i < nsteps; i++) {
        printf("  [%s] %s\n", step_ok[i] ? "PASS" : "FAIL", step_name[i]);
        if (!step_ok[i])
            failed++;
    }
    printf("Result: %d of %d checks passed. Log file: %s\n", nsteps - failed, nsteps, DEFAULT_LOG_FILE);
    return failed == 0 ? EXIT_OK : EXIT_FAIL;
}

/* ---------- entry point ---------- */

int main(int argc, char **argv)
{
    if (argc < 2) {
        print_usage();
        return EXIT_USAGE;
    }
    const char *cmd = argv[1];
    char **args = argv + 1;           /* args[0] is the command word */
    int nargs = argc - 1;

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_usage();
        return EXIT_OK;
    }

    static const char *known[] = { "jobs", "fcfs", "rr", "priority", "compare", "logger", "ipc",
                                   "banker", "workers", "rlimit", "status", "cleanup", "demo" };
    int is_known = 0;
    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++)
        if (strcmp(cmd, known[i]) == 0)
            is_known = 1;
    if (!is_known)
        return usage_error("unknown command '%s'", cmd);

    /* All commands except 'logger' (which runs its own) write to the shared log. */
    int own_logger = strcmp(cmd, "logger") != 0;
    if (own_logger) {
        if (ensure_dir(DEFAULT_LOG_DIR) != 0 ||
            logger_start(DEFAULT_LOG_FILE, 16, 0, 0) != 0)
            fprintf(stderr, "warning: logging disabled\n");
        logger_log("main", "command '%s' started", cmd);
    }

    int rc;
    if (!strcmp(cmd, "jobs") || !strcmp(cmd, "fcfs") || !strcmp(cmd, "rr") ||
        !strcmp(cmd, "priority") || !strcmp(cmd, "compare"))
        rc = cmd_schedule(cmd, nargs, args);
    else if (!strcmp(cmd, "logger"))
        rc = cmd_logger(nargs, args);
    else if (!strcmp(cmd, "ipc"))
        rc = nargs > 1 ? usage_error("'ipc' takes no arguments%s", "") : cmd_ipc();
    else if (!strcmp(cmd, "banker"))
        rc = cmd_banker(nargs, args);
    else if (!strcmp(cmd, "workers"))
        rc = cmd_workers(nargs, args);
    else if (!strcmp(cmd, "rlimit"))
        rc = nargs > 1 ? usage_error("'rlimit' takes no arguments%s", "") : (rlimit_demo() == 0 ? EXIT_OK : EXIT_FAIL);
    else if (!strcmp(cmd, "status"))
        rc = (ipc_status(), EXIT_OK);
    else if (!strcmp(cmd, "cleanup"))
        rc = (ipc_cleanup(), EXIT_OK);
    else
        rc = cmd_demo();

    if (own_logger) {
        logger_log("main", "command '%s' finished with exit code %d", cmd, rc);
        logger_stop();
    }
    return rc;
}
