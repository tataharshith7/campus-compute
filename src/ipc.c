/*
 * ipc.c - FR4: POSIX shared memory + named semaphores between two processes.
 *
 * Based on the Unit 2 notes, "Bounded buffer with semaphores":
 *     Initialize: mutex = 1, full = 0, empty = buffer capacity
 *     Producer: wait(empty); wait(mutex); insert; signal(mutex); signal(full);
 *     Consumer: wait(full);  wait(mutex); remove; signal(mutex); signal(empty);
 * and "Bounded buffer with a circular array" (buffer[], in, out, % size).
 * The circular buffer lives in a POSIX shared memory object instead of a
 * global array, so two separate processes (parent and fork()ed child) see it.
 * wait()/signal() are sem_timedwait()/sem_post() on named semaphores.
 *
 * Process creation follows Unit 1 Lab 1 (fork, pid > 0 parent / pid == 0 child,
 * then waitpid in the parent).
 *
 * Only the four names in ipc.h are ever created or removed.
 */
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "ipc.h"
#include "logger.h"

#define BUFFER_SIZE   IPC_SLOTS
#define WAIT_SECONDS  5     /* longest wait for the other process before giving up */

/* One item in the buffer: the part of a job the consumer needs. */
typedef struct {
    int job_id;
    int burst;
    int priority;
} item;

/* Layout of the shared memory object. */
typedef struct {
    item buffer[BUFFER_SIZE];
    int in;                  /* next free slot     (producer) */
    int out;                 /* next filled slot   (consumer) */
    /* Result written back by the consumer for the parent to check. */
    int items_received;
    long burst_total;
    int consumer_pid;
    int consumer_done;
} shared_data;

/* Current value of a semaphore, for printing (-1 if it cannot be read). */
static int sem_value(sem_t *s)
{
    int v = -1;
    if (sem_getvalue(s, &v) != 0)
        return -1;
    return v;
}

/* wait(S) with a timeout, so a dead partner process cannot hang us forever. */
static int sem_wait_timeout(sem_t *s)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += WAIT_SECONDS;
    while (sem_timedwait(s, &ts) != 0) {
        if (errno == EINTR)
            continue;
        return -1;      /* ETIMEDOUT or a real error; errno is set */
    }
    return 0;
}

/* Create a new shm object. A stale one from an interrupted run is replaced. */
static int create_shm(void)
{
    int fd = shm_open(IPC_SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0 && errno == EEXIST) {
        printf("  note: stale %s from an earlier run found, replacing it\n", IPC_SHM_NAME);
        shm_unlink(IPC_SHM_NAME);
        fd = shm_open(IPC_SHM_NAME, O_CREAT | O_EXCL | O_RDWR, 0600);
    }
    if (fd < 0)
        fprintf(stderr, "error: shm_open(%s): %s\n", IPC_SHM_NAME, strerror(errno));
    return fd;
}

/* Create a new named semaphore with an initial value; replaces a stale one. */
static sem_t *create_sem(const char *name, unsigned int value)
{
    sem_t *s = sem_open(name, O_CREAT | O_EXCL, 0600, value);
    if (s == SEM_FAILED && errno == EEXIST) {
        printf("  note: stale semaphore %s found, replacing it\n", name);
        sem_unlink(name);
        s = sem_open(name, O_CREAT | O_EXCL, 0600, value);
    }
    if (s == SEM_FAILED)
        fprintf(stderr, "error: sem_open(%s): %s\n", name, strerror(errno));
    return s;
}

static void close_sem(sem_t *s)
{
    if (s != NULL && s != SEM_FAILED)
        sem_close(s);
}

/*
 * Child process (consumer). It opens every object BY NAME, exactly as an
 * unrelated process would, instead of relying on what fork() copied.
 */
