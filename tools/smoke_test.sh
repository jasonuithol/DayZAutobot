#!/bin/bash
# End-to-end check of every command family against the throwaway server.
# Starts the server and the client if they aren't running (Steam must be running), then drives
# them. Leaves both running; `tools/autobot server-stop` and `tools/autobot kill` stop them.
#   tools/smoke_test.sh
cd "$(dirname "$0")/.." || exit 1
A=tools/autobot
PASS=0
FAIL=0
FAILED=()

# check <name> <expected substring|-> <command...>
check() {
	local name="$1" want="$2"
	shift 2
	local out
	out="$("$@" --raw 2>&1)"
	if echo "$out" | grep -q '"ok": true' && { [ "$want" = - ] || echo "$out" | grep -q -- "$want"; }; then
		PASS=$((PASS + 1))
		printf '  ok    %s\n' "$name"
	else
		FAIL=$((FAIL + 1))
		FAILED+=("$name")
		printf '  FAIL  %s\n        %s\n' "$name" "$(echo "$out" | head -c 400)"
	fi
}

echo "== setup"
running=$($A ps --raw)
if ! echo "$running" | grep -q '"server": {"running": true'; then
	check "server-start" '"state": "server"' $A server-start --wipe
fi
if ! echo "$running" | grep -q '"client": {"running": true'; then
	check "launch" '"state": "main_menu"' $A launch
fi
state=$($A status --raw)
if ! echo "$state" | grep -q '"state": "ingame"'; then
	echo "$state" | grep -q '"state": "dead"' && $A respawn --raw >/dev/null
	echo "$state" | grep -q '"state": "main_menu"' && check "join" '"state": "ingame"' $A join
fi

echo "== general"
check "ping (client)"     '"side": "client"' $A ping
check "ping (server)"     '"side": "server"' $A server ping
check "help"              '"commands"'       $A help
check "status"            '"state": "ingame"' $A status
check "server exec"       'CustomMission'    $A server exec code='return GetGame().GetMission().ClassName();'
check "screenshot"        '.png'             $A screenshot width=640 name=smoke

echo "== server helpers"
check "players"           'Autobot'          $A server players
check "sv_player"         '"blood"'          $A server sv_player
check "heal"              '"healed"'         $A server heal
check "god on"            '"god": true'      $A server god
check "god off"           '"god": false'     $A server god on=false
check "time"              '"hour": 12'       $A server time hour=12
check "weather"           '"clear"'          $A server weather preset=clear
check "teleport"          '"pos"'            $A server teleport x=11950 z=3420
sleep 2
check "spawn on ground"   'Canteen'          $A server spawn type=Canteen dist=1.5
check "spawn in inventory" 'Apple'           $A server spawn type=Apple at=inventory

echo "== observation"
check "player"            '"stance"'         $A player
check "inventory"         'Apple'            $A inventory
check "nearby"            'Canteen'          $A nearby radius=5
check "target"            '"actions"'        $A target

echo "== control"
check "look yaw"          '"yaw"'            $A look yaw=90
check "look at"           'Canteen'          $A look at=Canteen
check "actions (target=)" 'ActionTakeItem'   $A actions target=Canteen
check "take (tap F)"      '"started"'        $A action name=ActionTakeItem target=Canteen
check "it's carried"      'Canteen'          $A inventory
check "hands"             'Canteen'          $A hands item=Canteen
check "drink (hold LMB)"  'ActionDrink'      $A action name=ActionDrink hold=3
check "drop"              'Canteen'          $A drop
check "take to hands (hold F)" '"started"'   $A action name=ActionTakeItemToHands target=Canteen
check "hands holds it"    '"hands": {"type": "Canteen"' $A player
check "drop again"        'Canteen'          $A drop
check "take"              'Canteen'          $A take item=Canteen
check "hands (apple)"     'Apple'            $A hands item=Apple
check "hands none"        -                  $A hands item=none
check "stance crouch"     '"stance"'         $A stance value=crouch
check "stance erect"      '"stance"'         $A stance value=erect
check "move"              '"moved"'          $A move secs=1 speed=2
check "move_to"           '"dist"'           $A move_to x=11955 z=3430
check "stop"              '"stopped"'        $A stop
check "server delete"     '"deleted"'        $A server delete type=Canteen radius=30

echo "== vehicles (north-west airfield)"
check "teleport"          '"pos"'            $A server teleport x=4104 z=10560
sleep 3
check "spawn car"         'OffroadHatchback' $A server spawn type=OffroadHatchback x=4112 z=10560 full=true
check "place car"         '"heading"'        $A server vehicle_place type=OffroadHatchback x=4112 z=10560 yaw=0
check "service car"       '"fuel": 1'        $A server vehicle_service type=OffroadHatchback
check "vehicle_parts"     '"door_id"'        $A vehicle_parts target=OffroadHatchback
check "enter (driver)"    '"my_seat": 0'     $A enter target=OffroadHatchback seat=0
check "start engine"      '"started"'        $A action name=ActionStartEngine
check "lights on"         '"started"'        $A action name=ActionSwitchLights
check "lights are on"     '"lights_on": true' $A vehicle
check "horn"              -                  $A action name=ActionVehicleHornLong hold=1
check "drive_to"          '"dist"'           $A drive_to x=4112 z=10640 speed=30
check "drive reverse"     '"gear": "R"'      $A drive gear=R throttle=0.4 secs=2
check "stop_vehicle"      '"speed_kmh"'      $A stop_vehicle
check "engine off"        '"started"'        $A action name=ActionStopEngine
check "lights off"        '"started"'        $A action name=ActionSwitchLights
check "switch seat"       '"started"'        $A action name=ActionSwitchSeats seat=1
check "in seat 1"         '"my_seat": 1'     $A vehicle
check "leave"             '"pos"'            $A leave
sleep 1
check "remove car"        '"deleted"'     $A server delete type=OffroadHatchback radius=150

echo "== menus, death and reconnect"
check "server kill"       '"killed"'         $A server kill
sleep 2
check "respawn"           '"state": "ingame"' $A respawn
check "disconnect"        '"main_menu"'      $A disconnect
check "ui (main menu)"    'MainMenu'         $A ui
check "join"              '"state": "ingame"' $A join
check "hud off"           -                  $A hud show=false
check "hud on"            -                  $A hud show=true

echo
echo "passed $PASS, failed $FAIL"
[ "$FAIL" -eq 0 ] || { printf '  failed: %s\n' "${FAILED[@]}"; exit 1; }
