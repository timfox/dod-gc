/* Day of Defeat hand grenades on top of the HL1SDK CHandGrenade/CGrenade. */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "player.h"
#include "decals.h"
#include "dod_game.h"
#include "dod_grenade.h"

/* g_sModelIndexFireball / WExplosion / Smoke come from weapons.h. They are
 * already resident: the stock HL1 precache loads them once for every weapon,
 * so grenades add no sprite memory of their own. */

/* TE_EXPLOSION flag bits (common/const.h). */
#ifndef TE_EXPLFLAG_NODLIGHTS
#define TE_EXPLFLAG_NODLIGHTS 2
#endif
#ifndef TE_EXPLFLAG_NOPARTICLES
#define TE_EXPLFLAG_NOPARTICLES 8
#endif

static DodTeam DodGrenade_TeamForPlayer( CBasePlayer *pPlayer )
{
	if( !pPlayer )
		return DOD_TEAM_UNASSIGNED;
	return (DodTeam)Dod_TeamFromName( pPlayer->m_szTeamName );
}

void DodGrenade_Precache( void )
{
	DodGrenadeDef allies, axis;

	if( !Dod_GetGrenade( DOD_TEAM_ALLIES, &allies ) || !Dod_GetGrenade( DOD_TEAM_AXIS, &axis ) )
		return;
	/* Both sides are needed: a client can pick either team after the map
	 * loads, and precache is closed once play starts. The engine keeps the
	 * whole files in its ARAM asset cache, so a team switch is not a disc read. */
	PRECACHE_MODEL( (char *)allies.world_model );
	PRECACHE_MODEL( (char *)allies.view_model );
	PRECACHE_MODEL( (char *)allies.player_model );
	PRECACHE_MODEL( (char *)axis.world_model );
	PRECACHE_MODEL( (char *)axis.view_model );
	PRECACHE_MODEL( (char *)axis.player_model );
	PRECACHE_SOUND( "weapons/grenpinpull.wav" );
	PRECACHE_SOUND( "weapons/grenthrow.wav" );
}

BOOL DodGrenade_CanPrime( void )
{
	if( !Dod_GameIsActive() )
		return TRUE;
	return Dod_GrenadesLive() < DOD_MAX_LIVE_GRENADES;
}

CGrenade *DodGrenade_Throw( CBasePlayer *pPlayer, float held )
{
	DodGrenadeDef def;
	Vector angThrow;
	CGrenade *pGrenade;

	if( !Dod_GameIsActive() || !pPlayer || !Dod_GetGrenade( DodGrenade_TeamForPlayer( pPlayer ), &def ) )
		return NULL;

	angThrow = pPlayer->pev->v_angle + pPlayer->pev->punchangle;
	{
		const float speed = Dod_GrenadeThrowSpeed( &def, angThrow.x );
		angThrow.x = Dod_GrenadeThrowPitch( angThrow.x );
		UTIL_MakeVectors( angThrow );
		Vector vecSrc = pPlayer->pev->origin + pPlayer->pev->view_ofs + gpGlobals->v_forward * 16.0f;
		Vector vecThrow = gpGlobals->v_forward * speed + pPlayer->pev->velocity;

		/* A grenade cooked past its fuse goes off in the thrower's hand. */
		pGrenade = CGrenade::ShootTimed( pPlayer->pev, vecSrc, vecThrow, Dod_GrenadeFuseLeft( &def, held ) );
	}
	if( !pGrenade )
		return NULL;

	if( strcmp( def.world_model, "models/w_grenade.mdl" ) )
		SET_MODEL( ENT( pGrenade->pev ), def.world_model );
	pGrenade->pev->dmg = def.damage;
	pGrenade->pev->sequence = 0;
	pGrenade->pev->framerate = 1.0f;
	pGrenade->pev->iuser3 = (int)DODGC_GRENADE_MAGIC;
	/* Only grenades that took a slot give one back, so an over-cap throw
	 * (the pin was pulled before the cap filled) cannot underflow the count. */
	pGrenade->pev->iuser4 = Dod_GrenadeAcquire();

	EMIT_SOUND( ENT( pPlayer->pev ), CHAN_WEAPON, "weapons/grenthrow.wav", 1.0f, ATTN_NORM );
	return pGrenade;
}

static void DodGrenade_SendSparks( const Vector &origin )
{
	MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, origin );
		WRITE_BYTE( TE_SPARKS );
		WRITE_COORD( origin.x );
		WRITE_COORD( origin.y );
		WRITE_COORD( origin.z );
	MESSAGE_END();
}

