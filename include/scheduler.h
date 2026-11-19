/*
 * scheduler.h - FR2: FCFS, Round Robin and non-preemptive Priority scheduling.
 *
 * Tie-breaking rule (all algorithms): earlier arrival time first, then
 * lower job ID. For Priority, the HIGHER priority number wins first
 * (same convention as the course Lab 2 priority.c).
 * Round Robin: jobs arriving during a time slice are queued BEFORE the
 * job that was just preempted.
 */
#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "jobs.h"

#define MAX_SLICES  (MAX_JOBS * MAX_BURST + MAX_JOBS)
#define IDLE_ID     (-1)

typedef enum { ALG_FCFS, ALG_RR, ALG_PRIORITY } Algorithm;

typedef struct {
    int job_id;   /* IDLE_ID when the CPU is idle */
    int start;
    int end;
} Slice;

typedef struct {
    Algorithm alg;
    int quantum;            /* only used by Round Robin */
    Slice slices[MAX_SLICES];
    int nslices;
    int makespan;           /* time when the last job completed */
    int busy_time;
    double avg_turnaround;
    double avg_waiting;
    double avg_response;
} ScheduleResult;

const char *alg_name(Algorithm a);

/* Run one algorithm on the job set (the set is reset first). Returns 0 on success. */
int schedule_run(Algorithm alg, JobSet *set, int quantum, ScheduleResult *out);

/* Print timeline, per-job metrics and averages. */
void schedule_print(const JobSet *set, const ScheduleResult *r);

/* Run all three algorithms on the same job set and print a comparison table. */
int schedule_compare(JobSet *set, int quantum);

#endif
