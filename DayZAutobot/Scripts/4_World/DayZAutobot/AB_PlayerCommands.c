// Client-side player commands: observing, aiming, moving, actions, items.
// Everything here drives the local player the way input would (input overrides, simulated
// action keys, predictive inventory moves), so the server sees ordinary player behaviour.

class AB_Obj
{
	// Network id "low:high" (the same on client and server), or "" for map objects.
	static string Id(Object o)
	{
		if (!o)
			return "";
		int lo, hi;
		o.GetNetworkID(lo, hi);
		if (lo == 0 && hi == 0)
			return "";
		return lo.ToString() + ":" + hi.ToString();
	}

	static Object ById(string id)
	{
		array<string> parts = new array<string>;
		id.Split(":", parts);
		if (parts.Count() != 2)
			return null;
		return GetGame().GetObjectByNetworkId(parts[0].ToInt(), parts[1].ToInt());
	}

	static string Kind(Object o)
	{
		if (o.IsInherited(PlayerBase))
			return "player";
		if (o.IsInherited(ZombieBase))
			return "zombie";
		if (o.IsInherited(AnimalBase))
			return "animal";
		if (o.IsInherited(Transport))
			return "vehicle";
		if (o.IsInherited(ItemBase))
			return "item";
		if (o.IsInherited(BuildingBase) || o.IsInherited(House))
			return "building";
		if (o.IsTree())
			return "tree";
		if (o.IsBush())
			return "bush";
		if (o.IsInherited(EntityAI))
			return "entity";
		return "object";
	}

	// case-insensitive type match: exact class name, a base class, or (fuzzy) part of the type/display name
	static bool Matches(Object o, string spec, bool fuzzy = true)
	{
		if (spec == "")
			return true;
		string t = o.GetType();
		string tl = t;
		tl.ToLower();
		string sl = spec;
		sl.ToLower();
		if (tl == sl || o.IsKindOf(spec))
			return true;
		if (!fuzzy)
			return false;
		if (tl.Contains(sl))
			return true;
		EntityAI e = EntityAI.Cast(o);
		if (e)
		{
			string dn = e.GetDisplayName();
			dn.ToLower();
			if (dn.Contains(sl))
				return true;
		}
		return false;
	}

	static AB_JObj Describe(Object o, vector from = "0 0 0", bool withDist = false)
	{
		AB_JObj j = new AB_JObj;
		j.Str("type", o.GetType());
		string id = Id(o);
		if (id != "")
			j.Str("id", id);
		j.Str("kind", Kind(o));
		j.Vec("pos", o.GetPosition());
		if (withDist)
			j.Num("dist", vector.Distance(from, o.GetPosition()));
		EntityAI e = EntityAI.Cast(o);
		if (e)
		{
			j.Str("name", e.GetDisplayName());
			j.Int("health_level", e.GetHealthLevel());
			if (GetGame().IsServer())
				j.Num("health", e.GetHealth("", ""));
			j.Bool("alive", e.IsAlive());
			ItemBase ib = ItemBase.Cast(e);
			if (ib && ib.HasQuantity())
				j.Num("quantity", ib.GetQuantity());
			if (e.IsInherited(PlayerBase) && PlayerBase.Cast(e).GetIdentity())
				j.Str("player_name", PlayerBase.Cast(e).GetIdentity().GetName());
		}
		return j;
	}

	// Inventory tree of an entity: attachments (with slot) and cargo, recursively.
	static AB_JObj Tree(EntityAI e, int depth)
	{
		AB_JObj j = Describe(e);
		if (depth <= 0 || e.GetInventory() == null)
			return j;
		AB_JArr atts = new AB_JArr;
		for (int i = 0; i < e.GetInventory().AttachmentCount(); i++)
		{
			EntityAI a = e.GetInventory().GetAttachmentFromIndex(i);
			if (!a)
				continue;
			AB_JObj aj = Tree(a, depth - 1);
			InventoryLocation il = new InventoryLocation;
			a.GetInventory().GetCurrentInventoryLocation(il);
			aj.Str("slot", InventorySlots.GetSlotName(il.GetSlot()));
			atts.Obj(aj);
		}
		if (atts.Count() > 0)
			j.Arr("attachments", atts);
		CargoBase cargo = e.GetInventory().GetCargo();
		if (cargo)
		{
			AB_JArr items = new AB_JArr;
			for (int k = 0; k < cargo.GetItemCount(); k++)
				items.Obj(Tree(cargo.GetItem(k), depth - 1));
			j.Arr("cargo", items);
		}
		return j;
	}

	// An item the player carries (hands, clothing, cargo...), by network id or type spec.
	static EntityAI FindCarried(PlayerBase p, string spec)
	{
		if (spec == "")
			return null;
		Object byId = ById(spec);
		if (byId)
			return EntityAI.Cast(byId);
		array<EntityAI> all = new array<EntityAI>;
		p.GetInventory().EnumerateInventory(InventoryTraversalType.PREORDER, all);
		// exact type first, then base class, then fuzzy
		foreach (EntityAI e1 : all)
		{
			if (e1 != p && Matches(e1, spec, false))
				return e1;
		}
		foreach (EntityAI e2 : all)
		{
			if (e2 != p && Matches(e2, spec, true))
				return e2;
		}
		return null;
	}

