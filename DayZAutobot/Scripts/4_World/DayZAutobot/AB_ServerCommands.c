// Server-side test helpers (send with `autobot server <command>`; an offline game answers them too).
// They act with server authority: spawning, teleporting, healing, time and weather.

class AB_ServerCommand : AB_Command
{
	override bool Start()
	{
		if (!GetGame().IsServer())
			return Fail("server command: send it to the server (autobot server " + c.Cmd + " ...)");
		return Run();
	}

	bool Run()
	{
		return true;
	}

	// player=<name|id>, or the only/first player
	PlayerBase FindPlayer()
	{
		array<Man> players = new array<Man>;
		GetGame().GetPlayers(players);
		string spec = c.Str("player");
		foreach (Man m : players)
		{
			PlayerBase p = PlayerBase.Cast(m);
			if (!p)
				continue;
			if (spec == "")
				return p;
			if (AB_Obj.Id(p) == spec || (p.GetIdentity() && (p.GetIdentity().GetName() == spec || p.GetIdentity().GetPlainId() == spec)))
				return p;
		}
		// offline: the local player isn't in GetPlayers before it's fully set up
		if (spec == "" && GetGame().GetPlayer())
			return PlayerBase.Cast(GetGame().GetPlayer());
		return null;
	}

	// x= z= [y=], else in front of the player ([dist=2])
	bool Where(out vector pos)
	{
		if (c.Has("x") && c.Has("z"))
		{
			float x = c.Num("x");
			float z = c.Num("z");
			pos = Vector(x, c.Num("y", GetGame().SurfaceRoadY(x, z)), z);
			return true;
		}
		PlayerBase p = FindPlayer();
		if (!p)
			return false;
		vector d = p.GetDirection();
		d[1] = 0;
		pos = p.GetPosition() + d.Normalized() * c.Num("dist", 2);
		pos[1] = GetGame().SurfaceRoadY(pos[0], pos[2]);
		return true;
	}
}

class AB_Cmd_Players : AB_ServerCommand
{
	override bool Run()
	{
		array<Man> players = new array<Man>;
		GetGame().GetPlayers(players);
		AB_JArr list = new AB_JArr;
		foreach (Man m : players)
		{
			AB_JObj o = AB_Obj.Describe(m);
			if (m.GetIdentity())
				o.Str("steam_id", m.GetIdentity().GetPlainId());
			list.Obj(o);
		}
		c.R.Arr("players", list);
		return true;
	}
}

// server player [player=] - exact vitals the server holds
class AB_Cmd_SvPlayer : AB_ServerCommand
{
	override bool Run()
	{
		PlayerBase p = FindPlayer();
		if (!p)
			return Fail("no such player");
		c.R.Obj("player", AB_Obj.Describe(p));
		c.R.Num("health", p.GetHealth("", "Health")).Num("blood", p.GetHealth("", "Blood")).Num("shock", p.GetHealth("", "Shock"));
		c.R.Num("water", p.GetStatWater().Get()).Num("energy", p.GetStatEnergy().Get());
		c.R.Int("bleeding_sources", p.GetBleedingSourceCount());
		c.R.Bool("god", !p.GetAllowDamage()).Bool("unconscious", p.IsUnconscious());
		return true;
	}
}

// spawn type= [count=1] [at=ground|hands|inventory] [x= z= [y=]] [dist=2] [full=false] [quantity=] [player=]
class AB_Cmd_Spawn : AB_ServerCommand
{
	static bool TypeExists(string type)
	{
		return GetGame().ConfigIsExisting("CfgVehicles " + type) || GetGame().ConfigIsExisting("CfgWeapons " + type) || GetGame().ConfigIsExisting("CfgMagazines " + type) || GetGame().ConfigIsExisting("CfgNonAIVehicles " + type);
	}

