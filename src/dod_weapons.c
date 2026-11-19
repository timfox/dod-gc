/* Day of Defeat weapon table; see dod_weapons.h. */
#include "dod_weapons.h"

#include <stdio.h>
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
		"weapons/spring_shoot.wav", "weapons/tommy_reload_clipin.wav" },
	[DOD_WEAPON_THOMPSON] = { DOD_WEAPON_THOMPSON, "weapon_dod_thompson", "Thompson", A, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_THOMPSON),
		"models/v_tommy.mdl", "models/p_tommy.mdl", "dod_45", 250, 30, 180,
		24.0f, 0.09f, 0.045f, 0.9f, 3500.0f, 2.4f, 0.0f, 0, 2, 1, 15,
		"weapons/thompson_shoot.wav", "weapons/tommy_reload_clipin.wav" },
	[DOD_WEAPON_BAR] = { DOD_WEAPON_BAR, "weapon_dod_bar", "BAR", A, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_BAR),
		"models/v_bar.mdl", "models/p_barbu.mdl", "dod_30-06", 200, 20, 160,
		38.0f, 0.12f, 0.030f, 1.4f, 5500.0f, 2.6f, 0.0f, 0, 2, 2, 15,
		"weapons/bar_shoot.wav", "weapons/tommy_reload_clipin.wav" },
	[DOD_WEAPON_BROWNING] = { DOD_WEAPON_BROWNING, "weapon_dod_30cal", "30cal", A, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_BROWNING),
		"models/v_30cal.mdl", "models/p_30cal.mdl", "dod_30cal", 250, 150, 250,
		34.0f, 0.07f, 0.055f, 1.0f, 6000.0f, 5.0f, 0.0f, 1, 2, 4, 20,
		"weapons/30cal_shoot.wav", "weapons/bulletchain.wav" },
	[DOD_WEAPON_BAZOOKA] = { DOD_WEAPON_BAZOOKA, "weapon_dod_bazooka", "Bazooka", A, DOD_FIRE_ROCKET, DOD_HLID(DOD_WEAPON_BAZOOKA),
		"models/v_bazooka.mdl", "models/p_bazooka.mdl", "dod_rocket", 20, 1, 4,
		150.0f, 1.2f, 0.0f, 4.0f, 8192.0f, 3.5f, 0.0f, 0, 3, 0, 20,
		"weapons/rocket1.wav", "weapons/bazookareloadrocketin.wav" },
	[DOD_WEAPON_COLT] = { DOD_WEAPON_COLT, "weapon_dod_colt", "Colt", A, DOD_FIRE_SEMI, DOD_HLID(DOD_WEAPON_COLT),
		"models/v_colt.mdl", "models/p_colt.mdl", "dod_45", 250, 7, 21,
		32.0f, 0.22f, 0.025f, 1.2f, 3000.0f, 2.0f, 0.0f, 0, 1, 0, 10,
		"weapons/colt_shoot.wav", "weapons/colt_reload_clipin.wav" },
	[DOD_WEAPON_K98] = { DOD_WEAPON_K98, "weapon_dod_k98", "Kar98k", X, DOD_FIRE_BOLT, DOD_HLID(DOD_WEAPON_K98),
		"models/v_98k.mdl", "models/p_k98.mdl", "dod_8mm", 200, 5, 60,
		85.0f, 1.25f, 0.010f, 2.4f, 6000.0f, 3.0f, 0.0f, 0, 2, 0, 15,
		"weapons/kar_shoot.wav", "weapons/tommy_reload_clipin.wav" },
	[DOD_WEAPON_K98_SCOPED] = { DOD_WEAPON_K98_SCOPED, "weapon_dod_k98s", "Scoped Kar98k", X, DOD_FIRE_BOLT, DOD_HLID(DOD_WEAPON_K98_SCOPED),
		"models/v_scoped98k.mdl", "models/p_k98.mdl", "dod_8mm", 200, 5, 50,
		95.0f, 1.5f, 0.030f, 3.0f, 8192.0f, 3.0f, 20.0f, 0, 2, 3, 15,
		"weapons/kar_shoot.wav", "weapons/tommy_reload_clipin.wav" },
	[DOD_WEAPON_MP40] = { DOD_WEAPON_MP40, "weapon_dod_mp40", "MP40", X, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_MP40),
		"models/v_mp40.mdl", "models/p_mp40.mdl", "dod_9mm", 250, 32, 192,
		22.0f, 0.11f, 0.040f, 0.8f, 3500.0f, 2.2f, 0.0f, 0, 2, 1, 15,
		"weapons/mp40_shoot.wav", "weapons/tommy_reload_clipin.wav" },
	[DOD_WEAPON_STG44] = { DOD_WEAPON_STG44, "weapon_dod_stg44", "StG44", X, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_STG44),
		"models/v_mp44.mdl", "models/p_stg44.mdl", "dod_kurz", 250, 30, 180,
		33.0f, 0.10f, 0.028f, 1.2f, 5500.0f, 2.4f, 0.0f, 0, 2, 2, 15,
		"weapons/mp44_shoot.wav", "weapons/tommy_reload_clipin.wav" },
	[DOD_WEAPON_MG42] = { DOD_WEAPON_MG42, "weapon_dod_mg42", "MG42", X, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_MG42),
		"models/v_mg42.mdl", "models/p_mg42pr.mdl", "dod_8mm", 250, 250, 250,
		30.0f, 0.06f, 0.060f, 0.9f, 6000.0f, 5.5f, 0.0f, 1, 2, 4, 20,
		"weapons/mg42_shoot.wav", "weapons/bulletchain.wav" },
	[DOD_WEAPON_PSCHRECK] = { DOD_WEAPON_PSCHRECK, "weapon_dod_pschreck", "Panzerschreck", X, DOD_FIRE_ROCKET, DOD_HLID(DOD_WEAPON_PSCHRECK),
		"models/v_panzerschreck.mdl", "models/p_pschreck.mdl", "dod_rocket", 20, 1, 4,
		150.0f, 1.2f, 0.0f, 4.0f, 8192.0f, 3.5f, 0.0f, 0, 3, 0, 20,
		"weapons/rocket1.wav", "weapons/bazookareloadrocketin.wav" },
	[DOD_WEAPON_LUGER] = { DOD_WEAPON_LUGER, "weapon_dod_luger", "Luger", X, DOD_FIRE_SEMI, DOD_HLID(DOD_WEAPON_LUGER),
		"models/v_luger.mdl", "models/p_luger.mdl", "dod_9mm", 250, 8, 24,
		28.0f, 0.20f, 0.025f, 1.1f, 3000.0f, 2.0f, 0.0f, 0, 1, 0, 10,
		"weapons/luger_shoot.wav", "weapons/tommy_reload_clipin.wav" },
};

