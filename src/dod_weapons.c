/* Day of Defeat weapon table; see dod_weapons.h. */
#include "dod_weapons.h"

#include <string.h>

#define A DOD_TEAM_ALLIES
#define X DOD_TEAM_AXIS

/* Order follows DodWeaponId. Slots: 0 melee, 1 pistol, 2 primary, 3 rocket,
 * 4 grenade (the HL1 hand grenade keeps its own slot). */
static const DodWeaponDef g_weapons[DOD_WEAPON_COUNT] = {
	[DOD_WEAPON_NONE] = { DOD_WEAPON_NONE, "", "None", DOD_TEAM_UNASSIGNED, DOD_FIRE_SEMI, 0,
		"", "", "", 0, 0, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0, "", "" },
	/* id, class, title, team, mode, hl id,
	 * view, player, ammo, ammo max, clip, reserve,
	 * damage, interval, spread, recoil, range, reload, scope, bipod, slot, pos, weight, fire, reload sound */
	[DOD_WEAPON_GARAND] = { DOD_WEAPON_GARAND, "weapon_dod_garand", "M1 Garand", A, DOD_FIRE_SEMI, DOD_HLID(DOD_WEAPON_GARAND),
		"models/v_garand.mdl", "models/p_garand.mdl", "dod_30-06", 200, 8, 80,
		55.0f, 0.25f, 0.012f, 1.6f, 6000.0f, 2.2f, 0.0f, 0, 2, 0, 15,
		"weapons/garand_shoot.wav", "weapons/garand_reload_clipin.wav" },
	[DOD_WEAPON_SPRINGFIELD] = { DOD_WEAPON_SPRINGFIELD, "weapon_dod_spring", "Springfield", A, DOD_FIRE_BOLT, DOD_HLID(DOD_WEAPON_SPRINGFIELD),
		"models/v_spring.mdl", "models/p_spring.mdl", "dod_30-06", 200, 5, 50,
		95.0f, 1.5f, 0.030f, 3.0f, 8192.0f, 3.0f, 20.0f, 0, 2, 3, 15,
		"weapons/spring_shoot.wav", "weapons/garand_reload_clipin.wav" },
	[DOD_WEAPON_THOMPSON] = { DOD_WEAPON_THOMPSON, "weapon_dod_thompson", "Thompson", A, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_THOMPSON),
		"models/v_tommy.mdl", "models/p_tommy.mdl", "dod_45", 250, 30, 180,
		24.0f, 0.09f, 0.045f, 0.9f, 3500.0f, 2.4f, 0.0f, 0, 2, 1, 15,
		"weapons/thompson_shoot.wav", "weapons/tommy_reload_clipin.wav" },
	[DOD_WEAPON_BAR] = { DOD_WEAPON_BAR, "weapon_dod_bar", "BAR", A, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_BAR),
		"models/v_bren.mdl", "models/p_bren.mdl", "dod_30-06", 200, 20, 160,
		38.0f, 0.12f, 0.030f, 1.4f, 5500.0f, 2.6f, 0.0f, 0, 2, 2, 15,
		"weapons/bar_shoot.wav", "weapons/garand_reload_clipin.wav" },
	[DOD_WEAPON_BROWNING] = { DOD_WEAPON_BROWNING, "weapon_dod_30cal", "30cal", A, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_BROWNING),
		"models/v_30cal.mdl", "models/p_30cal.mdl", "dod_30cal", 250, 150, 250,
		34.0f, 0.07f, 0.055f, 1.0f, 6000.0f, 5.0f, 0.0f, 1, 2, 4, 20,
		"weapons/30cal_shoot.wav", "weapons/bulletchain.wav" },
	[DOD_WEAPON_BAZOOKA] = { DOD_WEAPON_BAZOOKA, "weapon_dod_bazooka", "Bazooka", A, DOD_FIRE_ROCKET, DOD_HLID(DOD_WEAPON_BAZOOKA),
		"models/v_bazooka.mdl", "models/p_bazooka.mdl", "dod_rocket", 20, 1, 4,
		150.0f, 1.2f, 0.0f, 4.0f, 8192.0f, 3.5f, 0.0f, 0, 3, 0, 20,
		"weapons/rocket1.wav", "weapons/bazookareload.wav" },
	[DOD_WEAPON_COLT] = { DOD_WEAPON_COLT, "weapon_dod_colt", "Colt", A, DOD_FIRE_SEMI, DOD_HLID(DOD_WEAPON_COLT),
		"models/v_colt.mdl", "models/p_colt.mdl", "dod_45", 250, 7, 21,
		32.0f, 0.22f, 0.025f, 1.2f, 3000.0f, 2.0f, 0.0f, 0, 1, 0, 10,
		"weapons/colt_shoot.wav", "weapons/colt_reload_clipin.wav" },
	[DOD_WEAPON_K98] = { DOD_WEAPON_K98, "weapon_dod_k98", "Kar98k", X, DOD_FIRE_BOLT, DOD_HLID(DOD_WEAPON_K98),
		"models/v_98k.mdl", "models/p_k98.mdl", "dod_8mm", 200, 5, 60,
		85.0f, 1.25f, 0.010f, 2.4f, 6000.0f, 3.0f, 0.0f, 0, 2, 0, 15,
		"weapons/kar_shoot.wav", "weapons/gewehr_reload_clipin.wav" },
	[DOD_WEAPON_K98_SCOPED] = { DOD_WEAPON_K98_SCOPED, "weapon_dod_k98s", "Scoped Kar98k", X, DOD_FIRE_BOLT, DOD_HLID(DOD_WEAPON_K98_SCOPED),
		"models/v_scoped98k.mdl", "models/p_k98.mdl", "dod_8mm", 200, 5, 50,
		95.0f, 1.5f, 0.030f, 3.0f, 8192.0f, 3.0f, 20.0f, 0, 2, 3, 15,
		"weapons/kar_shoot.wav", "weapons/gewehr_reload_clipin.wav" },
	[DOD_WEAPON_MP40] = { DOD_WEAPON_MP40, "weapon_dod_mp40", "MP40", X, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_MP40),
		"models/v_mp40.mdl", "models/p_mp40.mdl", "dod_9mm", 250, 32, 192,
		22.0f, 0.11f, 0.040f, 0.8f, 3500.0f, 2.2f, 0.0f, 0, 2, 1, 15,
		"weapons/mp40_shoot.wav", "weapons/gewehr_reload_clipin.wav" },
	[DOD_WEAPON_STG44] = { DOD_WEAPON_STG44, "weapon_dod_stg44", "StG44", X, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_STG44),
		"models/v_mp44.mdl", "models/p_stg44.mdl", "dod_kurz", 250, 30, 180,
		33.0f, 0.10f, 0.028f, 1.2f, 5500.0f, 2.4f, 0.0f, 0, 2, 2, 15,
		"weapons/mp44_shoot.wav", "weapons/gewehr_reload_clipin.wav" },
	[DOD_WEAPON_MG42] = { DOD_WEAPON_MG42, "weapon_dod_mg42", "MG42", X, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_MG42),
		"models/v_mg42.mdl", "models/p_mg42pr.mdl", "dod_8mm", 250, 250, 250,
		30.0f, 0.06f, 0.060f, 0.9f, 6000.0f, 5.5f, 0.0f, 1, 2, 4, 20,
		"weapons/mg42_shoot.wav", "weapons/bulletchain.wav" },
	[DOD_WEAPON_PSCHRECK] = { DOD_WEAPON_PSCHRECK, "weapon_dod_pschreck", "Panzerschreck", X, DOD_FIRE_ROCKET, DOD_HLID(DOD_WEAPON_PSCHRECK),
		"models/v_panzerschreck.mdl", "models/p_pschreck.mdl", "dod_rocket", 20, 1, 4,
		150.0f, 1.2f, 0.0f, 4.0f, 8192.0f, 3.5f, 0.0f, 0, 3, 0, 20,
		"weapons/rocket1.wav", "weapons/bazookareload.wav" },
	[DOD_WEAPON_LUGER] = { DOD_WEAPON_LUGER, "weapon_dod_luger", "Luger", X, DOD_FIRE_SEMI, DOD_HLID(DOD_WEAPON_LUGER),
		"models/v_luger.mdl", "models/p_luger.mdl", "dod_9mm", 250, 8, 24,
		28.0f, 0.20f, 0.025f, 1.1f, 3000.0f, 2.0f, 0.0f, 0, 1, 0, 10,
		"weapons/luger_shoot.wav", "weapons/colt_reload_clipin.wav" },
};

