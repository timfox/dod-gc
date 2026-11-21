/* ms_value.c - memory, names, strings, arrays, tables and operators. MIT licensed. */
#include "ms_internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* memory: every block carries its size so the budget stays exact       */
/* ------------------------------------------------------------------ */

#define HDR 8

static void *raw_alloc(ms_vm *vm, void *p, size_t old_size, size_t new_size)
{
    if (vm->host.alloc)
        return vm->host.alloc(vm->host.user, p, old_size, new_size);
    if (new_size == 0) {
        free(p);
        return NULL;
    }
    return realloc(p, new_size);
}

void *ms_realloc(ms_vm *vm, void *p, size_t new_size)
{
    size_t old = 0;
    char *base = NULL;
    if (p) {
        base = (char *)p - HDR;
        memcpy(&old, base, sizeof(size_t));
    }
    if (new_size == 0) {
        if (base) {
            raw_alloc(vm, base, old + HDR, 0);
            vm->mem_used -= old;
        }
        return NULL;
    }
    if (vm->cfg.memory_budget && vm->mem_used - old + new_size > vm->cfg.memory_budget) {
        vm->mem_failed++;
        return NULL;
    }
    char *nb = (char *)raw_alloc(vm, base, base ? old + HDR : 0, new_size + HDR);
    if (!nb) {
        vm->mem_failed++;
        return NULL;
    }
    memcpy(nb, &new_size, sizeof(size_t));
    vm->mem_used = vm->mem_used - old + new_size;
    if (vm->mem_used > vm->mem_peak)
        vm->mem_peak = vm->mem_used;
    return nb + HDR;
}

void *ms_alloc(ms_vm *vm, size_t size)
{
    void *p = ms_realloc(vm, NULL, size ? size : 1);
    if (p)
        memset(p, 0, size ? size : 1);
    return p;
}

void ms_free(ms_vm *vm, void *p)
{
    if (p)
        ms_realloc(vm, p, 0);
}

char *ms_strdup(ms_vm *vm, const char *s)
{
    size_t n = strlen(s);
    char *d = (char *)ms_alloc(vm, n + 1);
    if (d)
        memcpy(d, s, n + 1);
    return d;
}

void ms_strfree(ms_vm *vm, char *s)
{
    ms_free(vm, s);
}

/* ------------------------------------------------------------------ */
/* names                                                                */
/* ------------------------------------------------------------------ */

uint32_t ms_hash(const char *s, size_t len)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint8_t)s[i];
        h *= 16777619u;
    }
    return h;
}

static int names_grow_index(ms_vm *vm)
{
    ms_names *nm = &vm->names;
    uint32_t ncap = nm->icap ? nm->icap * 2 : 256;
    uint32_t *ni = (uint32_t *)ms_alloc(vm, ncap * sizeof(uint32_t));
    if (!ni)
        return 0;
    for (uint32_t i = 0; i < ncap; i++)
        ni[i] = MS_NAME_NONE;
    for (uint32_t id = 0; id < nm->n; id++) {
        uint32_t j = nm->hash[id] & (ncap - 1);
        while (ni[j] != MS_NAME_NONE)
            j = (j + 1) & (ncap - 1);
        ni[j] = id;
    }
    ms_free(vm, nm->index);
    nm->index = ni;
    nm->icap = ncap;
    return 1;
}

