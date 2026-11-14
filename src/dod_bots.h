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

#ifdef __cplusplus
}
#endif

#endif
