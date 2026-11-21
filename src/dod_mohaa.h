#ifndef DODGC_DOD_MOHAA_H
#define DODGC_DOD_MOHAA_H

/*
 * MOHAA mission scripts (.scr) on DoD-GC.
 *
 * A map that ships maps/<map>.scr gets its script run by the MOHAA script VM
 * (src/mohaa/, MIT, from timfox/mohaa-gc). This file binds the VM's commands
 * to GoldSrc entities through a small backend table, so the binding logic is
 * plain C and testable on a PC; dod_mohaa_ents.cpp fills the table from the
 * HL1SDK game DLL. Maps without a .scr cost nothing: no VM is created.
 *
 * MOHAA units are larger than GoldSrc's (player 94 vs 72 tall); positions are
 * scaled by DOD_MOHAA_SCALE on the way in and out, the same factor the map
 * converter uses. Angles are not scaled.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOD_MOHAA_SCALE (72.0f / 94.0f)
#define DOD_MOHAA_MAX_MOVERS 64

enum { DOD_MOHAA_ORIGIN = 0, DOD_MOHAA_ANGLES = 1 };
enum { DOD_MOHAA_TARGETNAME = 0, DOD_MOHAA_CLASSNAME = 1, DOD_MOHAA_MODEL = 2 };
enum { DOD_MOHAA_HIDDEN = 0, DOD_MOHAA_NOTSOLID = 1 };
/* Sound channels, from the ubersound alias's channel name. */
enum { DOD_MOHAA_CHAN_AUTO = 0, DOD_MOHAA_CHAN_VOICE, DOD_MOHAA_CHAN_ITEM, DOD_MOHAA_CHAN_BODY, DOD_MOHAA_CHAN_WEAPON };
/* Sound aliases come from maps/<map>.snd, written by mohaa-gc tools/ubersound.py (lines "alias sample volume mindist
   channel"; repeated aliases are variants picked at random). At most this many rows are kept. */
#define DOD_MOHAA_MAX_SOUNDS 256
/* Orders to a script actor (a MOHAA AI soldier, played by a grunt). pos is in GoldSrc units. */
enum {
	DOD_MOHAA_AI_ON = 0, DOD_MOHAA_AI_OFF, DOD_MOHAA_ATTACK,	/* attackplayer */
	DOD_MOHAA_WALKTO, DOD_MOHAA_RUNTO, DOD_MOHAA_TURNTO, DOD_MOHAA_STOP
};
enum { DOD_MOHAA_PRINT_CONSOLE = 0, DOD_MOHAA_PRINT_HUD = 1, DOD_MOHAA_PRINT_CENTER = 2, DOD_MOHAA_PRINT_ERROR = 3 };

/* Entity ids are opaque to this file: the backend packs edict index and serial so a
 * removed-and-reused edict never answers for an old reference. 0 is never valid. */
#define DOD_MOHAA_NODE_ID 0x80000000u	/* entity ids from the backend keep this bit clear */

typedef struct DodMohaaBackend {
	char *(*load_file)(const char *path, size_t *len);  /* NULL if missing */
	void (*free_file)(char *text);
	void (*print)(int channel, const char *text);
	int (*find)(const char *targetname, uint32_t *ids, int max);  /* returns total matches */
	int (*valid)(uint32_t id);
	void (*get_vec)(uint32_t id, int what, float out[3]);         /* GoldSrc units */
	void (*set_vec)(uint32_t id, int what, const float v[3]);
	float (*get_health)(uint32_t id);
	void (*set_health)(uint32_t id, float health);
	const char *(*get_str)(uint32_t id, int what);
	void (*set_targetname)(uint32_t id, const char *name);
	void (*set_flag)(uint32_t id, int flag, int on);
	void (*remove)(uint32_t id);
	void (*set_velocity)(uint32_t id, const float vel[3], const float avel[3]);
	void (*use_targets)(const char *targetname);                 /* "trigger $name" */
	uint32_t (*spawn)(const char *classname, const char *const *keys, const char *const *values, int count);
	/* Play a sample (relative to sound/, e.g. "mohaa/mechanics/door_01.wav") on an entity. loop 1 plays it on the
	   static channel (the wav's own loop marker repeats it), loop -1 stops that sample. attn is GoldSrc attenuation. */
	void (*sound)(uint32_t id, const char *sample, int loop, float volume, float attn, int channel);
	/* At level start, for each sample maps/<map>.snd names. NULL: nothing to precache (host tests). */
	void (*precache_sound)(const char *sample);
	/* An order to an actor; returns 0 when id is not one. The game sends "movedone" when a walk/run
	   order arrives and "death" when the actor dies, through Dod_MohaaNotify. NULL: no actors. */
	int (*actor)(uint32_t id, int order, const float pos[3]);
	/* Path nodes by targetname, asked when no entity has the name (MOHAA path nodes are not entities here: see
	   DodPaths in dod_grunts.h). node_find returns the node index or -1; node_pos gives its feet origin. A script
	   sees a node as an entity with id DOD_MOHAA_NODE_ID | index, which the backend never hands out. NULL: no nodes. */
	int (*node_find)(const char *name);
	void (*node_pos)(int index, float out[3]);
	/* Background music for every player (late joiners too): intro plays once, then loop repeats; loop "" plays
	   intro once and stops; intro NULL stops the music. Paths are game-relative ("sound/mohaa/music/x.wav"),
	   PCM wavs written by mohaa-gc tools/music.py. NULL: no music. */
	void (*music)(const char *intro, const char *loop);
	void *(*alloc)(void *ptr, size_t old_size, size_t new_size); /* NULL: malloc */
} DodMohaaBackend;

typedef struct DodMohaaStats {
	int active;            /* a VM is running for this map */
	int threads;
	size_t memory, memory_peak;
	int errors;
	int unknown_commands;  /* distinct command names nothing handles yet */
} DodMohaaStats;

/* Start the map's script if maps/<mapname>.scr exists. Replaces any previous map's VM.
 * memory_budget 0 means unlimited. Returns 1 when a script is running. */
int Dod_MohaaLevelInit(const DodMohaaBackend *backend, const char *mapname, float now, size_t memory_budget);
/* Every server frame: sends "prespawn" then "spawn" to level, runs threads, finishes moves. */
void Dod_MohaaThink(float now);
void Dod_MohaaShutdown(void);
/* Game events: Dod_MohaaNotify(id, "trigger") wakes "$t waittill trigger". */
void Dod_MohaaNotify(uint32_t id, const char *event);
void Dod_MohaaEntityFreed(uint32_t id);
void Dod_MohaaGetStats(DodMohaaStats *out);

#ifdef __cplusplus
}
#endif

#endif
