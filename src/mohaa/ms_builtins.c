/*
 * ms_builtins.c - the ScriptThread commands every MOHAA script relies on:
 * flow (wait, thread, waitthread, exec, goto, end, waittill), printing,
 * math, vectors and type helpers. Game-specific commands (spawn, trigger,
 * entity movement, sound, HUD) are registered by the host. MIT licensed.
 */
#include "ms_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define DEG2RAD(a) ((a) * 0.017453292519943295f)
#define RAD2DEG(a) ((a) * 57.29577951308232f)

/* ------------------------------------------------------------------ */
/* flow                                                                 */
/* ------------------------------------------------------------------ */

static int bi_wait(ms_call *c)
{
    ms_call_wait_time(c, ms_arg_float(c, 0));
    return 0;
}

static int bi_waitframe(ms_call *c)
{
    c->wait_kind = 4;
    return 0;
}

static int bi_end(ms_call *c)
{
    ms_thread *t = c->thread;
    /* "local.t end" from another thread ends that thread */
    if (c->target.type == MS_THREAD && t && c->target.u.id != t->id) {
        ms_thread *o = ms_find_thread(c->vm, c->target.u.id);
        if (o && !o->running)
            ms_kill_thread(c->vm, o);
        return 0;
    }
    if (!t)
        return 0;
    ms_release(c->vm, t->retval);
    t->retval = *ms_arg(c, 0);
    ms_retain(t->retval);
    c->wait_kind = 5;
    return 0;
}

static int bi_goto(ms_call *c)
{
    ms_thread *t = c->thread;
    const char *name = ms_arg_string(c, 0);
    const ms_label *lab = ms_find_label(t->script, ms_intern_lower(c->vm, name, strlen(name)));
    if (!lab)
        return ms_call_error(c, "label '%s' not found", name);
    t->pc = lab->body;
    return 0;
}

static int bi_waittill(ms_call *c)
{
    if (ms_argc(c) < 1)
        return ms_call_error(c, "needs an event name");
    ms_call_wait_signal(c, &c->target, ms_arg_string(c, 0));
    return 0;
}

/* resolve "label", "file::label" or file::label (const array) */
static int resolve(ms_call *c, int is_exec, ms_script **out_s, const ms_label **out_l)
{
    ms_vm *vm = c->vm;
    const ms_value *a = ms_arg(c, 0);
    char file[256] = "", label[128] = "";
    if (a->type == MS_CARRAY && a->u.c->n == 2) {
        char b1[256], b2[128];
        snprintf(file, sizeof(file), "%s", ms_tostring(&a->u.c->items[0], b1, sizeof(b1)));
        snprintf(label, sizeof(label), "%s", ms_tostring(&a->u.c->items[1], b2, sizeof(b2)));
    } else if (a->type == MS_STRING) {
        const char *s = a->u.s->data;
        const char *dc = strstr(s, "::");
        if (dc) {
            snprintf(file, sizeof(file), "%.*s", (int)(dc - s), s);
            snprintf(label, sizeof(label), "%s", dc + 2);
        } else if (is_exec)
            snprintf(file, sizeof(file), "%s", s);
        else
            snprintf(label, sizeof(label), "%s", s);
    } else {
        ms_call_error(c, "bad thread target (%s)", ms_type_name(a->type));
        return 0;
    }
    ms_script *s = c->thread ? c->thread->script : NULL;
    if (file[0]) {
        s = ms_get_script(vm, file);
        if (!s) {
            ms_call_error(c, "cannot load '%s'", file);
            return 0;
        }
    }
    if (!s) {
        ms_call_error(c, "no script to run '%s' in", label);
        return 0;
    }
    const ms_label *l = NULL;
    if (label[0]) {
        l = ms_find_label(s, ms_intern_lower(vm, label, strlen(label)));
        if (!l) {
            ms_call_error(c, "label '%s' not found in %s", label, s->path);
            return 0;
        }
    }
    *out_s = s;
    *out_l = l;
    return 1;
}

