/* Stub DoD gamerules — progress scaffolding for the GC port.
 * Not yet linked into dodgc.dol; HL1SDK deathmatch still provides runtime.
 */
#include "dod_game.h"

static DodGameRulesState g_dod_state;

void Dod_GameInit(void)
{
	g_dod_state.round_active = 0;
	g_dod_state.allies_score = 0;
	g_dod_state.axis_score = 0;
	g_dod_state.round_time_remaining = 0.f;
}

void Dod_GameShutdown(void)
{
	g_dod_state.round_active = 0;
}

void Dod_Think(float frametime)
{
	(void)frametime;
	if (!g_dod_state.round_active)
		return;
	if (g_dod_state.round_time_remaining > 0.f) {
		g_dod_state.round_time_remaining -= frametime;
		if (g_dod_state.round_time_remaining < 0.f)
			g_dod_state.round_time_remaining = 0.f;
	}
}

const DodGameRulesState *Dod_GetState(void)
{
	return &g_dod_state;
}
