#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>
#include <errno.h>
#include <time.h> // Added for clock_gettime
#include "internal.h"

// Helper to validate handles before use
static bool is_valid_handle(HANDLE h) {
    return h && h != INVALID_HANDLE_VALUE;
}

DWORD win_wait_sync_obj(HANDLE handle, DWORD timeout) {
    if (!is_valid_handle(handle)) return WAIT_FAILED;
    struct win_sync_object *obj = (struct win_sync_object*) handle->data;
    return obj->wait_fnc(obj, timeout);
}

typedef struct {
    void *DebugInfo;
    LONG LockCount;
    LONG RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    DWORD SpinCount;
} CRITICAL_SECTION;

__winfnc BOOL InitializeCriticalSectionEx(CRITICAL_SECTION *sect, DWORD spinCount, DWORD flags) {
    memset(sect, 0, sizeof(CRITICAL_SECTION));

    sect->LockCount = 0;
    sect->RecursionCount = 0;
    sect->OwningThread = NULL;
    sect->LockSemaphore = malloc(sizeof(pthread_mutex_t));
    if(!sect->LockSemaphore) { winerr_set_errno(); return FALSE; }
    sect->SpinCount = spinCount;

    pthread_mutexattr_t attr;
    cant_fail_ret(pthread_mutexattr_init(&attr));
    cant_fail_ret(pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE));
    if(pthread_mutex_init((pthread_mutex_t*) sect->LockSemaphore, &attr) != 0) {
        cant_fail_ret(pthread_mutexattr_destroy(&attr));
        winerr_set_errno();
        return FALSE;
    }
    cant_fail_ret(pthread_mutexattr_destroy(&attr));

    return TRUE;
}
WINAPI(InitializeCriticalSectionEx)

__winfnc void InitializeCriticalSection(CRITICAL_SECTION *sect) {
    if(!InitializeCriticalSectionEx(sect, 0, 0)) {
        log_error("InitializeCriticalSection failed");
        abort();
    }
}
WINAPI(InitializeCriticalSection)

__winfnc void DeleteCriticalSection(CRITICAL_SECTION* sect) {
    cant_fail_ret(pthread_mutex_destroy((pthread_mutex_t*) sect->LockSemaphore));
    free(sect->LockSemaphore);
}
WINAPI(DeleteCriticalSection)

__winfnc void EnterCriticalSection(CRITICAL_SECTION *sect) {
    cant_fail_ret(pthread_mutex_lock((pthread_mutex_t*) sect->LockSemaphore));
    if(sect->RecursionCount++ == 0) {
        sect->LockCount = 1;
        sect->OwningThread = (HANDLE) pthread_self();
    }
}
WINAPI(EnterCriticalSection)

__winfnc void LeaveCriticalSection(CRITICAL_SECTION *sect) {
    if(--sect->RecursionCount == 0) {
        sect->LockCount = 0;
        sect->OwningThread = NULL;
    }
    cant_fail_ret(pthread_mutex_unlock((pthread_mutex_t*) sect->LockSemaphore));
}
WINAPI(LeaveCriticalSection)

static pthread_rwlock_t events_lock = PTHREAD_RWLOCK_INITIALIZER;
static struct sync_event {
    struct win_sync_object sync_obj;
    struct sync_event *prev, *next;
    HANDLE handle;

    const char *name;
    bool manual_reset;

    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool state;
} *events_head;

static void evt_destr(struct sync_event *evt) {
    cant_fail_ret(pthread_rwlock_wrlock(&events_lock));
    if(evt->prev) evt->prev->next = evt->next;
    else events_head = evt->next;
    if(evt->next) evt->next->prev = evt->prev;
    cant_fail_ret(pthread_rwlock_unlock(&events_lock));

    cant_fail_ret(pthread_cond_destroy(&evt->cond));
    cant_fail_ret(pthread_mutex_destroy(&evt->lock));
    free((void*) evt->name);
    free(evt);
}

static DWORD evt_wait(struct sync_event *evt, DWORD timeout) {
    DWORD res = 0;
    cant_fail_ret(pthread_mutex_lock(&evt->lock));

    while(!evt->state) {
        if(timeout != INFINITE) {
            struct timespec time;
            clock_gettime(CLOCK_REALTIME, &time); // Fixed to use current time + timeout
            time.tv_sec += timeout / 1000;
            time.tv_nsec += (timeout % 1000) * 1000000L;
            if (time.tv_nsec >= 1000000000L) {
                time.tv_sec++;
                time.tv_nsec -= 1000000000L;
            }

            int err = pthread_cond_timedwait(&evt->cond, &evt->lock, &time);
            if(err == ETIMEDOUT) {
                res = WAIT_TIMEOUT;
                break;
            }
            cant_fail(err);
        } else cant_fail_ret(pthread_cond_wait(&evt->cond, &evt->lock));
    }

    if(res == 0 && !evt->manual_reset) evt->state = false;

    cant_fail_ret(pthread_mutex_unlock(&evt->lock));
    return res;
}

