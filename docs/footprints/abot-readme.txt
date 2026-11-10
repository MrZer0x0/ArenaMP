Footprints

a MWSE-Lua Morrowind mod by abot

Caveat:

Footprints placement may not always be accurate.

As this mod uses MWSE-Lua addSound() event to detect vanilla feet sound and trigger footprints spawning,
In theory some other MWSE-Lua mod could use the same event with higher priority and replace/block the vanilla
sounds before this mod could detect them.

In case you have things not working due to this kind a conflict, increasing in the MCM Panel the event priority used by this mod should solve it.


ChangeLog:

v 1.16
- footprint meshes should look better at nightime
- code tweaks and fixes

v 1.15
- more tweaks and fixes

v 1.14
- made footprint meshes single-face, halving used triangles. Could improve FPS in theory
- more tweaks and fixes

v 1.13
- improved footprint puffs effect color matching with surface texture
- more tweaks and fixes

v 1.12
- more tweaks & fixes

v 1.11
- more tweaks & fixes, landscape footprints are now attached to VFX root

v 1.10
- tweaks & fixes, better smooth timed fadeout of footprints

v 1.09
- fixed previous fix breaking another thing

v1.08
- fixed footprint scaling for some rescaled creatures

v1.07
- small tweaks

v1.06
- added option to try detecting and skipping staircases when placing footprints

v1.05
- added rat tailored footprints
- fixed footprint scaling when attaching footprints to rescaled meshes
- some more code tweaks

v1.04
- footprints now automatically fade out from alpha value set in MCM as time goes by until they are fully deleted
- some more code tweaks

v1.03
- fixed some bugs
- footprint puffs effect color should better match surface color
- added tracking time and deletion management for each footprint
- as alpha setting is global for each mesh and all its clones and can't be used for this,
added footprints are slowly sinking according to time passed effect instead
- added global footprints alpha/transparency setting for better matching of different texture replacers
- added setting for max real time duration in seconds for each footprint
- added setting for max number of spawned footprints
- added setting for compatibility with mods eventually allowing beast races to wear visible boots/shoes.
Beast races have bare feet by default.

v1.02
- improved precision and speed, less memory usage
- added option to get dirty feet after waterwalking
- added guar tailored footprints
- added footprint puffs effect on proper surfaces e.g. snow, sand, ash...

v1.01
- improved speed and memory usage
- changed duration of footprints parameter to simulated game hours (takes timescale into account)

Note:
Although this may seem like a purely immersion/cosmetic mod, I spent a long time trying to include some gameplay in it.
A smart usage of dirty/wet surfaces could allow you to leave footprints for a while e.g. inside a rocky dungeon, and use
them as hint for places already visited.