static void DodGrenade_SendDebris( const Vector &origin, const Vector &normal )
{
	MESSAGE_BEGIN( MSG_PVS, SVC_TEMPENTITY, origin );
		WRITE_BYTE( TE_STREAK_SPLASH );
		WRITE_COORD( origin.x );
		WRITE_COORD( origin.y );
		WRITE_COORD( origin.z );
		WRITE_COORD( normal.x );
		WRITE_COORD( normal.y );
		WRITE_COORD( normal.z );
		WRITE_BYTE( 5 );	/* palette: orange */
		WRITE_SHORT( 24 );	/* streaks */
		WRITE_SHORT( 140 );	/* speed */
		WRITE_SHORT( 80 );	/* random speed */
	MESSAGE_END();
}

BOOL DodGrenade_Explode( CGrenade *pGrenade, TraceResult *pTrace, int bitsDamageType )
{
	entvars_t *pev, *pevOwner;
	DodGrenadeDef def;
	int plan, contents, flags;
	BOOL water;

	if( !pGrenade || !pGrenade->pev || pGrenade->pev->iuser3 != (int)DODGC_GRENADE_MAGIC )
		return FALSE;
	pev = pGrenade->pev;
	/* Both teams share damage and radius; only models differ. */
	if( !Dod_GetGrenade( DOD_TEAM_ALLIES, &def ) )
		return FALSE;
	/* Never explode twice: a grenade shot or touched mid-blast. */
	pev->iuser3 = 0;
	if( pev->iuser4 )
	{
		Dod_GrenadeRelease();
		pev->iuser4 = 0;
	}

	pev->model = iStringNull;
	pev->solid = SOLID_NOT;
	pev->takedamage = DAMAGE_NO;

	if( pTrace->flFraction != 1.0f )
		pev->origin = pTrace->vecEndPos + ( pTrace->vecPlaneNormal * 0.6f );

	contents = UTIL_PointContents( pev->origin );
	water = ( contents == CONTENTS_WATER );
	plan = Dod_FxPlanExplosion( gpGlobals->time );

	/* Fireball (+ its dynamic light and sound). Smaller plans drop the
	 * client-side particle burst and the light first. */
	flags = TE_EXPLFLAG_NONE;
	if( !( plan & DOD_FX_SPARKS ) )
		flags |= TE_EXPLFLAG_NOPARTICLES;
	if( !( plan & DOD_FX_SMOKE ) )
		flags |= TE_EXPLFLAG_NODLIGHTS;
	MESSAGE_BEGIN( MSG_PAS, SVC_TEMPENTITY, pev->origin );
		WRITE_BYTE( TE_EXPLOSION );
		WRITE_COORD( pev->origin.x );
		WRITE_COORD( pev->origin.y );
		WRITE_COORD( pev->origin.z );
		WRITE_SHORT( water ? g_sModelIndexWExplosion : g_sModelIndexFireball );
		WRITE_BYTE( (int)( ( def.damage - 50.0f ) * 0.6f ) );	/* scale * 10 */
		WRITE_BYTE( 15 );	/* framerate */
		WRITE_BYTE( flags );
	MESSAGE_END();

	if( !water )
	{
		if( plan & DOD_FX_SPARKS )
			DodGrenade_SendSparks( pev->origin );
		if( plan & DOD_FX_DEBRIS )
			DodGrenade_SendDebris( pev->origin, pTrace->vecPlaneNormal );
	}

	pevOwner = pev->owner ? VARS( pev->owner ) : NULL;
	pev->owner = NULL;	/* the thrower can be hurt by their own blast */
	/* radius is explicit: 2.5 x damage, the same value HL1 would derive */
	::RadiusDamage( pev->origin, pev, pevOwner, pev->dmg, def.radius, CLASS_NONE, bitsDamageType );

	UTIL_DecalTrace( pTrace, RANDOM_LONG( 0, 1 ) ? DECAL_SCORCH2 : DECAL_SCORCH1 );
	switch( RANDOM_LONG( 0, 2 ) )
	{
	case 0: EMIT_SOUND( ENT( pev ), CHAN_VOICE, "weapons/debris1.wav", 0.55f, ATTN_NORM ); break;
	case 1: EMIT_SOUND( ENT( pev ), CHAN_VOICE, "weapons/debris2.wav", 0.55f, ATTN_NORM ); break;
	default: EMIT_SOUND( ENT( pev ), CHAN_VOICE, "weapons/debris3.wav", 0.55f, ATTN_NORM ); break;
	}

	pev->effects |= EF_NODRAW;
	pev->velocity = g_vecZero;
	if( ( plan & DOD_FX_SMOKE ) && !water )
	{
		pGrenade->SetThink( &CGrenade::Smoke );
		pev->nextthink = gpGlobals->time + 0.3f;
	}
	else
	{
		/* Under a barrage or under water the grenade just goes away. */
		if( water )
			UTIL_Bubbles( pev->origin - Vector( 64, 64, 64 ), pev->origin + Vector( 64, 64, 64 ), 100 );
		pGrenade->SetThink( &CBaseEntity::SUB_Remove );
		pev->nextthink = gpGlobals->time + 0.1f;
	}
	return TRUE;
}
