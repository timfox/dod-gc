/* Server-side Day of Defeat rules state for the GameCube HL1SDK port. */
#include "dod_game.h"
#include "dod_weapons.h"

#include <ctype.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#define DOD_CAPTURE_SECONDS_DEFAULT 10.0f

static DodGameRulesState g_dod_state;
static DodPlayerState g_dod_players[DOD_MAX_PLAYERS + 1];
static DodObjectiveState g_dod_objectives[DOD_MAX_OBJECTIVES];
static float g_score_remainder[4];
static float g_give_delay = 20.0f;	/* seconds between timed point awards */
static float g_give_timer;
static float g_round_seconds;
static int g_rounds_to_win;
static int g_dod_active;

static int dod_valid_player(int player_index)
{
	return player_index > 0 && player_index <= DOD_MAX_PLAYERS;
}

static int dod_valid_team(DodTeam team)
{
	return team == DOD_TEAM_ALLIES || team == DOD_TEAM_AXIS;
}

static DodObjectiveState *dod_find_objective(int id)
{
	int i;
	for (i = 0; i < DOD_MAX_OBJECTIVES; ++i) {
		if (g_dod_objectives[i].active && g_dod_objectives[i].id == id)
			return &g_dod_objectives[i];
	}
	return 0;
}

static void dod_recount_players(void)
{
	int i;
	g_dod_state.connected_players[DOD_TEAM_UNASSIGNED] = 0;
	g_dod_state.connected_players[DOD_TEAM_ALLIES] = 0;
	g_dod_state.connected_players[DOD_TEAM_AXIS] = 0;
	g_dod_state.connected_players[DOD_TEAM_SPECTATOR] = 0;
	for (i = 1; i <= DOD_MAX_PLAYERS; ++i) {
		if (g_dod_players[i].connected && g_dod_players[i].team >= DOD_TEAM_UNASSIGNED &&
			g_dod_players[i].team <= DOD_TEAM_SPECTATOR)
			++g_dod_state.connected_players[g_dod_players[i].team];
	}
}

static void dod_recount_objectives(void)
{
	int i;
	g_dod_state.controlled_objectives[DOD_TEAM_UNASSIGNED] = 0;
	g_dod_state.controlled_objectives[DOD_TEAM_ALLIES] = 0;
	g_dod_state.controlled_objectives[DOD_TEAM_AXIS] = 0;
	g_dod_state.objective_count = 0;
	for (i = 0; i < DOD_MAX_OBJECTIVES; ++i) {
		DodObjectiveState *objective = &g_dod_objectives[i];
		if (objective->active) {
			++g_dod_state.objective_count;
			if (dod_valid_team(objective->owner))
				++g_dod_state.controlled_objectives[objective->owner];
		}
	}
}

static void dod_add_team_score(DodTeam team, float amount)
{
	float total;
	int whole;
	/* Nothing scores between rounds. */
	if (!dod_valid_team(team) || amount <= 0.0f || g_dod_state.phase != DOD_ROUND_ACTIVE)
		return;
	total = g_score_remainder[team] + amount;
	whole = (int)total;
	g_score_remainder[team] = total - (float)whole;
	if (team == DOD_TEAM_ALLIES)
		g_dod_state.allies_score += whole;
	else
		g_dod_state.axis_score += whole;
}

static int g_grenades_live;
static int g_fx_count;

void Dod_GameInit(void)
{
	memset(&g_dod_state, 0, sizeof(g_dod_state));
	memset(g_dod_players, 0, sizeof(g_dod_players));
	memset(g_dod_objectives, 0, sizeof(g_dod_objectives));
	memset(g_score_remainder, 0, sizeof(g_score_remainder));
	g_give_timer = 0.0f;
	g_give_delay = 20.0f;
	g_dod_state.phase = DOD_ROUND_WAITING;
	g_dod_state.winner = DOD_TEAM_UNASSIGNED;
	g_round_seconds = 0.0f;
	g_rounds_to_win = 0;
	g_grenades_live = 0;
	g_fx_count = 0;
	g_dod_active = 1;
}

void Dod_GameShutdown(void)
{
	g_dod_state.round_active = 0;
	g_dod_state.phase = DOD_ROUND_WAITING;
	g_dod_state.winner = DOD_TEAM_UNASSIGNED;
	g_dod_state.round_time_remaining = 0.0f;
	g_dod_state.intermission_remaining = 0.0f;
	g_dod_active = 0;
	memset(g_dod_players, 0, sizeof(g_dod_players));
	memset(g_dod_objectives, 0, sizeof(g_dod_objectives));
	dod_recount_players();
	dod_recount_objectives();
}

int Dod_GameIsActive(void)
{
	return g_dod_active;
}

void Dod_SetRoundsToWin(int rounds)
{
	g_rounds_to_win = rounds > 0 ? rounds : 0;
	g_dod_state.rounds_to_win = g_rounds_to_win;
}

void Dod_ResetPlayerStats(void)
{
	int i;
	for (i = 1; i <= DOD_MAX_PLAYERS; ++i) {
		g_dod_players[i].kills = 0;
		g_dod_players[i].deaths = 0;
		g_dod_players[i].objective_score = 0;
		g_dod_players[i].teamkills = 0;
		g_dod_players[i].captures = 0;
		g_dod_players[i].score = 0;
	}
}

void Dod_StartRound(float duration_seconds)
{
	g_round_seconds = duration_seconds > 0.0f ? duration_seconds : 0.0f;
	++g_dod_state.round_number;
	g_dod_state.phase = DOD_ROUND_ACTIVE;
	g_dod_state.round_active = 1;
	g_dod_state.winner = DOD_TEAM_UNASSIGNED;
	g_dod_state.allies_score = 0;
	g_dod_state.axis_score = 0;
	g_dod_state.round_time_remaining = duration_seconds > 0.0f ? duration_seconds : 0.0f;
	g_dod_state.intermission_remaining = 0.0f;
	memset(g_score_remainder, 0, sizeof(g_score_remainder));
	g_give_timer = 0.0f;
}

