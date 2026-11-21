/* Grunts: cheap fodder for co-op (players against bots) matches. Engine-independent so the rules can be host-tested;
 * the game DLL side is dod_grunt.cpp.
 *
 * A grunt is the opposite of a bot. A bot is a fake client (a client slot, about five edicts for its weapons, a nav
 * graph, objectives, call-outs, stuck escapes). A grunt is one prop edict wearing a team's player body plus one row
 * of plain arrays here: position, heading, health, a state and three timers. No path graph, no objectives, no
 * call-outs, no weapons. It wanders, or runs forward, and when a player of the other side is in plain sight it stops
 * and shoots with a hit chance that falls off with distance. It is opt-in (dod_grunts, default 0) and sits beside the
 * bots: the bot system is not touched.
 *
 * Cost control is part of the design. Perception (one line trace per look) is staggered: each grunt looks about 3
 * times a second and a frame never runs more than DOD_GRUNT_SENSE_BUDGET looks. Movement is plain arithmetic every
 * frame, checked against the world by one hull trace per grunt every DOD_GRUNT_MOVE_CHECK seconds, staggered by slot.
 * DodGruntStats counts looks, traces, shots and the rest so the load can be read back. */
#ifndef DODGC_DOD_GRUNTS_H
#define DODGC_DOD_GRUNTS_H

#include "dod_game.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOD_GRUNT_MAX 48		/* pool size; one edict each, so also the most edicts they can take */
#define DOD_GRUNT_EDICT_RESERVE 24	/* edicts always left free for players, gibs, grenades, temp entities */
#define DOD_GRUNT_SENSE_BUDGET 4	/* line-of-sight looks per frame, all grunts together */
#define DOD_GRUNT_SENSE_INTERVAL 0.35f	/* seconds between one grunt's looks (jittered +-25%) */
#define DOD_GRUNT_MOVE_CHECK 0.15f	/* seconds between one grunt's collision checks */
#define DOD_GRUNT_SIGHT 1800.0f		/* units a grunt notices a player from */
#define DOD_GRUNT_WALK 90.0f		/* units per second */
#define DOD_GRUNT_RUN 190.0f
#define DOD_GRUNT_HEALTH 40
#define DOD_GRUNT_RESPAWN 10.0f		/* seconds a dead grunt lies there before a fresh one comes in */
#define DOD_GRUNT_CLOSE 220.0f		/* a rushing grunt stops this near its target and fights */

typedef enum {
	DOD_GRUNT_FREE = 0,	/* slot unused */
	DOD_GRUNT_DEAD,
	DOD_GRUNT_WANDER,	/* walks a heading, turns now and then */
	DOD_GRUNT_ADVANCE,	/* runs: at the seen target, else along the heading */
	DOD_GRUNT_SHOOT,	/* stands and fires at the seen target */
	DOD_GRUNT_HOLD		/* stands where a script put it; turns to its heading */
} DodGruntState;

/* Script ownership (MOHAA mission actors, see dod_mohaa.c). A scripted grunt never respawns and is never culled by the
 * dod_grunts count; the script moves it, turns it, switches its own AI off and on, and waits for its "movedone". */
enum {
	DOD_GRUNT_SCRIPTED = 1 << 0,	/* owned by a script */
	DOD_GRUNT_AI_OFF = 1 << 1,	/* ignores what it sees: no targets, no shooting, no wandering */
	DOD_GRUNT_ATTACK = 1 << 2	/* runs at what it sees (MOHAA attackplayer) instead of holding */
};
enum { DOD_GRUNT_ORDER_NONE = 0, DOD_GRUNT_ORDER_WALK, DOD_GRUNT_ORDER_RUN };
#define DOD_GRUNT_ARRIVE 32.0f		/* a move order is done this near its point */

/* Path nodes (MOHAA info_pathnode, from maps/<map>.nodes written by mohaa-gc tools/bsp19_to_map.py). They are not
 * entities: a node costs no edict, only 32 bytes here plus 14 bytes of search scratch. Links are made once at level
 * load with a hull trace each (DodPaths_Link); a move order then walks node to node, one A* per node reached. */
#define DOD_PATH_MAX 2048		/* nodes kept per map; the rest of a bigger map's nodes are dropped */
#define DOD_PATH_LINKS 8		/* neighbours per node */
#define DOD_PATH_TRIES 16		/* nearest candidates traced per node at link time */
#define DOD_PATH_REACH 400.0f		/* farthest link (MOHAA links nodes up to 512 units; x72/94 is 392) */
#define DOD_PATH_NONE 0xFFFF

