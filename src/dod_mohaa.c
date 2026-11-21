/* dod_mohaa.c - bind MOHAA script commands to GoldSrc entities. See dod_mohaa.h. */
#include "dod_mohaa.h"

#include "morpheus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct DodMover {
	uint32_t id;
	float time;              /* "$obj time 2" */
	int has_dest, has_angles;
	float dest[3];           /* GoldSrc units */
	float angles[3];
	float spin[3];           /* rotatex/y/z, degrees per second, kept across moves */
	int moving;
	float end_time;
} DodMover;

#define MAX_UNKNOWN 128

static struct {
	DodMohaaBackend be;
	ms_vm *vm;
	int ticks;
	float now;
	DodMover movers[DOD_MOHAA_MAX_MOVERS];
	const char *unknown[MAX_UNKNOWN];
	int nunknown;
} S;

/* ------------------------------------------------------------------ */
/* helpers                                                              */
/* ------------------------------------------------------------------ */

static void scale_in(const float in[3], float out[3])
{
	out[0] = in[0] * DOD_MOHAA_SCALE;
	out[1] = in[1] * DOD_MOHAA_SCALE;
	out[2] = in[2] * DOD_MOHAA_SCALE;
}

static void scale_out(const float in[3], float out[3])
{
	out[0] = in[0] / DOD_MOHAA_SCALE;
	out[1] = in[1] / DOD_MOHAA_SCALE;
	out[2] = in[2] / DOD_MOHAA_SCALE;
}

static uint32_t target_id(ms_call *c)
{
	return ms_call_target(c)->u.id;
}

static DodMover *mover(uint32_t id, int create)
{
	int i, free_slot = -1;
	for (i = 0; i < DOD_MOHAA_MAX_MOVERS; i++) {
		if (S.movers[i].id == id)
			return &S.movers[i];
		if (!S.movers[i].id && free_slot < 0)
			free_slot = i;
	}
	if (!create || free_slot < 0)
		return NULL;
	memset(&S.movers[free_slot], 0, sizeof(DodMover));
	S.movers[free_slot].id = id;
	S.movers[free_slot].time = 1.0f;   /* MOHAA's default move time */
	return &S.movers[free_slot];
}

static void forget(uint32_t id)
{
	DodMover *m = mover(id, 0);
	if (m)
		memset(m, 0, sizeof(*m));
}

/* destination argument: a vector in MOHAA units, or an entity to move onto */
static int dest_arg(ms_call *c, int i, float out[3])
{
	const ms_value *v = ms_arg(c, i);
	if (v->type == MS_ENTITY) {
		if (!S.be.valid(v->u.id))
			return 0;
		S.be.get_vec(v->u.id, DOD_MOHAA_ORIGIN, out);
		return 1;
	}
	if (v->type != MS_VECTOR && v->type != MS_STRING)
		return 0;
	if (v->type == MS_STRING) {
		/* "$node" or "node": a targetname (MOHAA path nodes are often passed by name) */
		const char *name = ms_arg_string(c, i);
		if (name[0] == '$')
			name++;
		if ((name[0] >= 'a' && name[0] <= 'z') || (name[0] >= 'A' && name[0] <= 'Z') || name[0] == '_') {
			uint32_t id = 0;
			if (S.be.find(name, &id, 1) < 1 || !id || !S.be.valid(id))
				return 0;
			S.be.get_vec(id, DOD_MOHAA_ORIGIN, out);
			return 1;
		}
	}
	{
		float raw[3];
		ms_arg_vector(c, i, raw);
		scale_in(raw, out);
	}
	return 1;
}

/* ------------------------------------------------------------------ */
/* host callbacks                                                       */
/* ------------------------------------------------------------------ */

static void *h_alloc(void *user, void *ptr, size_t old_size, size_t new_size)
{
	(void)user;
	return S.be.alloc(ptr, old_size, new_size);
}

static char *h_load(void *user, const char *path, size_t *len)
{
	(void)user;
	return S.be.load_file(path, len);
}

static void h_free(void *user, char *text)
{
	(void)user;
	if (S.be.free_file)
		S.be.free_file(text);
}

