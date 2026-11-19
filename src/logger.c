/*
 * logger.c - FR3: concurrent activity logger (bounded producer-consumer queue).
 *
 * Based on the Unit 2 notes:
 *   - "Bounded buffer with a circular array": buffer[], in, out, wrap with % size.
 *   - "Producer-consumer with a shared counter": counter tells full / empty.
 *   - "POSIX thread API": pthread_create() / pthread_join().
 * The notes' version busy-waits and leaves `counter` unprotected (they show it
 * can lose updates). Here every access to buffer/in/out/counter happens while
 * holding one pthread mutex, and instead of spinning:
 *   producer: while (counter == size) wait on condition variable not_full
 *   consumer: while (counter == 0)    wait on condition variable not_empty
 *
 * Clean termination: logger_stop() sets `shutdown`, broadcasts both conditions,
 * the consumer keeps writing until the queue is empty, then exits and is joined.
 * No accepted entry is ever lost.
 */
#include <errno.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "logger.h"

/* One log entry (the notes' "item"). */
typedef struct {
    char text[LOG_MSG_LEN];
} log_item;

/* The shared queue and everything the mutex protects. */
static log_item buffer[LOG_MAX_CAP];
static int buffer_size;        /* capacity chosen at start (BUFFER_SIZE)   */
static int in = 0;             /* next free slot (producer side)           */
static int out = 0;            /* next filled slot (consumer side)         */
static int counter = 0;        /* number of filled slots                   */
static int shutdown_requested = 0;
static LoggerStats stats;

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;
static pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;

/* Set up once by logger_start(), read-only while the consumer runs. */
static int running = 0;
static int echo_entries = 0;
static int consumer_delay_us = 0;
static pid_t owner_pid = 0;    /* process that owns the consumer thread */
static FILE *log_fp = NULL;
static pthread_t consumer_tid;

/* "2026-11-10 18:55:29.123" */
static void make_timestamp(char *out_buf, size_t n)
{
    struct timespec ts;
    struct tm tm;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm);
    size_t k = strftime(out_buf, n, "%Y-%m-%d %H:%M:%S", &tm);
    snprintf(out_buf + k, n - k, ".%03ld", ts.tv_nsec / 1000000L);
}

/* Consumer thread: remove entries from the queue and write them to the file. */
static void *consumer_thread(void *arg)
{
    (void)arg;
    log_item next_consumed;

    while (1) {
        pthread_mutex_lock(&mutex);
        while (counter == 0 && !shutdown_requested) {   /* queue empty: sleep */
            stats.empty_waits++;
            pthread_cond_wait(&not_empty, &mutex);
        }
        if (counter == 0 && shutdown_requested) {       /* drained and told to stop */
            pthread_mutex_unlock(&mutex);
            break;
        }
        next_consumed = buffer[out];
        out = (out + 1) % buffer_size;
        counter--;
        pthread_cond_signal(&not_full);                 /* a slot is free again */
        pthread_mutex_unlock(&mutex);

        /* File I/O is done outside the lock so producers are not held up by it. */
        fprintf(log_fp, "%s\n", next_consumed.text);
        fflush(log_fp);
        if (echo_entries) {
            printf("    [consumer] wrote: %s\n", next_consumed.text);
            fflush(stdout);
        }

        pthread_mutex_lock(&mutex);
        stats.consumed++;
        pthread_mutex_unlock(&mutex);

        if (consumer_delay_us > 0)
            usleep((useconds_t)consumer_delay_us);
    }
    return NULL;
}

int logger_start(const char *path, int capacity, int echo, int delay_us)
{
    if (capacity < 1 || capacity > LOG_MAX_CAP) {
        fprintf(stderr, "error: logger queue capacity must be 1..%d (got %d)\n", LOG_MAX_CAP, capacity);
        return -1;
    }
    if (running) {
        fprintf(stderr, "error: logger is already running\n");
        return -1;
    }
    if (ensure_dir(DEFAULT_LOG_DIR) != 0)
        return -1;

    FILE *fp = fopen(path, "a");
    if (fp == NULL) {
        fprintf(stderr, "error: cannot open log file '%s': %s\n", path, strerror(errno));
        return -1;
    }
    char ts[40];
    make_timestamp(ts, sizeof ts);
    fprintf(fp, "----- [%s] CampusCompute session started (pid %d) -----\n", ts, (int)getpid());
    fflush(fp);

    /* No other thread exists yet, so no lock is needed for the reset. */
    log_fp = fp;
    buffer_size = capacity;
    in = out = counter = 0;
    shutdown_requested = 0;
    echo_entries = echo;
    consumer_delay_us = delay_us;
    owner_pid = getpid();
    memset(&stats, 0, sizeof stats);

    int rc = pthread_create(&consumer_tid, NULL, consumer_thread, NULL);
    if (rc != 0) {
        fprintf(stderr, "error: pthread_create(consumer): %s\n", strerror(rc));
        fclose(fp);
        log_fp = NULL;
        return -1;
    }
    running = 1;
    return 0;
}

