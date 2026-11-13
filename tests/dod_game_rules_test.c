#include "dod_game.h"
#include "dod_weapons.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static void test_players_and_weapon_models(void)
{
	const DodPlayerState *player;
	const char *view_model = 0;
	const char *player_model = 0;

	Dod_GameInit();
	assert(Dod_GameIsActive());
	assert(Dod_PlayerConnect(1));
	assert(Dod_PlayerConnect(2));
	assert(Dod_AssignPlayerTeam(1, DOD_TEAM_ALLIES));
	assert(Dod_AssignPlayerTeam(2, DOD_TEAM_AXIS));
	assert(!Dod_SelectPlayerClass(1, DOD_CLASS_RIFLEMAN + DOD_CLASS_COUNT));
	assert(Dod_SelectPlayerClass(1, DOD_CLASS_RIFLEMAN));
	assert(Dod_GetWeaponModels(1, DOD_MODEL_AUTOMATIC, &view_model, &player_model));
	assert(view_model && strcmp(view_model, "models/v_garand.mdl") == 0);
	assert(player_model && strcmp(player_model, "models/p_garand.mdl") == 0);
	assert(Dod_GetWeaponModels(1, DOD_MODEL_MELEE, &view_model, &player_model));
	assert(strcmp(view_model, "models/v_amerk.mdl") == 0 && strcmp(player_model, "models/p_amerk.mdl") == 0);
	assert(Dod_GetWeaponModels(2, DOD_MODEL_MELEE, &view_model, &player_model));
	assert(strcmp(view_model, "models/v_spade.mdl") == 0 && strcmp(player_model, "models/p_spade.mdl") == 0);
	assert(!Dod_GetWeaponModels(1, DOD_MODEL_PISTOL, &view_model, &player_model));
	assert(Dod_SelectPlayerClass(1, DOD_CLASS_SNIPER));
	assert(Dod_GetWeaponModels(1, DOD_MODEL_PISTOL, &view_model, &player_model));
	assert(strcmp(view_model, "models/v_colt.mdl") == 0 && strcmp(player_model, "models/p_colt.mdl") == 0);
	assert(Dod_SelectPlayerClass(2, DOD_CLASS_MG));
	assert(Dod_GetWeaponModels(2, DOD_MODEL_PISTOL, &view_model, &player_model));
	assert(strcmp(view_model, "models/v_luger.mdl") == 0);
	assert(Dod_SelectPlayerClass(1, DOD_CLASS_RIFLEMAN));
	assert(Dod_TeamFromName("aLlIeS") == DOD_TEAM_ALLIES);
	assert(Dod_TeamFromName("German") == DOD_TEAM_AXIS);
	assert(Dod_TeamFromName("spectator") == DOD_TEAM_SPECTATOR);

	player = Dod_GetPlayer(1);
	assert(player && player->connected && player->team == DOD_TEAM_ALLIES);
	assert(player->player_class == DOD_CLASS_RIFLEMAN);
	assert(Dod_GetState()->connected_players[DOD_TEAM_ALLIES] == 1);
	assert(Dod_GetState()->connected_players[DOD_TEAM_AXIS] == 1);

	Dod_PlayerDisconnect(2);
	assert(Dod_GetState()->connected_players[DOD_TEAM_AXIS] == 0);
	Dod_GameShutdown();
	assert(!Dod_GameIsActive());
}

static void test_retail_class_names(void)
{
	assert(strcmp(Dod_TeamClassName(DOD_TEAM_ALLIES, DOD_CLASS_ASSAULT), "Staff Sergeant") == 0);
	assert(strcmp(Dod_TeamClassWeapon(DOD_TEAM_ALLIES, DOD_CLASS_ASSAULT), "Thompson") == 0);
	assert(strcmp(Dod_TeamClassName(DOD_TEAM_AXIS, DOD_CLASS_ASSAULT), "Stosstruppe") == 0);
	assert(strcmp(Dod_TeamClassWeapon(DOD_TEAM_AXIS, DOD_CLASS_ROCKET), "Panzerschreck") == 0);
	assert(strcmp(Dod_TeamClassName(DOD_TEAM_AXIS, DOD_CLASS_COUNT), "None") == 0);
}

