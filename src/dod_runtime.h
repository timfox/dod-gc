#ifndef DODGC_DOD_RUNTIME_H
#define DODGC_DOD_RUNTIME_H

#include "teamplay_gamerules.h"

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
	virtual const char *GetGameDescription( void ) { return "Day of Defeat"; }
	virtual edict_t *GetPlayerSpawnSpot( CBasePlayer *pPlayer );
};

void DodGC_ResetMapObjectives( void );

#endif
