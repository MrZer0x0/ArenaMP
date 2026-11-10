ArenaMP built-in third-person alternate animation layer

Source supplied by the project maintainer as:
  openmw-third-person-alt-attacks-master-animations.zip

Integration rules:
- xbase_anim: normal humanoid third-person skeletons
- xbase_animkna: beast-race third-person skeletons
- werewolf/custom skeletons: not overridden
- ReAnimation v3 remains the canonical attack-selection source and first-person layer
- ArenaMP maps canonical *Alt groups to the pack's *1 names only at render time
- the synchronized Attack packet therefore remains camera-independent

Do not rename the source groups in the KF files; nativeanimation.hpp owns the alias map.
