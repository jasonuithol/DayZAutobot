# Changelog

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
