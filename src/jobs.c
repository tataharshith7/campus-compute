/*
 * jobs.c - FR1: load a job file, validate every field, track job status.
 *
 * The course Lab 2 programs read "Process ID, Arrival Time, Burst Time"
 * with scanf(). Here the same fields (plus priority) come from a file so
 * the test input is repeatable, and every value is checked before use.
 *
 * File format: one job per line, "id arrival burst priority".
 * Blank lines and text after '#' are ignored.
 * Priority: a HIGHER number means a higher priority (as in Lab 2 priority.c).
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "jobs.h"
#include "logger.h"

/* Upper bound on arrival time so simulated clocks stay small and cannot overflow. */
#define MAX_ARRIVAL 1000

const char *job_status_name(JobStatus s)
{
    switch (s) {
    case JOB_NEW:     return "NEW";
    case JOB_READY:   return "READY";
    case JOB_RUNNING: return "RUNNING";
    case JOB_DONE:    return "DONE";
    }
    return "?";
}

/* Put every job back into its loaded state so it can be scheduled again. */
void jobs_reset(JobSet *set)
{
    int i;

    for (i = 0; i < set->count; i++) {
        Job *j = &set->jobs[i];
        j->remaining = j->burst;
        j->start = -1;
        j->completion = 0;
        j->turnaround = 0;
        j->waiting = 0;
        j->response = 0;
        j->status = JOB_NEW;
    }
}

/* Check one parsed job. Prints "file:line: reason" and returns -1 if invalid. */
static int validate_job(const char *path, int lineno, const Job *j, const JobSet *set)
{
    int i;

    if (j->id <= 0) {
        fprintf(stderr, "error: %s:%d: job id must be a positive integer (got %d)\n",
                path, lineno, j->id);
        return -1;
    }
    if (j->arrival < 0 || j->arrival > MAX_ARRIVAL) {
        fprintf(stderr, "error: %s:%d: arrival time must be between 0 and %d (got %d)\n",
                path, lineno, MAX_ARRIVAL, j->arrival);
        return -1;
    }
    if (j->burst <= 0 || j->burst > MAX_BURST) {
        fprintf(stderr, "error: %s:%d: CPU burst must be between 1 and %d (got %d)\n",
                path, lineno, MAX_BURST, j->burst);
        return -1;
    }
    if (j->priority < 0) {
        fprintf(stderr, "error: %s:%d: priority must be >= 0 (got %d)\n",
                path, lineno, j->priority);
        return -1;
    }
    for (i = 0; i < set->count; i++) {
        if (set->jobs[i].id == j->id) {
            fprintf(stderr, "error: %s:%d: duplicate job id %d\n", path, lineno, j->id);
            return -1;
        }
    }
    return 0;
}

int jobs_load(const char *path, JobSet *set)
{
    FILE *f;
    char line[256];
    int lineno = 0;

    set->count = 0;

    f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "error: cannot open job file '%s': %s\n", path, strerror(errno));
        return -1;
    }

    while (fgets(line, sizeof line, f) != NULL) {
        char *tok[5];
        char *t;
        char *hash;
        int ntok = 0;
        int k;
        Job j;

        lineno++;

        /* A line longer than the buffer would be split silently; reject it. */
        if (strchr(line, '\n') == NULL && !feof(f)) {
            fprintf(stderr, "error: %s:%d: line is too long\n", path, lineno);
            fclose(f);
            return -1;
        }

        hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';

        /* Split into fields; exactly 4 are expected (a 5th means "too many"). */
        for (t = strtok(line, " \t\r\n"); t != NULL && ntok < 5; t = strtok(NULL, " \t\r\n"))
            tok[ntok++] = t;

        if (ntok == 0)
            continue;   /* blank or comment-only line */

        if (ntok != 4) {
            fprintf(stderr, "error: %s:%d: expected 4 fields 'id arrival burst priority', got %s\n",
                    path, lineno, ntok < 4 ? "too few" : "too many");
            fclose(f);
            return -1;
        }

        memset(&j, 0, sizeof j);
        {
            int *fields[4] = { &j.id, &j.arrival, &j.burst, &j.priority };
            const char *names[4] = { "id", "arrival", "burst", "priority" };

            for (k = 0; k < 4; k++) {
                if (parse_int(tok[k], fields[k]) != 0) {
                    fprintf(stderr, "error: %s:%d: %s '%s' is not a valid integer\n",
                            path, lineno, names[k], tok[k]);
                    fclose(f);
                    return -1;
                }
            }
        }

        if (validate_job(path, lineno, &j, set) != 0) {
            fclose(f);
            return -1;
        }
        if (set->count >= MAX_JOBS) {
            fprintf(stderr, "error: %s:%d: too many jobs (limit %d)\n", path, lineno, MAX_JOBS);
            fclose(f);
            return -1;
        }
        set->jobs[set->count++] = j;
    }

    if (ferror(f)) {
        fprintf(stderr, "error: cannot read job file '%s'\n", path);
        fclose(f);
        return -1;
    }
    fclose(f);

    if (set->count == 0) {
        fprintf(stderr, "error: %s: no jobs found\n", path);
        return -1;
    }

    jobs_reset(set);
    logger_log("jobs", "loaded %d jobs from %s, all NEW", set->count, path);
    return 0;
}

void jobs_print(const JobSet *set)
{
    int i;

    printf("\nJob\tArrival\tBurst\tPriority\tStatus");
    for (i = 0; i < set->count; i++) {
        const Job *j = &set->jobs[i];
        printf("\nJ%d\t%d\t%d\t%d\t\t%s",
               j->id, j->arrival, j->burst, j->priority, job_status_name(j->status));
    }
    printf("\n\nTotal jobs = %d (higher priority number = higher priority)\n", set->count);
}