static int consumer_process(int nitems)
{
    int fd = shm_open(IPC_SHM_NAME, O_RDWR, 0);
    if (fd < 0) {
        perror("  [consumer] shm_open");
        return 1;
    }
    shared_data *shm = mmap(NULL, sizeof *shm, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (shm == MAP_FAILED) {
        perror("  [consumer] mmap");
        return 1;
    }
    sem_t *empty = sem_open(IPC_SEM_EMPTY_NAME, 0);
    sem_t *full = sem_open(IPC_SEM_FULL_NAME, 0);
    sem_t *mutex = sem_open(IPC_SEM_MUTEX_NAME, 0);
    if (empty == SEM_FAILED || full == SEM_FAILED || mutex == SEM_FAILED) {
        perror("  [consumer] sem_open");
        close_sem(empty);
        close_sem(full);
        close_sem(mutex);
        munmap(shm, sizeof *shm);
        return 1;
    }

    printf("  [consumer] This is child. Process Id = %d, Parent Process Id = %d\n",
           (int)getpid(), (int)getppid());
    printf("  [consumer] opened %s and 3 semaphores by name, waiting for jobs\n", IPC_SHM_NAME);
    fflush(stdout);

    int rc = 0;
    for (int i = 0; i < nitems; i++) {
        if (sem_wait_timeout(full) != 0) {          /* wait(full): an item is available */
            perror("  [consumer] wait(full)");
            rc = 1;
            break;
        }
        if (sem_wait_timeout(mutex) != 0) {         /* wait(mutex): exclusive access */
            perror("  [consumer] wait(mutex)");
            sem_post(full);
            rc = 1;
            break;
        }
        int slot = shm->out;
        item next_consumed = shm->buffer[slot];
        shm->out = (shm->out + 1) % BUFFER_SIZE;
        shm->items_received++;
        shm->burst_total += next_consumed.burst;
        sem_post(mutex);                            /* signal(mutex) */
        sem_post(empty);                            /* signal(empty): slot is free */

        printf("  [consumer pid %d] slot %d -> received J%d (burst %d, priority %d)   "
               "empty=%d full=%d\n",
               (int)getpid(), slot, next_consumed.job_id, next_consumed.burst,
               next_consumed.priority, sem_value(empty), sem_value(full));
        fflush(stdout);
        usleep(20000);   /* slow consumer, so the producer has to wait on 'empty' */
    }

    /* Write the result back into shared memory for the parent. */
    if (sem_wait_timeout(mutex) == 0) {
        shm->consumer_pid = (int)getpid();
        shm->consumer_done = (rc == 0);
        sem_post(mutex);
    } else {
        rc = 1;
    }

    sem_close(empty);
    sem_close(full);
    sem_close(mutex);
    munmap(shm, sizeof *shm);
    return rc;
}

/* Close and unlink everything this run created. Safe with partial state. */
static void release_all(shared_data *shm, sem_t *empty, sem_t *full, sem_t *mutex)
{
    if (shm != NULL && shm != MAP_FAILED)
        munmap(shm, sizeof *shm);
    close_sem(empty);
    close_sem(full);
    close_sem(mutex);
    shm_unlink(IPC_SHM_NAME);
    sem_unlink(IPC_SEM_EMPTY_NAME);
    sem_unlink(IPC_SEM_FULL_NAME);
    sem_unlink(IPC_SEM_MUTEX_NAME);
}

int ipc_demo(const JobSet *set)
{
    if (set == NULL || set->count < 1) {
        fprintf(stderr, "error: ipc needs at least one job to send\n");
        return -1;
    }

    printf("\n=== Shared-memory IPC (POSIX shm + named semaphores) ===\n");
    printf("Shared memory : %s (circular buffer of %d job slots + result fields)\n",
           IPC_SHM_NAME, BUFFER_SIZE);
    printf("Semaphores    : %s = %d, %s = 0, %s = 1\n",
           IPC_SEM_EMPTY_NAME, BUFFER_SIZE, IPC_SEM_FULL_NAME, IPC_SEM_MUTEX_NAME);
    printf("Producer = parent process, consumer = fork()ed child process\n\n");

    shared_data *shm = NULL;
    sem_t *empty = NULL, *full = NULL, *mutex = NULL;

    int fd = create_shm();
    if (fd < 0)
        return -1;
    if (ftruncate(fd, sizeof(shared_data)) != 0) {
        perror("error: ftruncate");
        close(fd);
        release_all(NULL, NULL, NULL, NULL);
        return -1;
    }
    shm = mmap(NULL, sizeof *shm, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);      /* the mapping stays valid after the descriptor is closed */
    if (shm == MAP_FAILED) {
        perror("error: mmap");
        release_all(NULL, NULL, NULL, NULL);
        return -1;
    }
    memset(shm, 0, sizeof *shm);

    empty = create_sem(IPC_SEM_EMPTY_NAME, BUFFER_SIZE);
    full = create_sem(IPC_SEM_FULL_NAME, 0);
    mutex = create_sem(IPC_SEM_MUTEX_NAME, 1);
    if (empty == SEM_FAILED || full == SEM_FAILED || mutex == SEM_FAILED) {
        release_all(shm, empty, full, mutex);
        return -1;
    }
    printf("  [producer] This is parent. Process Id = %d: created shared memory and 3 semaphores\n",
           (int)getpid());
    logger_log("ipc", "created %s and 3 named semaphores", IPC_SHM_NAME);

    fflush(stdout);     /* so the child does not inherit and repeat buffered output */
    pid_t pid = fork();

    if (pid == 0) // child process
    {
        int crc = consumer_process(set->count);
        fflush(stdout);
        _exit(crc);
    }
    else if (pid < 0) //fork failed
    {
        perror("error: fork");
        release_all(shm, empty, full, mutex);
        return -1;
    }

    // parent process: producer
    int rc = 0;
    long expected_total = 0;
    for (int i = 0; i < set->count; i++) {
        item next_produced = { set->jobs[i].id, set->jobs[i].burst, set->jobs[i].priority };

        /* sem_trywait only to SHOW when the buffer is full; the real wait is below. */
        if (sem_trywait(empty) == 0) {
            sem_post(empty);
        } else if (errno == EAGAIN) {
            printf("  [producer pid %d] buffer full (empty=0): producer blocks in wait(empty)\n",
                   (int)getpid());
            fflush(stdout);
        }
        if (sem_wait_timeout(empty) != 0) {         /* wait(empty): a free slot */
            perror("error: producer wait(empty)");
            rc = -1;
            break;
        }
        if (sem_wait_timeout(mutex) != 0) {         /* wait(mutex) */
            perror("error: producer wait(mutex)");
            sem_post(empty);
            rc = -1;
            break;
        }
        int slot = shm->in;
        shm->buffer[slot] = next_produced;
        shm->in = (shm->in + 1) % BUFFER_SIZE;
        sem_post(mutex);                            /* signal(mutex) */
        sem_post(full);                             /* signal(full): item ready */

        expected_total += next_produced.burst;
        printf("  [producer pid %d] slot %d <- sent J%d (burst %d, priority %d)       "
               "empty=%d full=%d\n",
               (int)getpid(), slot, next_produced.job_id, next_produced.burst,
               next_produced.priority, sem_value(empty), sem_value(full));
        fflush(stdout);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        perror("error: waitpid");
        rc = -1;
    } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        printf("\n  [producer] consumer pid %d exited normally (status 0)\n", (int)pid);
    } else {
        fprintf(stderr, "error: consumer process pid %d did not finish cleanly\n", (int)pid);
        rc = -1;
    }

    printf("  [producer] result the consumer wrote back into shared memory:\n");
    printf("    items received = %d (sent %d)\n", shm->items_received, set->count);
    printf("    burst total    = %ld (expected %ld)\n", shm->burst_total, expected_total);
    int match = shm->consumer_done && shm->items_received == set->count &&
                shm->burst_total == expected_total;
    printf("    verification   = %s\n", match ? "MATCH" : "MISMATCH");
    if (!match)
        rc = -1;
    logger_log("ipc", "exchanged %d jobs through shared memory: %s",
               set->count, match ? "verified" : "MISMATCH");

    release_all(shm, empty, full, mutex);
    printf("\n  cleanup: munmap + sem_close done; shm_unlink(%s),\n"
           "           sem_unlink(%s), sem_unlink(%s), sem_unlink(%s)\n",
           IPC_SHM_NAME, IPC_SEM_EMPTY_NAME, IPC_SEM_FULL_NAME, IPC_SEM_MUTEX_NAME);
    logger_log("ipc", "IPC objects unlinked");
    printf("\n");

    if (ipc_status() != 0)
        rc = -1;
    printf("RESULT: %s\n", rc == 0 ? "PASS - data exchanged and all IPC objects released"
                                   : "FAIL - see errors above");
    return rc;
}