static void h_print(void *user, int channel, const char *text)
{
	(void)user;
	switch (channel) {
	case MS_PRINT_HUD:
	case MS_PRINT_HUD_BOLD: S.be.print(DOD_MOHAA_PRINT_HUD, text); break;
	case MS_PRINT_ERROR: S.be.print(DOD_MOHAA_PRINT_ERROR, text); break;
	default: S.be.print(DOD_MOHAA_PRINT_CONSOLE, text); break;
	}
}

static int h_find(void *user, const char *name, uint32_t *ids, int max)
{
	(void)user;
	return S.be.find(name, ids, max);
}

static int h_valid(void *user, uint32_t id)
{
	(void)user;
	return S.be.valid(id);
}

static int h_get_field(void *user, const ms_value *l, const char *name, ms_value *out)
{
	float v[3];
	(void)user;
	if (l->type != MS_ENTITY)
		return 0;
	if (!strcmp(name, "origin")) {
		S.be.get_vec(l->u.id, DOD_MOHAA_ORIGIN, v);
		scale_out(v, v);
		*out = ms_vec(v[0], v[1], v[2]);
		return 1;
	}
	if (!strcmp(name, "angles")) {
		S.be.get_vec(l->u.id, DOD_MOHAA_ANGLES, v);
		*out = ms_vec(v[0], v[1], v[2]);
		return 1;
	}
	if (!strcmp(name, "health")) {
		*out = ms_float(S.be.get_health(l->u.id));
		return 1;
	}
	if (!strcmp(name, "targetname") || !strcmp(name, "classname") || !strcmp(name, "model")) {
		int what = name[0] == 't' ? DOD_MOHAA_TARGETNAME : name[0] == 'c' ? DOD_MOHAA_CLASSNAME : DOD_MOHAA_MODEL;
		const char *s = S.be.get_str(l->u.id, what);
		*out = ms_string(S.vm, s ? s : "");
		return 1;
	}
	return 0;
}

static int h_set_field(void *user, const ms_value *l, const char *name, const ms_value *val)
{
	float v[3];
	(void)user;
	if (l->type != MS_ENTITY)
		return 0;
	if (!strcmp(name, "origin") && val->type == MS_VECTOR) {
		scale_in(val->u.v, v);
		S.be.set_vec(l->u.id, DOD_MOHAA_ORIGIN, v);
		return 1;
	}
	if (!strcmp(name, "angles") && val->type == MS_VECTOR) {
		S.be.set_vec(l->u.id, DOD_MOHAA_ANGLES, val->u.v);
		return 1;
	}
	if (!strcmp(name, "health")) {
		S.be.set_health(l->u.id, ms_to_float(val));
		return 1;
	}
	if (!strcmp(name, "targetname")) {
		char buf[96];
		S.be.set_targetname(l->u.id, ms_tostring(val, buf, sizeof(buf)));
		return 1;
	}
	return 0;
}

/* Commands nobody binds yet are reported once each and otherwise ignored, so a
 * half-bound mission keeps running and the console lists what is missing. */
static int h_unknown(ms_call *c)
{
	const char *name = ms_call_name(c);
	int i;
	for (i = 0; i < S.nunknown; i++)
		if (S.unknown[i] == name)
			return 1;
	if (S.nunknown < MAX_UNKNOWN) {
		char msg[160];
		S.unknown[S.nunknown++] = name;   /* interned by the VM, stable while it lives */
		snprintf(msg, sizeof(msg), "mohaa: '%s' on %s is not bound yet\n", name, ms_type_name(ms_call_target(c)->type));
		S.be.print(DOD_MOHAA_PRINT_CONSOLE, msg);
	}
	return 1;
}

/* ------------------------------------------------------------------ */
/* entity commands                                                      */
/* ------------------------------------------------------------------ */

