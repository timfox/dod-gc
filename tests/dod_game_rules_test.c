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

static void test_player_body(void)
{
	/* us-inf: rifleman is light_body (0) + light_gear (0); player 1 has helmet2 (1) and head1 (1) */
	assert(Dod_PlayerBodyValue("us-inf", DOD_CLASS_RIFLEMAN, 0) == 0);
	assert(Dod_PlayerBodyValue("us-inf", DOD_CLASS_RIFLEMAN, 1) == 0 + 6 * 1 + 48 * 1 + 336 * 0);
	assert(Dod_PlayerBodyValue("us-inf", DOD_CLASS_MG, 0) == 3 + 336 * 3);	/* mg_body and mg_gear */
	assert(Dod_PlayerBodyValue("us-inf", DOD_CLASS_SNIPER, 2) == 4 + 6 * 2 + 48 * 2 + 336 * 4);
	/* axis-inf has a k43 part at index 1, so medium is 2 and the sniper is 5 */
	assert(Dod_PlayerBodyValue("axis-inf", DOD_CLASS_ASSAULT, 0) == 2 + 336 * 2);
	assert(Dod_PlayerBodyValue("axis-inf", DOD_CLASS_SNIPER, 5) == 5 + 48 * 5 + 336 * 5);
	/* brit-inf: one body, the gear is the class: Enfield 0, Sten 1, Bren 2, sniper gear 3 with the sniper helmet */
	assert(Dod_PlayerBodyValue("brit-inf", DOD_CLASS_RIFLEMAN, 0) == 0);
	assert(Dod_PlayerBodyValue("brit-inf", DOD_CLASS_ASSAULT, 3) == 0 + 48 * 3 + 336 * 1);
	assert(Dod_PlayerBodyValue("brit-inf", DOD_CLASS_SNIPER, 0) == 6 * 1 + 336 * 3);
	/* never past the model's groups (6 bodies, 8 helmets, 7 heads, 7 gear sets): the largest value is below 6*8*7*7 */
	{
		const char *models[3] = { "us-inf", "axis-inf", "brit-inf" };
		int m, c, p;
		for (m = 0; m < 3; ++m)
			for (c = DOD_CLASS_RIFLEMAN; c < DOD_CLASS_COUNT; ++c)
				for (p = 0; p < 32; ++p) {
					const int v = Dod_PlayerBodyValue(models[m], (DodClass)c, p);
					assert(v >= 0 && v < 6 * 8 * 7 * 7 && v % 6 < 6 && (v / 6) % 8 < 3 && (v / 48) % 7 < 6 && v / 336 < 6);
				}
	}
	/* the paratrooper models: one body, the gear is the weapon (US: carbine 5, grease gun 6; Axis: FG42 6, MP40 0) */
	assert(Dod_PlayerBodyValue("us-para", DOD_CLASS_RIFLEMAN, 0) == 336 * 5);
	assert(Dod_PlayerBodyValue("us-para", DOD_CLASS_ASSAULT, 1) == 6 * 1 + 48 * 1 + 336 * 6);
	assert(Dod_PlayerBodyValue("axis-para", DOD_CLASS_RIFLEMAN, 2) == 48 * 2 + 336 * 6);
	assert(Dod_PlayerBodyValue("axis-para", DOD_CLASS_ASSAULT, 0) == 0);
	assert(Dod_PlayerBodyValue("axis-para", DOD_CLASS_SNIPER, 0) == 336 * 1);
	assert(Dod_PlayerBodyValue("nonsense", DOD_CLASS_RIFLEMAN, 4) == 0);
	assert(Dod_PlayerBodyValue(NULL, DOD_CLASS_RIFLEMAN, 4) == 0);
	assert(Dod_PlayerBodyValue("us-inf", DOD_CLASS_NONE, 4) == 0);
}

static void test_kill_labels(void)
{
	Dod_SetAlliesCountry(DOD_COUNTRY_US);
	Dod_SetParatroopers(0, 0);
	assert(!strcmp(Dod_KillWeaponLabel("weapon_dod_garand", DOD_TEAM_ALLIES, 0), "M1 Garand"));
	assert(!strcmp(Dod_KillWeaponLabel("weapon_dod_mp40", DOD_TEAM_AXIS, 0), "MP40"));
	assert(!strcmp(Dod_KillWeaponLabel("weapon_crowbar", DOD_TEAM_ALLIES, 0), "Knife"));
	assert(!strcmp(Dod_KillWeaponLabel("weapon_crowbar", DOD_TEAM_AXIS, 0), "Spade"));
	assert(!strcmp(Dod_KillWeaponLabel("weapon_crowbar", DOD_TEAM_ALLIES, 1), "Fairbairn"));
	assert(!strcmp(Dod_KillWeaponLabel("grenade", DOD_TEAM_ALLIES, 0), "Grenade"));
	assert(!strcmp(Dod_KillWeaponLabel("weapon_handgrenade", DOD_TEAM_AXIS, 0), "Grenade"));
	assert(!strcmp(Dod_KillWeaponLabel("dod_rocket", DOD_TEAM_AXIS, 0), "Rocket"));
	assert(!strcmp(Dod_KillWeaponLabel("trigger_hurt", DOD_TEAM_AXIS, 0), "World") && !strcmp(Dod_KillWeaponLabel(0, DOD_TEAM_AXIS, 0), "World"));
	assert(!strcmp(Dod_KillWeaponLabel("weapon_nothing", DOD_TEAM_AXIS, 0), "Weapon"));
	/* the British and paratrooper variants keep their class names, so the title follows the kit */
	Dod_SetAlliesCountry(DOD_COUNTRY_BRITISH);
	assert(!strcmp(Dod_KillWeaponLabel("weapon_dod_garand", DOD_TEAM_ALLIES, 1), "Lee-Enfield"));
	Dod_SetAlliesCountry(DOD_COUNTRY_US);
}

