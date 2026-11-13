/* Table-driven Day of Defeat guns; see dod_weapon.h and dod_weapons.c. */
#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "nodes.h"
#include "player.h"
#include "gamerules.h"
#include "decals.h"
#include "dod_game.h"
#include "dod_weapons.h"
#include "dod_grenade.h"
#include "dod_weapon.h"

/* Defined in weapons.cpp, declared in no header. */
extern void UTIL_PrecacheOtherWeapon( const char *szClassname );

/* ---------------------------------------------------------------- rocket */

class CDodRocket : public CGrenade
{
public:
	void Spawn( void );
	void Precache( void );
	void EXPORT RocketTouch( CBaseEntity *pOther );
	static CDodRocket *Create( const Vector &origin, const Vector &velocity, entvars_t *pevOwner, float damage );
};

LINK_ENTITY_TO_CLASS( dod_rocket, CDodRocket )

#define DOD_ROCKET_SPEED 1200.0f

void CDodRocket::Precache( void )
{
	PRECACHE_MODEL( "models/w_bazooka_rocket.mdl" );
}

void CDodRocket::Spawn( void )
{
	Precache();
	pev->movetype = MOVETYPE_FLY;
	pev->solid = SOLID_BBOX;
	pev->classname = MAKE_STRING( "dod_rocket" );
	SET_MODEL( ENT( pev ), "models/w_bazooka_rocket.mdl" );
	UTIL_SetSize( pev, g_vecZero, g_vecZero );
	UTIL_SetOrigin( pev, pev->origin );
	pev->gravity = 0.0f;
	SetThink( NULL );
	SetTouch( &CDodRocket::RocketTouch );
}

CDodRocket *CDodRocket::Create( const Vector &origin, const Vector &velocity, entvars_t *pevOwner, float damage )
{
	CDodRocket *pRocket = GetClassPtr( (CDodRocket *)NULL );
	UTIL_SetOrigin( pRocket->pev, origin );
	pRocket->Spawn();
	pRocket->pev->velocity = velocity;
	pRocket->pev->angles = UTIL_VecToAngles( velocity );
	pRocket->pev->owner = ENT( pevOwner );
	pRocket->pev->dmg = damage;
	/* Marks it for the shared, budgeted explosion; rockets are not counted
	 * against the live grenade cap (iuser4 stays 0). */
	pRocket->pev->iuser3 = (int)DODGC_GRENADE_MAGIC;
	pRocket->pev->iuser4 = 0;

	/* Smoke trail from the already-resident smoke sprite: client side only. */
	MESSAGE_BEGIN( MSG_BROADCAST, SVC_TEMPENTITY );
		WRITE_BYTE( TE_BEAMFOLLOW );
		WRITE_SHORT( pRocket->entindex() );
		WRITE_SHORT( g_sModelIndexSmoke );
		WRITE_BYTE( 15 );	/* life, 0.1 s units */
		WRITE_BYTE( 4 );	/* width */
		WRITE_BYTE( 200 );
		WRITE_BYTE( 200 );
		WRITE_BYTE( 200 );
		WRITE_BYTE( 160 );
	MESSAGE_END();
	return pRocket;
}

void CDodRocket::RocketTouch( CBaseEntity *pOther )
{
	TraceResult tr;
	Vector dir = pev->velocity.Length() > 1.0f ? pev->velocity.Normalize() : Vector( 0, 0, -1 );

	/* A rocket that hits the sky just disappears. */
	if( UTIL_PointContents( pev->origin ) == CONTENTS_SKY )
	{
		UTIL_Remove( this );
		return;
	}
	UTIL_TraceLine( pev->origin - dir * 32.0f, pev->origin + dir * 64.0f, ignore_monsters, ENT( pev ), &tr );
	Explode( &tr, DMG_BLAST );
}

/* ----------------------------------------------------------- weapon base */

class CDodWeapon : public CBasePlayerWeapon
{
public:
	virtual int WeaponTableId( void ) = 0;
	const DodWeaponDef *Def( void ) { return Dod_GetWeaponDef( (DodWeaponId)WeaponTableId() ); }

