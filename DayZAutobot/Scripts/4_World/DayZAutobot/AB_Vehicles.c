// Vehicles: cars and trucks (CarScript), motorbikes (MotorbikeScript) and boats (BoatScript).
//
// Getting in/out, seats, doors, engine, lights and horn are the game's own actions: use the action
// command, with seat=/component= to pick a seat or door (AB_Target in AB_PlayerCommands.c).
// Driving controls (throttle, brake, steering, handbrake, gear) aren't actions: the engine reads them
// from the keyboard and hands them to Transport.OnInput, where scripts may override them (vanilla's own
// debug autopilot does). Like walking, an override only acts on the machine that sets it, so the
// driver's client mirrors its controls to the server by RPC (accepted only by a server started with
// -autobot, only from the driver's own client) and both sides apply them in OnInput.

const int AB_RPC_VEHICLE = 47110002;

class AB_Veh
{
	// the vehicle the player sits in, or null
	static Transport Of(PlayerBase p)
	{
		if (p == null || !p.IsInVehicle())
			return null;
		HumanCommandVehicle hcv = p.GetCommand_Vehicle();
		if (hcv == null)
			return null;
		return hcv.GetTransport();
	}

	// spec = id or type; empty = the nearest vehicle of any kind
	static Transport Near(vector center, float radius, string spec, Object ignore)
	{
		if (spec != "")
			return Transport.Cast(AB_Obj.FindNear(center, radius, spec, ignore));
		array<Object> objs = new array<Object>;
		GetGame().GetObjectsAtPosition(center, radius, objs, null);
		Transport best;
		float bestD = 1e9;
		foreach (Object o : objs)
		{
			Transport t = Transport.Cast(o);
			if (t == null)
				continue;
			float d = vector.Distance(center, o.GetPosition());
			if (d < bestD)
			{
				best = t;
				bestD = d;
			}
		}
		return best;
	}

	// the geometry component that the game maps to crew seat `seat` (what you aim at to get in there)
	static int ComponentForSeat(Transport t, int seat)
	{
		for (int ci = 0; ci < 512; ci++)
		{
			if (t.CrewPositionIndex(ci) == seat)
				return ci;
		}
		return -1;
	}

	// the component whose selections include `name` (case-insensitive), or -1
	static int ComponentNamed(Transport t, string name)
	{
		name.ToLower();
		for (int ci = 0; ci < 512; ci++)
		{
			TStringArray names = new TStringArray;
			t.GetActionComponentNameList(ci, names);
			foreach (string nm : names)
			{
				nm.ToLower();
				if (nm == name)
					return ci;
			}
		}
		return -1;
	}

	// Everything needed to get into a seat: where to stand (within 1 m of it), which door, its state.
	static AB_JObj SeatInfo(Transport t, int seat)
	{
		AB_JObj o = new AB_JObj;
		o.Int("seat", seat);
		int comp = ComponentForSeat(t, seat);
		o.Int("component", comp);
		Human h = t.CrewMember(seat);
		if (h != null)
			o.Str("occupant", AB_Obj.Id(h));
		o.Bool("can_get_through", t.CrewCanGetThrough(seat)).Bool("exit_area_free", t.IsAreaAtDoorFree(seat));
		vector entry;
		vector dir;
		t.CrewEntryWS(seat, entry, dir);
		CarScript car = CarScript.Cast(t);
		if (car != null)
		{
			TStringArray sel = new TStringArray;
			t.GetActionComponentNameList(comp, sel);
			foreach (string sn : sel)
			{
				string point = car.GetDoorConditionPointFromSelection(sn);
				if (point != "" && car.MemoryPointExists(point))
				{
					entry = car.ModelToWorld(car.GetMemoryPointPos(point));
					break;
				}
			}
			string slot = car.GetDoorInvSlotNameFromSeatPos(seat);
			if (slot != "")
			{
				o.Str("door_slot", slot);
				o.Int("door_component", ComponentNamed(t, slot));
				o.Str("door", typename.EnumToString(CarDoorState, car.GetCarDoorsState(slot)));
				// door actions (open/close) target the door attachment itself, on its own geometry
				EntityAI door = car.FindAttachmentBySlotName(slot);
				string doorSel = car.GetDoorSelectionNameFromSeatPos(seat);
				if (door != null)
				{
					o.Str("door_id", AB_Obj.Id(door));
					int dc = -1;
					for (int ci = 0; ci < 64 && dc < 0; ci++)
					{
						TStringArray dn = new TStringArray;
						door.GetActionComponentNameList(ci, dn);
						foreach (string d : dn)
						{
							if (d == doorSel)
								dc = ci;
						}
					}
					o.Int("door_entity_component", dc);
				}
			}
		}
		o.Vec("entry", entry);
		return o;
	}