	// The nearest object around a point that matches, by network id or type spec.
	static Object FindNear(vector center, float radius, string spec, Object ignore = null)
	{
		Object byId = ById(spec);
		if (byId)
			return byId;
		array<Object> objs = new array<Object>;
		GetGame().GetObjectsAtPosition(center, radius, objs, null);
		Object best;
		float bestD = 1e9;
		bool bestExact;
		foreach (Object o : objs)
		{
			if (o == ignore || !Matches(o, spec, true))
				continue;
			bool exact = Matches(o, spec, false);
			float d = vector.Distance(center, o.GetPosition());
			if ((exact && !bestExact) || (exact == bestExact && d < bestD))
			{
				best = o;
				bestD = d;
				bestExact = exact;
			}
		}
		return best;
	}
}

// Base for commands that need the local, living player.
class AB_PlayerCommand : AB_Command
{
	PlayerBase P;

	override bool Start()
	{
		P = PlayerBase.Cast(GetGame().GetPlayer());
		if (!P)
			return Fail("no player (state is " + AB_State.Name() + ")");
		return PlayerStart();
	}

	bool PlayerStart()
	{
		return true;
	}

	override bool Tick(float dt)
	{
		if (!P || !P.IsAlive())
			return Fail("player died or left");
		return PlayerTick(dt);
	}

	bool PlayerTick(float dt)
	{
		return true;
	}
}

// ---------------------------------------------------------------- aiming & movement

// Turning and moving: thin wrappers over the player's drive state (4_World AB_Input.c), which both
// client and server apply. Yaw follows the heading; pitch is the client's camera only.
class AB_Steer
{
	static float s_PitchSign = 0;

	static vector Flat(vector v)
	{
		v[1] = 0;
		return v.Normalized();
	}

	// signed yaw angle (radians) from direction a to direction b, around world up
	static float YawBetween(vector a, vector b)
	{
		a = Flat(a);
		b = Flat(b);
		float crossY = a[2] * b[0] - a[0] * b[2];
		float dot = a[0] * b[0] + a[2] * b[2];
		return Math.Atan2(crossY, dot);
	}

	static float Pitch(vector dir)
	{
		return Math.Asin(Math.Clamp(dir.Normalized()[1], -1, 1));
	}

	static vector AimDir()
	{
		return GetGame().GetCurrentCameraDirection();
	}

	static void Release(PlayerBase p)
	{
		if (p)
			p.AB_Release();
	}

	static void Move(PlayerBase p, float speed, float angle)
	{
		p.AB_SetMove(true, speed, angle);
	}
}

// Aim toward a world direction: heading on both sides, camera pitch on the client.
// Call Update every frame; returns true when aligned.
class AB_Aimer : Managed
{
	vector Target;	// desired world direction
	bool UsePitch;
	float Tolerance = 0.03;	// radians
	protected float m_PitchProbe0;
	protected int m_PitchFrames = -1;

	bool Update(PlayerBase p)
	{
		p.AB_SetHeading(true, AB_Drive.HeadingOf(Target));
		bool yawOk = Math.AbsFloat(p.AB_HeadingError()) < Tolerance;

		bool pitchOk = true;
		if (UsePitch)
		{
			vector cur = AB_Steer.AimDir();
			float perr = AB_Steer.Pitch(Target) - AB_Steer.Pitch(cur);
			pitchOk = Math.AbsFloat(perr) < Tolerance * 1.5;
			if (AB_Steer.s_PitchSign == 0)
			{
				// learn which way a positive pitch change moves the camera
				if (m_PitchFrames < 0)
				{
					m_PitchProbe0 = AB_Steer.Pitch(cur);
					m_PitchFrames = 0;
				}
				m_PitchFrames++;
				if (m_PitchFrames <= 3)
					p.m_AB_AimY = 0.05;
				float pm = AB_Steer.Pitch(cur) - m_PitchProbe0;
				if (Math.AbsFloat(pm) > 0.02)
				{
					if (pm > 0)
						AB_Steer.s_PitchSign = 1;
					else
						AB_Steer.s_PitchSign = -1;
				}
				else if (m_PitchFrames > 30)
					AB_Steer.s_PitchSign = 1;
				return false;
			}
			if (!pitchOk)
				p.m_AB_AimY = Math.Clamp(perr * 0.5, -0.2, 0.2) * AB_Steer.s_PitchSign;
		}
		return yawOk && pitchOk;
	}
}

// look (yaw= [pitch=]) | (x= z= [y=]) | (at=<id|type>) - turn the camera/body until aligned
class AB_Cmd_Look : AB_PlayerCommand
{
	ref AB_Aimer m_Aim = new AB_Aimer;
	int m_Stable;

	override float DefaultTimeout()
	{
		return 10;
	}