static int start(ms_call *c, int is_exec, int wait)
{
    ms_script *s;
    const ms_label *l;
    if (!resolve(c, is_exec, &s, &l))
        return 1;
    ms_thread *parent = c->thread;
    const ms_value *self = c->target.type == MS_THREAD ? (parent ? &parent->self : NULL) : &c->target;
    ms_group *group = (!is_exec && parent && parent->script == s) ? parent->group : NULL;
    int argc = ms_argc(c) - 1;
    ms_thread *t = ms_spawn_thread(c->vm, s, l, self, group, argc > 0 ? c->args + 1 : NULL, argc);
    if (!t)
        return 1;
    uint32_t id = t->id;
    ms_start_thread(c->vm, t);
    ms_thread *alive = ms_find_thread(c->vm, id);
    if (!wait) {
        ms_value h = ms_nil();
        h.type = MS_THREAD;
        h.u.id = id;
        ms_return(c, &h);
        return 0;
    }
    if (!alive) {
        ms_return(c, &t->retval);   /* finished already; freed only by the sweep */
        return 0;
    }
    c->wait_kind = 3;
    c->wait_thread = id;
    return 0;
}

static int bi_thread(ms_call *c) { return start(c, 0, 0); }
static int bi_waitthread(ms_call *c) { return start(c, 0, 1); }
static int bi_exec(ms_call *c) { return start(c, 1, 0); }
static int bi_waitexec(ms_call *c) { return start(c, 1, 1); }

static int bi_self(ms_call *c)
{
    ms_return(c, ms_call_self(c));
    return 0;
}

/* ------------------------------------------------------------------ */
/* printing                                                             */
/* ------------------------------------------------------------------ */

static void print_args(ms_call *c, int channel, int newline)
{
    char out[1024];
    size_t o = 0;
    for (int i = 0; i < ms_argc(c) && o + 1 < sizeof(out); i++) {
        const char *s = ms_arg_string(c, i);
        size_t n = strlen(s);
        if (n > sizeof(out) - 2 - o)
            n = sizeof(out) - 2 - o;
        memcpy(out + o, s, n);
        o += n;
    }
    if (newline)
        out[o++] = '\n';
    out[o] = 0;
    ms_vm *vm = c->vm;
    if (vm->host.print)
        vm->host.print(vm->host.user, channel, out);
    else
        fputs(out, stdout);
}

static int bi_print(ms_call *c) { print_args(c, MS_PRINT_SCRIPT, 0); return 0; }
static int bi_println(ms_call *c) { print_args(c, MS_PRINT_SCRIPT, 1); return 0; }
static int bi_dprintln(ms_call *c) { print_args(c, MS_PRINT_DEBUG, 1); return 0; }
static int bi_iprintln(ms_call *c) { print_args(c, MS_PRINT_HUD, 1); return 0; }
static int bi_iprintlnbold(ms_call *c) { print_args(c, MS_PRINT_HUD_BOLD, 1); return 0; }

static int bi_error(ms_call *c)
{
    return ms_call_error(c, "%s", ms_arg_string(c, 0));
}

static int bi_assert(ms_call *c)
{
    if (!ms_truthy(c->vm, ms_arg(c, 0)))
        return ms_call_error(c, "assertion failed");
    return 0;
}

/* ------------------------------------------------------------------ */
/* math                                                                 */
/* ------------------------------------------------------------------ */

static int bi_randomint(ms_call *c)
{
    int32_t n = ms_arg_int(c, 0);
    ms_return_int(c, n > 0 ? (int32_t)(ms_random01(c->vm) * (float)n) % n : 0);
    return 0;
}

static int bi_randomfloat(ms_call *c)
{
    ms_return_float(c, ms_random01(c->vm) * ms_arg_float(c, 0));
    return 0;
}

static int bi_abs(ms_call *c)
{
    const ms_value *v = ms_arg(c, 0);
    if (v->type == MS_INT)
        ms_return_int(c, v->u.i < 0 ? -v->u.i : v->u.i);
    else
        ms_return_float(c, fabsf(ms_to_float(v)));
    return 0;
}

