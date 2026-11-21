/*
 * morpheus.h - MOHAA ".scr" (Morpheus) script VM for DoD-GC.
 *
 * Clean-room implementation, MIT licensed (see LICENSE at the repo root).
 * Written from the behaviour of the shipped MOHAA scripts; no code from
 * OpenMoHAA or any other GPL project is used.
 *
 * The VM knows nothing about the game. The host (the DoD-GC game code)
 * supplies a ms_host table for file loading, printing and entity lookup,
 * and registers commands ("events") with ms_register(). Scripts then run
 * against host entities through those bindings.
 *
 * Plain C99, no globals, no floating-point formatting outside ms_tostring,
 * all memory through the host allocator so it can be budgeted on GameCube.
 */
#ifndef MORPHEUS_H
#define MORPHEUS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ms_vm    ms_vm;
typedef struct ms_call  ms_call;
typedef struct ms_str   ms_str;
typedef struct ms_array ms_array;
typedef struct ms_carr  ms_carr;

typedef enum {
    MS_NIL = 0,   /* undefined variable */
    MS_NULL,      /* null listener ("NULL") */
    MS_INT,
    MS_FLOAT,
    MS_STRING,
    MS_VECTOR,
    MS_ENTITY,    /* host entity handle */
    MS_LEVEL,     /* the "level" listener */
    MS_GAME,      /* the "game" listener */
    MS_THREAD,    /* a script thread handle */
    MS_ARRAY,     /* associative array (value semantics, copy on write) */
    MS_CARRAY     /* constant array a::b::c, 1-based */
} ms_type;

typedef struct ms_value {
    uint8_t type;
    union {
        int32_t   i;
        float     f;
        float     v[3];
        uint32_t  id;   /* MS_ENTITY / MS_THREAD */
        ms_str   *s;
        ms_array *a;
        ms_carr  *c;
    } u;
} ms_value;

/* ------------------------------------------------------------------ */
/* Host interface                                                       */
/* ------------------------------------------------------------------ */

enum { MS_PRINT_SCRIPT = 0, MS_PRINT_DEBUG, MS_PRINT_HUD, MS_PRINT_HUD_BOLD, MS_PRINT_ERROR };

typedef struct ms_host {
    void *user;

    /* Memory. NULL means malloc/realloc/free. */
    void *(*alloc)(void *user, void *ptr, size_t old_size, size_t new_size);

    /* Load a script file ("maps/m1l1.scr", "global/ambient.scr").
       Return malloc'd text (freed by free_file) or NULL if missing. */
    char *(*load_file)(void *user, const char *path, size_t *len);
    void  (*free_file)(void *user, char *text);

    void (*print)(void *user, int channel, const char *text);

    /* $name lookup. Write up to max entity ids, return how many exist. */
    int (*find_targetname)(void *user, const char *name, uint32_t *ids, int max);
    /* 0 once an entity has been removed. NULL means always valid. */
    int (*entity_valid)(void *user, uint32_t id);

    /* Native properties ($door.origin, level.time). Return 1 if handled.
       Unhandled names fall back to per-listener script variables. */
    int (*get_field)(void *user, const ms_value *listener, const char *name, ms_value *out);
    int (*set_field)(void *user, const ms_value *listener, const char *name, const ms_value *val);

    /* Called for a command nobody registered. Return 1 if handled. */
    int (*unknown_command)(ms_call *call);

    /* Random number in [0,1). NULL uses an internal LCG. */
    float (*random)(void *user);
} ms_host;

typedef struct ms_config {
    size_t   memory_budget;      /* 0 = unlimited; allocations past it fail */
    int      max_loops;          /* backward jumps per thread per run before it is killed (10000) */
    int      max_nested_threads; /* nested immediate starts before queueing (16) */
    int      max_errors;         /* script errors reported before going quiet (100) */
} ms_config;

ms_vm *ms_create(const ms_host *host, const ms_config *cfg);
void   ms_destroy(ms_vm *vm);

/* Compile and cache a script from text (also used by the loader). */
int ms_load_text(ms_vm *vm, const char *path, const char *text, size_t len);

/* Start a thread. label may be NULL (start of file). self may be NULL.
   Runs until the thread first waits. Returns thread id or 0 on failure. */
