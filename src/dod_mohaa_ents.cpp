/* dod_mohaa_ents.cpp - the HL1SDK side of the MOHAA script binding (see dod_mohaa.h). */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "gamerules.h"
#include "dod_game.h"
#include "dod_runtime.h"
#include "dod_mohaa.h"
#include "dod_mohaa_ents.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Script memory cap in KiB. MEM1 is tight on GameCube (see docs/MEMORY.md); a level whose
 * scripts do not fit is refused cleanly at load instead of starving the engine. */
static cvar_t mohaa_script_kb = { "mohaa_script_kb", "384", FCVAR_SERVER };
static int g_mohaa_cvar_registered;

/* id = edict index (12 bits) | serial number (20 bits): a freed and reused edict does
 * not answer for an old script reference */
static uint32_t IdFor( edict_t *e )
{
	if( !e || e->free )
		return 0;
	return (uint32_t)ENTINDEX( e ) | ( (uint32_t)( e->serialnumber & 0xFFFFF ) << 12 );
}

static edict_t *EdictFor( uint32_t id )
{
	int index = (int)( id & 0xFFF );
	if( !id || index <= 0 || index >= gpGlobals->maxEntities )
		return NULL;
	edict_t *e = INDEXENT( index );
	if( !e || e->free || (uint32_t)( e->serialnumber & 0xFFFFF ) != ( id >> 12 ) || !e->pvPrivateData )
		return NULL;
	return e;
}

static char *M_Load( const char *path, size_t *len )
{
	int n = 0;
	byte *data = LOAD_FILE_FOR_ME( (char *)path, &n );
	if( !data )
		return NULL;
	*len = (size_t)n;
	return (char *)data;
}

static void M_Free( char *text )
{
	FREE_FILE( text );
}

static void M_Print( int channel, const char *text )
{
	switch( channel )
	{
	case DOD_MOHAA_PRINT_HUD:
		UTIL_ClientPrintAll( HUD_PRINTTALK, text );
		break;
	case DOD_MOHAA_PRINT_CENTER:
		UTIL_ClientPrintAll( HUD_PRINTCENTER, text );
		break;
	case DOD_MOHAA_PRINT_ERROR:
		ALERT( at_console, "%s", text );
		break;
	default:
		ALERT( at_aiconsole, "%s", text );
		break;
	}
}

static int M_Find( const char *name, uint32_t *ids, int max )
{
	int n = 0;
	edict_t *e = NULL;
	while( !FNullEnt( e = FIND_ENTITY_BY_TARGETNAME( e, name ) ) )
	{
		if( n < max )
			ids[n] = IdFor( e );
		n++;
	}
	/* MOHAA's "$player" is the (first) player, who has no targetname in GoldSrc */
	if( !n && !strcmp( name, "player" ) )
	{
		CBaseEntity *p = UTIL_PlayerByIndex( 1 );
		if( p && max > 0 )
			ids[0] = IdFor( p->edict() );
		n = p ? 1 : 0;
	}
	return n;
}

static int M_Valid( uint32_t id )
{
	return EdictFor( id ) != NULL;
}

static void M_GetVec( uint32_t id, int what, float out[3] )
{
	edict_t *e = EdictFor( id );
	Vector v = g_vecZero;
	if( e )
		v = what == DOD_MOHAA_ANGLES ? e->v.angles : e->v.origin;
	out[0] = v.x;
	out[1] = v.y;
	out[2] = v.z;
}

static void M_SetVec( uint32_t id, int what, const float v[3] )
{
	edict_t *e = EdictFor( id );
	if( !e )
		return;
	if( what == DOD_MOHAA_ANGLES )
		e->v.angles = Vector( v[0], v[1], v[2] );
	else
		UTIL_SetOrigin( VARS( e ), Vector( v[0], v[1], v[2] ) );
}

static float M_GetHealth( uint32_t id )
{
	edict_t *e = EdictFor( id );
	return e ? e->v.health : 0.0f;
}

