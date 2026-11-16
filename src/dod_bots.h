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

/* Turn from current toward target (degrees) by at most max_step, the short
 * way around. Result is normalised to [-180, 180). */
float Dod_BotTurn(float current, float target, float max_step);
/* Shortest signed angle from a to b in degrees, in [-180, 180). */
float Dod_BotAngleDelta(float a, float b);

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

#ifdef __cplusplus
}
#endif

#endif
