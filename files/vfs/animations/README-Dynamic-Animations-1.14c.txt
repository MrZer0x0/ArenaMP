ArenaMP native integration of OpenMW Dynamic Animations 1.14c
================================================================

Source mod: OpenMW Dynamic Animations 1.14c by Taitechnic.

ArenaMP is based on OpenMW 0.47 and does not contain the OpenMW 0.49+
client Lua API used by the original .omwscripts package. The Lua scripts are
therefore not installed. Compatible KF/YAML/NIF resources are mounted through
ArenaMP's built-in VFS and the relevant behaviour is implemented in the native
ArenaMP animation controllers.

Integrated behaviour
--------------------

* The Z player animation menu contains a Walking style group.
* Classic, Dirnae, female and soldier walk cycles are selected only when the
  current player skeleton actually contains the requested group.
* The selected walking style is synchronized over TES3MP's existing
  ID_PLAYER_ANIM_PLAY channel and periodically refreshed for players entering
  the loaded-cell area.
* First-person walk, run and sneak animation sources are selected natively when
  available.
* Standard NPCs receive deterministic male, female, beast, noble and guard
  walking profiles. NPCs with custom Construction Set animation models keep
  their original animations.
* Ambient and dialogue NPC animation pools include the additional compatible
  idle and gesture groups.
* Weapon, spell, shield and torch controllers retain ownership of the upper
  body while a custom walking style is active.

Configuration
-------------

The feature has no separate in-game Animations tab. Its engine settings remain
in settings.cfg under [GUI]:

dynamic actor locomotion = true
dynamic first person locomotion = true

Existing ArenaMP interaction animations, persistent player poses, book/scroll
props, movement cancellation rules and multiplayer packet formats are retained.
