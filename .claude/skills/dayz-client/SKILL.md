---
name: dayz-client
description: Launch and drive an automated DayZ client (DayZAutobot) against a throwaway local DayZ server - load mods under test on both sides, join, move/aim, pick up and use items through the game's real action keys, inspect the actions an object offers (and why one is blocked), spawn/teleport/heal on the server, run Enforce script on the server, take screenshots. Use whenever a task needs a running DayZ client or an in-game check of a DayZ mod (testing a mod, reproducing a bug, verifying client/server behaviour).
---

# Driving DayZ with DayZAutobot

Everything goes through `tools/autobot` at the root of the DayZAutobot repo (`~/Projects/DayZAutobot`).
It prints JSON; the exit code is 0 when `"ok": true`. Arguments are `key=value`.

The client is **isolated and invisible**: its own Proton prefix and `-profiles` folder under
`~/.local/share/DayZAutobot/instances/<name>/`, rendered into a headless KWin compositor (no window on
the desktop, no mouse grabs). It still runs as the user's Steam account, so:
- Steam must be running and logged in (`steam -silent &` starts it without a window).
- The user can't play DayZ while the bot runs (one account, one game).
- The game writes its profile (main-menu character per server, recent servers) through the Steam Cloud
  API as the user. `launch` snapshots `userdata/<id>/221100/remote` and the CLI restores it once the
  client has exited, so the user's files end up as they were.

## Typical session

```bash
A=tools/autobot                                     # from ~/Projects/DayZAutobot
$A server-start --mods ~/Projects/DayZMissions/build/@HandCart   # throwaway server, port 2402, ~60 s
$A launch --mods ~/Projects/DayZMissions/build/@HandCart --connect 127.0.0.1   # ~45-60 s, in game
$A status                                           # state: main_menu/loading/spawning/ingame/dead
$A server spawn type=HandCart_HandTruck_Red dist=1.5
$A actions target=HandCart_HandTruck_Red why=true   # what the game offers, and why the rest is blocked
$A action name=ActionTakeItemToHands target=HandCart_HandTruck_Red   # holds F like a player
$A screenshot width=960                             # returns a PNG path; Read it to look
$A server exec code='return GetGame().GetMission().ClassName();'      # any Enforce script, server side
$A cycle                                            # after changing a mod: rebuild, restart both, rejoin
$A kill; $A server-stop                             # when done
```

`tools/smoke_test.sh` exercises every command family (about 90 s) and starts what isn't running.

## Mods under test

- `--mods` takes comma-separated mod folders (built `@Name` dirs with `addons/`), Workshop ids, or
  `@Name` of a subscribed Workshop mod. Give the **same** `--mods` to `server-start` and `launch`
  (client mods must match the server's). `--server-mods` is for `-serverMod` (server only).
- The server doesn't check signatures (`verifySignatures = 0`) or run BattlEye, so unsigned builds load.
- After rebuilding a mod: `$A cycle` restarts the server and client with the same settings (they must
  load identical PBOs) and rejoins. Add `--mods` to change them.
- `--mission <folder>` overlays a mission folder (init.c, db/, ...) on the vanilla offline mission;
  `--mission enoch|sakhal|nasdara` picks a vanilla terrain; `--wipe` clears the throwaway persistence.
- Script compile errors fail `server-start`/`launch` immediately and are printed (`"log": [...]`).
- Logs: `$A log [--server] [-n 80] [--grep RE]` (newest script log), `$A paths` for every folder.

## Conventions

- `autobot <cmd>` goes to the client; `autobot server <cmd>` goes to the test server. Commands marked
  `[server]` below only work there.
- Objects are identified by network id `"low:high"` (same on client and server, shown as `id`) or a
  type spec (class name, base class, or part of the type/display name; nearest match wins).
- Positions are `[x, y, z]`, y up. `look yaw=` uses compass degrees: 0 north (+z), 90 east (+x).
- Long commands block until done (`connect` 240 s, `move_to` 120 s, `action` 60 s...); pass
  `timeout=` to change a command's budget. `stop` cancels everything and releases keys/movement.
- Movement and turning go through the game's input overrides, applied on the client and mirrored to
  the server (the server only accepts that from players when started with `-autobot`).
- Actions go through the real action system: `action` simulates the action's key (click, or hold for
  continuous actions like eating or bandaging; `hold=secs` caps it). `target=` forces the action
  target instead of relying on the crosshair (the server still checks range). Reach is 2 m:
  `move_to` the object first.

## Vehicles

