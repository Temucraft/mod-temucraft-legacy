# mod-temucraft-legacy

Account-wide Legacy points from challenges (class levels 30/60/80, profession skill 150/300/450, honorable kills 100/1000/10000, exploration, dungeon and raid achievements), spent on perks per character with `.legacy` (a gossip menu): Well Rested, Working Overtime, Bountiful Harvest, Reinforce, Diplomat, Field Guide, Thrill of Adventure, Swift Flights (flight paths 5% faster per rank; the speed-up itself is done by [mod-temucraft-travel](https://github.com/Temucraft/mod-temucraft-travel)). A rewards track gives items at 15/25/40/55 points.

## Install

1. Put this folder in `modules/` of your AzerothCore source, re-run CMake and build.
2. The SQL in `data/sql/` is applied by the worldserver on its next start (AzerothCore's module updater). Every file deletes its own rows before inserting them, so running one again is safe.
   Copy `conf/mod-temucraft-legacy.conf.dist` to your `configs/modules/` as `mod-temucraft-legacy.conf` to change its settings.
3. No client changes: it works with an unmodified 3.3.5a client.

## What it adds, and where

| Database | Table | IDs |
|---|---|---|
| characters | `mod_temucraft_legacy_challenge` | per account: challenges done |
| characters | `mod_temucraft_legacy_perk` | per character: perks taken |
| characters | `mod_temucraft_legacy_reward` | per account: rewards claimed |
| (script) | gossip menu | 601630 (made by the script, nothing in the database) |

## Works with other modules

- Rewards are existing items: 46725, 49343, 40643, 49283.
- Challenges read stock achievements 42-45, 1283-1286, 1288, 685-686, 697, 4396.

## Independence

This module doesn't need any other module, nor any other Temucraft module. Everything is in its script and its three characters tables.

## Customising IDs

The entries above are a block reserved for this module. Nothing to clash with in the world database.

## Texts and translations

The 19 player-facing texts (chat messages, gossip options) are in the `module_string` table under the module name
`mod-temucraft-legacy`, so they can be changed without rebuilding. Translations go in `module_string_locale` with the same
module and id plus the locale (`deDE`, `esES`, `frFR`, ...). If a row is missing the English text in the code is used.

Part of the Temucraft modules for AzerothCore 3.3.5a.