static void test_class_limits(void)
{
	int i;
	Dod_GameInit();
	for (i = 1; i <= 4; ++i) {
		assert(Dod_PlayerConnect(i));
		assert(Dod_AssignPlayerTeam(i, DOD_TEAM_ALLIES));
	}
	assert(Dod_PlayerConnect(5) && Dod_AssignPlayerTeam(5, DOD_TEAM_AXIS));
	/* unlimited by default */
	assert(Dod_ClassLimit(DOD_TEAM_ALLIES, DOD_CLASS_SNIPER) == -1 && Dod_ClassAvailable(DOD_TEAM_ALLIES, DOD_CLASS_SNIPER, 1));
	/* a limit of 2 snipers: the third is turned away, the second player's own pick does not count against itself */
	Dod_SetClassLimit(DOD_TEAM_ALLIES, DOD_CLASS_SNIPER, 2);
	assert(Dod_SelectPlayerClass(1, DOD_CLASS_SNIPER) && Dod_SelectPlayerClass(2, DOD_CLASS_SNIPER));
	assert(Dod_ClassCount(DOD_TEAM_ALLIES, DOD_CLASS_SNIPER, 0) == 2);
	assert(!Dod_ClassAvailable(DOD_TEAM_ALLIES, DOD_CLASS_SNIPER, 3));
	assert(Dod_ClassAvailable(DOD_TEAM_ALLIES, DOD_CLASS_SNIPER, 2));	/* already one of them */
	assert(Dod_ClassAvailable(DOD_TEAM_ALLIES, DOD_CLASS_MG, 3) && Dod_ClassAvailable(DOD_TEAM_AXIS, DOD_CLASS_SNIPER, 5));
	/* a player who leaves the class makes room */
	assert(Dod_SelectPlayerClass(1, DOD_CLASS_RIFLEMAN));
	assert(Dod_ClassAvailable(DOD_TEAM_ALLIES, DOD_CLASS_SNIPER, 3));
	/* 0 turns a class off; the mask does too */
	Dod_SetClassLimit(DOD_TEAM_ALLIES, DOD_CLASS_ROCKET, 0);
	assert(!Dod_ClassAvailable(DOD_TEAM_ALLIES, DOD_CLASS_ROCKET, 3));
	Dod_SetClassMask(DOD_TEAM_AXIS, (1 << DOD_CLASS_RIFLEMAN) | (1 << DOD_CLASS_MG));
	assert(Dod_ClassAvailable(DOD_TEAM_AXIS, DOD_CLASS_MG, 5) && !Dod_ClassAvailable(DOD_TEAM_AXIS, DOD_CLASS_SNIPER, 5));
	Dod_SetClassMask(DOD_TEAM_AXIS, -1);
	assert(Dod_ClassAvailable(DOD_TEAM_AXIS, DOD_CLASS_SNIPER, 5));
	/* bad arguments */
	assert(!Dod_ClassAvailable(DOD_TEAM_SPECTATOR, DOD_CLASS_RIFLEMAN, 1) && !Dod_ClassAvailable(DOD_TEAM_ALLIES, DOD_CLASS_NONE, 1));
	assert(Dod_ClassLimit(DOD_TEAM_SPECTATOR, DOD_CLASS_SNIPER) == -1);
	Dod_SetClassLimit(DOD_TEAM_ALLIES, DOD_CLASS_SNIPER, -1);
	Dod_SetClassLimit(DOD_TEAM_ALLIES, DOD_CLASS_ROCKET, -1);
	Dod_GameShutdown();
}

static void test_stamina(void)
{
	float now = 100.0f, speed;
	int i;
	Dod_GameInit();
	assert(Dod_PlayerConnect(1));
	assert(Dod_PlayerStamina(1) == 100 && !Dod_PlayerSprinting(1));
	/* walking: run speed, no sprint */
	speed = Dod_StaminaThink(1, now, 0.1f, 1, 0, 1);
	assert(speed == DOD_RUN_SPEED && !Dod_PlayerSprinting(1));
	now += 0.1f; Dod_StaminaThink(1, now, 0.1f, 0, 0, 0);	/* release */
	now += 0.1f; speed = Dod_StaminaThink(1, now, 0.1f, 1, 0, 1);	/* press again inside the window: sprint */
	assert(speed == DOD_SPRINT_SPEED && Dod_PlayerSprinting(1));
	/* the sprint drains, 22 a second */
	for (i = 0; i < 10; ++i) { now += 0.1f; Dod_StaminaThink(1, now, 0.1f, 1, 0, 1); }
	assert(Dod_PlayerStamina(1) >= 76 && Dod_PlayerStamina(1) <= 78);
	/* releasing forward ends the sprint; a slow second press is no double tap */
	now += 0.1f; Dod_StaminaThink(1, now, 0.1f, 0, 0, 1);
	assert(!Dod_PlayerSprinting(1));
	now += 0.5f; speed = Dod_StaminaThink(1, now, 0.1f, 1, 0, 1);
	assert(speed == DOD_RUN_SPEED && !Dod_PlayerSprinting(1));
	/* no refill during the rest, then it refills */
	{
		const int before = Dod_PlayerStamina(1);
		now += 0.2f; Dod_StaminaThink(1, now, 0.2f, 1, 0, 1);
		assert(Dod_PlayerStamina(1) == before);
		for (i = 0; i < 20; ++i) { now += 0.5f; Dod_StaminaThink(1, now, 0.5f, 1, 0, 1); }
		assert(Dod_PlayerStamina(1) == 100);
	}
	/* a jump costs 8 */
	now += 0.1f; Dod_StaminaThink(1, now, 0.1f, 1, 1, 1);
	assert(Dod_PlayerStamina(1) == 92);
	/* running dry ends the sprint and locks it until the tube has refilled to 25 */
	Dod_StaminaReset(1);
	now += 1.0f; Dod_StaminaThink(1, now, 0.1f, 1, 0, 1);
	now += 0.1f; Dod_StaminaThink(1, now, 0.1f, 0, 0, 1);
	now += 0.1f; Dod_StaminaThink(1, now, 0.1f, 1, 0, 1);
	assert(Dod_PlayerSprinting(1));
	for (i = 0; i < 60 && Dod_PlayerSprinting(1); ++i) { now += 0.1f; Dod_StaminaThink(1, now, 0.1f, 1, 0, 1); }
	assert(!Dod_PlayerSprinting(1) && Dod_PlayerStamina(1) == 0);
	now += 0.1f; Dod_StaminaThink(1, now, 0.1f, 0, 0, 1);
	now += 0.1f; speed = Dod_StaminaThink(1, now, 0.1f, 1, 0, 1);
	assert(speed == DOD_RUN_SPEED);	/* locked: the double tap does nothing */
	/* standing against a wall is not sprinting */
	Dod_StaminaReset(1);
	now += 1.0f; Dod_StaminaThink(1, now, 0.1f, 1, 0, 1);
	now += 0.1f; Dod_StaminaThink(1, now, 0.1f, 0, 0, 0);
	now += 0.1f; speed = Dod_StaminaThink(1, now, 0.1f, 1, 0, 0);
	assert(speed == DOD_SPRINT_SPEED);	/* the speed has not built yet: the sprint starts */
	now += 0.5f; speed = Dod_StaminaThink(1, now, 0.5f, 1, 0, 0);	/* still not moving half a second later: it ends */
	assert(speed == DOD_RUN_SPEED && !Dod_PlayerSprinting(1));
	/* unknown players are harmless */
	assert(Dod_StaminaThink(0, now, 0.1f, 1, 0, 1) == DOD_RUN_SPEED && Dod_PlayerStamina(99) == 0);
	Dod_GameShutdown();
}