static int c_hide(ms_call *c) { S.be.set_flag(target_id(c), DOD_MOHAA_HIDDEN, 1); return 0; }
static int c_show(ms_call *c) { S.be.set_flag(target_id(c), DOD_MOHAA_HIDDEN, 0); return 0; }
static int c_notsolid(ms_call *c) { S.be.set_flag(target_id(c), DOD_MOHAA_NOTSOLID, 1); return 0; }
static int c_solid(ms_call *c) { S.be.set_flag(target_id(c), DOD_MOHAA_NOTSOLID, 0); return 0; }

static int c_remove(ms_call *c)
{
	uint32_t id = target_id(c);
	forget(id);
	S.be.remove(id);
	ms_entity_removed(S.vm, id);
	return 0;
}

static int c_origin(ms_call *c)
{
	float v[3];
	if (!dest_arg(c, 0, v))
		return ms_call_error(c, "needs a vector");
	S.be.set_vec(target_id(c), DOD_MOHAA_ORIGIN, v);
	return 0;
}

static int c_angles(ms_call *c)
{
	float v[3];
	ms_arg_vector(c, 0, v);
	S.be.set_vec(target_id(c), DOD_MOHAA_ANGLES, v);
	return 0;
}

static int c_health(ms_call *c)
{
	S.be.set_health(target_id(c), ms_arg_float(c, 0));
	return 0;
}

static int c_time(ms_call *c)
{
	DodMover *m = mover(target_id(c), 1);
	if (!m)
		return ms_call_error(c, "too many moving objects");
	m->time = ms_arg_float(c, 0);
	return 0;
}

static int c_moveto(ms_call *c)
{
	DodMover *m = mover(target_id(c), 1);
	if (!m)
		return ms_call_error(c, "too many moving objects");
	if (!dest_arg(c, 0, m->dest))
		return ms_call_error(c, "needs a vector or an entity");
	m->has_dest = 1;
	return 0;
}

/* moveUp 64, moveNorth 128 ...: offsets in MOHAA units from the pending destination */
static int c_move_dir(ms_call *c)
{
	const char *n = ms_call_name(c) + 4;   /* after "move" */
	DodMover *m = mover(target_id(c), 1);
	float d = ms_arg_float(c, 0) * DOD_MOHAA_SCALE;
	int axis = 0;
	if (!m)
		return ms_call_error(c, "too many moving objects");
	if (!m->has_dest) {
		S.be.get_vec(m->id, DOD_MOHAA_ORIGIN, m->dest);
		m->has_dest = 1;
	}
	if (!strcmp(n, "up")) axis = 2;
	else if (!strcmp(n, "down")) { axis = 2; d = -d; }
	else if (!strcmp(n, "north")) axis = 1;
	else if (!strcmp(n, "south")) { axis = 1; d = -d; }
	else if (!strcmp(n, "east")) axis = 0;
	else if (!strcmp(n, "west")) { axis = 0; d = -d; }
	m->dest[axis] += d;
	return 0;
}

static int c_rotateto(ms_call *c)
{
	DodMover *m = mover(target_id(c), 1);
	if (!m)
		return ms_call_error(c, "too many moving objects");
	ms_arg_vector(c, 0, m->angles);
	m->has_angles = 1;
	return 0;
}

static void apply_spin(DodMover *m)
{
	static const float zero[3] = {0, 0, 0};
	S.be.set_velocity(m->id, zero, m->spin);
}

/* rotateX 90: spin at 90 degrees a second until told otherwise (0 stops) */
static int c_rotate_axis(ms_call *c)
{
	const char *n = ms_call_name(c);
	DodMover *m = mover(target_id(c), 1);
	if (!m)
		return ms_call_error(c, "too many moving objects");
	/* MOHAA x/y/z are pitch, yaw and roll in GoldSrc's angle order */
	m->spin[n[6] == 'x' ? 0 : n[6] == 'y' ? 1 : 2] = ms_arg_float(c, 0);
	if (!m->moving)
		apply_spin(m);
	return 0;
}

