/* MOHAA script binding (dod_mohaa.c) against a mock GoldSrc world. */
#define _POSIX_C_SOURCE 200809L
#include "dod_mohaa.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Ent {
	int alive;
	char targetname[32], classname[32];
	float origin[3], angles[3], vel[3], avel[3];
	float health;
	int hidden, notsolid, used, serial;
	char sound[64];
} Ent;

#define NENTS 16
static Ent ents[NENTS];
static const char *files[8][2];
static char log_buf[4096];

static void logf_(const char *s)
{
	strncat(log_buf, s, sizeof(log_buf) - strlen(log_buf) - 1);
}

/* ids carry a serial so a reused slot does not answer for an old reference */
static uint32_t make_id(int i) { return (uint32_t)(i + 1) | ((uint32_t)ents[i].serial << 8); }
static Ent *ent(uint32_t id)
{
	int i = (int)(id & 0xff) - 1;
	if (i < 0 || i >= NENTS || !ents[i].alive || ents[i].serial != (int)(id >> 8))
		return NULL;
	return &ents[i];
}

static char *m_load(const char *path, size_t *len)
{
	int i;
	for (i = 0; i < 8 && files[i][0]; i++) {
		if (!strcmp(files[i][0], path)) {
			char *s = strdup(files[i][1]);
			*len = strlen(s);
			return s;
		}
	}
	return NULL;
}
static void m_free(char *t) { free(t); }
static void m_print(int ch, const char *t)
{
	static const char *p[] = {"", "[hud] ", "[center] ", "[error] "};
	logf_(p[ch]);
	logf_(t);
	if (ch == DOD_MOHAA_PRINT_CENTER)
		logf_("\n");
}
static int m_find(const char *name, uint32_t *ids, int max)
{
	int i, n = 0;
	for (i = 0; i < NENTS; i++)
		if (ents[i].alive && !strcmp(ents[i].targetname, name)) {
			if (n < max)
				ids[n] = make_id(i);
			n++;
		}
	return n;
}
static int m_valid(uint32_t id) { return ent(id) != NULL; }
static void m_get_vec(uint32_t id, int what, float out[3]) { memcpy(out, what ? ent(id)->angles : ent(id)->origin, 12); }
static void m_set_vec(uint32_t id, int what, const float v[3]) { memcpy(what ? ent(id)->angles : ent(id)->origin, v, 12); }
static float m_get_health(uint32_t id) { return ent(id)->health; }
static void m_set_health(uint32_t id, float h) { ent(id)->health = h; }
static const char *m_get_str(uint32_t id, int what) { return what == DOD_MOHAA_TARGETNAME ? ent(id)->targetname : ent(id)->classname; }
static void m_set_tn(uint32_t id, const char *n) { snprintf(ent(id)->targetname, 32, "%s", n); }
static void m_set_flag(uint32_t id, int f, int on) { if (f == DOD_MOHAA_HIDDEN) ent(id)->hidden = on; else ent(id)->notsolid = on; }
static void m_remove(uint32_t id) { ent(id)->alive = 0; }
static void m_set_vel(uint32_t id, const float v[3], const float a[3]) { memcpy(ent(id)->vel, v, 12); memcpy(ent(id)->avel, a, 12); }
static void m_use(const char *tn)
{
	int i;
	for (i = 0; i < NENTS; i++)
		if (ents[i].alive && !strcmp(ents[i].targetname, tn))
			ents[i].used++;
}
static int add(const char *tn, const char *cls, float x, float y, float z)
{
	int i;
	for (i = 0; i < NENTS; i++)
		if (!ents[i].alive) {
			int serial = ents[i].serial + 1;
			memset(&ents[i], 0, sizeof(Ent));
			ents[i].serial = serial;
			ents[i].alive = 1;
			snprintf(ents[i].targetname, 32, "%s", tn);
			snprintf(ents[i].classname, 32, "%s", cls);
			ents[i].origin[0] = x;
			ents[i].origin[1] = y;
			ents[i].origin[2] = z;
			ents[i].health = 100;
			return i;
		}
	return -1;
}
static uint32_t m_spawn(const char *cls, const char *const *k, const char *const *v, int n)
{
	int i = add("", cls, 0, 0, 0), j;
	for (j = 0; j < n; j++) {
		if (!strcmp(k[j], "targetname"))
			snprintf(ents[i].targetname, 32, "%s", v[j]);
		else if (!strcmp(k[j], "origin"))
			sscanf(v[j], "%f %f %f", &ents[i].origin[0], &ents[i].origin[1], &ents[i].origin[2]);
	}
	return make_id(i);
}
static char precached[512];
static void m_sound(uint32_t id, const char *s, int loop, float vol, float attn, int chan)
{
	char line[160];
	snprintf(ent(id)->sound, 64, "%s%s", loop > 0 ? "loop:" : (loop < 0 ? "stop:" : ""), s);
	snprintf(line, sizeof(line), "<sound %s %s %d %.2f %.2f %d>\n", ent(id)->targetname, s, loop, vol, attn, chan);
	logf_(line);
}
static void m_precache(const char *s)
{
	if (strlen(precached) + strlen(s) + 2 > sizeof(precached))
		return;
	strcat(precached, s);
	strcat(precached, ";");
}