static void test_voice(void)
{
	char path[64];
	int i, v;
	assert(Dod_VoiceCount() >= DOD_VOICE_MENU_COUNT + 2);
	assert(Dod_VoiceFind("attack") == 0 && Dod_VoiceFind("grenade") == 10 && Dod_VoiceFind("fireinhole") == 11);
	assert(Dod_VoiceFind("nonsense") == -1 && Dod_VoiceFind(0) == -1);
	assert(Dod_VoiceSound(0, DOD_TEAM_ALLIES, 0, path, sizeof(path)) > 0 && !strcmp(path, "player/usattack.wav"));
	assert(Dod_VoiceSound(0, DOD_TEAM_ALLIES, 1, path, sizeof(path)) > 0 && !strcmp(path, "player/britattack.wav"));
	assert(Dod_VoiceSound(0, DOD_TEAM_AXIS, 0, path, sizeof(path)) > 0 && !strcmp(path, "player/gerattack.wav"));
	assert(Dod_VoiceSound(Dod_VoiceFind("left"), DOD_TEAM_AXIS, 0, path, sizeof(path)) > 0 && !strcmp(path, "player/germanleft.wav"));
	assert(Dod_VoiceSound(0, DOD_TEAM_SPECTATOR, 0, path, sizeof(path)) == 0 && Dod_VoiceSound(99, DOD_TEAM_AXIS, 0, path, sizeof(path)) == 0);
	assert(Dod_VoiceSound(0, DOD_TEAM_AXIS, 0, path, 8) == 0 && path[0] == '\0');	/* too small: nothing partial */
	for (v = 0; v < Dod_VoiceCount(); ++v) {
		assert(Dod_VoiceLabel(v)[0]);
		for (i = 0; i < 3; ++i)
			assert(Dod_VoiceSound(v, i == 2 ? DOD_TEAM_AXIS : DOD_TEAM_ALLIES, i == 1, path, sizeof(path)) > 0);
	}
}

