#include "dod_achievements.h"
#include <string.h>

typedef struct { const char *name, *text; unsigned int goal; } AchDef;

static const AchDef g_def[DOD_ACH_COUNT] = {
	{ "Nuts!", "Block 100 flag captures", 100 },
	{ "Old School", "Get 150 kills with the Colt", 150 },
	{ "Roscoe", "Get 150 kills with the P38", 150 },
	{ "Egg Layer", "Get 250 kills with the US Frag Grenade", 250 },
	{ "Potato Masher", "Get 250 kills with the Stick Grenade", 250 },
	{ "HEATer", "Get 1000 kills as the Rocket player class", 1000 },
	{ "Dig For Victory!", "Get 150 kills with the Shovel", 150 },
	{ "Demolition Man", "Plant 100 bombs on enemy objectives", 100 },
	{ "Rocket Powered", "Get 500 kills with the Bazooka", 500 },
	{ "Black Maria", "Get 250 kills with the K98k's Rifle Grenade", 250 },
	{ "Hail Mary", "In Dod_Jagd, plant a bomb that explodes in overtime to extend the round", 0 },
	{ "Tank Terror", "Get 500 kills with the Pschreck", 500 },
	{ "Carpet Bomber", "Get 250 kills with the Garand's Rifle Grenade", 250 },
	{ "Cutthroat", "Get 150 kills with the Knife", 150 },
	{ "Jack of All Trades", "In a single life, get a kill with an MG, Sniper Rifle, Rifle, SubMG and a Grenade", 0 },
	{ "Knuckleduster", "Get 150 kills with your fists", 150 },
	{ "Combat Engineer", "Defuse 100 enemy bombs", 100 },
	{ "War Hero", "Complete All Achievements", 0 }
};

void DodAch_Reset(DodAchState *state) { memset(state, 0, sizeof(*state)); }

const char *DodAch_Name(DodAchId id) { return (unsigned)id < DOD_ACH_COUNT ? g_def[id].name : ""; }
const char *DodAch_Text(DodAchId id) { return (unsigned)id < DOD_ACH_COUNT ? g_def[id].text : ""; }

DodKillWeapon DodAch_ClassifyKill(const char *c, int melee_is_spade)
{
	if (!c || !c[0]) return DOD_KW_OTHER;
	if (!strcmp(c, "weapon_crowbar")) return melee_is_spade ? DOD_KW_SHOVEL : DOD_KW_KNIFE;
	if (!strcmp(c, "weapon_dod_colt")) return DOD_KW_COLT;
	if (!strcmp(c, "weapon_dod_luger")) return DOD_KW_LUGER;
	if (!strcmp(c, "weapon_dod_bazooka")) return DOD_KW_BAZOOKA;
	if (!strcmp(c, "weapon_dod_pschreck")) return DOD_KW_PSCHRECK;
	if (!strcmp(c, "weapon_dod_mg42") || !strcmp(c, "weapon_dod_30cal")) return DOD_KW_MG;
	if (!strcmp(c, "weapon_dod_spring") || !strcmp(c, "weapon_dod_k98s")) return DOD_KW_SNIPER;
	if (!strcmp(c, "weapon_dod_garand") || !strcmp(c, "weapon_dod_k98")) return DOD_KW_RIFLE;
	if (!strcmp(c, "weapon_dod_thompson") || !strcmp(c, "weapon_dod_mp40") || !strcmp(c, "weapon_dod_stg44")) return DOD_KW_SMG;
	if (!strcmp(c, "weapon_handgrenade") || strstr(c, "grenade")) return melee_is_spade ? DOD_KW_STICK : DOD_KW_FRAG;
	return DOD_KW_OTHER;
}

static unsigned int check(DodAchState *s)
{
	unsigned int before = s->unlocked, got = 0, all = (1u << DOD_ACH_WAR_HERO) - 1u;
	int i;
	for (i = 0; i < DOD_ACH_WAR_HERO; i++) {
		unsigned int goal, have = DodAch_Progress(s, (DodAchId)i, &goal);
		if (goal && have >= goal) s->unlocked |= 1u << i;
	}
	if ((s->life_mask & DOD_ACH_LIFE_ALL) == DOD_ACH_LIFE_ALL) s->unlocked |= 1u << DOD_ACH_JACK_OF_ALL_TRADES;
	if ((s->unlocked & all) == all) s->unlocked |= 1u << DOD_ACH_WAR_HERO;
	got = s->unlocked & ~before;
	return got;
}

