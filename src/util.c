/*
 * util.c - input parsing helpers shared by all modules.
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "common.h"

int parse_int(const char *s, int *out)
{
    if (s == NULL || *s == '\0')
        return -1;

    char *end = NULL;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || *end != '\0' || v < INT_MIN || v > INT_MAX)
        return -1;

    *out = (int)v;
    return 0;
}

int read_int_tokens(const char *path, int *vals, int max_vals)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "error: cannot open '%s': %s\n", path, strerror(errno));
        return -1;
    }

    char line[512];
    int n = 0;
    int lineno = 0;
    while (fgets(line, sizeof line, f) != NULL) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash != NULL)
            *hash = '\0';

        for (char *tok = strtok(line, " \t\r\n"); tok != NULL; tok = strtok(NULL, " \t\r\n")) {
            int v;
            if (parse_int(tok, &v) != 0) {
                fprintf(stderr, "error: %s:%d: '%s' is not a valid integer\n", path, lineno, tok);
                fclose(f);
                return -1;
            }
            if (n >= max_vals) {
                fprintf(stderr, "error: %s: too many values (limit %d)\n", path, max_vals);
                fclose(f);
                return -1;
            }
            vals[n++] = v;
        }
    }
    fclose(f);
    return n;
}

int ensure_dir(const char *path)
{
    if (mkdir(path, 0755) == 0 || errno == EEXIST)
        return 0;
    fprintf(stderr, "error: cannot create directory '%s': %s\n", path, strerror(errno));
    return -1;
}
