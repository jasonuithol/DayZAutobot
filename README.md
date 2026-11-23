# DayZ Autobot

A DayZ mod plus a command-line launcher for fully automating a DayZ client and a throwaway local
server, so that Claude Code (or any script) can build and test DayZ mods without a human at the
keyboard. It's the DayZ counterpart of AutoHeim (Valheim).

- launch an isolated, invisible client (own Proton prefix, own profile, headless compositor)
- start a throwaway dedicated server with the mods under test (no BattlEye, no signature checks)
- join, leave, respawn; rebuild and restart everything with one command (`cycle`)
- move, turn, aim, change stance; walk to a point
- see what the game offers: the actions on the crosshair target or any object, and *why* an action
  isn't offered (which condition fails)
- do actions through the game's own action system (simulated F / left mouse, click or hold)
- take, hold, drop and stash items
- vehicles: get in and out (walking round to the right door and opening it), switch seats, doors, hood
  and trunk, engine, lights, horn; drive cars and boats (`drive_to` steers, holds a speed, shifts a manual
  gearbox and stops), read speed, rpm, gear, fluids and crew; service, move and unflip vehicles
- server helpers: spawn anything (on the ground, in hands, in inventory), teleport, heal, god mode,
  kill, delete, time of day, weather
- run arbitrary Enforce script on the server (`exec`)
- read player state, inventory trees, nearby objects; take screenshots; read script logs
- list and click menu buttons (mod UIs too), show/hide the HUD

## How it works

```
tools/autobot (Python CLI) ── HTTP ──▶ bridge (localhost:7879, started on demand)
                                          ▲                      ▲
                                long-poll │ RestApi              │ RestApi
                                          │                      │
                         DayZ client (Proton, headless KWin)   DayZServer (Linux, throwaway mission)
                         + @DayZAutobot + mods under test      + @DayZAutobot + mods under test
```

- **The mod** (`DayZAutobot/`, Enforce Script) is inert unless the game was started with
  `-autobot=<port>`. When enabled, it long-polls the bridge with the game's own RestApi (an HTTP
  client), runs each command as a job on the main thread (spanning frames when it needs to) and
  posts the JSON result back. The client and the server each poll for their own commands, so the
  CLI drives both directly.
- **Movement and aiming** use the game's input overrides (`HumanInputController.Override*`). Those
  only act on the machine that sets them, so the client mirrors its drive state to the server with
  one RPC, which the server only accepts when it runs `-autobot` (so the mod can't be used to cheat
  on a normal server).
- **Vehicles** are driven through `Transport.OnInput`, where vanilla's own debug autopilot sets
  throttle, steering, brakes and gears; everything else (doors, seats, engine, lights, horn) is the
  game's actions with a forced target component (`seat=`/`component=`). Car and boat physics run on the
  driver's client, so the client-side controls are what count (they're mirrored to the server anyway).
- **Actions** use the real action system: the mod feeds simulated presses of the action inputs
  (UAAction = F, UADefaultAction = left mouse) into `ActionInput.Update`, and can force the action
  target through the game's own `ForceActionTarget` hook. Conditions, targeting and the client/server
  action flow are all the game's.
- **Code layout across script modules.** The engine recompiles the Mission module whenever a mission
  starts or ends, which leaves anything created by Mission code stale. So the core (polling, jobs,
  registry) lives in the Game module and most commands in World; only UI/HUD/respawn commands live in
  Mission, and they're re-registered after each reload.
- **Isolation.** The client runs from its own Proton prefix with its own `-profiles` folder (video
  settings, key bindings, logs), inside a headless KWin compositor with its own Xwayland and D-Bus:
  no window on your desktop, no mouse grabs. Screenshots come from that compositor. The throwaway
  server uses its own mission (`mpmissions/autobot.<terrain>`), config (`serverDZ.autobot.cfg`),
  ports (2402/27116) and profile folder; the vanilla `serverDZ.cfg` and missions are left alone.

## Requirements

- Linux with KDE Plasma 6 (`kwin_wayland --virtual` and `spectacle` for the headless client),
  ImageMagick, Python 3.8+
- Steam running and logged in, with DayZ and DayZServer installed (`~/.steam/debian-installation`;
  override with `STEAM=`), and a GE-Proton in `compatibilitytools.d` (`PROTON=` to pick one)
- Optional: `~/Projects/DayZMissions/tools/sign_pbo.sh` and the `tarbaby` key to sign the PBO

## Workshop

Published as Workshop item 3844648914 (public). The download includes the CLI as
`extras/autobot`; run from there (`python3 <library>/steamapps/workshop/content/221100/3844648914/extras/autobot`)
it loads that mod folder as is and finds the games, Proton and Workshop mods in any Steam library.
The Workshop page (`DayZAutobot/workshop.txt`) carries the usage instructions; it must not link here.
Release: tag `vX.Y.Z` (matching `AB_VERSION` and a `CHANGELOG.md` entry), push, then from DayZMissions
(where `mods/DayZAutobot` links to `DayZAutobot/`, git-excluded): `tools/workshop_publish.sh DayZAutobot <login>`
(the user runs it: Steam Guard), Steam > Go Online, `tools/workshop_tags.py DayZAutobot Mod`.