typedef struct DodPaths {
	int count;
	float (*pos)[3];			/* feet, GoldSrc units */
	uint32_t *name;				/* DodPaths_Hash of the targetname, 0 none */
	unsigned short (*link)[DOD_PATH_LINKS];	/* DOD_PATH_NONE past the last */
	float *cost;				/* search scratch */
	unsigned short *from, *heap, *slot;
	void *block;				/* one allocation holds all of it */
} DodPaths;

/* Lowercase FNV-1a of a node name (no '$'). */
uint32_t DodPaths_Hash(const char *name);
/* Parse a .nodes file ("x y z grav spawnflags targetname" lines, '#' comments). Returns the node count; 0 leaves g
 * empty. The text need not be NUL-terminated. */
int DodPaths_Load(DodPaths *g, const char *text, size_t len);
void DodPaths_Free(DodPaths *g);
/* Link each node to its nearest neighbours within reach that walkable() clears (both ways). Returns traces used. */
typedef int (*DodPathWalkable)(void *ctx, const float a[3], const float b[3]);
int DodPaths_Link(DodPaths *g, DodPathWalkable walkable, void *ctx, float reach);
/* The node named name, or -1. */
int DodPaths_Find(const DodPaths *g, const char *name);
/* The node nearest a point (height counts double, to keep to the same floor), or -1 with no nodes. */
int DodPaths_Nearest(const DodPaths *g, const float p[3]);
/* The neighbour of from on the shortest route to to; to itself when they are linked, -1 when there is no route. */
int DodPaths_NextHop(DodPaths *g, int from, int to);

typedef struct DodGruntStats {
	unsigned looks;		/* line-of-sight traces */
	unsigned move_traces;	/* collision hull traces */
	unsigned floor_traces;
	unsigned shots;
	unsigned hits;
	unsigned deaths;
	unsigned respawns;
	unsigned deferred;	/* looks that were due but pushed to a later frame by the budget */
} DodGruntStats;

typedef struct DodGruntPool {
	int cap;		/* slots in use, 0..DOD_GRUNT_MAX */
	float x[DOD_GRUNT_MAX], y[DOD_GRUNT_MAX], z[DOD_GRUNT_MAX];
	float yaw[DOD_GRUNT_MAX];		/* where the body faces */
	float heading[DOD_GRUNT_MAX];		/* where it walks */
	float phase[DOD_GRUNT_MAX];		/* animation frame, 0..256 */
	float next_look[DOD_GRUNT_MAX];
	float next_check[DOD_GRUNT_MAX];
	float next_fire[DOD_GRUNT_MAX];
	float turn_at[DOD_GRUNT_MAX];		/* when a wanderer picks a new heading */
	float respawn_at[DOD_GRUNT_MAX];
	float target_x[DOD_GRUNT_MAX], target_y[DOD_GRUNT_MAX];	/* last place a target was seen */
	float target_seen[DOD_GRUNT_MAX];	/* when; the memory fades after DOD_GRUNT_MEMORY seconds */
	short hp[DOD_GRUNT_MAX];
	signed char target[DOD_GRUNT_MAX];	/* entity index of the seen player, 0 none */
	unsigned char state[DOD_GRUNT_MAX];
	unsigned char team[DOD_GRUNT_MAX];
	unsigned char blocked[DOD_GRUNT_MAX];	/* the last collision check stopped it */
	unsigned char script[DOD_GRUNT_MAX];	/* DOD_GRUNT_SCRIPTED | AI_OFF | ATTACK */
	unsigned char order[DOD_GRUNT_MAX];	/* DOD_GRUNT_ORDER_* */
	unsigned char order_done[DOD_GRUNT_MAX];	/* set on arrival, cleared by DodGrunt_TakeOrderDone */
	float order_x[DOD_GRUNT_MAX], order_y[DOD_GRUNT_MAX];	/* the point it heads for now: a path node or the goal */
	float goal_x[DOD_GRUNT_MAX], goal_y[DOD_GRUNT_MAX];	/* where the order ends */
	unsigned short path_node[DOD_GRUNT_MAX];	/* the node order_x/y is, DOD_PATH_NONE on the last leg */
	unsigned short goal_node[DOD_GRUNT_MAX];	/* the node nearest the goal */
	DodPaths *paths;	/* the map's path nodes, or NULL: orders then go straight */
	unsigned rng;
	int look_cursor;	/* where the look scheduler resumes, so no slot is starved */
	DodGruntStats stats;
} DodGruntPool;

#define DOD_GRUNT_MEMORY 4.0f

/* Bytes one grunt costs in the pool (arrays only; the prop's edict is on top). */
int DodGrunt_RowBytes(void);