	static float Speed(Transport t)
	{
		Car car = Car.Cast(t);
		if (car != null)
			return car.GetSpeedometer();
		Motorbike bike = Motorbike.Cast(t);
		if (bike != null)
			return bike.GetSpeedometer();
		// boats have no speedometer: km/h along the boat's heading
		return vector.Dot(GetVelocity(t), t.GetDirection()) * 3.6;
	}

	static bool EngineOn(Transport t)
	{
		Car car = Car.Cast(t);
		if (car != null)
			return car.EngineIsOn();
		Motorbike bike = Motorbike.Cast(t);
		if (bike != null)
			return bike.EngineIsOn();
		Boat boat = Boat.Cast(t);
		if (boat != null)
			return boat.EngineIsOn();
		return false;
	}

	// Controls that slow the vehicle down: brakes, or for a boat (no brakes) reverse thrust.
	// Returns false once it's (nearly) still.
	static bool Brake(PlayerBase d, Transport t)
	{
		float speed = Speed(t);
		if (Math.AbsFloat(speed) < 1)
			return false;
		if (t.IsInherited(BoatScript))
		{
			// no brakes, and it won't shift into reverse while moving: throttle off, neutral, drift
			d.AB_SetControls(0, 0, 0, 0, GearFromName(t, "N"));
			return Math.AbsFloat(speed) >= 2;
		}
		d.AB_SetControls(0, 1, 0, 0, -1);
		return true;
	}

	static bool Automatic(Transport t)
	{
		Car car = Car.Cast(t);
		if (car != null)
			return car.GearboxGetType() == CarGearboxType.AUTOMATIC;
		Motorbike bike = Motorbike.Cast(t);
		if (bike != null)
			return bike.GearboxGetType() == CarGearboxType.AUTOMATIC;
		return false;
	}

	// the gear (or automatic mode: P R N D) the vehicle is in, in the same numbering ShiftTo takes
	static int CurrentGear(Transport t)
	{
		Car car = Car.Cast(t);
		if (car != null)
		{
			if (car.GearboxGetType() == CarGearboxType.AUTOMATIC)
				return car.GearboxGetMode();
			return car.GetGear();
		}
		Motorbike bike = Motorbike.Cast(t);
		if (bike != null)
		{
			if (bike.GearboxGetType() == CarGearboxType.AUTOMATIC)
				return bike.GearboxGetMode();
			return bike.GetGear();
		}
		Boat boat = Boat.Cast(t);
		if (boat != null)
			return boat.GetGear();
		return -1;
	}

	static string GearName(Transport t, int g)
	{
		if (g < 0)
			return "";
		Boat boat = Boat.Cast(t);
		if (boat != null)
		{
			int n = boat.GetNeutralGear();
			if (g == n)
				return "N";
			if (g < n)
				return "R";
			return (g - n).ToString();
		}
		if (Automatic(t))
		{
			if (g == CarAutomaticGearboxMode.P)
				return "P";
			if (g == CarAutomaticGearboxMode.R)
				return "R";
			if (g == CarAutomaticGearboxMode.N)
				return "N";
			return "D";
		}
		if (g == CarGear.REVERSE)
			return "R";
		if (g == CarGear.NEUTRAL)
			return "N";
		return (g - CarGear.FIRST + 1).ToString();
	}

	// "R", "N", "D", "P" or a forward gear number -> what ShiftTo takes for this vehicle; -2 = invalid
	static int GearFromName(Transport t, string spec)
	{
		spec.ToUpper();
		Boat boat = Boat.Cast(t);
		if (boat != null)
		{
			int n = boat.GetNeutralGear();
			if (spec == "N")
				return n;
			if (spec == "R")
				return n - 1;
			if (spec == "D")
				return n + 1;
			int bg = spec.ToInt();
			if (bg > 0 && n + bg < boat.GetGearCount())
				return n + bg;
			return -2;
		}
		if (Automatic(t))
		{
			if (spec == "P")
				return CarAutomaticGearboxMode.P;
			if (spec == "R")
				return CarAutomaticGearboxMode.R;
			if (spec == "N")
				return CarAutomaticGearboxMode.N;
			if (spec == "D" || spec.ToInt() > 0)
				return CarAutomaticGearboxMode.D;
			return -2;
		}
		if (spec == "R")
			return CarGear.REVERSE;
		if (spec == "N")
			return CarGear.NEUTRAL;
		if (spec == "D")
			return CarGear.FIRST;
		int g = spec.ToInt();
		if (g <= 0)
			return -2;
		return CarGear.FIRST + g - 1;
	}