static void test_loadouts(void)
{
	DodLoadout l;
	assert(Dod_GetLoadout(DOD_TEAM_ALLIES, DOD_CLASS_RIFLEMAN, &l));
	assert(strcmp(l.weapon, "M1 Garand") == 0 && l.clip_size == 8);
	assert(Dod_GetLoadout(DOD_TEAM_AXIS, DOD_CLASS_MG, &l));
	assert(strcmp(l.weapon, "MG42") == 0 && l.clip_size == 250);
	assert(!Dod_GetLoadout(DOD_TEAM_SPECTATOR, DOD_CLASS_MG, &l));
	assert(!Dod_GetLoadout(DOD_TEAM_AXIS, DOD_CLASS_NONE, &l));
}

static void test_player_line(void)
{
	char line[64];
	Dod_GameInit();
	assert(Dod_PlayerConnect(1));
	assert(Dod_PlayerConnect(2));
	assert(Dod_AssignPlayerTeam(1, DOD_TEAM_ALLIES));
	assert(Dod_AssignPlayerTeam(2, DOD_TEAM_AXIS));
	Dod_RecordKill(1, 2);
	assert(Dod_FormatPlayerLine(1, "Bob", line, sizeof(line)) > 0);
	assert(strcmp(line, "Allies  Bob  1/0") == 0);
	assert(Dod_FormatPlayerLine(2, "", line, sizeof(line)) > 0);
	assert(strcmp(line, "Axis  player  0/1") == 0);
	assert(Dod_FormatPlayerLine(3, "x", line, sizeof(line)) == 0);
	Dod_GameShutdown();
}

static void test_status_line(void)
{
	char line[96];
	char tiny[8];
	Dod_GameInit();
	Dod_StartRound(125.0f);
	assert(Dod_FormatStatus(line, sizeof(line)) > 0);
	assert(strcmp(line, "Allies 0  Axis 0 | Points 0-0 | 2:05") == 0);
	assert(Dod_FormatStatus(tiny, sizeof(tiny)) == 7);
	assert(strlen(tiny) == 7);
	Dod_StartRound(0.0f);
	Dod_FormatStatus(line, sizeof(line));
	assert(strcmp(line, "Allies 0  Axis 0 | Points 0-0") == 0);
	Dod_GameShutdown();
}

static void test_contested_capture_flip_and_round_win(void)
{
	const DodObjectiveState *objective;
	const DodGameRulesState *state;

	Dod_GameInit();
	assert(Dod_PlayerConnect(1));
	assert(Dod_PlayerConnect(2));
	assert(Dod_AssignPlayerTeam(1, DOD_TEAM_ALLIES));
	assert(Dod_AssignPlayerTeam(2, DOD_TEAM_AXIS));
	assert(Dod_RegisterObjective(0, DOD_OBJ_CAPTURE_POINT,
		DOD_TEAM_UNASSIGNED, 2.0f, 60.0f));
	assert(Dod_RegisterObjective(1, DOD_OBJ_CAPTURE_POINT,
		DOD_TEAM_UNASSIGNED, 2.0f, 0.0f));
	Dod_StartRound(0.0f);

	/* Contested players do not advance capture progress. */
	assert(!Dod_TickObjective(0, 1, 1, 2.0f));
	objective = Dod_GetObjective(0);
	assert(objective && objective->owner == DOD_TEAM_UNASSIGNED);
	assert(objective->capture_progress == 0.0f);

	/* A change in capturing team starts that team's progress from zero. */
	assert(!Dod_TickObjective(0, 1, 0, 1.0f));
	assert(!Dod_TickObjective(0, 0, 1, 0.5f));
	objective = Dod_GetObjective(0);
	assert(objective && objective->capturing_team == DOD_TEAM_AXIS);
	assert(objective->capture_progress == 0.25f);
	assert(Dod_TickObjective(0, 0, 1, 1.5f));
	objective = Dod_GetObjective(0);
	assert(objective && objective->owner == DOD_TEAM_AXIS);
	assert(Dod_GetState()->controlled_objectives[DOD_TEAM_AXIS] == 1);
	assert(Dod_GetState()->phase == DOD_ROUND_ACTIVE);

	/* Held-point scoring accumulates, then capturing every point ends the round. */
	Dod_Think(30.0f);
	assert(Dod_GetState()->axis_score == 35);
	assert(!Dod_TickObjective(1, 0, 1, 1.5f));
	assert(Dod_TickObjective(1, 0, 1, 0.5f));
	state = Dod_GetState();
	assert(state->objective_count == 2);
	assert(state->controlled_objectives[DOD_TEAM_AXIS] == 2);
	assert(state->axis_score == 40);
	assert(state->winner == DOD_TEAM_AXIS);
	assert(state->phase == DOD_ROUND_INTERMISSION);
	assert(!state->round_active);
	assert(state->intermission_remaining == 8.0f);
}