/* How many grunts to run: the request, never above the pool, never above the edicts that are free minus the reserve. */
int Dod_GruntTargetCount(int requested, int edicts_free);

void DodGrunt_Init(DodGruntPool *p, unsigned seed);
/* Random float in [0, 1). A small xorshift: grunts must not share the engine's RANDOM_* state in host tests. */
float DodGrunt_Rand(DodGruntPool *p);

/* Put grunt i into the world alive at a spawn point. rush: 1 runs forward, 0 wanders. */
void DodGrunt_Spawn(DodGruntPool *p, int i, int team, float x, float y, float z, float yaw, float now, int rush);

/* The look scheduler: fills out[] with up to budget grunt slots whose look is due, starting at the cursor. Returns the
 * count. Looks that were due but over the budget are counted in stats.deferred. Dead and free slots never look. */
int DodGrunt_DueLooks(DodGruntPool *p, float now, int budget, int *out);

/* A look's result. visible: a player of the other side is in plain sight at (tx, ty) at distance dist. Sets the
 * state: shoot inside the firing range, otherwise advance (rush) or keep wandering. */
void DodGrunt_Looked(DodGruntPool *p, int i, float now, int visible, int target, float tx, float ty, float dist, int rush);

/* Per-frame update of one live grunt: moves it along its heading or toward the target's last seen place, advances the
 * walk cycle, turns the body. No traces. Wanderers pick a new heading when their timer runs out. */
void DodGrunt_Move(DodGruntPool *p, int i, float now, float dt);

/* Collision check result for the next DOD_GRUNT_MOVE_CHECK seconds of travel. clear: nothing in the way. When blocked
 * the grunt stops and turns away (by a random 60 to 180 degrees) and the check repeats at the next due time. */
void DodGrunt_MoveChecked(DodGruntPool *p, int i, int clear);
int DodGrunt_CheckDue(DodGruntPool *p, int i, float now);
/* The ground distance the next check should probe along the heading. */
float DodGrunt_ProbeDistance(const DodGruntPool *p, int i);

/* Shooting. A grunt fires when it stands in SHOOT, has a target and its cooldown is up. Returns 1 and re-arms the
 * cooldown (1.0 to 2.0 s, shorter with skill 0..3) when a shot is due now. */
int DodGrunt_ShotDue(DodGruntPool *p, int i, float now, int skill);
/* Chance that a shot lands, 0..1: high up close, thin at range, better with skill 0..3. */
float DodGrunt_HitChance(float dist, int skill);
/* One shot's outcome from a random draw in [0, 1). */
int DodGrunt_ShotHits(float dist, int skill, float roll);

/* Damage taken. Returns 1 when this killed it. Dead grunts lie there DOD_GRUNT_RESPAWN seconds. */
int DodGrunt_Hurt(DodGruntPool *p, int i, int damage, float now);
int DodGrunt_RespawnDue(const DodGruntPool *p, int i, float now);

/* ---- script control (MOHAA actors) ---- */
/* A free slot for a script actor, searched from the top so the dod_grunts count (which fills from slot 0) rarely meets
 * it. -1 when the pool is full. */
int DodGrunt_FreeScriptSlot(const DodGruntPool *p);
/* Put a script-owned grunt in the world. It holds still, AI on, until told otherwise. */
void DodGrunt_SpawnScripted(DodGruntPool *p, int i, int team, float x, float y, float z, float yaw, float now);
/* ai_on / ai_off. Switching off drops the target and any firing; the grunt holds or finishes its move order. */
void DodGrunt_SetAI(DodGruntPool *p, int i, int on);
/* attackplayer: AI on, and run at what it sees instead of holding. */
void DodGrunt_SetAttack(DodGruntPool *p, int i, int on);
/* walkto / runto: head for (x, y, z) ignoring what it sees; on arrival it holds and order_done is set. With path
 * nodes it walks node to node from the one nearest it to the one nearest the goal, then straight to the goal. */
void DodGrunt_OrderMove(DodGruntPool *p, int i, float x, float y, float z, int run);
/* Drop a move order without the done event; the grunt holds where it is. */
void DodGrunt_OrderStop(DodGruntPool *p, int i);
/* turnto / lookat: face (tx, ty). A holding grunt turns there; one with a move order keeps walking. */
void DodGrunt_Face(DodGruntPool *p, int i, float tx, float ty);
/* 1 once after a move order arrived (the host turns it into the script's "movedone"). */
int DodGrunt_TakeOrderDone(DodGruntPool *p, int i);

/* Facing: the yaw a grunt should look toward a point from its place. */
float DodGrunt_YawTo(float x, float y, float tx, float ty);

#ifdef __cplusplus
}
#endif

#endif