	void Spawn( void );
	void Precache( void );
	int GetItemInfo( ItemInfo *p );
	int AddToPlayer( CBasePlayer *pPlayer );
	BOOL Deploy( void );
	void Holster( int skiplocal = 0 );
	void ItemPostFrame( void );
	void PrimaryAttack( void );
	void SecondaryAttack( void );
	void Reload( void );
	void WeaponIdle( void );
	virtual BOOL UseDecrement( void ) { return FALSE; }

private:
	void Fire( const DodWeaponDef *def );
	void FireRocket( const DodWeaponDef *def );
	void Unscope( void );
	int AccuracyFlags( void );
	const char *AnimExt( const DodWeaponDef *def );

	int m_fTriggerHeld;	/* the trigger has not been released since the last shot */
	int m_fScoped;
	int m_fDeployed;	/* bipod down */
	int m_iFireAnim;
};

int CDodWeapon::GetItemInfo( ItemInfo *p )
{
	const DodWeaponDef *def = Def();
	if( !def )
		return 0;
	p->pszName = def->classname;
	p->pszAmmo1 = def->ammo;
	p->iMaxAmmo1 = def->ammo_max;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = def->clip;
	p->iSlot = def->slot;
	p->iPosition = def->position;
	p->iFlags = 0;
	p->iId = m_iId = def->hl_id;
	p->iWeight = def->weight;
	return 1;
}

void CDodWeapon::Precache( void )
{
	/* Registration only (W_Precache runs this for every gun at map load).
	 * The models and sounds are precached in Spawn(), when a player is
	 * actually given the weapon, so unused guns cost no memory. */
}

void CDodWeapon::Spawn( void )
{
	const DodWeaponDef *def = Def();
	if( !def )
	{
		UTIL_Remove( this );
		return;
	}
	m_iId = def->hl_id;
	Precache();

	/* Late precache: this is the only model/sound load for this weapon. */
	PRECACHE_MODEL( (char *)def->view_model );
	PRECACHE_MODEL( (char *)def->player_model );
	PRECACHE_SOUND( (char *)def->fire_sound );
	PRECACHE_SOUND( (char *)def->reload_sound );
	if( def->mode == DOD_FIRE_ROCKET )
		UTIL_PrecacheOther( "dod_rocket" );

	/* The world body is the stock weapon box, already resident. */
	SET_MODEL( ENT( pev ), "models/w_weaponbox.mdl" );
	m_iDefaultAmmo = def->clip;
	m_iClip = -1;
	FallInit();
}

int CDodWeapon::AddToPlayer( CBasePlayer *pPlayer )
{
	if( CBasePlayerWeapon::AddToPlayer( pPlayer ) )
	{
		MESSAGE_BEGIN( MSG_ONE, gmsgWeapPickup, NULL, pPlayer->pev );
			WRITE_BYTE( m_iId );
		MESSAGE_END();
		return TRUE;
	}
	return FALSE;
}

const char *CDodWeapon::AnimExt( const DodWeaponDef *def )
{
	if( def->slot == 1 )
		return "onehanded";
	if( def->mode == DOD_FIRE_ROCKET )
		return "rpg";
	if( def->mode == DOD_FIRE_BOLT )
		return "bow";
	return "mp5";
}

BOOL CDodWeapon::Deploy( void )
{
	const DodWeaponDef *def = Def();
	if( !def )
		return FALSE;
	m_fScoped = FALSE;
	m_fTriggerHeld = TRUE;	/* a held fire button from the last weapon does not shoot */
	return DefaultDeploy( def->view_model, def->player_model, DODW_ANIM_DRAW, AnimExt( def ) );
}

void CDodWeapon::Unscope( void )
{
	if( m_fScoped )
	{
		m_fScoped = FALSE;
		m_pPlayer->pev->fov = m_pPlayer->m_iFOV = 0;
	}
}

void CDodWeapon::Holster( int skiplocal )
{
	Unscope();
	m_fDeployed = FALSE;
	CBasePlayerWeapon::Holster( skiplocal );
}

