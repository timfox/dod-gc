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
enum { DOD_MOHAA_PRINT_CONSOLE = 0, DOD_MOHAA_PRINT_HUD = 1, DOD_MOHAA_PRINT_CENTER = 2, DOD_MOHAA_PRINT_ERROR = 3 };

/* Entity ids are opaque to this file: the backend packs edict index and serial so a
 * removed-and-reused edict never answers for an old reference. 0 is never valid. */
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
	void (*sound)(uint32_t id, const char *sample, int loop);   /* loop -1 stops the loop */
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