	override bool PlayerStart()
	{
		vector eye = GetGame().GetCurrentCameraPosition();
		vector dir;
		if (c.Has("at"))
		{
			Object o = AB_Obj.FindNear(P.GetPosition(), c.Num("radius", 50), c.Str("at"), P);
			if (!o)
				return Fail("nothing matching '" + c.Str("at") + "' nearby");
			vector tp = o.GetPosition();
			if (o.IsMan() || o.IsInherited(DayZCreature))
				tp = tp + "0 1.2 0";
			else if (o.IsInherited(Transport))
				tp = tp + "0 1 0";
			dir = tp - eye;
			m_Aim.UsePitch = true;
			c.R.Obj("target", AB_Obj.Describe(o));
		}
		else if (c.Has("x") && c.Has("z"))
		{
			float x = c.Num("x");
			float z = c.Num("z");
			float y = c.Num("y", GetGame().SurfaceRoadY(x, z));
			dir = Vector(x, y, z) - eye;
			m_Aim.UsePitch = c.Has("y") || c.Bool("pitch_to_ground", true);
		}
		else if (c.Has("yaw"))
		{
			// yaw in degrees, compass style: 0 = north (+z), 90 = east (+x)
			float yaw = c.Num("yaw") * Math.DEG2RAD;
			float pitch = c.Num("pitch", 0) * Math.DEG2RAD;
			dir = Vector(Math.Sin(yaw) * Math.Cos(pitch), Math.Sin(pitch), Math.Cos(yaw) * Math.Cos(pitch));
			m_Aim.UsePitch = c.Has("pitch");
		}
		else
			return Fail("give yaw= [pitch=], x= z= [y=], or at=<id|type>");
		m_Aim.Target = dir.Normalized();
		return false;
	}

	override bool PlayerTick(float dt)
	{
		if (m_Aim.Update(P))
			m_Stable++;
		else
			m_Stable = 0;
		if (m_Stable < 3)
			return false;
		AB_Steer.Release(P);
		AB_Cmd_Player.Aim(c.R);
		return true;
	}

	override void Cancel()
	{
		AB_Steer.Release(P);
	}
}

// move [dir=forward|back|left|right] [angle=] [speed=2] secs=1 - move relative to the heading
class AB_Cmd_Move : AB_PlayerCommand
{
	float m_Secs;
	float m_Speed;
	float m_Angle;
	vector m_Start;

	override bool PlayerStart()
	{
		m_Secs = c.Num("secs", 1);
		m_Speed = c.Num("speed", 2);
		string d = c.Str("dir", "forward");
		if (d == "back")
			m_Angle = 180;
		else if (d == "left")
			m_Angle = -90;
		else if (d == "right")
			m_Angle = 90;
		m_Angle = c.Num("angle", m_Angle);
		m_Start = P.GetPosition();
		return false;
	}

	override bool PlayerTick(float dt)
	{
		if (c.Elapsed >= m_Secs)
		{
			AB_Steer.Release(P);
			c.R.Vec("from", m_Start).Vec("to", P.GetPosition()).Num("moved", vector.Distance(m_Start, P.GetPosition()));
			return true;
		}
		AB_Steer.Move(P, m_Speed, m_Angle);
		return false;
	}

	override void Cancel()
	{
		AB_Steer.Release(P);
	}
}

// move_to x= z= [speed=2] [radius=1.5] - walk/run there in a straight line (no pathfinding)
class AB_Cmd_MoveTo : AB_PlayerCommand
{
	vector m_Goal;
	float m_Radius;
	float m_Speed;
	ref AB_Aimer m_Aim = new AB_Aimer;
	vector m_LastPos;
	float m_StuckFor;

	override float DefaultTimeout()
	{
		return 120;
	}

	override bool PlayerStart()
	{
		if (!c.Has("x") || !c.Has("z"))
			return Fail("x= and z= required");
		m_Goal = Vector(c.Num("x"), 0, c.Num("z"));
		m_Radius = c.Num("radius", 1.5);
		m_Speed = c.Num("speed", 2);
		m_Aim.Tolerance = 0.08;
		m_LastPos = P.GetPosition();
		return false;
	}

	override bool PlayerTick(float dt)
	{
		vector pos = P.GetPosition();
		vector flat = Vector(pos[0], 0, pos[2]);
		float dist = vector.Distance(flat, m_Goal);
		if (dist <= m_Radius)
		{
			AB_Steer.Release(P);
			c.R.Vec("pos", pos).Num("dist", dist);
			return true;
		}
		m_Aim.Target = (m_Goal - flat).Normalized();
		m_Aim.Update(P);
		float err = Math.AbsFloat(P.AB_HeadingError());
		float speed = m_Speed;
		if (err > 0.6)
			speed = 0;		// turn on the spot first
		else if (dist < 4 && speed > 1)
			speed = 1;
		AB_Steer.Move(P, speed, 0);

		// stuck detection: barely moved for a while while trying to move
		if (speed > 0 && vector.Distance(pos, m_LastPos) < 0.3 * dt)
			m_StuckFor += dt;
		else
			m_StuckFor = 0;
		m_LastPos = pos;
		if (m_StuckFor > 3)
		{
			AB_Steer.Release(P);
			c.R.Vec("pos", pos).Num("dist", dist);
			return Fail("stuck (blocked by something?) " + dist.ToString() + " m short");
		}
		return false;
	}

