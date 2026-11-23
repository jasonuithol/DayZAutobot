// The agent core: polls the autobot bridge (tools/autobot, a localhost HTTP server) through the
// game's RestApi, runs each command as a job on the main thread (spanning frames when needed) and
// posts the result back. Nothing happens unless the game was started with -autobot=<port>.
//
// It lives in the Game module because the engine recompiles the Mission module whenever a mission
// starts or ends (joining, leaving, the main menu): anything created by Mission code goes stale
// then. Commands come from registrars in the World module (never reloaded) and the Mission module
// (re-registered after each reload; a Mission command still running at a reload is failed).

const string AB_VERSION = "0.1.0";

void AB_Log(string msg)
{
	Print("[Autobot] " + msg);
}

class AB_Request
{
	string id;
	string cmd;
	ref array<string> args;
}

class AB_Ctx : Managed
{
	string Id;
	string Cmd;
	ref map<string, string> Args = new map<string, string>;
	ref AB_JObj R = new AB_JObj;	// the command's result object
	string Error;
	float Elapsed;
	float Timeout = 30;
	ref AB_Command Command;
	bool FromMission;	// its command class lives in the (reloadable) Mission module

	bool Has(string key)
	{
		return Args.Contains(key);
	}

	string Str(string key, string def = "")
	{
		string v;
		if (Args.Find(key, v))
			return v;
		return def;
	}

	float Num(string key, float def = 0)
	{
		string v;
		if (Args.Find(key, v) && v != "")
			return v.ToFloat();
		return def;
	}

	int Int(string key, int def = 0)
	{
		string v;
		if (Args.Find(key, v) && v != "")
			return v.ToInt();
		return def;
	}

	bool Bool(string key, bool def = false)
	{
		string v;
		bool found = Args.Find(key, v);
		if (!found || v == "")
			return def;
		v.ToLower();
		return v == "true" || v == "1" || v == "yes" || v == "on";
	}

	// "x y z", "x,y,z" or "[x, y, z]"
	vector Vec(string key, vector def = "0 0 0")
	{
		string v;
		bool found = Args.Find(key, v);
		if (!found || v == "")
			return def;
		v.Replace("[", "");
		v.Replace("]", "");
		v.Replace(",", " ");
		return v.ToVector();
	}

	// Ends the command with an error. Returns true so commands can `return Fail("...")`.
	bool Fail(string msg)
	{
		Error = msg;
		return true;
	}
}

// Base class of every command. A new instance is made per request, so commands keep their own
// state in members. Start() runs on the frame the request arrives; returning false keeps the
// command alive and Tick() is then called every frame until it returns true.
// (Don't name a method Step(): the engine never calls script methods with that name.)
class AB_Command : Managed
{
	AB_Ctx c;

	float DefaultTimeout()
	{
		return 30;
	}

	bool Start()
	{
		return true;
	}

	bool Tick(float dt)
	{
		return true;
	}

	// Called when the command is stopped (the stop command, a timeout).
	void Cancel()
	{
	}

	bool Fail(string msg)
	{
		return c.Fail(msg);
	}
}

class AB_CommandInfo : Managed
{
	typename Type;
	string Usage;
	bool FromMission;

	void AB_CommandInfo(typename t, string usage, bool fromMission)
	{
		Type = t;
		Usage = usage;
		FromMission = fromMission;
	}
}

// A module's set of commands. The core finds registrars by class name (AB_WorldCommands,
// AB_MissionCommands) so modules above Game can add commands without being referenced from here.
// Mods under test can add their own: see the README.
class AB_Registrar : Managed
{
	void Register(AB_Registry reg)
	{
	}

	// every frame, before jobs run (client and server)
	void OnFrame(float dt)
	{
	}

	// the stop command: release anything held (keys, movement)
	void OnStopAll()
	{
	}
}

class AB_Registry : Managed
{
	ref map<string, ref AB_CommandInfo> Commands = new map<string, ref AB_CommandInfo>;
	ref array<ref AB_Registrar> Registrars = new array<ref AB_Registrar>;
	protected bool m_Mission;