/* Producer side: format one entry and insert it into the bounded queue. */
int logger_log(const char *source, const char *fmt, ...)
{
    /*
     * A forked child gets a copy of this state but NOT the consumer thread
     * (fork copies only the calling thread), so logging there is a no-op.
     */
    if (!running || getpid() != owner_pid)
        return -1;

    /* Sized so timestamp + source + message always fit in one LOG_MSG_LEN entry. */
    char ts[32], msg[LOG_MSG_LEN - 64];
    log_item next_produced;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    make_timestamp(ts, sizeof ts);
    snprintf(next_produced.text, sizeof next_produced.text, "[%s] [%-10.16s] %s", ts, source, msg);

    pthread_mutex_lock(&mutex);
    while (counter == buffer_size && !shutdown_requested) {   /* queue full: sleep */
        stats.full_waits++;
        pthread_cond_wait(&not_full, &mutex);
    }
    if (shutdown_requested) {
        pthread_mutex_unlock(&mutex);
        return -1;
    }
    buffer[in] = next_produced;
    in = (in + 1) % buffer_size;
    counter++;
    stats.produced++;
    if (counter > stats.max_occupancy)
        stats.max_occupancy = counter;
    pthread_cond_signal(&not_empty);                          /* wake the consumer */
    pthread_mutex_unlock(&mutex);
    return 0;
}

int logger_stop(void)
{
    if (!running || getpid() != owner_pid)
        return -1;

    pthread_mutex_lock(&mutex);
    shutdown_requested = 1;
    pthread_cond_broadcast(&not_empty);
    pthread_cond_broadcast(&not_full);
    pthread_mutex_unlock(&mutex);

    pthread_join(consumer_tid, NULL);   /* consumer drains the queue before exiting */

    char ts[40];
    make_timestamp(ts, sizeof ts);
    fprintf(log_fp, "----- [%s] session ended: %ld entries written -----\n", ts, stats.consumed);
    fclose(log_fp);
    log_fp = NULL;
    running = 0;
    return (int)stats.consumed;
}

int logger_is_running(void)
{
    return running;
}

void logger_get_stats(LoggerStats *dst)
{
    pthread_mutex_lock(&mutex);
    *dst = stats;
    pthread_mutex_unlock(&mutex);
}

/* ---------------- FR3 demonstration ---------------- */

#define MAX_PRODUCERS 8

/* Argument for each producer thread; lives in logger_demo() until it is joined. */
typedef struct {
    int id;
    int events;
    int logged;
} producer_arg;

static void *producer_thread(void *arg)
{
    producer_arg *p = arg;
    char source[16];
    snprintf(source, sizeof source, "producer-%d", p->id);

    for (int e = 1; e <= p->events; e++) {
        printf("  [producer %d] queueing event %d/%d\n", p->id, e, p->events);
        fflush(stdout);
        if (logger_log(source, "lab event %d of %d: job submitted by producer %d", e, p->events, p->id) == 0)
            p->logged++;
        usleep(500);
    }
    printf("  [producer %d] finished, thread exiting\n", p->id);
    fflush(stdout);
    return NULL;
}

int logger_demo(int producers, int events_per_producer, int capacity, const char *path)
{
    if (producers < 1 || producers > MAX_PRODUCERS) {
        fprintf(stderr, "error: producers must be 1..%d (got %d)\n", MAX_PRODUCERS, producers);
        return -1;
    }
    if (events_per_producer < 1 || events_per_producer > 1000) {
        fprintf(stderr, "error: events per producer must be 1..1000 (got %d)\n", events_per_producer);
        return -1;
    }
    if (capacity < 1 || capacity > LOG_MAX_CAP) {
        fprintf(stderr, "error: queue capacity must be 1..%d (got %d)\n", LOG_MAX_CAP, capacity);
        return -1;
    }

    /* main.c starts a session logger for most commands; the demo needs its own settings. */
    int restart_session = logger_is_running();
    if (restart_session)
        logger_stop();

    printf("\n=== Producer-consumer logger (POSIX threads) ===\n");
    printf("Producers: %d   Events each: %d   Queue capacity: %d   Log file: %s\n",
           producers, events_per_producer, capacity, path);
    printf("Synchronization: 1 mutex + condition variables 'not_full' and 'not_empty'\n");
    printf("The consumer is slowed down (3 ms per entry) so producers hit the 'queue full' wait.\n\n");

    if (logger_start(path, capacity, 1, 3000) != 0)
        return -1;
    printf("  [main] consumer thread started\n");

    pthread_t tids[MAX_PRODUCERS];
    producer_arg args[MAX_PRODUCERS];
    int started = 0;
    for (int i = 0; i < producers; i++) {
        args[i] = (producer_arg){ .id = i + 1, .events = events_per_producer, .logged = 0 };
        int rc = pthread_create(&tids[i], NULL, producer_thread, &args[i]);
        if (rc != 0) {
            fprintf(stderr, "error: pthread_create(producer %d): %s\n", i + 1, strerror(rc));
            break;
        }
        started++;
    }

    for (int i = 0; i < started; i++) {
        pthread_join(tids[i], NULL);
        printf("  [main] joined producer %d (%d events logged)\n", i + 1, args[i].logged);
    }

    int written = logger_stop();
    printf("  [main] shutdown signalled; consumer drained the queue and was joined\n");

    LoggerStats st;
    logger_get_stats(&st);
    long expected = (long)producers * events_per_producer;
    printf("\nLogger statistics:\n");
    printf("  entries produced             = %ld (expected %ld)\n", st.produced, expected);
    printf("  entries written by consumer  = %d\n", written);
    printf("  producer waits (queue full)  = %ld\n", st.full_waits);
    printf("  consumer waits (queue empty) = %ld\n", st.empty_waits);
    printf("  max queue occupancy          = %d / %d\n", st.max_occupancy, capacity);
    printf("  threads joined               = %d producers + 1 consumer (none left running)\n", started);

    int ok = (started == producers) && st.produced == expected && written == expected;
    printf("RESULT: %s\n", ok ? "PASS - every entry written exactly once, clean shutdown"
                              : "FAIL - produced and written counts differ");

    if (restart_session)
        logger_start(DEFAULT_LOG_FILE, 16, 0, 0);
    return ok ? 0 : -1;
}
