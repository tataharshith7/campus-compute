/*
 * logger.h - FR3: concurrent activity logger.
 *
 * Producers (any thread) call logger_log(), which places a formatted line in
 * a bounded queue. A single consumer thread drains the queue to the log file.
 * A mutex protects the queue; condition variables signal "not full" and
 * "not empty".
 */
#ifndef LOGGER_H
#define LOGGER_H

#define LOG_MSG_LEN   256
#define LOG_MAX_CAP   64

typedef struct {
    long produced;        /* entries placed in the queue                */
    long consumed;        /* entries written to the file                */
    long full_waits;      /* times a producer blocked on a full queue   */
    long empty_waits;     /* times the consumer blocked on empty queue  */
    int  max_occupancy;   /* highest number of queued entries observed  */
} LoggerStats;

/*
 * Start the consumer thread. capacity: queue size (1..LOG_MAX_CAP).
 * echo: also print each written entry to stdout. consumer_delay_us: artificial
 * delay per entry so the "queue full" path can be observed in demos.
 * Returns 0 on success.
 */
int logger_start(const char *path, int capacity, int echo, int consumer_delay_us);

/*
 * Queue one log entry. Safe to call from many threads. Does nothing and
 * returns -1 if the logger is not running or if called in a forked child.
 */
int logger_log(const char *source, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

/* Signal shutdown, let the consumer drain the queue, join it, close the file. */
int logger_stop(void);

int logger_is_running(void);
void logger_get_stats(LoggerStats *out);

/* Producer-consumer demonstration. Returns 0 if every entry was written. */
int logger_demo(int producers, int events_per_producer, int capacity, const char *path);

#endif