	void Add(string name, typename t, string usage)
	{
		Commands.Set(name, new AB_CommandInfo(t, usage, m_Mission));
	}

	void AddRegistrar(AB_Registrar r, bool fromMission)
	{
		m_Mission = fromMission;
		r.Register(this);
		m_Mission = false;
		Registrars.Insert(r);
	}

	// drop everything a (now stale) Mission module registered
	void ForgetMission()
	{
		array<string> gone = new array<string>;
		foreach (string name, AB_CommandInfo info : Commands)
		{
			if (info.FromMission)
				gone.Insert(name);
		}
		foreach (string g : gone)
			Commands.Remove(g);
	}
}

class AB_PollCallback : RestCallback
{
	override void OnSuccess(string data, int dataSize)
	{
		AB_Agent.Get().OnPollData(data);
	}

	override void OnError(int errorCode)
	{
		AB_Agent.Get().OnPollFailed("error " + errorCode);
	}

	override void OnTimeout()
	{
		AB_Agent.Get().OnPollFailed("timeout");
	}
}

class AB_PostCallback : RestCallback
{
	override void OnSuccess(string data, int dataSize)
	{
		AB_Agent.Get().OnPosted(this);
	}

	override void OnError(int errorCode)
	{
		AB_Agent.Get().OnPosted(this);
	}

	override void OnTimeout()
	{
		AB_Agent.Get().OnPosted(this);
	}
}

// Game state as the commands see it, from Game-module types only (survives Mission reloads).
class AB_State
{
	// main_menu | loading | spawning | ingame | dead | server
	static string Name()
	{
		DayZGame g = GetDayZGame();
		if (g.IsDedicatedServer())
			return "server";
		if (g.AB_IsLoading())
			return "loading";
		Mission m = g.GetMission();
		if (m == null)
			return "loading";
		if (!m.IsMissionGameplay())
			return "main_menu";
		Man p = g.GetPlayer();
		if (p == null)
			return "spawning";
		// the login queue / spawn countdown screens sit over a player that isn't playable yet
		UIManager ui = g.GetUIManager();
		if (ui != null)
		{
			UIScriptedMenu top = ui.GetMenu();
			if (top != null && top.IsInherited(LoginScreenBase))
				return "spawning";
		}
		if (!p.IsAlive())
			return "dead";
		return "ingame";
	}

	static string MenuName()
	{
		if (GetGame().IsDedicatedServer() || GetGame().GetUIManager() == null)
			return "";
		UIScriptedMenu menu = GetGame().GetUIManager().GetMenu();
		if (menu)
			return menu.ClassName();
		return "";
	}

	static void Describe(AB_JObj o)
	{
		AB_Agent a = AB_Agent.Get();
		o.Str("state", Name()).Str("side", a.Side()).Str("version", AB_VERSION).Str("instance", a.Instance);
		Mission m = GetGame().GetMission();
		if (m)
			o.Str("mission", m.ClassName());
		o.Str("menu", MenuName());
		o.Bool("multiplayer", GetGame().IsMultiplayer());
		string world;
		GetGame().GetWorldName(world);
		o.Str("world", world);
		Man p = GetGame().GetPlayer();
		if (p)
		{
			o.Vec("pos", p.GetPosition());
			o.Bool("alive", p.IsAlive());
		}
		o.Int("jobs", a.Jobs.Count());
	}
}

class AB_Agent : Managed
{
	static ref AB_Agent s_Agent;
	static bool s_Disabled;
	static float s_LastTick = -1;

	string Instance = "default";
	int Port;
	ref AB_Registry Registry;
	ref array<ref AB_Ctx> Jobs = new array<ref AB_Ctx>;
	ref array<ref AB_Ctx> Incoming = new array<ref AB_Ctx>;
	float FrameTime;
	float Uptime;