	override bool Run()
	{
		string type = c.Str("type");
		if (type == "")
			return Fail("type= required");
		if (!TypeExists(type))
			return Fail("no such class: " + type);
		string at = c.Str("at", "ground");
		int count = c.Int("count", 1);
		AB_JArr made = new AB_JArr;
		for (int i = 0; i < count; i++)
		{
			EntityAI e;
			if (at == "hands" || at == "inventory")
			{
				PlayerBase p = FindPlayer();
				if (!p)
					return Fail("no player to give it to");
				if (at == "hands")
				{
					if (p.GetItemInHands())
						return Fail("the player's hands are full");
					e = p.GetHumanInventory().CreateInHands(type);
				}
				else
					e = p.GetInventory().CreateInInventory(type);
			}
			else
			{
				vector pos;
				if (!Where(pos))
					return Fail("give x= z= (no player to spawn in front of)");
				if (count > 1)
					pos = pos + Vector(Math.RandomFloat(-1, 1), 0, Math.RandomFloat(-1, 1)) * Math.Sqrt(count);
				int flags = ECE_PLACE_ON_SURFACE;
				if (GetGame().IsKindOf(type, "DZ_LightAI"))
					flags = flags | ECE_INITAI;
				if (c.Has("y"))
					flags = ECE_CREATEPHYSICS | ECE_KEEPHEIGHT;
				e = EntityAI.Cast(GetGame().CreateObjectEx(type, pos, flags));
			}
			if (!e)
				return Fail("the game didn't create " + type + " (no room in inventory?)");
			if (c.Bool("full", false))
				e.OnDebugSpawn();
			ItemBase ib = ItemBase.Cast(e);
			if (ib && c.Has("quantity"))
				ib.SetQuantity(c.Num("quantity"));
			// spawned vehicles otherwise get the 3 s lifetime of mission-spawned objects
			if (e.IsInherited(Transport))
			{
				e.SetLifetimeMax(3888000);
				e.SetLifetime(3888000);
			}
			made.Obj(AB_Obj.Describe(e));
		}
		c.R.Arr("spawned", made);
		return true;
	}
}

// teleport x= z= [y=] [player=] - y defaults to the ground (roads/bridges included)
class AB_Cmd_Teleport : AB_ServerCommand
{
	override bool Run()
	{
		PlayerBase p = FindPlayer();
		if (!p)
			return Fail("no such player");
		if (!c.Has("x") || !c.Has("z"))
			return Fail("x= and z= required");
		float x = c.Num("x");
		float z = c.Num("z");
		vector pos = Vector(x, c.Num("y", GetGame().SurfaceRoadY(x, z)), z);
		if (p.IsInVehicle())
			return Fail("the player is in a vehicle");
		p.SetPosition(pos);
		c.R.Vec("pos", pos);
		return true;
	}
}

class AB_Cmd_Heal : AB_ServerCommand
{
	override bool Run()
	{
		PlayerBase p = FindPlayer();
		if (!p)
			return Fail("no such player");
		p.SetHealth("", "Health", p.GetMaxHealth("", "Health"));
		p.SetHealth("", "Blood", p.GetMaxHealth("", "Blood"));
		p.SetHealth("", "Shock", p.GetMaxHealth("", "Shock"));
		p.GetStatWater().Set(p.GetStatWater().GetMax());
		p.GetStatEnergy().Set(p.GetStatEnergy().GetMax());
		if (p.GetBleedingManagerServer())
			p.GetBleedingManagerServer().RemoveAllSources();
		c.R.Str("healed", AB_Obj.Id(p));
		return true;
	}
}

// god [on=true] [player=] - no damage at all
class AB_Cmd_God : AB_ServerCommand
{
	override bool Run()
	{
		PlayerBase p = FindPlayer();
		if (!p)
			return Fail("no such player");
		bool on = c.Bool("on", true);
		p.SetAllowDamage(!on);
		c.R.Bool("god", on);
		return true;
	}
}

class AB_Cmd_Kill : AB_ServerCommand
{
	override bool Run()
	{
		Object o;
		if (c.Has("id"))
			o = AB_Obj.ById(c.Str("id"));
		else
			o = FindPlayer();
		EntityAI e = EntityAI.Cast(o);
		if (!e)
			return Fail("nothing to kill");
		e.SetAllowDamage(true);
		e.SetHealth("", "", 0);
		c.R.Obj("killed", AB_Obj.Describe(e));
		return true;
	}
}