static void start_move(DodMover *m)
{
	float org[3], ang[3], vel[3] = {0, 0, 0}, avel[3];
	int i;
	S.be.get_vec(m->id, DOD_MOHAA_ORIGIN, org);
	S.be.get_vec(m->id, DOD_MOHAA_ANGLES, ang);
	if (m->time <= 0.0f) {
		if (m->has_dest)
			S.be.set_vec(m->id, DOD_MOHAA_ORIGIN, m->dest);
		if (m->has_angles)
			S.be.set_vec(m->id, DOD_MOHAA_ANGLES, m->angles);
		m->has_dest = m->has_angles = 0;
		m->moving = 0;
		m->end_time = S.now;
		return;
	}
	for (i = 0; i < 3; i++) {
		vel[i] = m->has_dest ? (m->dest[i] - org[i]) / m->time : 0.0f;
		avel[i] = m->has_angles ? (m->angles[i] - ang[i]) / m->time : m->spin[i];
	}
	S.be.set_velocity(m->id, vel, avel);
	m->moving = 1;
	m->end_time = S.now + m->time;
}

static int c_move(ms_call *c)
{
	DodMover *m = mover(target_id(c), 1);
	if (!m)
		return ms_call_error(c, "too many moving objects");
	start_move(m);
	return 0;
}

static int c_waitmove(ms_call *c)
{
	DodMover *m = mover(target_id(c), 1);
	if (!m)
		return ms_call_error(c, "too many moving objects");
	start_move(m);
	if (m->moving)
		ms_call_wait_signal(c, ms_call_target(c), "movedone");
	return 0;
}

/* ---- actors (MOHAA AI soldiers; the game plays them with grunts) ---- */

static int actor_order(ms_call *c, int order, const float pos[3])
{
	static const float zero[3] = {0, 0, 0};
	if (!S.be.actor || !S.be.actor(target_id(c), order, pos ? pos : zero))
		return ms_call_error(c, "%s: not an actor", ms_call_name(c));
	return 0;
}

static int c_ai_on(ms_call *c) { return actor_order(c, DOD_MOHAA_AI_ON, NULL); }
static int c_ai_off(ms_call *c) { return actor_order(c, DOD_MOHAA_AI_OFF, NULL); }
static int c_attackplayer(ms_call *c) { return actor_order(c, DOD_MOHAA_ATTACK, NULL); }

static int c_goto(ms_call *c)
{
	float pos[3];
	const char *name = ms_call_name(c);
	if (!dest_arg(c, 0, pos))
		return ms_call_error(c, "%s needs a vector, an entity or a node name", name);
	return actor_order(c, (name[0] == 'r' || name[0] == 'R') ? DOD_MOHAA_RUNTO : DOD_MOHAA_WALKTO, pos);
}

static int c_turnto(ms_call *c)
{
	float pos[3];
	if (!dest_arg(c, 0, pos))
		return 0;	/* "turnto NULL" clears a facing target in MOHAA; holding still is the same here */
	return actor_order(c, DOD_MOHAA_TURNTO, pos);
}

static int c_playsound(ms_call *c) { S.be.sound(target_id(c), ms_arg_string(c, 0), 0); return 0; }
static int c_loopsound(ms_call *c) { S.be.sound(target_id(c), ms_arg_string(c, 0), 1); return 0; }
static int c_stoploopsound(ms_call *c) { S.be.sound(target_id(c), "", -1); return 0; }

/* ------------------------------------------------------------------ */
/* thread commands                                                      */
/* ------------------------------------------------------------------ */

static void trigger_value(const ms_value *v)
{
	if (v->type == MS_ENTITY) {
		const char *tn = S.be.valid(v->u.id) ? S.be.get_str(v->u.id, DOD_MOHAA_TARGETNAME) : NULL;
		if (tn && *tn)
			S.be.use_targets(tn);
	} else if (v->type == MS_CARRAY || v->type == MS_ARRAY) {
		/* every entity in "$guys" shares the targetname, so one use fires them all */
		const ms_value *first = ms_array_item(v, 0);
		if (first)
			trigger_value(first);
	} else if (ms_string_text(v)) {
		const char *s = ms_string_text(v);
		S.be.use_targets(s[0] == '$' ? s + 1 : s);
	}
}

static int c_trigger(ms_call *c)
{
	trigger_value(ms_arg(c, 0));
	return 0;
}

