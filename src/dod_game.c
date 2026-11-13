/* Server-side Day of Defeat rules state for the GameCube HL1SDK port. */
#include "dod_game.h"
#include "dod_weapons.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define DOD_CAPTURE_SECONDS_DEFAULT 10.0f
#define DOD_INTERMISSION_SECONDS 8.0f

static DodGameRulesState g_dod_state;
static DodPlayerState g_dod_players[DOD_MAX_PLAYERS + 1];
static DodObjectiveState g_dod_objectives[DOD_MAX_OBJECTIVES];
static float g_score_remainder[4];
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
	if (!dod_valid_team(team) || amount <= 0.0f)
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
	g_dod_state.phase = DOD_ROUND_WAITING;
	g_dod_state.winner = DOD_TEAM_UNASSIGNED;
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

void Dod_StartRound(float duration_seconds)
{
	g_dod_state.phase = DOD_ROUND_ACTIVE;
	g_dod_state.round_active = 1;
	g_dod_state.winner = DOD_TEAM_UNASSIGNED;
	g_dod_state.allies_score = 0;
	g_dod_state.axis_score = 0;
	g_dod_state.round_time_remaining = duration_seconds > 0.0f ? duration_seconds : 0.0f;
	g_dod_state.intermission_remaining = 0.0f;
	memset(g_score_remainder, 0, sizeof(g_score_remainder));
}

void Dod_EndRound(DodTeam winner)
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
	g_dod_state.phase = DOD_ROUND_INTERMISSION;
	g_dod_state.round_active = 0;
	g_dod_state.round_time_remaining = 0.0f;
	g_dod_state.intermission_remaining = DOD_INTERMISSION_SECONDS;
}

void Dod_Think(float frametime)
{
	int i;
	if (frametime <= 0.0f)
		return;
	if (g_dod_state.phase == DOD_ROUND_ACTIVE) {
		if (g_dod_state.round_time_remaining > 0.0f) {
			g_dod_state.round_time_remaining -= frametime;
			if (g_dod_state.round_time_remaining <= 0.0f)
				Dod_EndRound(DOD_TEAM_UNASSIGNED);
		}
		for (i = 0; i < DOD_MAX_OBJECTIVES; ++i) {
			DodObjectiveState *objective = &g_dod_objectives[i];
			if (objective->active && dod_valid_team(objective->owner))
				dod_add_team_score(objective->owner,
					objective->score_per_minute * frametime / 60.0f);
		}
	} else if (g_dod_state.phase == DOD_ROUND_INTERMISSION) {
		g_dod_state.intermission_remaining -= frametime;
		if (g_dod_state.intermission_remaining < 0.0f)
			g_dod_state.intermission_remaining = 0.0f;
	}
}

const DodGameRulesState *Dod_GetState(void)
{
	return &g_dod_state;
}

int Dod_PlayerConnect(int player_index)
{
	if (!dod_valid_player(player_index))
		return 0;
	memset(&g_dod_players[player_index], 0, sizeof(g_dod_players[player_index]));
	g_dod_players[player_index].connected = 1;
	g_dod_players[player_index].team = DOD_TEAM_UNASSIGNED;
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
		if (victim && dod_valid_team(attacker->team) && attacker->team == victim->team)
			return;
		++attacker->kills;
		++attacker->objective_score;
		dod_add_team_score(attacker->team, 1.0f);
	}
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
		/* Retail: Allies carry the American knife, Axis the spade. */
		if (view_model) *view_model = player->team == DOD_TEAM_ALLIES ? "models/v_amerk.mdl" : "models/v_spade.mdl";
		if (player_model) *player_model = player->team == DOD_TEAM_ALLIES ? "models/p_amerk.mdl" : "models/p_spade.mdl";
		return 1;
	}
	if (kind == DOD_MODEL_PISTOL) {
		if (!Dod_ClassHasSidearm(player->player_class))
			return 0;
		if (view_model) *view_model = player->team == DOD_TEAM_ALLIES ? "models/v_colt.mdl" : "models/v_luger.mdl";
		if (player_model) *player_model = player->team == DOD_TEAM_ALLIES ? "models/p_colt.mdl" : "models/p_luger.mdl";
		return 1;
	}
	if (kind == DOD_MODEL_AUTOMATIC) {
		if (player->player_class == DOD_CLASS_ASSAULT) {
			if (view_model) *view_model = player->team == DOD_TEAM_ALLIES ? "models/v_tommy.mdl" : "models/v_mp40.mdl";
			if (player_model) *player_model = player->team == DOD_TEAM_ALLIES ? "models/p_tommy.mdl" : "models/p_mp40.mdl";
			return 1;
		}
		if (player->player_class == DOD_CLASS_MG) {
			if (view_model) *view_model = player->team == DOD_TEAM_ALLIES ? "models/v_30cal.mdl" : "models/v_mg42.mdl";
			if (player_model) *player_model = player->team == DOD_TEAM_ALLIES ? "models/p_30cal.mdl" : "models/p_mg42pr.mdl";
			return 1;
		}
		if (player->player_class == DOD_CLASS_RIFLEMAN) {
			if (view_model) *view_model = player->team == DOD_TEAM_ALLIES ? "models/v_garand.mdl" : "models/v_98k.mdl";
			if (player_model) *player_model = player->team == DOD_TEAM_ALLIES ? "models/p_garand.mdl" : "models/p_k98.mdl";
			return 1;
		}
	}
	if (kind == DOD_MODEL_SUPPORT && player->player_class == DOD_CLASS_SUPPORT) {
		if (view_model) *view_model = player->team == DOD_TEAM_ALLIES ? "models/v_bren.mdl" : "models/v_mp44.mdl";
		if (player_model) *player_model = player->team == DOD_TEAM_ALLIES ? "models/p_bren.mdl" : "models/p_stg44.mdl";
		return 1;
	}
	if (kind == DOD_MODEL_SNIPER && player->player_class == DOD_CLASS_SNIPER) {
		if (view_model) *view_model = player->team == DOD_TEAM_ALLIES ? "models/v_spring.mdl" : "models/v_scoped98k.mdl";
		if (player_model) *player_model = player->team == DOD_TEAM_ALLIES ? "models/p_spring.mdl" : "models/p_k98.mdl";
		return 1;
	}
	if (kind == DOD_MODEL_ROCKET && player->player_class == DOD_CLASS_ROCKET) {
		if (view_model) *view_model = player->team == DOD_TEAM_ALLIES ? "models/v_bazooka.mdl" : "models/v_panzerschreck.mdl";
		if (player_model) *player_model = player->team == DOD_TEAM_ALLIES ? "models/p_bazooka.mdl" : "models/p_pschreck.mdl";
		return 1;
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

int Dod_GetGrenade(DodTeam team, DodGrenadeDef *out)
{
	if ((team != DOD_TEAM_ALLIES && team != DOD_TEAM_AXIS) || !out)
		return 0;
	*out = g_grenades[team - 1];
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
	objective->capture_seconds = capture_seconds > 0.0f ? capture_seconds : DOD_CAPTURE_SECONDS_DEFAULT;
	objective->score_per_minute = score_per_minute > 0.0f ? score_per_minute : 0.0f;
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
	dod_add_team_score(team, 5.0f);
	if (g_dod_state.objective_count > 0 &&
		g_dod_state.controlled_objectives[team] == g_dod_state.objective_count)
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