	protected RestContext m_PollCtx;
	protected RestContext m_PostCtx;
	protected ref AB_PollCallback m_PollCb;
	protected ref array<ref AB_PostCallback> m_PostCbs = new array<ref AB_PostCallback>;
	protected bool m_Polling;
	protected float m_RetryIn;
	protected int m_Failures;
	protected AB_Ctx m_Running;		// set while a command's code runs; still set next frame = it crashed
	protected bool m_WorldRegistered;
	protected typename m_MissionRegistrar;

	static AB_Agent Get()
	{
		return s_Agent;
	}

	// Called every frame by DayZGame.OnUpdate.
	static void Tick(float dt)
	{
		if (s_Disabled)
			return;
		if (!s_Agent)
		{
			string port;
			bool enabled = GetCLIParam("autobot", port);
			if (!enabled || port.ToInt() <= 0)
			{
				s_Disabled = true;
				return;
			}
			s_Agent = new AB_Agent(port.ToInt());
		}
		// the dedicated server passes a zero timeslice, so measure frames ourselves
		float now = GetGame().GetTickTime();
		if (s_LastTick >= 0)
			dt = now - s_LastTick;
		s_LastTick = now;
		s_Agent.Update(dt);
	}

	void AB_Agent(int port)
	{
		Port = port;
		Registry = new AB_Registry;
		RegisterGeneral();
		string inst;
		bool given = GetCLIParam("autobotInst", inst);
		if (given && inst != "")
			Instance = inst;
		AB_Log("enabled, version " + AB_VERSION + ", bridge port " + Port + ", instance " + Instance);
	}

	protected void RegisterGeneral()
	{
		Registry.Add("ping", AB_Cmd_Ping, "ping - check the game answers; returns version, side and frame time");
		Registry.Add("help", AB_Cmd_Help, "help [name=] - list commands with usage");
		Registry.Add("stop", AB_Cmd_Stop, "stop - cancel every running command (releases scripted keys and movement)");
		Registry.Add("jobs", AB_Cmd_Jobs, "jobs - list running commands");
	}

	// Picks up the World registrar once, and the Mission registrar again after every reload.
	protected void UpdateRegistrars()
	{
		string name;
		if (!m_WorldRegistered)
		{
			name = "AB_WorldCommands";
			typename w = name.ToType();
			if (w)
			{
				Registry.AddRegistrar(AB_Registrar.Cast(w.Spawn()), false);
				m_WorldRegistered = true;
			}
		}
		name = "AB_MissionCommands";
		typename mt = name.ToType();
		if (mt && mt != m_MissionRegistrar)
		{
			if (m_MissionRegistrar)
			{
				// the Mission module was recompiled: its old command classes are stale
				AB_Log("mission scripts reloaded");
				for (int i = Jobs.Count() - 1; i >= 0; i--)
				{
					if (Jobs[i].FromMission && Jobs[i].Command != null)
					{
						Jobs[i].Error = "the game reloaded its mission scripts (mission change) while this ran";
						Finish(Jobs[i]);
					}
				}
				Registry.ForgetMission();
				for (int k = Registry.Registrars.Count() - 1; k >= 0; k--)
				{
					AB_Registrar old = Registry.Registrars[k];
					if (old == null || old.Type() == m_MissionRegistrar)
						Registry.Registrars.Remove(k);
				}
			}
			m_MissionRegistrar = mt;
			Registry.AddRegistrar(AB_Registrar.Cast(mt.Spawn()), true);
		}
	}

	// Which bridge queues this game takes commands from: a client takes its instance's,
	// a dedicated server takes "server", and an offline game (client and server in one) both.
	string Targets()
	{
		if (GetGame().IsDedicatedServer())
			return "server";
		if (IsOfflineGame())
			return Instance + ",server";
		return Instance;
	}

	static bool IsOfflineGame()
	{
		Mission m = GetGame().GetMission();
		if (m == null)
			return false;
		return m.IsMissionGameplay() && GetGame().IsServer() && !GetGame().IsMultiplayer();
	}

	string Side()
	{
		if (GetGame().IsDedicatedServer())
			return "server";
		if (IsOfflineGame())
			return "offline";
		return "client";
	}

