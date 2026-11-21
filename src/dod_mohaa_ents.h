#ifndef DODGC_DOD_MOHAA_ENTS_H
#define DODGC_DOD_MOHAA_ENTS_H

/* Game-DLL entry points of the MOHAA script binding (dod_mohaa_ents.cpp). */

/* At map start (game rules construction): runs maps/<map>.scr if the map has one. */
void DodGC_MohaaLevelInit( void );
/* Every game rules Think. */
void DodGC_MohaaThink( void );
/* An entity event a script can wait for, e.g. ("trigger") from a trigger firing. */
void DodGC_MohaaEntityEvent( edict_t *e, const char *event );

#endif
