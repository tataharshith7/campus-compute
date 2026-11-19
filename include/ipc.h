/*
 * ipc.h - FR4: POSIX shared memory + named semaphores between two processes.
 */
#ifndef IPC_H
#define IPC_H

#include "jobs.h"

#define IPC_SHM_NAME        "/campuscompute_shm"
#define IPC_SEM_EMPTY_NAME  "/campuscompute_empty"
#define IPC_SEM_FULL_NAME   "/campuscompute_full"
#define IPC_SEM_MUTEX_NAME  "/campuscompute_mutex"
#define IPC_SLOTS           4

/*
 * Parent (producer) sends each job through a shared-memory ring buffer to a
 * forked child (consumer); the child writes a result back. Returns 0 on success.
 */
int ipc_demo(const JobSet *set);

/* Report which of this program's IPC objects exist. Returns how many exist. */
int ipc_status(void);

/* Remove this program's IPC objects left by an interrupted run. */
int ipc_cleanup(void);

#endif