/* Primary weapon by [team - 1][class]. */
static const DodWeaponId g_class_primary[2][DOD_CLASS_COUNT] = {
	{ DOD_WEAPON_NONE, DOD_WEAPON_GARAND, DOD_WEAPON_THOMPSON, DOD_WEAPON_BAR,
	  DOD_WEAPON_SPRINGFIELD, DOD_WEAPON_BROWNING, DOD_WEAPON_BAZOOKA },
	{ DOD_WEAPON_NONE, DOD_WEAPON_K98, DOD_WEAPON_MP40, DOD_WEAPON_STG44,
	  DOD_WEAPON_K98_SCOPED, DOD_WEAPON_MG42, DOD_WEAPON_PSCHRECK }
};

/* The British kit: the same ids, class names, slots and ammo pools as the American guns they stand in for (the table
 * above names them), with the British models, sounds and numbers. The stats are plausible, not retail data. */
static const DodWeaponDef g_british[DOD_WEAPON_COUNT] = {
	[DOD_WEAPON_GARAND] = { DOD_WEAPON_GARAND, "weapon_dod_garand", "Lee-Enfield", A, DOD_FIRE_BOLT, DOD_HLID(DOD_WEAPON_GARAND),
		"models/v_enfield.mdl", "models/p_enfield.mdl", "dod_30-06", 200, 10, 50,
		80.0f, 1.0f, 0.012f, 2.0f, 6000.0f, 3.2f, 0.0f, 0, 2, 0, 15,
		"weapons/enfield_shoot.wav", "weapons/tommy_reload_clipin.wav" },
	[DOD_WEAPON_SPRINGFIELD] = { DOD_WEAPON_SPRINGFIELD, "weapon_dod_spring", "Scoped Lee-Enfield", A, DOD_FIRE_BOLT, DOD_HLID(DOD_WEAPON_SPRINGFIELD),
		"models/v_enfield_scoped.mdl", "models/p_enfields.mdl", "dod_30-06", 200, 10, 50,
		95.0f, 1.4f, 0.030f, 3.0f, 8192.0f, 3.2f, 20.0f, 0, 2, 3, 15,
		"weapons/enfieldsniper_shoot.wav", "weapons/tommy_reload_clipin.wav" },
	[DOD_WEAPON_THOMPSON] = { DOD_WEAPON_THOMPSON, "weapon_dod_thompson", "Sten", A, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_THOMPSON),
		"models/v_sten.mdl", "models/p_sten.mdl", "dod_45", 250, 32, 192,
		21.0f, 0.10f, 0.045f, 0.9f, 3000.0f, 2.3f, 0.0f, 0, 2, 1, 15,
		"weapons/sten_shoot.wav", "weapons/tommy_reload_clipin.wav" },
	[DOD_WEAPON_BAR] = { DOD_WEAPON_BAR, "weapon_dod_bar", "Bren", A, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_BAR),
		"models/v_bren.mdl", "models/p_bren.mdl", "dod_30-06", 200, 30, 150,
		36.0f, 0.125f, 0.030f, 1.3f, 5500.0f, 3.0f, 0.0f, 0, 2, 2, 15,
		"weapons/bren_shoot.wav", "weapons/tommy_reload_clipin.wav" },
	[DOD_WEAPON_BAZOOKA] = { DOD_WEAPON_BAZOOKA, "weapon_dod_bazooka", "PIAT", A, DOD_FIRE_ROCKET, DOD_HLID(DOD_WEAPON_BAZOOKA),
		"models/v_piat.mdl", "models/p_piat.mdl", "dod_rocket", 20, 1, 4,
		140.0f, 1.5f, 0.0f, 4.0f, 8192.0f, 4.0f, 0.0f, 0, 3, 0, 20,
		"weapons/piat_shoot.wav", "weapons/bazookareloadrocketin.wav" },
	[DOD_WEAPON_COLT] = { DOD_WEAPON_COLT, "weapon_dod_colt", "Webley", A, DOD_FIRE_SEMI, DOD_HLID(DOD_WEAPON_COLT),
		"models/v_webley.mdl", "models/p_webley.mdl", "dod_45", 250, 6, 18,
		33.0f, 0.30f, 0.030f, 1.3f, 3000.0f, 3.0f, 0.0f, 0, 1, 0, 10,
		"weapons/webley_shoot.wav", "weapons/colt_reload_clipin.wav" },
};

