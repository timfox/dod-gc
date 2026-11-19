/* Day of Defeat bot planning and decisions: engine-independent so the rules
 * can be host-tested. The game DLL side is dod_bot.cpp.
 *
 * A bot is a server-side fake client. It costs one edict and one client slot
 * and shares the player model the human already has loaded, which is why the
 * count is capped by the free client slots: on the GameCube the listen server
 * has exactly two (a 178 KiB BSS table), so a default Create Game match is the
 * human plus one bot. */
#ifndef DODGC_DOD_BOTS_H
#define DODGC_DOD_BOTS_H

#include "dod_game.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bots allowed on top of the humans. requested is the dod_bots cvar,
 * hard_cap the platform ceiling (1 on GameCube). Never negative, never more
 * than the free client slots, never more than the cap. */
int Dod_BotTargetCount(int requested, int max_clients, int humans, int hard_cap);

/* The bot count to ask for. A networked listen server (the link is up) keeps
 * its free slot for the peer unless bots are explicitly allowed online. */
int Dod_BotRequestedCount(int requested, int network_up, int allowed_online);

/* The team a new bot joins: the smaller one, so bots fill the side the
 * humans are not on. A tie goes to Axis. */
DodTeam Dod_BotPickTeam(int allies, int axis);
/* Team for a joining bot: fewer players first, ties go to the side with fewer bots. */
DodTeam Dod_BotPickTeamBalanced(int allies, int axis, int allies_bots, int axis_bots);

/* Class for a new bot. roll is any non-negative number (a random draw).
 * Mostly cheap infantry classes; each special class (sniper, MG, rocket) is
 * taken at most once per team so bots do not all load the heavy models.
 * special_counts[] is per class on that team. */
DodClass Dod_BotPickClass(int roll, const int special_counts[DOD_CLASS_COUNT]);

/* Eased turning: one step of a view angle (degrees) toward a target. The turn speed builds up and brakes at most
 * `accel` degrees/s per second and never exceeds `max_speed`, and the speed it aims for is the one from which it can
 * still brake to a stop on the target (sqrt(2 * accel * distance)), so the view swings, slows and settles without
 * overshoot or a snap. *vel is the current angular speed (deg/s, signed), kept between calls. The short way
 * round, result normalised to [-180, 180). */
float Dod_BotEase(float *angle, float *vel, float target, float max_speed, float accel, float dt);

/* Turn from current toward target (degrees) by at most max_step, the short
 * way around. Result is normalised to [-180, 180). */
float Dod_BotTurn(float current, float target, float max_step);
/* Shortest signed angle from a to b in degrees, in [-180, 180). */
float Dod_BotAngleDelta(float a, float b);
/* pev->angles pitch of the body for a view pitch (v_angle, positive = looking down): the engine uses -(v_angle / 3) and the renderer blends with angles.pitch * 3 */
float Dod_BotBodyPitch(float view_pitch);

/* Aim error in degrees: grows with distance, shrinks with skill (0-3). */
float Dod_BotAimError(float distance, int skill);

typedef struct DodBotInput {
	int has_target;		/* an enemy is visible */
	float target_distance;
	float aim_error;	/* degrees between where the bot looks and the target */
	int blocked;		/* wall ahead */
	int ledge;		/* drop ahead */
	int clip_empty;
	float time;		/* for the strafe rhythm */
	int semi_auto;		/* needs a fresh trigger pull each shot */
	int fire_phase;		/* alternates every call, to release the trigger */
} DodBotInput;

typedef struct DodBotMove {
	float forward;		/* -1..1 */
	float side;		/* -1..1 */
	int jump;
	int fire;
	int reload;
	int turn_away;		/* pick a new heading (blocked or at a ledge) */
} DodBotMove;

#define DOD_BOT_FIRE_CONE 6.0f		/* degrees: fire only when this close */
#define DOD_BOT_CLOSE 250.0f		/* back off inside this */
#define DOD_BOT_FAR 700.0f		/* advance beyond this */

