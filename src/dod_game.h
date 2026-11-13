/* Day of Defeat rules core for the HL1SDK based GameCube port. */
#ifndef DODGC_DOD_GAME_H
#define DODGC_DOD_GAME_H

#ifdef __cplusplus
extern "C" {
#endif

#define DOD_MAX_PLAYERS 32
#define DOD_MAX_OBJECTIVES 32

typedef enum {
	DOD_TEAM_UNASSIGNED = 0,
	DOD_TEAM_ALLIES = 1,
	DOD_TEAM_AXIS = 2,
	DOD_TEAM_SPECTATOR = 3
} DodTeam;

typedef enum {
	DOD_CLASS_NONE = 0,
	DOD_CLASS_RIFLEMAN,
	DOD_CLASS_ASSAULT,
	DOD_CLASS_SUPPORT,
	DOD_CLASS_SNIPER,
	DOD_CLASS_MG,
	DOD_CLASS_ROCKET,
	DOD_CLASS_COUNT
} DodClass;

typedef enum {
	DOD_OBJ_NONE = 0,
	DOD_OBJ_CAPTURE_POINT,
	DOD_OBJ_DESTROY_TARGET,
	DOD_OBJ_TICK_CONTROL
} DodObjectiveKind;

typedef enum {
	DOD_MODEL_MELEE = 0,
	DOD_MODEL_AUTOMATIC,
	DOD_MODEL_SUPPORT,
	DOD_MODEL_SNIPER,
	DOD_MODEL_ROCKET,
	DOD_MODEL_PISTOL,
	DOD_MODEL_GRENADE
} DodWeaponModelKind;

typedef enum {
	DOD_ROUND_WAITING = 0,
	DOD_ROUND_ACTIVE,
	DOD_ROUND_INTERMISSION
} DodRoundPhase;

typedef struct DodPlayerState {
	int connected;
	DodTeam team;
	DodClass player_class;
	int kills;
	int deaths;
	int objective_score;
} DodPlayerState;

typedef struct DodObjectiveState {
	int active;
	int id;
	DodObjectiveKind kind;
	DodTeam owner;
	DodTeam capturing_team;
	float capture_progress;
	float capture_seconds;
	float score_per_minute;
} DodObjectiveState;

typedef struct DodGameRulesState {
	DodRoundPhase phase;
	DodTeam winner;
	int round_active;
	int allies_score;
	int axis_score;
	float round_time_remaining;
	float intermission_remaining;
	int connected_players[4];
	int controlled_objectives[4];
	int objective_count;
} DodGameRulesState;

/* Game lifecycle and round clock. A zero duration disables the time limit. */
void Dod_GameInit(void);
void Dod_GameShutdown(void);
int Dod_GameIsActive(void);
void Dod_StartRound(float duration_seconds);
void Dod_EndRound(DodTeam winner);
void Dod_Think(float frametime);
const DodGameRulesState *Dod_GetState(void);

/* Player state. Player indices are the engine's 1-based client slots. */
int Dod_PlayerConnect(int player_index);
void Dod_PlayerDisconnect(int player_index);
int Dod_AssignPlayerTeam(int player_index, DodTeam team);
int Dod_SelectPlayerClass(int player_index, DodClass player_class);
void Dod_RecordKill(int attacker_index, int victim_index);
const DodPlayerState *Dod_GetPlayer(int player_index);
int Dod_TeamFromName(const char *name);
const char *Dod_TeamName(DodTeam team);
const char *Dod_ClassName(DodClass player_class);
/* Retail DoD class title for a team ("Staff Sergeant", "Stosstruppe", ...). */
const char *Dod_TeamClassName(DodTeam team, DodClass player_class);
/* Retail primary weapon title for the class on that team ("Thompson", ...). */
const char *Dod_TeamClassWeapon(DodTeam team, DodClass player_class);
/* Approximate DoD 1.3: heavy and special classes spawn with a Colt/Luger. */
int Dod_ClassHasSidearm(DodClass player_class);
int Dod_GetWeaponModels(int player_index, DodWeaponModelKind kind,
	const char **view_model, const char **player_model);

/* Retail-style class loadout (approximate DoD 1.3 values: magazine size and
 * spare rounds issued at spawn). The runtime maps these onto the HL1 stand-in
 * weapon's ammo type until real DoD weapon entities exist. */
typedef struct DodLoadout {
	const char *weapon;
	int clip_size;
	int reserve_rounds;
	int grenades;
} DodLoadout;

int Dod_GetLoadout(DodTeam team, DodClass player_class, DodLoadout *out);

/* Hand grenades. Allies throw the Mk2 pineapple, Axis the stick grenade; values
 * are approximate DoD 1.3. radius follows the HL1 RadiusDamage rule (2.5 x
 * damage) so the falloff below matches what the game DLL really applies. */
typedef struct DodGrenadeDef {
	const char *view_model;
	const char *player_model;
	const char *world_model;
	float fuse;		/* seconds from pin pull to detonation */
	float damage;		/* at the blast centre */
	float radius;		/* GoldSrc units; zero damage beyond it */
	float throw_speed;	/* maximum release speed, units/second */
	int max_carry;
} DodGrenadeDef;

int Dod_GetGrenade(DodTeam team, DodGrenadeDef *out);
/* Linear falloff from centre to radius; zero outside it or for bad input. */
float Dod_GrenadeDamageAt(const DodGrenadeDef *def, float distance);
/* The HL1 hand grenade lifts the aim 10 degrees and stretches it; this is the
 * pitch the grenade is actually thrown along (negative looks up). */
float Dod_GrenadeThrowPitch(float view_pitch);
/* Throw speed for a view pitch in degrees, clamped to the def's maximum.
 * Looking up lobs further, down is a short toss. */
float Dod_GrenadeThrowSpeed(const DodGrenadeDef *def, float view_pitch);
/* Fuse left after cooking for held_seconds; never negative. */
float Dod_GrenadeFuseLeft(const DodGrenadeDef *def, float held_seconds);

/* Live grenade entities are capped so the static edict pool and the client
 * temp-entity budget cannot be exhausted by grenade spam. Acquire before
 * creating the entity (0 = refuse), release when it detonates or is removed. */
#define DOD_MAX_LIVE_GRENADES 8
int Dod_GrenadeAcquire(void);
void Dod_GrenadeRelease(void);
int Dod_GrenadesLive(void);

/* Explosion effect budget. Each blast plans which effect layers to send; the
 * layers shrink as blasts arrive within DOD_FX_WINDOW seconds of each other so
 * the client's tempent and sprite memory stays bounded. The fireball (and its
 * damage) is never skipped. */
#define DOD_FX_FIREBALL 1
#define DOD_FX_SMOKE 2
#define DOD_FX_SPARKS 4
#define DOD_FX_DEBRIS 8
#define DOD_FX_WINDOW 2.0f
int Dod_FxPlanExplosion(float now);

/* One-line match status for the HUD, e.g. "Allies 5  Axis 3 | Points 2-1 | 12:34".
 * Returns the number of characters written (excluding the terminator). */
int Dod_FormatStatus(char *buffer, int size);

/* Scoreboard line for one connected player, e.g. "Allies  Bob  5/2".
 * Returns characters written, or 0 if the player is not connected. */
int Dod_FormatPlayerLine(int player_index, const char *name, char *buffer, int size);

/* Objective registration and capture. Counts may be zero; contested zones
 * pause. Capture is normalized from 0 (uncaptured) to 1 (complete). */
void Dod_ClearObjectives(void);
int Dod_RegisterObjective(int id, DodObjectiveKind kind, DodTeam initial_owner,
	float capture_seconds, float score_per_minute);
int Dod_TickObjective(int id, int allies_inside, int axis_inside, float frametime);
int Dod_SetObjectiveOwner(int id, DodTeam owner);
const DodObjectiveState *Dod_GetObjective(int id);

#ifdef __cplusplus
}
#endif

#endif /* DODGC_DOD_GAME_H */
