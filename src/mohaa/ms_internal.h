/* ms_internal.h - private structures of the Morpheus script VM. MIT licensed. */
#ifndef MS_INTERNAL_H
#define MS_INTERNAL_H

#include "morpheus.h"
#include <stdarg.h>

#define MS_NAME_NONE 0xFFFFFFFFu

/* ---- heap objects ---- */

struct ms_str {
    int32_t  rc;
    uint32_t len;
    uint32_t hash;
    char     data[1];
};

struct ms_carr {
    int32_t  rc;
    int32_t  n;
    ms_value items[1];
};

typedef struct ms_slot {
    ms_value key;
    ms_value val;
} ms_slot;

struct ms_array {
    int32_t  rc;
    int32_t  n, cap;        /* slots, insertion ordered */
    ms_slot *slots;
    int32_t *index;         /* open addressing, -1 empty, power of two */
    int32_t  icap;
};

/* name id -> value, used for local/level/game/parm/group and entity vars */
typedef struct ms_table {
    uint32_t *keys;         /* MS_NAME_NONE = empty */
    ms_value *vals;
    uint32_t  n, cap;       /* cap is a power of two or 0 */
} ms_table;

/* ---- names ---- */

typedef struct ms_names {
    char    **str;
    uint32_t *hash;
    uint32_t  n, cap;
    uint32_t *index;        /* open addressing over ids */
    uint32_t  icap;
} ms_names;

/* ---- compiled scripts ---- */

typedef struct ms_label {
    uint32_t name;          /* interned, lowercase */
    uint32_t entry;         /* binds parameters, then falls into body */
    uint32_t body;
} ms_label;

typedef struct ms_case {
    ms_value key;           /* MS_INT or MS_STRING */
    uint32_t pc;
} ms_case;

typedef struct ms_switch {
    ms_case *cases;
    uint32_t n;
    uint32_t def;           /* pc of "default:" or end of switch */
} ms_switch;

typedef struct ms_line {
    uint32_t pc, line;
} ms_line;

typedef struct ms_script {
    char      *path;
    uint8_t   *code;
    uint32_t   code_len;
    ms_value  *consts;      /* string constants */
    uint32_t   nconsts;
    ms_label  *labels;
    uint32_t   nlabels;
    ms_switch *switches;
    uint32_t   nswitches;
    ms_line   *lines;
    uint32_t   nlines;
    struct ms_script *next;
} ms_script;

/* ---- threads ---- */

enum {
    TS_RUNNING = 0,
    TS_READY,          /* run at the next opportunity */
    TS_WAIT_TIME,
    TS_WAIT_FRAME,
    TS_WAIT_SIGNAL,
    TS_WAIT_THREAD,
    TS_DEAD
};

typedef struct ms_group {
    int32_t  rc;
    ms_table vars;
} ms_group;

typedef struct ms_try {
    uint32_t catch_pc;
    int32_t  sp;
} ms_try;

#define MS_MAX_TRY 8

typedef struct ms_thread {
    uint32_t   id;
    ms_script *script;
    uint32_t   pc;
    ms_value  *stack;
    int32_t    sp, scap;
    ms_table   locals;
    ms_value   self;
    ms_group  *group;
    ms_value  *args;
    int32_t    argc;
    int32_t    state;
    float      wake_time;
    uint32_t   wait_frame;
    ms_value   wait_listener;
    uint32_t   wait_name;
    uint32_t   wait_thread;
    int32_t    result_slot;  /* stack slot to fill when a waitthread returns, or -1 */
    ms_value   retval;
    int32_t    loops;
    int32_t    error;        /* set by vm_error, consumed by the interpreter */
    int32_t    running;      /* nesting guard */
    ms_try     trys[MS_MAX_TRY];
    int32_t    ntry;
    struct ms_thread *next;
} ms_thread;

/* ---- commands ---- */

typedef struct ms_cmd {
    uint32_t  name;
    unsigned  on;
    ms_cmd_fn fn;
    void     *data;
    struct ms_cmd *next;     /* same name, other listener kinds */
} ms_cmd;

struct ms_call {
    ms_vm          *vm;
    ms_thread      *thread;
    ms_cmd         *cmd;
    uint32_t        name;
    ms_value        target;
    ms_value       *args;
    int32_t         argc;
    ms_value        ret;
    int32_t         wait_kind;   /* 0 none, 1 time, 2 signal, 3 thread, 4 frame, 5 end */
    float           wait_time;
    ms_value        wait_listener;
    uint32_t        wait_name;
    uint32_t        wait_thread;
    char            sbuf[4][64];
    int32_t         sbuf_next;
};

typedef struct ms_entvars {
    uint32_t id;
    ms_table vars;
    struct ms_entvars *next;
} ms_entvars;

struct ms_vm {
    ms_host     host;
    ms_config   cfg;
    size_t      mem_used, mem_peak;
    int32_t     mem_failed;
    ms_names    names;
    ms_cmd    **cmds;          /* indexed by name id, grown on demand */
    uint32_t    ncmds;
    ms_script  *scripts;
    ms_thread  *threads;
    ms_thread  *current;
    uint32_t    next_thread_id;
    ms_table    level_vars, game_vars, parm_vars;
    ms_entvars *entvars;
    float       time;
    uint32_t    frame;
    int32_t     depth;
    int32_t     errors;
    uint32_t    rng;
    /* well known names */
    uint32_t    n_size, n_removed;
};