DodBotMove Dod_BotDecide(const DodBotInput *in);

/* Stuck detection. The bot heads straight for its goal with no path graph, so
 * a wall between the two would trap it in a turn-away / re-aim loop. Every
 * DOD_BOT_STUCK_WINDOW seconds the tracker compares the position with the last
 * sample; when a bot that was trying to move covered less than
 * DOD_BOT_STUCK_MIN units it asks for an escape: a heading change that grows
 * with each consecutive failure, and a longer hold before re-aiming. */
#define DOD_BOT_STUCK_WINDOW 1.0f
#define DOD_BOT_STUCK_MIN 24.0f

typedef struct DodBotStuck {
	float x, y;
	float next_check;
	int failures;		/* consecutive stuck samples */
	int primed;
} DodBotStuck;

typedef struct DodBotEscape {
	int stuck;		/* this sample found the bot stuck */
	float turn;		/* degrees to add to the heading */
	float hold;		/* seconds to keep that heading before re-aiming */
	int jump;
} DodBotEscape;

/* wants_move: the bot was trying to walk (alive, no enemy to stand and fight).
 * Not stuck, or too early to tell, returns a zeroed escape. roll is a random
 * draw in [0, 1) picking left or right. */
DodBotEscape Dod_BotStuckUpdate(DodBotStuck *s, float x, float y, float time, int wants_move, float roll);


/* ---------------------------------------------------------------- objectives
 * Retail-style goals: attackers go to the nearest control point their team may capture and does
 * not own and stay in its capture area until it flips; defenders guard owned points and retake
 * lost ones. Straight-line distance only (there is no path graph); a bot that cannot get to a
 * point blacklists it for a while and the next best one is chosen. */
#define DOD_BOT_MAX_OBJECTIVES 16

typedef struct DodBotObjectiveInfo {
	int id;
	DodTeam owner;
	DodTeam capturing_team;		/* who is capturing it right now, or unassigned */
	int can_cap[4];			/* indexed by DodTeam: may that team capture it */
	float distance;			/* from the bot, ground plane */
	int blocked;			/* blacklisted for this bot: it could not get there */
	int crowd;			/* teammates already heading there (spreads attackers out) */
	int need;			/* players of the bot's team that must be inside to capture (0 or 1: one) */
} DodBotObjectiveInfo;

typedef enum {
	DOD_BOT_GOAL_NONE = 0,
	DOD_BOT_GOAL_ATTACK,
	DOD_BOT_GOAL_DEFEND
} DodBotGoalKind;

/* The objective id the bot should go to, or -1. current_id is the one it already has (kept while
 * it is still a valid choice, so it stays inside an area until the point flips). Attack first;
 * with nothing to take, defend: a point under attack beats the nearest owned one. */
int Dod_BotChooseObjective(const DodBotObjectiveInfo *list, int count, DodTeam team, int current_id, int *kind);

/* Blacklist bookkeeping for a bot with `slots` entries: add an id until `until`, test one. */
void Dod_BotBlockAdd(int *ids, float *untils, int slots, int id, float until, float now);
int Dod_BotBlockHas(const int *ids, const float *untils, int slots, int id, float now);

/* ---------------------------------------------------------------- GOAP planner
 * A bot's behaviour is a small goal-oriented action plan. The world is a bitmask of facts; each goal
 * wants some of them true or false and has an insistence (how badly) computed from the facts and the
 * bot's role; the planner finds the cheapest chain of actions (at most DOD_GOAP_MAX_PLAN) from the
 * current facts to the wanted ones. Everything is fixed size and allocation free: a search is a few
 * hundred integer operations, cheap enough to re-run every ~0.5 s per bot and on events. */
