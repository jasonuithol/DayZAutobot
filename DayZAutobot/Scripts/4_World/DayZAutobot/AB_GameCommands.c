// Game-level commands: state, connecting, quitting, running script. These live in the World
// module so they keep running across the Mission-module reload a join/leave causes.

class AB_Cmd_Status : AB_Command
{
	override bool Start()
	{
		AB_State.Describe(c.R);
		return true;
	}
}

// Waits for a state: wait state=ingame|main_menu ... (comma list allowed)
class AB_Cmd_Wait : AB_Command
{
	ref array<string> m_Want = new array<string>;

	override float DefaultTimeout()
	{
		return 180;
	}

	override bool Start()
	{
		string want = c.Str("state", "ingame");
		want.Split(",", m_Want);
		return Tick(0);
	}

	override bool Tick(float dt)
	{
		string now = AB_State.Name();
		if (m_Want.Find(now) < 0)
			return false;
		// once in game, give the client a moment to settle (inventory/HUD come in a frame later)
		if (now == "ingame" && c.Elapsed < 0.5)
			return false;
		AB_State.Describe(c.R);
		return true;
	}
}

class AB_Cmd_Quit : AB_Command
{
	override bool Start()
	{
		AB_Log("quit requested");
		// let the result reach the bridge first
		GetGame().GetCallQueue(CALL_CATEGORY_SYSTEM).CallLater(GetGame().RequestExit, 750, false, 0);
		return true;
	}
}

// connect address=127.0.0.1 port=2302 [query_port=] [password=] [wait=true] [retry_secs=60]
// A server still holding the previous session (logout timer) bounces the client straight back
// to the menu, so a quick bounce is retried until retry_secs.
class AB_Cmd_Connect : AB_Command
{
	bool m_Wait;
	bool m_LeftMenu;
	float m_MenuSince = -1;
	int m_Attempts;

	override float DefaultTimeout()
	{
		return 240;
	}

	override bool Start()
	{
		if (AB_State.Name() != "main_menu")
			return Fail("connect needs the main menu (state is " + AB_State.Name() + "); disconnect first");
		m_Wait = c.Bool("wait", true);
		Attempt();
		return !m_Wait;
	}

	void Attempt()
	{
		string ip = c.Str("address", "127.0.0.1");
		int port = c.Int("port", 2302);
		int qport = c.Int("query_port", port + 24714);
		m_Attempts++;
		m_LeftMenu = false;
		m_MenuSince = -1;
		AB_Log("connecting to " + ip + ":" + port + " (attempt " + m_Attempts + ")");
		GetDayZGame().ConnectFromServerBrowserEx(ip, port, qport, c.Str("password"));
	}

	override bool Tick(float dt)
	{
		string s = AB_State.Name();
		if (s == "ingame" || s == "dead")
		{
			AB_State.Describe(c.R);
			c.R.Int("attempts", m_Attempts);
			return true;
		}
		if (s != "main_menu")
		{
			m_LeftMenu = true;
			return false;
		}
		if (m_MenuSince < 0)
			m_MenuSince = c.Elapsed;
		// back at (or still on) the menu: give it a moment, then retry or give up
		if (!m_LeftMenu && c.Elapsed - m_MenuSince < 20)
			return false;
		if (m_LeftMenu && c.Elapsed - m_MenuSince < 3)
			return false;
		if (c.Elapsed < c.Num("retry_secs", 60))
		{
			if (GetGame().GetUIManager() && GetGame().GetUIManager().IsDialogVisible())
				GetGame().GetUIManager().CloseDialog();
			Attempt();
			return false;
		}
		return Fail("connection failed after " + m_Attempts + " attempts (see the script log)");
	}
}

// disconnect - leave the server/mission straight away (no logout timer) and wait for the main menu
class AB_Cmd_Disconnect : AB_Command
{
	override float DefaultTimeout()
	{
		return 60;
	}

	override bool Start()
	{
		if (AB_State.Name() == "main_menu")
			return Fail("already at the main menu");
		GetGame().GetMission().AbortMission();
		return false;
	}

	override bool Tick(float dt)
	{
		if (AB_State.Name() != "main_menu")
			return false;
		AB_State.Describe(c.R);
		return true;
	}
}

// exec file=$profile:autobot/exec/x.c - compile a script file in the mission's module and call its
// `string AB_Run()`. The CLI writes the file (exec code="..." / exec file=local.c).
class AB_Cmd_Exec : AB_Command
{
	override bool Start()
	{
		string file = c.Str("file");
		if (file == "")
			return Fail("file= required");
		if (!FileExist(file))
			return Fail("no such file: " + file);
		ScriptModule parent = GetGame().GameScript;
		Mission m = GetGame().GetMission();
		if (m && m.MissionScript)
			parent = m.MissionScript;
		ScriptModule mod = ScriptModule.LoadScript(parent, file, false);
		if (!mod)
			return Fail("compile failed (the script log has the error)");
		string ret;
		mod.CallFunction(null, "AB_Run", ret, null);
		mod.Release();
		c.R.Str("returned", ret);
		return true;
	}
}

void AB_RegisterGameCommands(AB_Registry reg)
{
	reg.Add("status", AB_Cmd_Status, "status - state (main_menu/loading/spawning/ingame/dead/server), menu, mission, world, position");
	reg.Add("wait", AB_Cmd_Wait, "wait [state=ingame] [timeout=180] - block until the game reaches a state (comma list ok)");
	reg.Add("quit", AB_Cmd_Quit, "quit - exit the game");
	reg.Add("connect", AB_Cmd_Connect, "connect [address=127.0.0.1] [port=2302] [query_port=] [password=] [wait=true] [retry_secs=60] - join a server from the main menu (retries while the server still holds the last session)");
	reg.Add("disconnect", AB_Cmd_Disconnect, "disconnect - leave the server or mission immediately, back to the main menu");
	reg.Add("exec", AB_Cmd_Exec, "exec code=\"...\" | file=local.c - run Enforce script; statements, or define string AB_Run(). Server/offline only: the retail client refuses to compile scripts");
}

// The World module's commands. Found by name by the core (3_Game AB_Core.c).
class AB_WorldCommands : AB_Registrar
{
	override void Register(AB_Registry reg)
	{
		AB_RegisterGameCommands(reg);
		AB_RegisterPlayerCommands(reg);
		AB_RegisterServerCommands(reg);
	}

	override void OnFrame(float dt)
	{
		if (!GetGame().IsDedicatedServer())
			AB_Input.NextFrame();
	}

	override void OnStopAll()
	{
		if (GetGame().IsDedicatedServer())
			return;
		AB_Input.ReleaseAll();
		PlayerBase p = PlayerBase.Cast(GetGame().GetPlayer());
		if (p)
			p.AB_Release();
	}
}
