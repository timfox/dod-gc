#include "dod_achievements.h"
#include <assert.h>
#include <string.h>

static void test_counted(void)
{
	DodAchState s; unsigned int i, got = 0;
	DodAch_Reset(&s);
	for (i = 0; i < 149; i++) assert(!DodAch_RecordKill(&s, DOD_KW_COLT, DOD_CLASS_RIFLEMAN));
	got = DodAch_RecordKill(&s, DOD_KW_COLT, DOD_CLASS_RIFLEMAN);
	assert(got == (1u << DOD_ACH_OLD_SCHOOL));
	assert(!DodAch_RecordKill(&s, DOD_KW_COLT, DOD_CLASS_RIFLEMAN));	/* only once */
	for (i = 0; i < 99; i++) assert(!DodAch_RecordPlant(&s));
	assert(DodAch_RecordPlant(&s) == (1u << DOD_ACH_DEMOLITION_MAN));
	for (i = 0; i < 99; i++) DodAch_RecordBlock(&s);
	assert(DodAch_RecordBlock(&s) == (1u << DOD_ACH_NUTS));
	for (i = 0; i < 99; i++) DodAch_RecordDefuse(&s);
	assert(DodAch_RecordDefuse(&s) == (1u << DOD_ACH_COMBAT_ENGINEER));
	for (i = 0; i < 999; i++) DodAch_RecordKill(&s, DOD_KW_OTHER, DOD_CLASS_ROCKET);
	assert(DodAch_RecordKill(&s, DOD_KW_OTHER, DOD_CLASS_ROCKET) == (1u << DOD_ACH_HEATER));
}

static void test_classify(void)
{
	assert(DodAch_ClassifyKill("weapon_crowbar", 1) == DOD_KW_SHOVEL);
	assert(DodAch_ClassifyKill("weapon_crowbar", 0) == DOD_KW_KNIFE);
	assert(DodAch_ClassifyKill("weapon_dod_luger", 1) == DOD_KW_LUGER);
	assert(DodAch_ClassifyKill("grenade", 0) == DOD_KW_FRAG);
	assert(DodAch_ClassifyKill("grenade", 1) == DOD_KW_STICK);
	assert(DodAch_ClassifyKill("weapon_dod_pschreck", 1) == DOD_KW_PSCHRECK);
	assert(DodAch_ClassifyKill(0, 0) == DOD_KW_OTHER);
	assert(DodAch_ClassifyKill("worldspawn", 0) == DOD_KW_OTHER);
}

static void test_jack(void)
{
	DodAchState s; unsigned int g;
	DodAch_Reset(&s);
	DodAch_RecordKill(&s, DOD_KW_MG, DOD_CLASS_MG);
	DodAch_RecordKill(&s, DOD_KW_SNIPER, DOD_CLASS_SNIPER);
	DodAch_RecordKill(&s, DOD_KW_RIFLE, DOD_CLASS_RIFLEMAN);
	DodAch_RecordKill(&s, DOD_KW_SMG, DOD_CLASS_ASSAULT);
	DodAch_LifeReset(&s);	/* died: the set starts over */
	assert(!DodAch_RecordKill(&s, DOD_KW_FRAG, DOD_CLASS_RIFLEMAN));
	DodAch_RecordKill(&s, DOD_KW_MG, DOD_CLASS_MG);
	DodAch_RecordKill(&s, DOD_KW_SNIPER, DOD_CLASS_SNIPER);
	DodAch_RecordKill(&s, DOD_KW_RIFLE, DOD_CLASS_RIFLEMAN);
	g = DodAch_RecordKill(&s, DOD_KW_SMG, DOD_CLASS_ASSAULT);
	assert(g == (1u << DOD_ACH_JACK_OF_ALL_TRADES));
}

static void test_hail_mary_and_war_hero(void)
{
	DodAchState s; unsigned int i, g, goal;
	DodAch_Reset(&s);
	assert(!DodAch_RecordOvertimeBomb(&s, "dod_charlie"));
	assert(DodAch_RecordOvertimeBomb(&s, "dod_jagd") == (1u << DOD_ACH_HAIL_MARY));
	s.unlocked = ((1u << DOD_ACH_WAR_HERO) - 1u) & ~(1u << DOD_ACH_NUTS);
	for (i = 0; i < 99; i++) DodAch_RecordBlock(&s);
	g = DodAch_RecordBlock(&s);
	assert(g == ((1u << DOD_ACH_NUTS) | (1u << DOD_ACH_WAR_HERO)));
	assert(DodAch_Progress(&s, DOD_ACH_NUTS, &goal) == 100 && goal == 100);
	assert(strcmp(DodAch_Name(DOD_ACH_ROSCOE), "Roscoe") == 0);
}

static void test_blob(void)
{
	DodAchState a, b; unsigned char blob[DOD_ACH_BLOB_SIZE];
	DodAch_Reset(&a);
	DodAch_RecordKill(&a, DOD_KW_KNIFE, DOD_CLASS_RIFLEMAN);
	DodAch_RecordPlant(&a);
	DodAch_Pack(&a, blob);
	DodAch_Reset(&b);
	assert(DodAch_Unpack(&b, blob, sizeof blob));
	assert(b.kills[DOD_KW_KNIFE] == 1 && b.bombs_planted == 1);
	blob[20] ^= 1;	/* damaged: rejected, state untouched */
	b.bombs_planted = 7;
	assert(!DodAch_Unpack(&b, blob, sizeof blob));
	assert(b.bombs_planted == 7);
	assert(!DodAch_Unpack(&b, blob, 10));
}

int main(void)
{
	test_counted(); test_classify(); test_jack(); test_hail_mary_and_war_hero(); test_blob();
	return 0;
}