uint32_t ms_intern(ms_vm *vm, const char *s, size_t len)
{
    ms_names *nm = &vm->names;
    uint32_t h = ms_hash(s, len);
    if (nm->icap) {
        uint32_t j = h & (nm->icap - 1);
        while (nm->index[j] != MS_NAME_NONE) {
            uint32_t id = nm->index[j];
            if (nm->hash[id] == h && strlen(nm->str[id]) == len && memcmp(nm->str[id], s, len) == 0)
                return id;
            j = (j + 1) & (nm->icap - 1);
        }
    }
    if ((nm->n + 1) * 2 > nm->icap && !names_grow_index(vm))
        return MS_NAME_NONE;
    if (nm->n == nm->cap) {
        uint32_t ncap = nm->cap ? nm->cap * 2 : 128;
        char **ns = (char **)ms_realloc(vm, nm->str, ncap * sizeof(char *));
        if (!ns)
            return MS_NAME_NONE;
        nm->str = ns;
        uint32_t *nh = (uint32_t *)ms_realloc(vm, nm->hash, ncap * sizeof(uint32_t));
        if (!nh)
            return MS_NAME_NONE;
        nm->hash = nh;
        nm->cap = ncap;
    }
    char *copy = (char *)ms_alloc(vm, len + 1);
    if (!copy)
        return MS_NAME_NONE;
    memcpy(copy, s, len);
    copy[len] = 0;
    uint32_t id = nm->n++;
    nm->str[id] = copy;
    nm->hash[id] = h;
    uint32_t j = h & (nm->icap - 1);
    while (nm->index[j] != MS_NAME_NONE)
        j = (j + 1) & (nm->icap - 1);
    nm->index[j] = id;
    return id;
}