static void test_kill_scoring_and_timed_tie(void)
{
	const DodPlayerState *allies;
	const DodPlayerState *axis;
	const DodGameRulesState *state;

	Dod_GameInit();
	assert(Dod_PlayerConnect(1));
	assert(Dod_PlayerConnect(2));
	assert(Dod_PlayerConnect(3));
	assert(Dod_AssignPlayerTeam(1, DOD_TEAM_ALLIES));
	assert(Dod_AssignPlayerTeam(2, DOD_TEAM_AXIS));
	assert(Dod_AssignPlayerTeam(3, DOD_TEAM_ALLIES));
	Dod_StartRound(10.0f);

	Dod_RecordKill(1, 2);
	allies = Dod_GetPlayer(1);
	axis = Dod_GetPlayer(2);
	assert(allies && allies->kills == 1 && allies->objective_score == 1);
	assert(axis && axis->deaths == 1);
	assert(Dod_GetState()->allies_score == 1);

	/* Friendly kills count as a death but do not award an attacker kill/score. */
	Dod_RecordKill(1, 3);
	assert(Dod_GetPlayer(1)->kills == 1);
	assert(Dod_GetPlayer(3)->deaths == 1);
	assert(Dod_GetState()->allies_score == 1);

	Dod_StartRound(1.0f);
	Dod_Think(1.1f);
	state = Dod_GetState();
	assert(state->phase == DOD_ROUND_INTERMISSION);
	assert(state->winner == DOD_TEAM_UNASSIGNED);
	assert(state->round_time_remaining == 0.0f);
	Dod_Think(8.5f);
	assert(Dod_GetState()->intermission_remaining == 0.0f);
}

