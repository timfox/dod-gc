/* Day of Defeat weapon table for the HL1SDK based GameCube port.
 *
 * Engine-independent: every number a weapon needs (damage, rate of fire,
 * spread, clip, reload time, ammo, models, sounds, fire mode) lives in one
 * table so the game DLL has a single generic weapon class and the values can
 * be host-tested. Numbers are approximate DoD 1.3, tuned for the HL1 damage
 * model; they are meant to be adjusted here, not in the weapon code. */
#ifndef DODGC_DOD_WEAPONS_H
#define DODGC_DOD_WEAPONS_H

#include "dod_game.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	DOD_FIRE_SEMI = 0,	/* one shot per trigger pull (Garand, pistols) */
	DOD_FIRE_AUTO,		/* held trigger (SMGs, BAR, StG44, MGs) */
	DOD_FIRE_BOLT,		/* one shot, then a bolt cycle (Kar98k, Springfield) */
	DOD_FIRE_ROCKET		/* fires a rocket (Bazooka, Panzerschreck) */
} DodFireMode;

typedef enum {
	DOD_WEAPON_NONE = 0,
	DOD_WEAPON_GARAND,
	DOD_WEAPON_SPRINGFIELD,
	DOD_WEAPON_THOMPSON,
	DOD_WEAPON_BAR,
	DOD_WEAPON_BROWNING,
	DOD_WEAPON_BAZOOKA,
	DOD_WEAPON_COLT,
	DOD_WEAPON_K98,
	DOD_WEAPON_K98_SCOPED,
	DOD_WEAPON_MP40,
	DOD_WEAPON_STG44,
	DOD_WEAPON_MG42,
	DOD_WEAPON_PSCHRECK,
	DOD_WEAPON_LUGER,
	DOD_WEAPON_COUNT
} DodWeaponId;

/* HL1 weapon ids are a 32 bit mask; 1-15, 30 and 31 are taken. HL1 keeps its
 * ItemInfo (clip size, ammo) per id, so every weapon needs its own: the 14
 * weapons use 16-29 in DodWeaponId order. */
#define DOD_HLID_BASE 16
#define DOD_HLID(id) (DOD_HLID_BASE - 1 + (int)(id))

typedef struct DodWeaponDef {
	DodWeaponId id;
	const char *classname;		/* weapon_dod_garand */
	const char *title;		/* "M1 Garand" */
	DodTeam team;
	DodFireMode mode;
	int hl_id;			/* DOD_HLID( id ), unique per weapon */
	const char *view_model;
	const char *player_model;
	const char *ammo;		/* shared by calibre: "dod_30-06" */
	int ammo_max;			/* reserve cap, kept <= 250 for the HUD byte */
	int clip;			/* magazine / belt size */
	int reserve;			/* spare rounds issued at spawn */
	float damage;			/* per bullet, or rocket centre damage */
	float interval;			/* seconds between shots (bolt: cycle time) */
	float spread;			/* HL1 cone half width, standing and moving slowly */
	float recoil;			/* view punch in degrees per shot */
	float range;			/* bullet reach in GoldSrc units */
	float reload_seconds;
	float scope_fov;		/* zoomed FOV, 0 = no scope */
	int bipod;			/* can deploy a bipod for accuracy */
	int slot;			/* HL1 weapon HUD slot / position / weight */
	int position;
	int weight;
	const char *fire_sound;
	const char *reload_sound;
} DodWeaponDef;

const DodWeaponDef *Dod_GetWeaponDef(DodWeaponId id);
const DodWeaponDef *Dod_FindWeaponDef(const char *classname);
/* The primary weapon a class is issued, NULL for bad input. */
const DodWeaponDef *Dod_ClassWeaponDef(DodTeam team, DodClass player_class);
/* The Colt/Luger, NULL when the class carries no sidearm. */
const DodWeaponDef *Dod_ClassSidearmDef(DodTeam team, DodClass player_class);

/* Accuracy modifiers for Dod_WeaponSpread. */
#define DOD_ACC_MOVING 1
#define DOD_ACC_CROUCHED 2
#define DOD_ACC_DEPLOYED 4	/* bipod down */
#define DOD_ACC_SCOPED 8
#define DOD_ACC_AIRBORNE 16

/* Effective spread for a shot. Moving and jumping widen it; crouching,
 * a deployed bipod and a scope tighten it. Never negative. */
float Dod_WeaponSpread(const DodWeaponDef *def, int accuracy_flags);
/* Rounds a reload moves into the magazine: limited by room and by reserve. */
int Dod_ReloadAmount(const DodWeaponDef *def, int clip, int reserve);
/* Whether the trigger may fire again: auto always, semi/bolt only on a new pull. */
int Dod_WeaponMayFire(const DodWeaponDef *def, int trigger_was_held);

#ifdef __cplusplus
}
#endif

#endif /* DODGC_DOD_WEAPONS_H */