/* The paratrooper kit, like the British one: same ids, class names, slots and ammo pools as what they stand in for. The
 * stats are plausible, not retail data. */
static const DodWeaponDef g_para_allies[DOD_WEAPON_COUNT] = {
	[DOD_WEAPON_GARAND] = { DOD_WEAPON_GARAND, "weapon_dod_garand", "M1 Carbine", A, DOD_FIRE_SEMI, DOD_HLID(DOD_WEAPON_GARAND),
		"models/v_m1carbine.mdl", "models/p_m1carb.mdl", "dod_30-06", 200, 15, 90,
		38.0f, 0.18f, 0.015f, 1.2f, 5000.0f, 2.4f, 0.0f, 0, 2, 0, 15,
		"weapons/carbine_shoot.wav", "weapons/tommy_reload_clipin.wav" },
	[DOD_WEAPON_THOMPSON] = { DOD_WEAPON_THOMPSON, "weapon_dod_thompson", "Grease Gun", A, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_THOMPSON),
		"models/v_greasegun.mdl", "models/p_grease.mdl", "dod_45", 250, 32, 192,
		22.0f, 0.10f, 0.045f, 0.9f, 3200.0f, 2.3f, 0.0f, 0, 2, 1, 15,
		"weapons/greasegun_shoot.wav", "weapons/tommy_reload_clipin.wav" },
};
static const DodWeaponDef g_para_axis[DOD_WEAPON_COUNT] = {
	[DOD_WEAPON_K98] = { DOD_WEAPON_K98, "weapon_dod_k98", "FG42", X, DOD_FIRE_AUTO, DOD_HLID(DOD_WEAPON_K98),
		"models/v_fg42.mdl", "models/p_fg42.mdl", "dod_8mm", 200, 20, 100,
		32.0f, 0.075f, 0.030f, 1.6f, 6000.0f, 3.0f, 0.0f, 0, 2, 0, 15,
		"weapons/fg42_shoot.wav", "weapons/tommy_reload_clipin.wav" },
};
static int g_paras[2];	/* [0] Allies, [1] Axis */