/* memory */
void *ms_alloc(ms_vm *vm, size_t size);
void *ms_realloc(ms_vm *vm, void *p, size_t new_size);
void  ms_free(ms_vm *vm, void *p);
char *ms_strdup(ms_vm *vm, const char *s);
void  ms_strfree(ms_vm *vm, char *s);

/* names */
uint32_t    ms_intern(ms_vm *vm, const char *s, size_t len);
uint32_t    ms_intern_lower(ms_vm *vm, const char *s, size_t len);
const char *ms_name(const ms_vm *vm, uint32_t id);
void        ms_names_free(ms_vm *vm);
uint32_t    ms_hash(const char *s, size_t len);

/* values */
ms_value  ms_string_len(ms_vm *vm, const char *s, size_t len);
ms_str   *ms_str_new(ms_vm *vm, const char *s, size_t len);
ms_value  ms_carr_new(ms_vm *vm, int n);
ms_value  ms_array_copy(ms_vm *vm, const ms_array *src);
ms_value *ms_array_get(const ms_value *arr, const ms_value *key);
ms_value *ms_array_slot(ms_vm *vm, ms_value *arr, const ms_value *key); /* creates; arr must be unique */
int       ms_equal(ms_vm *vm, const ms_value *a, const ms_value *b);
int       ms_compare(ms_vm *vm, const ms_value *a, const ms_value *b, int *out); /* 0 = incomparable */
int       ms_binop(ms_vm *vm, int op, const ms_value *a, const ms_value *b, ms_value *out, const char **err);
int       ms_unop(ms_vm *vm, int op, const ms_value *a, ms_value *out, const char **err);
int       ms_same_listener(const ms_value *a, const ms_value *b);
int       ms_is_listener(const ms_value *v);
ms_value  ms_to_string_value(ms_vm *vm, const ms_value *v);  /* new reference */

/* tables */
ms_value *ms_table_get(const ms_table *t, uint32_t key);
ms_value *ms_table_slot(ms_vm *vm, ms_table *t, uint32_t key);
void      ms_table_free(ms_vm *vm, ms_table *t);
ms_table *ms_entity_vars(ms_vm *vm, uint32_t id, int create);

/* compile */
ms_script *ms_compile(ms_vm *vm, const char *path, const char *text, size_t len, char *err, size_t errlen);
void       ms_script_free(ms_vm *vm, ms_script *s);
const ms_label *ms_find_label(const ms_script *s, uint32_t name);
uint32_t   ms_line_for_pc(const ms_script *s, uint32_t pc);

/* vm */
void       ms_report(ms_vm *vm, const char *fmt, ...);
void       vm_error(ms_vm *vm, ms_thread *t, const char *fmt, ...);
ms_script *ms_get_script(ms_vm *vm, const char *path);
ms_thread *ms_find_thread(ms_vm *vm, uint32_t id);
ms_thread *ms_spawn_thread(ms_vm *vm, ms_script *s, const ms_label *lab, const ms_value *self,
                           ms_group *group, const ms_value *args, int argc);
void       ms_start_thread(ms_vm *vm, ms_thread *t);
void       ms_kill_thread(ms_vm *vm, ms_thread *t);
ms_group  *ms_group_new(ms_vm *vm);
void       ms_group_release(ms_vm *vm, ms_group *g);
ms_cmd    *ms_find_cmd(ms_vm *vm, uint32_t name, unsigned on);
float      ms_random01(ms_vm *vm);

/* builtins */
void ms_register_builtins(ms_vm *vm);

/* bytecode */
enum {
    OP_NOP = 0,
    OP_PUSH_NIL, OP_PUSH_NULL, OP_PUSH_INT, OP_PUSH_FLOAT, OP_PUSH_STR,
    OP_MAKE_VEC, OP_PUSH_LISTENER,
    OP_LOAD_SCOPE, OP_STORE_SCOPE,
    OP_GET_FIELD, OP_SET_FIELD,
    OP_GET_INDEX, OP_STORE_INDEXED,
    OP_SIZE, OP_TARGETNAME,
    OP_MAKE_CARR, OP_MAKE_ARRAY,
    OP_BIN, OP_UN, OP_NOT,
    OP_JMP, OP_JF, OP_JT, OP_JF_KEEP, OP_JT_KEEP,
    OP_POP, OP_DUP,
    OP_CMD, OP_METHOD,
    OP_ARG,
    OP_SWITCH,
    OP_TRY, OP_ENDTRY,
    OP_END
};

/* listener scopes, OP_PUSH_LISTENER / OP_LOAD_SCOPE operand */
enum { SC_GAME = 0, SC_LEVEL, SC_LOCAL, SC_PARM, SC_SELF, SC_GROUP, SC_OWNER };

/* binary ops */
enum {
    BO_ADD = 0, BO_SUB, BO_MUL, BO_DIV, BO_MOD, BO_SHL, BO_SHR,
    BO_AND, BO_OR, BO_XOR, BO_EQ, BO_NE, BO_LT, BO_GT, BO_LE, BO_GE
};
enum { UO_NEG = 0, UO_COMPL, UO_INC, UO_DEC };

/* OP_STORE_INDEXED base kinds */
enum { IB_SCOPE = 0, IB_FIELD };

#endif
