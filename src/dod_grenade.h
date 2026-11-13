#ifndef DODGC_DOD_GRENADE_H
#define DODGC_DOD_GRENADE_H

/* Day of Defeat hand grenades for the HL1SDK game DLL.
 *
 * The HL1 CHandGrenade/CGrenade code stays the throwing and bouncing
 * implementation. This layer swaps in the DoD models, fuse and damage, caps
 * live grenades, and replaces the explosion with a budgeted effect plan so
 * the GameCube's MEM1 temp-entity pool and sprite memory stay bounded. */

/* Include after extdll.h/util.h/cbase.h: BOOL, TraceResult and CGrenade come from them. */
class CBasePlayer;
class CGrenade;

#define DODGC_GRENADE_MAGIC 0xD0D60001

/* Precache the models, sprites and sounds grenades use. Called from the
 * weapon's Precache(); nothing else is loaded for grenades. */
void DodGrenade_Precache( void );

/* True when a new pin may be pulled (live cap not reached). */
BOOL DodGrenade_CanPrime( void );

/* Create the thrown grenade for the player's team. held is how long the pin
 * was out (cooking shortens the fuse). Returns NULL if the DoD game is not
 * active, so the caller can fall back to the stock HL1 throw. */
CGrenade *DodGrenade_Throw( CBasePlayer *pPlayer, float held );

/* Replacement for CGrenade::Explode( TraceResult *, int ). Returns TRUE when
 * the grenade is a DoD grenade and the explosion was handled. */
BOOL DodGrenade_Explode( CGrenade *pGrenade, TraceResult *pTrace, int bitsDamageType );

#endif