uint32_t ms_intern_lower(ms_vm *vm, const char *s, size_t len)
{
    char buf[128] = {0};
    char *tmp = len < sizeof(buf) ? buf : (char *)ms_alloc(vm, len + 1);
    if (!tmp)
        return MS_NAME_NONE;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        tmp[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    uint32_t id = ms_intern(vm, tmp, len);
    if (tmp != buf)
        ms_free(vm, tmp);
    return id;
}

const char *ms_name(const ms_vm *vm, uint32_t id)
{
    if (id >= vm->names.n)
        return "?";
    return vm->names.str[id];
}

void ms_names_free(ms_vm *vm)
{
    for (uint32_t i = 0; i < vm->names.n; i++)
        ms_free(vm, vm->names.str[i]);
    ms_free(vm, vm->names.str);
    ms_free(vm, vm->names.hash);
    ms_free(vm, vm->names.index);
    memset(&vm->names, 0, sizeof(vm->names));
}

/* ------------------------------------------------------------------ */
/* basic values                                                         */
/* ------------------------------------------------------------------ */

ms_value ms_nil(void)
{
    ms_value v;
    memset(&v, 0, sizeof(v));
    return v;
}

ms_value ms_int(int32_t i)
{
    ms_value v = ms_nil();
    v.type = MS_INT;
    v.u.i = i;
    return v;
}

ms_value ms_float(float f)
{
    ms_value v = ms_nil();
    v.type = MS_FLOAT;
    v.u.f = f;
    return v;
}

ms_value ms_vec(float x, float y, float z)
{
    ms_value v = ms_nil();
    v.type = MS_VECTOR;
    v.u.v[0] = x;
    v.u.v[1] = y;
    v.u.v[2] = z;
    return v;
}

ms_value ms_entity(uint32_t id)
{
    ms_value v = ms_nil();
    v.type = MS_ENTITY;
    v.u.id = id;
    return v;
}

ms_str *ms_str_new(ms_vm *vm, const char *s, size_t len)
{
    ms_str *st = (ms_str *)ms_alloc(vm, sizeof(ms_str) + len);
    if (!st)
        return NULL;
    st->rc = 1;
    st->len = (uint32_t)len;
    if (len)
        memcpy(st->data, s, len);
    st->data[len] = 0;
    st->hash = ms_hash(st->data, len);
    return st;
}

ms_value ms_string_len(ms_vm *vm, const char *s, size_t len)
{
    ms_value v = ms_nil();
    ms_str *st = ms_str_new(vm, s, len);
    if (!st)
        return v;
    v.type = MS_STRING;
    v.u.s = st;
    return v;
}

ms_value ms_string(ms_vm *vm, const char *s)
{
    return ms_string_len(vm, s, strlen(s));
}

void ms_retain(ms_value v)
{
    switch (v.type) {
    case MS_STRING: v.u.s->rc++; break;
    case MS_ARRAY: v.u.a->rc++; break;
    case MS_CARRAY: v.u.c->rc++; break;
    default: break;
    }
}

static void array_free(ms_vm *vm, ms_array *a)
{
    for (int32_t i = 0; i < a->n; i++) {
        ms_release(vm, a->slots[i].key);
        ms_release(vm, a->slots[i].val);
    }
    ms_free(vm, a->slots);
    ms_free(vm, a->index);
    ms_free(vm, a);
}

void ms_release(ms_vm *vm, ms_value v)
{
    switch (v.type) {
    case MS_STRING:
        if (--v.u.s->rc == 0)
            ms_free(vm, v.u.s);
        break;
    case MS_ARRAY:
        if (--v.u.a->rc == 0)
            array_free(vm, v.u.a);
        break;
    case MS_CARRAY:
        if (--v.u.c->rc == 0) {
            for (int32_t i = 0; i < v.u.c->n; i++)
                ms_release(vm, v.u.c->items[i]);
            ms_free(vm, v.u.c);
        }
        break;
    default:
        break;
    }
}

const char *ms_type_name(int type)
{
    static const char *names[] = {"NIL", "NULL", "int", "float", "string", "vector",
                                  "entity", "level", "game", "thread", "array", "const array"};
    if (type < 0 || type > MS_CARRAY)
        return "?";
    return names[type];
}

int ms_is_listener(const ms_value *v)
{
    return v->type == MS_ENTITY || v->type == MS_LEVEL || v->type == MS_GAME || v->type == MS_THREAD;
}

int ms_same_listener(const ms_value *a, const ms_value *b)
{
    if (a->type != b->type)
        return 0;
    if (a->type == MS_ENTITY || a->type == MS_THREAD)
        return a->u.id == b->u.id;
    return a->type == MS_LEVEL || a->type == MS_GAME || a->type == MS_NULL;
}

static int entity_alive(ms_vm *vm, uint32_t id)
{
    return vm->host.entity_valid ? vm->host.entity_valid(vm->host.user, id) : 1;
}

/* parse a string as a number; returns 1 on a full match */
static int str_number(const char *s, float *out, int *is_int)
{
    char *end;
    if (!*s)
        return 0;
    long l = strtol(s, &end, 10);
    if (*end == 0) {
        *out = (float)l;
        *is_int = 1;
        return 1;
    }
    double d = strtod(s, &end);
    if (*end == 0) {
        *out = (float)d;
        *is_int = 0;
        return 1;
    }
    return 0;
}

float ms_to_float(const ms_value *v)
{
    switch (v->type) {
    case MS_INT: return (float)v->u.i;
    case MS_FLOAT: return v->u.f;
    case MS_STRING: {
        float f;
        int ii;
        return str_number(v->u.s->data, &f, &ii) ? f : (float)atof(v->u.s->data);
    }
    default: return 0.0f;
    }
}

int32_t ms_to_int(const ms_value *v)
{
    switch (v->type) {
    case MS_INT: return v->u.i;
    case MS_FLOAT: return (int32_t)v->u.f;
    case MS_STRING: return (int32_t)atoi(v->u.s->data);
    default: return 0;
    }
}

int ms_array_size(const ms_value *arr)
{
    if (arr->type == MS_ARRAY) {
        int n = 0;
        for (int32_t i = 0; i < arr->u.a->n; i++)
            if (arr->u.a->slots[i].val.type != MS_NIL)
                n++;
        return n;
    }
    if (arr->type == MS_CARRAY)
        return arr->u.c->n;
    return 0;
}

const ms_value *ms_array_item(const ms_value *arr, int i)
{
    if (i < 0)
        return NULL;
    if (arr->type == MS_CARRAY)
        return i < arr->u.c->n ? &arr->u.c->items[i] : NULL;
    if (arr->type == MS_ARRAY) {
        for (int32_t k = 0; k < arr->u.a->n; k++) {
            if (arr->u.a->slots[k].val.type == MS_NIL)
                continue;
            if (i-- == 0)
                return &arr->u.a->slots[k].val;
        }
    }
    return NULL;
}

const char *ms_string_text(const ms_value *v)
{
    return v->type == MS_STRING ? v->u.s->data : NULL;
}

int ms_truthy(ms_vm *vm, const ms_value *v)
{
    switch (v->type) {
    case MS_NIL:
    case MS_NULL: return 0;
    case MS_INT: return v->u.i != 0;
    case MS_FLOAT: return v->u.f != 0.0f;
    case MS_STRING: return v->u.s->len != 0;
    case MS_VECTOR: return v->u.v[0] != 0.0f || v->u.v[1] != 0.0f || v->u.v[2] != 0.0f;
    case MS_ENTITY: return entity_alive(vm, v->u.id);
    case MS_THREAD: return ms_find_thread(vm, v->u.id) != NULL;
    case MS_ARRAY:
    case MS_CARRAY: return ms_array_size(v) != 0;
    default: return 1;
    }
}

const char *ms_tostring(const ms_value *v, char *buf, size_t size)
{
    switch (v->type) {
    case MS_NIL: snprintf(buf, size, "NIL"); break;
    case MS_NULL: snprintf(buf, size, "NULL"); break;
    case MS_INT: snprintf(buf, size, "%d", (int)v->u.i); break;
    case MS_FLOAT: snprintf(buf, size, "%.3f", (double)v->u.f); break;
    case MS_STRING: snprintf(buf, size, "%s", v->u.s->data); break;
    case MS_VECTOR:
        snprintf(buf, size, "(%.3f %.3f %.3f)", (double)v->u.v[0], (double)v->u.v[1], (double)v->u.v[2]);
        break;
    case MS_ENTITY: snprintf(buf, size, "entity#%u", (unsigned)v->u.id); break;
    case MS_LEVEL: snprintf(buf, size, "level"); break;
    case MS_GAME: snprintf(buf, size, "game"); break;
    case MS_THREAD: snprintf(buf, size, "thread#%u", (unsigned)v->u.id); break;
    case MS_ARRAY: snprintf(buf, size, "array[%d]", ms_array_size(v)); break;
    case MS_CARRAY: snprintf(buf, size, "const array[%d]", (int)v->u.c->n); break;
    default: snprintf(buf, size, "?"); break;
    }
    return buf;
}

ms_value ms_to_string_value(ms_vm *vm, const ms_value *v)
{
    if (v->type == MS_STRING) {
        ms_retain(*v);
        return *v;
    }
    char buf[96];
    return ms_string(vm, ms_tostring(v, buf, sizeof(buf)));
}

/* ------------------------------------------------------------------ */
/* const arrays                                                         */
/* ------------------------------------------------------------------ */

ms_value ms_carr_new(ms_vm *vm, int n)
{
    ms_value v = ms_nil();
    ms_carr *c = (ms_carr *)ms_alloc(vm, sizeof(ms_carr) + sizeof(ms_value) * (size_t)(n > 0 ? n - 1 : 0));
    if (!c)
        return v;
    c->rc = 1;
    c->n = n;
    v.type = MS_CARRAY;
    v.u.c = c;
    return v;
}

/* ------------------------------------------------------------------ */
/* associative arrays                                                   */
/* ------------------------------------------------------------------ */

static uint32_t key_hash(const ms_value *k)
{
    switch (k->type) {
    case MS_INT: return (uint32_t)k->u.i * 2654435761u;
    case MS_FLOAT: {
        /* integral floats hash like ints so 1 and 1.0 find the same slot */
        float f = k->u.f;
        if (f == (float)(int32_t)f)
            return (uint32_t)(int32_t)f * 2654435761u;
        uint32_t b;
        memcpy(&b, &f, 4);
        return b * 2654435761u;
    }
    case MS_STRING: {
        /* numeric strings hash like their number ("1" == 1 as a key) */
        float f;
        int ii;
        if (str_number(k->u.s->data, &f, &ii) && f == (float)(int32_t)f)
            return (uint32_t)(int32_t)f * 2654435761u;
        return k->u.s->hash;
    }
    case MS_ENTITY:
    case MS_THREAD: return k->u.id * 40503u + k->type;
    default: return k->type;
    }
}

ms_value ms_array_new(ms_vm *vm)
{
    ms_value v = ms_nil();
    ms_array *a = (ms_array *)ms_alloc(vm, sizeof(ms_array));
    if (!a)
        return v;
    a->rc = 1;
    v.type = MS_ARRAY;
    v.u.a = a;
    return v;
}

static int array_reindex(ms_vm *vm, ms_array *a, int32_t icap)
{
    int32_t *ni = (int32_t *)ms_alloc(vm, sizeof(int32_t) * (size_t)icap);
    if (!ni)
        return 0;
    for (int32_t i = 0; i < icap; i++)
        ni[i] = -1;
    for (int32_t s = 0; s < a->n; s++) {
        uint32_t j = key_hash(&a->slots[s].key) & (uint32_t)(icap - 1);
        while (ni[j] >= 0)
            j = (j + 1) & (uint32_t)(icap - 1);
        ni[j] = s;
    }
    ms_free(vm, a->index);
    a->index = ni;
    a->icap = icap;
    return 1;
}

static int key_equal(const ms_value *a, const ms_value *b)
{
    if ((a->type == MS_INT || a->type == MS_FLOAT || a->type == MS_STRING) &&
        (b->type == MS_INT || b->type == MS_FLOAT || b->type == MS_STRING)) {
        if (a->type == MS_STRING && b->type == MS_STRING)
            return a->u.s->len == b->u.s->len && memcmp(a->u.s->data, b->u.s->data, a->u.s->len) == 0;
        float fa, fb;
        int ia, ib;
        if (a->type == MS_STRING) {
            if (!str_number(a->u.s->data, &fa, &ia))
                return 0;
        } else
            fa = ms_to_float(a);
        if (b->type == MS_STRING) {
            if (!str_number(b->u.s->data, &fb, &ib))
                return 0;
        } else
            fb = ms_to_float(b);
        return fa == fb;
    }
    return ms_same_listener(a, b);
}

ms_value *ms_array_get(const ms_value *arr, const ms_value *key)
{
    if (arr->type != MS_ARRAY || !arr->u.a->icap)
        return NULL;
    const ms_array *a = arr->u.a;
    uint32_t j = key_hash(key) & (uint32_t)(a->icap - 1);
    while (a->index[j] >= 0) {
        ms_slot *s = &a->slots[a->index[j]];
        if (key_equal(&s->key, key))
            return &s->val;
        j = (j + 1) & (uint32_t)(a->icap - 1);
    }
    return NULL;
}

ms_value *ms_array_slot(ms_vm *vm, ms_value *arr, const ms_value *key)
{
    ms_value *found = ms_array_get(arr, key);
    if (found)
        return found;
    ms_array *a = arr->u.a;
    if ((a->n + 1) * 2 > a->icap && !array_reindex(vm, a, a->icap ? a->icap * 2 : 8))
        return NULL;
    if (a->n == a->cap) {
        int32_t ncap = a->cap ? a->cap * 2 : 4;
        ms_slot *ns = (ms_slot *)ms_realloc(vm, a->slots, sizeof(ms_slot) * (size_t)ncap);
        if (!ns)
            return NULL;
        a->slots = ns;
        a->cap = ncap;
    }
    ms_slot *s = &a->slots[a->n];
    s->key = *key;
    ms_retain(s->key);
    s->val = ms_nil();
    uint32_t j = key_hash(key) & (uint32_t)(a->icap - 1);
    while (a->index[j] >= 0)
        j = (j + 1) & (uint32_t)(a->icap - 1);
    a->index[j] = a->n++;
    return &s->val;
}

ms_value ms_array_copy(ms_vm *vm, const ms_array *src)
{
    ms_value v = ms_array_new(vm);
    if (v.type != MS_ARRAY)
        return v;
    for (int32_t i = 0; i < src->n; i++) {
        ms_value *d = ms_array_slot(vm, &v, &src->slots[i].key);
        if (!d)
            break;
        *d = src->slots[i].val;
        ms_retain(*d);
    }
    return v;
}

int ms_array_set(ms_vm *vm, ms_value *arr, const ms_value *key, const ms_value *val)
{
    if (arr->type != MS_ARRAY)
        return 0;
    if (arr->u.a->rc > 1) {
        ms_value c = ms_array_copy(vm, arr->u.a);
        ms_release(vm, *arr);
        *arr = c;
        if (c.type != MS_ARRAY)
            return 0;
    }
    ms_value *slot = ms_array_slot(vm, arr, key);
    if (!slot)
        return 0;
    ms_release(vm, *slot);
    *slot = *val;
    ms_retain(*slot);
    return 1;
}

/* ------------------------------------------------------------------ */
/* tables (name id -> value)                                            */
/* ------------------------------------------------------------------ */

ms_value *ms_table_get(const ms_table *t, uint32_t key)
{
    if (!t->cap)
        return NULL;
    uint32_t j = (key * 2654435761u) & (t->cap - 1);
    while (t->keys[j] != MS_NAME_NONE) {
        if (t->keys[j] == key)
            return &t->vals[j];
        j = (j + 1) & (t->cap - 1);
    }
    return NULL;
}

static int table_grow(ms_vm *vm, ms_table *t)
{
    uint32_t ncap = t->cap ? t->cap * 2 : 8;
    uint32_t *nk = (uint32_t *)ms_alloc(vm, sizeof(uint32_t) * ncap);
    ms_value *nv = (ms_value *)ms_alloc(vm, sizeof(ms_value) * ncap);
    if (!nk || !nv) {
        ms_free(vm, nk);
        ms_free(vm, nv);
        return 0;
    }
    for (uint32_t i = 0; i < ncap; i++)
        nk[i] = MS_NAME_NONE;
    for (uint32_t i = 0; i < t->cap; i++) {
        if (t->keys[i] == MS_NAME_NONE)
            continue;
        uint32_t j = (t->keys[i] * 2654435761u) & (ncap - 1);
        while (nk[j] != MS_NAME_NONE)
            j = (j + 1) & (ncap - 1);
        nk[j] = t->keys[i];
        nv[j] = t->vals[i];
    }
    ms_free(vm, t->keys);
    ms_free(vm, t->vals);
    t->keys = nk;
    t->vals = nv;
    t->cap = ncap;
    return 1;
}

ms_value *ms_table_slot(ms_vm *vm, ms_table *t, uint32_t key)
{
    ms_value *v = ms_table_get(t, key);
    if (v)
        return v;
    if ((t->n + 1) * 4 > t->cap * 3 && !table_grow(vm, t))
        return NULL;
    uint32_t j = (key * 2654435761u) & (t->cap - 1);
    while (t->keys[j] != MS_NAME_NONE)
        j = (j + 1) & (t->cap - 1);
    t->keys[j] = key;
    t->vals[j] = ms_nil();
    t->n++;
    return &t->vals[j];
}

void ms_table_free(ms_vm *vm, ms_table *t)
{
    for (uint32_t i = 0; i < t->cap; i++)
        if (t->keys[i] != MS_NAME_NONE)
            ms_release(vm, t->vals[i]);
    ms_free(vm, t->keys);
    ms_free(vm, t->vals);
    memset(t, 0, sizeof(*t));
}

ms_table *ms_entity_vars(ms_vm *vm, uint32_t id, int create)
{
    for (ms_entvars *e = vm->entvars; e; e = e->next)
        if (e->id == id)
            return &e->vars;
    if (!create)
        return NULL;
    ms_entvars *e = (ms_entvars *)ms_alloc(vm, sizeof(ms_entvars));
    if (!e)
        return NULL;
    e->id = id;
    e->next = vm->entvars;
    vm->entvars = e;
    return &e->vars;
}

/* ------------------------------------------------------------------ */
/* operators                                                            */
/* ------------------------------------------------------------------ */

static int is_num(const ms_value *v)
{
    return v->type == MS_INT || v->type == MS_FLOAT;
}

int ms_equal(ms_vm *vm, const ms_value *a, const ms_value *b)
{
    if (a->type == MS_NIL || b->type == MS_NIL)
        return a->type == b->type;
    if (a->type == MS_NULL || b->type == MS_NULL) {
        const ms_value *o = a->type == MS_NULL ? b : a;
        if (o->type == MS_NULL)
            return 1;
        if (o->type == MS_ENTITY)
            return !entity_alive(vm, o->u.id);
        if (o->type == MS_THREAD)
            return ms_find_thread(vm, o->u.id) == NULL;
        return 0;
    }
    if (is_num(a) && is_num(b))
        return ms_to_float(a) == ms_to_float(b);
    if (a->type == MS_STRING && b->type == MS_STRING)
        return a->u.s->len == b->u.s->len && memcmp(a->u.s->data, b->u.s->data, a->u.s->len) == 0;
    if ((a->type == MS_STRING && is_num(b)) || (b->type == MS_STRING && is_num(a))) {
        const ms_value *s = a->type == MS_STRING ? a : b;
        const ms_value *n = a->type == MS_STRING ? b : a;
        float f;
        int ii;
        return str_number(s->u.s->data, &f, &ii) && f == ms_to_float(n);
    }
    if (a->type == MS_VECTOR && b->type == MS_VECTOR)
        return a->u.v[0] == b->u.v[0] && a->u.v[1] == b->u.v[1] && a->u.v[2] == b->u.v[2];
    if (a->type == MS_ARRAY && b->type == MS_ARRAY)
        return a->u.a == b->u.a;
    if (a->type == MS_CARRAY && b->type == MS_CARRAY)
        return a->u.c == b->u.c;
    return ms_same_listener(a, b);
}

int ms_compare(ms_vm *vm, const ms_value *a, const ms_value *b, int *out)
{
    (void)vm;
    if (is_num(a) && is_num(b)) {
        if (a->type == MS_INT && b->type == MS_INT)
            *out = (a->u.i > b->u.i) - (a->u.i < b->u.i);
        else {
            float fa = ms_to_float(a), fb = ms_to_float(b);
            *out = (fa > fb) - (fa < fb);
        }
        return 1;
    }
    if (a->type == MS_STRING && b->type == MS_STRING) {
        int c = strcmp(a->u.s->data, b->u.s->data);
        *out = (c > 0) - (c < 0);
        return 1;
    }
    return 0;
}

static ms_value concat(ms_vm *vm, const ms_value *a, const ms_value *b)
{
    char ba[96], bb[96];
    const char *sa = a->type == MS_STRING ? a->u.s->data : ms_tostring(a, ba, sizeof(ba));
    const char *sb = b->type == MS_STRING ? b->u.s->data : ms_tostring(b, bb, sizeof(bb));
    size_t la = a->type == MS_STRING ? a->u.s->len : strlen(sa);
    size_t lb = b->type == MS_STRING ? b->u.s->len : strlen(sb);
    ms_value v = ms_nil();
    ms_str *s = (ms_str *)ms_alloc(vm, sizeof(ms_str) + la + lb);
    if (!s)
        return v;
    s->rc = 1;
    s->len = (uint32_t)(la + lb);
    memcpy(s->data, sa, la);
    memcpy(s->data + la, sb, lb);
    s->data[la + lb] = 0;
    s->hash = ms_hash(s->data, s->len);
    v.type = MS_STRING;
    v.u.s = s;
    return v;
}

int ms_binop(ms_vm *vm, int op, const ms_value *a, const ms_value *b, ms_value *out, const char **err)
{
    *out = ms_nil();
    *err = NULL;
    int c;
    switch (op) {
    case BO_EQ: *out = ms_int(ms_equal(vm, a, b)); return 1;
    case BO_NE: *out = ms_int(!ms_equal(vm, a, b)); return 1;
    case BO_LT:
    case BO_GT:
    case BO_LE:
    case BO_GE:
        if (!ms_compare(vm, a, b, &c))
            break;
        *out = ms_int(op == BO_LT ? c < 0 : op == BO_GT ? c > 0 : op == BO_LE ? c <= 0 : c >= 0);
        return 1;
    case BO_ADD:
        if (a->type == MS_STRING || b->type == MS_STRING) {
            *out = concat(vm, a, b);
            return out->type == MS_STRING;
        }
        /* fall through */
    case BO_SUB:
    case BO_MUL:
    case BO_DIV:
        if (a->type == MS_INT && b->type == MS_INT) {
            int32_t x = a->u.i, y = b->u.i;
            switch (op) {
            case BO_ADD: *out = ms_int(x + y); return 1;
            case BO_SUB: *out = ms_int(x - y); return 1;
            case BO_MUL: *out = ms_int(x * y); return 1;
            default:
                if (y == 0) {
                    *err = "division by zero";
                    return 0;
                }
                *out = ms_int(x / y);
                return 1;
            }
        }
        if (is_num(a) && is_num(b)) {
            float x = ms_to_float(a), y = ms_to_float(b);
            switch (op) {
            case BO_ADD: *out = ms_float(x + y); return 1;
            case BO_SUB: *out = ms_float(x - y); return 1;
            case BO_MUL: *out = ms_float(x * y); return 1;
            default:
                if (y == 0.0f) {
                    *err = "division by zero";
                    return 0;
                }
                *out = ms_float(x / y);
                return 1;
            }
        }
        if (a->type == MS_VECTOR && b->type == MS_VECTOR && (op == BO_ADD || op == BO_SUB)) {
            float s = op == BO_ADD ? 1.0f : -1.0f;
            *out = ms_vec(a->u.v[0] + s * b->u.v[0], a->u.v[1] + s * b->u.v[1], a->u.v[2] + s * b->u.v[2]);
            return 1;
        }
        if (op == BO_MUL && ((a->type == MS_VECTOR && is_num(b)) || (b->type == MS_VECTOR && is_num(a)))) {
            const ms_value *v = a->type == MS_VECTOR ? a : b;
            float s = ms_to_float(a->type == MS_VECTOR ? b : a);
            *out = ms_vec(v->u.v[0] * s, v->u.v[1] * s, v->u.v[2] * s);
            return 1;
        }
        if (op == BO_DIV && a->type == MS_VECTOR && is_num(b)) {
            float s = ms_to_float(b);
            if (s == 0.0f) {
                *err = "division by zero";
                return 0;
            }
            *out = ms_vec(a->u.v[0] / s, a->u.v[1] / s, a->u.v[2] / s);
            return 1;
        }
        break;
    case BO_MOD:
        if (a->type == MS_INT && b->type == MS_INT) {
            if (b->u.i == 0) {
                *err = "modulus by zero";
                return 0;
            }
            *out = ms_int(a->u.i % b->u.i);
            return 1;
        }
        if (is_num(a) && is_num(b)) {
            float y = ms_to_float(b);
            if (y == 0.0f) {
                *err = "modulus by zero";
                return 0;
            }
            *out = ms_float(fmodf(ms_to_float(a), y));
            return 1;
        }
        break;
    case BO_SHL:
    case BO_SHR:
    case BO_AND:
    case BO_OR:
    case BO_XOR:
        if (is_num(a) && is_num(b)) {
            int32_t x = ms_to_int(a), y = ms_to_int(b);
            switch (op) {
            case BO_SHL: *out = ms_int((int32_t)((uint32_t)x << (y & 31))); break;
            case BO_SHR: *out = ms_int(x >> (y & 31)); break;
            case BO_AND: *out = ms_int(x & y); break;
            case BO_OR: *out = ms_int(x | y); break;
            default: *out = ms_int(x ^ y); break;
            }
            return 1;
        }
        break;
    }
    *err = "incompatible types";
    return 0;
}

int ms_unop(ms_vm *vm, int op, const ms_value *a, ms_value *out, const char **err)
{
    (void)vm;
    *err = NULL;
    *out = ms_nil();
    switch (op) {
    case UO_NEG:
        if (a->type == MS_INT) { *out = ms_int(-a->u.i); return 1; }
        if (a->type == MS_FLOAT) { *out = ms_float(-a->u.f); return 1; }
        if (a->type == MS_VECTOR) { *out = ms_vec(-a->u.v[0], -a->u.v[1], -a->u.v[2]); return 1; }
        break;
    case UO_COMPL:
        if (is_num(a)) { *out = ms_int(~ms_to_int(a)); return 1; }
        break;
    case UO_INC:
    case UO_DEC: {
        int d = op == UO_INC ? 1 : -1;
        if (a->type == MS_INT) { *out = ms_int(a->u.i + d); return 1; }
        if (a->type == MS_FLOAT) { *out = ms_float(a->u.f + (float)d); return 1; }
        if (a->type == MS_NIL) { *out = ms_int(d); return 1; }
        break;
    }
    }
    *err = "incompatible type";
    return 0;
}