uint32_t ms_exec(ms_vm *vm, const char *path, const char *label,
                 const ms_value *self, const ms_value *args, int argc);

/* Advance script time and run every thread that is ready. */
void ms_frame(ms_vm *vm, float now);

/* Wake threads waiting on (listener, name): "$door waittill trigger". */
void ms_notify(ms_vm *vm, const ms_value *listener, const char *name);

/* Host removed an entity: wake "removed" waiters, kill threads owned by it. */
void ms_entity_removed(ms_vm *vm, uint32_t id);

int    ms_thread_count(const ms_vm *vm);
size_t ms_memory_used(const ms_vm *vm);
size_t ms_memory_peak(const ms_vm *vm);
int    ms_error_count(const ms_vm *vm);
float  ms_time(const ms_vm *vm);

/* ------------------------------------------------------------------ */
/* Commands (events)                                                    */
/* ------------------------------------------------------------------ */

/* Which listener a command applies to. */
enum {
    MS_ON_THREAD = 1 << 0,  /* bare "command args" (ScriptThread events) */
    MS_ON_ENTITY = 1 << 1,  /* "$ent command args" */
    MS_ON_LEVEL  = 1 << 2,
    MS_ON_GAME   = 1 << 3,
    MS_ON_ANY    = 0xF
};

typedef int (*ms_cmd_fn)(ms_call *call);  /* return 0 ok, nonzero error */

int ms_register(ms_vm *vm, const char *name, unsigned on, ms_cmd_fn fn, void *data);

/* Inside a command: */
ms_vm          *ms_call_vm(ms_call *c);
void           *ms_call_data(ms_call *c);      /* data given to ms_register */
void           *ms_call_user(ms_call *c);      /* host->user */
const char     *ms_call_name(ms_call *c);
const ms_value *ms_call_target(ms_call *c);    /* listener the command was sent to */
const ms_value *ms_call_self(ms_call *c);      /* the calling thread's self */
int             ms_argc(ms_call *c);
const ms_value *ms_arg(ms_call *c, int i);     /* NIL value past the end */
int32_t         ms_arg_int(ms_call *c, int i);
float           ms_arg_float(ms_call *c, int i);
const char     *ms_arg_string(ms_call *c, int i); /* valid until the call returns */
void            ms_arg_vector(ms_call *c, int i, float out[3]);
void            ms_return(ms_call *c, const ms_value *v);
void            ms_return_int(ms_call *c, int32_t v);
void            ms_return_float(ms_call *c, float v);
void            ms_return_string(ms_call *c, const char *s);
void            ms_return_vector(ms_call *c, const float v[3]);
void            ms_return_entity(ms_call *c, uint32_t id);
int             ms_call_error(ms_call *c, const char *fmt, ...);
/* Suspend the calling thread after the command returns. */
void            ms_call_wait_time(ms_call *c, float seconds);
void            ms_call_wait_signal(ms_call *c, const ms_value *listener, const char *name);

/* ------------------------------------------------------------------ */
/* Values                                                               */
/* ------------------------------------------------------------------ */

ms_value ms_nil(void);
ms_value ms_int(int32_t i);
ms_value ms_float(float f);
ms_value ms_vec(float x, float y, float z);
ms_value ms_entity(uint32_t id);
ms_value ms_string(ms_vm *vm, const char *s);   /* caller owns one reference */
void     ms_retain(ms_value v);
void     ms_release(ms_vm *vm, ms_value v);
int      ms_truthy(ms_vm *vm, const ms_value *v);
float    ms_to_float(const ms_value *v);
int32_t  ms_to_int(const ms_value *v);
/* Write a printable form; returns buf. */
const char *ms_tostring(const ms_value *v, char *buf, size_t size);
const char *ms_type_name(int type);

/* Array helpers for hosts that return entity lists. */
ms_value ms_array_new(ms_vm *vm);
int      ms_array_set(ms_vm *vm, ms_value *arr, const ms_value *key, const ms_value *val);
int      ms_array_size(const ms_value *arr);
/* i-th element (0-based, insertion order) of an array or const array; NULL past the end. */
const ms_value *ms_array_item(const ms_value *arr, int i);
/* text of a string value, NULL for other types */
const char *ms_string_text(const ms_value *v);

#ifdef __cplusplus
}
#endif

#endif