	override void Cancel()
	{
		AB_Steer.Release(P);
	}
}

// stance value=erect|crouch|prone
class AB_Cmd_Stance : AB_PlayerCommand
{
	int m_Want;

	override float DefaultTimeout()
	{
		return 10;
	}

	override bool PlayerStart()
	{
		string s = c.Str("value", "erect");
		if (s == "crouch")
			m_Want = DayZPlayerConstants.STANCEIDX_CROUCH;
		else if (s == "prone")
			m_Want = DayZPlayerConstants.STANCEIDX_PRONE;
		else
			m_Want = DayZPlayerConstants.STANCEIDX_ERECT;
		HumanCommandMove mv = P.GetCommand_Move();
		if (!mv)
			return Fail("can't change stance now (not in the move command)");
		mv.ForceStance(m_Want);
		return false;
	}

	override bool PlayerTick(float dt)
	{
		HumanMovementState st = new HumanMovementState;
		P.GetMovementState(st);
		int cur = st.m_iStanceIdx;
		if (cur >= DayZPlayerConstants.STANCEIDX_RAISEDERECT)
			cur -= DayZPlayerConstants.STANCEIDX_RAISEDERECT;
		if (cur != m_Want)
			return false;
		c.R.Int("stance", cur);
		return true;
	}
}

// ---------------------------------------------------------------- observation

class AB_Cmd_Player : AB_PlayerCommand
{
	static string StanceName(int idx)
	{
		if (idx >= DayZPlayerConstants.STANCEIDX_RAISEDERECT)
			idx -= DayZPlayerConstants.STANCEIDX_RAISEDERECT;
		if (idx == DayZPlayerConstants.STANCEIDX_CROUCH)
			return "crouch";
		if (idx == DayZPlayerConstants.STANCEIDX_PRONE)
			return "prone";
		return "erect";
	}

	static void Aim(AB_JObj o)
	{
		vector d = AB_Steer.AimDir();
		float yaw = Math.Atan2(d[0], d[2]) * Math.RAD2DEG;
		if (yaw < 0)
			yaw += 360;
		o.Num("yaw", yaw).Num("pitch", AB_Steer.Pitch(d) * Math.RAD2DEG);
	}

	override bool PlayerStart()
	{
		c.R.Vec("pos", P.GetPosition());
		Aim(c.R);
		c.R.Bool("alive", P.IsAlive()).Bool("unconscious", P.IsUnconscious()).Bool("restrained", P.IsRestrained());
		if (P.GetIdentity())
			c.R.Str("name", P.GetIdentity().GetName());
		c.R.Str("id", AB_Obj.Id(P)).Str("type", P.GetType());
		// vitals: synced values the HUD uses (exact numbers are on the server: `server player`)
		c.R.Int("health_level", P.GetHealthLevel());
		c.R.Num("stamina", P.GetStaminaHandler().GetSyncedStaminaNormalized());
		c.R.Int("bleeding_sources", P.GetBleedingSourceCount());
		HumanMovementState st = new HumanMovementState;
		P.GetMovementState(st);
		c.R.Str("stance", StanceName(st.m_iStanceIdx)).Bool("raised", st.IsRaised()).Int("movement", st.m_iMovement);
		c.R.Num("speed", GetVelocity(P).Length());
		EntityAI hands = P.GetItemInHands();
		if (hands)
			c.R.Obj("hands", AB_Obj.Describe(hands));
		if (P.IsInVehicle())
		{
			Transport t = Transport.Cast(P.GetParent());
			if (t)
				c.R.Obj("vehicle", AB_Obj.Describe(t));
		}
		ActionBase running = P.GetActionManager().GetRunningAction();
		if (running)
			c.R.Str("running_action", running.ClassName());
		return true;
	}
}

// inventory [depth=4] - everything the player wears and carries, as a tree
class AB_Cmd_Inventory : AB_PlayerCommand
{
	override bool PlayerStart()
	{
		int depth = c.Int("depth", 4);
		EntityAI hands = P.GetItemInHands();
		if (hands)
			c.R.Obj("hands", AB_Obj.Tree(hands, depth));
		AB_JArr worn = new AB_JArr;
		for (int i = 0; i < P.GetInventory().AttachmentCount(); i++)
		{
			EntityAI a = P.GetInventory().GetAttachmentFromIndex(i);
			if (!a)
				continue;
			AB_JObj aj = AB_Obj.Tree(a, depth);
			InventoryLocation il = new InventoryLocation;
			a.GetInventory().GetCurrentInventoryLocation(il);
			aj.Str("slot", InventorySlots.GetSlotName(il.GetSlot()));
			worn.Obj(aj);
		}
		c.R.Arr("worn", worn);
		return true;
	}
}