void Dod_EndRoundDelayed(DodTeam winner, float restart_delay_seconds)
{
	if (g_dod_state.phase != DOD_ROUND_ACTIVE)
		return;
	if (!dod_valid_team(winner)) {
		if (g_dod_state.allies_score > g_dod_state.axis_score)
			winner = DOD_TEAM_ALLIES;
		else if (g_dod_state.axis_score > g_dod_state.allies_score)
			winner = DOD_TEAM_AXIS;
		else
			winner = DOD_TEAM_UNASSIGNED;
	}
	g_dod_state.winner = winner;
	if (dod_valid_team(winner))
		++g_dod_state.rounds_won[winner];
	g_dod_state.phase = DOD_ROUND_INTERMISSION;
	g_dod_state.round_active = 0;
	g_dod_state.round_time_remaining = 0.0f;
	g_dod_state.intermission_remaining = restart_delay_seconds > 0.0f ? restart_delay_seconds : DOD_INTERMISSION_SECONDS;
}

void Dod_EndRound(DodTeam winner)
{
	Dod_EndRoundDelayed(winner, 0.0f);
}

void Dod_AddTeamScore(DodTeam team, int points)
{
	if (points > 0)
		dod_add_team_score(team, (float)points);
}

void Dod_SetMasterDriven(int on)
{
	g_dod_state.master_driven = on ? 1 : 0;
}

void Dod_SetTimerDriven(int on)
{
	g_dod_state.timer_driven = on ? 1 : 0;
}

void Dod_SetRoundClock(float seconds)
{
	g_round_seconds = seconds > 0.0f ? seconds : 0.0f;
	if (g_dod_state.phase == DOD_ROUND_ACTIVE)
		g_dod_state.round_time_remaining = g_round_seconds;
}

int Dod_TeamOwnsAllObjectives(DodTeam team)
{
	return dod_valid_team(team) && g_dod_state.objective_count > 0 &&
		g_dod_state.controlled_objectives[team] == g_dod_state.objective_count;
}

/* The intermission ran out: close the match if a team has won enough rounds,
 * put every control point back to its starting owner, and start the next round. */
static void dod_next_round(void)
{
	int i, team;
	if (g_rounds_to_win > 0) {
		for (team = DOD_TEAM_ALLIES; team <= DOD_TEAM_AXIS; ++team) {
			if (g_dod_state.rounds_won[team] >= g_rounds_to_win) {
				g_dod_state.match_winner = (DodTeam)team;
				++g_dod_state.match_serial;
				memset(g_dod_state.rounds_won, 0, sizeof(g_dod_state.rounds_won));
				g_dod_state.round_number = 0;
				Dod_ResetPlayerStats();
				break;
			}
		}
	}
	for (i = 0; i < DOD_MAX_OBJECTIVES; ++i) {
		DodObjectiveState *objective = &g_dod_objectives[i];
		if (!objective->active)
			continue;
		objective->owner = objective->default_owner;
		objective->capturing_team = DOD_TEAM_UNASSIGNED;
		objective->capture_progress = 0.0f;
	}
	dod_recount_objectives();
	Dod_StartRound(g_round_seconds);
	++g_dod_state.restart_serial;
}

void Dod_Think(float frametime)
{
	int i;
	if (frametime <= 0.0f)
		return;
	if (g_dod_state.phase == DOD_ROUND_ACTIVE) {
		if (g_dod_state.round_time_remaining > 0.0f) {
			g_dod_state.round_time_remaining -= frametime;
			if (g_dod_state.round_time_remaining <= 0.0f) {
				g_dod_state.round_time_remaining = 0.0f;
				if (g_dod_state.timer_driven)
					++g_dod_state.timeout_serial;	/* the map's dod_round_timer fires its target */
				else
					Dod_EndRound(DOD_TEAM_UNASSIGNED);
			}
		}
		for (i = 0; i < DOD_MAX_OBJECTIVES; ++i) {
			DodObjectiveState *objective = &g_dod_objectives[i];
			if (objective->active && dod_valid_team(objective->owner))
				dod_add_team_score(objective->owner,
					objective->score_per_minute * frametime / 60.0f);
		}
		/* the timed award of retail: every give delay each owned point gives its owner its point value */
		g_give_timer += frametime;
		if (g_give_delay > 0.0f && g_give_timer >= g_give_delay) {
			g_give_timer -= g_give_delay;
			for (i = 0; i < DOD_MAX_OBJECTIVES; ++i) {
				const DodObjectiveState *objective = &g_dod_objectives[i];
				if (objective->active && dod_valid_team(objective->owner) && objective->time_points > 0)
					dod_add_team_score(objective->owner, (float)objective->time_points);
			}
		}
	} else if (g_dod_state.phase == DOD_ROUND_INTERMISSION) {
		g_dod_state.intermission_remaining -= frametime;
		if (g_dod_state.intermission_remaining <= 0.0f) {
			g_dod_state.intermission_remaining = 0.0f;
			dod_next_round();
		}
	}
}

const DodGameRulesState *Dod_GetState(void)
{
	return &g_dod_state;
}

void Dod_StaminaReset(int player_index)
{
	DodPlayerState *player = dod_valid_player(player_index) ? &g_dod_players[player_index] : 0;
	if (!player)
		return;
	player->stamina = DOD_STAMINA_MAX;
	player->sprinting = 0;
	player->sprint_locked = 0;
	player->forward_down = 0;
	player->forward_pressed_at = -10.0f;
	player->forward_released_at = -10.0f;
	player->stamina_rest_until = 0.0f;
}