static void M_SetHealth( uint32_t id, float health )
{
	edict_t *e = EdictFor( id );
	if( e )
		e->v.health = health;
}

static const char *M_GetStr( uint32_t id, int what )
{
	edict_t *e = EdictFor( id );
	if( !e )
		return "";
	string_t s = what == DOD_MOHAA_CLASSNAME ? e->v.classname : what == DOD_MOHAA_MODEL ? e->v.model : e->v.targetname;
	return s ? STRING( s ) : "";
}

static void M_SetTargetname( uint32_t id, const char *name )
{
	edict_t *e = EdictFor( id );
	if( e )
		e->v.targetname = ALLOC_STRING( name );
}

static void M_SetFlag( uint32_t id, int flag, int on )
{
	edict_t *e = EdictFor( id );
	if( !e )
		return;
	if( flag == DOD_MOHAA_HIDDEN )
	{
		if( on )
			e->v.effects |= EF_NODRAW;
		else
			e->v.effects &= ~EF_NODRAW;
		return;
	}
	if( on )
		e->v.solid = SOLID_NOT;
	else
	{
		const char *model = e->v.model ? STRING( e->v.model ) : "";
		e->v.solid = model[0] == '*' ? SOLID_BSP : SOLID_BBOX;
	}
	UTIL_SetOrigin( VARS( e ), e->v.origin );	/* relink with the new solidity */
}

static void M_Remove( uint32_t id )
{
	edict_t *e = EdictFor( id );
	if( e && DodGrunts_ScriptRemove( e ) )
		return;	/* an actor: its grunt slot goes with it */
	CBaseEntity *ent = e ? CBaseEntity::Instance( e ) : NULL;
	if( ent && !ent->IsPlayer() )
		UTIL_Remove( ent );
}

static void M_SetVelocity( uint32_t id, const float vel[3], const float avel[3] )
{
	edict_t *e = EdictFor( id );
	if( !e )
		return;
	/* brush entities (func_wall, doors) are pushers; point entities fly through */
	if( e->v.movetype == MOVETYPE_NONE )
		e->v.movetype = e->v.solid == SOLID_BSP ? MOVETYPE_PUSH : MOVETYPE_NOCLIP;
	e->v.velocity = Vector( vel[0], vel[1], vel[2] );
	e->v.avelocity = Vector( avel[0], avel[1], avel[2] );
}

static void M_UseTargets( const char *targetname )
{
	FireTargets( targetname, NULL, NULL, USE_TOGGLE, 0 );
}

/* A MOHAA AI soldier: "spawn actor ..." or "spawn models/human/<who>.tik ...". */
static int IsActorClass( const char *classname )
{
	return !strcmp( classname, "actor" ) || !strncmp( classname, "models/human/", 13 ) || !strncmp( classname, "human/", 6 );
}

static uint32_t SpawnActor( const char *classname, const char *const *keys, const char *const *values, int count )
{
	Vector origin = g_vecZero;
	float yaw = 0.0f;
	const char *targetname = NULL;
	/* German models play the Axis, everyone else the Allies (MOHAA's own split is by model too) */
	int team = strstr( classname, "german" ) ? DOD_TEAM_AXIS : DOD_TEAM_ALLIES;
	for( int i = 0; i < count; i++ ) {
		if( !strcmp( keys[i], "origin" ) )
			sscanf( values[i], "%f %f %f", &origin.x, &origin.y, &origin.z );
		else if( !strcmp( keys[i], "angle" ) )
			yaw = (float)atof( values[i] );
		else if( !strcmp( keys[i], "angles" ) )
			sscanf( values[i], "%*f %f", &yaw );
		else if( !strcmp( keys[i], "targetname" ) )
			targetname = values[i];
		else if( !strcmp( keys[i], "model" ) && strstr( values[i], "german" ) )
			team = DOD_TEAM_AXIS;
	}
	edict_t *e = DodGrunts_ScriptSpawn( team, origin, yaw );
	if( !e )
		return 0;
	if( targetname )
		e->v.targetname = ALLOC_STRING( targetname );
	return IdFor( e );
}