static int c_spawn(ms_call *c)
{
	const char *keys[16], *vals[16];
	char origin[64];
	int n = 0, i;
	uint32_t id;
	for (i = 1; i + 1 < ms_argc(c) && n < 16; i += 2) {
		keys[n] = ms_arg_string(c, i);
		if (!strcmp(keys[n], "origin")) {
			float v[3];
			ms_arg_vector(c, i + 1, v);
			scale_in(v, v);
			snprintf(origin, sizeof(origin), "%g %g %g", v[0], v[1], v[2]);
			vals[n] = origin;
		} else
			vals[n] = ms_arg_string(c, i + 1);
		n++;
	}
	id = S.be.spawn(ms_arg_string(c, 0), keys, vals, n);
	if (!id)
		return ms_call_error(c, "could not spawn '%s'", ms_arg_string(c, 0));
	ms_return_entity(c, id);
	return 0;
}

static int c_centerprint(ms_call *c)
{
	S.be.print(DOD_MOHAA_PRINT_CENTER, ms_arg_string(c, 0));
	return 0;
}

static void register_commands(ms_vm *vm)
{
	static const struct { const char *name; unsigned on; ms_cmd_fn fn; } table[] = {
		{"hide", MS_ON_ENTITY, c_hide},
		{"show", MS_ON_ENTITY, c_show},
		{"notsolid", MS_ON_ENTITY, c_notsolid},
		{"solid", MS_ON_ENTITY, c_solid},
		{"remove", MS_ON_ENTITY, c_remove},
		{"delete", MS_ON_ENTITY, c_remove},
		{"origin", MS_ON_ENTITY, c_origin},
		{"angles", MS_ON_ENTITY, c_angles},
		{"health", MS_ON_ENTITY, c_health},
		{"time", MS_ON_ENTITY, c_time},
		{"moveto", MS_ON_ENTITY, c_moveto},
		{"moveup", MS_ON_ENTITY, c_move_dir},
		{"movedown", MS_ON_ENTITY, c_move_dir},
		{"movenorth", MS_ON_ENTITY, c_move_dir},
		{"movesouth", MS_ON_ENTITY, c_move_dir},
		{"moveeast", MS_ON_ENTITY, c_move_dir},
		{"movewest", MS_ON_ENTITY, c_move_dir},
		{"rotateto", MS_ON_ENTITY, c_rotateto},
		{"rotatex", MS_ON_ENTITY, c_rotate_axis},
		{"rotatey", MS_ON_ENTITY, c_rotate_axis},
		{"rotatez", MS_ON_ENTITY, c_rotate_axis},
		{"move", MS_ON_ENTITY, c_move},
		{"waitmove", MS_ON_ENTITY, c_waitmove},
		{"playsound", MS_ON_ENTITY, c_playsound},
		{"loopsound", MS_ON_ENTITY, c_loopsound},
		{"stoploopsound", MS_ON_ENTITY, c_stoploopsound},
		{"ai_on", MS_ON_ENTITY, c_ai_on},
		{"ai_off", MS_ON_ENTITY, c_ai_off},
		{"attackplayer", MS_ON_ENTITY, c_attackplayer},
		{"runto", MS_ON_ENTITY, c_goto},
		{"walkto", MS_ON_ENTITY, c_goto},
		{"turnto", MS_ON_ENTITY, c_turnto},
		{"lookat", MS_ON_ENTITY, c_turnto},
		{"trigger", MS_ON_THREAD, c_trigger},
		{"spawn", MS_ON_THREAD, c_spawn},
		{"centerprint", MS_ON_THREAD, c_centerprint},
		{NULL, 0, NULL}
	};
	int i;
	for (i = 0; table[i].name; i++)
		ms_register(vm, table[i].name, table[i].on, table[i].fn, NULL);
}

/* ------------------------------------------------------------------ */
/* lifecycle                                                            */
/* ------------------------------------------------------------------ */

void Dod_MohaaShutdown(void)
{
	if (S.vm)
		ms_destroy(S.vm);
	S.vm = NULL;
	memset(S.movers, 0, sizeof(S.movers));
	S.nunknown = 0;
	S.ticks = 0;
}

