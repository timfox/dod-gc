#ifndef DODGC_DOD_RUNTIME_H
#define DODGC_DOD_RUNTIME_H

#include "teamplay_gamerules.h"
#include "dod_game.h"

class CHalfLifeDoD : public CHalfLifeTeamplay
{
public:
	CHalfLifeDoD();
	virtual BOOL ClientCommand( CBasePlayer *pPlayer, const char *pcmd );
	virtual void InitHUD( CBasePlayer *pPlayer );
	virtual void PlayerSpawn( CBasePlayer *pPlayer );
	virtual void PlayerKilled( CBasePlayer *pVictim, entvars_t *pKiller, entvars_t *pInflictor );
	virtual void ClientDisconnected( edict_t *pClient );
	virtual void Think( void );
	virtual void ClientUserInfoChanged( CBasePlayer *pPlayer, char *infobuffer );
	virtual const char *GetGameDescription( void ) { return "Day of Defeat"; }
	virtual edict_t *GetPlayerSpawnSpot( CBasePlayer *pPlayer );
	virtual int DeadPlayerWeapons( CBasePlayer *pPlayer );
	virtual int DeadPlayerAmmo( CBasePlayer *pPlayer );
	virtual BOOL FPlayerCanRespawn( CBasePlayer *pPlayer );
};

/* A control point as the bots see it: who owns it, who is capturing it, who may capture it, and
 * the volume a capturer has to stand in (its dod_capture_area, else a sphere around the point). */
typedef struct DodGCObjectiveView {
	int id;
	DodTeam owner;
	DodTeam capturing;
	int can_cap[4];		/* indexed by DodTeam */
	int num_cap[4];		/* players of that team that must be inside to capture (1 on most points) */
	Vector center, mins, maxs;
} DodGCObjectiveView;

/* Fill up to max views, one per control point; returns the count. */
int DodGC_ListObjectives( DodGCObjectiveView *out, int max );

/* trigger_hurt team filter (spawnflag 64 hurts Axis only, 128 Allies only). */
BOOL DodGC_TriggerHurtApplies( int spawnflags, CBaseEntity *pOther );

void DodGC_ResetMapObjectives( void );
/* A voice line (Dod_VoiceFind names: "enemyahead", "fireinhole"...) from a living player of a side, in that side's
 * voice, with a per-player cooldown so a bot or a grenade spree does not chatter. Returns TRUE when it played. */
BOOL DodGC_Voice( CBasePlayer *pPlayer, int voice, BOOL automatic );
void DodGC_VoiceAuto( CBasePlayer *pPlayer, const char *name );
/* Put a bot on a team with a class: the same steps a human's menu choices take. */
void DodGC_BotJoin( CBasePlayer *pPlayer, DodTeam team, DodClass player_class );
/* The first class open under the class limits, starting at the one asked for (the asked one when all are closed). */
DodClass DodGC_OpenClass( CBasePlayer *pPlayer, DodTeam team, DodClass wanted );
/* A human joins as the menus do (the test harness): spawns out of observer mode. */
void DodGC_HumanJoin( CBasePlayer *pPlayer, DodTeam team, DodClass player_class );

/* The player model name a side wears on this map ("us-inf", "brit-inf", "axis-para"...). */
const char *DodGC_BodyModelForTeam( DodTeam team );
/* Film actors (dod_film_actor.cpp): the film_cast commands and the cast's precache, from the
 * gamerules constructor; moving the actors, from its Think. */
void DodFilm_Init( void );
void DodFilm_Think( void );

#endif
