#ifndef LIBTUDOR_WINAPI_INTERNAL_H
#define LIBTUDOR_WINAPI_INTERNAL_H

#include "api.h"
#include <sys/syscall.h> // For syscall()
#include <unistd.h>      // For syscall()

// --- Threading ---
void win_init_tib();
DWORD win_get_thread_id();

// --- Synchronization ---

// Forward declare the struct
struct win_sync_object;

// Define the callback function type
typedef DWORD win_sync_obj_wait_fnc(struct win_sync_object *sync_obj, DWORD timeout);

// FULLY DEFINE the struct here so it's not incomplete
struct win_sync_object {
    win_sync_obj_wait_fnc *wait_fnc;
};

DWORD win_wait_sync_obj(HANDLE handle, DWORD timeout);

// Event functions
HANDLE win_create_event(const char *name, bool initial_state, bool manual_reset);
void win_set_event(HANDLE evt);
void win_reset_event(HANDLE evt);

#endif