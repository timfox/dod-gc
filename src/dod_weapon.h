#ifndef DODGC_DOD_WEAPON_H
#define DODGC_DOD_WEAPON_H

/* Day of Defeat guns for the HL1SDK game DLL: one table-driven weapon class
 * (CDodWeapon) with a thin subclass per classname, replacing the HL1 MP5,
 * shotgun, crossbow, RPG and Glock stand-ins. Every number comes from
 * dod_weapons.c. Include after extdll.h/util.h/cbase.h/weapons.h. */

/* Viewmodel animation indices the weapon asks for. The engine's gc_vmanim
 * maps each onto the sequence of the DoD model with the matching name. */
enum dodweapon_anim_e
{
	DODW_ANIM_IDLE = 0,
	DODW_ANIM_FIRE1,	/* 1-3: the model's own shoot variants */
	DODW_ANIM_FIRE2,
	DODW_ANIM_FIRE3,
	DODW_ANIM_RELOAD,
	DODW_ANIM_DRAW,
	DODW_ANIM_HOLSTER,
	DODW_ANIM_IDLE_EMPTY,
	DODW_ANIM_FIRE_EMPTY
};

/* Register every DoD gun's ItemInfo and ammo from W_Precache. Models and
 * sounds are NOT precached here: they are loaded when a weapon is granted, so
 * a map only ever holds what its players actually carry. */
void DodWeapons_PrecacheAll( void );

#endif