float Dod_StaminaThink(int player_index, float now, float dt, int forward, int jumped, int moving)
{
	DodPlayerState *player = dod_valid_player(player_index) ? &g_dod_players[player_index] : 0;
	if (!player || !player->connected)
		return DOD_RUN_SPEED;
	if (dt < 0.0f)
		dt = 0.0f;
	/* the double tap: a second press within the window of the first release starts the sprint */
	if (forward && !player->forward_down) {
		if (now - player->forward_released_at <= DOD_DOUBLE_TAP_SECONDS && player->forward_released_at > -5.0f &&
			!player->sprint_locked && player->stamina >= 15.0f)
		{
			player->sprinting = 1;
			player->sprint_started_at = now;
		}
		player->forward_pressed_at = now;
	} else if (!forward && player->forward_down) {
		player->forward_released_at = now;
		player->sprinting = 0;
	}
	player->forward_down = forward;
	/* stopped against a wall: not sprinting (the speed takes a moment to build after the tap) */
	if (player->sprinting && !moving && now - player->sprint_started_at > 0.4f)
		player->sprinting = 0;
	if (player->sprinting) {
		player->stamina -= DOD_STAMINA_DRAIN * dt;
		player->stamina_rest_until = now + DOD_STAMINA_REST;
		if (player->stamina <= 0.0f) {
			player->stamina = 0.0f;
			player->sprinting = 0;
			player->sprint_locked = 1;
		}
	}
	if (jumped && player->stamina > 0.0f) {
		player->stamina -= DOD_STAMINA_JUMP;
		if (player->stamina < 0.0f)
			player->stamina = 0.0f;
		player->stamina_rest_until = now + DOD_STAMINA_REST;
	}
	if (!player->sprinting && now >= player->stamina_rest_until && player->stamina < DOD_STAMINA_MAX) {
		player->stamina += DOD_STAMINA_REGEN * dt;
		if (player->stamina > DOD_STAMINA_MAX)
			player->stamina = DOD_STAMINA_MAX;
	}
	if (player->sprint_locked && player->stamina >= DOD_STAMINA_RESUME)
		player->sprint_locked = 0;
	return player->sprinting ? DOD_SPRINT_SPEED : DOD_RUN_SPEED;
}

int Dod_PlayerStamina(int player_index)
{
	const DodPlayerState *player = dod_valid_player(player_index) ? &g_dod_players[player_index] : 0;
	return player && player->connected ? (int)(player->stamina + 0.5f) : 0;
}

int Dod_PlayerSprinting(int player_index)
{
	const DodPlayerState *player = dod_valid_player(player_index) ? &g_dod_players[player_index] : 0;
	return player && player->connected && player->sprinting;
}

int Dod_PlayerConnect(int player_index)
{
	if (!dod_valid_player(player_index))
		return 0;
	memset(&g_dod_players[player_index], 0, sizeof(g_dod_players[player_index]));
	g_dod_players[player_index].connected = 1;
	g_dod_players[player_index].team = DOD_TEAM_UNASSIGNED;
	Dod_StaminaReset(player_index);
	dod_recount_players();
	return 1;
}

void Dod_PlayerDisconnect(int player_index)
{
	if (!dod_valid_player(player_index))
		return;
	memset(&g_dod_players[player_index], 0, sizeof(g_dod_players[player_index]));
	dod_recount_players();
}

int Dod_AssignPlayerTeam(int player_index, DodTeam team)
{
	DodPlayerState *player;
	if (!dod_valid_player(player_index) || team < DOD_TEAM_UNASSIGNED || team > DOD_TEAM_SPECTATOR)
		return 0;
	player = &g_dod_players[player_index];
	if (!player->connected)
		return 0;
	player->team = team;
	if (!dod_valid_team(team))
		player->player_class = DOD_CLASS_NONE;
	dod_recount_players();
	return 1;
}

int Dod_SelectPlayerClass(int player_index, DodClass player_class)
{
	DodPlayerState *player;
	if (!dod_valid_player(player_index) || player_class <= DOD_CLASS_NONE || player_class >= DOD_CLASS_COUNT)
		return 0;
	player = &g_dod_players[player_index];
	if (!player->connected || !dod_valid_team(player->team))
		return 0;
	player->player_class = player_class;
	return 1;
}

void Dod_RecordKill(int attacker_index, int victim_index)
{
	DodPlayerState *attacker = dod_valid_player(attacker_index) ? &g_dod_players[attacker_index] : 0;
	DodPlayerState *victim = dod_valid_player(victim_index) ? &g_dod_players[victim_index] : 0;
	if (victim && victim->connected)
		++victim->deaths;
	if (attacker && attacker->connected && attacker_index != victim_index) {
		if (victim && dod_valid_team(attacker->team) && attacker->team == victim->team) {
			++attacker->teamkills;
			return;
		}
		++attacker->kills;
	}
}

void Dod_RecordCapture(int player_index, int points)
{
	DodPlayerState *player = dod_valid_player(player_index) ? &g_dod_players[player_index] : 0;
	if (player && player->connected) {
		++player->captures;
		player->score += points > 0 ? points : 0;
		player->objective_score = player->score;
	}
}

int Dod_PlayerScore(int player_index)
{
	const DodPlayerState *player = dod_valid_player(player_index) ? &g_dod_players[player_index] : 0;
	return player && player->connected ? player->score : 0;
}

void Dod_SetObjectiveTeamPoints(int id, int team_points, int time_points)
{
	DodObjectiveState *objective = dod_find_objective(id);
	if (objective) {
		objective->team_points = team_points > 0 ? team_points : 0;
		objective->time_points = time_points > 0 ? time_points : 0;
	}
}

void Dod_SetPointGiveDelay(float seconds)
{
	g_give_delay = seconds > 0.0f ? seconds : 0.0f;
}

