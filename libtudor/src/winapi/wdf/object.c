#include "internal.h"
#include <tudor/log.h>

// Fix 1: Define missing enum if not present
#ifndef WdfSynchronizationScopeInheritFromParent
#define WdfSynchronizationScopeInheritFromParent 0
#endif

// --- Helper Functions ---

void wdf_object_ref(struct wdf_object *obj) {
    if(obj) __sync_fetch_and_add(&obj->ref_count, 1);
}

void wdf_object_unref(struct wdf_object *obj) {
    if(obj) __sync_sub_and_fetch(&obj->ref_count, 1);
}

void wdf_create_obj(struct wdf_object *parent, struct wdf_object *obj, wdf_obj_destr_fnc *destr, WDF_OBJECT_ATTRIBUTES *attrs) {
    *obj = (struct wdf_object) {0};
    obj->destr = destr;
    
    // Initialize mutexes EXACTLY ONCE
    cant_fail_ret(pthread_mutex_init(&obj->contexts_lock, NULL));
    cant_fail_ret(pthread_mutex_init(&obj->evtqueue_lock, NULL));
    
    // KEEP THIS! Base WDF reference count.
    obj->ref_count = 1; 

    if(attrs) {
        obj->attrs = *attrs;
        
        // --- CONTEXT ALLOCATION PATCH v2 ---
        // WDF specifies size in either Override OR TypeInfo. We must check both.
        size_t ctx_size = attrs->ContextSizeOverride;
        if (ctx_size == 0 && attrs->ContextTypeInfo != NULL) {
            ctx_size = attrs->ContextTypeInfo->ContextSize;
        }

        if (ctx_size > 0 || attrs->ContextTypeInfo != NULL) {
            struct wdf_object_context *ctx = malloc(sizeof(struct wdf_object_context));
            // Allocate at least 8 bytes if size is missing but type is requested
            ctx->data = calloc(1, ctx_size > 0 ? ctx_size : 8); 
            ctx->type = attrs->ContextTypeInfo;
            ctx->attrs = *attrs;
            ctx->next = NULL;
            obj->context_head = ctx;
        }
        // ------------------------------------
    } else {
        obj->attrs.SynchronizationScope = WdfSynchronizationScopeInheritFromParent; 
    }

    //Add to parent list
    if(parent) {
        obj->parent_obj = parent;
        obj->parent_list = &parent->child_list;
        
        cant_fail_ret(pthread_rwlock_wrlock(&parent->child_list.lock));
        if(!parent->child_list.dead) {
            obj->prev = NULL;
            obj->next = parent->child_list.head;
            if(parent->child_list.head) parent->child_list.head->prev = obj;
            parent->child_list.head = obj;
        } else {
            log_warn("Tried to create child of dead parent object!");
        }
        cant_fail_ret(pthread_rwlock_unlock(&parent->child_list.lock));
    }

    wdf_init_obj_list(&obj->child_list);
}

void wdf_cleanup_obj(struct wdf_object *obj) {
    //Remove from parent list
    if(obj->parent_list) {
        // Fix 2: Use '->' because parent_list is a pointer
        cant_fail_ret(pthread_rwlock_wrlock(&obj->parent_list->lock));
        if(!obj->parent_list->dead) {
            if(obj->prev) obj->prev->next = obj->next;
            else obj->parent_list->head = obj->next;
            if(obj->next) obj->next->prev = obj->prev;
        }
        cant_fail_ret(pthread_rwlock_unlock(&obj->parent_list->lock));
    }

    //Destroy children
    wdf_destroy_obj_list(&obj->child_list);

    //Destroy contexts
    cant_fail_ret(pthread_mutex_lock(&obj->contexts_lock));
    while(obj->context_head) {
        struct wdf_object_context *ctx = obj->context_head;
        obj->context_head = ctx->next;
        if(ctx->attrs.EvtCleanupCallback) ctx->attrs.EvtCleanupCallback(obj);
        if(ctx->attrs.EvtDestroyCallback) ctx->attrs.EvtDestroyCallback(obj);
        free(ctx->data);
        free(ctx);
    }
    cant_fail_ret(pthread_mutex_unlock(&obj->contexts_lock));
    cant_fail_ret(pthread_mutex_destroy(&obj->contexts_lock));

    //Destroy event queue
    wdf_evtqueue_clear_obj(obj);
    cant_fail_ret(pthread_mutex_destroy(&obj->evtqueue_lock));
}