enum {
	DOD_WS_HAS_AMMO = 1 << 0,	/* a round in the clip */
	DOD_WS_ENEMY_VISIBLE = 1 << 1,
	DOD_WS_ENEMY_KNOWN = 1 << 2,	/* seen or heard recently (last-known position valid) */
	DOD_WS_POINT_HELD = 1 << 3,	/* the objective is owned by the bot's team */
	DOD_WS_POINT_CONTESTED = 1 << 4,	/* the enemy is capturing the objective */
	DOD_WS_LOW_HEALTH = 1 << 5,
	DOD_WS_NEAR_TEAM = 1 << 6,	/* a teammate within reach */
	DOD_WS_IN_COVER = 1 << 7,	/* out of sight of the last-known enemy */
	DOD_WS_AT_POINT = 1 << 8	/* inside the objective's capture volume */
};

typedef enum {
	DOD_GOAL_CAPTURE = 0,
	DOD_GOAL_DEFEND,
	DOD_GOAL_HUNT,
	DOD_GOAL_COVER,
	DOD_GOAL_RELOAD,
	DOD_GOAL_REGROUP,
	DOD_GOAL_COUNT
} DodGoapGoal;

/* What the bot actually does while a plan step runs. */
typedef enum {
	DOD_ACT_NONE = 0,
	DOD_ACT_RELOAD,
	DOD_ACT_ATTACK,		/* shoot the visible enemy */
	DOD_ACT_SEEK,		/* go to the last-known enemy position */
	DOD_ACT_TAKE_COVER,
	DOD_ACT_REGROUP,	/* go to a teammate */
	DOD_ACT_MOVE_POINT,
	DOD_ACT_CAPTURE,	/* stand in the volume until the point flips */
	DOD_ACT_GUARD,		/* stand in the volume and watch */
	DOD_ACT_COUNT
} DodGoapAction;

typedef enum {
	DOD_ROLE_ATTACKER = 0,
	DOD_ROLE_DEFENDER
} DodBotRole;

#define DOD_GOAP_MAX_PLAN 5

typedef struct DodGoapPlan {
	int goal;			/* DodGoapGoal, or -1 when there is nothing to do */
	int count;			/* steps; steps[0] is what to do now */
	unsigned char steps[DOD_GOAP_MAX_PLAN];
	int cost;
} DodGoapPlan;

/* The goal's wanted facts: (state & mask) == value satisfies it. */
void Dod_GoapGoalWant(int goal, unsigned *mask, unsigned *value);
/* 0..100: how much the bot wants this goal now. */
int Dod_GoapInsistence(int goal, unsigned state, int role, int team_bots);
/* The most insistent goal; `current` (or -1) gets a bonus so a bot does not flap between close goals.
 * Goals already satisfied are skipped. Returns -1 when none is wanted. */
int Dod_GoapPickGoal(unsigned state, int role, int team_bots, int current);
/* Cheapest plan from state to the goal, or count 0 when it holds already / cannot be reached. */
DodGoapPlan Dod_GoapPlan(unsigned state, int goal);
/* One call: pick the goal and plan it. */
DodGoapPlan Dod_GoapThink(unsigned state, int role, int team_bots, int current_goal);
/* Facts after a step runs (planning's own transition, exposed for tests). */
int Dod_GoapStepApplicable(int action, unsigned state);
unsigned Dod_GoapStepApply(int action, unsigned state);

/* Team role split: the bot at `rank` (0-based, by entity index) among `team_bots` on its team.
 * With two or more bots and a held point, about a third (at least one) defend the held points, the
 * rest attack; a lone bot attacks, and defends only when there is nothing left to take. */
int Dod_BotRole(int rank, int team_bots, int held_points);
/* The objective for a role: an attacker takes the nearest uncaptured point (teammates already on it
 * make it count as farther by DOD_BOT_CROWD_PENALTY each so a team spreads out, except a point that needs
 * several players, where a forming group is joined instead: each teammate already there makes it nearer), a defender guards a
 * point under attack, else the nearest held one. Each falls back to the other when it has no target. */
#define DOD_BOT_CROWD_PENALTY 450.0f
int Dod_BotChooseObjectiveRole(const DodBotObjectiveInfo *list, int count, DodTeam team, int current_id, int role, int *kind);