void Dod_SetObjectivePointsForCap(int id, int points)
{
	DodObjectiveState *objective = dod_find_objective(id);
	if (objective)
		objective->points_for_cap = points > 0 ? points : 0;
}

const DodPlayerState *Dod_GetPlayer(int player_index)
{
	return dod_valid_player(player_index) ? &g_dod_players[player_index] : 0;
}

static int dod_name_equal(const char *left, const char *right)
{
	if (!left || !right)
		return 0;
	while (*left && *right) {
		if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
			return 0;
		++left;
		++right;
	}
	return *left == '\0' && *right == '\0';
}

int Dod_TeamFromName(const char *name)
{
	if (dod_name_equal(name, "allies") || dod_name_equal(name, "allied"))
		return DOD_TEAM_ALLIES;
	if (dod_name_equal(name, "axis") || dod_name_equal(name, "german"))
		return DOD_TEAM_AXIS;
	if (dod_name_equal(name, "spectator") || dod_name_equal(name, "spec"))
		return DOD_TEAM_SPECTATOR;
	return DOD_TEAM_UNASSIGNED;
}

const char *Dod_TeamName(DodTeam team)
{
	switch (team) {
	case DOD_TEAM_ALLIES: return "Allies";
	case DOD_TEAM_AXIS: return "Axis";
	case DOD_TEAM_SPECTATOR: return "Spectator";
	default: return "Unassigned";
	}
}

const char *Dod_ClassName(DodClass player_class)
{
	switch (player_class) {
	case DOD_CLASS_RIFLEMAN: return "Rifleman";
	case DOD_CLASS_ASSAULT: return "Assault";
	case DOD_CLASS_SUPPORT: return "Support";
	case DOD_CLASS_SNIPER: return "Sniper";
	case DOD_CLASS_MG: return "Machine Gunner";
	case DOD_CLASS_ROCKET: return "Rocket";
	default: return "None";
	}
}

static const char *const g_allies_class[DOD_CLASS_COUNT] = {
	"None", "Rifleman", "Staff Sergeant", "Master Sergeant",
	"Sniper", "Support Infantry", "Bazooka"
};
static const char *const g_axis_class[DOD_CLASS_COUNT] = {
	"None", "Grenadier", "Stosstruppe", "Unteroffizier",
	"Scharfschuetze", "MG42 Gunner", "Panzerjaeger"
};
static const char *const g_allies_weapon[DOD_CLASS_COUNT] = {
	"None", "M1 Garand", "Thompson", "BAR", "Springfield", "30cal", "Bazooka"
};
static const char *const g_axis_weapon[DOD_CLASS_COUNT] = {
	"None", "Kar98k", "MP40", "StG44", "Scoped Kar98k", "MG42", "Panzerschreck"
};

const char *Dod_TeamClassName(DodTeam team, DodClass player_class)
{
	if (player_class <= DOD_CLASS_NONE || player_class >= DOD_CLASS_COUNT)
		return "None";
	if (team == DOD_TEAM_ALLIES) return g_allies_class[player_class];
	if (team == DOD_TEAM_AXIS) return g_axis_class[player_class];
	return Dod_ClassName(player_class);
}

const char *Dod_TeamClassWeapon(DodTeam team, DodClass player_class)
{
	if (player_class <= DOD_CLASS_NONE || player_class >= DOD_CLASS_COUNT)
		return "None";
	if (team == DOD_TEAM_ALLIES && Dod_GetAlliesCountry() == DOD_COUNTRY_BRITISH) {
		const DodWeaponDef *def = Dod_ClassWeaponDef(team, player_class);	/* the British kit has its own names */
		return def ? def->title : "None";
	}
	if (team == DOD_TEAM_ALLIES) return g_allies_weapon[player_class];
	if (team == DOD_TEAM_AXIS) return g_axis_weapon[player_class];
	return "None";
}

int Dod_ClassHasSidearm(DodClass player_class)
{
	return player_class == DOD_CLASS_SUPPORT || player_class == DOD_CLASS_SNIPER ||
		player_class == DOD_CLASS_MG || player_class == DOD_CLASS_ROCKET;
}

int Dod_GetWeaponModels(int player_index, DodWeaponModelKind kind,
	const char **view_model, const char **player_model)
{
	const DodPlayerState *player = Dod_GetPlayer(player_index);
	if (!g_dod_active || !player || !player->connected || !dod_valid_team(player->team))
		return 0;
	if (view_model) *view_model = 0;
	if (player_model) *player_model = 0;
	if (kind == DOD_MODEL_GRENADE) {
		/* Allies: Mk2 grenade, Axis: stick grenade. */
		DodGrenadeDef def;
		if (!Dod_GetGrenade(player->team, &def))
			return 0;
		if (view_model) *view_model = def.view_model;
		if (player_model) *player_model = def.player_model;
		return 1;
	}
	if (kind == DOD_MODEL_MELEE) {
		/* Retail: Allies carry the American knife (the British the Fairbairn), Axis the spade. */
		const int british = player->team == DOD_TEAM_ALLIES && Dod_GetAlliesCountry() == DOD_COUNTRY_BRITISH;
		if (Dod_TeamIsParatroopers(player->team)) {	/* both paratrooper teams carry the para knife */
			if (view_model) *view_model = "models/v_paraknife.mdl";
			if (player_model) *player_model = "models/p_paraknife.mdl";
			return 1;
		}
		if (view_model) *view_model = player->team == DOD_TEAM_AXIS ? "models/v_spade.mdl" : (british ? "models/v_fairbairn.mdl" : "models/v_amerk.mdl");
		if (player_model) *player_model = player->team == DOD_TEAM_AXIS ? "models/p_spade.mdl" : (british ? "models/p_fairbairn.mdl" : "models/p_amerk.mdl");
		return 1;
	}
	if (kind == DOD_MODEL_PISTOL) {
		const DodWeaponDef *side = Dod_ClassSidearmDef(player->team, player->player_class);
		if (!Dod_ClassHasSidearm(player->player_class) || !side)
			return 0;
		if (view_model) *view_model = side->view_model;
		if (player_model) *player_model = side->player_model;
		return 1;
	}
	{
		/* the class's primary gun: its models come from its definition, so the British kit follows */
		const DodWeaponDef *gun = Dod_ClassWeaponDef(player->team, player->player_class);
		const DodClass c = player->player_class;
		int wanted = 0;
		if (kind == DOD_MODEL_AUTOMATIC)
			wanted = c == DOD_CLASS_ASSAULT || c == DOD_CLASS_MG || c == DOD_CLASS_RIFLEMAN;
		else if (kind == DOD_MODEL_SUPPORT)
			wanted = c == DOD_CLASS_SUPPORT;
		else if (kind == DOD_MODEL_SNIPER)
			wanted = c == DOD_CLASS_SNIPER;
		else if (kind == DOD_MODEL_ROCKET)
			wanted = c == DOD_CLASS_ROCKET;
		if (wanted && gun) {
			if (view_model) *view_model = gun->view_model;
			if (player_model) *player_model = gun->player_model;
			return 1;
		}
	}
	return 0;
}

