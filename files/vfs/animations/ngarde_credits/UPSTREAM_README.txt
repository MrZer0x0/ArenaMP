
# N'Garde: OpenMW Lua Active Block and Weapon Parry
- No, this is not a 100% hit-chance mod. 
- No, this is not a M&B like directional combat system
- Please read on for further details


KCD/Tainted Grail/TES4/TES5 inspired modern-ish active block and parry system for OpenMW Morrowind.

Native block chance(which only affects shields) fully disabled. Design idea being that this may have been in the base game, had the original developers had another year or two to cook.


# Installation:
    - Place the unpacked directory in a convenient location.
    - Add the path to the uncompressed directory in OpenMW launcher "Data Directories" tab
    - Enable provided ngarde.omwscripts file
    - If you're still running openmw 0.50 for whatever reason - download and enable ngarde.omwaddon file, otherwise you don't need it, but if you keep it when updating it won't hurt anything either
    - If you're running NCG - enable ngardencgdescription.omwaddon file. This will make new block skill description match NCG.
    - If you're running Skill Evolution - update to 1.9.8 or above for N'Garde interoperability. 
    - If you want parry sparks and weapon sheathing to work on Almalexia - download and enable the secondary omwaddon file.
    - Enable "Use Additional Animation Sources" setting in the launcher
    - Enable "smooth animation transitions" setting in the launcher
    - Enable "smooth movement" setting in the launcher. This one and one above will prevent jitter on NPCs in combat
    - Launch the game, go to "settings"->"scripts"->"N'Garde" and assign a parry hotkey
    - If after assigning the hotkey nothing happens when pressing it - try saving and reloading the game

# Active shield block and weapon parry:
    - Player vs NPC, NPC vs NPC.
    - Native block chance for shields fully disabled 
    - Parry enemy melee attacks with shields or weapons
    - Block enemy arrows with shields
    - Melee combat distance keeping. Enemies will try to stay at their weapon range and try to avoid obstacles and ledges behind them.
    - Can only parry/block attacks from enemies that you are facing - parry arc is different by weapon type
    - Negate magical component of point enchanted weapon's damage if the attack is parried successfully
    - Negate magical component of AOE enchanted weapon's damage (disabled by default, magic VFX will still play)
    - NPCs can parry player and each other, NPCs can recognize threats, and parry attacks with "human-like" reaction time
    - Includes skeletons, dremora and other creatures that can wield weapons
    - Weapons differ in parry/defence effectiveness, shield and gauntlets vary in effectiveness by armor class
    - To prevent NPC turtling they have an internal parry cool-down
    - Higher skilled enemies will have lower cool-down, but will cower behind their guard less
    - Move speed penalty per weapon type when parry is raised
    - NPCs windup attacks for slightly longer to better allow for parry
    - NPCs try and keep their distance at the edge of their weapon range
    - Perfect parry and "Weak" parry. Once parry is raised - defendant has small perfect parry window.
    - Perfect Parry becomes possible at configurable skill level, after which window length is determined by skill levels.
    - If an attack is parried during that window - all damage is negated and the opponent is staggered
    - Affected by block skill + "parry tool" skill (block for shield, or related weapon skill)
    - Once window ends - parry becomes "weak", and amount of damage it is able to negate is determined by skill levels
    - If an NPC perfect parries player, they attempt a counter attack
    - Low skill enemies will have lower chance of even attempting to parry. This is additionally affected by current fatigue.
# Stagger:
    - From perfect parry, or regular hits. While staggered NPCs and Player can't attack or parry
    - Stagger cool-down of 0.5 to 3.0 seconds based on endurance, strength and block skill, higher is better.
    - While staggered - player or NPC can't attack - this means that any attack that was charged but not released - will be released as soon stagger starts. Melee or ranged - can't really hold a bowstring drawn or a weapon raised while getting hit.
# HandToHand block and Iron Palm:
    - Can parry h2h attacks with bare hands, and can parry weapons while wearing armored gauntlets/bracers
    - "Iron Palm" Becomes feasible at configurable skill level. Parry enemy weapon attacks with your bare hands.
    - Live out that DnD monk fantasy
    - Block with your hands if you get into a fight while holding "non-suitable items" such as throwing weapons, lockpicks or probes
# Arrow Blocking
    - Shields can always block arrows, and offer best protection.
    - NPCs faced with a ranged threat will "guess" on when you're going to release the shot and will try to raise their guard in time. Or react to the shot itself and scramble to protect themselves. Whether they will be successful depends on distance and projectile speed.
    - Weapon Arrow deflection: At very high skill level both NPCs and Player are able to deflect incoming arrows/bolts/throwing weapons with a weapon. Skill requirement is 125 in weapon skill or 90 in _both_ weapon skill and block. NPCs that don't meet skill requirements won't even try. 
    - Only perfect parry is capable of deflecting a projectile without a shield. 
    - Greatly recomend installing Bullseye for this one.