// nearby [radius=30] [type=] [kind=player|zombie|animal|item|vehicle|entity|building|...] [limit=40] [static=false]
class AB_Cmd_Nearby : AB_Command
{
	override bool Start()
	{
		vector center;
		Object self = GetGame().GetPlayer();
		if (c.Has("x") && c.Has("z"))
			center = Vector(c.Num("x"), c.Num("y", GetGame().SurfaceY(c.Num("x"), c.Num("z"))), c.Num("z"));
		else if (self)
			center = self.GetPosition();
		else
			return Fail("no player; give x= z=");
		float radius = c.Num("radius", 30);
		string type = c.Str("type");
		string kind = c.Str("kind");
		bool wantStatic = c.Bool("static", false);
		int limit = c.Int("limit", 40);
		array<Object> objs = new array<Object>;
		GetGame().GetObjectsAtPosition(center, radius, objs, null);
		array<Object> keep = new array<Object>;
		array<float> dists = new array<float>;
		foreach (Object o : objs)
		{
			if (!o || o == self)
				continue;
			string k = AB_Obj.Kind(o);
			if (kind != "" && k != kind)
				continue;
			if (kind == "" && !wantStatic && type == "" && (k == "object" || k == "building" || k == "tree" || k == "bush"))
				continue;
			if (!AB_Obj.Matches(o, type))
				continue;
			// skip things inside inventories (they report their owner's position)
			EntityAI e = EntityAI.Cast(o);
			if (e && e.GetHierarchyParent())
				continue;
			float d = vector.Distance(center, o.GetPosition());
			int at = 0;
			while (at < dists.Count() && dists[at] <= d)
				at++;
			keep.InsertAt(o, at);
			dists.InsertAt(d, at);
		}
		AB_JArr list = new AB_JArr;
		for (int i = 0; i < keep.Count() && i < limit; i++)
			list.Obj(AB_Obj.Describe(keep[i], center, true));
		c.R.Int("count", keep.Count()).Arr("objects", list);
		return true;
	}
}

// target [range=5] - what the crosshair is on, and the actions the game offers right now
class AB_Cmd_Target : AB_PlayerCommand
{
	override bool PlayerStart()
	{
		float range = c.Num("range", 5);
		vector from = GetGame().GetCurrentCameraPosition();
		vector to = from + GetGame().GetCurrentCameraDirection() * range;
		vector hitPos, hitNormal;
		int comp;
		set<Object> hits = new set<Object>;
		if (DayZPhysics.RaycastRV(from, to, hitPos, hitNormal, comp, hits, null, P, true, false, ObjIntersectView, 0.05))
		{
			c.R.Vec("hit_pos", hitPos).Num("hit_dist", vector.Distance(from, hitPos));
			if (hits.Count() > 0)
				c.R.Obj("object", AB_Obj.Describe(hits.Get(0)));
		}
		c.R.Arr("actions", AB_Cmd_Actions.List(P));
		return true;
	}
}

// actions - the actions available now (what the HUD would offer), per input
class AB_Cmd_Actions : AB_PlayerCommand
{
	static string InputName(ActionInput ai)
	{
		string n = ai.ClassName();
		n.Replace("ActionInput", "");
		return n;
	}

	static void Collect(ActionInput ai, array<ActionBase> found)
	{
		int n = ai.GetPossibleActionsCount();
		if (n >= 0)
		{
			array<ActionBase> many = ai.GetPossibleActions();
			for (int i = 0; i < n; i++)
			{
				ActionBase a = many.Get(i);
				if (a != null)
					found.Insert(a);
			}
			return;
		}
		ActionBase one = ai.GetPossibleAction();
		if (one != null)
			found.Insert(one);
	}

	static AB_JArr List(PlayerBase p)
	{
		AB_JArr list = new AB_JArr;
		// (no `!am || !am.AB_Inputs()`: Enforce evaluates `!x.Method()` in an || wrongly)
		ActionManagerClient am = ActionManagerClient.Cast(p.GetActionManager());
		if (am == null)
			return list;
		array<ActionInput> inputs = am.AB_Inputs();
		if (inputs == null)
			return list;
		foreach (ActionInput ai : inputs)
		{
			array<ActionBase> acts = new array<ActionBase>;
			Collect(ai, acts);
			foreach (ActionBase a : acts)
			{
				AB_JObj o = new AB_JObj;
				o.Str("action", a.ClassName()).Str("text", Widget.TranslateString(a.GetText())).Str("input", InputName(ai));
				if (ai.m_Target && ai.m_Target.GetObject())
					o.Str("target", ai.m_Target.GetObject().GetType()).Str("target_id", AB_Obj.Id(ai.m_Target.GetObject()));
				list.Obj(o);
			}
		}
		return list;
	}

	int m_Frames;

	override bool PlayerStart()
	{
		if (c.Has("target"))
		{
			Object o = AB_Target.Find(P, c);
			if (o == null)
				return Fail("no '" + c.Str("target") + "' within " + c.Num("radius", 4) + " m");
			AB_Target.Force(P, o);
			m_Frames = AB_Target.Updates(P);
			return false;
		}
		return Report();
	}