	protected void EnsureRest()
	{
		if (m_PollCtx)
			return;
		RestApi api = GetRestApi();
		if (!api)
			api = CreateRestApi();
		api.SetOption(ERestOption.ERESTOPTION_READOPERATION, 15);
		api.SetOption(ERestOption.ERESTOPTION_CONNECTION, 3);
		string base = "http://127.0.0.1:" + Port + "/";
		// two contexts so a long poll never holds up a result
		m_PollCtx = api.GetRestContext(base + "poll/");
		m_PostCtx = api.GetRestContext(base + "result/");
		m_PostCtx.SetHeader("application/json");
		m_PollCb = new AB_PollCallback;
	}

	void Update(float dt)
	{
		FrameTime = dt;
		Uptime += dt;
		UpdateRegistrars();
		EnsureRest();
		// a registrar from an unloaded Mission module is freed under us: drop it
		for (int ri = Registry.Registrars.Count() - 1; ri >= 0; ri--)
		{
			AB_Registrar r = Registry.Registrars[ri];
			if (r == null)
				Registry.Registrars.Remove(ri);
			else
				r.OnFrame(dt);
		}

		if (m_Running)
		{
			// the last command's code didn't return: a script error aborted it
			AB_Ctx broken = m_Running;
			m_Running = null;
			broken.Error = "script error inside the command (see the script log)";
			Finish(broken);
		}

		if (!m_Polling)
		{
			m_RetryIn -= dt;
			if (m_RetryIn <= 0)
				Poll();
		}

		while (Incoming.Count() > 0)
		{
			AB_Ctx n = Incoming[0];
			Incoming.RemoveOrdered(0);
			StartJob(n);
		}

		for (int i = Jobs.Count() - 1; i >= 0; i--)
		{
			if (i >= Jobs.Count())
				continue;
			AB_Ctx c = Jobs[i];
			if (c.Command == null)
			{
				// its class lived in a Mission module that has been unloaded
				c.Error = "the game reloaded its mission scripts (mission change) while this ran";
				Finish(c);
				continue;
			}
			c.Elapsed += dt;
			if (c.Elapsed > c.Timeout)
			{
				c.Command.Cancel();	// (non-null: checked above)
				c.Error = "timed out after " + c.Timeout + "s";
				Finish(c);
				continue;
			}
			m_Running = c;
			bool done = c.Command.Tick(dt);
			m_Running = null;
			if (done || c.Error != "")
				Finish(c);
		}
	}

	protected void Poll()
	{
		m_Polling = true;
		string q = "?targets=" + Targets() + "&side=" + Side() + "&version=" + AB_VERSION + "&wait=4";
		m_PollCtx.GET(m_PollCb, q);
	}

	void OnPollData(string data)
	{
		m_Polling = false;
		m_RetryIn = 0;
		if (m_Failures > 0)
			AB_Log("bridge reachable again");
		m_Failures = 0;
		if (data == "" || data == "{}")
			return;
		AB_Request req = new AB_Request;
		string err;
		JsonSerializer js = new JsonSerializer;
		bool parsed = js.ReadFromString(req, data, err);
		if (!parsed || req.cmd == "")
		{
			AB_Log("bad request: " + err);
			return;
		}
		AB_Ctx c = new AB_Ctx;
		c.Id = req.id;
		c.Cmd = req.cmd;
		if (req.args)
		{
			for (int i = 0; i + 1 < req.args.Count(); i += 2)
				c.Args.Set(req.args[i], req.args[i + 1]);
		}
		Incoming.Insert(c);
	}

	void OnPollFailed(string why)
	{
		m_Polling = false;
		m_Failures++;
		// back off while the bridge is down, but stay responsive once it is back
		m_RetryIn = Math.Min(0.25 * m_Failures, 2);
		if (m_Failures == 1)
			AB_Log("bridge not reachable (" + why + "), retrying");
	}

	void OnPosted(AB_PostCallback cb)
	{
		int i = m_PostCbs.Find(cb);
		if (i >= 0)
			m_PostCbs.Remove(i);
	}

