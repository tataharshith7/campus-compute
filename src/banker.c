/*
 * banker.c - FR5: Banker's safety algorithm.
 *
 * The course lab files have no Banker's program, so this follows the
 * textbook safety algorithm written in the same plain array-and-loop style
 * as the Lab 2 programs:
 *   1. Need = Maximum - Allocation, Work = Available, Finish[i] = false.
 *   2. Find an i with Finish[i] == false and Need[i] <= Work.
 *   3. Work = Work + Allocation[i], Finish[i] = true, go to step 2.
 *   4. If every Finish[i] is true the state is SAFE, otherwise UNSAFE.
 *
 * State file (all integers, '#' starts a comment):
 *   n m                      number of processes and resource types
 *   available[m]
 *   allocation[n][m]         one row per process
 *   maximum[n][m]            one row per process
 */
#include <stdio.h>

#include "banker.h"
#include "common.h"
#include "logger.h"

int banker_load(const char *path, BankerState *st)
{
    /* One extra slot so "too many values" is detected instead of ignored. */
    int vals[2 + BANKER_MAX_R + 2 * BANKER_MAX_P * BANKER_MAX_R + 1];
    int count, expected, k, i, j;

    count = read_int_tokens(path, vals, (int)(sizeof vals / sizeof vals[0]));
    if (count < 0)
        return -1;
    if (count < 2) {
        fprintf(stderr, "error: %s: missing the 'n m' header line\n", path);
        return -1;
    }

    st->n = vals[0];
    st->m = vals[1];
    if (st->n < 1 || st->n > BANKER_MAX_P || st->m < 1 || st->m > BANKER_MAX_R) {
        fprintf(stderr, "error: %s: need 1..%d processes and 1..%d resource types (got n=%d m=%d)\n",
                path, BANKER_MAX_P, BANKER_MAX_R, st->n, st->m);
        return -1;
    }

    expected = 2 + st->m + 2 * st->n * st->m;
    if (count != expected) {
        fprintf(stderr, "error: %s: expected %d integers for n=%d m=%d "
                "(header + Available + Allocation + Maximum), found %d\n",
                path, expected, st->n, st->m, count);
        return -1;
    }

    k = 2;
    for (j = 0; j < st->m; j++)
        st->available[j] = vals[k++];
    for (i = 0; i < st->n; i++)
        for (j = 0; j < st->m; j++)
            st->allocation[i][j] = vals[k++];
    for (i = 0; i < st->n; i++)
        for (j = 0; j < st->m; j++)
            st->maximum[i][j] = vals[k++];

    for (j = 0; j < st->m; j++) {
        if (st->available[j] < 0) {
            fprintf(stderr, "error: %s: Available[R%d] is negative (%d)\n",
                    path, j, st->available[j]);
            return -1;
        }
    }

    /* Need = Maximum - Allocation; a process can never hold more than its maximum. */
    for (i = 0; i < st->n; i++) {
        for (j = 0; j < st->m; j++) {
            if (st->allocation[i][j] < 0 || st->maximum[i][j] < 0) {
                fprintf(stderr, "error: %s: negative Allocation or Maximum for P%d R%d\n",
                        path, i, j);
                return -1;
            }
            if (st->allocation[i][j] > st->maximum[i][j]) {
                fprintf(stderr, "error: %s: P%d holds %d of R%d but its Maximum is %d\n",
                        path, i, st->allocation[i][j], j, st->maximum[i][j]);
                return -1;
            }
            st->need[i][j] = st->maximum[i][j] - st->allocation[i][j];
        }
    }
    return 0;
}

static void print_vector(const int *v, int m)
{
    int j;

    for (j = 0; j < m; j++)
        printf("%d ", v[j]);
}

