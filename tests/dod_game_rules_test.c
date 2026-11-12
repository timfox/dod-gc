#include "dod_game.h"

#include <assert.h>
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

int main(void)
{
	test_players_and_weapon_models();
	test_retail_class_names();
	test_status_line();
	test_contested_capture_flip_and_round_win();
	test_kill_scoring_and_timed_tie();
	return 0;
}