	static void Fluids(Transport t, AB_JObj o)
	{
		AB_JObj f = new AB_JObj;
		Car car = Car.Cast(t);
		Motorbike bike = Motorbike.Cast(t);
		Boat boat = Boat.Cast(t);
		array<string> names = {"fuel", "oil", "brake", "coolant"};
		for (int i = 0; i < 4; i++)
		{
			float cap = 0;
			float frac = 0;
			if (car != null)
			{
				cap = car.GetFluidCapacity(i);
				frac = car.GetFluidFraction(i);
			}
			else if (bike != null)
			{
				cap = bike.GetFluidCapacity(i);
				frac = bike.GetFluidFraction(i);
			}
			else if (boat != null)
			{
				cap = boat.GetFluidCapacity(i);
				frac = boat.GetFluidFraction(i);
			}
			if (cap > 0)
				f.Num(names[i], frac);
		}
		o.Obj("fluids", f);
	}

	static void Describe(Transport t, AB_JObj o)
	{
		o.Str("type", t.GetType()).Str("id", AB_Obj.Id(t)).Str("name", t.GetDisplayName()).Vec("pos", t.GetPosition());
		vector d = t.GetDirection();
		float yaw = Math.Atan2(d[0], d[2]) * Math.RAD2DEG;
		if (yaw < 0)
			yaw += 360;
		o.Num("heading", yaw).Num("up", vector.Dot(t.GetDirectionUp(), vector.Up));
		o.Num("speed_kmh", Speed(t)).Bool("engine_on", EngineOn(t)).Bool("lights_on", t.LightIsOn());
		o.Int("health_level", t.GetHealthLevel());
		if (GetGame().IsServer())
			o.Num("health", t.GetHealth("", ""));
		string kind = "car";
		Car car = Car.Cast(t);
		Motorbike bike = Motorbike.Cast(t);
		Boat boat = Boat.Cast(t);
		if (bike != null)
			kind = "motorbike";
		else if (boat != null)
			kind = "boat";
		o.Str("kind", kind);
		int gear = CurrentGear(t);
		o.Str("gear", GearName(t, gear)).Int("gear_index", gear);
		AB_JObj ctl = new AB_JObj;
		if (car != null)
		{
			o.Num("rpm", car.EngineGetRPM()).Num("rpm_redline", car.EngineGetRPMRedline());
			o.Str("gearbox", typename.EnumToString(CarGearboxType, car.GearboxGetType())).Int("gears", car.GetGearCount());
			ctl.Num("throttle", car.GetThrottle()).Num("brake", car.GetBrake()).Num("steering", car.GetSteering()).Num("handbrake", car.GetHandbrake());
			o.Int("wheels", car.WheelCountPresent()).Int("wheel_slots", car.WheelCount());
		}
		else if (bike != null)
		{
			o.Num("rpm", bike.EngineGetRPM()).Num("rpm_redline", bike.EngineGetRPMRedline());
			o.Str("gearbox", typename.EnumToString(CarGearboxType, bike.GearboxGetType())).Int("gears", bike.GetGearCount());
			ctl.Num("throttle", bike.GetThrottle()).Num("brake", bike.GetBrake()).Num("steering", bike.GetSteering()).Num("handbrake", bike.GetHandbrake());
			o.Bool("fallen", bike.IsFallen()).Num("kickstand", bike.GetKickstand()).Int("clutch", bike.GetClutch());
			o.Int("wheels", bike.WheelCountPresent()).Int("wheel_slots", bike.WheelCount());
		}
		else if (boat != null)
		{
			o.Bool("has_engine", boat.HasEngine()).Num("rpm", boat.EngineGetRPM()).Int("gears", boat.GetGearCount());
			ctl.Num("throttle", boat.GetThrottle()).Num("steering", boat.GetSteering());
		}
		o.Obj("controls", ctl);
		o.Int("oninput_calls", s_InputCalls).Int("scripted_inputs", s_Applied);
		Fluids(t, o);
		AB_JArr crew = new AB_JArr;
		for (int i = 0; i < t.CrewSize(); i++)
		{
			AB_JObj seat = new AB_JObj;
			seat.Int("seat", i);
			Human h = t.CrewMember(i);
			if (h != null)
			{
				seat.Str("id", AB_Obj.Id(h));
				if (h.GetIdentity())
					seat.Str("name", h.GetIdentity().GetName());
			}
			crew.Obj(seat);
		}
		o.Arr("crew", crew);
		// attachments (wheels, doors, battery, spark plug, radiator...) and what's in the trunk
		AB_JObj tree = AB_Obj.Tree(t, 2);
		o.Raw("parts", tree.Text());
	}