	protected void StartJob(AB_Ctx c)
	{
		AB_CommandInfo info;
		if (!Registry.Commands.Find(c.Cmd, info))
		{
			c.Error = "unknown command '" + c.Cmd + "' (try: help)";
			Post(c);
			return;
		}
		c.Command = AB_Command.Cast(info.Type.Spawn());
		c.FromMission = info.FromMission;
		c.Command.c = c;
		c.Timeout = c.Num("timeout", c.Command.DefaultTimeout());
		m_Running = c;
		bool done = c.Command.Start();
		m_Running = null;
		if (done || c.Error != "")
		{
			Post(c);
			return;
		}
		Jobs.Insert(c);
	}

	protected void Finish(AB_Ctx c)
	{
		int i = Jobs.Find(c);
		if (i >= 0)
			Jobs.RemoveOrdered(i);
		Post(c);
	}

	// Stops every running command except `keep`; returns how many were stopped.
	int StopAll(AB_Ctx keep = null)
	{
		int n = 0;
		for (int i = Jobs.Count() - 1; i >= 0; i--)
		{
			AB_Ctx c = Jobs[i];
			if (c == keep)
				continue;
			if (c.Command != null)
				c.Command.Cancel();
			c.Error = "stopped";
			Finish(c);
			n++;
		}
		foreach (AB_Registrar r2 : Registry.Registrars)
		{
			if (r2 != null)
				r2.OnStopAll();
		}
		return n;
	}

	protected void Post(AB_Ctx c)
	{
		string body = "{\"id\":" + AB_J.Esc(c.Id) + ",\"cmd\":" + AB_J.Esc(c.Cmd);
		if (c.Error != "")
			body += ",\"ok\":false,\"error\":" + AB_J.Esc(c.Error);
		else
			body += ",\"ok\":true";
		if (!c.R.IsEmpty())
			body += ",\"result\":" + c.R.Text();
		body += "}";
		AB_PostCallback cb = new AB_PostCallback;
		m_PostCbs.Insert(cb);
		m_PostCtx.POST(cb, "", body);
	}
}

// ---------------------------------------------------------------- general commands

class AB_Cmd_Ping : AB_Command
{
	override bool Start()
	{
		AB_Agent a = AB_Agent.Get();
		c.R.Str("version", AB_VERSION).Str("side", a.Side()).Str("instance", a.Instance);
		c.R.Num("frame_ms", a.FrameTime * 1000).Num("uptime", a.Uptime);
		return true;
	}
}

class AB_Cmd_Help : AB_Command
{
	override bool Start()
	{
		AB_Registry reg = AB_Agent.Get().Registry;
		string only = c.Str("name");
		array<string> names = new array<string>;
		foreach (string n, AB_CommandInfo info : reg.Commands)
		{
			if (only == "" || n == only)
				names.Insert(n);
		}
		names.Sort();
		AB_JArr list = new AB_JArr;
		foreach (string name : names)
			list.Str(reg.Commands.Get(name).Usage);
		c.R.Arr("commands", list);
		return true;
	}
}

class AB_Cmd_Stop : AB_Command
{
	override bool Start()
	{
		c.R.Int("stopped", AB_Agent.Get().StopAll(c));
		return true;
	}
}

class AB_Cmd_Jobs : AB_Command
{
	override bool Start()
	{
		AB_JArr list = new AB_JArr;
		array<ref AB_Ctx> jobs = AB_Agent.Get().Jobs;
		foreach (AB_Ctx j : jobs)
		{
			AB_JObj o = new AB_JObj;
			o.Str("cmd", j.Cmd).Str("id", j.Id).Num("elapsed", j.Elapsed);
			list.Obj(o);
		}
		c.R.Arr("jobs", list);
		return true;
	}
}

// ---------------------------------------------------------------- the per-frame hook

modded class DayZGame
{
	bool AB_IsLoading()
	{
		return m_loading && m_loading.IsLoading();
	}

	override void OnUpdate(bool doSim, float timeslice)
	{
		super.OnUpdate(doSim, timeslice);
		AB_Agent.Tick(timeslice);
	}
}
