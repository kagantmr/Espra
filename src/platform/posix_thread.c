#include "thread.h"
#include <pthread.h>
#include <stdlib.h>

// Fully define the opaque thread handle for POSIX
struct espra_thread {
    pthread_t sys_thread;
};

// Fully define the opaque mutex handle for POSIX
struct espra_mutex {
    pthread_mutex_t sys_mutex;
};

espra_thread_t* espra_thread_create(void* (*start_routine)(void*), void* arg) {
    espra_thread_t* t = malloc(sizeof(espra_thread_t));
    if (!t) return NULL;

    if (pthread_create(&t->sys_thread, NULL, start_routine, arg) != 0) {
        free(t);
        return NULL;
    }
    return t;
}

int espra_thread_detach(espra_thread_t* thread) {
    if (!thread) return -1;
    return pthread_detach(thread->sys_thread);
}

void espra_thread_free(espra_thread_t* thread) {
    free(thread);
}

espra_mutex_t* espra_mutex_create(void) {
    espra_mutex_t* m = malloc(sizeof(espra_mutex_t));
    if (!m) return NULL;

    if (pthread_mutex_init(&m->sys_mutex, NULL) != 0) {
        free(m);
        return NULL;
    }
    return m;
}

void espra_mutex_lock(espra_mutex_t* mutex) {
    if (mutex) {
        pthread_mutex_lock(&mutex->sys_mutex);
    }
}

void espra_mutex_unlock(espra_mutex_t* mutex) {
    if (mutex) {
        pthread_mutex_unlock(&mutex->sys_mutex);
    }
}

void espra_mutex_destroy(espra_mutex_t* mutex) {
    if (mutex) {
        pthread_mutex_destroy(&mutex->sys_mutex);
        free(mutex);
    }
}