	static int s_InputCalls;	// OnInput calls seen on this machine (diagnostics)
	static int s_Applied;		// ...of which applied scripted controls

	// Applies the driver's scripted controls (both sides call this from OnInput).
	static void Apply(Transport t)
	{
		s_InputCalls++;
		PlayerBase d = PlayerBase.Cast(t.CrewDriver());
		if (d == null || !d.m_AB_VActive)
			return;
		s_Applied++;
		Car car = Car.Cast(t);
		if (car != null)
		{
			car.SetThrottle(d.m_AB_VThrottle);
			car.SetBrake(d.m_AB_VBrake);
			car.SetSteering(d.m_AB_VSteer);
			car.SetHandbrake(d.m_AB_VHandbrake);
			if (d.m_AB_VGear >= 0 && CurrentGear(t) != d.m_AB_VGear)
				car.ShiftTo(d.m_AB_VGear);
			return;
		}
		Motorbike bike = Motorbike.Cast(t);
		if (bike != null)
		{
			// kickstand up and clutch engaged, as vanilla's own debug autopilot does (a player's gear
			// change goes through an animation that handles the clutch; ShiftTo doesn't)
			bike.SetKickstand(0);
			bike.SetClutch(0);
			bike.SetThrottle(d.m_AB_VThrottle);
			bike.SetBrake(d.m_AB_VBrake);
			bike.SetSteering(d.m_AB_VSteer);
			bike.SetHandbrake(d.m_AB_VHandbrake);
			if (d.m_AB_VGear >= 0 && CurrentGear(t) != d.m_AB_VGear)
				bike.ShiftTo(d.m_AB_VGear);
			return;
		}
		Boat boat = Boat.Cast(t);
		if (boat != null)
		{
			boat.SetThrottle(d.m_AB_VThrottle);
			boat.SetSteering(d.m_AB_VSteer);
			if (d.m_AB_VGear >= 0 && boat.GetGear() != d.m_AB_VGear)
				boat.ShiftTo(d.m_AB_VGear);
		}
	}
}

modded class CarScript
{
	override void OnInput(float dt)
	{
		super.OnInput(dt);
		AB_Veh.Apply(this);
	}
}

modded class MotorbikeScript
{
	override void OnInput(float dt)
	{
		super.OnInput(dt);
		AB_Veh.Apply(this);
	}
}

modded class BoatScript
{
	override void OnInput(float dt)
	{
		super.OnInput(dt);
		AB_Veh.Apply(this);
	}
}