## Usage

```bash
tools/autobot build                       # pack DayZAutobot/ into build/@DayZAutobot
tools/autobot server-start [--mods a,b] [--mission DIR|TERRAIN] [--wipe]
tools/autobot launch [--mods a,b] [--connect 127.0.0.1]
tools/autobot status
tools/autobot server spawn type=Canteen dist=1.5
tools/autobot actions target=Canteen
tools/autobot action name=ActionTakeItem target=Canteen
tools/autobot screenshot width=960
tools/autobot cycle                       # rebuild, restart server + client, rejoin
tools/autobot help                        # every game command with usage
tools/autobot kill; tools/autobot server-stop
```

The full command reference, conventions and gotchas are in
[`.claude/skills/dayz-client/SKILL.md`](.claude/skills/dayz-client/SKILL.md), which Claude Code loads
as the `dayz-client` skill. `tools/smoke_test.sh` runs every command family end to end.

### Adding commands from a mod under test

A mod can register its own commands without depending on Autobot at runtime: in its 4_World scripts,

```c
#ifdef DayZAutobot
class MyMod_Cmd_State : AB_Command
{
	override bool Start()
	{
		c.R.Int("charges", MyMod.GetCharges());
		return true;
	}
}

modded class AB_WorldCommands
{
	override void Register(AB_Registry reg)
	{
		super.Register(reg);
		reg.Add("mymod_state", MyMod_Cmd_State, "mymod_state - charges left");
	}
}
#endif
```

`DayZAutobot` is defined whenever the Autobot mod is loaded. `Start()` returning false keeps the
command running; `Tick(dt)` is then called every frame until it returns true. Use `c.Str/Num/Int/Bool`
for arguments, `c.R` for the result and `return Fail("...")` for errors. Don't name a method `Step()`:
the engine never calls script methods with that name.

## Lessons baked into the code

- **Client input overrides don't reach the server** in 1.30: the server simulates from the raw input
  it receives, so a client-only override moves the player locally and gets snapped back. Hence the
  mirrored drive state. `GetGame().IsClient()` is false on a multiplayer client; use `!IsServer()`.
- **`ScriptModule.LoadScript` is blocked on the retail client** ("can't be called on Retail
  Client"), and `MakeScreenshot` writes nothing there. `exec` works on the server; screenshots come
  from the compositor.
- **`modded class` only works inside the module that defines the class**, so the registry is extended
  with registrar classes found by name (`typename.ToType()`).
- **The Mission module is recompiled on every join/leave**; objects created from it are freed. A
  leftover reference turned into a null that threw ~70,000 exceptions.
- **Enforce gotchas:** `!obj.Method()` inside `||` evaluates wrongly when the method returns an
  object/array (compare with `null`); `foreach` over a method call's result is unreliable (use a
  local); a method named `Step()` is never called; `TextWidget` has no text getter; there's no
  ternary operator.
- **The action manager runs at the simulation rate, slower than frames.** Waiting "3 frames" for a
  forced target isn't enough; wait on its update counter. Only the four standard action inputs take a
  forced target; their subclasses (vehicle lights, horn, NVG) throw on a non-vehicle one, aborting the
  whole action update.
- **Xvfb doesn't work:** it reports a 0 Hz refresh rate and the game divides by it. A nested
  `kwin_wayland --virtual` gives a 60 Hz output, and NVIDIA Vulkan presents to its Xwayland; X11 can't
  read those frames (white), so screenshots go through Spectacle on the nested session's D-Bus.
- **With `-profiles`, video settings live in `<profiles>/Users/<Windows user>/DayZ.cfg`**, not
  Documents. A desktop-sized window saved there made the next headless start die with "Out of
  memory"; the launcher writes a fresh one every launch.
- **The client writes its profile through Steam Cloud as you**, keyed by server `shardId`; the test
  server uses its own shard (`abtest`) and the launcher snapshots/restores the cloud folder.
- **`typename.Spawn()` objects die even when held in `ref` fields.** The registrars keep a static ref
  to themselves; before that their per-frame hook silently never ran.
- **Taps and holds share keys** (F = Interact tap / ContinuousInteract hold). A simulated tap must only
  reach tap inputs, or tapping a car door also starts pushing the car; a held key must be let go the
  moment its action ends, or the game chains the next action on it (get in, get out, get in...).
- **Vehicles:** getting in needs the door open and the player within 1 m of the seat's door point;
  door actions target the door attachment itself. 1.30 motorbikes ignore scripted throttle (cars and
  boats don't); boats can't be boarded while swimming and won't shift into reverse while moving.
- **A script that fails with "Formula too complex" crashes the server** when `exec` loads it.
- `HumanInputController.OverrideMovementAngle` is in degrees, positive to the right; car steering is
  positive to the right too.
- The test server's process is named `enfMain`; so is the Wine client. Network ids change when the
  server restarts. The headless client is capped at 60 fps (`-limitFPS`), or it renders ~550.

## Changelog

See [CHANGELOG.md](CHANGELOG.md).
