/*
 * common.h - shared constants and small helpers for CampusCompute.
 */
#ifndef COMMON_H
#define COMMON_H

#define DEFAULT_JOBS_FILE      "data/jobs.txt"
#define DEFAULT_BANKER_SAFE    "data/banker_safe.txt"
#define DEFAULT_BANKER_UNSAFE  "data/banker_unsafe.txt"
#define DEFAULT_LOG_DIR        "logs"
#define DEFAULT_LOG_FILE       "logs/campuscompute.log"
#define DEFAULT_RR_QUANTUM     2

/* Exit codes used by every command. */
#define EXIT_OK     0
#define EXIT_FAIL   1
#define EXIT_USAGE  2

/* Strictly parse a base-10 int. Returns 0 on success, -1 on malformed input. */
int parse_int(const char *s, int *out);

/*
 * Read every integer from a text file into vals[]. Text after '#' on a line
 * is a comment. Returns the number of integers read, or -1 on error
 * (an error message has already been printed).
 */
int read_int_tokens(const char *path, int *vals, int max_vals);

/* Create directory if it does not exist. Returns 0 on success. */
int ensure_dir(const char *path);

#endif