// The driver's scripted controls, kept on the player (on both sides).
modded class PlayerBase
{
	bool m_AB_VActive;
	float m_AB_VThrottle;
	float m_AB_VBrake;
	float m_AB_VSteer;
	float m_AB_VHandbrake;
	int m_AB_VGear = -1;		// what ShiftTo takes; -1 = leave the gear alone
	protected float m_AB_VSentAt = -1;
	protected string m_AB_VSent;

	void AB_SetControls(float throttle, float brake, float steer, float handbrake, int gear)
	{
		m_AB_VActive = true;
		m_AB_VThrottle = Math.Clamp(throttle, 0, 1);
		m_AB_VBrake = Math.Clamp(brake, 0, 1);
		m_AB_VSteer = Math.Clamp(steer, -1, 1);
		m_AB_VHandbrake = Math.Clamp(handbrake, 0, 1);
		m_AB_VGear = gear;
		AB_SyncControls(false);
	}

	void AB_ReleaseControls()
	{
		if (!m_AB_VActive)
			return;
		m_AB_VActive = false;
		m_AB_VThrottle = 0;
		m_AB_VBrake = 0;
		m_AB_VSteer = 0;
		m_AB_VHandbrake = 0;
		m_AB_VGear = -1;
		AB_SyncControls(true);
	}

	protected void AB_SyncControls(bool force)
	{
		if (!GetGame().IsMultiplayer() || GetGame().IsServer())
			return;
		// quantised so a closed-loop controller doesn't send a message every frame
		string state = m_AB_VActive.ToString() + " " + Math.Round(m_AB_VThrottle * 20) + " " + Math.Round(m_AB_VBrake * 20) + " " + Math.Round(m_AB_VSteer * 40) + " " + Math.Round(m_AB_VHandbrake * 20) + " " + m_AB_VGear;
		float now = GetGame().GetTickTime();
		if (!force && state == m_AB_VSent)
			return;
		if (!force && now - m_AB_VSentAt < 0.05)
			return;
		ScriptRPC rpc = new ScriptRPC();
		rpc.Write(m_AB_VActive);
		rpc.Write(m_AB_VThrottle);
		rpc.Write(m_AB_VBrake);
		rpc.Write(m_AB_VSteer);
		rpc.Write(m_AB_VHandbrake);
		rpc.Write(m_AB_VGear);
		rpc.Send(this, AB_RPC_VEHICLE, true, null);
		m_AB_VSentAt = now;
		m_AB_VSent = state;
	}

	override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
	{
		if (rpc_type != AB_RPC_VEHICLE)
		{
			super.OnRPC(sender, rpc_type, ctx);
			return;
		}
		// only a test server started with -autobot listens, and only to the player's own client
		if (!GetGame().IsServer() || !AB_Drive.ServerEnabled())
			return;
		if (sender == null || GetIdentity() == null || sender.GetId() != GetIdentity().GetId())
			return;
		bool active;
		float throttle, brake, steer, handbrake;
		int gear;
		if (!ctx.Read(active) || !ctx.Read(throttle) || !ctx.Read(brake) || !ctx.Read(steer) || !ctx.Read(handbrake) || !ctx.Read(gear))
			return;
		m_AB_VActive = active;
		m_AB_VThrottle = throttle;
		m_AB_VBrake = brake;
		m_AB_VSteer = steer;
		m_AB_VHandbrake = handbrake;
		m_AB_VGear = gear;
	}
}

// ---------------------------------------------------------------- commands

// vehicle [target=<id|type>] - the vehicle you're in (or a nearby one): speed, engine, gear, controls,
// fluids, crew per seat, parts
class AB_Cmd_Vehicle : AB_Command
{
	override bool Start()
	{
		Transport t;
		PlayerBase p = PlayerBase.Cast(GetGame().GetPlayer());
		if (c.Has("target") || p == null)
		{
			vector center = "0 0 0";
			if (p != null)
				center = p.GetPosition();
			else
			{
				// the server has no player of its own: search around the first player
				array<Man> players = new array<Man>;
				GetGame().GetPlayers(players);
				if (players.Count() > 0)
					center = players[0].GetPosition();
			}
			if (c.Has("x"))
				center = Vector(c.Num("x"), GetGame().SurfaceY(c.Num("x"), c.Num("z")), c.Num("z"));
			t = AB_Veh.Near(center, c.Num("radius", 30), c.Str("target"), p);
		}
		else
			t = AB_Veh.Of(p);
		if (t == null)
			return Fail("no vehicle (not in one; give target=<id|type>)");
		AB_Veh.Describe(t, c.R);
		if (p != null && AB_Veh.Of(p) == t)
			c.R.Int("my_seat", t.CrewMemberIndex(p));
		return true;
	}
}

// vehicle_parts [target=] - the vehicle's action components: which seat each belongs to and their
// selection names (doors, hood, trunk...), to pass as component= to actions/action
class AB_Cmd_VehicleParts : AB_PlayerCommand
{
	override bool PlayerStart()
	{
		Transport t = AB_Veh.Of(P);
		if (c.Has("target") || t == null)
			t = AB_Veh.Near(P.GetPosition(), c.Num("radius", 10), c.Str("target"), P);
		if (t == null)
			return Fail("no vehicle nearby");
		AB_JArr list = new AB_JArr;
		for (int ci = 0; ci < 512; ci++)
		{
			TStringArray names = new TStringArray;
			t.GetActionComponentNameList(ci, names);
			int seat = t.CrewPositionIndex(ci);
			if (names.Count() == 0 && seat < 0)
				continue;
			AB_JObj o = new AB_JObj;
			o.Int("component", ci);
			if (seat >= 0)
				o.Int("seat", seat);
			AB_JArr n = new AB_JArr;
			foreach (string nm : names)
				n.Str(nm);
			o.Arr("selections", n);
			list.Obj(o);
		}
		c.R.Str("type", t.GetType()).Str("id", AB_Obj.Id(t)).Int("seat_count", t.CrewSize()).Arr("components", list);
		AB_JArr seats = new AB_JArr;
		for (int i = 0; i < t.CrewSize(); i++)
			seats.Obj(AB_Veh.SeatInfo(t, i));
		c.R.Arr("seats", seats);
		// every door-type part (doors, hood, trunk): open/close them with
		// action name=ActionOpenCarDoorsOutside|ActionCloseCarDoorsOutside target=<id> component=<component>
		CarScript car = CarScript.Cast(t);
		AB_JArr doors = new AB_JArr;
		for (int ai = 0; ai < t.GetInventory().AttachmentCount(); ai++)
		{
			CarDoor door = CarDoor.Cast(t.GetInventory().GetAttachmentFromIndex(ai));
			if (door == null)
				continue;
			InventoryLocation il = new InventoryLocation;
			door.GetInventory().GetCurrentInventoryLocation(il);
			string slot = InventorySlots.GetSlotName(il.GetSlot());
			AB_JObj d = new AB_JObj;
			d.Str("slot", slot).Str("type", door.GetType()).Str("id", AB_Obj.Id(door));
			if (car != null)
				d.Str("state", typename.EnumToString(CarDoorState, car.GetCarDoorsState(slot)));
			for (int dc = 0; dc < 64; dc++)
			{
				TStringArray dn = new TStringArray;
				door.GetActionComponentNameList(dc, dn);
				if (dn.Count() > 0)
				{
					d.Int("component", dc).Str("selection", dn[0]);
					break;
				}
			}
			doors.Obj(d);
		}
		c.R.Arr("doors", doors);
		return true;
	}
}