/* Grenades issued per [team - 1][class]. Clip and reserve come from the
 * weapon table (dod_weapons.c) so the two can never disagree. */
static const int g_loadout_grenades[2][DOD_CLASS_COUNT] = {
	{ 0, 2, 2, 2, 0, 0, 0 },
	{ 0, 2, 2, 2, 0, 0, 0 }
};

int Dod_GetLoadout(DodTeam team, DodClass player_class, DodLoadout *out)
{
	const DodWeaponDef *weapon = Dod_ClassWeaponDef(team, player_class);
	if (!weapon || !out)
		return 0;
	out->weapon = weapon->title;
	out->clip_size = weapon->clip;
	out->reserve_rounds = weapon->reserve;
	out->grenades = g_loadout_grenades[team - 1][player_class];
	return 1;
}

static const DodGrenadeDef g_grenades[2] = {
	{ "models/v_grenade.mdl", "models/p_grenade.mdl", "models/w_grenade.mdl", 5.0f, 100.0f, 250.0f, 800.0f, 4 },
	{ "models/v_stick.mdl", "models/p_stick.mdl", "models/w_stick.mdl", 5.0f, 100.0f, 250.0f, 800.0f, 4 }
};

/* The British Mills bomb: the Allies' grenade on the British maps, same numbers. */
static const DodGrenadeDef g_mills = {
	"models/v_mills.mdl", "models/p_mills.mdl", "models/w_mills.mdl", 5.0f, 100.0f, 250.0f, 800.0f, 4
};

int Dod_GetGrenade(DodTeam team, DodGrenadeDef *out)
{
	if ((team != DOD_TEAM_ALLIES && team != DOD_TEAM_AXIS) || !out)
		return 0;
	*out = team == DOD_TEAM_ALLIES && Dod_GetAlliesCountry() == DOD_COUNTRY_BRITISH ? g_mills : g_grenades[team - 1];
	return 1;
}

float Dod_GrenadeDamageAt(const DodGrenadeDef *def, float distance)
{
	if (!def || def->radius <= 0.0f || def->damage <= 0.0f || distance < 0.0f || distance >= def->radius)
		return 0.0f;
	return def->damage * (1.0f - distance / def->radius);
}

float Dod_GrenadeThrowPitch(float view_pitch)
{
	return view_pitch < 0.0f ? -10.0f + view_pitch * (80.0f / 90.0f)
		: -10.0f + view_pitch * (100.0f / 90.0f);
}

float Dod_GrenadeThrowSpeed(const DodGrenadeDef *def, float view_pitch)
{
	float speed = (90.0f - Dod_GrenadeThrowPitch(view_pitch)) * 6.5f;
	if (!def)
		return 0.0f;
	if (speed < 0.0f)
		speed = 0.0f;
	return speed > def->throw_speed ? def->throw_speed : speed;
}

float Dod_GrenadeFuseLeft(const DodGrenadeDef *def, float held_seconds)
{
	float left;
	if (!def)
		return 0.0f;
	left = def->fuse - (held_seconds > 0.0f ? held_seconds : 0.0f);
	return left > 0.0f ? left : 0.0f;
}

int Dod_GrenadeAcquire(void)
{
	if (g_grenades_live >= DOD_MAX_LIVE_GRENADES)
		return 0;
	g_grenades_live++;
	return 1;
}

void Dod_GrenadeRelease(void)
{
	if (g_grenades_live > 0)
		g_grenades_live--;
}

int Dod_GrenadesLive(void)
{
	return g_grenades_live;
}

#define DOD_FX_HISTORY 8
static float g_fx_times[DOD_FX_HISTORY];

int Dod_FxPlanExplosion(float now)
{
	static const int plans[4] = {
		DOD_FX_FIREBALL | DOD_FX_SMOKE | DOD_FX_SPARKS | DOD_FX_DEBRIS,
		DOD_FX_FIREBALL | DOD_FX_SMOKE | DOD_FX_SPARKS,
		DOD_FX_FIREBALL | DOD_FX_SMOKE,
		DOD_FX_FIREBALL
	};
	int recent = 0, kept = 0, i;
	for (i = 0; i < g_fx_count; i++) {
		if (now - g_fx_times[i] < DOD_FX_WINDOW && now >= g_fx_times[i]) {
			g_fx_times[kept++] = g_fx_times[i];
			recent++;
		}
	}
	if (kept >= DOD_FX_HISTORY)
		kept = DOD_FX_HISTORY - 1;
	g_fx_times[kept++] = now;
	g_fx_count = kept;
	return plans[recent > 3 ? 3 : recent];
}

