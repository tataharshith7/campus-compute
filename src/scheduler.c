/*
 * scheduler.c - FR2: CPU scheduling simulation (FCFS, Round Robin and
 * non-preemptive Priority).
 *
 * The three algorithms are built from the course Lab 2 programs:
 *   - FCFS.c         sort by arrival (then process id), CPU idles until the
 *                    next job arrives, start/completion/waiting/turnaround.
 *   - Round_robin.c  ready queue with front/rear and visited[]; after each
 *                    slice the newly arrived jobs are queued FIRST and the
 *                    preempted job goes to the back.
 *   - priority.c     pick the highest priority among arrived jobs, tie on
 *                    arrival. The lab version is preemptive (runs 1 unit at a
 *                    time); here the selected job runs its whole burst, which
 *                    is what the brief asks for (non-preemptive).
 * Changes from the labs: input comes from a validated JobSet instead of
 * scanf(), a full tie-break rule is applied, response time and a text Gantt
 * chart are added, and events are sent to the activity logger.
 *
 * Time is simulated in integer units.
 *   turnaround = completion - arrival
 *   waiting    = turnaround - burst
 *   response   = first start - arrival
 *
 * Tie-breaking rule (all algorithms): earlier arrival first, then lower job ID.
 * Priority: HIGHER priority number runs first, then the tie-break rule above.
 */
#include <stdio.h>
#include <string.h>

#include "logger.h"
#include "scheduler.h"

const char *alg_name(Algorithm a)
{
    switch (a) {
    case ALG_FCFS:     return "FCFS";
    case ALG_RR:       return "Round Robin";
    case ALG_PRIORITY: return "Priority (non-preemptive)";
    }
    return "?";
}

/*
 * Record one block of the timeline. Consecutive idle blocks are merged
 * (the priority loop advances idle time one unit at a time). Job blocks are
 * kept separate so every Round Robin quantum is visible in the chart.
 */
static void add_slice(ScheduleResult *r, int job_id, int start, int end)
{
    if (end <= start)
        return;

    if (job_id == IDLE_ID && r->nslices > 0) {
        Slice *last = &r->slices[r->nslices - 1];
        if (last->job_id == IDLE_ID && last->end == start) {
            last->end = end;
            return;
        }
    }

    if (r->nslices < MAX_SLICES) {
        r->slices[r->nslices].job_id = job_id;
        r->slices[r->nslices].start = start;
        r->slices[r->nslices].end = end;
        r->nslices++;
    }
    if (job_id != IDLE_ID)
        r->busy_time += end - start;
}

/*
 * Fill idx[] with job indices in arrival order.
 * Same nested-loop swap sort as Lab 2 FCFS.c: "Sort according to Arrival
 * Time. If arrival time is same, smaller Process ID first."
 */
static void sort_by_arrival(const JobSet *set, int *idx)
{
    int i, j, temp;
    int n = set->count;

    for (i = 0; i < n; i++)
        idx[i] = i;

    for (i = 0; i < n - 1; i++) {
        for (j = i + 1; j < n; j++) {
            const Job *a = &set->jobs[idx[i]];
            const Job *b = &set->jobs[idx[j]];
            if (a->arrival > b->arrival ||
                (a->arrival == b->arrival && a->id > b->id)) {
                temp = idx[i];
                idx[i] = idx[j];
                idx[j] = temp;
            }
        }
    }
}

/* Lifecycle helpers: NEW -> READY -> RUNNING -> (READY ->) DONE. */
static void mark_ready(Job *j, int time, Algorithm alg)
{
    j->status = JOB_READY;
    logger_log("sched", "%s: t=%d J%d READY", alg_name(alg), time, j->id);
}

static void mark_running(Job *j, int time, Algorithm alg)
{
    if (j->start < 0)
        j->start = time;          /* first time on the CPU (for response time) */
    j->status = JOB_RUNNING;
    logger_log("sched", "%s: t=%d J%d RUNNING", alg_name(alg), time, j->id);
}

static void mark_done(Job *j, int time, Algorithm alg)
{
    j->remaining = 0;
    j->completion = time;
    j->turnaround = j->completion - j->arrival;
    j->waiting = j->turnaround - j->burst;
    j->response = j->start - j->arrival;
    j->status = JOB_DONE;
    logger_log("sched", "%s: t=%d J%d DONE (TAT=%d WT=%d)",
               alg_name(alg), time, j->id, j->turnaround, j->waiting);
}