void Dod_SetParatroopers(int allies, int axis)
{
	g_paras[0] = allies != 0;
	g_paras[1] = axis != 0;
}

int Dod_TeamIsParatroopers(DodTeam team)
{
	return (team == DOD_TEAM_ALLIES && g_paras[0]) || (team == DOD_TEAM_AXIS && g_paras[1]);
}

int Dod_ParatroopersForMap(const char *mapname)
{
	static const struct { const char *name; int teams; } maps[] = {
		{ "dod_escape", 3 }, { "dod_falaise", 3 }, { "dod_flugplatz", 3 }, { "dod_glider", 3 }, { "dod_kraftstoff", 3 },
		{ "dod_jagd", 2 }, { "dod_sturm", 1 }, { "dod_switch", 1 }, { "dod_zalec", 1 }
	};
	size_t i;
	if (!mapname)
		return 0;
	for (i = 0; i < sizeof(maps) / sizeof(maps[0]); ++i)
		if (!strcmp(mapname, maps[i].name))
			return maps[i].teams;
	return 0;
}

static DodAlliesCountry g_allies_country = DOD_COUNTRY_US;

void Dod_SetAlliesCountry(DodAlliesCountry country)
{
	g_allies_country = country == DOD_COUNTRY_BRITISH ? DOD_COUNTRY_BRITISH : DOD_COUNTRY_US;
}

DodAlliesCountry Dod_GetAlliesCountry(void)
{
	return g_allies_country;
}

DodAlliesCountry Dod_AlliesCountryForMap(const char *mapname)
{
	static const char *const british[] = { "dod_caen", "dod_flash", "dod_jagd" };
	size_t i;
	if (!mapname)
		return DOD_COUNTRY_US;
	for (i = 0; i < sizeof(british) / sizeof(british[0]); ++i)
		if (!strcmp(mapname, british[i]))
			return DOD_COUNTRY_BRITISH;
	return DOD_COUNTRY_US;
}

const DodWeaponDef *Dod_GetWeaponDef(DodWeaponId id)
{
	if (id <= DOD_WEAPON_NONE || id >= DOD_WEAPON_COUNT)
		return 0;
	if (g_allies_country == DOD_COUNTRY_BRITISH && g_british[id].id == id)
		return &g_british[id];
	if (g_paras[0] && g_allies_country != DOD_COUNTRY_BRITISH && g_para_allies[id].id == id)
		return &g_para_allies[id];
	if (g_paras[1] && g_para_axis[id].id == id)
		return &g_para_axis[id];
	return &g_weapons[id];
}

int Dod_WeaponWorldModel(const DodWeaponDef *def, char *buffer, int size)
{
	const char *stem;
	int written;
	if (!buffer || size <= 0)
		return 0;
	buffer[0] = '\0';
	if (!def || strncmp(def->view_model, "models/v_", 9))
		return 0;
	stem = def->view_model + 9;
	if (!strcmp(stem, "panzerschreck.mdl"))
		stem = "pschreck.mdl";
	else if (!strcmp(stem, "m1carbine.mdl"))
		stem = "m1carb.mdl";
	written = snprintf(buffer, (size_t)size, "models/w_%s", stem);
	if (written < 0 || written >= size) {
		buffer[0] = '\0';
		return 0;
	}
	return written;
}