/* On Linux, shm objects appear as /dev/shm/<name> and named semaphores as /dev/shm/sem.<name>. */
static int object_exists(const char *name, int is_sem)
{
    char path[128];
    snprintf(path, sizeof path, "/dev/shm/%s%s", is_sem ? "sem." : "", name + 1);
    struct stat sb;
    return stat(path, &sb) == 0;
}

static const struct {
    const char *name;
    int is_sem;
} our_objects[] = {
    { IPC_SHM_NAME, 0 },
    { IPC_SEM_EMPTY_NAME, 1 },
    { IPC_SEM_FULL_NAME, 1 },
    { IPC_SEM_MUTEX_NAME, 1 },
};
#define NUM_OBJECTS ((int)(sizeof our_objects / sizeof our_objects[0]))

int ipc_status(void)
{
    int present = 0;
    printf("IPC objects created by CampusCompute:\n");
    for (int i = 0; i < NUM_OBJECTS; i++) {
        int e = object_exists(our_objects[i].name, our_objects[i].is_sem);
        present += e;
        printf("  %-10s %-22s %s\n", our_objects[i].is_sem ? "semaphore" : "shm",
               our_objects[i].name, e ? "PRESENT" : "not present (released)");
    }
    return present;
}

int ipc_cleanup(void)
{
    int failed = 0;
    printf("Removing only CampusCompute's own IPC objects:\n");
    for (int i = 0; i < NUM_OBJECTS; i++) {
        const char *name = our_objects[i].name;
        int r = our_objects[i].is_sem ? sem_unlink(name) : shm_unlink(name);
        const char *what;
        if (r == 0) {
            what = "removed";
        } else if (errno == ENOENT) {
            what = "not present";
        } else {
            what = strerror(errno);
            failed = 1;
        }
        printf("  %-10s %-22s %s\n", our_objects[i].is_sem ? "semaphore" : "shm", name, what);
    }
    logger_log("cleanup", "ipc cleanup run");
    return failed ? -1 : 0;
}