/* ---------------------------------------------------------------- FCFS */
/* Based on Lab 2 FCFS.c. */
static void run_fcfs(JobSet *set, ScheduleResult *r)
{
    int idx[MAX_JOBS];
    int i;
    int time = 0;

    sort_by_arrival(set, idx);

    for (i = 0; i < set->count; i++) {
        Job *j = &set->jobs[idx[i]];

        /* CPU remains idle until process arrives */
        if (time < j->arrival) {
            add_slice(r, IDLE_ID, time, j->arrival);
            time = j->arrival;
        }

        mark_ready(j, j->arrival, r->alg);
        mark_running(j, time, r->alg);
        add_slice(r, j->id, time, time + j->burst);
        time = time + j->burst;
        mark_done(j, time, r->alg);
    }
}

/* --------------------------------------------------------- Round Robin */
/* Based on Lab 2 Round_robin.c. */
static void run_round_robin(JobSet *set, ScheduleResult *r, int quantum)
{
    /*
     * Each job enters the queue once when it arrives and once after every
     * slice it does not finish in, so this bound can never be exceeded.
     */
    static int queue[MAX_SLICES + MAX_JOBS];
    int visited[MAX_JOBS] = { 0 };
    int order[MAX_JOBS];
    int front = 0, rear = 0;
    int time = 0, completed = 0;
    int k, p, run;
    int n = set->count;

    /* Admit new arrivals in arrival order (then job id), like FCFS. */
    sort_by_arrival(set, order);

    while (completed < n) {
        /* If ready queue is empty, move time to next arrival */
        if (front == rear) {
            int next = -1;

            for (k = 0; k < n; k++) {
                if (visited[order[k]] == 0) {
                    next = order[k];   /* order[] is sorted, so first unvisited is earliest */
                    break;
                }
            }
            if (time < set->jobs[next].arrival) {
                add_slice(r, IDLE_ID, time, set->jobs[next].arrival);
                time = set->jobs[next].arrival;
            }
            /* Queue every job that has arrived by now (several may share a time). */
            for (k = 0; k < n; k++) {
                int i = order[k];
                if (visited[i] == 0 && set->jobs[i].arrival <= time) {
                    queue[rear++] = i;
                    visited[i] = 1;
                    mark_ready(&set->jobs[i], set->jobs[i].arrival, r->alg);
                }
            }
        }

        /* Select process from ready queue */
        p = queue[front++];

        /* Execute for time quantum or remaining burst time */
        run = set->jobs[p].remaining > quantum ? quantum : set->jobs[p].remaining;
        mark_running(&set->jobs[p], time, r->alg);
        add_slice(r, set->jobs[p].id, time, time + run);
        time = time + run;
        set->jobs[p].remaining = set->jobs[p].remaining - run;

        /*
         * Add all newly arrived processes first.
         * This follows the ready-queue order used in the college material.
         */
        for (k = 0; k < n; k++) {
            int i = order[k];
            if (visited[i] == 0 && set->jobs[i].arrival <= time) {
                queue[rear++] = i;
                visited[i] = 1;
                mark_ready(&set->jobs[i], set->jobs[i].arrival, r->alg);
            }
        }

        /* If current process is not completed, put it back */
        if (set->jobs[p].remaining > 0) {
            queue[rear++] = p;
            set->jobs[p].status = JOB_READY;
            logger_log("sched", "%s: t=%d J%d quantum expired, back to READY",
                       alg_name(r->alg), time, set->jobs[p].id);
        } else {
            completed++;
            mark_done(&set->jobs[p], time, r->alg);
        }
    }
}

/* ------------------------------------------------- Priority (non-preemptive) */

/* Does job a beat job b? Higher priority number, then earlier arrival, then lower id. */
static int has_higher_priority(const Job *a, const Job *b)
{
    if (a->priority != b->priority)
        return a->priority > b->priority;
    if (a->arrival != b->arrival)
        return a->arrival < b->arrival;
    return a->id < b->id;
}