/* actors: classname "actor" in the mock; orders are logged */
static int m_node_find(const char *name)
{
	return strcmp(name, "path7") ? -1 : 7;
}

static void m_node_pos(int index, float out[3])
{
	out[0] = 10.0f * (float)index / 7.0f;
	out[1] = 20.0f;
	out[2] = 30.0f;
}

static int m_actor(uint32_t id, int order, const float pos[3])
{
	static const char *names[] = {"ai_on", "ai_off", "attack", "walkto", "runto", "turnto", "stop"};
	char line[96];
	Ent *e = ent(id);
	if (!e || strcmp(e->classname, "actor"))
		return 0;
	snprintf(line, sizeof(line), "<%s %s %.1f %.1f %.1f>\n", e->targetname, names[order], pos[0], pos[1], pos[2]);
	logf_(line);
	return 1;
}

static DodMohaaBackend backend(void)
{
	DodMohaaBackend b;
	memset(&b, 0, sizeof(b));
	b.load_file = m_load;
	b.free_file = m_free;
	b.print = m_print;
	b.find = m_find;
	b.valid = m_valid;
	b.get_vec = m_get_vec;
	b.set_vec = m_set_vec;
	b.get_health = m_get_health;
	b.set_health = m_set_health;
	b.get_str = m_get_str;
	b.set_targetname = m_set_tn;
	b.set_flag = m_set_flag;
	b.remove = m_remove;
	b.set_velocity = m_set_vel;
	b.use_targets = m_use;
	b.spawn = m_spawn;
	b.sound = m_sound;
	b.actor = m_actor;
	b.node_find = m_node_find;
	b.node_pos = m_node_pos;
	b.precache_sound = m_precache;
	return b;
}

static void reset(void)
{
	Dod_MohaaShutdown();
	memset(ents, 0, sizeof(ents));
	memset(files, 0, sizeof(files));
	log_buf[0] = 0;
	precached[0] = 0;
}

static int near(float a, float b) { return fabsf(a - b) < 0.01f; }

static void run(float from, float to)
{
	float t;
	for (t = from; t <= to + 0.0001f; t += 0.05f)
		Dod_MohaaThink(t);
}

static void test_no_script_no_vm(void)
{
	DodMohaaBackend b = backend();
	DodMohaaStats st;
	reset();
	assert(!Dod_MohaaLevelInit(&b, "dod_avalanche", 0, 0));
	Dod_MohaaThink(1.0f);
	Dod_MohaaGetStats(&st);
	assert(!st.active && st.memory == 0);
}