/* ---------------------------------------------------------------- perception and aim
 * Bots play by the same rules as a person: they see only what is inside their view cone and in line of
 * sight, take a moment to react to a new target, aim with an error that grows with distance, the target's
 * speed and their own movement and settles while they track, fire in bursts, and otherwise only hear
 * gunfire and footsteps (an approximate position) and remember where they last saw someone for a while.
 * dod_bot_skill 0..3 (easy, medium, hard, expert; the default is 1) picks a row of these numbers. */
typedef struct DodBotSkill {
	float fov_half;		/* degrees either side of where the bot looks */
	float react_min, react_max;	/* seconds between first seeing a target and the first shot */
	float aim_base;		/* degrees of error at point-blank, standing, on a still target */
	float aim_dist;		/* ... added per 500 units of distance */
	float aim_target_speed;	/* ... added per 200 units/s the target moves */
	float aim_own_speed;	/* ... added per 200 units/s the bot moves */
	float settle;		/* error shrinks to 1 / (1 + settle * seconds tracked), never below DOD_BOT_SETTLE_FLOOR */
	float turn_rate;	/* degrees per second the view can swing */
	float hear_scale;	/* multiplies the hearing radii */
	float memory;		/* seconds an enemy position is remembered after it was seen */
	float recoil;		/* degrees per second the view climbs while an automatic weapon fires */
	float turn_accel;	/* degrees per second squared the view's turn speed can build up or brake: eased, never instant */
} DodBotSkill;

#define DOD_BOT_SETTLE_FLOOR 0.4f
#define DOD_BOT_CLOSE_NOTICE 96.0f	/* inside this a bot notices an enemy in line of sight whatever it faces */
#define DOD_BOT_HEAR_GUNFIRE 1600.0f
#define DOD_BOT_HEAR_STEPS 450.0f

DodBotSkill Dod_BotSkillParams(int skill);	/* skill clamped to 0..3 */
/* Is a target at yaw to_yaw inside a cone of half_angle either side of view_yaw? (degrees) */
int Dod_BotInFov(float view_yaw, float to_yaw, float half_angle);
/* A reaction time in [react_min, react_max]; roll is in [0, 1). */
float Dod_BotReactionDelay(const DodBotSkill *sk, float roll);
/* Aim error in degrees, see DodBotSkill. track_time is how long the same target has been in view. */
float Dod_BotAimErrorEx(const DodBotSkill *sk, float distance, float target_speed, float own_speed, float track_time);
/* How far away a bot can hear a source: 0 for a silent one (still, or walking with the walk key). Footsteps
 * are muffled without a line of sight. */
float Dod_BotHearRadius(const DodBotSkill *sk, int gunfire, float source_speed, int has_los);
/* The position the bot believes a heard source is at: the real one plus an error that grows with distance.
 * roll_a/roll_b in [0, 1) pick the direction and the share of the error. */
void Dod_BotHeardOffset(float distance, float roll_a, float roll_b, float *dx, float *dy);
/* How long to hold the trigger and how long to pause afterwards (seconds). Automatic weapons fire short
 * bursts that get shorter with distance; semi-automatic ones get a longer window and a short pause. roll in [0, 1). */
float Dod_BotBurstLength(int automatic, float distance);
float Dod_BotBurstPause(int automatic, float distance, float roll);

/* ---------------------------------------------------------------- navigation graph
 * Bots find their way with a waypoint graph baked offline from the map's player hull (tools/gen_waypoints.py,
 * data/waypoints/<map>.wpt, staged next to the BSP): nodes are floor positions, edges are walks, jumps, drops
 * and ladders between neighbouring nodes. The format is big-endian; see the tool for the layout. The search
 * is A* over the graph. Everything here is engine-independent so it can be host-tested. */
#define DOD_NAV_MAX_PATH 72
#define DOD_NAV_MAX_ZONES 12
#define DOD_NAV_HAZARD_COST 4000.0f	/* added to an edge that crosses a zone that hurts the planning team */