int Dod_FormatStatus(char *buffer, int size)
{
	const DodGameRulesState *state = Dod_GetState();
	int seconds, written;
	if (!buffer || size <= 0)
		return 0;
	seconds = state->round_time_remaining > 0.0f ? (int)state->round_time_remaining : 0;
	if (state->round_time_remaining > 0.0f)
		written = snprintf(buffer, (size_t)size, "Allies %d  Axis %d | Points %d-%d | %d:%02d",
			state->allies_score, state->axis_score,
			state->controlled_objectives[DOD_TEAM_ALLIES],
			state->controlled_objectives[DOD_TEAM_AXIS], seconds / 60, seconds % 60);
	else
		written = snprintf(buffer, (size_t)size, "Allies %d  Axis %d | Points %d-%d",
			state->allies_score, state->axis_score,
			state->controlled_objectives[DOD_TEAM_ALLIES],
			state->controlled_objectives[DOD_TEAM_AXIS]);
	/* With a match tally, show the round and who has won how many. */
	if (state->rounds_to_win > 0 && written > 0 && written < size)
		written += snprintf(buffer + written, (size_t)(size - written), " | Round %d (%d-%d)",
			state->round_number, state->rounds_won[DOD_TEAM_ALLIES], state->rounds_won[DOD_TEAM_AXIS]);
	if (written < 0)
		buffer[0] = '\0', written = 0;
	else if (written >= size)
		written = size - 1;
	return written;
}

int Dod_FormatPlayerLine(int player_index, const char *name, char *buffer, int size)
{
	const DodPlayerState *player = Dod_GetPlayer(player_index);
	int written;
	if (!buffer || size <= 0)
		return 0;
	buffer[0] = '\0';
	if (!player || !player->connected)
		return 0;
	written = snprintf(buffer, (size_t)size, "%s  %s  %d/%d", Dod_TeamName(player->team),
		name && name[0] ? name : "player", player->kills, player->deaths);
	if (written < 0) {
		buffer[0] = '\0';
		return 0;
	}
	return written >= size ? size - 1 : written;
}

int Dod_PlayerBodyValue(const char *model, DodClass player_class, int player_index)
{
	/* indexes into the model's body (and gear) group, by class: rifleman, assault, support, sniper, mg, rocket */
	static const int us_part[DOD_CLASS_COUNT] = { 0, 0, 1, 2, 4, 3, 2 };	/* light, medium, heavy, sniper, mg, heavy */
	static const int axis_part[DOD_CLASS_COUNT] = { 0, 0, 2, 3, 5, 4, 3 };	/* light, medium, heavy, sniper, mg, heavy */
	static const int brit_gear[DOD_CLASS_COUNT] = { 0, 0, 1, 2, 3, 2, 0 };	/* Enfield, Sten, Bren, sniper, Bren, Enfield */
	int part, gear, helmet, head, who = player_index > 0 ? player_index : 0;
	if (!model || player_class <= DOD_CLASS_NONE || player_class >= DOD_CLASS_COUNT)
		return 0;
	if (!strcmp(model, "us-inf")) {
		part = gear = us_part[player_class];
		helmet = who % 3;	/* helmet1..3 (hair, 3 and up, is for the paratroopers) */
		head = who % 5;
	} else if (!strcmp(model, "axis-inf")) {
		part = gear = axis_part[player_class];
		helmet = 0;
		head = who % 6;
	} else if (!strcmp(model, "brit-inf")) {
		part = 0;
		gear = brit_gear[player_class];
		helmet = player_class == DOD_CLASS_SNIPER ? 1 : 0;
		head = who % 4;
	} else if (!strcmp(model, "us-para")) {
		/* one body; the gear is the weapon: Thompson 0, Springfield 1, Garand 2, MG 3, BAR 4, Carbine 5, Grease Gun 6 */
		static const int gear_of[DOD_CLASS_COUNT] = { 0, 5, 6, 4, 1, 3, 2 };
		part = 0;
		gear = gear_of[player_class];
		helmet = who % 2;
		head = who % 7;
	} else if (!strcmp(model, "axis-para")) {
		/* MP40 0, sniper 1, K98 2, MG 3, MP44 4, K43 5, FG42 6 */
		static const int gear_of[DOD_CLASS_COUNT] = { 0, 6, 0, 4, 1, 3, 2 };
		part = 0;
		gear = gear_of[player_class];
		helmet = 0;
		head = who % 7;
	} else {
		return 0;
	}
	return part + 6 * helmet + 48 * head + 336 * gear;
}

const char *Dod_KillWeaponLabel(const char *classname, DodTeam team, int british)
{
	const DodWeaponDef *def;
	if (!classname || !classname[0] || !strcmp(classname, "worldspawn") || !strncmp(classname, "trigger_", 8))
		return "World";
	if (!strcmp(classname, "weapon_crowbar"))
		return team == DOD_TEAM_AXIS ? "Spade" : (british ? "Fairbairn" : "Knife");
	if (strstr(classname, "grenade") || !strcmp(classname, "weapon_handgrenade"))
		return "Grenade";
	if (strstr(classname, "rocket"))
		return "Rocket";
	def = Dod_FindWeaponDef(classname);
	if (def && def->title[0])
		return def->title;
	return "Weapon";
}

static int g_class_limit[4][DOD_CLASS_COUNT];
static int g_class_mask[4] = { -1, -1, -1, -1 };
static int g_class_limits_set;

static void dod_class_limits_init(void)
{
	int t, c;
	if (g_class_limits_set)
		return;
	for (t = 0; t < 4; ++t)
		for (c = 0; c < DOD_CLASS_COUNT; ++c)
			g_class_limit[t][c] = -1;
	g_class_limits_set = 1;
}