const DodWeaponDef *Dod_FindWeaponDef(const char *classname)
{
	int i;
	if (!classname)
		return 0;
	for (i = DOD_WEAPON_NONE + 1; i < DOD_WEAPON_COUNT; i++) {
		if (strcmp(g_weapons[i].classname, classname) == 0)
			return Dod_GetWeaponDef((DodWeaponId)i);
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

/* ---------------------------------------------------------------- view model animations */

typedef struct DodViewAnims {
	const char *model;
	signed char idle, idle_empty, fire_count, fire[3], fire_empty, reload, draw, holster, pinpull;
} DodViewAnims;

static const DodViewAnims g_view_anims[] = {
#include "dod_viewanims.h"
};

typedef struct DodViewSounds {
	const char *model;
	int role, count;
	DodViewSoundEvent ev[DOD_VIEW_SOUND_MAX];
} DodViewSounds;

static const DodViewSounds g_view_sounds[] = {
#include "dod_viewsounds.h"
};

int Dod_ViewSounds(const char *view_model, DodAnimRole role, DodViewSoundEvent out[DOD_VIEW_SOUND_MAX])
{
	size_t i;
	int n;
	if (!view_model)
		return 0;
	for (i = 0; i < sizeof(g_view_sounds) / sizeof(g_view_sounds[0]); ++i) {
		const DodViewSounds *v = &g_view_sounds[i];
		if (v->role == (int)role && !strcmp(v->model, view_model)) {
			for (n = 0; n < v->count; ++n)
				out[n] = v->ev[n];
			return v->count;
		}
	}
	return 0;
}

static const DodViewAnims *view_anims(const char *view_model)
{
	size_t i;
	if (!view_model)
		return 0;
	for (i = 0; i < sizeof(g_view_anims) / sizeof(g_view_anims[0]); ++i)
		if (!strcmp(g_view_anims[i].model, view_model))
			return &g_view_anims[i];
	return 0;
}

int Dod_ViewAnim(const char *view_model, DodAnimRole role, int variant)
{
	const DodViewAnims *a = view_anims(view_model);
	int v;
	if (!a)
		return -1;
	switch (role) {
	case DOD_ANIM_IDLE: return a->idle;
	case DOD_ANIM_IDLE_EMPTY: return a->idle_empty >= 0 ? a->idle_empty : a->idle;
	case DOD_ANIM_FIRE:
		if (a->fire_count <= 0)
			return -1;
		v = variant < 0 ? 0 : variant % a->fire_count;
		return a->fire[v];
	case DOD_ANIM_FIRE_EMPTY:
		return a->fire_empty >= 0 ? a->fire_empty : (a->fire_count > 0 ? a->fire[0] : -1);
	case DOD_ANIM_RELOAD: return a->reload;
	case DOD_ANIM_DRAW: return a->draw;
	case DOD_ANIM_HOLSTER: return a->holster;
	case DOD_ANIM_PINPULL: return a->pinpull;
	default: return -1;
	}
}

/* The stock HL1 numbers the crowbar and hand grenade code sends (dlls/crowbar.cpp, handgrenade.cpp). */
enum { STOCK_CROWBAR_IDLE = 0, STOCK_CROWBAR_DRAW, STOCK_CROWBAR_HOLSTER, STOCK_CROWBAR_ATTACK1HIT, STOCK_CROWBAR_ATTACK1MISS,
	STOCK_CROWBAR_ATTACK2MISS, STOCK_CROWBAR_ATTACK2HIT, STOCK_CROWBAR_ATTACK3MISS, STOCK_CROWBAR_ATTACK3HIT };
enum { STOCK_GRENADE_IDLE = 0, STOCK_GRENADE_FIDGET, STOCK_GRENADE_PINPULL, STOCK_GRENADE_THROW1, STOCK_GRENADE_THROW2,
	STOCK_GRENADE_THROW3, STOCK_GRENADE_HOLSTER, STOCK_GRENADE_DRAW };

static int pick(int found, int idle)
{
	return found >= 0 ? found : idle;
}

int Dod_RemapAnim(const char *classname, int anim)
{
	const DodWeaponDef *def;
	const char *model;
	int idle, mapped;
	if (!classname)
		return anim;
	if (!strcmp(classname, "weapon_crowbar")) {
		/* every DoD melee model has the same four sequences: idle, two slashes, draw */
		model = "models/v_amerk.mdl";
		idle = Dod_ViewAnim(model, DOD_ANIM_IDLE, 0);
		switch (anim) {
		case STOCK_CROWBAR_IDLE: return idle;
		case STOCK_CROWBAR_DRAW: return pick(Dod_ViewAnim(model, DOD_ANIM_DRAW, 0), idle);
		case STOCK_CROWBAR_HOLSTER: return idle;
		case STOCK_CROWBAR_ATTACK2MISS:
		case STOCK_CROWBAR_ATTACK2HIT: return pick(Dod_ViewAnim(model, DOD_ANIM_FIRE, 1), idle);
		case STOCK_CROWBAR_ATTACK1HIT:
		case STOCK_CROWBAR_ATTACK1MISS:
		case STOCK_CROWBAR_ATTACK3MISS:
		case STOCK_CROWBAR_ATTACK3HIT: return pick(Dod_ViewAnim(model, DOD_ANIM_FIRE, 0), idle);
		default: return idle;
		}
	}
	if (!strcmp(classname, "weapon_handgrenade")) {
		model = "models/v_grenade.mdl";	/* the stick grenade and Mills bomb share its layout */
		idle = Dod_ViewAnim(model, DOD_ANIM_IDLE, 0);
		switch (anim) {
		case STOCK_GRENADE_IDLE:
		case STOCK_GRENADE_FIDGET: return idle;
		case STOCK_GRENADE_PINPULL: return pick(Dod_ViewAnim(model, DOD_ANIM_PINPULL, 0), idle);
		case STOCK_GRENADE_THROW1:
		case STOCK_GRENADE_THROW2:
		case STOCK_GRENADE_THROW3: return pick(Dod_ViewAnim(model, DOD_ANIM_FIRE, 0), idle);
		case STOCK_GRENADE_HOLSTER: return pick(Dod_ViewAnim(model, DOD_ANIM_HOLSTER, 0), idle);
		case STOCK_GRENADE_DRAW: return pick(Dod_ViewAnim(model, DOD_ANIM_DRAW, 0), idle);
		default: return idle;
		}
	}
	def = Dod_FindWeaponDef(classname);
	if (!def || !def->view_model[0])
		return anim;
	model = def->view_model;
	idle = Dod_ViewAnim(model, DOD_ANIM_IDLE, 0);
	if (idle < 0)
		return anim;	/* a model the table does not know: leave the number alone */
	switch (anim) {
	case DOD_GENERIC_ANIM_IDLE: return idle;
	case DOD_GENERIC_ANIM_FIRE1:
	case DOD_GENERIC_ANIM_FIRE1 + 1:
	case DOD_GENERIC_ANIM_FIRE1 + 2: mapped = Dod_ViewAnim(model, DOD_ANIM_FIRE, anim - DOD_GENERIC_ANIM_FIRE1); break;
	case DOD_GENERIC_ANIM_RELOAD: mapped = Dod_ViewAnim(model, DOD_ANIM_RELOAD, 0); break;
	case DOD_GENERIC_ANIM_DRAW: mapped = Dod_ViewAnim(model, DOD_ANIM_DRAW, 0); break;
	case DOD_GENERIC_ANIM_HOLSTER: mapped = Dod_ViewAnim(model, DOD_ANIM_HOLSTER, 0); break;
	case DOD_GENERIC_ANIM_IDLE_EMPTY: mapped = Dod_ViewAnim(model, DOD_ANIM_IDLE_EMPTY, 0); break;
	case DOD_GENERIC_ANIM_FIRE_EMPTY: mapped = Dod_ViewAnim(model, DOD_ANIM_FIRE_EMPTY, 0); break;
	default: return anim;
	}
	return pick(mapped, idle);
}
