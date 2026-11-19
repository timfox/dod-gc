# FilmGc WW2 series: maps and the events they stand in for

Each episode is a short documentary (about two and a half minutes) told over scenes rebuilt in one retail Day
of Defeat map. A map is matched to an event only where its setting fits: the right country, season,
terrain and troops, and in most cases a briefing written by its authors about that campaign. Each episode
opens with the note that the pictures are game recreations, keeps a fact check beside it, and closes with a
dedication. Order is by date of the event, except the pilot.

| # | Episode | Map | Event | Date | Status |
|---|---|---|---|---|---|
| 1 | Omaha | dod_charlie | The landing at Omaha Beach | 6 Jun 1944 | Pilot; capture in progress |
| 2 | Salerno | dod_avalanche | The Salerno beachhead and the fight for Altavilla | 9-18 Sep 1943 | Written, fact-checked, framed |
| 3 | La Fière | dod_merderet | The 82nd Airborne at the La Fière bridge and causeway | 6-9 Jun 1944 | Written, fact-checked, framed |
| 4 | Sicily | dod_saints | Operation Husky: American infantry in the Sicilian hill towns | Jul-Aug 1943 | Planned |
| 5 | Anzio | dod_anzio | Operation Shingle: the landing and the long beachhead | Jan-May 1944 | Planned |
| 6 | Gliders | dod_glider | The glider and parachute landings behind the beaches | 6 Jun 1944 | Planned |
| 7 | Villers-Bocage | dod_jagd | British armour and infantry at Villers-Bocage, on the road to Caen | 13 Jun 1944 | Planned |
| 8 | The hedgerows | dod_donner | The fight through the bocage to Saint-Lô | Jun-Jul 1944 | Planned |
| 9 | Caen | dod_caen | The British and Canadian battle for Caen | Jun-Jul 1944 | Planned |
| 10 | Falaise | dod_falaise | The Falaise Pocket | Aug 1944 | Planned |
| 11 | Market Garden | dod_escape | The 101st Airborne and the bridges of Operation Market Garden | Sep 1944 | Planned |
| 12 | The forest | dod_forest | The Hürtgen Forest | Sep 1944-Feb 1945 | Planned |
| 13 | The Bulge | dod_kalt, dod_kraftstoff | The Ardennes offensive: frozen towns, and the German army's need for fuel | Dec 1944-Jan 1945 | Planned |
| 14 | The airfields | dod_flugplatz | Allied troops reach the jet airfields in Germany | Mar-Apr 1945 | Planned |
| 15 | The Po valley | dod_vicenza | The last offensive in Italy | Apr 1945 | Planned |

Maps without a firm match (dod_chemille, dod_flash, dod_northbound, dod_sturm, dod_switch, dod_zalec) are
used for general scenes inside other episodes (street fighting, a village at dawn), never captioned as a
named place.

Facts that need special care, flagged before writing:
- dod_falaise's briefing mixes up the Falaise Gap with the Cotentin; the episode follows the sources, not
  the briefing.
- dod_escape's briefing puts the 101st on the Maas; their Market Garden sector was the Eindhoven-Veghel
  road ("Hell's Highway"). The narration names only what the sources say.
- dod_jagd has Cromwell tanks in its briefing; Villers-Bocage is best known for the Tiger ambush of 13 June.
  Keep the narration to what the sources support.

## How an episode is made

1. Pick the map's places with `tools/film/scout.py` (a host renderer of the real map), set anchors, and frame
   each shot until `film.py check` passes and the scout frames read.
2. Write narration from three or more sources per fact; write the fact check beside the episode.
3. ADR is generic and invented; no real person's words or voice. Foley cues use the game's own sounds.
4. Voice on ceres (`tools/film/voice.py render`), capture on ceres (`scripts/film-capture.sh`), one Dolphin
   at a time.
