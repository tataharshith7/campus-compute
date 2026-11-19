# CampusCompute

Command-line prototype for the OS Level 3 mini-project: job scheduling, a
pthread producer-consumer logger, POSIX shared-memory IPC, Banker's safety
check, worker failure detection with fork/waitpid, and a resource-limit demo.

## Prerequisites

Linux, Ubuntu or WSL2 Ubuntu, with GCC and Make:

    sudo apt update && sudo apt install build-essential

## Build

    make            # builds ./campuscompute (flags: -std=c11 -Wall -Wextra, links -pthread -lrt)
    make clean      # removes build/, the binary and logs/

## Run

    ./campuscompute help

| Command | Feature |
|---|---|
| `jobs [file]` | FR1: load, validate and show jobs (default `data/jobs.txt`) |
| `fcfs [file]` | FR2: First Come First Served |
| `rr [file] [quantum]` | FR2: Round Robin, default quantum 2 (`rr 3` also works) |
| `priority [file]` | FR2: non-preemptive Priority |
| `compare [file] [quantum]` | FR2: all three policies on the same jobs |
| `logger [producers] [events]` | FR3: pthread producer-consumer logger (default 3 x 5) |
| `ipc` | FR4: shared memory + named semaphores between two processes |
| `banker <file>` | FR5: Banker's safety check (`data/banker_safe.txt`, `data/banker_unsafe.txt`) |
| `workers normal` / `workers fail` | FR6: fork/waitpid, normal and simulated failure |
| `rlimit` | FR7: setrlimit demonstration |
| `status` | FR7: which CampusCompute IPC objects exist |
| `cleanup` | FR7: remove only CampusCompute's own IPC objects |
| `demo` | everything above plus a PASS/FAIL summary (FR8: integrated run) |

Invalid commands or input print an error plus the usage text and exit with code 2.

## Rules used

- Job file: one job per line, `id arrival burst priority`; `#` starts a comment.
- Priority: a HIGHER number means higher priority (as in the lab notes). Priority scheduling is non-preemptive.
- Tie-breaking: earlier arrival first, then lower job id.
- Round Robin: default time quantum 2; jobs that arrive during a slice are queued before the preempted job.
- Metrics: turnaround = completion - arrival; waiting = turnaround - burst.
- Worker failures in `workers fail` are SIMULATED and labelled as such.

## Files created and cleanup

- `logs/campuscompute.log`: activity log (created at run time; `make clean` removes it).
- POSIX IPC objects: `/campuscompute_shm`, `/campuscompute_empty`, `/campuscompute_full`, `/campuscompute_mutex`.
  The program removes them itself. If a run is interrupted (Ctrl+C), run `./campuscompute cleanup`.
  Check with `./campuscompute status` or `ls /dev/shm`.
  Only these four names are ever touched.

## Layout

    include/   headers (one per feature)
    src/       main.c util.c jobs.c scheduler.c banker.c logger.c ipc.c workers.c rlimit_demo.c
    data/      jobs.txt banker_safe.txt banker_unsafe.txt
    Makefile README.md