	override bool PlayerTick(float dt)
	{
		if (AB_Target.Updates(P) < m_Frames + 2)
			return false;
		bool done = Report();
		AB_Target.Clear(P);
		return done;
	}

	bool Report()
	{
		c.R.Arr("actions", List(P));
		AB_JObj diag = new AB_JObj;
		ActionManagerClient am = ActionManagerClient.Cast(P.GetActionManager());
		am.AB_Diagnose(diag);
		c.R.Obj("state", diag);
		if (c.Bool("why", false))
		{
			AB_JArr why = new AB_JArr;
			am.AB_Why(why);
			c.R.Arr("why", why);
		}
		ActionBase running = P.GetActionManager().GetRunningAction();
		if (running)
			c.R.Str("running", running.ClassName());
		return true;
	}
}

// Forces the action target of every action input (the game's own hook for this), so actions can be
// offered for an object without pixel-exact aiming. The server still checks range and conditions.
class AB_Target
{
	static bool Force(PlayerBase p, Object o)
	{
		ActionManagerClient am = ActionManagerClient.Cast(p.GetActionManager());
		if (am == null)
			return false;
		array<ActionInput> inputs = am.AB_Inputs();
		Object parent = null;
		EntityAI e = EntityAI.Cast(o);
		if (e != null && e.GetHierarchyParent() != null)
			parent = e.GetHierarchyParent();
		// only the four standard inputs (use/interact, click/hold) take a forced target; their subclasses
		// (vehicle lights, horn, NVG...) throw on a non-vehicle one, which aborts the game's action update
		for (int i = 0; i < inputs.Count(); i++)
		{
			string cls = inputs[i].ClassName();
			if (cls != "ContinuousDefaultActionInput" && cls != "DefaultActionInput" && cls != "ContinuousInteractActionInput" && cls != "InteractActionInput")
				continue;
			ActionTarget at = new ActionTarget(o, parent, -1, o.GetPosition(), 1);
			inputs[i].ForceActionTarget(at);
		}
		return true;
	}

	// how many times the game's action manager has updated (it runs at the simulation rate, slower
	// than frames): wait on this, not on frames, for a forced target to take effect
	static int Updates(PlayerBase p)
	{
		ActionManagerClient am = ActionManagerClient.Cast(p.GetActionManager());
		if (am == null)
			return 0;
		return am.m_AB_Updates;
	}

	static void Clear(PlayerBase p)
	{
		ActionManagerClient am = ActionManagerClient.Cast(p.GetActionManager());
		if (am == null)
			return;
		array<ActionInput> inputs = am.AB_Inputs();
		foreach (ActionInput ai : inputs)
			ai.ClearForcedTarget();
	}

	// target=<id|type> within radius= of the player, or null (and a failure message) if none
	static Object Find(PlayerBase p, AB_Ctx c)
	{
		return AB_Obj.FindNear(p.GetPosition(), c.Num("radius", 4), c.Str("target"), p);
	}
}

// action name=<ActionClass or text> [target=<id|type>] [hold=secs] [wait=true] - do an action the game
// offers, by simulating its key (click, or hold for continuous actions: eating, bandaging, repairing...).
// Without target= it uses whatever the crosshair is on (aim with look first).
class AB_Cmd_Action : AB_PlayerCommand
{
	ActionInput m_Input;
	ActionBase m_Action;
	int m_Key = -1;
	float m_Hold;
	bool m_Started;
	bool m_Wait;
	bool m_Forced;
	int m_Phase;		// 0 = let a forced target take effect, 1 = pressed, waiting
	int m_ForcedAt;
	int m_PressedAt;
	int m_Frames;

	override float DefaultTimeout()
	{
		return 60;
	}

	static bool NameMatches(ActionBase a, string spec)
	{
		string s = spec;
		s.ToLower();
		string cn = a.ClassName();
		cn.ToLower();
		string tx = Widget.TranslateString(a.GetText());
		tx.ToLower();
		return cn == s || tx == s || ("action" + s) == cn;
	}

	override bool PlayerStart()
	{
		if (c.Str("name") == "")
			return Fail("name= required (see: actions)");
		if (P.GetActionManager().GetRunningAction() != null)
			return Fail("another action is running: " + P.GetActionManager().GetRunningAction().ClassName());
		if (c.Has("target"))
		{
			Object o = AB_Target.Find(P, c);
			if (o == null)
				return Fail("no '" + c.Str("target") + "' within " + c.Num("radius", 4) + " m");
			AB_Target.Force(P, o);
			m_Forced = true;
			m_ForcedAt = AB_Target.Updates(P);
			return false;	// the inputs pick the target up on the next action-manager update
		}
		return Press();
	}

