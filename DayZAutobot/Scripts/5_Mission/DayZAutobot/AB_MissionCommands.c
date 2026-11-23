// Mission-module commands: UI, HUD and respawn, which need GUI/mission classes. The engine
// recompiles this module whenever a mission starts or ends; the core re-registers these then.

// hud show=true|false - show or hide the in-game HUD (the CLI hides it for screenshot hud=false)
class AB_Cmd_Hud : AB_Command
{
	override bool Start()
	{
		MissionGameplay mg = MissionGameplay.Cast(GetGame().GetMission());
		if (mg == null || mg.GetHud() == null)
			return Fail("no HUD (not in game)");
		mg.GetHud().Show(c.Bool("show", true));
		return true;
	}
}

// UI: the widgets of the menu on top (or the whole workspace), for clicking mod menus too.
class AB_UI
{
	static string WidgetText(Widget w)
	{
		string t;
		ButtonWidget b = ButtonWidget.Cast(w);
		if (b)
		{
			b.GetText(t);
			if (t != "")
				return t;
		}
		// a button's label is usually a text child
		Widget ch = w.GetChildren();
		while (ch)
		{
			if (ch.IsVisible())
			{
				t = WidgetText(ch);
				if (t != "")
					return t;
			}
			ch = ch.GetSibling();
		}
		return "";
	}

	static void Collect(Widget w, array<Widget> buttons)
	{
		while (w)
		{
			if (w.IsVisible())
			{
				if (w.IsInherited(ButtonWidget))
					buttons.Insert(w);
				Collect(w.GetChildren(), buttons);
			}
			w = w.GetSibling();
		}
	}

	static UIScriptedMenu TopMenu()
	{
		if (!GetGame().GetUIManager())
			return null;
		return GetGame().GetUIManager().GetMenu();
	}

	static Widget Root()
	{
		UIScriptedMenu m = TopMenu();
		if (m && m.GetLayoutRoot())
			return m.GetLayoutRoot();
		return GetGame().GetWorkspace();
	}
}

// ui - list the visible buttons (name and label) of the menu on top
class AB_Cmd_Ui : AB_Command
{
	override bool Start()
	{
		UIScriptedMenu m = AB_UI.TopMenu();
		if (m)
			c.R.Str("menu", m.ClassName()).Int("menu_id", m.GetID());
		array<Widget> buttons = new array<Widget>;
		Widget root = AB_UI.Root();
		if (root)
			AB_UI.Collect(root.GetChildren(), buttons);
		AB_JArr list = new AB_JArr;
		foreach (Widget b : buttons)
		{
			AB_JObj o = new AB_JObj;
			o.Str("name", b.GetName()).Str("text", AB_UI.WidgetText(b));
			list.Obj(o);
		}
		c.R.Arr("buttons", list);
		return true;
	}
}

// click name=<widget name> | text=<label> - click a button on the menu on top
class AB_Cmd_Click : AB_Command
{
	override bool Start()
	{
		array<Widget> buttons = new array<Widget>;
		Widget root = AB_UI.Root();
		if (root)
			AB_UI.Collect(root.GetChildren(), buttons);
		string want = c.Str("text");
		want.ToLower();
		Widget hit;
		foreach (Widget b : buttons)
		{
			string label = AB_UI.WidgetText(b);
			label.ToLower();
			if ((c.Has("name") && b.GetName() == c.Str("name")) || (want != "" && label == want))
			{
				hit = b;
				break;
			}
		}
		if (!hit)
			return Fail("no visible button like that (see: ui)");
		UIScriptedMenu m = AB_UI.TopMenu();
		bool handled;
		if (m)
			handled = m.OnClick(hit, 0, 0, 0);
		c.R.Str("clicked", hit.GetName()).Bool("handled_by_menu", handled);
		return true;
	}
}

// menu [close=true] [open=<menu id>] - what menu is open; close the top one or open one by id
class AB_Cmd_Menu : AB_Command
{
	override bool Start()
	{
		UIManager ui = GetGame().GetUIManager();
		if (!ui)
			return Fail("no UI");
		if (c.Bool("close", false) && ui.GetMenu())
			ui.GetMenu().Close();
		if (c.Has("open"))
			ui.EnterScriptedMenu(c.Int("open"), null);
		UIScriptedMenu m = ui.GetMenu();
		if (m)
			c.R.Str("menu", m.ClassName()).Int("menu_id", m.GetID());
		else
			c.R.Null("menu");
		return true;
	}
}

// respawn [random=true] - after death, respawn through the in-game menu and wait for the new character
class AB_Cmd_Respawn : AB_Command
{
	bool m_Requested;
	Object m_Old;

	override float DefaultTimeout()
	{
		return 120;
	}

	override bool Start()
	{
		PlayerBase p = PlayerBase.Cast(GetGame().GetPlayer());
		if (AB_State.Name() == "main_menu")
			return Fail("not in a game (state main_menu)");
		if (p && p.IsAlive() && !p.IsUnconscious())
			return Fail("the player is alive");
		m_Old = p;
		return false;
	}

	override bool Tick(float dt)
	{
		PlayerBase p = PlayerBase.Cast(GetGame().GetPlayer());
		if (m_Requested)
		{
			if (p && p != m_Old && p.IsAlive() && AB_State.Name() == "ingame")
			{
				AB_State.Describe(c.R);
				return true;
			}
			return false;
		}
		InGameMenu menu = InGameMenu.Cast(GetGame().GetUIManager().FindMenu(MENU_INGAME));
		if (!menu)
		{
			if (c.Elapsed > 0.5 && !GetGame().GetUIManager().IsMenuOpen(MENU_INGAME))
				GetGame().GetUIManager().EnterScriptedMenu(MENU_INGAME, null);
			return false;
		}
		menu.AB_Respawn(c.Bool("random", true));
		m_Requested = true;
		return false;
	}
}


modded class InGameMenu
{
	// what the respawn dialog's buttons end up doing
	void AB_Respawn(bool random)
	{
		GameRespawn(random);
	}
}

class AB_MissionCommands : AB_Registrar
{
	override void Register(AB_Registry reg)
	{
	reg.Add("hud", AB_Cmd_Hud, "hud show=true|false - show or hide the HUD");
	reg.Add("ui", AB_Cmd_Ui, "ui - the menu on top and its visible buttons (name, label)");
	reg.Add("click", AB_Cmd_Click, "click name=<widget> | text=<label> - click a button on the menu on top");
	reg.Add("menu", AB_Cmd_Menu, "menu [close=true] [open=<MENU_ id>] - show/close/open menus");
		reg.Add("respawn", AB_Cmd_Respawn, "respawn [random=true] - respawn after death and wait for the new character");
	}
}