/* Based on Lab 2 priority.c, changed to run the selected job to completion. */
static void run_priority(JobSet *set, ScheduleResult *r)
{
    int time = 0, count = 0;
    int i, selected;
    int n = set->count;

    while (count < n) {
        selected = -1;

        /* Select highest priority among arrived processes */
        for (i = 0; i < n; i++) {
            Job *j = &set->jobs[i];

            if (j->status == JOB_DONE || j->arrival > time)
                continue;
            if (j->status == JOB_NEW)
                mark_ready(j, j->arrival, r->alg);
            if (selected == -1 || has_higher_priority(j, &set->jobs[selected]))
                selected = i;
        }

        /* No process has arrived: CPU idle for one unit */
        if (selected == -1) {
            add_slice(r, IDLE_ID, time, time + 1);
            time++;
            continue;
        }

        /* Non-preemptive: the selected job keeps the CPU for its whole burst */
        mark_running(&set->jobs[selected], time, r->alg);
        add_slice(r, set->jobs[selected].id, time, time + set->jobs[selected].burst);
        time = time + set->jobs[selected].burst;
        count++;
        mark_done(&set->jobs[selected], time, r->alg);
    }
}

/* ------------------------------------------------------------- driver */

int schedule_run(Algorithm alg, JobSet *set, int quantum, ScheduleResult *out)
{
    int i;
    int total_wt = 0, total_tat = 0, total_rt = 0;

    if (set->count <= 0) {
        fprintf(stderr, "error: no jobs to schedule\n");
        return -1;
    }
    if (alg == ALG_RR && quantum <= 0) {
        fprintf(stderr, "error: Round Robin time quantum must be a positive integer\n");
        return -1;
    }

    memset(out, 0, sizeof *out);
    out->alg = alg;
    out->quantum = quantum;
    jobs_reset(set);

    if (alg == ALG_RR)
        logger_log("sched", "%s started on %d jobs (quantum=%d)", alg_name(alg), set->count, quantum);
    else
        logger_log("sched", "%s started on %d jobs", alg_name(alg), set->count);

    switch (alg) {
    case ALG_FCFS:     run_fcfs(set, out); break;
    case ALG_RR:       run_round_robin(set, out, quantum); break;
    case ALG_PRIORITY: run_priority(set, out); break;
    }

    for (i = 0; i < set->count; i++) {
        total_wt += set->jobs[i].waiting;
        total_tat += set->jobs[i].turnaround;
        total_rt += set->jobs[i].response;
        if (set->jobs[i].completion > out->makespan)
            out->makespan = set->jobs[i].completion;
    }
    out->avg_waiting = (double)total_wt / set->count;
    out->avg_turnaround = (double)total_tat / set->count;
    out->avg_response = (double)total_rt / set->count;

    logger_log("sched", "%s finished: avg WT=%.2f avg TAT=%.2f", alg_name(alg),
               out->avg_waiting, out->avg_turnaround);
    return 0;
}

/* Text Gantt chart, 10 blocks per row, with the time under each boundary. */
static void print_gantt(const ScheduleResult *r)
{
    const int per_row = 10;
    int base, s, end;

    for (base = 0; base < r->nslices; base += per_row) {
        end = base + per_row < r->nslices ? base + per_row : r->nslices;

        printf("|");
        for (s = base; s < end; s++) {
            if (r->slices[s].job_id == IDLE_ID)
                printf(" idle |");
            else
                printf(" J%-3d |", r->slices[s].job_id);
        }
        printf("\n");
        for (s = base; s < end; s++)
            printf("%-7d", r->slices[s].start);
        printf("%d\n", r->slices[end - 1].end);
    }
}

static void print_order(const ScheduleResult *r)
{
    int s;

    printf("Execution order: ");
    for (s = 0; s < r->nslices; s++) {
        if (s > 0)
            printf(" -> ");
        if (r->slices[s].job_id == IDLE_ID)
            printf("idle(%d-%d)", r->slices[s].start, r->slices[s].end);
        else
            printf("J%d", r->slices[s].job_id);
    }
    printf("\n");
}

static void print_rules(const ScheduleResult *r)
{
    if (r->alg == ALG_RR)
        printf("Time quantum = %d\n", r->quantum);
    if (r->alg == ALG_PRIORITY)
        printf("Rule: HIGHER priority number runs first; a running job is never preempted\n");
    printf("Tie-breaking: earlier arrival first, then lower job ID");
    if (r->alg == ALG_RR)
        printf("; jobs arriving during a slice queue before the preempted job");
    printf("\n");
}