static void test_level_flow_and_movers(void)
{
	DodMohaaBackend b = backend();
	DodMohaaStats st;
	int door, wall, trig;
	reset();
	door = add("door1", "func_wall", 0, 0, 0);
	wall = add("wall", "func_wall", 10, 0, 0);
	trig = add("relay", "trigger_relay", 0, 0, 0);
	files[0][0] = "maps/m1l1.scr";
	files[0][1] =
		"main:\n"
		"\tlevel waittill prespawn\n"
		"\t$wall hide\n"
		"\t$wall notsolid\n"
		"\tlevel waittill spawn\n"
		"\tprintln \"spawned\"\n"
		"\t$door1 time 2\n"
		"\t$door1 moveto (0 0 94)\n"   /* 94 MOHAA units = 72 GoldSrc */
		"\t$door1 playsound door_open\n"
		"\t$door1 waitmove\n"
		"\tprintln \"door at \" $door1.origin\n"
		"\ttrigger $relay\n"
		"\tlocal.e = spawn script_model targetname crate origin (94 0 0)\n"
		"\tprintln \"crate \" local.e.targetname\n"
		"\t$door1 moveUp 94\n"
		"\t$door1 time 0\n"
		"\t$door1 move\n"
		"\tiprintln \"objective\"\n"
		"\tcenterprint \"go\"\n"
		"\t$door1 unboundcommand 1\n"
		"\t$door1 unboundcommand 2\n"
		"\t$wall remove\n"
		"\tprintln \"wall gone \" ($wall == NULL)\n"
		"end\n";
	assert(Dod_MohaaLevelInit(&b, "m1l1", 0, 256 * 1024));
	Dod_MohaaThink(0.05f);                      /* prespawn */
	assert(ents[wall].hidden && ents[wall].notsolid);
	Dod_MohaaThink(0.10f);                      /* spawn: the move starts */
	assert(strstr(log_buf, "spawned"));
	assert(near(ents[door].vel[2], 36.0f));     /* 72 GoldSrc units over 2 s */
	assert(!ents[door].sound[0]);	/* no maps/m1l1.snd: aliases play nothing (and are reported) */
	assert(strstr(log_buf, "sound 'door_open' is not in this map's sound table"));
	run(0.15f, 2.0f);
	assert(!strstr(log_buf, "door at"));
	run(2.05f, 2.3f);
	assert(strstr(log_buf, "door at (0.000 0.000 94.000)"));
	assert(near(ents[door].vel[2], 0.0f));
	assert(ents[trig].used == 1);
	assert(strstr(log_buf, "crate crate"));
	assert(near(ents[door].origin[2], 144.0f)); /* moveUp 94 with time 0 snaps */
	assert(strstr(log_buf, "[hud] objective"));
	assert(strstr(log_buf, "[center] go"));
	assert(strstr(log_buf, "wall gone 1"));
	assert(!ents[wall].alive);
	Dod_MohaaGetStats(&st);
	assert(st.active && st.errors == 0 && st.unknown_commands == 1);
	assert(st.memory_peak < 64 * 1024);
	/* the unbound command was reported once, not twice */
	assert(strstr(log_buf, "'unboundcommand'") == strrchr(log_buf, '\'') - strlen("'unboundcommand"));
}

static void test_waittill_trigger_and_freed(void)
{
	DodMohaaBackend b = backend();
	int t;
	reset();
	t = add("trig", "trigger_multiple", 0, 0, 0);
	add("guard", "monster", 0, 0, 0);
	files[0][0] = "maps/t.scr";
	files[0][1] =
		"main:\n"
		"\t$guard thread watch\n"
		"\t$trig waittill trigger\n"
		"\tprintln \"hit\"\n"
		"end\n"
		"watch:\n"
		"\tself waittill removed\n"
		"\tprintln \"guard removed\"\n"
		"end\n";
	assert(Dod_MohaaLevelInit(&b, "t", 0, 0));
	run(0.05f, 0.5f);
	assert(!strstr(log_buf, "hit"));
	Dod_MohaaNotify(make_id(t), "trigger");
	assert(strstr(log_buf, "hit"));
	ents[1].alive = 0;
	Dod_MohaaEntityFreed(make_id(1));
	assert(strstr(log_buf, "guard removed"));
}

static void test_budget_refuses_cleanly(void)
{
	DodMohaaBackend b = backend();
	reset();
	files[0][0] = "maps/big.scr";
	files[0][1] = "main:\n\tprintln \"hi\"\nend\n";
	assert(!Dod_MohaaLevelInit(&b, "big", 0, 512));   /* far too small to compile */
	assert(strstr(log_buf, "[error]"));
}

static void test_actors(void)
{
	DodMohaaBackend b = backend();
	int guy;
	reset();
	guy = add("guy", "actor", 0, 0, 0);
	add("node1", "info_pathnode", 72, 0, 0);
	add("crate", "func_wall", 0, 0, 0);
	files[0][0] = "maps/a.scr";
	files[0][1] =
		"main:\n"
		"\t$guy ai_off\n"
		"\t$guy runto $node1\n"
		"\t$guy waittill movedone\n"
		"\tprintln \"arrived\"\n"
		"\t$guy walkto \"node1\"\n"
		"\t$guy walkto (0 94 0)\n"
		"\t$guy turnto $crate\n"
		"\t$guy runto $path7\n"
		"\t$guy walkto \"path7\"\n"
		"\tprintln $path7.origin\n"
		"\t$guy runto $nowhere\n"
		"\t$guy ai_on\n"
		"\t$guy attackplayer\n"
		"\t$guy waittill death\n"
		"\tprintln \"guy died\"\n"
		"\t$crate ai_on\n"
		"end\n";
	assert(Dod_MohaaLevelInit(&b, "a", 0, 0));
	run(0.05f, 0.3f);
	assert(strstr(log_buf, "<guy ai_off 0.0 0.0 0.0>\n<guy runto 72.0 0.0 0.0>\n"));
	assert(!strstr(log_buf, "arrived"));
	Dod_MohaaNotify(make_id(guy), "movedone");
	run(0.35f, 0.5f);
	assert(strstr(log_buf, "arrived\n<guy walkto 72.0 0.0 0.0>\n<guy walkto 0.0 72.0 0.0>\n<guy turnto 0.0 0.0 0.0>\n"
		"<guy runto 10.0 20.0 30.0>\n<guy walkto 10.0 20.0 30.0>\n"));
	assert(strstr(log_buf, "<guy ai_on 0.0 0.0 0.0>\n<guy attack 0.0 0.0 0.0>\n"));
	assert(strstr(log_buf, "(13.056 26.111 39.167)\n"));	/* $path7.origin, back in MOHAA units */
	assert(strstr(log_buf, "runto needs a vector, an entity or a node name"));	/* $nowhere */
	Dod_MohaaNotify(make_id(guy), "death");
	run(0.55f, 0.7f);
	assert(strstr(log_buf, "guy died"));
	assert(strstr(log_buf, "ai_on: not an actor"));	/* a func_wall is no soldier: reported, script goes on */
}