	// find the action among what the inputs offer and press its key; true = finished (failed)
	bool Press()
	{
		string spec = c.Str("name");
		ActionManagerClient am = ActionManagerClient.Cast(P.GetActionManager());
		array<ActionInput> inputs = am.AB_Inputs();
		foreach (ActionInput ai : inputs)
		{
			array<ActionBase> acts = new array<ActionBase>;
			AB_Cmd_Actions.Collect(ai, acts);
			foreach (ActionBase a : acts)
			{
				if (NameMatches(a, spec))
				{
					m_Input = ai;
					m_Action = a;
					break;
				}
			}
			if (m_Action != null)
				break;
		}
		if (m_Action == null)
		{
			c.R.Arr("available", AB_Cmd_Actions.List(P));
			Cancel();
			return Fail("action '" + spec + "' isn't offered right now (give target=, aim at it, or hold the item first; actions why=true explains)");
		}
		UAInput ua = m_Input.GetUAInput();
		if (ua == null)
		{
			Cancel();
			return Fail("that action has no input");
		}
		// select it among the input's choices, then press the input's key
		m_Input.ForceAction(m_Action, m_Input.m_Target, m_Input.m_MainItem);
		m_Wait = c.Bool("wait", true);
		m_Hold = c.Num("hold", 0);
		bool continuous = m_Input.GetInputType() == ActionInputType.AIT_CONTINUOUS || m_Input.GetInputType() == ActionInputType.AIT_CLICKCONTINUOUS;
		m_Key = ua.ID();
		if (continuous)
			AB_Input.HoldId(m_Key, m_Hold);
		else
			AB_Input.ClickId(m_Key);
		c.R.Str("action", m_Action.ClassName()).Str("text", Widget.TranslateString(m_Action.GetText())).Str("input", AB_Cmd_Actions.InputName(m_Input));
		if (m_Input.m_Target && m_Input.m_Target.GetObject())
			c.R.Obj("target", AB_Obj.Describe(m_Input.m_Target.GetObject()));
		m_Phase = 1;
		m_PressedAt = AB_Target.Updates(P);
		return false;
	}

	override bool PlayerTick(float dt)
	{
		m_Frames++;
		if (m_Phase == 0)
		{
			if (AB_Target.Updates(P) < m_ForcedAt + 2)
				return false;
			return Press();
		}
		ActionBase running = P.GetActionManager().GetRunningAction();
		if (!m_Started)
		{
			if (running != null)
			{
				m_Started = true;
				m_Input.ClearForcedAction();
				if (!m_Wait)
				{
					Done();
					return true;
				}
			}
			else if (m_Frames > 25 && AB_Target.Updates(P) > m_PressedAt + 15)
			{
				// single actions can complete within a frame; nothing running is fine if it fired
				Done();
				c.R.Bool("started", false);
				c.R.Str("note", "no running action seen; it may have completed instantly or been refused");
				return true;
			}
			return false;
		}
		if (running != null)
			return false;
		Done();
		c.R.Bool("started", true).Num("secs", c.Elapsed);
		return true;
	}

	void Done()
	{
		if (m_Input)
			m_Input.ClearForcedAction();
		if (m_Key >= 0)
			AB_Input.ReleaseId(m_Key);
		if (m_Forced)
			AB_Target.Clear(P);
	}

	override void Cancel()
	{
		if (P)
			Done();
	}
}

// key name=UAAction [hold=secs] - click (or hold) an action input by its input name
class AB_Cmd_Key : AB_Command
{
	override bool Start()
	{
		string name = c.Str("name");
		bool ok;
		if (c.Has("hold"))
			ok = AB_Input.Hold(name, c.Num("hold"));
		else if (c.Bool("release", false))
		{
			AB_Input.Release(name);
			ok = true;
		}
		else
			ok = AB_Input.Click(name);
		if (!ok)
			return Fail("unknown input '" + name + "' (e.g. UADefaultAction, UAAction, UAReloadMagazine)");
		c.R.Str("note", "only action inputs (the ActionInput classes) see simulated keys");
		return true;
	}
}

// ---------------------------------------------------------------- items

// take item=<id|type> [radius=3] [to=inventory|hands] - pick an item up from the ground
class AB_Cmd_Take : AB_PlayerCommand
{
	EntityAI m_Item;

	override float DefaultTimeout()
	{
		return 10;
	}

	override bool PlayerStart()
	{
		Object o = AB_Obj.FindNear(P.GetPosition(), c.Num("radius", 3), c.Str("item"), P);
		m_Item = EntityAI.Cast(o);
		if (!m_Item || !m_Item.IsInherited(ItemBase))
			return Fail("no item matching '" + c.Str("item") + "' within reach");
		bool ok = true;
		if (c.Str("to", "inventory") == "hands")
			P.PredictiveTakeEntityToHands(m_Item);
		else
			ok = P.PredictiveTakeEntityToInventory(FindInventoryLocationType.ANY, m_Item);
		if (!ok)
			return Fail("the game refused (no room, too far, or reserved)");
		c.R.Obj("item", AB_Obj.Describe(m_Item));
		return false;
	}

	override bool PlayerTick(float dt)
	{
		if (m_Item && m_Item.GetHierarchyRootPlayer() == P)
			return true;
		if (c.Elapsed > 5)
			return Fail("the item didn't arrive (refused by the server?)");
		return false;
	}
}

