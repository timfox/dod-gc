/* Day of Defeat achievements: a tiny counter set for the local player (host-testable, no engine calls). */
#ifndef DODGC_DOD_ACHIEVEMENTS_H
#define DODGC_DOD_ACHIEVEMENTS_H

#include "dod_game.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	DOD_ACH_NUTS = 0,	/* Nuts!: block 100 flag captures */
	DOD_ACH_OLD_SCHOOL,	/* 150 Colt kills */
	DOD_ACH_ROSCOE,	/* 150 P38 (Luger) kills */
	DOD_ACH_EGG_LAYER,	/* 250 US frag grenade kills */
	DOD_ACH_POTATO_MASHER,	/* 250 stick grenade kills */
	DOD_ACH_HEATER,	/* 1000 kills as the Rocket class */
	DOD_ACH_DIG_FOR_VICTORY,	/* 150 shovel kills */
	DOD_ACH_DEMOLITION_MAN,	/* plant 100 bombs on enemy objectives */
	DOD_ACH_ROCKET_POWERED,	/* 500 Bazooka kills */
	DOD_ACH_BLACK_MARIA,	/* 250 K98k rifle grenade kills */
	DOD_ACH_HAIL_MARY,	/* dod_jagd: a bomb exploding in overtime extends the round */
	DOD_ACH_TANK_TERROR,	/* 500 Pschreck kills */
	DOD_ACH_CARPET_BOMBER,	/* 250 Garand rifle grenade kills */
	DOD_ACH_CUTTHROAT,	/* 150 knife kills */
	DOD_ACH_JACK_OF_ALL_TRADES,	/* in one life: MG, sniper rifle, rifle, SMG and grenade kill */
	DOD_ACH_KNUCKLEDUSTER,	/* 150 fist kills */
	DOD_ACH_COMBAT_ENGINEER,	/* defuse 100 enemy bombs */
	DOD_ACH_WAR_HERO,	/* all the others */
	DOD_ACH_COUNT
} DodAchId;

/* What a kill was made with, as far as the achievements care. */
typedef enum {
	DOD_KW_OTHER = 0, DOD_KW_COLT, DOD_KW_LUGER, DOD_KW_FRAG, DOD_KW_STICK, DOD_KW_SHOVEL, DOD_KW_KNIFE, DOD_KW_FISTS,
	DOD_KW_BAZOOKA, DOD_KW_PSCHRECK, DOD_KW_K98_RIFLE_GRENADE, DOD_KW_GARAND_RIFLE_GRENADE,
	DOD_KW_MG, DOD_KW_SNIPER, DOD_KW_RIFLE, DOD_KW_SMG,
	DOD_KW_COUNT
} DodKillWeapon;

#define DOD_ACH_LIFE_MG 1u
#define DOD_ACH_LIFE_SNIPER 2u
#define DOD_ACH_LIFE_RIFLE 4u
#define DOD_ACH_LIFE_SMG 8u
#define DOD_ACH_LIFE_GRENADE 16u
#define DOD_ACH_LIFE_ALL 31u

typedef struct {
	unsigned int kills[DOD_KW_COUNT];	/* by weapon */
	unsigned int rocket_class_kills;
	unsigned int blocked_captures, bombs_planted, bombs_defused;
	unsigned int unlocked;	/* bit per DodAchId */
	unsigned int life_mask;	/* weapon kinds killed with since the last spawn (not saved) */
} DodAchState;

#define DOD_ACH_BLOB_SIZE 96

void DodAch_Reset(DodAchState *state);
/* Weapon kind of a kill from the weapon / inflictor classname. `melee_is_spade`: the killer's melee is the Axis spade. */
DodKillWeapon DodAch_ClassifyKill(const char *classname, int melee_is_spade);
/* Each Record* returns the mask of achievements that unlocked on this event (War Hero included). */
unsigned int DodAch_RecordKill(DodAchState *state, DodKillWeapon weapon, DodClass killer_class);
unsigned int DodAch_RecordBlock(DodAchState *state);
unsigned int DodAch_RecordPlant(DodAchState *state);
unsigned int DodAch_RecordDefuse(DodAchState *state);
unsigned int DodAch_RecordOvertimeBomb(DodAchState *state, const char *map);
void DodAch_LifeReset(DodAchState *state);
const char *DodAch_Name(DodAchId id);
const char *DodAch_Text(DodAchId id);
/* Progress toward a counted goal: *goal is 0 for the one-shot ones. */
unsigned int DodAch_Progress(const DodAchState *state, DodAchId id, unsigned int *goal);
/* Save blob (little endian, magic, version, checksum). Unpack rejects any damaged blob and leaves `state` untouched. */
void DodAch_Pack(const DodAchState *state, unsigned char *blob);
int DodAch_Unpack(DodAchState *state, const unsigned char *blob, unsigned int len);

#ifdef __cplusplus
}
#endif
#endif