void banker_print(const BankerState *st)
{
    int i, j;

    printf("Processes = %d, Resource types = %d (R0..R%d)\n", st->n, st->m, st->m - 1);
    printf("\nAvailable: ");
    print_vector(st->available, st->m);

    printf("\n\nProcess\tAllocation\tMaximum\t\tNeed (Max - Alloc)");
    for (i = 0; i < st->n; i++) {
        printf("\nP%d\t", i);
        for (j = 0; j < st->m; j++)
            printf("%d ", st->allocation[i][j]);
        printf("\t\t");
        for (j = 0; j < st->m; j++)
            printf("%d ", st->maximum[i][j]);
        printf("\t\t");
        for (j = 0; j < st->m; j++)
            printf("%d ", st->need[i][j]);
    }
    printf("\n");
}

int banker_is_safe(const BankerState *st, int *seq, int verbose)
{
    int work[BANKER_MAX_R];
    int finish[BANKER_MAX_P] = { 0 };
    int count = 0, found, pass = 0;
    int i, j;

    /* Step 1: Work = Available */
    for (j = 0; j < st->m; j++)
        work[j] = st->available[j];

    if (verbose) {
        printf("\nSafety check (scan P0..P%d in order, repeat until no process can finish)\n",
               st->n - 1);
        printf("Initial Work = Available = ");
        print_vector(work, st->m);
        printf("\n");
    }

    while (count < st->n) {
        found = 0;
        pass++;
        if (verbose)
            printf("Pass %d:\n", pass);

        for (i = 0; i < st->n; i++) {
            if (finish[i] == 1)
                continue;

            /* Step 2: is Need[i] <= Work for every resource? */
            for (j = 0; j < st->m; j++)
                if (st->need[i][j] > work[j])
                    break;

            if (verbose) {
                printf("  P%d: Need ", i);
                print_vector(st->need[i], st->m);
                printf("%s Work ", j == st->m ? "<=" : "> ");
                print_vector(work, st->m);
            }

            if (j == st->m) {
                /* Step 3: P[i] can finish and releases what it holds */
                for (j = 0; j < st->m; j++)
                    work[j] += st->allocation[i][j];
                finish[i] = 1;
                seq[count++] = i;
                found = 1;
                if (verbose) {
                    printf("-> P%d finishes, releases ", i);
                    print_vector(st->allocation[i], st->m);
                    printf("-> Work = ");
                    print_vector(work, st->m);
                    printf("\n");
                }
                logger_log("banker", "P%d can finish, Work now has %d types", i, st->m);
            } else if (verbose) {
                printf("-> must wait\n");
            }
        }

        /* A whole pass with no progress: nobody else can ever finish */
        if (found == 0)
            break;
    }

    /* Step 4 */
    if (count == st->n)
        return 1;

    if (verbose) {
        printf("\nNo remaining process can finish with Work = ");
        print_vector(work, st->m);
        printf("\nBlocked processes:\n");
        for (i = 0; i < st->n; i++) {
            if (finish[i] == 1)
                continue;
            printf("  P%d Need ", i);
            print_vector(st->need[i], st->m);
            printf("is more than Work for:");
            for (j = 0; j < st->m; j++)
                if (st->need[i][j] > work[j])
                    printf(" R%d", j);
            printf("\n");
        }
    }
    return 0;
}

int banker_run_file(const char *path)
{
    BankerState st;
    int seq[BANKER_MAX_P];
    int safe, i;

    if (banker_load(path, &st) != 0)
        return -1;

    printf("\n===== Banker's Safety Algorithm: %s =====\n", path);
    banker_print(&st);

    safe = banker_is_safe(&st, seq, 1);
    if (safe) {
        printf("\nRESULT: SAFE state. Safe sequence: < ");
        for (i = 0; i < st.n; i++)
            printf("P%d ", seq[i]);
        printf(">\n");
        logger_log("banker", "%s: SAFE", path);
    } else {
        printf("\nRESULT: UNSAFE state. No safe sequence exists, so the system could deadlock.\n");
        logger_log("banker", "%s: UNSAFE", path);
    }
    return safe;
}