int CDodWeapon::AccuracyFlags( void )
{
	int flags = 0;
	if( !( m_pPlayer->pev->flags & FL_ONGROUND ) )
		flags |= DOD_ACC_AIRBORNE;
	else if( m_pPlayer->pev->velocity.Length2D() > 40.0f )
		flags |= DOD_ACC_MOVING;
	if( m_pPlayer->pev->flags & FL_DUCKING )
		flags |= DOD_ACC_CROUCHED;
	if( m_fDeployed )
		flags |= DOD_ACC_DEPLOYED;
	if( m_fScoped )
		flags |= DOD_ACC_SCOPED;
	return flags;
}

void CDodWeapon::ItemPostFrame( void )
{
	const DodWeaponDef *def = Def();
	if( def )
	{
		if( !( m_pPlayer->pev->button & IN_ATTACK ) )
			m_fTriggerHeld = FALSE;
		/* A bipod only holds while the gunner stays put. */
		if( m_fDeployed && m_pPlayer->pev->velocity.Length2D() > 60.0f )
			m_fDeployed = FALSE;
	}
	CBasePlayerWeapon::ItemPostFrame();
}

void CDodWeapon::PrimaryAttack( void )
{
	const DodWeaponDef *def = Def();
	if( !def )
		return;
	if( m_pPlayer->pev->waterlevel == 3 )
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = gpGlobals->time + 0.15f;
		return;
	}
	if( m_iClip <= 0 )
	{
		PlayEmptySound();
		m_flNextPrimaryAttack = gpGlobals->time + 0.2f;
		return;
	}
	if( !Dod_WeaponMayFire( def, m_fTriggerHeld ) )
		return;
	m_fTriggerHeld = TRUE;
	if( def->mode == DOD_FIRE_ROCKET )
		FireRocket( def );
	else
		Fire( def );
}

void CDodWeapon::Fire( const DodWeaponDef *def )
{
	Vector vecSrc, vecAim;
	float spread;

	m_iClip--;
	m_pPlayer->m_iWeaponVolume = LOUD_GUN_VOLUME;
	m_pPlayer->m_iWeaponFlash = NORMAL_GUN_FLASH;
	m_pPlayer->pev->effects = (int)( m_pPlayer->pev->effects ) | EF_MUZZLEFLASH;
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

	UTIL_MakeVectors( m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle );
	vecSrc = m_pPlayer->GetGunPosition();
	vecAim = gpGlobals->v_forward;
	spread = Dod_WeaponSpread( def, AccuracyFlags() );

	m_pPlayer->FireBulletsPlayer( 1, vecSrc, vecAim, Vector( spread, spread, spread ), def->range,
		BULLET_PLAYER_MP5, 2, (int)def->damage, m_pPlayer->pev, m_pPlayer->random_seed );

	EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_WEAPON, def->fire_sound, 1.0f, ATTN_NORM, 0,
		94 + RANDOM_LONG( 0, 15 ) );
	m_pPlayer->pev->punchangle.x -= def->recoil;

	m_iFireAnim = ( m_iFireAnim + 1 ) % 3;
	SendWeaponAnim( m_iClip ? DODW_ANIM_FIRE1 + m_iFireAnim : DODW_ANIM_FIRE_EMPTY );

	m_flNextPrimaryAttack = gpGlobals->time + def->interval;
	m_flTimeWeaponIdle = gpGlobals->time + def->interval + 2.0f;
}

void CDodWeapon::FireRocket( const DodWeaponDef *def )
{
	Vector vecSrc, vecAim;

	m_iClip--;
	m_pPlayer->m_iWeaponVolume = LOUD_GUN_VOLUME;
	m_pPlayer->m_iWeaponFlash = BRIGHT_GUN_FLASH;
	m_pPlayer->SetAnimation( PLAYER_ATTACK1 );

	UTIL_MakeVectors( m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle );
	vecAim = gpGlobals->v_forward;
	vecSrc = m_pPlayer->GetGunPosition() + vecAim * 16.0f;

	CDodRocket::Create( vecSrc, vecAim * DOD_ROCKET_SPEED, m_pPlayer->pev, def->damage );

	EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_WEAPON, def->fire_sound, 1.0f, ATTN_NORM, 0, PITCH_NORM );
	m_pPlayer->pev->punchangle.x -= def->recoil;
	SendWeaponAnim( DODW_ANIM_FIRE1 );

	m_flNextPrimaryAttack = gpGlobals->time + def->interval;
	m_flTimeWeaponIdle = gpGlobals->time + def->interval + 2.0f;
}

