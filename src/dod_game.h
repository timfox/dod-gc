/* Day of Defeat game-mode surface (HL1SDK deathmatch foundations).
 *
 * Steam DoD ships assets only; retail client is vgui2-blocked on stock Xash.
 * This header is the local port contract — implement against hlsdk-portable
 * deathmatch, then link via HLSDK_GAMECUBE_GAMEDIR=dod into dodgc.dol.
 */
#ifndef DODGC_DOD_GAME_H
#define DODGC_DOD_GAME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Teams mirror retail DoD (Allies / Axis). */
typedef enum {
	DOD_TEAM_UNASSIGNED = 0,
	DOD_TEAM_ALLIES = 1,
	DOD_TEAM_AXIS = 2,
	DOD_TEAM_SPECTATOR = 3
} DodTeam;

/* Class slots — expand as weapons/models are wired to content/dod. */
typedef enum {
	DOD_CLASS_NONE = 0,
	DOD_CLASS_RIFLEMAN,
	DOD_CLASS_ASSAULT,
	DOD_CLASS_SUPPORT,
	DOD_CLASS_SNIPER,
	DOD_CLASS_MG,
	DOD_CLASS_ROCKET
} DodClass;

/* Objective kinds used by DoD maps (capture / destroy / tick). */
typedef enum {
	DOD_OBJ_NONE = 0,
	DOD_OBJ_CAPTURE_POINT,
	DOD_OBJ_DESTROY_TARGET,
	DOD_OBJ_TICK_CONTROL
} DodObjectiveKind;

typedef struct DodGameRulesState {
	int round_active;
	int allies_score;
	int axis_score;
	float round_time_remaining;
} DodGameRulesState;

/* Lifecycle hooks — stubs until wired into HLSDK gamerules. */
void Dod_GameInit(void);
void Dod_GameShutdown(void);
void Dod_Think(float frametime);
const DodGameRulesState *Dod_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* DODGC_DOD_GAME_H */