// drive [throttle=0..1] [brake=0..1] [steer=-1..1] [handbrake=0..1] [gear=R|N|D|P|1..] [secs=1] [keep=false]
// Holds those controls (as the driver) for secs, then lets go (keep=true: until stop / another drive).
class AB_Cmd_Drive : AB_PlayerCommand
{
	Transport m_T;
	float m_Secs;

	override float DefaultTimeout()
	{
		return 600;
	}

	override bool PlayerStart()
	{
		m_T = AB_Veh.Of(P);
		if (m_T == null || m_T.CrewDriver() != P)
			return Fail("not in a driver's seat (get in with: action name=ActionGetInTransport target=<vehicle> seat=0)");
		int gear = -1;
		if (c.Has("gear"))
		{
			gear = AB_Veh.GearFromName(m_T, c.Str("gear"));
			if (gear == -2)
				return Fail("gear: R, N, D, P (automatic) or a forward gear number");
		}
		P.AB_SetControls(c.Num("throttle", 0), c.Num("brake", 0), c.Num("steer", 0), c.Num("handbrake", 0), gear);
		m_Secs = c.Num("secs", 1);
		if (c.Bool("keep", false))
			return Report();
		return false;
	}

	bool Report()
	{
		AB_Veh.Describe(m_T, c.R);
		return true;
	}

	override bool PlayerTick(float dt)
	{
		if (c.Elapsed < m_Secs)
			return false;
		P.AB_ReleaseControls();
		return Report();
	}

	override void Cancel()
	{
		if (P)
			P.AB_ReleaseControls();
	}
}

// drive_to x= z= [speed=25 km/h] [radius=6] [stop=true] - drive there in a straight line: steers,
// keeps the speed, shifts a manual gearbox, and brakes to a stop at the end. Fails when stuck.
class AB_Cmd_DriveTo : AB_PlayerCommand
{
	Transport m_T;
	vector m_Goal;
	float m_Speed;
	float m_Radius;
	bool m_Arrived;
	float m_Best = 1e9;
	float m_SinceProgress;
	float m_Stopping;
	float m_StuckFor;
	float m_ShiftWait;
	int m_Gear;

	override float DefaultTimeout()
	{
		return 300;
	}

	override bool PlayerStart()
	{
		m_T = AB_Veh.Of(P);
		if (m_T == null || m_T.CrewDriver() != P)
			return Fail("not in a driver's seat");
		if (!c.Has("x") || !c.Has("z"))
			return Fail("x= and z= required");
		if (!AB_Veh.EngineOn(m_T) && !m_T.IsInherited(BoatScript))
			return Fail("the engine is off (action name=ActionStartEngine)");
		m_Goal = Vector(c.Num("x"), 0, c.Num("z"));	// flat: compared with the vehicle position at y 0
		m_Speed = c.Num("speed", 25);
		m_Radius = c.Num("radius", 6);
		m_Gear = AB_Veh.GearFromName(m_T, "D");
		return false;
	}

