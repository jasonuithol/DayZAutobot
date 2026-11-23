# DayZ Autobot

Automation mod for the DayZ client and a throwaway server, plus its CLI. To *use* it, follow the
`dayz-client` skill (`.claude/skills/dayz-client/SKILL.md`). This file is for changing it.

- Mod source: `DayZAutobot/` (packed by `tools/autobot build` into `build/@DayZAutobot`, signed with the
  tarbaby key when DayZMissions' `sign_pbo.sh` is available).
  - `Scripts/3_Game/DayZAutobot/AB_Core.c`: agent (RestApi polling, jobs, results), `AB_Command`, `AB_Ctx`,
    registry and registrars, `AB_State`, general commands, the `DayZGame.OnUpdate` hook. `AB_Json.c`: JSON writer.
  - `Scripts/4_World/DayZAutobot/`: `AB_Input.c` (simulated action keys, the drive state mirrored to the
    server by RPC, action-manager diagnostics), `AB_GameCommands.c` (status/wait/connect/disconnect/quit/exec
    and the `AB_WorldCommands` registrar), `AB_PlayerCommands.c`, `AB_ServerCommands.c`.
  - `Scripts/5_Mission/DayZAutobot/AB_MissionCommands.c`: hud/ui/click/menu/respawn (`AB_MissionCommands`).
- CLI: `tools/autobot` (Python, no dependencies): bridge, build, launch/kill, server-start/stop, cycle,
  Steam Cloud snapshot/restore, screenshots (Spectacle on the headless KWin), exec, logs.
- New command: a class extending `AB_Command` (`Start()`, optional `Tick(dt)`/`Cancel()`/`DefaultTimeout()`),
  added in a `Register*` function with a usage line. Put it in 4_World unless it needs GUI/mission classes:
  the Mission module is recompiled on every join/leave. Add a line to `tools/smoke_test.sh` and to the
  skill's command list. Bump `AB_VERSION` (AB_Core.c) and add a `CHANGELOG.md` entry.
- After editing: `tools/autobot cycle` (rebuild, restart server and client, rejoin; ~90 s), then
  `tools/smoke_test.sh` (~90 s, 44 checks). Check `tools/autobot log --grep "Virtual Machine"` stays empty.
- Vanilla scripts for reference: unpack `~/.steam/debian-installation/steamapps/common/DayZ/dta/scripts.pbo`
  with `~/Projects/DayZMissions/tools/unpbo.py` into the session scratchpad and grep there.
- Gotchas that cost real time (details in README "Lessons"): `modded class` only within the defining
  module; Mission-module objects die on join/leave; `!x.Method() || ...` and `foreach` over a call result
  misbehave; never name a method `Step`; client input overrides are local only; `IsClient()` is false on a
  MP client; the action manager ticks slower than frames; force targets only on the 4 standard inputs;
  `LoadScript` is blocked on the retail client; Xvfb crashes the game (0 Hz).
- Never touch the user's real game: the bot has its own prefix, profiles and display; the server its own
  mission/config/ports/shard. Steam Cloud files are snapshotted at launch and restored after exit.
  Don't connect the bot to the user's VPS or public servers.
- Version lives in `AB_VERSION` (AB_Core.c), history in `CHANGELOG.md`; bump both and tag `vX.Y.Z` together.
- The Workshop download ships `tools/autobot` as `extras/autobot` (a symlink in `DayZAutobot/extras/`, copied
  at build). Keep it working "installed" (`INSTALLED` = it sits in a mod folder): no repo paths, no build.
  Workshop text is `DayZAutobot/workshop.txt` (no GitHub links, no double quotes); releasing: README "Workshop".
  Only the user uploads (Steam Guard); never run workshop_publish.sh yourself.