typedef enum {
	DOD_NAV_WALK = 0,
	DOD_NAV_JUMP = 1,	/* up to ~45 units: the bot jumps */
	DOD_NAV_DROP = 2,	/* one way: walk off a ledge */
	DOD_NAV_LADDER = 3	/* climb: both ways, between a ladder's foot and its top */
} DodNavKind;

typedef struct DodNav {
	const unsigned char *nodes;	/* node_count * 7 bytes, inside the loaded file */
	const unsigned char *edges;	/* edge_count * 2 bytes */
	unsigned short *first;		/* node_count + 1: index of each node's first edge */
	float *g;			/* A* scratch */
	float *f;			/* g + estimate, the heap's key */
	short *parent;
	unsigned char *state;
	unsigned short *heap;		/* binary min-heap of open nodes by f */
	short *hpos;			/* where each open node sits in the heap */
	int node_count, edge_count;
	unsigned int bsp_size;		/* size of the BSP the graph was baked from */
	int pen_from[8], pen_to[8];	/* edges a bot got stuck on, avoided for a while (shared by all bots) */
	float pen_until[8], now;
	int pen_next;
	/* damage zones (trigger_hurt boxes): a route through one is avoided by the teams it hurts */
	int zone_count;
	float zone_min[DOD_NAV_MAX_ZONES][3], zone_max[DOD_NAV_MAX_ZONES][3];
	unsigned char zone_teams[DOD_NAV_MAX_ZONES];	/* bit (1 << team) for each team the zone hurts */
	int avoid_team;				/* the team whose routes are being planned, or 0 */
	unsigned char *edge_blocked;		/* per edge: a solid toggle wall (func_wall_toggle) stops the walk along it right now; A* skips it */
	int edge_blocked_count;
	int allow_partial;	/* a route longer than the caller's array: return its first stretch instead of failing */
	int ignore_blocked;	/* plan through blocked edges too (a bot that finds no other route walks up to the wall and breaches it) */
	/* a search in flight (resumable, see Dod_NavSearchBegin): one at a time, shared by all bots */
	int search_active, search_from, search_to, search_open, search_owner;
	float search_stamp;
	float max_jump_rise;	/* a jump link that climbs more than this is never searched (default 56) */
} DodNav;

/* Parse a .wpt image into a zeroed or freed DodNav. The data must stay alive while the DodNav is used.
 * Returns 1 on success, 0 on a bad file (nothing stays allocated then). */
int Dod_NavLoad(DodNav *nav, const unsigned char *data, int len);
void Dod_NavFree(DodNav *nav);
void Dod_NavNodePos(const DodNav *nav, int node, float *x, float *y, float *z);
/* Edges whose segment crosses one of the boxes (each grown by 24 on x and y, 36 on z): the ones worth a hull trace when a wall that can
 * toggle is solid. out receives edge indexes (0..edge_count-1); returns how many (up to max). */
int Dod_NavEdgesCrossing(const DodNav *nav, const float (*mins)[3], const float (*maxs)[3], int box_count, int *out, int max);
void Dod_NavEdgeNodes(const DodNav *nav, int edge, int *from, int *to);
/* Blocked edges are skipped by A*. Clear returns how many were blocked. */
void Dod_NavSetEdgeBlocked(DodNav *nav, int edge, int blocked);
int Dod_NavClearEdgeBlocks(DodNav *nav);
int Dod_NavEdgeBlocked(const DodNav *nav, int edge);
/* The kind of the edge from one node to another, or -1 when there is none. */
int Dod_NavEdgeKind(const DodNav *nav, int from, int to);
/* Up to max node indexes nearest to (x, y, z), nearest first; returns how many. */
int Dod_NavNearest(const DodNav *nav, float x, float y, float z, int *out, int max);
/* Edges that proved impassable cost much more until `until` (the search still uses them when nothing else
 * connects). from == -1 marks a node as a bad place to start from. Dod_NavSetTime gives the search the clock. */
