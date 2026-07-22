#include "thread.h"
#include <windows.h>
#include <stdlib.h>

// Fully define the opaque thread handle for Windows
struct espra_thread
{
    HANDLE h_thread;
};

// Fully define the opaque mutex handle for Windows
struct espra_mutex
{
    CRITICAL_SECTION cs;
};

// Private proxy context to carry arguments across the Win32 API boundary
typedef struct
{
    void *(*start_routine)(void *);
    void *arg;
} win32_trampoline_ctx_t;

// The static trampoline function matching exactly what the Windows kernel expects
static DWORD WINAPI win32_thread_trampoline(LPVOID lpParam)
{
    win32_trampoline_ctx_t *ctx = (win32_trampoline_ctx_t *)lpParam;
    if (!ctx)
        return 1;

    // Unpack and execute your clean, platform-agnostic code loop
    ctx->start_routine(ctx->arg);

    // Clean up the temporary wrapper allocation inside the new thread context
    free(ctx);
    return 0;
}

espra_thread_t *espra_thread_create(void *(*start_routine)(void *), void *arg)
{
    espra_thread_t *t = malloc(sizeof(espra_thread_t));
    win32_trampoline_ctx_t *ctx = malloc(sizeof(win32_trampoline_ctx_t));

    if (!t || !ctx)
    {
        free(t);
        free(ctx);
        return NULL;
    }

    ctx->start_routine = start_routine;
    ctx->arg = arg;

    // Spin up the native Win32 kernel thread routing through our proxy function
    t->h_thread = CreateThread(NULL, 0, win32_thread_trampoline, ctx, 0, NULL);
    if (t->h_thread == NULL)
    {
        free(ctx);
        free(t);
        return NULL;
    }
    return t;
}

int espra_thread_detach(espra_thread_t *thread)
{
    if (!thread || thread->h_thread == NULL)
        return -1;

    // Closing the handle detaches it, telling the OS to auto-reclaim resources upon completion
    if (CloseHandle(thread->h_thread))
    {
        thread->h_thread = NULL;
        return 0;
    }
    return -1;
}

void espra_thread_free(espra_thread_t *thread)
{
    if (thread)
    {
        // Safe guard: if they free without detaching, make sure we don't leak the handle
        if (thread->h_thread != NULL)
        {
            CloseHandle(thread->h_thread);
        }
        free(thread);
    }
}

// --- Mutex Guard Management ---

espra_mutex_t *espra_mutex_create(void)
{
    espra_mutex_t *m = malloc(sizeof(espra_mutex_t));
    if (!m)
        return NULL;

    InitializeCriticalSection(&m->cs);
    return m;
}

void espra_mutex_lock(espra_mutex_t *mutex)
{
    if (mutex)
    {
        EnterCriticalSection(&mutex->cs);
    }
}

void espra_mutex_unlock(espra_mutex_t *mutex)
{
    if (mutex)
    {
        LeaveCriticalSection(&mutex->cs);
    }
}

void espra_mutex_destroy(espra_mutex_t *mutex)
{
    if (mutex)
    {
        DeleteCriticalSection(&mutex->cs);
        free(mutex);
    }
}