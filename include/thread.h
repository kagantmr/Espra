/**
 * OS-neutral threading/synchronization implementation.
 * Implement this file to port into another operating system.
 */

#pragma once

// Forward declarations of our opaque types
typedef struct espra_thread espra_thread_t;
typedef struct espra_mutex espra_mutex_t;

// Spawns a new background thread running the start_routine function callback.
espra_thread_t *espra_thread_create(void *(*start_routine)(void *), void *arg);

// Detaches a thread so its OS resources are auto-reclaimed on termination.
int espra_thread_detach(espra_thread_t *thread);

// Frees the memory wrapper handle allocation itself.
void espra_thread_free(espra_thread_t *thread);

// Allocates and initializes a new mutual exclusion lock.
espra_mutex_t *espra_mutex_create(void);

// Locks the mutex gate (blocks if another thread holds it).
void espra_mutex_lock(espra_mutex_t *mutex);

// Unlocks the mutex gate.
void espra_mutex_unlock(espra_mutex_t *mutex);

// Tears down the internal lock and frees the allocated memory wrapper handle.
void espra_mutex_destroy(espra_mutex_t *mutex);