	// manual gearbox: up near the redline, down when slow (vanilla's debug autopilot does the same)
	int PickGear(float speed, float throttle, float dt)
	{
		if (AB_Veh.Automatic(m_T) || m_T.IsInherited(BoatScript))
			return m_Gear;
		m_ShiftWait -= dt;
		float rpm = 0;
		float redline = 1;
		int count = 6;
		Car car = Car.Cast(m_T);
		Motorbike bike = Motorbike.Cast(m_T);
		if (car != null)
		{
			rpm = car.EngineGetRPM();
			redline = car.EngineGetRPMRedline();
			count = car.GetGearCount();
		}
		else if (bike != null)
		{
			rpm = bike.EngineGetRPM();
			redline = bike.EngineGetRPMRedline();
			count = bike.GetGearCount();
		}
		if (m_Gear < CarGear.FIRST)
			m_Gear = CarGear.FIRST;
		if (m_ShiftWait > 0)
			return m_Gear;
		if (rpm > redline * 0.8 && throttle > 0.1 && m_Gear < count - 1)
		{
			m_Gear++;
			m_ShiftWait = 1;
		}
		else if (rpm < redline * 0.35 && m_Gear > CarGear.FIRST)
		{
			m_Gear--;
			m_ShiftWait = 1;
		}
		return m_Gear;
	}

	override bool PlayerTick(float dt)
	{
		vector pos = m_T.GetPosition();
		vector flat = Vector(pos[0], 0, pos[2]);
		float dist = vector.Distance(flat, m_Goal);
		float speed = AB_Veh.Speed(m_T);
		if (!m_Arrived && dist <= m_Radius)
			m_Arrived = true;
		if (m_Arrived)
		{
			m_Stopping += dt;
			bool wantStop = c.Bool("stop", true);
			bool slowing = false;
			if (wantStop)
				slowing = AB_Veh.Brake(P, m_T);
			if (m_Stopping > 20)
				slowing = false;
			if (!slowing)
			{
				P.AB_ReleaseControls();
				c.R.Num("dist", dist);
				AB_Veh.Describe(m_T, c.R);
				return true;
			}
			return false;
		}
		// steer toward the goal (positive steering = right)
		float err = AB_Steer.YawBetween(m_T.GetDirection(), m_Goal - flat);
		float steer = Math.Clamp(err * 1.5, -1, 1);
		// slow down for sharp turns and near the end (tight enough not to orbit the goal)
		float want = m_Speed;
		if (Math.AbsFloat(err) > 0.6)
			want = Math.Min(want, 10);
		want = Math.Min(want, Math.Max(6, dist * 1.2));
		// no progress for 20 s (orbiting the goal, blocked): give up
		if (dist < m_Best - 1)
		{
			m_Best = dist;
			m_SinceProgress = 0;
		}
		else
			m_SinceProgress += dt;
		if (m_SinceProgress > 20)
		{
			P.AB_ReleaseControls();
			AB_Veh.Describe(m_T, c.R);
			return Fail("no progress for 20 s, " + dist.ToString() + " m short (orbiting or blocked)");
		}
		float throttle = 0;
		float brake = 0;
		if (speed < want)
			throttle = Math.Clamp((want - speed) * 0.08 + 0.25, 0, 1);
		else if (speed > want + 6)
			brake = Math.Clamp((speed - want) * 0.05, 0, 1);
		P.AB_SetControls(throttle, brake, steer, 0, PickGear(speed, throttle, dt));
		// stuck: pushing on but barely moving
		if (throttle > 0.3 && Math.AbsFloat(speed) < 2 && c.Elapsed > 4)
			m_StuckFor += dt;
		else
			m_StuckFor = 0;
		if (m_StuckFor > 5)
		{
			P.AB_ReleaseControls();
			AB_Veh.Describe(m_T, c.R);
			return Fail("stuck " + dist.ToString() + " m short (blocked, wrong gear, or out of fuel?)");
		}
		return false;
	}

	override void Cancel()
	{
		if (P)
			P.AB_ReleaseControls();
	}
}

// stop_vehicle - brake to a standstill, then let go of everything
class AB_Cmd_StopVehicle : AB_PlayerCommand
{
	Transport m_T;

	override float DefaultTimeout()
	{
		return 60;
	}

	override bool PlayerStart()
	{
		m_T = AB_Veh.Of(P);
		if (m_T == null || m_T.CrewDriver() != P)
			return Fail("not in a driver's seat");
		if (AB_Veh.Brake(P, m_T))
			return false;
		P.AB_ReleaseControls();
		AB_Veh.Describe(m_T, c.R);
		return true;
	}