```bash
$A server spawn type=OffroadHatchback x= z= full=true   # assembled; full fuel/oil/coolant
$A server vehicle_service type=OffroadHatchback         # repair everything, fill every fluid
$A server vehicle_place type=OffroadHatchback x= z= yaw=0   # move/unflip (crew stays aboard)
$A vehicle_parts target=OffroadHatchback   # per seat: entry point, door id/component/state; doors, hood, trunk
$A enter target=OffroadHatchback seat=0    # walks round to the door, opens it, holds F to get in (0 = driver)
$A action name=ActionStartEngine           # engine (bikes: ActionStartEngineMotorbike, boats: ActionStartEngineBoat)
$A action name=ActionSwitchLights          # headlights; horn: ActionVehicleHornShort / ActionVehicleHornLong hold=2
$A drive_to x= z= speed=40                 # steers, holds speed, shifts a manual box, stops at the end
$A drive throttle=0.5 steer=0.3 gear=2 secs=3   # raw controls (steer + = right; gear R/N/D/P/1..)
$A stop_vehicle                            # brake to a standstill (a boat: throttle off, neutral, drift)
$A action name=ActionStopEngine
$A action name=ActionSwitchSeats seat=1    # seat= picks a seat component of the vehicle you're in
$A leave                                   # hold F to get out; opens the door from inside first if shut
$A action name=ActionOpenCarDoorsOutside target=<door id> component=0   # doors/hood/trunk from vehicle_parts
$A vehicle                                 # speed, rpm, gear, controls, fluids, crew per seat, parts
```

- Doors, seats, engine, lights, horn, pushing, getting in and out are the game's own actions;
  driving controls are fed to the vehicle in `OnInput`, as vanilla's debug autopilot does.
- Rear seats need the front seat tipped (`ActionAnimateSeats` on it) and the door open, like a player.
- Cars and boats drive. **Motorbikes (1.30) don't move under scripted throttle**: getting on, the
  engine, lights and horn work, but the engine ignores script throttle (rider input only).
- A boat can't be boarded while swimming: put it in shallow water (`vehicle_place`) and board from
  wading depth. It won't shift into reverse while moving, so it stops by drifting.
- Vehicles have physics: an unbraked car on a slope rolls when something wakes it. The north-west
  airfield (about x 4040-4160, z 10400-10840) is flat concrete for driving tests.
- There's no pathfinding: `move_to`/`enter`/`drive_to` go in straight lines (enter steers round the
  vehicle it's entering) and give up after 8 s without progress.
- Network ids change when the server restarts; refer to vehicles by type after a restart.
- Get out before killing or respawning the player: dying in a vehicle and respawning crashed both
  the 1.30 client and the server (engine bug).

## Commands

Client: `status`, `wait state=`, `player`, `inventory [depth=]`, `nearby [radius= type= kind=]`,
`target`, `actions [target= seat=|component= why=true]`, `action name= [target= seat=|component= hold=]`,
`key name=UAAction [hold=|release=true]`, `keys`, `vehicle`, `vehicle_parts`, `drive`, `drive_to`,
`stop_vehicle`, `enter` and `leave` (CLI),
`look yaw=/x= z=/at=`, `move [dir= speed= secs=]`, `move_to x= z=`, `stance value=`,
`take item=`, `hands item=|none`, `drop [item=]`, `respawn`, `chat text=`, `connect`, `disconnect`,
`quit`, `hud show=`, `ui`, `click name=|text=`, `menu [close=true|open=]`, `screenshot [width= hud=false]`.

Server: `players`, `sv_player`, `spawn type= [at=ground|hands|inventory] [count= full=true quantity=]`,
`vehicle_service`, `vehicle_place`,
`teleport x= z=`, `heal`, `god [on=]`, `kill [id=]`, `delete id=|type=`, `time hour=`,
`weather preset=`, `exec code=|file=`.

Both: `ping`, `help [name=]`, `jobs`, `stop`. `$A help` prints every usage line from the game.

## Gotchas

- A tap (`action` on a click-type input) and a hold (hold-type input) are kept apart as in the game:
  F tapped takes an item into the inventory, F held takes it to the hands. A held key is let go as soon
  as the action ends, or the game would chain the next one (get in -> get out).
- `exec` scripts must keep expressions short: one that fails with "Formula too complex" crashes the
  whole server (an engine bug). Split long string concatenations over several `+=` lines. `out` and
  `local` are reserved words.
- `exec` only works on the server (and an offline game): the retail client refuses to compile script
  ("LoadScript can't be called on Retail Client"). `exec code=` takes statements (add `return "...";`
  to get a value back) or a full `string AB_Run()`; compile errors come back in `"log"`.
- The engine recompiles the Mission script module on every join/leave. Autobot's core lives in the
  Game module and most commands in World, so they survive; a Mission-module command running across
  a join/leave fails with a clear error instead.
- A rejoin right after `disconnect` can bounce while the server still holds the old session (logout
  timer); `connect` retries for 60 s (`retry_secs=`).
- DayZ under Proton sometimes dies at startup with "Out of memory (requested 0 KB)"; `launch` detects
  the crash dump and relaunches (up to 3 tries, reported as `retried_after`).
- Spawned items can land inside props (hay bales, rocks) or tall grass; `target=` sidesteps aiming.
- Text in `actions` is localised; match actions by class name (`ActionTakeItem`) or the English text.
- The game ignores scripted keys for inputs that aren't action inputs (mods reading
  `GetUApi().GetInputByName(...)` directly), and can't be steered by real keyboard/mouse at all.
- Never point this at a public server or the user's VPS: it's for the local throwaway server.