static void test_sounds(void)
{
	DodMohaaBackend b = backend();
	int door, i, a = 0, bb = 0;
	reset();
	door = add("door", "func_door", 0, 0, 0);
	add("siren", "info_target", 0, 0, 0);
	files[0][0] = "maps/s.scr";
	files[0][1] =
		"main:\n"
		"\t$door playsound door_open\n"
		"\t$siren loopsound alarm\n"
		"\t$siren loopsound alarm\n"     /* replaces, does not stack */
		"\twait 1\n"
		"\t$siren stoploopsound\n"
		"\t$door playsound no_such_alias\n"
		"\t$door playsound no_such_alias\n"
		"end\n";
	files[1][0] = "maps/s.snd";
	files[1][1] =
		"# alias sample volume mindist channel\n"
		"door_open mohaa/door_01.wav 0.80 160 auto\n"
		"door_open mohaa/door_02.wav 0.70 320 item\n"
		"alarm mohaa/alarm.wav 1.00 600 voice\n"
		"DOOR_SHUT mohaa/door_01.wav 0.5 64 body\n";
	assert(Dod_MohaaLevelInit(&b, "s", 0, 0));
	/* each sample precached once, even when two aliases share it */
	assert(!strcmp(precached, "mohaa/door_01.wav;mohaa/door_02.wav;mohaa/alarm.wav;"));
	assert(strstr(log_buf, "<sound door mohaa/door_0"));
	assert(strstr(log_buf, "<sound siren mohaa/alarm.wav 1 1.00 0.25 1>\n"
		"<sound siren mohaa/alarm.wav -1 0.00 0.00 0>\n<sound siren mohaa/alarm.wav 1 1.00 0.25 1>\n"));
	run(0.05f, 1.2f);
	assert(strstr(log_buf, "<sound siren mohaa/alarm.wav -1"));
	assert(strstr(ents[1].sound, "stop:mohaa/alarm.wav"));
	/* an unknown alias is reported once and plays nothing */
	{
		const char *msg = "sound 'no_such_alias' is not in this map's sound table";
		const char *p = strstr(log_buf, msg);
		assert(p && !strstr(p + strlen(msg), "no_such_alias"));
	}
	/* variants: both door sounds come up, with their own volume and attenuation (160 -> 0.80, 320 -> 0.40) */
	for (i = 0; i < 40; i++) {
		log_buf[0] = 0;
		precached[0] = 0;
		Dod_MohaaShutdown();
		files[0][1] = "main:\n\t$door playsound door_open\n\t$door playsound DOOR_SHUT\nend\n";
		assert(Dod_MohaaLevelInit(&b, "s", (float)i, 0));
		a += strstr(log_buf, "mohaa/door_01.wav 0 0.80 0.80 0") != NULL;
		bb += strstr(log_buf, "mohaa/door_02.wav 0 0.70 0.40 2") != NULL;
		assert(strstr(log_buf, "mohaa/door_01.wav 0 0.50 2.00 3"));   /* case-insensitive alias */
	}
	assert(a > 0 && bb > 0 && a + bb == 40);
	(void)door;
}

int main(void)
{
	test_no_script_no_vm();
	test_level_flow_and_movers();
	test_waittill_trigger_and_freed();
	test_budget_refuses_cleanly();
	test_actors();
	test_sounds();
	Dod_MohaaShutdown();
	printf("dod_mohaa_test: ok\n");
	return 0;
}