void Dod_NavPenalize(DodNav *nav, int from, int to, float until);
int Dod_NavPenalized(const DodNav *nav, int from, int to);
void Dod_NavSetTime(DodNav *nav, float now);
/* Register a damage zone (a box) that hurts the teams in team_mask (bit 1 << DodTeam); up to DOD_NAV_MAX_ZONES. */
void Dod_NavAddHazard(DodNav *nav, const float mins[3], const float maxs[3], int team_mask);
/* The team the next searches are for: edges that cross a zone hurting it cost DOD_NAV_HAZARD_COST more. 0 = none. */
void Dod_NavSetAvoidTeam(DodNav *nav, DodTeam team);
/* Is a player of this team standing at (x, y, z) (origin) inside a damage zone that hurts it? */
int Dod_NavInHazard(const DodNav *nav, DodTeam team, float x, float y, float z);	/* margin 16: the hull touches the zone */
/* The same with the box grown by `margin` sideways (0 = the origin is strictly inside it). */
int Dod_NavInHazardMargin(const DodNav *nav, DodTeam team, float x, float y, float z, float margin);
/* Up to max nodes inside a capture volume (the box grown by margin sideways and 72 vertically), nearest to its
 * centre first; returns how many. A bot heading for a point should route to one of these, not to the node nearest
 * the centre, which can lie outside a large or odd-shaped volume. */
int Dod_NavInside(const DodNav *nav, const float mins[3], const float maxs[3], float margin, int *out, int max);
/* A* from one node to another. Fills path (from first to last, inclusive) and returns its length, or 0 when
 * there is no path or it is longer than max_path. */
int Dod_NavPath(DodNav *nav, int from, int to, int *path, int max_path);
/* The same search in slices. Begin starts it for `owner` (returns 1; also 1 when the owner is already running this
 * very search; 0 when another owner's search is in flight and was stepped within the last second, which is left
 * alone). Step expands at most `budget` nodes: returns 0 while it goes on, the path length when done, -1 when
 * there is no path (or it is longer than max_path). Only one search is in flight, so a plan call stays small. */
int Dod_NavSearchBegin(DodNav *nav, int from, int to, int owner, float now);
int Dod_NavSearchStep(DodNav *nav, int budget, float now, int *path, int max_path);

/* ---------------------------------------------------------------- spectator
 * A spectator is the local human watching a bot match. It takes the human's own
 * client slot (GoldSrc has no slotless spectator; HLTV also needs one), so the
 * cost is a few static bytes. The hlsdk observer code does the camera work (chase,
 * in-eye, roaming; fire = next, alt-fire = previous, jump = mode). These helpers
 * decide what is worth watching and hide the overview-map modes, which need
 * map overview data the GameCube build does not ship. */

/* Observer modes, matching the HLSDK OBS_* values in const.h. */
#define DOD_SPEC_CHASE_LOCKED 1
#define DOD_SPEC_CHASE_FREE 2
#define DOD_SPEC_ROAMING 3
#define DOD_SPEC_IN_EYE 4
#define DOD_SPEC_MAP_FREE 5
#define DOD_SPEC_MAP_CHASE 6

/* Map the HLSDK's mode to one the GameCube can show: the two overview-map
 * modes fall back to a free chase camera, anything unknown to in-eye. */
int Dod_SpecSanitizeMode(int mode);

/* Seconds the director stays on a target at least / at most. */
#define DOD_SPEC_MIN_DWELL 4.0f
#define DOD_SPEC_MAX_DWELL 14.0f
/* Seconds the director stays quiet after the spectator picks a target by hand. */
#define DOD_SPEC_MANUAL_HOLD 20.0f

typedef struct DodSpecCandidate {
	int index;		/* entity index of the player */
	int alive;
	float enemy_distance;	/* to the nearest living enemy, < 0 when none */
} DodSpecCandidate;

/* How interesting a candidate is to watch: 0 for the dead, otherwise higher the
 * closer it is to an enemy (a fight), with a small floor so a lone walker still
 * beats nothing. */
float Dod_SpecInterest(const DodSpecCandidate *c);