static int bi_int(ms_call *c) { ms_return_int(c, ms_arg_int(c, 0)); return 0; }
static int bi_float(ms_call *c) { ms_return_float(c, ms_arg_float(c, 0)); return 0; }

static int bi_string(ms_call *c)
{
    ms_value s = ms_to_string_value(c->vm, ms_arg(c, 0));
    ms_return(c, &s);
    ms_release(c->vm, s);
    return 0;
}

static int bi_sin(ms_call *c) { ms_return_float(c, sinf(DEG2RAD(ms_arg_float(c, 0)))); return 0; }
static int bi_cos(ms_call *c) { ms_return_float(c, cosf(DEG2RAD(ms_arg_float(c, 0)))); return 0; }
static int bi_tan(ms_call *c) { ms_return_float(c, tanf(DEG2RAD(ms_arg_float(c, 0)))); return 0; }
static int bi_atan(ms_call *c)
{
    if (ms_argc(c) >= 2)
        ms_return_float(c, RAD2DEG(atan2f(ms_arg_float(c, 0), ms_arg_float(c, 1))));
    else
        ms_return_float(c, RAD2DEG(atanf(ms_arg_float(c, 0))));
    return 0;
}
static int bi_sqrt(ms_call *c)
{
    float f = ms_arg_float(c, 0);
    if (f < 0.0f)
        return ms_call_error(c, "negative argument");
    ms_return_float(c, sqrtf(f));
    return 0;
}

/* ------------------------------------------------------------------ */
/* vectors                                                              */
/* ------------------------------------------------------------------ */