/* Primary weapon by [team - 1][class]. */
static const DodWeaponId g_class_primary[2][DOD_CLASS_COUNT] = {
	{ DOD_WEAPON_NONE, DOD_WEAPON_GARAND, DOD_WEAPON_THOMPSON, DOD_WEAPON_BAR,
	  DOD_WEAPON_SPRINGFIELD, DOD_WEAPON_BROWNING, DOD_WEAPON_BAZOOKA },
	{ DOD_WEAPON_NONE, DOD_WEAPON_K98, DOD_WEAPON_MP40, DOD_WEAPON_STG44,
	  DOD_WEAPON_K98_SCOPED, DOD_WEAPON_MG42, DOD_WEAPON_PSCHRECK }
};

const DodWeaponDef *Dod_GetWeaponDef(DodWeaponId id)
{
	if (id <= DOD_WEAPON_NONE || id >= DOD_WEAPON_COUNT)
		return 0;
	return &g_weapons[id];
}

const DodWeaponDef *Dod_FindWeaponDef(const char *classname)
{
	int i;
	if (!classname)
		return 0;
	for (i = DOD_WEAPON_NONE + 1; i < DOD_WEAPON_COUNT; i++) {
		if (strcmp(g_weapons[i].classname, classname) == 0)
			return &g_weapons[i];
	}
	return 0;
}