void CDodWeapon::SecondaryAttack( void )
{
	const DodWeaponDef *def = Def();
	if( !def )
		return;
	if( def->scope_fov > 0.0f )
	{
		m_fScoped = !m_fScoped;
		m_pPlayer->pev->fov = m_pPlayer->m_iFOV = m_fScoped ? (int)def->scope_fov : 0;
		m_flNextSecondaryAttack = gpGlobals->time + 0.3f;
	}
	else if( def->bipod )
	{
		/* Deploy only when the gunner is standing still. */
		if( m_fDeployed || m_pPlayer->pev->velocity.Length2D() < 5.0f )
			m_fDeployed = !m_fDeployed;
		m_flNextSecondaryAttack = gpGlobals->time + 0.5f;
	}
}

void CDodWeapon::Reload( void )
{
	const DodWeaponDef *def = Def();
	if( !def || m_iClip >= def->clip )
		return;
	if( Dod_ReloadAmount( def, m_iClip, m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] ) <= 0 )
		return;
	Unscope();
	if( DefaultReload( def->clip, DODW_ANIM_RELOAD, def->reload_seconds ) )
		EMIT_SOUND_DYN( ENT( m_pPlayer->pev ), CHAN_ITEM, def->reload_sound, 1.0f, ATTN_NORM, 0, PITCH_NORM );
}

void CDodWeapon::WeaponIdle( void )
{
	ResetEmptySound();
	m_pPlayer->GetAutoaimVector( AUTOAIM_5DEGREES );
	if( m_flTimeWeaponIdle > UTIL_WeaponTimeBase() )
		return;
	SendWeaponAnim( m_iClip > 0 ? DODW_ANIM_IDLE : DODW_ANIM_IDLE_EMPTY );
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 10.0f;
}

/* -------------------------------------------------------------- classnames */

#define DOD_WEAPON( Cls, id, name ) \
	class Cls : public CDodWeapon { public: virtual int WeaponTableId( void ) { return id; } }; \
	LINK_ENTITY_TO_CLASS( name, Cls )

DOD_WEAPON( CDodGarand, DOD_WEAPON_GARAND, weapon_dod_garand )
DOD_WEAPON( CDodSpringfield, DOD_WEAPON_SPRINGFIELD, weapon_dod_spring )
DOD_WEAPON( CDodThompson, DOD_WEAPON_THOMPSON, weapon_dod_thompson )
DOD_WEAPON( CDodBar, DOD_WEAPON_BAR, weapon_dod_bar )
DOD_WEAPON( CDodBrowning, DOD_WEAPON_BROWNING, weapon_dod_30cal )
DOD_WEAPON( CDodBazooka, DOD_WEAPON_BAZOOKA, weapon_dod_bazooka )
DOD_WEAPON( CDodColt, DOD_WEAPON_COLT, weapon_dod_colt )
DOD_WEAPON( CDodK98, DOD_WEAPON_K98, weapon_dod_k98 )
DOD_WEAPON( CDodK98Scoped, DOD_WEAPON_K98_SCOPED, weapon_dod_k98s )
DOD_WEAPON( CDodMP40, DOD_WEAPON_MP40, weapon_dod_mp40 )
DOD_WEAPON( CDodStg44, DOD_WEAPON_STG44, weapon_dod_stg44 )
DOD_WEAPON( CDodMG42, DOD_WEAPON_MG42, weapon_dod_mg42 )
DOD_WEAPON( CDodPschreck, DOD_WEAPON_PSCHRECK, weapon_dod_pschreck )
DOD_WEAPON( CDodLuger, DOD_WEAPON_LUGER, weapon_dod_luger )

void DodWeapons_PrecacheAll( void )
{
	int id;
	for( id = DOD_WEAPON_NONE + 1; id < DOD_WEAPON_COUNT; id++ )
		UTIL_PrecacheOtherWeapon( Dod_GetWeaponDef( (DodWeaponId)id )->classname );
}