static int bi_vector_length(ms_call *c)
{
    float v[3];
    ms_arg_vector(c, 0, v);
    ms_return_float(c, sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
    return 0;
}

static int bi_vector_normalize(ms_call *c)
{
    float v[3];
    ms_arg_vector(c, 0, v);
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (l > 0.0f) {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    }
    ms_return_vector(c, v);
    return 0;
}

static int bi_vector_add(ms_call *c)
{
    float a[3], b[3];
    ms_arg_vector(c, 0, a);
    ms_arg_vector(c, 1, b);
    float r[3] = {a[0] + b[0], a[1] + b[1], a[2] + b[2]};
    ms_return_vector(c, r);
    return 0;
}

static int bi_vector_subtract(ms_call *c)
{
    float a[3], b[3];
    ms_arg_vector(c, 0, a);
    ms_arg_vector(c, 1, b);
    float r[3] = {a[0] - b[0], a[1] - b[1], a[2] - b[2]};
    ms_return_vector(c, r);
    return 0;
}

static int bi_vector_scale(ms_call *c)
{
    float a[3];
    ms_arg_vector(c, 0, a);
    float s = ms_arg_float(c, 1);
    float r[3] = {a[0] * s, a[1] * s, a[2] * s};
    ms_return_vector(c, r);
    return 0;
}

static int bi_vector_dot(ms_call *c)
{
    float a[3], b[3];
    ms_arg_vector(c, 0, a);
    ms_arg_vector(c, 1, b);
    ms_return_float(c, a[0] * b[0] + a[1] * b[1] + a[2] * b[2]);
    return 0;
}

static int bi_vector_cross(ms_call *c)
{
    float a[3], b[3];
    ms_arg_vector(c, 0, a);
    ms_arg_vector(c, 1, b);
    float r[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
    ms_return_vector(c, r);
    return 0;
}

static int bi_vector_within(ms_call *c)
{
    float a[3], b[3];
    ms_arg_vector(c, 0, a);
    ms_arg_vector(c, 1, b);
    float d = ms_arg_float(c, 2);
    float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
    ms_return_int(c, x * x + y * y + z * z <= d * d);
    return 0;
}

static int bi_vector_toangles(ms_call *c)
{
    float v[3], ang[3] = {0, 0, 0};
    ms_arg_vector(c, 0, v);
    if (v[0] == 0.0f && v[1] == 0.0f) {
        ang[0] = v[2] > 0.0f ? 270.0f : 90.0f;
    } else {
        float yaw = RAD2DEG(atan2f(v[1], v[0]));
        if (yaw < 0.0f)
            yaw += 360.0f;
        float fwd = sqrtf(v[0] * v[0] + v[1] * v[1]);
        float pitch = RAD2DEG(atan2f(v[2], fwd));
        if (pitch < 0.0f)
            pitch += 360.0f;
        ang[0] = -pitch < 0.0f ? 360.0f - pitch : -pitch;
        if (ang[0] >= 360.0f)
            ang[0] -= 360.0f;
        ang[1] = yaw;
    }
    ms_return_vector(c, ang);
    return 0;
}

static void angle_vectors(const float ang[3], float f[3], float r[3], float u[3])
{
    float sp = sinf(DEG2RAD(ang[0])), cp = cosf(DEG2RAD(ang[0]));
    float sy = sinf(DEG2RAD(ang[1])), cy = cosf(DEG2RAD(ang[1]));
    float sr = sinf(DEG2RAD(ang[2])), cr = cosf(DEG2RAD(ang[2]));
    f[0] = cp * cy;
    f[1] = cp * sy;
    f[2] = -sp;
    r[0] = -sr * sp * cy + cr * sy;   /* this is "left" */
    r[1] = -sr * sp * sy - cr * cy;
    r[2] = -sr * cp;
    r[0] = -r[0];
    r[1] = -r[1];
    r[2] = -r[2];
    u[0] = cr * sp * cy + sr * sy;
    u[1] = cr * sp * sy - sr * cy;
    u[2] = cr * cp;
}

static int bi_angles_toforward(ms_call *c)
{
    float a[3], f[3], l[3], u[3];
    ms_arg_vector(c, 0, a);
    angle_vectors(a, f, l, u);
    ms_return_vector(c, f);
    return 0;
}

static int bi_angles_toleft(ms_call *c)
{
    float a[3], f[3], l[3], u[3];
    ms_arg_vector(c, 0, a);
    angle_vectors(a, f, l, u);
    ms_return_vector(c, l);
    return 0;
}

static int bi_angles_toup(ms_call *c)
{
    float a[3], f[3], l[3], u[3];
    ms_arg_vector(c, 0, a);
    angle_vectors(a, f, l, u);
    ms_return_vector(c, u);
    return 0;
}

/* ------------------------------------------------------------------ */
/* types and arrays                                                     */
/* ------------------------------------------------------------------ */

static int bi_isdefined(ms_call *c)
{
    const ms_value *v = ms_arg(c, 0);
    int d = v->type != MS_NIL && v->type != MS_NULL;
    if (v->type == MS_ENTITY || v->type == MS_THREAD)
        d = ms_truthy(c->vm, v);
    ms_return_int(c, d);
    return 0;
}

static int bi_isarray(ms_call *c)
{
    const ms_value *v = ms_arg(c, 0);
    ms_return_int(c, v->type == MS_ARRAY || v->type == MS_CARRAY);
    return 0;
}

static int bi_typeof(ms_call *c)
{
    ms_return_string(c, ms_type_name(ms_arg(c, 0)->type));
    return 0;
}

static int array_pairs(ms_call *c, int keys)
{
    const ms_value *a = ms_arg(c, 0);
    ms_value out = ms_array_new(c->vm);
    if (out.type != MS_ARRAY)
        return ms_call_error(c, "out of memory");
    int k = 0;
    if (a->type == MS_ARRAY) {
        for (int32_t i = 0; i < a->u.a->n; i++) {
            if (a->u.a->slots[i].val.type == MS_NIL)
                continue;
            ms_value key = ms_int(++k);
            ms_array_set(c->vm, &out, &key, keys ? &a->u.a->slots[i].key : &a->u.a->slots[i].val);
        }
    } else if (a->type == MS_CARRAY) {
        for (int32_t i = 0; i < a->u.c->n; i++) {
            ms_value key = ms_int(++k);
            ms_value ik = ms_int(i + 1);
            ms_array_set(c->vm, &out, &key, keys ? &ik : &a->u.c->items[i]);
        }
    }
    ms_return(c, &out);
    ms_release(c->vm, out);
    return 0;
}

static int bi_getarraykeys(ms_call *c) { return array_pairs(c, 1); }
static int bi_getarrayvalues(ms_call *c) { return array_pairs(c, 0); }

static int bi_gettime(ms_call *c)
{
    ms_return_float(c, c->vm->time);
    return 0;
}

void ms_register_builtins(ms_vm *vm)
{
    static const struct { const char *name; unsigned on; ms_cmd_fn fn; } table[] = {
        {"wait", MS_ON_THREAD, bi_wait},
        {"waitframe", MS_ON_THREAD, bi_waitframe},
        {"end", MS_ON_THREAD, bi_end},
        {"goto", MS_ON_THREAD, bi_goto},
        {"waittill", MS_ON_ANY, bi_waittill},
        {"thread", MS_ON_ANY, bi_thread},
        {"waitthread", MS_ON_ANY, bi_waitthread},
        {"exec", MS_ON_ANY, bi_exec},
        {"waitexec", MS_ON_ANY, bi_waitexec},
        {"self", MS_ON_THREAD, bi_self},
        {"print", MS_ON_THREAD, bi_print},
        {"println", MS_ON_THREAD, bi_println},
        {"dprintln", MS_ON_THREAD, bi_dprintln},
        {"mprint", MS_ON_THREAD, bi_print},
        {"mprintln", MS_ON_THREAD, bi_println},
        {"iprintln", MS_ON_THREAD, bi_iprintln},
        {"iprintln_noloc", MS_ON_THREAD, bi_iprintln},
        {"iprintlnbold", MS_ON_THREAD, bi_iprintlnbold},
        {"iprintlnbold_noloc", MS_ON_THREAD, bi_iprintlnbold},
        {"error", MS_ON_THREAD, bi_error},
        {"assert", MS_ON_THREAD, bi_assert},
        {"randomint", MS_ON_THREAD, bi_randomint},
        {"randomfloat", MS_ON_THREAD, bi_randomfloat},
        {"abs", MS_ON_THREAD, bi_abs},
        {"int", MS_ON_THREAD, bi_int},
        {"float", MS_ON_THREAD, bi_float},
        {"string", MS_ON_THREAD, bi_string},
        {"sin", MS_ON_THREAD, bi_sin},
        {"cos", MS_ON_THREAD, bi_cos},
        {"tan", MS_ON_THREAD, bi_tan},
        {"atan", MS_ON_THREAD, bi_atan},
        {"sqrt", MS_ON_THREAD, bi_sqrt},
        {"vector_length", MS_ON_THREAD, bi_vector_length},
        {"vector_normalize", MS_ON_THREAD, bi_vector_normalize},
        {"vector_add", MS_ON_THREAD, bi_vector_add},
        {"vector_subtract", MS_ON_THREAD, bi_vector_subtract},
        {"vector_scale", MS_ON_THREAD, bi_vector_scale},
        {"vector_dot", MS_ON_THREAD, bi_vector_dot},
        {"vector_cross", MS_ON_THREAD, bi_vector_cross},
        {"vector_within", MS_ON_THREAD, bi_vector_within},
        {"vector_toangles", MS_ON_THREAD, bi_vector_toangles},
        {"angles_toforward", MS_ON_THREAD, bi_angles_toforward},
        {"angles_toleft", MS_ON_THREAD, bi_angles_toleft},
        {"angles_toup", MS_ON_THREAD, bi_angles_toup},
        {"isdefined", MS_ON_THREAD, bi_isdefined},
        {"isarray", MS_ON_THREAD, bi_isarray},
        {"typeof", MS_ON_THREAD, bi_typeof},
        {"getarraykeys", MS_ON_THREAD, bi_getarraykeys},
        {"getarrayvalues", MS_ON_THREAD, bi_getarrayvalues},
        {"gettime", MS_ON_THREAD, bi_gettime},
        {NULL, 0, NULL}
    };
    for (int i = 0; table[i].name; i++)
        ms_register(vm, table[i].name, table[i].on, table[i].fn, NULL);
}
