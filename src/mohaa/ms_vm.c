/* ms_vm.c - threads, scheduler and bytecode interpreter. MIT licensed. */
#include "ms_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* errors                                                               */
/* ------------------------------------------------------------------ */

static void host_print(ms_vm *vm, int ch, const char *s)
{
    if (vm->host.print)
        vm->host.print(vm->host.user, ch, s);
    else
        fputs(s, ch == MS_PRINT_ERROR ? stderr : stdout);
}

void ms_report(ms_vm *vm, const char *fmt, ...)
{
    char msg[512];
    va_list ap;
    vm->errors++;
    if (vm->errors > vm->cfg.max_errors)
        return;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg) - 2, fmt, ap);
    va_end(ap);
    strcat(msg, "\n");
    host_print(vm, MS_PRINT_ERROR, msg);
    if (vm->errors == vm->cfg.max_errors)
        host_print(vm, MS_PRINT_ERROR, "script: too many errors, no more will be reported\n");
}

void vm_error(ms_vm *vm, ms_thread *t, const char *fmt, ...)
{
    char msg[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    if (t) {
        t->error = 1;
        if (t->ntry > 0)
            return;   /* caught by try/catch */
        ms_report(vm, "script error: %s:%u: %s", t->script->path,
                  (unsigned)ms_line_for_pc(t->script, t->pc ? t->pc - 1 : 0), msg);
    } else
        ms_report(vm, "script error: %s", msg);
}

/* ------------------------------------------------------------------ */
/* create / destroy                                                     */
/* ------------------------------------------------------------------ */

ms_vm *ms_create(const ms_host *host, const ms_config *cfg)
{
    ms_vm tmp;
    memset(&tmp, 0, sizeof(tmp));
    if (host)
        tmp.host = *host;
    if (cfg)
        tmp.cfg = *cfg;
    if (!tmp.cfg.max_loops)
        tmp.cfg.max_loops = 10000;
    if (!tmp.cfg.max_nested_threads)
        tmp.cfg.max_nested_threads = 16;
    if (!tmp.cfg.max_errors)
        tmp.cfg.max_errors = 100;
    ms_vm *vm = (ms_vm *)ms_alloc(&tmp, sizeof(ms_vm));
    if (!vm)
        return NULL;
    *vm = tmp;
    vm->next_thread_id = 1;
    vm->rng = 0x12345678u;
    vm->n_size = ms_intern(vm, "size", 4);
    vm->n_removed = ms_intern(vm, "removed", 7);
    ms_register_builtins(vm);
    return vm;
}

static void thread_free(ms_vm *vm, ms_thread *t)
{
    for (int32_t i = 0; i < t->sp; i++)
        ms_release(vm, t->stack[i]);
    ms_free(vm, t->stack);
    ms_table_free(vm, &t->locals);
    for (int32_t i = 0; i < t->argc; i++)
        ms_release(vm, t->args[i]);
    ms_free(vm, t->args);
    ms_release(vm, t->retval);
    if (t->group)
        ms_group_release(vm, t->group);
    ms_free(vm, t);
}

void ms_destroy(ms_vm *vm)
{
    if (!vm)
        return;
    while (vm->threads) {
        ms_thread *n = vm->threads->next;
        thread_free(vm, vm->threads);
        vm->threads = n;
    }
    /* values first: they may point into the scripts' constant pools */
    while (vm->entvars) {
        ms_entvars *n = vm->entvars->next;
        ms_table_free(vm, &vm->entvars->vars);
        ms_free(vm, vm->entvars);
        vm->entvars = n;
    }
    ms_table_free(vm, &vm->level_vars);
    ms_table_free(vm, &vm->game_vars);
    ms_table_free(vm, &vm->parm_vars);
    while (vm->scripts) {
        ms_script *n = vm->scripts->next;
        ms_script_free(vm, vm->scripts);
        vm->scripts = n;
    }
    while (vm->retired) {
        ms_pool *n = vm->retired->next;
        ms_free(vm, vm->retired);
        vm->retired = n;
    }
    for (uint32_t i = 0; i < vm->ncmds; i++) {
        ms_cmd *c = vm->cmds[i];
        while (c) {
            ms_cmd *n = c->next;
            ms_free(vm, c);
            c = n;
        }
    }
    ms_free(vm, vm->cmds);
    ms_names_free(vm);
    ms_vm tmp = *vm;
    ms_free(&tmp, vm);
}

int ms_thread_count(const ms_vm *vm)
{
    int n = 0;
    for (ms_thread *t = vm->threads; t; t = t->next)
        if (t->state != TS_DEAD)
            n++;
    return n;
}

size_t ms_memory_used(const ms_vm *vm) { return vm->mem_used; }
size_t ms_memory_peak(const ms_vm *vm) { return vm->mem_peak; }
int ms_error_count(const ms_vm *vm) { return vm->errors; }
float ms_time(const ms_vm *vm) { return vm->time; }

float ms_random01(ms_vm *vm)
{
    if (vm->host.random)
        return vm->host.random(vm->host.user);
    vm->rng = vm->rng * 1664525u + 1013904223u;
    return (float)(vm->rng >> 8) / 16777216.0f;
}

/* ------------------------------------------------------------------ */
/* commands                                                             */
/* ------------------------------------------------------------------ */

int ms_register(ms_vm *vm, const char *name, unsigned on, ms_cmd_fn fn, void *data)
{
    uint32_t id = ms_intern_lower(vm, name, strlen(name));
    if (id == MS_NAME_NONE)
        return 0;
    if (id >= vm->ncmds) {
        uint32_t nn = vm->ncmds ? vm->ncmds : 64;
        while (nn <= id)
            nn *= 2;
        ms_cmd **nc = (ms_cmd **)ms_realloc(vm, vm->cmds, sizeof(ms_cmd *) * nn);
        if (!nc)
            return 0;
        memset(nc + vm->ncmds, 0, sizeof(ms_cmd *) * (nn - vm->ncmds));
        vm->cmds = nc;
        vm->ncmds = nn;
    }
    /* a later registration for the same listener kinds replaces the old one */
    for (ms_cmd *c = vm->cmds[id]; c; c = c->next) {
        if (c->on == on) {
            c->fn = fn;
            c->data = data;
            return 1;
        }
    }
    ms_cmd *c = (ms_cmd *)ms_alloc(vm, sizeof(ms_cmd));
    if (!c)
        return 0;
    c->name = id;
    c->on = on;
    c->fn = fn;
    c->data = data;
    c->next = vm->cmds[id];   /* newest first, so hosts can override builtins */
    vm->cmds[id] = c;
    return 1;
}

ms_cmd *ms_find_cmd(ms_vm *vm, uint32_t name, unsigned on)
{
    if (name >= vm->ncmds)
        return NULL;
    for (ms_cmd *c = vm->cmds[name]; c; c = c->next)
        if (c->on & on)
            return c;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* scripts                                                              */
/* ------------------------------------------------------------------ */

static void normalize_path(const char *in, char *out, size_t size)
{
    size_t o = 0;
    for (; *in && o + 1 < size; in++) {
        char c = *in;
        if (c == '\\')
            c = '/';
        if (c >= 'A' && c <= 'Z')
            c = (char)(c + 32);
        out[o++] = c;
    }
    out[o] = 0;
}

int ms_load_text(ms_vm *vm, const char *path, const char *text, size_t len)
{
    char np[256], err[320];
    normalize_path(path, np, sizeof(np));
    ms_script *s = ms_compile(vm, np, text, len, err, sizeof(err));
    if (!s) {
        ms_report(vm, "script compile error: %s", err);
        return 0;
    }
    /* replace an older copy */
    for (ms_script **pp = &vm->scripts; *pp; pp = &(*pp)->next) {
        if (strcmp((*pp)->path, np) == 0) {
            int used = 0;
            for (ms_thread *t = vm->threads; t; t = t->next)
                if (t->script == *pp && t->state != TS_DEAD)
                    used = 1;
            if (used) {
                ms_report(vm, "script: %s is running, not reloaded", np);
                ms_script_free(vm, s);
                return 0;
            }
            ms_script *old = *pp;
            s->next = old->next;
            *pp = s;
            ms_script_retire(vm, old);   /* variables may still hold its constants */
            return 1;
        }
    }
    s->next = vm->scripts;
    vm->scripts = s;
    return 1;
}

ms_script *ms_get_script(ms_vm *vm, const char *path)
{
    char np[256];
    normalize_path(path, np, sizeof(np));
    for (ms_script *s = vm->scripts; s; s = s->next)
        if (strcmp(s->path, np) == 0)
            return s;
    if (!vm->host.load_file) {
        ms_report(vm, "script: cannot load %s (no loader)", np);
        return NULL;
    }
    size_t len = 0;
    char *text = vm->host.load_file(vm->host.user, np, &len);
    if (!text) {
        ms_report(vm, "script: file not found: %s", np);
        return NULL;
    }
    int ok = ms_load_text(vm, np, text, len);
    if (vm->host.free_file)
        vm->host.free_file(vm->host.user, text);
    else
        free(text);
    if (!ok)
        return NULL;
    for (ms_script *s = vm->scripts; s; s = s->next)
        if (strcmp(s->path, np) == 0)
            return s;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* threads                                                              */
/* ------------------------------------------------------------------ */

ms_group *ms_group_new(ms_vm *vm)
{
    ms_group *g = (ms_group *)ms_alloc(vm, sizeof(ms_group));
    if (g)
        g->rc = 1;
    return g;
}

void ms_group_release(ms_vm *vm, ms_group *g)
{
    if (--g->rc == 0) {
        ms_table_free(vm, &g->vars);
        ms_free(vm, g);
    }
}

ms_thread *ms_find_thread(ms_vm *vm, uint32_t id)
{
    for (ms_thread *t = vm->threads; t; t = t->next)
        if (t->id == id)
            return t->state == TS_DEAD ? NULL : t;
    return NULL;
}

ms_thread *ms_spawn_thread(ms_vm *vm, ms_script *s, const ms_label *lab, const ms_value *self,
                           ms_group *group, const ms_value *args, int argc)
{
    ms_thread *t = (ms_thread *)ms_alloc(vm, sizeof(ms_thread));
    if (!t) {
        ms_report(vm, "script: out of memory starting a thread");
        return NULL;
    }
    t->scap = 16;
    t->stack = (ms_value *)ms_alloc(vm, sizeof(ms_value) * (size_t)t->scap);
    if (argc > 0)
        t->args = (ms_value *)ms_alloc(vm, sizeof(ms_value) * (size_t)argc);
    if (!t->stack || (argc > 0 && !t->args)) {
        ms_free(vm, t->stack);
        ms_free(vm, t->args);
        ms_free(vm, t);
        ms_report(vm, "script: out of memory starting a thread");
        return NULL;
    }
    for (int i = 0; i < argc; i++) {
        t->args[i] = args[i];
        ms_retain(t->args[i]);
    }
    t->argc = argc;
    t->id = vm->next_thread_id++;
    if (!vm->next_thread_id)
        vm->next_thread_id = 1;
    t->script = s;
    t->pc = lab ? lab->entry : 0;
    t->self = self ? *self : ms_nil();
    if (!ms_is_listener(&t->self))
        t->self.type = t->self.type == MS_NULL ? MS_NULL : MS_NIL;
    if (group) {
        group->rc++;
        t->group = group;
    } else
        t->group = ms_group_new(vm);
    t->state = TS_READY;
    t->result_slot = -1;
    ms_thread **pp = &vm->threads;
    while (*pp)
        pp = &(*pp)->next;
    *pp = t;
    return t;
}

static void run_thread(ms_vm *vm, ms_thread *t);

void ms_start_thread(ms_vm *vm, ms_thread *t)
{
    if (t->state != TS_READY || t->running)
        return;
    if (vm->depth >= vm->cfg.max_nested_threads)
        return;   /* stays READY, picked up by the next ms_frame */
    run_thread(vm, t);
}

static void wake_waiters_of(ms_vm *vm, ms_thread *done)
{
    for (ms_thread *w = vm->threads; w; w = w->next) {
        if (w->state != TS_WAIT_THREAD || w->wait_thread != done->id)
            continue;
        if (w->result_slot >= 0 && w->result_slot < w->sp) {
            ms_release(vm, w->stack[w->result_slot]);
            w->stack[w->result_slot] = done->retval;
            ms_retain(done->retval);
        }
        w->result_slot = -1;
        w->state = TS_READY;
    }
}

static void finish_thread(ms_vm *vm, ms_thread *t)
{
    if (t->state == TS_DEAD)
        return;
    t->state = TS_DEAD;
    wake_waiters_of(vm, t);
}

void ms_kill_thread(ms_vm *vm, ms_thread *t)
{
    finish_thread(vm, t);
}

static void run_ready(ms_vm *vm)
{
    /* threads woken by other threads (waitthread, notify) run in the same frame */
    if (vm->depth)
        return;
    for (int pass = 0; pass < 64; pass++) {
        int ran = 0;
        for (ms_thread *t = vm->threads; t; t = t->next) {
            if (t->state == TS_READY && !t->running) {
                ms_start_thread(vm, t);
                ran = 1;
            }
        }
        if (!ran)
            return;
    }
}

static void sweep(ms_vm *vm)
{
    if (vm->depth)
        return;
    ms_thread **pp = &vm->threads;
    while (*pp) {
        ms_thread *t = *pp;
        if (t->state == TS_DEAD && !t->running) {
            *pp = t->next;
            thread_free(vm, t);
        } else
            pp = &t->next;
    }
}

uint32_t ms_exec(ms_vm *vm, const char *path, const char *label, const ms_value *self,
                 const ms_value *args, int argc)
{
    ms_script *s = ms_get_script(vm, path);
    if (!s)
        return 0;
    const ms_label *lab = NULL;
    if (label && *label) {
        lab = ms_find_label(s, ms_intern_lower(vm, label, strlen(label)));
        if (!lab) {
            ms_report(vm, "script: label '%s' not found in %s", label, s->path);
            return 0;
        }
    }
    ms_thread *t = ms_spawn_thread(vm, s, lab, self, NULL, args, argc);
    if (!t)
        return 0;
    uint32_t id = t->id;
    ms_start_thread(vm, t);
    run_ready(vm);
    sweep(vm);
    return id;
}

void ms_frame(ms_vm *vm, float now)
{
    vm->time = now;
    vm->frame++;
    for (ms_thread *t = vm->threads; t; t = t->next) {
        if (t->running)
            continue;
        if ((t->state == TS_WAIT_TIME && t->wake_time <= now) ||
            (t->state == TS_WAIT_FRAME && t->wait_frame < vm->frame))
            t->state = TS_READY;
    }
    run_ready(vm);
    sweep(vm);
}

void ms_notify(ms_vm *vm, const ms_value *listener, const char *name)
{
    uint32_t id = ms_intern_lower(vm, name, strlen(name));
    int woke = 0;
    for (ms_thread *t = vm->threads; t; t = t->next) {
        if (t->state == TS_WAIT_SIGNAL && t->wait_name == id && ms_same_listener(&t->wait_listener, listener)) {
            t->state = TS_READY;
            woke = 1;
        }
    }
    if (woke) {
        run_ready(vm);
        sweep(vm);
    }
}

void ms_entity_removed(ms_vm *vm, uint32_t id)
{
    ms_value e = ms_entity(id);
    ms_notify(vm, &e, "removed");
    for (ms_thread *t = vm->threads; t; t = t->next) {
        if (t->state == TS_DEAD)
            continue;
        int owned = t->self.type == MS_ENTITY && t->self.u.id == id;
        int waiting = t->state == TS_WAIT_SIGNAL && t->wait_listener.type == MS_ENTITY && t->wait_listener.u.id == id;
        if ((owned || waiting) && !t->running)
            finish_thread(vm, t);
    }
    for (ms_entvars **pp = &vm->entvars; *pp; pp = &(*pp)->next) {
        if ((*pp)->id == id) {
            ms_entvars *e2 = *pp;
            *pp = e2->next;
            ms_table_free(vm, &e2->vars);
            ms_free(vm, e2);
            break;
        }
    }
    run_ready(vm);
    sweep(vm);
}

/* ------------------------------------------------------------------ */
/* the call interface                                                   */
/* ------------------------------------------------------------------ */

static const ms_value nil_value = {0, {0}};

ms_vm *ms_call_vm(ms_call *c) { return c->vm; }
void *ms_call_data(ms_call *c) { return c->cmd ? c->cmd->data : NULL; }
void *ms_call_user(ms_call *c) { return c->vm->host.user; }
const char *ms_call_name(ms_call *c) { return ms_name(c->vm, c->name); }
const ms_value *ms_call_target(ms_call *c) { return &c->target; }
const ms_value *ms_call_self(ms_call *c) { return c->thread ? &c->thread->self : &nil_value; }
int ms_argc(ms_call *c) { return c->argc; }

const ms_value *ms_arg(ms_call *c, int i)
{
    return (i >= 0 && i < c->argc) ? &c->args[i] : &nil_value;
}

int32_t ms_arg_int(ms_call *c, int i) { return ms_to_int(ms_arg(c, i)); }
float ms_arg_float(ms_call *c, int i) { return ms_to_float(ms_arg(c, i)); }

const char *ms_arg_string(ms_call *c, int i)
{
    const ms_value *v = ms_arg(c, i);
    if (v->type == MS_STRING)
        return v->u.s->data;
    char *buf = c->sbuf[c->sbuf_next++ & 3];
    return ms_tostring(v, buf, sizeof(c->sbuf[0]));
}

void ms_arg_vector(ms_call *c, int i, float out[3])
{
    const ms_value *v = ms_arg(c, i);
    if (v->type == MS_VECTOR) {
        out[0] = v->u.v[0];
        out[1] = v->u.v[1];
        out[2] = v->u.v[2];
    } else if (v->type == MS_STRING) {
        float x = 0, y = 0, z = 0;
        const char *s = v->u.s->data;
        while (*s == '(' || *s == ' ')
            s++;
        sscanf(s, "%f %f %f", &x, &y, &z);
        out[0] = x;
        out[1] = y;
        out[2] = z;
    } else {
        out[0] = out[1] = out[2] = ms_to_float(v);
    }
}

void ms_return(ms_call *c, const ms_value *v)
{
    ms_release(c->vm, c->ret);
    c->ret = *v;
    ms_retain(c->ret);
}

void ms_return_int(ms_call *c, int32_t v) { ms_value x = ms_int(v); ms_return(c, &x); }
void ms_return_float(ms_call *c, float v) { ms_value x = ms_float(v); ms_return(c, &x); }
void ms_return_vector(ms_call *c, const float v[3]) { ms_value x = ms_vec(v[0], v[1], v[2]); ms_return(c, &x); }
void ms_return_entity(ms_call *c, uint32_t id) { ms_value x = ms_entity(id); ms_return(c, &x); }

void ms_return_string(ms_call *c, const char *s)
{
    ms_value x = ms_string(c->vm, s);
    ms_return(c, &x);
    ms_release(c->vm, x);
}

int ms_call_error(ms_call *c, const char *fmt, ...)
{
    char msg[300];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    vm_error(c->vm, c->thread, "%s: %s", ms_name(c->vm, c->name), msg);
    return 1;
}

void ms_call_wait_time(ms_call *c, float seconds)
{
    if (seconds <= 0.0f) {
        c->wait_kind = 4;
        return;
    }
    c->wait_kind = 1;
    c->wait_time = seconds;
}

void ms_call_wait_signal(ms_call *c, const ms_value *listener, const char *name)
{
    c->wait_kind = 2;
    c->wait_listener = *listener;
    c->wait_name = ms_intern_lower(c->vm, name, strlen(name));
}

/* ------------------------------------------------------------------ */
/* interpreter                                                          */
/* ------------------------------------------------------------------ */

static inline uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static inline uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }
static inline float rdf(const uint8_t *p) { float v; memcpy(&v, p, 4); return v; }

static int push(ms_vm *vm, ms_thread *t, ms_value v)
{
    if (t->sp == t->scap) {
        int32_t ncap = t->scap * 2;
        ms_value *ns = (ms_value *)ms_realloc(vm, t->stack, sizeof(ms_value) * (size_t)ncap);
        if (!ns) {
            ms_release(vm, v);
            vm_error(vm, t, "out of memory (stack)");
            return 0;
        }
        t->stack = ns;
        t->scap = ncap;
    }
    t->stack[t->sp++] = v;
    return 1;
}

static ms_value pop(ms_thread *t)
{
    return t->sp > 0 ? t->stack[--t->sp] : nil_value;
}

static ms_table *scope_table(ms_vm *vm, ms_thread *t, int sc)
{
    switch (sc) {
    case SC_LOCAL: return &t->locals;
    case SC_LEVEL: return &vm->level_vars;
    case SC_GAME: return &vm->game_vars;
    case SC_PARM: return &vm->parm_vars;
    case SC_GROUP: return &t->group->vars;
    default: return NULL;
    }
}

/* variable table behind a listener value (entity, level, game, thread) */
static ms_table *listener_table(ms_vm *vm, const ms_value *obj, int create)
{
    switch (obj->type) {
    case MS_ENTITY: return ms_entity_vars(vm, obj->u.id, create);
    case MS_LEVEL: return &vm->level_vars;
    case MS_GAME: return &vm->game_vars;
    case MS_THREAD: {
        ms_thread *o = ms_find_thread(vm, obj->u.id);
        return o ? &o->locals : NULL;
    }
    default: return NULL;
    }
}

static ms_value get_field(ms_vm *vm, ms_thread *t, const ms_value *obj, uint32_t name)
{
    ms_value out = ms_nil();
    if (!ms_is_listener(obj)) {
        if (obj->type == MS_ARRAY || obj->type == MS_CARRAY) {
            vm_error(vm, t, "field '%s' applied to an array", ms_name(vm, name));
        } else
            vm_error(vm, t, "field '%s' applied to %s", ms_name(vm, name), ms_type_name(obj->type));
        return out;
    }
    if (obj->type == MS_ENTITY && vm->host.entity_valid && !vm->host.entity_valid(vm->host.user, obj->u.id)) {
        vm_error(vm, t, "field '%s' applied to a removed entity", ms_name(vm, name));
        return out;
    }
    if (obj->type != MS_THREAD && vm->host.get_field &&
        vm->host.get_field(vm->host.user, obj, ms_name(vm, name), &out))
        return out;
    if (obj->type == MS_LEVEL && strcmp(ms_name(vm, name), "time") == 0)
        return ms_float(vm->time);
    ms_table *tab = listener_table(vm, obj, 0);
    ms_value *v = tab ? ms_table_get(tab, name) : NULL;
    if (v) {
        out = *v;
        ms_retain(out);
    }
    return out;
}

/* takes ownership of val */
static void set_field(ms_vm *vm, ms_thread *t, const ms_value *obj, uint32_t name, ms_value val)
{
    if (!ms_is_listener(obj)) {
        vm_error(vm, t, "cannot set field '%s' on %s", ms_name(vm, name), ms_type_name(obj->type));
        ms_release(vm, val);
        return;
    }
    if (obj->type == MS_ENTITY && vm->host.entity_valid && !vm->host.entity_valid(vm->host.user, obj->u.id)) {
        vm_error(vm, t, "field '%s' set on a removed entity", ms_name(vm, name));
        ms_release(vm, val);
        return;
    }
    if (obj->type != MS_THREAD && vm->host.set_field &&
        vm->host.set_field(vm->host.user, obj, ms_name(vm, name), &val)) {
        ms_release(vm, val);
        return;
    }
    ms_table *tab = listener_table(vm, obj, 1);
    ms_value *slot = tab ? ms_table_slot(vm, tab, name) : NULL;
    if (!slot) {
        vm_error(vm, t, "cannot set field '%s'", ms_name(vm, name));
        ms_release(vm, val);
        return;
    }
    ms_release(vm, *slot);
    *slot = val;
}

static ms_value targetname(ms_vm *vm, ms_thread *t, const ms_value *name)
{
    char buf[96];
    const char *s = name->type == MS_STRING ? name->u.s->data : ms_tostring(name, buf, sizeof(buf));
    ms_value out = ms_nil();
    out.type = MS_NULL;
    if (!vm->host.find_targetname)
        return out;
    uint32_t ids[16];
    int n = vm->host.find_targetname(vm->host.user, s, ids, 16);
    if (n <= 0)
        return out;
    if (n == 1)
        return ms_entity(ids[0]);
    uint32_t *all = ids;
    if (n > 16) {
        all = (uint32_t *)ms_alloc(vm, sizeof(uint32_t) * (size_t)n);
        if (!all) {
            vm_error(vm, t, "out of memory");
            return out;
        }
        n = vm->host.find_targetname(vm->host.user, s, all, n);
    }
    out = ms_carr_new(vm, n);
    if (out.type == MS_CARRAY)
        for (int i = 0; i < n; i++)
            out.u.c->items[i] = ms_entity(all[i]);
    if (all != ids)
        ms_free(vm, all);
    return out;
}

static ms_value get_index(ms_vm *vm, ms_thread *t, const ms_value *obj, const ms_value *idx)
{
    ms_value out = ms_nil();
    switch (obj->type) {
    case MS_NIL: return out;
    case MS_ARRAY: {
        ms_value *v = ms_array_get(obj, idx);
        if (v) {
            out = *v;
            ms_retain(out);
        }
        return out;
    }
    case MS_CARRAY: {
        int32_t i = ms_to_int(idx);
        if (i < 1 || i > obj->u.c->n) {
            vm_error(vm, t, "const array index %d out of range 1..%d", (int)i, (int)obj->u.c->n);
            return out;
        }
        out = obj->u.c->items[i - 1];
        ms_retain(out);
        return out;
    }
    case MS_STRING: {
        int32_t i = ms_to_int(idx);
        if (i < 0 || (uint32_t)i >= obj->u.s->len) {
            vm_error(vm, t, "string index %d out of range", (int)i);
            return out;
        }
        return ms_string_len(vm, obj->u.s->data + i, 1);
    }
    case MS_VECTOR: {
        int32_t i = ms_to_int(idx);
        if (i < 0 || i > 2) {
            vm_error(vm, t, "vector index %d out of range", (int)i);
            return out;
        }
        return ms_float(obj->u.v[i]);
    }
    default:
        if (ms_is_listener(obj) && ms_to_int(idx) == 1) {
            out = *obj;   /* a single entity behaves like a one element list */
            return out;
        }
        vm_error(vm, t, "[] applied to %s", ms_type_name(obj->type));
        return out;
    }
}

/* index chain assignment: root[i1][i2]... = val (takes ownership of val) */
static void store_indexed(ms_vm *vm, ms_thread *t, ms_value *root, ms_value *idx, int n, ms_value val)
{
    ms_value *slot = root;
    for (int k = 0; k < n; k++) {
        if (slot->type == MS_NIL) {
            *slot = ms_array_new(vm);
            if (slot->type != MS_ARRAY)
                goto oom;
        } else if (slot->type == MS_ARRAY) {
            if (slot->u.a->rc > 1) {
                ms_value c = ms_array_copy(vm, slot->u.a);
                if (c.type != MS_ARRAY)
                    goto oom;
                ms_release(vm, *slot);
                *slot = c;
            }
        } else {
            vm_error(vm, t, "cannot index-assign into %s", ms_type_name(slot->type));
            ms_release(vm, val);
            return;
        }
        if (idx[k].type == MS_NIL) {
            vm_error(vm, t, "array index is NIL");
            ms_release(vm, val);
            return;
        }
        slot = ms_array_slot(vm, slot, &idx[k]);
        if (!slot)
            goto oom;
    }
    ms_release(vm, *slot);
    *slot = val;
    return;
oom:
    vm_error(vm, t, "out of memory");
    ms_release(vm, val);
}

/* dispatch one command to one listener; returns 1 if a command ran */
static int dispatch(ms_vm *vm, ms_call *c)
{
    unsigned on;
    switch (c->target.type) {
    case MS_ENTITY: on = MS_ON_ENTITY; break;
    case MS_LEVEL: on = MS_ON_LEVEL; break;
    case MS_GAME: on = MS_ON_GAME; break;
    case MS_THREAD: on = MS_ON_THREAD; break;
    default:
        vm_error(vm, c->thread, "command '%s' applied to %s", ms_name(vm, c->name), ms_type_name(c->target.type));
        return 0;
    }
    if (c->target.type == MS_ENTITY && vm->host.entity_valid && !vm->host.entity_valid(vm->host.user, c->target.u.id)) {
        vm_error(vm, c->thread, "command '%s' applied to a removed entity", ms_name(vm, c->name));
        return 0;
    }
    ms_cmd *cmd = ms_find_cmd(vm, c->name, on);
    if (cmd) {
        c->cmd = cmd;
        cmd->fn(c);
        return 1;
    }
    c->cmd = NULL;
    if (vm->host.unknown_command && vm->host.unknown_command(c))
        return 1;
    vm_error(vm, c->thread, "unknown command '%s' for %s", ms_name(vm, c->name), ms_type_name(c->target.type));
    return 0;
}

static void interpret(ms_vm *vm, ms_thread *t)
{
    const uint8_t *code = t->script->code;
    const char *err;
    for (;;) {
        if (t->state != TS_RUNNING)
            return;
        if (t->error) {
            t->error = 0;
            if (t->ntry > 0) {
                ms_try *tr = &t->trys[--t->ntry];
                while (t->sp > tr->sp)
                    ms_release(vm, pop(t));
                t->pc = tr->catch_pc;
            }
        }
        uint32_t at = t->pc;
        uint8_t op = code[t->pc++];
        switch (op) {
        case OP_NOP: break;
        case OP_PUSH_NIL: push(vm, t, ms_nil()); break;
        case OP_PUSH_NULL: { ms_value v = ms_nil(); v.type = MS_NULL; push(vm, t, v); break; }
        case OP_PUSH_INT: push(vm, t, ms_int((int32_t)rd32(code + t->pc))); t->pc += 4; break;
        case OP_PUSH_I8: push(vm, t, ms_int((int8_t)code[t->pc])); t->pc += 1; break;
        case OP_PUSH_FLOAT: push(vm, t, ms_float(rdf(code + t->pc))); t->pc += 4; break;
        case OP_PUSH_STR: {
            ms_value v;
            v.type = MS_STRING;
            v.u.s = t->script->consts[rd16(code + t->pc)];
            t->pc += 2;
            ms_retain(v);
            push(vm, t, v);
            break;
        }
        case OP_MAKE_VEC: {
            ms_value z = pop(t), y = pop(t), x = pop(t);
            ms_value v = ms_vec(ms_to_float(&x), ms_to_float(&y), ms_to_float(&z));
            if ((x.type != MS_INT && x.type != MS_FLOAT) || (y.type != MS_INT && y.type != MS_FLOAT) ||
                (z.type != MS_INT && z.type != MS_FLOAT))
                vm_error(vm, t, "vector built from non-numbers");
            ms_release(vm, x);
            ms_release(vm, y);
            ms_release(vm, z);
            push(vm, t, v);
            break;
        }
        case OP_PUSH_LISTENER: {
            int sc = code[t->pc++];
            ms_value v = ms_nil();
            switch (sc) {
            case SC_GAME: v.type = MS_GAME; break;
            case SC_LEVEL: v.type = MS_LEVEL; break;
            case SC_LOCAL: v.type = MS_THREAD; v.u.id = t->id; break;
            case SC_SELF: v = t->self; break;
            case SC_OWNER: v = t->self; break;
            default: break;
            }
            push(vm, t, v);
            break;
        }
        case OP_LOAD_SCOPE: {
            int sc = code[t->pc];
            uint32_t name = rd16(code + t->pc + 1);
            t->pc += 3;
            ms_value out = ms_nil();
            if ((sc == SC_LEVEL || sc == SC_GAME)) {
                ms_value l = ms_nil();
                l.type = sc == SC_LEVEL ? MS_LEVEL : MS_GAME;
                out = get_field(vm, t, &l, name);
            } else {
                ms_value *v = ms_table_get(scope_table(vm, t, sc), name);
                if (v) {
                    out = *v;
                    ms_retain(out);
                }
            }
            push(vm, t, out);
            break;
        }
        case OP_STORE_SCOPE: {
            int sc = code[t->pc];
            uint32_t name = rd16(code + t->pc + 1);
            t->pc += 3;
            ms_value v = pop(t);
            if (sc == SC_LEVEL || sc == SC_GAME) {
                ms_value l = ms_nil();
                l.type = sc == SC_LEVEL ? MS_LEVEL : MS_GAME;
                set_field(vm, t, &l, name, v);
            } else {
                ms_value *slot = ms_table_slot(vm, scope_table(vm, t, sc), name);
                if (!slot) {
                    vm_error(vm, t, "out of memory");
                    ms_release(vm, v);
                } else {
                    ms_release(vm, *slot);
                    *slot = v;
                }
            }
            break;
        }
        case OP_GET_FIELD: {
            uint32_t name = rd16(code + t->pc);
            t->pc += 2;
            ms_value obj = pop(t);
            ms_value out = get_field(vm, t, &obj, name);
            ms_release(vm, obj);
            push(vm, t, out);
            break;
        }
        case OP_SET_FIELD: {
            uint32_t name = rd16(code + t->pc);
            t->pc += 2;
            ms_value obj = pop(t);
            ms_value v = pop(t);
            set_field(vm, t, &obj, name, v);
            ms_release(vm, obj);
            break;
        }
        case OP_GET_INDEX: {
            ms_value idx = pop(t), obj = pop(t);
            ms_value out = get_index(vm, t, &obj, &idx);
            ms_release(vm, idx);
            ms_release(vm, obj);
            push(vm, t, out);
            break;
        }
        case OP_STORE_INDEXED: {
            int kind = code[t->pc], sc = code[t->pc + 1];
            uint32_t name = rd16(code + t->pc + 2);
            int n = code[t->pc + 4];
            t->pc += 5;
            ms_value idx[32];
            for (int k = n - 1; k >= 0; k--)
                idx[k] = pop(t);
            ms_value obj = kind == IB_FIELD ? pop(t) : ms_nil();
            ms_value val = pop(t);
            ms_value *root = NULL;
            if (kind == IB_SCOPE && sc != SC_LEVEL && sc != SC_GAME) {
                root = ms_table_slot(vm, scope_table(vm, t, sc), name);
            } else {
                ms_value l = obj;
                if (kind == IB_SCOPE)
                    l.type = sc == SC_LEVEL ? MS_LEVEL : MS_GAME;
                ms_table *tab = listener_table(vm, &l, 1);
                if (!tab)
                    vm_error(vm, t, "cannot index-assign field '%s' of %s", ms_name(vm, name), ms_type_name(l.type));
                else
                    root = ms_table_slot(vm, tab, name);
            }
            if (root)
                store_indexed(vm, t, root, idx, n, val);
            else
                ms_release(vm, val);
            for (int k = 0; k < n; k++)
                ms_release(vm, idx[k]);
            ms_release(vm, obj);
            break;
        }
        case OP_SIZE: {
            ms_value v = pop(t);
            int32_t n = 0;
            switch (v.type) {
            case MS_STRING: n = (int32_t)v.u.s->len; break;
            case MS_ARRAY:
            case MS_CARRAY: n = ms_array_size(&v); break;
            case MS_NIL:
            case MS_NULL: n = 0; break;
            case MS_ENTITY:
            case MS_LEVEL:
            case MS_GAME:
            case MS_THREAD: n = 1; break;
            default: vm_error(vm, t, ".size of %s", ms_type_name(v.type)); break;
            }
            ms_release(vm, v);
            push(vm, t, ms_int(n));
            break;
        }
        case OP_TARGETNAME: {
            ms_value name = pop(t);
            ms_value v = targetname(vm, t, &name);
            ms_release(vm, name);
            push(vm, t, v);
            break;
        }
        case OP_MAKE_CARR: {
            int n = rd16(code + t->pc);
            t->pc += 2;
            ms_value c = ms_carr_new(vm, n);
            for (int k = n - 1; k >= 0; k--) {
                ms_value v = pop(t);
                if (c.type == MS_CARRAY)
                    c.u.c->items[k] = v;
                else
                    ms_release(vm, v);
            }
            push(vm, t, c);
            break;
        }
        case OP_MAKE_ARRAY: {
            int n = rd16(code + t->pc);
            t->pc += 2;
            ms_value a = ms_array_new(vm);
            for (int k = n; k >= 1; k--) {
                ms_value v = pop(t);
                ms_value key = ms_int(k);
                if (a.type == MS_ARRAY)
                    ms_array_set(vm, &a, &key, &v);
                ms_release(vm, v);
            }
            push(vm, t, a);
            break;
        }
        case OP_BIN: {
            int bop = code[t->pc++];
            ms_value b = pop(t), a = pop(t), r;
            if (!ms_binop(vm, bop, &a, &b, &r, &err)) {
                static const char *names[] = {"+", "-", "*", "/", "%", "<<", ">>", "&", "|", "^",
                                              "==", "!=", "<", ">", "<=", ">="};
                vm_error(vm, t, "binary '%s' applied to %s and %s: %s", names[bop], ms_type_name(a.type),
                         ms_type_name(b.type), err);
            }
            ms_release(vm, a);
            ms_release(vm, b);
            push(vm, t, r);
            break;
        }
        case OP_UN: {
            int uop = code[t->pc++];
            ms_value a = pop(t), r;
            if (!ms_unop(vm, uop, &a, &r, &err))
                vm_error(vm, t, "unary operator applied to %s", ms_type_name(a.type));
            ms_release(vm, a);
            push(vm, t, r);
            break;
        }
        case OP_NOT: {
            ms_value a = pop(t);
            int b = !ms_truthy(vm, &a);
            ms_release(vm, a);
            push(vm, t, ms_int(b));
            break;
        }
        case OP_JMP:
        case OP_JF:
        case OP_JT: {
            uint32_t target = rd32(code + t->pc);
            t->pc += 4;
            int take = 1;
            if (op != OP_JMP) {
                ms_value c = pop(t);
                take = ms_truthy(vm, &c) == (op == OP_JT);
                ms_release(vm, c);
            }
            if (take) {
                if (target <= at && ++t->loops > vm->cfg.max_loops) {
                    vm_error(vm, t, "possible infinite loop (%d iterations without a wait), thread killed",
                             vm->cfg.max_loops);
                    finish_thread(vm, t);
                    return;
                }
                t->pc = target;
            }
            break;
        }
        case OP_POP: ms_release(vm, pop(t)); break;
        case OP_DUP: {
            ms_value v = t->sp ? t->stack[t->sp - 1] : ms_nil();
            ms_retain(v);
            push(vm, t, v);
            break;
        }
        case OP_CMD:
        case OP_METHOD: {
            uint32_t name = rd16(code + t->pc);
            int argc = code[t->pc + 2];
            int keep = code[t->pc + 3];
            t->pc += 4;
            ms_call c;
            memset(&c, 0, sizeof(c));
            c.vm = vm;
            c.thread = t;
            c.name = name;
            c.argc = argc;
            int base = t->sp - argc;
            ms_value target = ms_nil();
            if (op == OP_METHOD) {
                target = t->stack[base - 1];
            } else {
                target.type = MS_THREAD;
                target.u.id = t->id;
            }
            c.args = &t->stack[base];
            if (target.type == MS_CARRAY || target.type == MS_ARRAY) {
                /* "$guys hide" applies to every entity in the list */
                ms_value list = target;
                int n = list.type == MS_CARRAY ? list.u.c->n : list.u.a->n;
                for (int k = 0; k < n; k++) {
                    ms_value item = list.type == MS_CARRAY ? list.u.c->items[k] : list.u.a->slots[k].val;
                    if (item.type == MS_NIL)
                        continue;
                    c.target = item;
                    ms_release(vm, c.ret);
                    c.ret = ms_nil();
                    dispatch(vm, &c);
                    if (t->state == TS_DEAD)
                        break;
                }
            } else {
                c.target = target;
                dispatch(vm, &c);
            }
            /* the command may have grown nothing on our stack, but re-read in case */
            for (int k = 0; k < argc; k++)
                ms_release(vm, pop(t));
            if (op == OP_METHOD)
                ms_release(vm, pop(t));
            if (keep)
                push(vm, t, c.ret);
            else
                ms_release(vm, c.ret);
            if (t->state == TS_DEAD)
                return;
            switch (c.wait_kind) {
            case 1:
                t->state = TS_WAIT_TIME;
                t->wake_time = vm->time + c.wait_time;
                return;
            case 2:
                t->state = TS_WAIT_SIGNAL;
                t->wait_listener = c.wait_listener;
                t->wait_name = c.wait_name;
                return;
            case 3:
                t->state = TS_WAIT_THREAD;
                t->wait_thread = c.wait_thread;
                t->result_slot = keep ? t->sp - 1 : -1;
                return;
            case 4:
                t->state = TS_WAIT_FRAME;
                t->wait_frame = vm->frame;
                return;
            case 5:
                finish_thread(vm, t);
                return;
            default:
                break;
            }
            code = t->script->code;   /* goto may not change script, but be safe */
            break;
        }
        case OP_ARG: {
            int k = code[t->pc++];
            ms_value v = k < t->argc ? t->args[k] : ms_nil();
            ms_retain(v);
            push(vm, t, v);
            break;
        }
        case OP_SWITCH: {
            const ms_switch *S = &t->script->switches[rd32(code + t->pc)];
            t->pc += 4;
            ms_value v = pop(t);
            uint32_t target = S->def;
            for (uint32_t k = 0; k < S->n; k++) {
                if (ms_equal(vm, &S->cases[k].key, &v)) {
                    target = S->cases[k].pc;
                    break;
                }
            }
            ms_release(vm, v);
            t->pc = target;
            break;
        }
        case OP_TRY: {
            uint32_t target = rd32(code + t->pc);
            t->pc += 4;
            if (t->ntry == MS_MAX_TRY) {
                vm_error(vm, t, "try blocks nested too deep");
                break;
            }
            t->trys[t->ntry].catch_pc = target;
            t->trys[t->ntry].sp = t->sp;
            t->ntry++;
            break;
        }
        case OP_ENDTRY:
            if (t->ntry > 0)
                t->ntry--;
            break;
        case OP_END:
        default:
            finish_thread(vm, t);
            return;
        }
    }
}

static void run_thread(ms_vm *vm, ms_thread *t)
{
    ms_thread *prev = vm->current;
    t->state = TS_RUNNING;
    t->loops = 0;
    t->running++;
    vm->current = t;
    vm->depth++;
    interpret(vm, t);
    vm->depth--;
    vm->current = prev;
    t->running--;
}
