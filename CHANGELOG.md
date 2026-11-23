# Changelog

## 0.2.1 - 2026-11-24

- CLI: `-h`/`--help` after a command prints the help instead of running the command (`cycle --help`
  used to rebuild and restart everything), an unknown `--option` is an error instead of being
  ignored, and local commands (`cycle`, `launch`, `server-start`...) reject stray `key=value` arguments.

## 0.2.0 - 2026-11-24

- Vehicles: `vehicle` (speed, rpm, gear, controls, fluids, crew per seat, parts), `vehicle_parts`
  (per seat: where to stand, door id/component/state; every door, the hood and the trunk), `drive`
  (raw throttle/brake/steering/handbrake/gear), `drive_to` (steers, holds a speed, shifts a manual
  gearbox, stops), `stop_vehicle`, and on the server `vehicle_service` (repair, fill fluids) and
  `vehicle_place` (move/unflip, crew aboard). The CLI's `enter` walks round to a seat's door, opens it
  and gets in; `leave` gets out, opening the door from inside first if it's shut. Cars and boats drive; 1.30 motorbikes ignore scripted throttle.
- `actions`/`action` take `seat=` and `component=` to target vehicle seats, doors and parts; `why=true`
  checks the forced target.
- Fixed: a simulated tap also triggered the hold action on the same key (tapping F on a car door pushed
  the car; F on an item took it to the hands); a held key now lets go when its action ends instead of
  chaining the next one (get in, get out...); timed holds (`hold=`) never let go, because the per-frame
  hook had silently stopped running (`typename.Spawn()` objects are freed); headlights/horn/NVG weren't
  listed; `move_to` circled points it couldn't settle on (it now side-steps the last 2 m and gives up
  after 8 s without progress); searches by x/z ignored terrain height.
- `action` keeps looking for its action for up to 3 s (`settle=`): right after an item goes to the
  hands the game offers nothing for about a second.
- New `keys` command (simulated presses held right now). The client is capped at 60 fps.

## 0.1.1 - 2026-11-23

- The command-line tool now ships with the mod (`extras/autobot` in the Workshop download) and runs
  from there: it loads the downloaded mod folder as is instead of building it.
- Finds DayZ, DayZ Server, Proton and Workshop mods in any Steam library (not just one Steam folder),
  and falls back to Valve's Proton when no GE-Proton is installed.
- The Workshop page explains where the tool is and how to use it.

## 0.1.0 - 2026-11-23

First version.

- Mod: agent polling a localhost bridge through RestApi, jobs spanning frames, results as JSON;
  inert without `-autobot=<port>`. Core in the Game module, commands in World/Mission with
  registrars, so joins/leaves (Mission-module reloads) don't break it.
- Client commands: status/wait, connect (with retries)/disconnect/quit, player, inventory, nearby,
  target, actions (with forced `target=` and `why=true` diagnostics), action (simulated click/hold of
  the action keys), key, look, move, move_to, stance, take, hands, drop, respawn, chat, hud, ui,
  click, menu.
- Server commands: players, sv_player, spawn, teleport, heal, god, kill, delete, time, weather, exec.
- Movement/turning mirrored client → server by one RPC the server only accepts with `-autobot`.
- CLI: build (+ optional signing), launch (own Proton prefix, headless KWin, fresh DayZ.cfg, crash
  retry), kill, server-start/stop (throwaway mission, own config/ports/shard), cycle, join, ps,
  paths, log, screenshots, exec with compile-error capture, Steam Cloud snapshot/restore.
- `tools/smoke_test.sh`: 44 end-to-end checks.