static void test_scoreboard(void)
{
	char board[512], tiny[40];
	const char *names[DOD_MAX_PLAYERS + 1] = { 0 };
	Dod_GameInit();
	assert(Dod_PlayerConnect(1) && Dod_PlayerConnect(2) && Dod_PlayerConnect(3) && Dod_PlayerConnect(4));
	assert(Dod_AssignPlayerTeam(1, DOD_TEAM_ALLIES) && Dod_AssignPlayerTeam(2, DOD_TEAM_ALLIES));
	assert(Dod_AssignPlayerTeam(3, DOD_TEAM_AXIS) && Dod_AssignPlayerTeam(4, DOD_TEAM_AXIS));
	Dod_SelectPlayerClass(1, DOD_CLASS_SNIPER);
	Dod_SelectPlayerClass(3, DOD_CLASS_MG);
	names[1] = "Ann"; names[2] = "Bob"; names[3] = "Cy"; /* 4 has no name */
	assert(Dod_RegisterObjective(0, DOD_OBJ_CAPTURE_POINT, DOD_TEAM_AXIS, 2.0f, 0.0f));
	Dod_RecordKill(1, 3);
	Dod_RecordKill(1, 4);
	Dod_RecordKill(2, 3);
	Dod_RecordKill(2, 1);	/* team kill: no kill, one team kill */
	/* Score is objectives only: kills and team kills never touch it */
	assert(Dod_PlayerScore(1) == 0 && Dod_PlayerScore(2) == 0);
	Dod_RecordCapture(2, 1);	/* a point with the default points_for_cap 1 */
	Dod_SetObjectivePointsForCap(0, 3);
	Dod_RecordCapture(2, Dod_GetObjective(0)->points_for_cap);	/* a point the map set to 3 */
	assert(Dod_PlayerScore(2) == 4 && Dod_PlayerScore(1) == 0 && Dod_PlayerScore(99) == 0);
	assert(Dod_FormatScoreboard(board, sizeof(board), names, 1) > 0);
	/* Bob leads on Score 4 although Ann has more kills; Ann is the viewer; Score, Kills, Deaths are separate columns */
	assert(strstr(board, "Allies  "));
	assert(strstr(board, "\n1. Bob  --  S4 K1 D0\n>2. Ann  SNP  S0 K2 D1\n") || strstr(board, "1. Bob  --  S4 K1 D0\n>2. Ann  SNP  S0 K2 D1\n"));
	assert(strstr(board, "Axis  "));
	assert(strstr(board, "\n1. player  --  S0 K0 D1\n2. Cy  MG  S0 K0 D2\n")); /* equal Score and kills: fewer deaths first */
	/* a buffer that is too small drops whole lines, never part of one */
	assert(Dod_FormatScoreboard(tiny, sizeof(tiny), names, 0) > 0);
	assert(strlen(tiny) < sizeof(tiny) && tiny[strlen(tiny) - 1] == '\n');
	Dod_ResetPlayerStats();
	assert(Dod_PlayerScore(2) == 0);
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
	/* a match tally adds the round and the rounds won */
	Dod_GameInit();
	Dod_SetRoundsToWin(2);
	Dod_StartRound(125.0f);
	Dod_FormatStatus(line, sizeof(line));
	assert(strcmp(line, "Allies 0  Axis 0 | Points 0-0 | 2:05 | Round 1 (0-0)") == 0);
	Dod_EndRound(DOD_TEAM_AXIS);
	Dod_FormatStatus(line, sizeof(line));
	assert(strcmp(line, "Allies 0  Axis 0 | Points 0-0 | Round 1 (0-1)") == 0);
	assert(Dod_FormatStatus(tiny, sizeof(tiny)) == 7);
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
	Dod_SetObjectiveTeamPoints(0, 5, 0);
	Dod_SetObjectiveTeamPoints(1, 5, 0);
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
	assert(allies && allies->kills == 1 && allies->objective_score == 0);
	assert(axis && axis->deaths == 1);
	assert(Dod_GetState()->allies_score == 0);	/* kills add nothing to the team score, as in retail */

	/* Friendly kills count as a death but do not award an attacker kill/score. */
	Dod_RecordKill(1, 3);
	assert(Dod_GetPlayer(1)->kills == 1);
	assert(Dod_GetPlayer(3)->deaths == 1);
	assert(Dod_GetState()->allies_score == 0);

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

static void tick_to_capture(int id, DodTeam team)
{
	/* hold a point with one player until it flips */
	int guard = 0;
	while (!Dod_TickObjective(id, team == DOD_TEAM_ALLIES, team == DOD_TEAM_AXIS, 1.0f) && ++guard < 100)
		;
	assert(guard < 100);
}

static void test_team_points(void)
{
	Dod_GameInit();
	assert(Dod_PlayerConnect(1) && Dod_AssignPlayerTeam(1, DOD_TEAM_ALLIES));
	/* retail defaults: capturing gives the team point_team_points (0), holding gives point_pointvalue (1) every 20 s */
	assert(Dod_RegisterObjective(0, DOD_OBJ_CAPTURE_POINT, DOD_TEAM_UNASSIGNED, 1.0f, 0.0f));
	assert(Dod_RegisterObjective(1, DOD_OBJ_CAPTURE_POINT, DOD_TEAM_AXIS, 1.0f, 0.0f));
	assert(Dod_RegisterObjective(2, DOD_OBJ_CAPTURE_POINT, DOD_TEAM_UNASSIGNED, 1.0f, 0.0f));	/* keeps the round going */
	Dod_SetObjectiveTeamPoints(2, 0, 0);
	assert(Dod_GetObjective(0)->team_points == 0 && Dod_GetObjective(0)->time_points == 1);
	Dod_StartRound(0.0f);
	assert(Dod_TickObjective(0, 1, 0, 1.0f));
	assert(Dod_GetState()->allies_score == 0 && Dod_GetState()->axis_score == 0);	/* no capture bonus by default */
	Dod_Think(19.0f);
	assert(Dod_GetState()->allies_score == 0 && Dod_GetState()->axis_score == 0);
	Dod_Think(1.0f);	/* the first give delay: each owner gets one point per point it holds */
	assert(Dod_GetState()->allies_score == 1 && Dod_GetState()->axis_score == 1);
	/* the map sets the values and the delay */
	Dod_SetObjectiveTeamPoints(0, 3, 4);
	Dod_SetPointGiveDelay(5.0f);
	Dod_Think(5.0f);
	assert(Dod_GetState()->allies_score == 5 && Dod_GetState()->axis_score == 2);
	assert(Dod_TickObjective(0, 0, 1, 1.0f));	/* Axis take it: its 3 team points */
	assert(Dod_GetState()->axis_score == 5);
	/* a point worth nothing over time gives nothing */
	Dod_SetObjectiveTeamPoints(0, 0, 0);
	Dod_Think(5.0f);
	assert(Dod_GetState()->axis_score == 6 && Dod_GetState()->allies_score == 5);
	Dod_GameShutdown();
}

static void test_match_loop(void)
{
	const DodGameRulesState *state;
	const DodPlayerState *player;
	int serial, i;

	Dod_GameInit();
	assert(Dod_PlayerConnect(1) && Dod_PlayerConnect(2));
	assert(Dod_AssignPlayerTeam(1, DOD_TEAM_ALLIES) && Dod_AssignPlayerTeam(2, DOD_TEAM_AXIS));
	assert(Dod_SelectPlayerClass(1, DOD_CLASS_SNIPER));
	/* Axis start with both points; Allies must take them. */
	assert(Dod_RegisterObjective(0, DOD_OBJ_CAPTURE_POINT, DOD_TEAM_AXIS, 2.0f, 60.0f));
	assert(Dod_RegisterObjective(1, DOD_OBJ_CAPTURE_POINT, DOD_TEAM_AXIS, 2.0f, 0.0f));
	Dod_SetRoundsToWin(2);
	Dod_StartRound(300.0f);
	state = Dod_GetState();
	assert(state->round_number == 1 && state->rounds_to_win == 2);
	assert(state->phase == DOD_ROUND_ACTIVE && state->restart_serial == 0);
	assert(Dod_GetObjective(0)->default_owner == DOD_TEAM_AXIS);

	/* Owning every point at the start is not a win: only a capture ends the round. */
	Dod_Think(1.0f);
	assert(state->phase == DOD_ROUND_ACTIVE);

	/* Allies take both points: round one goes to Allies. */
	Dod_RecordKill(1, 2);
	assert(state->allies_score == 0);	/* a kill adds nothing to the team score */
	tick_to_capture(0, DOD_TEAM_ALLIES);
	assert(state->phase == DOD_ROUND_ACTIVE && state->controlled_objectives[DOD_TEAM_ALLIES] == 1);
	tick_to_capture(1, DOD_TEAM_ALLIES);
	assert(state->phase == DOD_ROUND_INTERMISSION && state->winner == DOD_TEAM_ALLIES);
	assert(state->rounds_won[DOD_TEAM_ALLIES] == 1 && state->rounds_won[DOD_TEAM_AXIS] == 0);

	/* Nothing scores between rounds (kills are still recorded). */
	{
		const int score = state->allies_score;
		Dod_RecordKill(1, 2);
		Dod_Think(0.5f);
		assert(state->allies_score == score);
		assert(Dod_GetPlayer(1)->kills == 2);
	}
	/* A tick on a point during the intermission does nothing. */
	assert(!Dod_TickObjective(0, 0, 1, 100.0f));

	/* The intermission runs out and the next round starts by itself. */
	serial = state->restart_serial;
	Dod_Think(DOD_INTERMISSION_SECONDS);
	assert(state->phase == DOD_ROUND_ACTIVE && state->round_active);
	assert(state->restart_serial == serial + 1);
	assert(state->round_number == 2);
	assert(state->winner == DOD_TEAM_UNASSIGNED);
	assert(state->allies_score == 0 && state->axis_score == 0);	/* round scores reset */
	assert(state->round_time_remaining == 300.0f);
	assert(Dod_GetObjective(0)->owner == DOD_TEAM_AXIS && Dod_GetObjective(1)->owner == DOD_TEAM_AXIS);
	assert(state->controlled_objectives[DOD_TEAM_AXIS] == 2 && state->controlled_objectives[DOD_TEAM_ALLIES] == 0);
	assert(state->rounds_won[DOD_TEAM_ALLIES] == 1);		/* the tally carries over */
	player = Dod_GetPlayer(1);
	assert(player->connected && player->team == DOD_TEAM_ALLIES && player->player_class == DOD_CLASS_SNIPER);
	assert(state->match_serial == 0);

	/* A timed-out round with a lead goes to the leader; a second win ends the match. */
	Dod_AddTeamScore(DOD_TEAM_ALLIES, 1);	/* the lead, from a dod_score_ent: kills add none */
	Dod_Think(300.0f);
	assert(state->phase == DOD_ROUND_INTERMISSION && state->winner == DOD_TEAM_ALLIES);
	assert(state->rounds_won[DOD_TEAM_ALLIES] == 2);
	serial = state->restart_serial;
	Dod_Think(DOD_INTERMISSION_SECONDS);
	assert(state->match_serial == 1 && state->match_winner == DOD_TEAM_ALLIES);
	assert(state->rounds_won[DOD_TEAM_ALLIES] == 0 && state->rounds_won[DOD_TEAM_AXIS] == 0);
	assert(state->round_number == 1);			/* a fresh match starts at round one */
	assert(state->restart_serial == serial + 1 && state->phase == DOD_ROUND_ACTIVE);
	player = Dod_GetPlayer(1);
	assert(player->kills == 0 && player->deaths == 0 && player->objective_score == 0);
	assert(player->team == DOD_TEAM_ALLIES && player->player_class == DOD_CLASS_SNIPER);

	/* A drawn round awards nobody and the loop keeps going. */
	Dod_Think(300.0f);
	assert(state->phase == DOD_ROUND_INTERMISSION && state->winner == DOD_TEAM_UNASSIGNED);
	assert(state->rounds_won[DOD_TEAM_ALLIES] == 0 && state->rounds_won[DOD_TEAM_AXIS] == 0);
	Dod_Think(DOD_INTERMISSION_SECONDS);
	assert(state->phase == DOD_ROUND_ACTIVE && state->match_serial == 1);

	/* Rounds repeat forever with no match limit, and a zero-length clock never times out. */
	Dod_GameInit();
	assert(Dod_RegisterObjective(0, DOD_OBJ_CAPTURE_POINT, DOD_TEAM_UNASSIGNED, 1.0f, 0.0f));
	Dod_StartRound(0.0f);
	for (i = 0; i < 100; i++)
		Dod_Think(60.0f);
	assert(state->phase == DOD_ROUND_ACTIVE);
	Dod_EndRound(DOD_TEAM_ALLIES);
	for (i = 0; i < 5; i++) {
		Dod_Think(DOD_INTERMISSION_SECONDS);
		assert(state->phase == DOD_ROUND_ACTIVE);
		Dod_EndRound(DOD_TEAM_AXIS);
	}
	assert(state->match_serial == 0 && state->rounds_won[DOD_TEAM_AXIS] == 5);
	Dod_SetRoundsToWin(-4);
	assert(state->rounds_to_win == 0);
	Dod_GameInit();
}

static void test_map_driven_round_flow(void)
{
	const DodGameRulesState *state;
	int i;

	/* Master-driven: owning every point reports a capture but the map ends the round. */
	Dod_GameInit();
	state = Dod_GetState();
	assert(!state->master_driven && !state->timer_driven);
	assert(Dod_RegisterObjective(1, DOD_OBJ_CAPTURE_POINT, DOD_TEAM_AXIS, 1.0f, 0.0f));
	assert(Dod_RegisterObjective(2, DOD_OBJ_CAPTURE_POINT, DOD_TEAM_AXIS, 1.0f, 0.0f));
	Dod_SetMasterDriven(1);
	Dod_StartRound(0.0f);
	assert(Dod_TeamOwnsAllObjectives(DOD_TEAM_AXIS) && !Dod_TeamOwnsAllObjectives(DOD_TEAM_ALLIES));
	assert(Dod_TickObjective(1, 1, 0, 1.0f));
	assert(state->capture_serial == 1 && state->last_capture_team == DOD_TEAM_ALLIES);
	assert(!Dod_TeamOwnsAllObjectives(DOD_TEAM_ALLIES));
	assert(Dod_TickObjective(2, 1, 0, 1.0f));
	assert(state->capture_serial == 2 && Dod_TeamOwnsAllObjectives(DOD_TEAM_ALLIES));
	assert(state->phase == DOD_ROUND_ACTIVE);	/* the core did not end it */

	/* The map's dod_score_ent: points, then a round end with its own restart delay. */
	Dod_AddTeamScore(DOD_TEAM_ALLIES, 50);
	assert(state->allies_score >= 50);
	Dod_EndRoundDelayed(DOD_TEAM_ALLIES, 5.0f);
	assert(state->phase == DOD_ROUND_INTERMISSION && state->winner == DOD_TEAM_ALLIES);
	assert(state->rounds_won[DOD_TEAM_ALLIES] == 1);
	assert(state->intermission_remaining == 5.0f);
	Dod_AddTeamScore(DOD_TEAM_AXIS, 50);	/* nothing scores between rounds */
	assert(state->axis_score == 0);
	for (i = 0; i < 4; ++i)
		Dod_Think(1.0f);
	assert(state->phase == DOD_ROUND_INTERMISSION && state->restart_serial == 0);
	Dod_Think(1.5f);
	assert(state->phase == DOD_ROUND_ACTIVE && state->restart_serial == 1);
	assert(Dod_TeamOwnsAllObjectives(DOD_TEAM_AXIS));	/* points back to their default owners */
	assert(state->allies_score == 0 && state->axis_score == 0);
	assert(state->master_driven);	/* the flag survives the restart */

	/* A second delayed end uses the default intermission when no delay is given. */
	Dod_EndRoundDelayed(DOD_TEAM_AXIS, 0.0f);
	assert(state->intermission_remaining == DOD_INTERMISSION_SECONDS);
	Dod_GameInit();

	/* Timer-driven: the clock running out reports a timeout instead of ending the round. */
	state = Dod_GetState();
	Dod_StartRound(600.0f);
	Dod_SetRoundClock(720.0f);
	assert(state->round_time_remaining == 720.0f);
	Dod_SetTimerDriven(1);
	Dod_Think(719.0f);
	assert(state->round_time_remaining > 0.0f && state->timeout_serial == 0);
	Dod_Think(2.0f);
	assert(state->phase == DOD_ROUND_ACTIVE && state->timeout_serial == 1 && state->round_time_remaining == 0.0f);
	Dod_Think(1.0f);
	assert(state->timeout_serial == 1);	/* reported once */
	Dod_EndRoundDelayed(DOD_TEAM_AXIS, 10.0f);
	assert(state->winner == DOD_TEAM_AXIS && state->intermission_remaining == 10.0f);
	Dod_Think(10.5f);
	assert(state->phase == DOD_ROUND_ACTIVE && state->round_time_remaining == 720.0f);	/* clock restarts */
	Dod_SetTimerDriven(0);
	Dod_Think(721.0f);
	assert(state->phase == DOD_ROUND_INTERMISSION);	/* without a timer the core ends it by score */
	Dod_GameInit();
}

static void test_hurt_zone_team_flags(void)
{
	/* flash's spawn-protection zones: 64 keeps the Axis out of the Allies' base, 128 the Allies out of the Axis'.
	 * Before the filter existed every zone killed everyone, including players spawning in their own base. */
	assert(Dod_HurtAppliesToTeam(DOD_HURT_AXIS_ONLY, DOD_TEAM_AXIS));
	assert(!Dod_HurtAppliesToTeam(DOD_HURT_AXIS_ONLY, DOD_TEAM_ALLIES));
	assert(Dod_HurtAppliesToTeam(DOD_HURT_ALLIES_ONLY, DOD_TEAM_ALLIES));
	assert(!Dod_HurtAppliesToTeam(DOD_HURT_ALLIES_ONLY, DOD_TEAM_AXIS));
	/* the stock flags around them do not matter; no team flag, or both, hurts everyone */
	assert(Dod_HurtAppliesToTeam(0, DOD_TEAM_ALLIES) && Dod_HurtAppliesToTeam(0, DOD_TEAM_AXIS));
	assert(Dod_HurtAppliesToTeam(1 | 2 | 16, DOD_TEAM_ALLIES));
	assert(Dod_HurtAppliesToTeam(DOD_HURT_AXIS_ONLY | DOD_HURT_ALLIES_ONLY, DOD_TEAM_AXIS));
	assert(Dod_HurtAppliesToTeam(DOD_HURT_AXIS_ONLY | 16, DOD_TEAM_AXIS) && !Dod_HurtAppliesToTeam(DOD_HURT_ALLIES_ONLY | 1, DOD_TEAM_AXIS));
	/* avalanche's zone (129 = target-only + Allies-only) */
	assert(Dod_HurtAppliesToTeam(129, DOD_TEAM_ALLIES) && !Dod_HurtAppliesToTeam(129, DOD_TEAM_AXIS));
	/* a spectator or unassigned player is not hurt by a team zone, but is by a plain one */
	assert(!Dod_HurtAppliesToTeam(DOD_HURT_AXIS_ONLY, DOD_TEAM_SPECTATOR) && !Dod_HurtAppliesToTeam(DOD_HURT_ALLIES_ONLY, DOD_TEAM_UNASSIGNED));
	assert(Dod_HurtAppliesToTeam(0, DOD_TEAM_SPECTATOR));
}

static void test_view_animations(void)
{
	int id;
	/* every gun's view model is in the table */
	for (id = DOD_WEAPON_NONE + 1; id < DOD_WEAPON_COUNT; ++id) {
		const DodWeaponDef *def = Dod_GetWeaponDef((DodWeaponId)id);
		assert(def && Dod_ViewAnim(def->view_model, DOD_ANIM_IDLE, 0) >= 0);
		assert(Dod_ViewAnim(def->view_model, DOD_ANIM_FIRE, 0) >= 0 && Dod_ViewAnim(def->view_model, DOD_ANIM_RELOAD, 0) >= 0);
	}
	/* the Garand: shoot1-3 are 1-3, shoot_empty 4, reload 5, draw 6, empty_idle 7 (its reload was sent as 4 and its draw as 5) */
	assert(Dod_RemapAnim("weapon_dod_garand", DOD_GENERIC_ANIM_FIRE1 + 2) == 3);
	assert(Dod_RemapAnim("weapon_dod_garand", DOD_GENERIC_ANIM_FIRE_EMPTY) == 4);
	assert(Dod_RemapAnim("weapon_dod_garand", DOD_GENERIC_ANIM_RELOAD) == 5);
	assert(Dod_RemapAnim("weapon_dod_garand", DOD_GENERIC_ANIM_DRAW) == 6);
	assert(Dod_RemapAnim("weapon_dod_garand", DOD_GENERIC_ANIM_IDLE_EMPTY) == 7);
	/* the Thompson, MP40 and StG44: sequence 1 is the reload, 2 the draw, shoot1/2 are 3/4 (two fire variants wrap) */
	assert(Dod_RemapAnim("weapon_dod_thompson", DOD_GENERIC_ANIM_FIRE1) == 3 && Dod_RemapAnim("weapon_dod_thompson", DOD_GENERIC_ANIM_FIRE1 + 1) == 4);
	assert(Dod_RemapAnim("weapon_dod_thompson", DOD_GENERIC_ANIM_FIRE1 + 2) == 3);
	assert(Dod_RemapAnim("weapon_dod_mp40", DOD_GENERIC_ANIM_RELOAD) == 1 && Dod_RemapAnim("weapon_dod_mp40", DOD_GENERIC_ANIM_DRAW) == 2);
	assert(Dod_RemapAnim("weapon_dod_stg44", DOD_GENERIC_ANIM_IDLE_EMPTY) == 5);
	/* bolt actions have one shoot, no empty variants: they fall back */
	assert(Dod_RemapAnim("weapon_dod_k98", DOD_GENERIC_ANIM_FIRE1 + 1) == 1 && Dod_RemapAnim("weapon_dod_k98", DOD_GENERIC_ANIM_FIRE_EMPTY) == 1);
	assert(Dod_RemapAnim("weapon_dod_k98", DOD_GENERIC_ANIM_RELOAD) == 2 && Dod_RemapAnim("weapon_dod_k98", DOD_GENERIC_ANIM_IDLE_EMPTY) == 0);
	assert(Dod_RemapAnim("weapon_dod_spring", DOD_GENERIC_ANIM_RELOAD) == 3 && Dod_RemapAnim("weapon_dod_spring", DOD_GENERIC_ANIM_DRAW) == 4);
	/* pistols */
	assert(Dod_RemapAnim("weapon_dod_colt", DOD_GENERIC_ANIM_FIRE_EMPTY) == 6 && Dod_RemapAnim("weapon_dod_colt", DOD_GENERIC_ANIM_DRAW) == 5);
	assert(Dod_RemapAnim("weapon_dod_luger", DOD_GENERIC_ANIM_FIRE_EMPTY) == 4 && Dod_RemapAnim("weapon_dod_luger", DOD_GENERIC_ANIM_RELOAD) == 6);
	/* launchers and the belt-fed guns */
	assert(Dod_RemapAnim("weapon_dod_bazooka", DOD_GENERIC_ANIM_FIRE1) == 3 && Dod_RemapAnim("weapon_dod_bazooka", DOD_GENERIC_ANIM_RELOAD) == 7);
	assert(Dod_RemapAnim("weapon_dod_pschreck", DOD_GENERIC_ANIM_DRAW) == 1);
	assert(Dod_RemapAnim("weapon_dod_mg42", DOD_GENERIC_ANIM_FIRE1) == 40 && Dod_RemapAnim("weapon_dod_mg42", DOD_GENERIC_ANIM_RELOAD) == 58);
	assert(Dod_RemapAnim("weapon_dod_30cal", DOD_GENERIC_ANIM_IDLE_EMPTY) == 9 && Dod_RemapAnim("weapon_dod_30cal", DOD_GENERIC_ANIM_DRAW) == 0);
	/* the HL1 crowbar wearing the knife: stock draw is 1 (a slash in the knife model), the knife draws with 3 */
	assert(Dod_RemapAnim("weapon_crowbar", 1) == 3 && Dod_RemapAnim("weapon_crowbar", 2) == 0);
	assert(Dod_RemapAnim("weapon_crowbar", 4) == 1 && Dod_RemapAnim("weapon_crowbar", 6) == 2 && Dod_RemapAnim("weapon_crowbar", 8) == 1);
	/* the HL1 hand grenade wearing the DoD grenades: stock draw is 7 (an exploding pin pull in the model), it draws with 1 */
	assert(Dod_RemapAnim("weapon_handgrenade", 7) == 1 && Dod_RemapAnim("weapon_handgrenade", 2) == 2);
	assert(Dod_RemapAnim("weapon_handgrenade", 4) == 4 && Dod_RemapAnim("weapon_handgrenade", 6) == 3 && Dod_RemapAnim("weapon_handgrenade", 1) == 0);
	/* unknown weapons and numbers are left alone */
	assert(Dod_RemapAnim("weapon_nothing", 5) == 5 && Dod_RemapAnim(0, 5) == 5 && Dod_RemapAnim("weapon_dod_garand", 42) == 42);
	assert(Dod_ViewAnim("models/v_nothing.mdl", DOD_ANIM_IDLE, 0) == -1 && Dod_ViewAnim(0, DOD_ANIM_IDLE, 0) == -1);
}

static void test_para_kit(void)
{
	int id, ok = 1;
	Dod_SetAlliesCountry(DOD_COUNTRY_US);
	Dod_SetParatroopers(0, 0);
	assert(Dod_ParatroopersForMap("dod_escape") == 3 && Dod_ParatroopersForMap("dod_jagd") == 2);
	assert(Dod_ParatroopersForMap("dod_sturm") == 1 && Dod_ParatroopersForMap("dod_charlie") == 0 && Dod_ParatroopersForMap(0) == 0);
	assert(!Dod_TeamIsParatroopers(DOD_TEAM_ALLIES) && !Dod_TeamIsParatroopers(DOD_TEAM_AXIS));
	assert(strcmp(Dod_GetWeaponDef(DOD_WEAPON_K98)->title, "Kar98k") == 0);
	Dod_SetParatroopers(1, 1);
	assert(Dod_TeamIsParatroopers(DOD_TEAM_ALLIES) && Dod_TeamIsParatroopers(DOD_TEAM_AXIS));
	assert(strcmp(Dod_GetWeaponDef(DOD_WEAPON_K98)->title, "FG42") == 0);
	assert(Dod_GetWeaponDef(DOD_WEAPON_K98)->mode == DOD_FIRE_AUTO && Dod_GetWeaponDef(DOD_WEAPON_K98)->clip == 20);
	assert(strcmp(Dod_GetWeaponDef(DOD_WEAPON_GARAND)->title, "M1 Carbine") == 0);
	assert(strcmp(Dod_GetWeaponDef(DOD_WEAPON_THOMPSON)->title, "Grease Gun") == 0);
	assert(strcmp(Dod_ClassWeaponDef(DOD_TEAM_AXIS, DOD_CLASS_RIFLEMAN)->title, "FG42") == 0);
	/* the para guns stand in for the stock ones: same ids, class names, slots, ammo pools and team, with animations */
	for (id = DOD_WEAPON_NONE + 1; id < DOD_WEAPON_COUNT; ++id) {
		const DodWeaponDef *para = Dod_GetWeaponDef((DodWeaponId)id);
		const DodWeaponDef *stock;
		Dod_SetParatroopers(0, 0);
		stock = Dod_GetWeaponDef((DodWeaponId)id);
		if (stock != para) {
			ok &= para->id == stock->id && para->hl_id == stock->hl_id && para->team == stock->team &&
				strcmp(para->classname, stock->classname) == 0 && strcmp(para->ammo, stock->ammo) == 0 &&
				para->slot == stock->slot && para->position == stock->position &&
				Dod_ViewAnim(para->view_model, DOD_ANIM_IDLE, 0) >= 0 && Dod_ViewAnim(para->view_model, DOD_ANIM_FIRE, 0) >= 0;
		}
		Dod_SetParatroopers(1, 1);
	}
	assert(ok);
	/* the Axis only: the Allies keep the Garand */
	Dod_SetParatroopers(0, 1);
	assert(strcmp(Dod_GetWeaponDef(DOD_WEAPON_GARAND)->title, "M1 Garand") == 0 && strcmp(Dod_GetWeaponDef(DOD_WEAPON_K98)->title, "FG42") == 0);
	/* British Allies keep the British kit even if the map said paras */
	Dod_SetParatroopers(1, 0);
	Dod_SetAlliesCountry(DOD_COUNTRY_BRITISH);
	assert(strcmp(Dod_GetWeaponDef(DOD_WEAPON_GARAND)->title, "Lee-Enfield") == 0);
	Dod_SetAlliesCountry(DOD_COUNTRY_US);
	Dod_SetParatroopers(0, 0);
}

static void test_british_kit(void)
{
	DodLoadout load;
	DodGrenadeDef grenade;
	const char *view, *player_model;
	int id, team_ok = 1;
	/* American by default; only the retail British maps switch */
	Dod_SetAlliesCountry(DOD_COUNTRY_US);
	assert(Dod_GetAlliesCountry() == DOD_COUNTRY_US);
	assert(Dod_AlliesCountryForMap("dod_charlie") == DOD_COUNTRY_US && Dod_AlliesCountryForMap("dod_avalanche") == DOD_COUNTRY_US);
	assert(Dod_AlliesCountryForMap("dod_flash") == DOD_COUNTRY_BRITISH && Dod_AlliesCountryForMap("dod_caen") == DOD_COUNTRY_BRITISH);
	assert(Dod_AlliesCountryForMap("dod_jagd") == DOD_COUNTRY_BRITISH && Dod_AlliesCountryForMap(0) == DOD_COUNTRY_US);
	assert(Dod_AlliesCountryForMap("custom_map") == DOD_COUNTRY_US);
	assert(strcmp(Dod_GetWeaponDef(DOD_WEAPON_GARAND)->title, "M1 Garand") == 0);
	/* the US BAR wears the BAR's models now (they were the British Bren's) */
	assert(strcmp(Dod_GetWeaponDef(DOD_WEAPON_BAR)->view_model, "models/v_bar.mdl") == 0);

	Dod_SetAlliesCountry(DOD_COUNTRY_BRITISH);
	/* the British guns stand in for the American ones: same ids, class names, slots, ammo pools and team */
	for (id = DOD_WEAPON_NONE + 1; id < DOD_WEAPON_COUNT; ++id) {
		const DodWeaponDef *def = Dod_GetWeaponDef((DodWeaponId)id);
		Dod_SetAlliesCountry(DOD_COUNTRY_US);
		{
			const DodWeaponDef *us = Dod_GetWeaponDef((DodWeaponId)id);
			if (def != us) {
				const DodWeaponDef *brit;
				Dod_SetAlliesCountry(DOD_COUNTRY_BRITISH);
				brit = Dod_GetWeaponDef((DodWeaponId)id);
				team_ok &= brit->id == us->id && brit->hl_id == us->hl_id && brit->team == DOD_TEAM_ALLIES && us->team == DOD_TEAM_ALLIES &&
					strcmp(brit->classname, us->classname) == 0 && strcmp(brit->ammo, us->ammo) == 0 && brit->slot == us->slot &&
					brit->position == us->position && Dod_ViewAnim(brit->view_model, DOD_ANIM_IDLE, 0) >= 0 &&
					Dod_ViewAnim(brit->view_model, DOD_ANIM_FIRE, 0) >= 0;
			}
		}
		Dod_SetAlliesCountry(DOD_COUNTRY_BRITISH);
	}
	assert(team_ok);
	assert(strcmp(Dod_ClassWeaponDef(DOD_TEAM_ALLIES, DOD_CLASS_RIFLEMAN)->title, "Lee-Enfield") == 0);
	assert(Dod_ClassWeaponDef(DOD_TEAM_ALLIES, DOD_CLASS_RIFLEMAN)->mode == DOD_FIRE_BOLT && Dod_ClassWeaponDef(DOD_TEAM_ALLIES, DOD_CLASS_RIFLEMAN)->clip == 10);
	assert(strcmp(Dod_ClassWeaponDef(DOD_TEAM_ALLIES, DOD_CLASS_ASSAULT)->title, "Sten") == 0);
	assert(strcmp(Dod_ClassWeaponDef(DOD_TEAM_ALLIES, DOD_CLASS_SUPPORT)->title, "Bren") == 0);
	assert(strcmp(Dod_ClassWeaponDef(DOD_TEAM_ALLIES, DOD_CLASS_SNIPER)->title, "Scoped Lee-Enfield") == 0);
	assert(Dod_ClassWeaponDef(DOD_TEAM_ALLIES, DOD_CLASS_SNIPER)->scope_fov > 0.0f);
	assert(strcmp(Dod_ClassWeaponDef(DOD_TEAM_ALLIES, DOD_CLASS_ROCKET)->title, "PIAT") == 0);
	assert(strcmp(Dod_ClassSidearmDef(DOD_TEAM_ALLIES, DOD_CLASS_SUPPORT)->title, "Webley") == 0);
	assert(strcmp(Dod_ClassWeaponDef(DOD_TEAM_ALLIES, DOD_CLASS_MG)->title, "30cal") == 0);	/* no British replacement on disc */
	/* the Axis are untouched */
	assert(strcmp(Dod_ClassWeaponDef(DOD_TEAM_AXIS, DOD_CLASS_RIFLEMAN)->title, "Kar98k") == 0);
	assert(strcmp(Dod_ClassSidearmDef(DOD_TEAM_AXIS, DOD_CLASS_SUPPORT)->title, "Luger") == 0);
	/* names, loadouts, models */
	assert(strcmp(Dod_TeamClassWeapon(DOD_TEAM_ALLIES, DOD_CLASS_ASSAULT), "Sten") == 0 && strcmp(Dod_TeamClassWeapon(DOD_TEAM_AXIS, DOD_CLASS_ASSAULT), "MP40") == 0);
	assert(Dod_GetLoadout(DOD_TEAM_ALLIES, DOD_CLASS_RIFLEMAN, &load) && strcmp(load.weapon, "Lee-Enfield") == 0 && load.clip_size == 10);
	assert(Dod_FindWeaponDef("weapon_dod_garand") == Dod_GetWeaponDef(DOD_WEAPON_GARAND) && strcmp(Dod_FindWeaponDef("weapon_dod_garand")->title, "Lee-Enfield") == 0);
	assert(Dod_GetGrenade(DOD_TEAM_ALLIES, &grenade) && strcmp(grenade.view_model, "models/v_mills.mdl") == 0 && strcmp(grenade.world_model, "models/w_mills.mdl") == 0);
	assert(Dod_GetGrenade(DOD_TEAM_AXIS, &grenade) && strcmp(grenade.view_model, "models/v_stick.mdl") == 0);
	/* the animation remap follows the British models: the Lee-Enfield's reload is 2, the Sten's draw 2, the PIAT's launch 6 */
	assert(Dod_RemapAnim("weapon_dod_garand", DOD_GENERIC_ANIM_RELOAD) == 2 && Dod_RemapAnim("weapon_dod_garand", DOD_GENERIC_ANIM_DRAW) == 3);
	assert(Dod_RemapAnim("weapon_dod_thompson", DOD_GENERIC_ANIM_DRAW) == 2 && Dod_RemapAnim("weapon_dod_bazooka", DOD_GENERIC_ANIM_FIRE1) == 6);

	/* back to American for the tests after this one */
	Dod_SetAlliesCountry(DOD_COUNTRY_US);
	assert(strcmp(Dod_ClassWeaponDef(DOD_TEAM_ALLIES, DOD_CLASS_RIFLEMAN)->title, "M1 Garand") == 0);
	assert(Dod_GetGrenade(DOD_TEAM_ALLIES, &grenade) && strcmp(grenade.view_model, "models/v_grenade.mdl") == 0);
	(void)view; (void)player_model;
}

int main(void)
{
	test_players_and_weapon_models();
	test_retail_class_names();
	test_status_line();
	test_player_line();
	test_kill_labels();
	test_class_limits();
	test_stamina();
	test_voice();
	test_scoreboard();
	test_team_points();
	test_player_body();
	test_loadouts();
	test_contested_capture_flip_and_round_win();
	test_kill_scoring_and_timed_tie();
	test_grenades();
	test_weapon_table();
	test_match_loop();
	test_map_driven_round_flow();
	test_hurt_zone_team_flags();
	test_view_animations();
	test_british_kit();
	test_para_kit();
	return 0;
}