	override bool PlayerTick(float dt)
	{
		if (AB_Veh.Brake(P, m_T) && c.Elapsed < 30)
			return false;
		P.AB_ReleaseControls();
		AB_Veh.Describe(m_T, c.R);
		return true;
	}
}

// [server] vehicle_service [id=|type=] [radius=20] - repair everything and fill every fluid
class AB_Cmd_VehicleService : AB_ServerCommand
{
	override bool Run()
	{
		Transport t = FindVehicle();
		if (t == null)
			return Fail("no vehicle found (id= or type=, near the player)");
		t.SetFullHealth();
		array<EntityAI> parts = new array<EntityAI>;
		t.GetInventory().EnumerateInventory(InventoryTraversalType.PREORDER, parts);
		foreach (EntityAI part : parts)
		{
			if (part != t && part.GetHierarchyParent() == t)
				part.SetFullHealth();
		}
		for (int i = 0; i < 4; i++)
		{
			Car car = Car.Cast(t);
			Motorbike bike = Motorbike.Cast(t);
			Boat boat = Boat.Cast(t);
			if (car != null && car.GetFluidCapacity(i) > 0)
				car.Fill(i, car.GetFluidCapacity(i));
			if (bike != null && bike.GetFluidCapacity(i) > 0)
				bike.Fill(i, bike.GetFluidCapacity(i));
			if (boat != null && boat.GetFluidCapacity(i) > 0)
				boat.Fill(i, boat.GetFluidCapacity(i));
		}
		t.Synchronize();
		AB_Veh.Describe(t, c.R);
		return true;
	}

	Transport FindVehicle()
	{
		if (c.Has("id"))
			return Transport.Cast(AB_Obj.ById(c.Str("id")));
		vector center;
		PlayerBase p = FindPlayer();
		if (c.Has("near_x"))
			center = Vector(c.Num("near_x"), GetGame().SurfaceY(c.Num("near_x"), c.Num("near_z")), c.Num("near_z"));
		else if (p != null)
			center = p.GetPosition();
		return AB_Veh.Near(center, c.Num("radius", 20), c.Str("type"), p);
	}
}

// [server] vehicle_place id=|type= x= z= [yaw=0] - put a vehicle there upright and still (unflip, reposition)
class AB_Cmd_VehiclePlace : AB_Cmd_VehicleService
{
	override bool Run()
	{
		Transport t = FindVehicle();
		if (t == null)
			return Fail("no vehicle found");
		if (!c.Has("x") || !c.Has("z"))
			return Fail("x= and z= required");
		float x = c.Num("x");
		float z = c.Num("z");
		vector pos = Vector(x, GetGame().SurfaceRoadY(x, z) + c.Num("lift", 0.5), z);
		t.SetOrientation(Vector(c.Num("yaw", 0), 0, 0));
		t.SetPosition(pos);
		SetVelocity(t, "0 0 0");
		dBodySetAngularVelocity(t, "0 0 0");
		t.Synchronize();
		AB_Veh.Describe(t, c.R);
		return true;
	}
}

void AB_RegisterVehicleCommands(AB_Registry reg)
{
	reg.Add("vehicle", AB_Cmd_Vehicle, "vehicle [target=<id|type>] - the vehicle you're in (or a nearby one): speed, engine, gear, controls, fluids, crew by seat, parts");
	reg.Add("vehicle_parts", AB_Cmd_VehicleParts, "vehicle_parts [target=] - per seat: where to stand to get in, door component and state; and every action component (doors, hood, trunk...) for component= in actions/action");
	reg.Add("drive", AB_Cmd_Drive, "drive [throttle=0..1] [brake=] [steer=-1..1 (+ right)] [handbrake=] [gear=R|N|D|P|1..] [secs=1] [keep=false] - hold the driving controls");
	reg.Add("drive_to", AB_Cmd_DriveTo, "drive_to x= z= [speed=25 km/h] [radius=6] [stop=true] - drive there in a straight line (steers, keeps speed, shifts, brakes to a stop)");
	reg.Add("stop_vehicle", AB_Cmd_StopVehicle, "stop_vehicle - brake to a standstill (a boat: reverse thrust) and let go of the controls");
	reg.Add("vehicle_service", AB_Cmd_VehicleService, "[server] vehicle_service [id=|type=] [radius=20] [near_x= near_z=] - repair the vehicle and its parts, fill every fluid (default: nearest to the player)");
	reg.Add("vehicle_place", AB_Cmd_VehiclePlace, "[server] vehicle_place id=|type= x= z= [yaw=0] - put a vehicle there, upright and still (unflip, move)");
}