void wdf_init_obj_list(struct wdf_object_list *list) {
    list->dead = false;
    list->head = NULL;
    cant_fail_ret(pthread_rwlock_init(&list->lock, NULL));
}

void wdf_destroy_obj_list(struct wdf_object_list *list) {
    cant_fail_ret(pthread_rwlock_wrlock(&list->lock));
    list->dead = true;
    while(list->head) {
        struct wdf_object *obj = list->head;
        list->head = obj->next;
        
        cant_fail_ret(pthread_rwlock_unlock(&list->lock));
        if(obj->destr) obj->destr(obj);
        else wdf_cleanup_obj(obj);
        cant_fail_ret(pthread_rwlock_wrlock(&list->lock));
    }
    cant_fail_ret(pthread_rwlock_unlock(&list->lock));
    cant_fail_ret(pthread_rwlock_destroy(&list->lock));
}

void winwdf_destroy_object(WDFOBJECT obj) {
    struct wdf_object *wobj = (struct wdf_object*) obj;
    if(wobj->destr) wobj->destr(wobj);
    else wdf_cleanup_obj(wobj);
}

// Fix 3: Implement WdfObjectDelete wrapper so WDFFUNC can find it
__winfnc void WdfObjectDelete(WDFOBJECT obj) {
    winwdf_destroy_object(obj);
}
WDFFUNC(WdfObjectDelete, 80)

// --- WDF Functions ---

__winfnc NTSTATUS WdfObjectCreate(WDF_DRIVER_GLOBALS *globals, WDF_OBJECT_ATTRIBUTES *attributes, WDFOBJECT *out_obj) {
    struct wdf_object *obj = (struct wdf_object*) malloc(sizeof(struct wdf_object));
    if(!obj) return 0xC0000017; 

    wdf_create_obj(NULL, obj, NULL, attributes);
    
    if(out_obj) *out_obj = obj;
    return 0; 
}
WDFFUNC(WdfObjectCreate, 81)

__winfnc void WdfObjectReferenceActual(WDF_DRIVER_GLOBALS *globals, WDFOBJECT Handle, void* Tag, LONG Line, void* File) {
    wdf_object_ref((struct wdf_object*)Handle);
}
WDFFUNC(WdfObjectReferenceActual, 84)

__winfnc void WdfObjectDereferenceActual(WDF_DRIVER_GLOBALS *globals, WDFOBJECT Handle, void* Tag, LONG Line, void* File) {
    wdf_object_unref((struct wdf_object*)Handle);
}
WDFFUNC(WdfObjectDereferenceActual, 86)

__winfnc void* WdfObjectGetTypedContextWorker(WDF_DRIVER_GLOBALS *globals, WDFOBJECT Handle, WDF_OBJECT_CONTEXT_TYPE_INFO *TypeInfo) {
    struct wdf_object *obj = (struct wdf_object*) Handle;
    if (!obj) return NULL;

    void *ret_data = NULL;
    cant_fail_ret(pthread_mutex_lock(&obj->contexts_lock));
    
    struct wdf_object_context *ctx = obj->context_head;
    while(ctx) {
        // If TypeInfo is NULL, just return the first context (common fallback).
        // Otherwise, try to match the type info.
        if(!TypeInfo || ctx->type == TypeInfo) {
            ret_data = ctx->data;
            break;
        }
        ctx = ctx->next;
    }
    
    cant_fail_ret(pthread_mutex_unlock(&obj->contexts_lock));
    
    if (!ret_data) {
        fprintf(stderr, "[WDF] FATAL: Driver requested context type %p but it was not found on object %p!\n", TypeInfo, Handle);
        abort(); // Better to abort cleanly here than segfault later
    }
    
    return ret_data;
}
WDFFUNC(WdfObjectGetTypedContextWorker, 123) // Ensure the index is 123 for UMDF 2.0!