const DodWeaponDef *Dod_ClassWeaponDef(DodTeam team, DodClass player_class)
{
	if ((team != DOD_TEAM_ALLIES && team != DOD_TEAM_AXIS) ||
		player_class <= DOD_CLASS_NONE || player_class >= DOD_CLASS_COUNT)
		return 0;
	return Dod_GetWeaponDef(g_class_primary[team - 1][player_class]);
}

const DodWeaponDef *Dod_ClassSidearmDef(DodTeam team, DodClass player_class)
{
	if ((team != DOD_TEAM_ALLIES && team != DOD_TEAM_AXIS) || !Dod_ClassHasSidearm(player_class))
		return 0;
	return Dod_GetWeaponDef(team == DOD_TEAM_ALLIES ? DOD_WEAPON_COLT : DOD_WEAPON_LUGER);
}

float Dod_WeaponSpread(const DodWeaponDef *def, int flags)
{
	float spread;
	if (!def)
		return 0.0f;
	spread = def->spread;
	if (flags & DOD_ACC_AIRBORNE)
		spread *= 4.0f;
	else if (flags & DOD_ACC_MOVING)
		spread *= 2.5f;
	if (flags & DOD_ACC_CROUCHED)
		spread *= 0.6f;
	if ((flags & DOD_ACC_DEPLOYED) && def->bipod)
		spread *= 0.3f;
	if ((flags & DOD_ACC_SCOPED) && def->scope_fov > 0.0f)
		spread *= 0.15f;
	return spread;
}

int Dod_ReloadAmount(const DodWeaponDef *def, int clip, int reserve)
{
	int room;
	if (!def || reserve <= 0)
		return 0;
	if (clip < 0)
		clip = 0;
	room = def->clip - clip;
	if (room <= 0)
		return 0;
	return room < reserve ? room : reserve;
}

int Dod_WeaponMayFire(const DodWeaponDef *def, int trigger_was_held)
{
	if (!def)
		return 0;
	if (def->mode == DOD_FIRE_AUTO)
		return 1;
	return !trigger_was_held;
}