unsigned int DodAch_Progress(const DodAchState *s, DodAchId id, unsigned int *goal)
{
	*goal = (unsigned)id < DOD_ACH_COUNT ? g_def[id].goal : 0;
	switch (id) {
	case DOD_ACH_NUTS: return s->blocked_captures;
	case DOD_ACH_OLD_SCHOOL: return s->kills[DOD_KW_COLT];
	case DOD_ACH_ROSCOE: return s->kills[DOD_KW_LUGER];
	case DOD_ACH_EGG_LAYER: return s->kills[DOD_KW_FRAG];
	case DOD_ACH_POTATO_MASHER: return s->kills[DOD_KW_STICK];
	case DOD_ACH_HEATER: return s->rocket_class_kills;
	case DOD_ACH_DIG_FOR_VICTORY: return s->kills[DOD_KW_SHOVEL];
	case DOD_ACH_DEMOLITION_MAN: return s->bombs_planted;
	case DOD_ACH_ROCKET_POWERED: return s->kills[DOD_KW_BAZOOKA];
	case DOD_ACH_BLACK_MARIA: return s->kills[DOD_KW_K98_RIFLE_GRENADE];
	case DOD_ACH_TANK_TERROR: return s->kills[DOD_KW_PSCHRECK];
	case DOD_ACH_CARPET_BOMBER: return s->kills[DOD_KW_GARAND_RIFLE_GRENADE];
	case DOD_ACH_CUTTHROAT: return s->kills[DOD_KW_KNIFE];
	case DOD_ACH_KNUCKLEDUSTER: return s->kills[DOD_KW_FISTS];
	case DOD_ACH_COMBAT_ENGINEER: return s->bombs_defused;
	default: return (s->unlocked >> id) & 1u;
	}
}

unsigned int DodAch_RecordKill(DodAchState *s, DodKillWeapon w, DodClass cls)
{
	if ((unsigned)w >= DOD_KW_COUNT) return 0;
	++s->kills[w];
	if (cls == DOD_CLASS_ROCKET) ++s->rocket_class_kills;
	switch (w) {
	case DOD_KW_MG: s->life_mask |= DOD_ACH_LIFE_MG; break;
	case DOD_KW_SNIPER: s->life_mask |= DOD_ACH_LIFE_SNIPER; break;
	case DOD_KW_RIFLE: s->life_mask |= DOD_ACH_LIFE_RIFLE; break;
	case DOD_KW_SMG: s->life_mask |= DOD_ACH_LIFE_SMG; break;
	case DOD_KW_FRAG: case DOD_KW_STICK: s->life_mask |= DOD_ACH_LIFE_GRENADE; break;
	default: break;
	}
	return check(s);
}

unsigned int DodAch_RecordBlock(DodAchState *s) { ++s->blocked_captures; return check(s); }
unsigned int DodAch_RecordPlant(DodAchState *s) { ++s->bombs_planted; return check(s); }
unsigned int DodAch_RecordDefuse(DodAchState *s) { ++s->bombs_defused; return check(s); }

unsigned int DodAch_RecordOvertimeBomb(DodAchState *s, const char *map)
{
	unsigned int before = s->unlocked;
	if (!map || strcmp(map, "dod_jagd")) return 0;
	s->unlocked |= 1u << DOD_ACH_HAIL_MARY;
	check(s);
	return s->unlocked & ~before;
}

void DodAch_LifeReset(DodAchState *s) { s->life_mask = 0; }

/* Blob: "DACH", u16 version 1, u16 reserved, kills[16], rocket, blocked, planted, defused, unlocked (u32 each), crc32. */
static unsigned int crc32(const unsigned char *d, unsigned int n)
{
	unsigned int c = 0xFFFFFFFFu, i, k;
	for (i = 0; i < n; i++) {
		c ^= d[i];
		for (k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
	}
	return ~c;
}
static void put32(unsigned char *p, unsigned int v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }
static unsigned int get32(const unsigned char *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24); }

void DodAch_Pack(const DodAchState *s, unsigned char *b)
{
	int i;
	memset(b, 0, DOD_ACH_BLOB_SIZE);
	put32(b, 0x48434144u);	/* "DACH" little endian */
	put32(b + 4, 1);
	for (i = 0; i < DOD_KW_COUNT; i++) put32(b + 8 + 4 * i, s->kills[i]);
	put32(b + 8 + 4 * DOD_KW_COUNT, s->rocket_class_kills);
	put32(b + 12 + 4 * DOD_KW_COUNT, s->blocked_captures);
	put32(b + 16 + 4 * DOD_KW_COUNT, s->bombs_planted);
	put32(b + 20 + 4 * DOD_KW_COUNT, s->bombs_defused);
	put32(b + 24 + 4 * DOD_KW_COUNT, s->unlocked);
	put32(b + DOD_ACH_BLOB_SIZE - 4, crc32(b, DOD_ACH_BLOB_SIZE - 4));
}

int DodAch_Unpack(DodAchState *s, const unsigned char *b, unsigned int len)
{
	DodAchState n;
	int i;
	if (len < DOD_ACH_BLOB_SIZE || get32(b) != 0x48434144u || get32(b + 4) != 1
		|| get32(b + DOD_ACH_BLOB_SIZE - 4) != crc32(b, DOD_ACH_BLOB_SIZE - 4))
		return 0;
	DodAch_Reset(&n);
	for (i = 0; i < DOD_KW_COUNT; i++) n.kills[i] = get32(b + 8 + 4 * i);
	n.rocket_class_kills = get32(b + 8 + 4 * DOD_KW_COUNT);
	n.blocked_captures = get32(b + 12 + 4 * DOD_KW_COUNT);
	n.bombs_planted = get32(b + 16 + 4 * DOD_KW_COUNT);
	n.bombs_defused = get32(b + 20 + 4 * DOD_KW_COUNT);
	n.unlocked = get32(b + 24 + 4 * DOD_KW_COUNT) & ((1u << DOD_ACH_COUNT) - 1u);
	*s = n;
	return 1;
}