// hands item=<id|type> - take a carried item into the hands (the "move to hands" inventory action)
class AB_Cmd_Hands : AB_PlayerCommand
{
	EntityAI m_Item;
	float m_Retry;

	override float DefaultTimeout()
	{
		return 10;
	}

	override bool PlayerStart()
	{
		if (c.Str("item") == "" || c.Str("item") == "none")
		{
			// empty hands into the inventory
			EntityAI held = P.GetItemInHands();
			if (held == null)
				return true;
			P.PredictiveMoveItemFromHandsToInventory();
			return false;
		}
		m_Item = AB_Obj.FindCarried(P, c.Str("item"));
		if (!m_Item)
			return Fail("not carrying anything matching '" + c.Str("item") + "'");
		if (P.GetItemInHands() == m_Item)
			return true;
		P.PredictiveTakeEntityToHands(m_Item);
		return false;
	}

	override bool PlayerTick(float dt)
	{
		if (m_Item == null)
		{
			if (P.GetItemInHands() == null)
				return true;
			if (c.Elapsed > 6)
				return Fail("couldn't put it away (no room?)");
			// the game refuses a new inventory move while the last one is still being sent: retry
			m_Retry += dt;
			if (m_Retry > 0.5)
			{
				m_Retry = 0;
				P.PredictiveMoveItemFromHandsToInventory();
			}
		}
		else if (P.GetItemInHands() == m_Item)
		{
			c.R.Obj("hands", AB_Obj.Describe(m_Item));
			return true;
		}
		return false;
	}
}

// drop [item=<id|type>] - drop an item (default: what's in the hands)
class AB_Cmd_Drop : AB_PlayerCommand
{
	EntityAI m_Item;

	override float DefaultTimeout()
	{
		return 10;
	}

	override bool PlayerStart()
	{
		if (c.Has("item"))
			m_Item = AB_Obj.FindCarried(P, c.Str("item"));
		else
			m_Item = P.GetItemInHands();
		if (!m_Item)
			return Fail("nothing to drop");
		if (!P.PredictiveDropEntity(m_Item))
			return Fail("the game refused to drop it");
		c.R.Obj("item", AB_Obj.Describe(m_Item));
		return false;
	}

	override bool PlayerTick(float dt)
	{
		return !m_Item || m_Item.GetHierarchyRootPlayer() != P;
	}
}

// chat text= - say something in chat (shows up in the server's chat log too)
class AB_Cmd_Chat : AB_Command
{
	override bool Start()
	{
		if (!GetGame().GetPlayer())
			return Fail("no player");
		GetGame().ChatPlayer(c.Str("text"));
		return true;
	}
}

void AB_RegisterPlayerCommands(AB_Registry reg)
{
	reg.Add("player", AB_Cmd_Player, "player - position, aim (yaw/pitch), stance, health level, stamina, bleeding, hands, vehicle, running action");
	reg.Add("inventory", AB_Cmd_Inventory, "inventory [depth=4] - hands and worn items with attachments and cargo, as a tree");
	reg.Add("nearby", AB_Cmd_Nearby, "nearby [radius=30] [type=] [kind=player|zombie|animal|item|vehicle|entity|building] [static=false] [limit=40] [x= z=] - objects sorted by distance");
	reg.Add("target", AB_Cmd_Target, "target [range=5] - what the crosshair hits, plus the actions on offer");
	reg.Add("actions", AB_Cmd_Actions, "actions [target=<id|type>] [why=true] - the actions the game offers now (per input), for the crosshair target or target=; why=true says which condition blocks each action the target/item defines");
	reg.Add("action", AB_Cmd_Action, "action name=<ActionClass|text> [target=<id|type>] [hold=secs] [wait=true] - perform an offered action by pressing/holding its key");
	reg.Add("key", AB_Cmd_Key, "key name=UAAction|UADefaultAction|... [hold=secs] [release=true] - click/hold an action input");
	reg.Add("look", AB_Cmd_Look, "look yaw= [pitch=] | x= z= [y=] | at=<id|type> - turn until aimed (yaw 0 = north, 90 = east)");
	reg.Add("move", AB_Cmd_Move, "move [dir=forward|back|left|right] [angle=] [speed=1 walk|2 jog|3 sprint] [secs=1] - move relative to the heading");
	reg.Add("move_to", AB_Cmd_MoveTo, "move_to x= z= [speed=2] [radius=1.5] - walk there in a straight line (fails when stuck)");
	reg.Add("stance", AB_Cmd_Stance, "stance value=erect|crouch|prone");
	reg.Add("take", AB_Cmd_Take, "take item=<id|type> [radius=3] [to=inventory|hands] - pick up from the ground");
	reg.Add("hands", AB_Cmd_Hands, "hands item=<id|type|none> - move a carried item into the hands (none: put away)");
	reg.Add("drop", AB_Cmd_Drop, "drop [item=<id|type>] - drop an item (default: hands)");
	reg.Add("chat", AB_Cmd_Chat, "chat text= - say something in chat");
}