static int M_Actor( uint32_t id, int order, const float pos[3] )
{
	edict_t *e = EdictFor( id );
	return e ? DodGrunts_ScriptOrder( e, order, pos ) : 0;
}

static uint32_t M_Spawn( const char *classname, const char *const *keys, const char *const *values, int count )
{
	if( IsActorClass( classname ) )
		return SpawnActor( classname, keys, values, count );
	/* MOHAA's script helpers have no GoldSrc class: a point entity holds their place */
	if( !strcmp( classname, "script_model" ) || !strcmp( classname, "script_origin" ) || !strcmp( classname, "script_object" ) )
		classname = "info_target";
	edict_t *e = CREATE_NAMED_ENTITY( ALLOC_STRING( classname ) );
	if( FNullEnt( e ) )
		return 0;
	for( int i = 0; i < count; i++ )
	{
		if( !strcmp( keys[i], "model" ) )
			continue;	/* MOHAA .tik models are not converted yet; an unprecached model would abort the server */
		KeyValueData kvd;
		kvd.szClassName = (char *)classname;
		kvd.szKeyName = (char *)keys[i];
		kvd.szValue = (char *)values[i];
		kvd.fHandled = FALSE;
		DispatchKeyValue( e, &kvd );
	}
	if( DispatchSpawn( e ) < 0 )
		return 0;
	return IdFor( e );
}

static void M_Sound( uint32_t id, const char *sample, int loop )
{
	/* MOHAA plays ubersound aliases ("door_open"), not wav paths, and GoldSrc can only play
	 * precached samples. Until the alias table is converted, log what a mission asks for. */
	(void)id;
	if( loop >= 0 )
		ALERT( at_aiconsole, "mohaa: sound %s%s not mapped yet\n", sample, loop ? " (loop)" : "" );
}

void DodGC_MohaaLevelInit( void )
{
	if( !g_mohaa_cvar_registered )
	{
		CVAR_REGISTER( &mohaa_script_kb );
		g_mohaa_cvar_registered = 1;
	}
	DodMohaaBackend b;
	memset( &b, 0, sizeof( b ) );
	b.load_file = M_Load;
	b.free_file = M_Free;
	b.print = M_Print;
	b.find = M_Find;
	b.valid = M_Valid;
	b.get_vec = M_GetVec;
	b.set_vec = M_SetVec;
	b.get_health = M_GetHealth;
	b.set_health = M_SetHealth;
	b.get_str = M_GetStr;
	b.set_targetname = M_SetTargetname;
	b.set_flag = M_SetFlag;
	b.remove = M_Remove;
	b.set_velocity = M_SetVelocity;
	b.use_targets = M_UseTargets;
	b.spawn = M_Spawn;
	b.sound = M_Sound;
	b.actor = M_Actor;
	size_t budget = (size_t)CVAR_GET_FLOAT( "mohaa_script_kb" ) * 1024u;
	if( Dod_MohaaLevelInit( &b, STRING( gpGlobals->mapname ), gpGlobals->time, budget ) ) {
		DodGrunts_PrecacheBodies();	/* the mission's soldiers wear the sides' player bodies */
		ALERT( at_console, "mohaa: running maps/%s.scr (budget %u KiB)\n", STRING( gpGlobals->mapname ),
			(unsigned)( budget / 1024u ) );
	}
}

void DodGC_MohaaThink( void )
{
	Dod_MohaaThink( gpGlobals->time );
}

void DodGC_MohaaEntityEvent( edict_t *e, const char *event )
{
	uint32_t id = IdFor( e );
	if( id )
		Dod_MohaaNotify( id, event );
}