/* The next (or previous) living candidate after current in list order, wrapping
 * around; current may be 0 or not in the list. Returns an entity index or 0. */
int Dod_SpecCycle(const DodSpecCandidate *list, int count, int current, int reverse);

/* The auto director: which candidate to follow. Keeps current until it has been
 * watched DOD_SPEC_MIN_DWELL seconds and is dead or clearly duller than the best
 * (best interest > current + 25%), or DOD_SPEC_MAX_DWELL elapsed. Returns an
 * entity index, current when nothing better, 0 when nobody is alive. */
int Dod_SpecDirectorPick(const DodSpecCandidate *list, int count, int current, float dwell);

/* ---------------------------------------------------------------- team intel, patrol and search
 * Natural team deathmatch play: nobody knows where the enemy is, so bots share what they actually saw, heard or lost a
 * mate to (a short per-team list of contacts that fade), roam towards where fights happen when nothing else is asked
 * of them, and sweep the area around a lost target instead of standing where it last was. Each bot also has a fixed
 * aggression (0..1) that decides how readily it answers a mate's callout, so a team does not move as one blob.
 * All allocation free and engine independent (host tested). */
#define DOD_INTEL_SLOTS 6
#define DOD_INTEL_MERGE_RADIUS 250.0f
typedef enum { DOD_INTEL_HEARD = 1, DOD_INTEL_DEATH = 2, DOD_INTEL_SEEN = 3 } DodIntelKind;

typedef struct DodIntelEntry {
	float pos[3];
	float placed, until;	/* when it was reported, and when it stops being worth chasing */
	int kind;		/* DodIntelKind, 0 = empty slot */
	int by;			/* who reported it last (a player index), so nobody answers their own call-out */
} DodIntelEntry;

typedef struct DodTeamIntel {
	DodIntelEntry team[2][DOD_INTEL_SLOTS];	/* [0] Allies, [1] Axis: where that team believes enemies are */
} DodTeamIntel;

void Dod_IntelReset(DodTeamIntel *intel);
/* A contact for `team` (the reporter's side): merged into one within DOD_INTEL_MERGE_RADIUS (the newer position wins
 * and the stronger kind stays), else it takes an empty or the oldest slot. */
void Dod_IntelReport(DodTeamIntel *intel, DodTeam team, const float pos[3], int kind, int by, float now, float ttl);
/* The contact worth answering: fresh and strong beat old and weak, near beats far, anything inside min_dist of `from`
 * (the bot is already there) or beyond max_dist is skipped. Fills out_pos and out_placed; returns the kind or 0. */
int Dod_IntelPick(const DodTeamIntel *intel, DodTeam team, const float from[3], int self, float after, float now, float min_dist, float max_dist, float out_pos[3], float *out_placed);
/* A bot got there and found nothing: drop the contacts within radius of pos. */
void Dod_IntelClearNear(DodTeamIntel *intel, DodTeam team, const float pos[3], float radius);
/* 0..1, fixed per bot (a hash of its slot and the skill), so a team has hunters and holders. */
float Dod_BotAggression(int bot_index, int skill);
/* Does a bot answer a contact? p = aggression, halved for a defender, falling with distance (to nothing at 2400);
 * a death callout counts for less than a sighting. roll in [0, 1). */
int Dod_BotAnswersIntel(float aggression, int role, float distance, int kind, float roll);
/* A node to walk to when there is no objective or no lead: one of the candidate nodes, scored by how close it is to
 * `ref` (where fights are: the enemy's side), at least min_own from the bot, away from the bot's last picks and from where
 * mates are already heading. If ref_radius > 0 only nodes within it of ref qualify (a search around a lost target).
 * Returns the best node or -1. */
int Dod_BotPickPatrol(const DodNav *nav, const float own[3], float min_own, const float ref[3], float ref_radius,
	const int *recent, int recent_count, const float (*avoid)[3], int avoid_count, const int *cand, int cand_count);

#ifdef __cplusplus
}
#endif

#endif