void Dod_SetClassLimit(DodTeam team, DodClass player_class, int limit)
{
	dod_class_limits_init();
	if ((team == DOD_TEAM_ALLIES || team == DOD_TEAM_AXIS) && player_class > DOD_CLASS_NONE && player_class < DOD_CLASS_COUNT)
		g_class_limit[team][player_class] = limit < 0 ? -1 : limit;
}

void Dod_SetClassMask(DodTeam team, int mask)
{
	if (team == DOD_TEAM_ALLIES || team == DOD_TEAM_AXIS)
		g_class_mask[team] = mask;
}

int Dod_ClassLimit(DodTeam team, DodClass player_class)
{
	dod_class_limits_init();
	if ((team != DOD_TEAM_ALLIES && team != DOD_TEAM_AXIS) || player_class <= DOD_CLASS_NONE || player_class >= DOD_CLASS_COUNT)
		return -1;
	return g_class_limit[team][player_class];
}

int Dod_ClassCount(DodTeam team, DodClass player_class, int except_player)
{
	int i, n = 0;
	for (i = 1; i <= DOD_MAX_PLAYERS; ++i)
		if (i != except_player && g_dod_players[i].connected && g_dod_players[i].team == team &&
			g_dod_players[i].player_class == player_class)
			++n;
	return n;
}

int Dod_ClassAvailable(DodTeam team, DodClass player_class, int player_index)
{
	int limit;
	if ((team != DOD_TEAM_ALLIES && team != DOD_TEAM_AXIS) || player_class <= DOD_CLASS_NONE || player_class >= DOD_CLASS_COUNT)
		return 0;
	if (g_class_mask[team] != -1 && !(g_class_mask[team] & (1 << player_class)))
		return 0;
	limit = Dod_ClassLimit(team, player_class);
	if (limit < 0)
		return 1;
	return Dod_ClassCount(team, player_class, player_index) < limit;
}

static const struct { const char *cmd, *label, *us, *brit, *ger; } g_voices[] = {
	{ "attack", "Move out!", "usattack", "britattack", "gerattack" },
	{ "fallback", "Fall back!", "usfallback", "britfallback", "gerfallback" },
	{ "cover", "Covering fire!", "uscover", "britcover", "gercover" },
	{ "backup", "I need backup!", "usbackup", "britbackup", "gerbackup" },
	{ "hold", "Hold this position!", "ushold", "brithold", "gerhold" },
	{ "yessir", "Yes sir!", "usyessir", "brityessir", "geryessir" },
	{ "left", "Left!", "usleft", "britleft", "germanleft" },
	{ "right", "Right!", "usright", "britright", "germanright" },
	{ "needammo", "I need ammo!", "usneedammo", "britneedammo", "gerneedammo" },
	{ "sniper", "Sniper!", "ussniper", "britsniper", "gersniper" },
	{ "grenade", "Grenade!", "usgrenade", "britgrenade", "gergrenade" },
	/* called automatically */
	{ "fireinhole", "Fire in the hole!", "usfireinhole", "britfireinhole", "gerfireinhole" },
	{ "enemyahead", "Enemy ahead!", "usenemyahead", "britenemyahead", "gerenemyahead" },
	{ "medic", "Medic!", "usmedic", "britmedic", "germedic" },
};

int Dod_VoiceCount(void)
{
	return (int)(sizeof(g_voices) / sizeof(g_voices[0]));
}

const char *Dod_VoiceLabel(int voice)
{
	return voice >= 0 && voice < Dod_VoiceCount() ? g_voices[voice].label : "";
}

int Dod_VoiceFind(const char *name)
{
	int i;
	if (!name)
		return -1;
	for (i = 0; i < Dod_VoiceCount(); ++i)
		if (!strcmp(name, g_voices[i].cmd))
			return i;
	return -1;
}

int Dod_VoiceSound(int voice, DodTeam team, int british, char *buffer, int size)
{
	const char *base;
	int written;
	if (!buffer || size <= 0 || voice < 0 || voice >= Dod_VoiceCount())
		return 0;
	if (team == DOD_TEAM_AXIS)
		base = g_voices[voice].ger;
	else if (team == DOD_TEAM_ALLIES)
		base = british ? g_voices[voice].brit : g_voices[voice].us;
	else
		return 0;
	written = snprintf(buffer, (size_t)size, "player/%s.wav", base);
	if (written < 0 || written >= size) {
		buffer[0] = '\0';
		return 0;
	}
	return written;
}

static const char *dod_class_tag(DodClass c)
{
	switch (c) {
	case DOD_CLASS_RIFLEMAN: return "RIF";
	case DOD_CLASS_ASSAULT: return "ASL";
	case DOD_CLASS_SUPPORT: return "SUP";
	case DOD_CLASS_SNIPER: return "SNP";
	case DOD_CLASS_MG: return "MG";
	case DOD_CLASS_ROCKET: return "RKT";
	default: return "--";
	}
}

/* true when player a ranks above player b: Score, then kills, then fewer deaths, then lower index */
static int dod_ranks_above(int a, int b)
{
	const int sa = Dod_PlayerScore(a), sb = Dod_PlayerScore(b);
	if (sa != sb) return sa > sb;
	if (g_dod_players[a].kills != g_dod_players[b].kills) return g_dod_players[a].kills > g_dod_players[b].kills;
	if (g_dod_players[a].deaths != g_dod_players[b].deaths) return g_dod_players[a].deaths < g_dod_players[b].deaths;
	return a < b;
}