void schedule_print(const JobSet *set, const ScheduleResult *r)
{
    int i;

    printf("\n===== %s Scheduling =====\n", alg_name(r->alg));
    print_rules(r);
    printf("\n");
    print_order(r);
    printf("\nGantt chart:\n");
    print_gantt(r);

    /* Same table layout as the Lab 2 programs, plus priority/start/response/status. */
    printf("\nJob\tArrival\tBurst\tPriority\tStart\tCompletion\tWaiting\tTurnaround\tResponse\tStatus");
    for (i = 0; i < set->count; i++) {
        const Job *j = &set->jobs[i];
        printf("\nJ%d\t%d\t%d\t%d\t\t%d\t%d\t\t%d\t%d\t\t%d\t\t%s",
               j->id, j->arrival, j->burst, j->priority, j->start,
               j->completion, j->waiting, j->turnaround, j->response,
               job_status_name(j->status));
    }

    printf("\n\nAverage Waiting Time = %.2f", r->avg_waiting);
    printf("\nAverage Turn Around Time = %.2f", r->avg_turnaround);
    printf("\nAverage Response Time = %.2f", r->avg_response);
    printf("\nTotal time = %d, CPU busy = %d, CPU utilisation = %.1f%%\n",
           r->makespan, r->busy_time,
           r->makespan ? 100.0 * r->busy_time / r->makespan : 0.0);
}

int schedule_compare(JobSet *set, int quantum)
{
    /* static: each ScheduleResult holds the whole timeline and is large */
    static ScheduleResult results[3];
    const Algorithm algs[3] = { ALG_FCFS, ALG_RR, ALG_PRIORITY };
    int a, i, best_wt = 0, best_tat = 0, best_rt = 0;

    printf("\n===== Scheduling comparison: same %d jobs, RR time quantum = %d =====\n",
           set->count, quantum);
    printf("Tie-breaking (all): earlier arrival, then lower job ID. "
           "Priority: higher number first, non-preemptive.\n");

    for (a = 0; a < 3; a++) {
        if (schedule_run(algs[a], set, quantum, &results[a]) != 0)
            return -1;
        printf("\n%s\n", alg_name(algs[a]));
        print_gantt(&results[a]);

        /* Completion time per job under this algorithm */
        printf("Completion:");
        for (i = 0; i < set->count; i++)
            printf(" J%d=%d", set->jobs[i].id, set->jobs[i].completion);
        printf("\n");
    }

    printf("\n%-27s %-8s %-8s %-8s %-10s %-10s\n",
           "Algorithm", "Avg WT", "Avg TAT", "Avg RT", "Total time", "Switches");
    printf("------------------------------------------------------------------------\n");
    for (a = 0; a < 3; a++) {
        const ScheduleResult *r = &results[a];
        int switches = 0;

        /* Count changes from one job to a different job (idle gaps are not switches). */
        for (i = 1; i < r->nslices; i++)
            if (r->slices[i].job_id != IDLE_ID && r->slices[i - 1].job_id != IDLE_ID &&
                r->slices[i].job_id != r->slices[i - 1].job_id)
                switches++;

        printf("%-27s %-8.2f %-8.2f %-8.2f %-10d %-10d\n", alg_name(r->alg),
               r->avg_waiting, r->avg_turnaround, r->avg_response, r->makespan, switches);

        if (r->avg_waiting < results[best_wt].avg_waiting)
            best_wt = a;
        if (r->avg_turnaround < results[best_tat].avg_turnaround)
            best_tat = a;
        if (r->avg_response < results[best_rt].avg_response)
            best_rt = a;
    }
    printf("------------------------------------------------------------------------\n");
    printf("Lowest average waiting time    : %s (%.2f)\n",
           alg_name(results[best_wt].alg), results[best_wt].avg_waiting);
    printf("Lowest average turnaround time : %s (%.2f)\n",
           alg_name(results[best_tat].alg), results[best_tat].avg_turnaround);
    printf("Lowest average response time   : %s (%.2f)\n",
           alg_name(results[best_rt].alg), results[best_rt].avg_response);

    logger_log("sched", "comparison done: best WT=%s best RT=%s",
               alg_name(results[best_wt].alg), alg_name(results[best_rt].alg));
    return 0;
}