static void test_grenades(void)
{
	DodGrenadeDef allies, axis;
	const char *view_model = 0, *player_model = 0;
	int i, plan;

	Dod_GameInit();
	assert(!Dod_GetGrenade(DOD_TEAM_UNASSIGNED, &allies));
	assert(!Dod_GetGrenade(DOD_TEAM_ALLIES, 0));
	assert(Dod_GetGrenade(DOD_TEAM_ALLIES, &allies));
	assert(Dod_GetGrenade(DOD_TEAM_AXIS, &axis));
	assert(strcmp(allies.view_model, "models/v_grenade.mdl") == 0);
	assert(strcmp(allies.world_model, "models/w_grenade.mdl") == 0);
	assert(strcmp(axis.view_model, "models/v_stick.mdl") == 0);
	assert(strcmp(axis.player_model, "models/p_stick.mdl") == 0);
	assert(strcmp(axis.world_model, "models/w_stick.mdl") == 0);
	/* radius follows the HL1 RadiusDamage rule the game DLL applies */
	assert(allies.radius == allies.damage * 2.5f);
	{
		DodLoadout load;
		int c;
		for (c = DOD_CLASS_RIFLEMAN; c < DOD_CLASS_COUNT; c++) {
			assert(Dod_GetLoadout(DOD_TEAM_ALLIES, (DodClass)c, &load));
			assert(load.grenades <= allies.max_carry);
			assert(Dod_GetLoadout(DOD_TEAM_AXIS, (DodClass)c, &load));
			assert(load.grenades <= axis.max_carry);
		}
	}

	/* falloff: full at the centre, linear, zero at and beyond the radius */
	assert(Dod_GrenadeDamageAt(&allies, 0.0f) == allies.damage);
	assert(Dod_GrenadeDamageAt(&allies, allies.radius * 0.5f) == allies.damage * 0.5f);
	assert(Dod_GrenadeDamageAt(&allies, allies.radius) == 0.0f);
	assert(Dod_GrenadeDamageAt(&allies, allies.radius + 1.0f) == 0.0f);
	assert(Dod_GrenadeDamageAt(&allies, -1.0f) == 0.0f);
	assert(Dod_GrenadeDamageAt(0, 10.0f) == 0.0f);

	/* throws: lobbing up goes further than a toss down, always clamped */
	assert(Dod_GrenadeThrowSpeed(&allies, -45.0f) > Dod_GrenadeThrowSpeed(&allies, 45.0f));
	for (i = -90; i <= 90; i += 5) {
		const float s = Dod_GrenadeThrowSpeed(&allies, (float)i);
		assert(s >= 0.0f && s <= allies.throw_speed);
	}
	assert(Dod_GrenadeThrowSpeed(0, 0.0f) == 0.0f);
	assert(Dod_GrenadeThrowPitch(0.0f) == -10.0f);
	assert(fabsf(Dod_GrenadeThrowPitch(-90.0f) + 90.0f) < 0.001f);
	assert(fabsf(Dod_GrenadeThrowPitch(90.0f) - 90.0f) < 0.001f);

	/* cooking shortens the fuse and it never goes negative */
	assert(Dod_GrenadeFuseLeft(&allies, 0.0f) == allies.fuse);
	assert(Dod_GrenadeFuseLeft(&allies, 2.0f) == allies.fuse - 2.0f);
	assert(Dod_GrenadeFuseLeft(&allies, allies.fuse + 3.0f) == 0.0f);
	assert(Dod_GrenadeFuseLeft(&allies, -4.0f) == allies.fuse);

	/* grenade models by team, once connected */
	assert(Dod_PlayerConnect(1));
	assert(Dod_PlayerConnect(2));
	assert(Dod_AssignPlayerTeam(1, DOD_TEAM_ALLIES));
	assert(Dod_AssignPlayerTeam(2, DOD_TEAM_AXIS));
	assert(Dod_GetWeaponModels(1, DOD_MODEL_GRENADE, &view_model, &player_model));
	assert(strcmp(view_model, "models/v_grenade.mdl") == 0 && strcmp(player_model, "models/p_grenade.mdl") == 0);
	assert(Dod_GetWeaponModels(2, DOD_MODEL_GRENADE, &view_model, &player_model));
	assert(strcmp(view_model, "models/v_stick.mdl") == 0 && strcmp(player_model, "models/p_stick.mdl") == 0);
	assert(!Dod_GetWeaponModels(3, DOD_MODEL_GRENADE, &view_model, &player_model));

	/* live cap: refuse past the limit, release frees a slot, never underflows */
	for (i = 0; i < DOD_MAX_LIVE_GRENADES; i++)
		assert(Dod_GrenadeAcquire());
	assert(!Dod_GrenadeAcquire());
	assert(Dod_GrenadesLive() == DOD_MAX_LIVE_GRENADES);
	Dod_GrenadeRelease();
	assert(Dod_GrenadeAcquire());
	for (i = 0; i < DOD_MAX_LIVE_GRENADES + 4; i++)
		Dod_GrenadeRelease();
	assert(Dod_GrenadesLive() == 0);
	Dod_GrenadeAcquire();
	Dod_GameInit();
	assert(Dod_GrenadesLive() == 0);

	/* fx budget: a lone blast gets every layer, a barrage degrades to the
	 * fireball alone, and the fireball is never dropped; quiet time recovers */
	plan = Dod_FxPlanExplosion(10.0f);
	assert(plan == (DOD_FX_FIREBALL | DOD_FX_SMOKE | DOD_FX_SPARKS | DOD_FX_DEBRIS));
	plan = Dod_FxPlanExplosion(10.2f);
	assert(plan == (DOD_FX_FIREBALL | DOD_FX_SMOKE | DOD_FX_SPARKS));
	plan = Dod_FxPlanExplosion(10.4f);
	assert(plan == (DOD_FX_FIREBALL | DOD_FX_SMOKE));
	for (i = 0; i < 20; i++) {
		plan = Dod_FxPlanExplosion(10.5f + 0.01f * (float)i);
		assert(plan == DOD_FX_FIREBALL);
	}
	plan = Dod_FxPlanExplosion(20.0f);
	assert(plan == (DOD_FX_FIREBALL | DOD_FX_SMOKE | DOD_FX_SPARKS | DOD_FX_DEBRIS));
	/* a clock that goes backwards (map change) must not wedge the budget */
	plan = Dod_FxPlanExplosion(1.0f);
	assert(plan & DOD_FX_FIREBALL);
	Dod_GameInit();
}