HANDLE win_create_event(const char *name, bool initial_state, bool manual_reset) {
    struct sync_event *evt = (struct sync_event*) malloc(sizeof(struct sync_event));
    if(!evt) { winerr_set_errno(); return NULL; }
    evt->sync_obj.wait_fnc = (win_sync_obj_wait_fnc*) evt_wait;
    evt->handle = winhandle_create(evt, (winhandle_destr_fnc*) evt_destr);

    evt->name = name ? strdup(name) : NULL;
    evt->state = initial_state;
    evt->manual_reset = manual_reset;
    cant_fail_ret(pthread_mutex_init(&evt->lock, NULL));
    cant_fail_ret(pthread_cond_init(&evt->cond, NULL));

    cant_fail_ret(pthread_rwlock_wrlock(&events_lock));
    evt->prev = NULL;
    evt->next = events_head;
    if(events_head) events_head->prev = evt;
    events_head = evt;
    cant_fail_ret(pthread_rwlock_unlock(&events_lock));

    return evt->handle;
}

void win_set_event(HANDLE handle) {
    if (!is_valid_handle(handle)) return; // Safety check
    struct sync_event *evt = (struct sync_event*) handle->data;

    cant_fail_ret(pthread_mutex_lock(&evt->lock));
    evt->state = true;
    cant_fail_ret(pthread_cond_broadcast(&evt->cond));
    cant_fail_ret(pthread_mutex_unlock(&evt->lock));
}

void win_reset_event(HANDLE handle) {
    if (!is_valid_handle(handle)) return; // Safety check
    struct sync_event *evt = (struct sync_event*) handle->data;

    cant_fail_ret(pthread_mutex_lock(&evt->lock));
    evt->state = false;
    cant_fail_ret(pthread_cond_broadcast(&evt->cond));
    cant_fail_ret(pthread_mutex_unlock(&evt->lock));
}

__winfnc HANDLE CreateEventA(void *attrs, BOOL manual_reset, BOOL initial_state, const char *name) {
    return win_create_event(name, initial_state, manual_reset);
}
WINAPI(CreateEventA);

__winfnc HANDLE CreateEventW(void *attrs, BOOL manual_reset, BOOL initial_state, const char16_t *name) {
    char *cname = name ? winstr_to_str(name) : NULL;
    HANDLE evt = CreateEventA(attrs, manual_reset, initial_state, cname);
    free(cname);
    return evt;
}
WINAPI(CreateEventW);

__winfnc BOOL SetEvent(HANDLE handle) {
    if (!is_valid_handle(handle)) return FALSE;
    win_set_event(handle);
    return TRUE;
}
WINAPI(SetEvent)

__winfnc BOOL ResetEvent(HANDLE handle) {
    if (!is_valid_handle(handle)) return FALSE;
    win_reset_event(handle);
    return TRUE;
}
WINAPI(ResetEvent)

__winfnc DWORD WaitForSingleObject(HANDLE handle, DWORD timeout) {
    if (!is_valid_handle(handle)) return WAIT_FAILED;
    return win_wait_sync_obj(handle, timeout);
}
WINAPI(WaitForSingleObject)

// --- FLS / TLS ---
#define NUM_FLS_IDXS 128
#define FLS_OUT_OF_INDEXES 0xffffffff

typedef __winfnc void PflsCallbackFunction(void *flsData);

static pthread_rwlock_t fls_lock = PTHREAD_RWLOCK_INITIALIZER;
static int fls_next_free;
static struct {
    int next_idx;
    pthread_key_t key;
    PflsCallbackFunction *callback;
} fls_indices[NUM_FLS_IDXS];

struct fls_value {
    PflsCallbackFunction *callback;
    void *data;
};

__constr static void init_fls() {
    fls_next_free = NUM_FLS_IDXS;
    for(int i = NUM_FLS_IDXS-1; i >= 0; i--) {
        fls_indices[i].next_idx = fls_next_free;
        fls_next_free = i;
    }
}

static void fls_destructor(void *ptr) {
    struct fls_value *val = (struct fls_value*) ptr;
    if(val->callback) val->callback(val->data);
    free(val);
}