// delete id= | type= [radius=5] [x= z=] - remove objects (entities only, never map objects)
class AB_Cmd_Delete : AB_ServerCommand
{
	override bool Run()
	{
		array<Object> doomed = new array<Object>;
		if (c.Has("id"))
		{
			Object one = AB_Obj.ById(c.Str("id"));
			if (one)
				doomed.Insert(one);
		}
		else if (c.Has("type"))
		{
			vector center;
			if (!Where(center))
				return Fail("give x= z=");
			array<Object> objs = new array<Object>;
			GetGame().GetObjectsAtPosition(center, c.Num("radius", 5), objs, null);
			foreach (Object o : objs)
			{
				if (o.IsInherited(EntityAI) && !o.IsInherited(PlayerBase) && AB_Obj.Matches(o, c.Str("type"), false) && !EntityAI.Cast(o).GetHierarchyParent())
					doomed.Insert(o);
			}
		}
		else
			return Fail("id= or type= required");
		foreach (Object d : doomed)
			GetGame().ObjectDelete(d);
		c.R.Int("deleted", doomed.Count());
		return true;
	}
}

// time hour= [minute=0] - set the time of day (date unchanged)
class AB_Cmd_Time : AB_ServerCommand
{
	override bool Run()
	{
		int y, mo, d, h, mi;
		GetGame().GetWorld().GetDate(y, mo, d, h, mi);
		if (c.Has("hour"))
			GetGame().GetWorld().SetDate(y, mo, d, c.Int("hour"), c.Int("minute", 0));
		GetGame().GetWorld().GetDate(y, mo, d, h, mi);
		c.R.Str("date", y.ToString() + "-" + mo.ToString() + "-" + d.ToString()).Int("hour", h).Int("minute", mi);
		return true;
	}
}

// weather preset=clear|overcast|rain|storm|fog - set and hold for an hour
class AB_Cmd_Weather : AB_ServerCommand
{
	override bool Run()
	{
		Weather w = GetGame().GetWeather();
		string preset = c.Str("preset", "clear");
		float overcast = 0;
		float rain = 0;
		float fog = 0;
		if (preset == "overcast")
			overcast = 0.7;
		else if (preset == "rain")
		{
			overcast = 0.9;
			rain = 0.8;
		}
		else if (preset == "storm")
		{
			overcast = 1;
			rain = 1;
		}
		else if (preset == "fog")
			fog = 0.8;
		else if (preset != "clear")
			return Fail("preset: clear, overcast, rain, storm or fog");
		w.SetRainThresholds(0, 1, 0);
		w.GetOvercast().Set(overcast, 0, 3600);
		w.GetRain().Set(rain, 0, 3600);
		w.GetFog().Set(fog, 0, 3600);
		c.R.Str("preset", preset);
		return true;
	}
}

void AB_RegisterServerCommands(AB_Registry reg)
{
	reg.Add("players", AB_Cmd_Players, "[server] players - everyone connected");
	reg.Add("sv_player", AB_Cmd_SvPlayer, "[server] sv_player [player=] - exact health/blood/shock/water/energy, god mode");
	reg.Add("spawn", AB_Cmd_Spawn, "[server] spawn type= [count=1] [at=ground|hands|inventory] [x= z= [y=]] [dist=2] [full=false] [quantity=] [player=]");
	reg.Add("teleport", AB_Cmd_Teleport, "[server] teleport x= z= [y=ground] [player=]");
	reg.Add("heal", AB_Cmd_Heal, "[server] heal [player=] - full health, blood, shock, food, water; stop bleeding");
	reg.Add("god", AB_Cmd_God, "[server] god [on=true] [player=] - take no damage");
	reg.Add("kill", AB_Cmd_Kill, "[server] kill [player=] | id= - kill a player or entity");
	reg.Add("delete", AB_Cmd_Delete, "[server] delete id= | type= [radius=5] [x= z=] - delete entities (never map objects or players)");
	reg.Add("time", AB_Cmd_Time, "[server] time [hour= [minute=0]] - read or set the time of day");
	reg.Add("weather", AB_Cmd_Weather, "[server] weather preset=clear|overcast|rain|storm|fog");
}