static void test_weapon_table(void)
{
	int id, team, c, i;
	const DodWeaponDef *def;
	DodLoadout load;
	int seen_hl[32] = {0};

	assert(!Dod_GetWeaponDef(DOD_WEAPON_NONE));
	assert(!Dod_GetWeaponDef(DOD_WEAPON_COUNT));
	assert(!Dod_GetWeaponDef((DodWeaponId)-1));
	assert(!Dod_FindWeaponDef(0));
	assert(!Dod_FindWeaponDef("weapon_crowbar"));
	assert(Dod_FindWeaponDef("weapon_dod_garand")->id == DOD_WEAPON_GARAND);

	for (id = DOD_WEAPON_NONE + 1; id < DOD_WEAPON_COUNT; id++) {
		def = Dod_GetWeaponDef((DodWeaponId)id);
		assert(def && def->id == (DodWeaponId)id);
		assert(Dod_FindWeaponDef(def->classname) == def);
		assert(strncmp(def->classname, "weapon_dod_", 11) == 0);
		assert(def->team == DOD_TEAM_ALLIES || def->team == DOD_TEAM_AXIS);
		assert(def->hl_id == DOD_HLID(id) && def->hl_id >= 16 && def->hl_id <= 29);
		assert(def->clip > 0 && def->reserve > 0 && def->reserve <= def->ammo_max);
		assert(def->ammo_max <= 250);	/* the HUD ammo message is one byte */
		assert(def->damage > 0.0f && def->interval > 0.0f && def->reload_seconds > 0.0f);
		assert(def->spread >= 0.0f && def->range > 0.0f);
		assert(def->slot >= 0 && def->slot <= 4 && def->position >= 0);
		assert(strncmp(def->ammo, "dod_", 4) == 0);
		assert(strncmp(def->view_model, "models/v_", 9) == 0);
		assert(strncmp(def->player_model, "models/p_", 9) == 0);
		assert(strncmp(def->fire_sound, "weapons/", 8) == 0);
		assert(strncmp(def->reload_sound, "weapons/", 8) == 0);
		assert(def->mode != DOD_FIRE_BOLT || def->interval >= 1.0f);	/* bolt cycle is slow */
		assert(def->mode != DOD_FIRE_AUTO || def->interval <= 0.15f);	/* automatics are fast */
		assert(def->mode != DOD_FIRE_SEMI || (def->interval >= 0.15f && def->interval < 1.0f));
		assert(def->mode != DOD_FIRE_ROCKET || (def->clip == 1 && def->spread == 0.0f));
		assert(def->scope_fov == 0.0f || def->mode == DOD_FIRE_BOLT);
		assert(def->bipod == 0 || def->mode == DOD_FIRE_AUTO);
		/* HL1 keeps ItemInfo per id, so ids must be unique */
		assert(!seen_hl[def->hl_id]);
		seen_hl[def->hl_id] = 1;
	}

	/* every class on every team has a primary that matches its loadout */
	for (team = DOD_TEAM_ALLIES; team <= DOD_TEAM_AXIS; team++) {
		for (c = DOD_CLASS_RIFLEMAN; c < DOD_CLASS_COUNT; c++) {
			def = Dod_ClassWeaponDef((DodTeam)team, (DodClass)c);
			assert(def && def->team == (DodTeam)team);
			assert(Dod_GetLoadout((DodTeam)team, (DodClass)c, &load));
			assert(strcmp(load.weapon, def->title) == 0);
			assert(load.clip_size == def->clip && load.reserve_rounds == def->reserve);
			assert(strcmp(load.weapon, Dod_TeamClassWeapon((DodTeam)team, (DodClass)c)) == 0);
			if (Dod_ClassHasSidearm((DodClass)c)) {
				const DodWeaponDef *side = Dod_ClassSidearmDef((DodTeam)team, (DodClass)c);
				assert(side && side->team == (DodTeam)team && side->slot == 1);
			} else
				assert(!Dod_ClassSidearmDef((DodTeam)team, (DodClass)c));
		}
	}
	assert(!Dod_ClassWeaponDef(DOD_TEAM_SPECTATOR, DOD_CLASS_RIFLEMAN));
	assert(!Dod_ClassWeaponDef(DOD_TEAM_ALLIES, DOD_CLASS_NONE));
	assert(!Dod_ClassWeaponDef(DOD_TEAM_AXIS, DOD_CLASS_COUNT));
	assert(!Dod_ClassSidearmDef(DOD_TEAM_UNASSIGNED, DOD_CLASS_MG));

	/* the class weapon uses the same models the model lookup hands out */
	Dod_GameInit();
	assert(Dod_PlayerConnect(1) && Dod_AssignPlayerTeam(1, DOD_TEAM_ALLIES));
	for (c = DOD_CLASS_RIFLEMAN; c < DOD_CLASS_COUNT; c++) {
		const char *view = 0, *player = 0;
		DodWeaponModelKind kind = c == DOD_CLASS_ROCKET ? DOD_MODEL_ROCKET
			: c == DOD_CLASS_SNIPER ? DOD_MODEL_SNIPER
			: c == DOD_CLASS_SUPPORT ? DOD_MODEL_SUPPORT : DOD_MODEL_AUTOMATIC;
		assert(Dod_SelectPlayerClass(1, (DodClass)c));
		def = Dod_ClassWeaponDef(DOD_TEAM_ALLIES, (DodClass)c);
		if (c == DOD_CLASS_MG) {
			continue;	/* the MG models are not part of the stand-in lookup */
		}
		assert(Dod_GetWeaponModels(1, kind, &view, &player));
		assert(strcmp(view, def->view_model) == 0);
		assert(strcmp(player, def->player_model) == 0);
	}
	Dod_GameInit();

	/* spread: each modifier moves it the right way and it never goes negative */
	def = Dod_GetWeaponDef(DOD_WEAPON_BROWNING);
	assert(Dod_WeaponSpread(def, 0) == def->spread);
	assert(Dod_WeaponSpread(def, DOD_ACC_MOVING) > Dod_WeaponSpread(def, 0));
	assert(Dod_WeaponSpread(def, DOD_ACC_AIRBORNE) > Dod_WeaponSpread(def, DOD_ACC_MOVING));
	assert(Dod_WeaponSpread(def, DOD_ACC_CROUCHED) < Dod_WeaponSpread(def, 0));
	assert(Dod_WeaponSpread(def, DOD_ACC_DEPLOYED) < Dod_WeaponSpread(def, DOD_ACC_CROUCHED));
	assert(Dod_WeaponSpread(def, DOD_ACC_SCOPED) == def->spread);	/* no scope on an MG */
	def = Dod_GetWeaponDef(DOD_WEAPON_GARAND);
	assert(Dod_WeaponSpread(def, DOD_ACC_DEPLOYED) == def->spread);	/* no bipod on a rifle */
	def = Dod_GetWeaponDef(DOD_WEAPON_SPRINGFIELD);
	assert(Dod_WeaponSpread(def, DOD_ACC_SCOPED) < Dod_WeaponSpread(def, DOD_ACC_CROUCHED));
	assert(Dod_WeaponSpread(0, 0) == 0.0f);
	for (id = DOD_WEAPON_NONE + 1; id < DOD_WEAPON_COUNT; id++)
		for (i = 0; i < 32; i++)
			assert(Dod_WeaponSpread(Dod_GetWeaponDef((DodWeaponId)id), i) >= 0.0f);

	/* reload moves what fits, limited by reserve */
	def = Dod_GetWeaponDef(DOD_WEAPON_THOMPSON);
	assert(Dod_ReloadAmount(def, 0, 180) == 30);
	assert(Dod_ReloadAmount(def, 12, 180) == 18);
	assert(Dod_ReloadAmount(def, 12, 5) == 5);
	assert(Dod_ReloadAmount(def, 30, 180) == 0);
	assert(Dod_ReloadAmount(def, 99, 180) == 0);
	assert(Dod_ReloadAmount(def, -3, 180) == 30);
	assert(Dod_ReloadAmount(def, 0, 0) == 0);
	assert(Dod_ReloadAmount(0, 0, 10) == 0);

	/* trigger discipline */
	assert(Dod_WeaponMayFire(Dod_GetWeaponDef(DOD_WEAPON_THOMPSON), 1));
	assert(!Dod_WeaponMayFire(Dod_GetWeaponDef(DOD_WEAPON_GARAND), 1));
	assert(Dod_WeaponMayFire(Dod_GetWeaponDef(DOD_WEAPON_GARAND), 0));
	assert(!Dod_WeaponMayFire(Dod_GetWeaponDef(DOD_WEAPON_K98), 1));
	assert(!Dod_WeaponMayFire(0, 0));
}

int main(void)
{
	test_players_and_weapon_models();
	test_retail_class_names();
	test_status_line();
	test_player_line();
	test_loadouts();
	test_contested_capture_flip_and_round_win();
	test_kill_scoring_and_timed_tie();
	test_grenades();
	test_weapon_table();
	return 0;
}