static void *default_alloc(void *ptr, size_t old_size, size_t new_size)
{
	(void)old_size;
	if (!new_size) {
		free(ptr);
		return NULL;
	}
	return realloc(ptr, new_size);
}

int Dod_MohaaLevelInit(const DodMohaaBackend *backend, const char *mapname, float now, size_t memory_budget)
{
	char path[128];
	size_t len = 0;
	char *text;
	ms_host host;
	ms_config cfg;

	Dod_MohaaShutdown();
	if (!backend || !mapname || !*mapname)
		return 0;
	S.be = *backend;
	if (!S.be.alloc)
		S.be.alloc = default_alloc;
	S.now = now;
	snprintf(path, sizeof(path), "maps/%s.scr", mapname);
	text = S.be.load_file(path, &len);
	if (!text)
		return 0;   /* not a MOHAA map: no VM, no cost */
	if (S.be.free_file)
		S.be.free_file(text);

	memset(&host, 0, sizeof(host));
	host.alloc = h_alloc;
	host.load_file = h_load;
	host.free_file = h_free;
	host.print = h_print;
	host.find_targetname = h_find;
	host.entity_valid = h_valid;
	host.get_field = h_get_field;
	host.set_field = h_set_field;
	host.unknown_command = h_unknown;
	memset(&cfg, 0, sizeof(cfg));
	cfg.memory_budget = memory_budget;
	S.vm = ms_create(&host, &cfg);
	if (!S.vm) {
		S.be.print(DOD_MOHAA_PRINT_ERROR, "mohaa: cannot create the script VM\n");
		return 0;
	}
	register_commands(S.vm);
	if (!ms_exec(S.vm, path, NULL, NULL, NULL, 0)) {
		S.be.print(DOD_MOHAA_PRINT_ERROR, "mohaa: level script did not start\n");
		Dod_MohaaShutdown();
		return 0;
	}
	return 1;
}

void Dod_MohaaThink(float now)
{
	ms_value level = ms_nil();
	int i;
	if (!S.vm)
		return;
	S.now = now;
	level.type = MS_LEVEL;
	/* MOHAA level scripts wait for these before touching the world */
	if (S.ticks == 0)
		ms_notify(S.vm, &level, "prespawn");
	else if (S.ticks == 1)
		ms_notify(S.vm, &level, "spawn");
	S.ticks++;
	for (i = 0; i < DOD_MOHAA_MAX_MOVERS && S.vm; i++) {
		DodMover *m = &S.movers[i];
		ms_value ent;
		if (!m->id || !m->moving || now < m->end_time)
			continue;
		if (!S.be.valid(m->id)) {
			memset(m, 0, sizeof(*m));
			continue;
		}
		if (m->has_dest)
			S.be.set_vec(m->id, DOD_MOHAA_ORIGIN, m->dest);
		if (m->has_angles)
			S.be.set_vec(m->id, DOD_MOHAA_ANGLES, m->angles);
		m->has_dest = m->has_angles = 0;
		m->moving = 0;
		apply_spin(m);
		ent = ms_entity(m->id);
		ms_notify(S.vm, &ent, "movedone");
	}
	if (S.vm)
		ms_frame(S.vm, now);
}

void Dod_MohaaNotify(uint32_t id, const char *event)
{
	ms_value ent = ms_entity(id);
	if (S.vm && id)
		ms_notify(S.vm, &ent, event);
}

void Dod_MohaaEntityFreed(uint32_t id)
{
	if (!S.vm || !id)
		return;
	forget(id);
	ms_entity_removed(S.vm, id);
}

void Dod_MohaaGetStats(DodMohaaStats *out)
{
	memset(out, 0, sizeof(*out));
	if (!S.vm)
		return;
	out->active = 1;
	out->threads = ms_thread_count(S.vm);
	out->memory = ms_memory_used(S.vm);
	out->memory_peak = ms_memory_peak(S.vm);
	out->errors = ms_error_count(S.vm);
	out->unknown_commands = S.nunknown;
}
