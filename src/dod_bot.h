#ifndef DODGC_DOD_BOT_H
#define DODGC_DOD_BOT_H

/* Day of Defeat bots for the HL1SDK game DLL (fake clients). Logic lives in
 * dod_bots.c; this file only drives the engine. Include after extdll.h/util.h/
 * cbase.h/player.h. */

/* Register the cvars; called when the DoD rules are created. */
void DodBots_Init( void );
/* Once per server frame: keep the bot count at dod_bots, run each bot's AI. */
void DodBots_Think( void );
/* True while a bot's loadout is being granted: its weapons skip the viewmodel
 * precache (a bot has no view), so it never spends the human's viewmodel budget. */
void DodBots_SetGranting( BOOL granting );
BOOL DodBots_Granting( void );
BOOL DodBots_IsBot( CBasePlayer *pPlayer );

#endif