# Miss Feedback:
    - In combat when a humanoid actor "dodges" an attack via MW under the hood diceroll system natively nothing happens
    - N'Garde adds 11 short "bob,weave and dodge" animations that don't interrupt flow of combat but allow visual feedback telling player that "your attack was not effective"
    - Only applies to humanoid NPCs at this time. 
# Optional - Glancing Blows:
    - "Glancing Blows" mode, disabled by default. Preserves vanilla hit chance mechanic, but - a missed attack becomes a "glancing blow" deals a maximum of 20% damage and doesn't grant skill XP to the attacker. Time-to-kill is roughly the same, just better combat feedback.
    - If "strength affects h2h damage" is enabled in launcher - missed h2h attacks lose strength bonus, but instead only get reduced to 40% damage.
    - Takes "normal weapon resistance" and armour into account.
    - While disabled, attacks that are missed can still be parried or perfect parried to stagger the opponent.
# Optional - Quick Mode:
    - Normally actor is considered "parrying" as soon as parry animation end. 
    - Quick mode changes that to the start of parry animation. If you are playing on mobile, on very low fps, or generally struggling with parrying enemy attacks - this one may be for you

## Compatibility
# Playtested and compatible with following combat mods:
- [Better Armor Training](https://www.nexusmods.com/morrowind/mods/59863) - this is a recommendation. With N'Garde you will get hit less, so armor skills will progress slower. This alleviates that.
- [Mercy](https://www.nexusmods.com/morrowind/mods/55064)
- [HBFS](https://www.nexusmods.com/morrowind/mods/55542)
- [Natural Character Growth](https://www.nexusmods.com/morrowind/mods/57801) Load additional ngardencgdescription.omwaddon if you're running NCG for matching block skill description.
- [Skill Evolution](https://www.nexusmods.com/morrowind/mods/57802) Needs 1.9.8+ version of SE, otherwise N'Garde may break in a way where player can't parry but NPCs can. Full integration with Skill Evolution feats
- [Take Cover](https://www.nexusmods.com/morrowind/mods/54976)
- [Friendlier Fire](https://www.nexusmods.com/morrowind/mods/57975)
- [Iron Fist](https://www.nexusmods.com/morrowind/mods/57619D)
- [GRIP](https://www.nexusmods.com/morrowind/mods/57640)
- [Maxar's Timed Attack](https://www.nexusmods.com/morrowind/mods/55344)
- [Disarm Them All](https://www.nexusmods.com/morrowind/mods/58430) compatible, with interop. on perfect parry disarm is not possible. Load N'Garde after Disarm them all
- [Gothic Style Knockout](https://www.nexusmods.com/morrowind/mods/58576) now compatible, thanks to the fix submitted by Foxunder
- [Dual Wielding](https://www.nexusmods.com/morrowind/mods/57839)
- [Patient Defence](https://www.nexusmods.com/morrowind/mods/59915)- by Vitruvian Guar. Mod softly relies on N'Garde for longer NPC Attack windup to allow active dodging
- [Spellstrike](https://www.nexusmods.com/morrowind/mods/59812)- by Vitruvian Guar - we have some interop to allow for Spellstrike windup to override N'Grade windup

# Not yet tested but made generally compatible
- [CHIM 2090](https://github.com/DreamWeave-MP/S3ctors-S3cret-St4sh/releases/tag/development) - by S3ctor, When N'Garde is installed it will take over active blocking and parrying while CHIM will retain the rest of its functionality for those of us that want different style of combat in MW. Get the development release for latest build.

# Fully replaces:
- Shields UP, and similar active blocking mods.
# Not Compatible
- Better bodies v.3.2 - Minor incompatibility, wrist positions during parry animations cause wrists to look extra thin. I recommend VSBR, but otherwise - I will take a look if I can update animations for compatibility at some point

# Generally
- Seems to be widely compatible and was written in a way that ensures that if another mod is not doing the same thing, and not doing anything silly - N'Garde should work. Load N'Garde after other mods that add onHit handlers in your load order


## Documentation
See wiki https://gitlab.com/Arrean/openmwluaparry/-/wikis/Home
### Interop:
- [Check out the wiki](https://gitlab.com/Arrean/openmwluaparry/-/wikis/Home/Interop-Hooks)

## Acknowledgements

- S3ctor, for guiding me to some of the implementation details and ideas (check out CHIM 2090). And for script context checker code.
- Ownlyme, for the raycast script, even if I hacked it to pieces. As well as for slider settings renderer 
- SaintJ, for 3rd person H2HBlock Animation
- Dubiousnpc - for the assist with animation work. Specifically goblin shield block, Almalexia parry. And npc miss-dodge animations.(And maybe more to come)
- Cybvep - skill effect formula testing and design
- Greatness7 for the nif of additional bones used to spawn vfx
- Axemagister author of MGE XE mod "Impact Sounds" for the sparks VFX
- Xerxse for inspiration and legacy shield Raised animation.
- Entirety of OpenMW discord server for fun times, support along the way and extensive play testing
- SFX By Dragon-Studio and Freesound-community on pixabay

### Localisation:
- Ukrainian - Arrean
- French - Billie
- German - Semaro, Yenster