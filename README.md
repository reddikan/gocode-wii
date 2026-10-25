# GO CODE

A pixel-art squad tactics shooter for the Nintendo Wii (homebrew), played with the Classic Controller in Dolphin or on a real Wii.

Lead a four-operator hostage rescue team through ten night-time missions. Plan every operator's route on the floor plan, stack them on the doors, then call the **GO code**: charges blow, flashbangs pop and the team storms in. Missions take a minute or two, and your progress saves to the SD card after each one.

## The squad

| Operator | Role | Weapon | Ability |
| --- | --- | --- | --- |
| **VEGA** | Point | Assault rifle | **Flashbang** - blinds everyone in the room for a few seconds |
| **KNOX** | Breacher | Shotgun | **Breach charge** - blows a door, a locked door or a cracked wall (stuns whoever is behind it) |
| **PIKE** | Recon | Silenced SMG (quiet - doesn't raise the alarm) | **Drone** - in planning, look inside a room before you go in |
| **MAE** | Medic | Pistol | **Smoke** - blocks everyone's sight; she also revives downed teammates on her own |

## How it plays

1. **Plan.** Pick an operator (ZL / ZR) and lay down waypoints with **A** - they find their own way between them. On a waypoint, **Y** makes them *wait for the GO code* and **X** sets their ability there (a flash or smoke lands where the cursor is; Knox breaches the door or cracked wall next to him). Pike's drone (X) scans the room under the cursor straight away. Press **+** and choose *Start the assault*.
2. **Stack up.** The squad moves out and shoots on sight. When everyone who is waiting for GO is in position, the music builds and *ALL IN POSITION* flashes.
3. **GO.** Press **X**. Charges blow first, flashbangs go in, then everyone moves at once. Take out the guards before they hurt the hostages, reach the hostages to secure them, and neutralize every hostile.

You can take over any operator mid-fight by pushing the left stick (right stick aims; they still shoot on their own), and **A** hands them back to the plan. **R** freezes the action so you can change the plan.

A guard who sees you, or hears the fight close by, starts a countdown over their hostage - kill, flash or breach on them before it runs out. If the alarm goes on too long, every guard panics.

Each mission awards up to three stars: complete it, nobody goes down, and beat the par time. Retrying after a failure keeps your plan so you can tweak it.

## Missions

1. **Safehouse** - a small house, two operators, and full intel. The tutorial.
2. **Corner Store** - Pike joins; a reinforced storage door.
3. **Motel 9** - Mae joins; two hostages in two units, thin walls between rooms.
4. **Office Floor** - corner office and a reinforced server room.
5. **Warehouse 17** - a big floor with shelving that blocks sight.
6. **Neon Room** - a nightclub with a pulsing dance floor; VIP lounge and office.
7. **Embassy Row** - the ambassador behind reinforced double doors; breach in from the side.
8. **Penthouse** - a rooftop suite with long sight lines.
9. **First City Bank** - the vault is reinforced on both sides.
10. **The Mansion** - three hostages, one in a locked safe room.

## Controls (Classic Controller)

| Button | Planning | Assault |
| --- | --- | --- |
| Left stick / D-Pad | move the cursor | take control of the selected operator |
| Right stick | | aim (while in control) |
| A | add a waypoint | hand the operator back to the plan |
| B | undo the last waypoint | |
| Y | wait for GO on the last waypoint | use the selected operator's ability now |
| X | ability at the last waypoint | **call the GO code** |
| ZL / ZR | choose operator | choose operator |
| L | zoom the planning view | camera: whole squad / selected operator |
| R | | tactical pause - change the plan |
| + | menu (start the assault) | pause (music / sound, restart, abort) |
| - | 16:9 / letterboxed 4:3 | |
| HOME | exit | |

## Saving

Progress (unlocked missions, stars, best times, music / sound settings) is saved to `sd:/apps/gocode/save.dat`. In Dolphin this needs the virtual SD card: *Config > Wii > SD Card > Insert SD Card* (on by default in recent versions). The title screen says whether saving is working.

## Building

With devkitPro installed: `make` builds `boot.dol` (open it in Dolphin) and an `apps/gocode/` folder for a real Wii's SD card.

Pushed to GitHub, the Actions workflow builds `boot.dol` in the devkitPro container and uploads it as the `gocode-wii` artifact.

## Tests

`sh tools/run_tests.sh` builds a native test harness (any C compiler; Pillow for PNG export) that checks every map, renders screenshots through a software copy of the GPU path (including the lighting pass), and plays every mission with a planning bot and a careless bot. `sh tools/run_tests.sh reel 6` renders a preview video of mission 6 with its music and sound (needs ffmpeg).

The maps are built by `tools/maps.py` (rooms, doors, props and actors placed by coordinates), which writes `source/maps.inc`.

## How it is made

- Everything is drawn as coloured triangles by the GPU. The characters are pixel-art sprites painted at start-up from simple shapes, outlined automatically and stored as runs, so each run is one quad that can turn freely - crisp pixels that still rotate smoothly.
- Lighting is a half-tile light grid rebuilt every frame from lamps, fluorescent tubes (some flicker), neon, street lights, moonlight through the windows, muzzle flashes, explosions and every operator's flashlight, with walls casting shadows. It is drawn as a 2x multiply over the whole scene, so lights can over-brighten as well as darken; an additive pass adds the glows, flashlight beams in the air, laser sights, tracers, sparks and rain.
- Unexplored rooms stay a dim blueprint until someone sees into them (or the drone does).
- Every sound is synthesized at start-up: layered gunshots (crack, body and room tail), the breach, the flashbang and its ear ringing, glass, doors, footsteps, and the squad's radio callouts ("Breaching!", "Flash out!", "Contact!", "Tango down", "Hostage secure", "Go go go!") from a tiny formant speech synthesizer squeezed through a radio band-pass with a squelch.
- The music is composed and synthesized at start-up as three layers in C minor that loop in sync: a tension bed, a pressure layer (pulsing bass, string tremolo, heartbeat) that comes in as the squad moves and swells when everyone is in position, and an assault layer (drums, driving bass, brass stabs) that slams in on GO and whenever shots are fired, then drains away.
