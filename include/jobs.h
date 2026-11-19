/*
 * jobs.h - FR1: job representation, loading/validation, lifecycle status.
 */
#ifndef JOBS_H
#define JOBS_H

#define MAX_JOBS      64
#define MAX_BURST     100   /* keeps Round Robin timelines bounded */

typedef enum {
    JOB_NEW,       /* loaded, has not arrived yet          */
    JOB_READY,     /* arrived, waiting in the ready queue  */
    JOB_RUNNING,   /* currently on the (simulated) CPU     */
    JOB_DONE       /* finished executing                   */
} JobStatus;

typedef struct {
    int id;
    int arrival;
    int burst;
    int priority;     /* higher number = higher priority (course convention) */

    /* Filled in by the schedulers. */
    int remaining;
    int start;        /* first time on CPU, -1 if never ran */
    int completion;
    int turnaround;   /* completion - arrival */
    int waiting;      /* turnaround - burst   */
    int response;     /* start - arrival      */
    JobStatus status;
} Job;

typedef struct {
    Job jobs[MAX_JOBS];
    int count;
} JobSet;

/* Load and validate a job file. Returns 0 on success, -1 on error. */
int jobs_load(const char *path, JobSet *set);

/* Reset all per-run fields so the same set can be scheduled again. */
void jobs_reset(JobSet *set);

/* Print the job table with current status. */
void jobs_print(const JobSet *set);

const char *job_status_name(JobStatus s);

#endif
