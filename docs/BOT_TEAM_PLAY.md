# Bot team play: tracking players down

Builds on the waypoint graph (A*), the GOAP planner and the fair perception rules (view cone, line of sight, reaction
time, aim error, hearing, memory). This adds what makes a match feel like players hunting each other rather than bots
walking between points. Logic lives in `src/dod_bots.c` (host tested in `tests/dod_bots_test.c`), wiring in `src/dod_bot.cpp`.

* **Team intel** (`DodTeamIntel`, 2 teams x 6 contacts, about 300 bytes of static data). A bot that sees an enemy, hears
  one, or whose mate dies posts a contact for its team. Contacts merge within 250 units and fade (6 s sighting, 5 s sound, 20 s death).
  Nothing is a wallhack: a position only exists because a bot perceived it (or died there).
* **Answering a call-out.** A bot with no lead of its own may adopt the best contact (fresh, strong, near beats old, weak, far)
  between 300 and 2400 units away. `Dod_BotAggression` gives every bot a fixed 0..1 temperament (hash of slot and skill), and
  `Dod_BotAnswersIntel` turns it into a chance that falls with distance, is halved for defenders and reduced for death
  call-outs. Some bots hunt, some hold; the team spreads out. The adopted position feeds the existing HUNT goal and SEEK step.
* **Search.** Arriving at a lead with nobody in sight no longer means standing there: the bot sweeps up to two waypoints near the lost
  position (`Dod_BotPickPatrol` with a 700 unit radius) before it gives the lead up and clears the team contact.
* **Roam.** With no objective and no lead (team deathmatch maps, or every point held and no role to play) a bot walks to a waypoint
  towards the fight (freshest contact, else the nearest objective it does not hold, else the centre of the graph) that it has not
  visited lately and no mate is heading for.

Not done yet, noted for later: grenade use, peeking and cover-to-cover advance, per-bot skill variance beyond temperament.

Perf and memory: bot state grows by about 40 bytes per bot; the pick scans 8 candidate nodes and runs at most once per 0.5 s think.

Evidence to capture on ceres (`~/Desktop/xash3d-gc/output/media/`): a TDM-style run on a map with bots only roaming and
converging on a firefight; a bot arriving at a lost contact and sweeping. Not yet Dolphin-verified.