__winfnc DWORD FlsAlloc(PflsCallbackFunction *callback) {
    cant_fail_ret(pthread_rwlock_wrlock(&fls_lock));
    DWORD idx = FLS_OUT_OF_INDEXES;
    if(fls_next_free < NUM_FLS_IDXS) {
        idx = fls_next_free;
        fls_next_free = fls_indices[idx].next_idx;
        fls_indices[idx].next_idx = -1;
        cant_fail_ret(pthread_key_create(&fls_indices[idx].key, fls_destructor));
        fls_indices[idx].callback = callback;
    } else log_warn("Ran out of FLS indices!");
    cant_fail_ret(pthread_rwlock_unlock(&fls_lock));
    return idx;
}
WINAPI(FlsAlloc);

__winfnc BOOL FlsFree(DWORD idx) {
    cant_fail_ret(pthread_rwlock_wrlock(&fls_lock));
    BOOL suc;
    if(0 <= idx && idx < NUM_FLS_IDXS && fls_indices[idx].next_idx < 0) {
        cant_fail_ret(pthread_key_delete(fls_indices[idx].key));
        fls_indices[idx].next_idx = fls_next_free;
        fls_next_free = idx;
        suc = TRUE;
    } else suc = FALSE;
    cant_fail_ret(pthread_rwlock_unlock(&fls_lock));
    if(!suc) winerr_set();
    return suc;
}
WINAPI(FlsFree);

__winfnc void *FlsGetValue(DWORD idx) {
    cant_fail_ret(pthread_rwlock_rdlock(&fls_lock));
    void *data = NULL;
    if(0 <= idx && idx < NUM_FLS_IDXS && fls_indices[idx].next_idx < 0) {
        struct fls_value *val = (struct fls_value*) pthread_getspecific(fls_indices[idx].key);
        if(val) data = val->data;
    } else winerr_set();
    cant_fail_ret(pthread_rwlock_unlock(&fls_lock));
    return data;
}
WINAPI(FlsGetValue);

__winfnc BOOL FlsSetValue(DWORD idx, void *data) {
    cant_fail_ret(pthread_rwlock_rdlock(&fls_lock));
    BOOL suc;
    if(0 <= idx && idx < NUM_FLS_IDXS && fls_indices[idx].next_idx < 0) {
        struct fls_value *val = (struct fls_value*) pthread_getspecific(fls_indices[idx].key);
        if (val) free(val); // Avoid double-free or leak, simplified
        
        // Re-allocate to ensure fresh state
        val = (struct fls_value*) malloc(sizeof(struct fls_value));
        if(!val) {
            winerr_set_errno();
            suc = FALSE;
        } else {
            val->callback = fls_indices[idx].callback;
            val->data = data;
            cant_fail_ret(pthread_setspecific(fls_indices[idx].key, val));
            suc = TRUE;
        }
    } else {
        winerr_set();
        suc = FALSE;
    }
    cant_fail_ret(pthread_rwlock_unlock(&fls_lock));
    return suc;
}
WINAPI(FlsSetValue);

__winfnc DWORD TlsAlloc() { return FlsAlloc(NULL); }
WINAPI(TlsAlloc)

__winfnc BOOL TlsFree(DWORD idx) { return FlsFree(idx); }
WINAPI(TlsFree)

__winfnc void *TlsGetValue(DWORD idx) { return FlsGetValue(idx); }
WINAPI(TlsGetValue)

__winfnc BOOL TlsSetValue(DWORD idx, void *data) { return FlsSetValue(idx, data); }
WINAPI(TlsSetValue)

__winfnc BOOL InitializeCriticalSectionAndSpinCount(void *lpCriticalSection, DWORD dwSpinCount) {
    InitializeCriticalSection(lpCriticalSection);
    return TRUE;
}
WINAPI(InitializeCriticalSectionAndSpinCount)

__winfnc DWORD WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles, BOOL bWaitAll, DWORD dwMilliseconds) {
    unsigned long long start_time;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    start_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    while (1) {
        int signaled_count = 0;
        for (DWORD i = 0; i < nCount; i++) {
            DWORD status = WaitForSingleObject(lpHandles[i], 0);
            if (status == 0) { // WAIT_OBJECT_0
                if (!bWaitAll) return 0 + i;
                signaled_count++;
            }
        }

        if (bWaitAll && signaled_count == nCount) return 0;

        clock_gettime(CLOCK_MONOTONIC, &ts);
        unsigned long long current_time = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        if (dwMilliseconds != 0xFFFFFFFF && (current_time - start_time) >= dwMilliseconds) {
            return 0x00000102L; // WAIT_TIMEOUT
        }

        struct timespec sleep_ts = {0, 10000000}; // 10ms
        nanosleep(&sleep_ts, NULL);
    }
}
WINAPI(WaitForMultipleObjects)