int Dod_FormatScoreboard(char *buffer, int size, const char *const *names, int viewer_index)
{
	static const DodTeam order[2] = { DOD_TEAM_ALLIES, DOD_TEAM_AXIS };
	int len = 0, t;
	if (!buffer || size <= 1)
		return 0;
	buffer[0] = '\0';
	for (t = 0; t < 2; ++t) {
		int ranked[DOD_MAX_PLAYERS], n = 0, i, j, rank = 0;
		const DodTeam team = order[t];
		int w = snprintf(buffer + len, (size_t)(size - len), "%s%s  %d\n", t ? "\n" : "", Dod_TeamName(team),
			team == DOD_TEAM_ALLIES ? g_dod_state.allies_score : g_dod_state.axis_score);
		if (w < 0 || w >= size - len)
			break;
		len += w;
		for (i = 1; i <= DOD_MAX_PLAYERS; ++i)
			if (g_dod_players[i].connected && g_dod_players[i].team == team)
				ranked[n++] = i;
		for (i = 1; i < n; ++i) {	/* insertion sort, n is at most 32 */
			const int v = ranked[i];
			for (j = i - 1; j >= 0 && dod_ranks_above(v, ranked[j]); --j)
				ranked[j + 1] = ranked[j];
			ranked[j + 1] = v;
		}
		for (i = 0; i < n; ++i) {
			const int p = ranked[i];
			const char *name = names && names[p] && names[p][0] ? names[p] : "player";
			w = snprintf(buffer + len, (size_t)(size - len), "%s%d. %s  %s  S%d K%d D%d\n", p == viewer_index ? ">" : "", ++rank,
				name, dod_class_tag(g_dod_players[p].player_class), Dod_PlayerScore(p), g_dod_players[p].kills,
				g_dod_players[p].deaths);
			if (w < 0 || w >= size - len) {
				buffer[len] = '\0';	/* the line did not fit: drop it and everything below */
				return len;
			}
			len += w;
		}
	}
	return len;
}

void Dod_ClearObjectives(void)
{
	memset(g_dod_objectives, 0, sizeof(g_dod_objectives));
	dod_recount_objectives();
}

int Dod_RegisterObjective(int id, DodObjectiveKind kind, DodTeam initial_owner,
	float capture_seconds, float score_per_minute)
{
	int i;
	DodObjectiveState *objective = dod_find_objective(id);
	if (id < 0 || kind <= DOD_OBJ_NONE || kind > DOD_OBJ_TICK_CONTROL)
		return 0;
	if (!objective) {
		for (i = 0; i < DOD_MAX_OBJECTIVES; ++i) {
			if (!g_dod_objectives[i].active) {
				objective = &g_dod_objectives[i];
				break;
			}
		}
	}
	if (!objective)
		return 0;
	memset(objective, 0, sizeof(*objective));
	objective->active = 1;
	objective->id = id;
	objective->kind = kind;
	objective->owner = dod_valid_team(initial_owner) ? initial_owner : DOD_TEAM_UNASSIGNED;
	objective->default_owner = objective->owner;
	objective->capture_seconds = capture_seconds > 0.0f ? capture_seconds : DOD_CAPTURE_SECONDS_DEFAULT;
	objective->score_per_minute = score_per_minute > 0.0f ? score_per_minute : 0.0f;
	objective->points_for_cap = 1;
	objective->team_points = 0;
	objective->time_points = 1;
	dod_recount_objectives();
	return 1;
}

int Dod_TickObjective(int id, int allies_inside, int axis_inside, float frametime)
{
	DodObjectiveState *objective = dod_find_objective(id);
	DodTeam team = DOD_TEAM_UNASSIGNED;
	if (!objective || frametime <= 0.0f || g_dod_state.phase != DOD_ROUND_ACTIVE)
		return 0;
	if (allies_inside > 0 && axis_inside <= 0)
		team = DOD_TEAM_ALLIES;
	else if (axis_inside > 0 && allies_inside <= 0)
		team = DOD_TEAM_AXIS;
	if (!dod_valid_team(team))
		return 0;
	if (objective->owner == team) {
		objective->capturing_team = DOD_TEAM_UNASSIGNED;
		objective->capture_progress = 0.0f;
		return 0;
	}
	if (objective->capturing_team != team) {
		objective->capturing_team = team;
		objective->capture_progress = 0.0f;
	}
	objective->capture_progress += frametime / objective->capture_seconds;
	if (objective->capture_progress < 1.0f)
		return 0;
	objective->capture_progress = 0.0f;
	objective->capturing_team = DOD_TEAM_UNASSIGNED;
	objective->owner = team;
	dod_recount_objectives();
	dod_add_team_score(team, (float)objective->team_points);
	++g_dod_state.capture_serial;
	g_dod_state.last_capture_team = team;
	if (!g_dod_state.master_driven && Dod_TeamOwnsAllObjectives(team))
		Dod_EndRound(team);
	return 1;
}

int Dod_SetObjectiveOwner(int id, DodTeam owner)
{
	DodObjectiveState *objective = dod_find_objective(id);
	if (!objective || (owner != DOD_TEAM_UNASSIGNED && !dod_valid_team(owner)))
		return 0;
	objective->owner = owner;
	objective->capturing_team = DOD_TEAM_UNASSIGNED;
	objective->capture_progress = 0.0f;
	dod_recount_objectives();
	return 1;
}

const DodObjectiveState *Dod_GetObjective(int id)
{
	return dod_find_objective(id);
}

int Dod_HurtAppliesToTeam(int spawnflags, DodTeam team)
{
	const int axis_only = (spawnflags & DOD_HURT_AXIS_ONLY) != 0;
	const int allies_only = (spawnflags & DOD_HURT_ALLIES_ONLY) != 0;
	if (axis_only == allies_only)
		return 1;
	if (team != DOD_TEAM_ALLIES && team != DOD_TEAM_AXIS)
		return 0;
	return axis_only ? team == DOD_TEAM_AXIS : team == DOD_TEAM_ALLIES;
}
