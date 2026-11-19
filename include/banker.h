/*
 * banker.h - FR5: Banker's safety algorithm.
 */
#ifndef BANKER_H
#define BANKER_H

#define BANKER_MAX_P 10
#define BANKER_MAX_R 10

typedef struct {
    int n;                                   /* processes      */
    int m;                                   /* resource types */
    int available[BANKER_MAX_R];
    int allocation[BANKER_MAX_P][BANKER_MAX_R];
    int maximum[BANKER_MAX_P][BANKER_MAX_R];
    int need[BANKER_MAX_P][BANKER_MAX_R];    /* maximum - allocation */
} BankerState;

/* Load and validate a state file. Returns 0 on success, -1 on error. */
int banker_load(const char *path, BankerState *st);

/* Print Available, Allocation, Maximum and Need. */
void banker_print(const BankerState *st);

/*
 * Run the safety algorithm, printing each step when verbose.
 * Returns 1 if safe (seq[] holds the safe sequence), 0 if unsafe.
 */
int banker_is_safe(const BankerState *st, int *seq, int verbose);

/* Load + print + check one file. Returns 1 safe, 0 unsafe, -1 error. */
int banker_run_file(const char *path);

#endif
