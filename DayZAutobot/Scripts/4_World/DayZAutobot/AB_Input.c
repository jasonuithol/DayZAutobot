// Scripted presses of the action inputs (UADefaultAction = left mouse, UAAction = F, ...).
// The game's ActionInput.Update reads the real key state; while a simulated press exists for
// its input we feed it that instead, so actions run exactly as if the key had been used:
// same targets, same conditions, same client/server flow.

class AB_Press : Managed
{
	bool Hold;			// held until released / Until; otherwise a single click
	float Until;		// tick time the hold ends (0 = until released)
	int ClickFrame = -1;	// frame a click is being delivered in
	bool Done;
}

class AB_Input
{
	static ref map<int, ref AB_Press> s_Presses = new map<int, ref AB_Press>;
	static int s_Frame;

	static int InputId(string name)
	{
		UAInput inp = GetUApi().GetInputByName(name);
		if (!inp)
			return -1;
		return inp.ID();
	}

	static bool Click(string name)
	{
		return ClickId(InputId(name));
	}

	static bool Hold(string name, float secs)
	{
		return HoldId(InputId(name), secs);
	}

	static bool ClickId(int id)
	{
		if (id < 0)
			return false;
		s_Presses.Set(id, new AB_Press);
		return true;
	}

	static bool HoldId(int id, float secs)
	{
		if (id < 0)
			return false;
		AB_Press p = new AB_Press;
		p.Hold = true;
		if (secs > 0)
			p.Until = GetGame().GetTickTime() + secs;
		s_Presses.Set(id, p);
		return true;
	}

	static void Release(string name)
	{
		s_Presses.Remove(InputId(name));
	}

	static void ReleaseId(int id)
	{
		s_Presses.Remove(id);
	}

	static void ReleaseAll()
	{
		s_Presses.Clear();
	}

	static bool IsPressed(string name)
	{
		return s_Presses.Contains(InputId(name));
	}

	// Called once per frame by the agent: retires finished clicks and expired holds.
	static void NextFrame()
	{
		s_Frame++;
		float now = GetGame().GetTickTime();
		array<int> gone = new array<int>;
		// (plain ifs: Enforce has mis-evaluated long &&/|| chains over fields)
		foreach (int id, AB_Press p : s_Presses)
		{
			bool retire = false;
			if (p.Done)
				retire = true;
			if (!p.Hold)
			{
				if (p.ClickFrame >= 0)
				{
					if (p.ClickFrame < s_Frame)
						retire = true;
				}
			}
			else if (p.Until > 0)
			{
				if (now >= p.Until)
					retire = true;
			}
			if (retire)
				gone.Insert(id);
		}
		foreach (int g : gone)
			s_Presses.Remove(g);
	}

	// A key held for one action: let go of it the moment that action ends (from OnActionEnd), before
	// the game reads the key again and chains the next action on it (get in -> get out -> ...).
	static ActionBase s_HoldFor;
	static int s_HoldKey = -1;

	static void HoldFor(ActionBase action, int key)
	{
		s_HoldFor = action;
		s_HoldKey = key;
	}

	static void OnActionEnded(ActionBase action)
	{
		if (action == null || action != s_HoldFor)
			return;
		ReleaseId(s_HoldKey);
		s_HoldFor = null;
		s_HoldKey = -1;
	}

	static AB_Press Find(UAInput inp)
	{
		if (!inp || s_Presses.Count() == 0)
			return null;
		return s_Presses.Get(inp.ID());
	}
}

modded class ActionInput
{
	override void Update()
	{
		AB_Press p;
		if (m_Enabled && m_input)
			p = AB_Input.Find(m_input.InputP());
		if (!p)
		{
			super.Update();
			return;
		}

		bool firstFrame = false;
		if (p.ClickFrame < 0)
		{
			p.ClickFrame = AB_Input.s_Frame;
		}
		if (p.ClickFrame == AB_Input.s_Frame)
			firstFrame = true;

		// The same key drives a tap input (F: Interact) and a hold input (F held: ContinuousInteract).
		// The game tells them apart by timing (LocalClick vs LocalHoldBegin), so a scripted tap must
		// only reach tap inputs and a scripted hold only hold inputs, or a tap on a car door also
		// starts pushing the car.
		switch (m_InputType)
		{
			case ActionInputType.AIT_CONTINUOUS:
				// active while held; a tap never reaches it
				m_JustActivate = p.Hold && firstFrame && !m_Active;
				m_Active = p.Hold;
				break;
			case ActionInputType.AIT_CLICKCONTINUOUS:
				// a tap starts it, the next tap stops it
				m_JustActivate = false;
				if (!p.Hold && firstFrame)
				{
					m_JustActivate = !m_Active;
					m_Active = !m_Active;
				}
				break;
			case ActionInputType.AIT_HOLDSINGLE:
				// fires once when a hold begins
				m_Active = p.Hold && firstFrame;
				m_JustActivate = m_Active;
				break;
			default:
				// single / double click: fires once on a tap
				m_Active = !p.Hold && firstFrame;
				m_JustActivate = m_Active;
				break;
		}
	}
}

modded class ActionManagerClient
{
	int m_AB_Updates;
	int m_AB_Finds;
	string m_AB_Skip;

	array<ActionInput> AB_Inputs()
	{
		return m_OrederedAllActionInput;
	}

	override void Update(int pCurrentCommandID)
	{
		m_AB_Updates++;
		super.Update(pCurrentCommandID);
	}

	override void OnActionEnd()
	{
		if (m_CurrentActionData != null)
			AB_Input.OnActionEnded(m_CurrentActionData.m_Action);
		super.OnActionEnd();
	}

	override protected void FindContextualUserActions(int pCurrentCommandID)
	{
		m_AB_Finds++;
		if (!m_ActionPossible)
			m_AB_Skip = "action not possible (command " + pCurrentCommandID + ")";
		else if (HasHandInventoryReservation())
			m_AB_Skip = "hand inventory reservation";
		else if (g_Game.IsInventoryOpen())
			m_AB_Skip = "inventory open";
		else if (GetRunningAction() != null)
			m_AB_Skip = "running action " + GetRunningAction().ClassName();
		else
			m_AB_Skip = "";
		super.FindContextualUserActions(pCurrentCommandID);
	}

	// For each action the target/held item defines: is it possible, and if not, which check fails.
	void AB_Why(AB_JArr outList)
	{
		ActionTarget t = FindActionTarget();
		// a forced target (actions target=/seat=/component=) is what the inputs use: check that one
		foreach (ActionInput fi : m_OrederedAllActionInput)
		{
			if (fi.m_ForcedTarget != null && fi.m_ForcedTarget.GetObject() != null)
			{
				t = fi.m_ForcedTarget;
				break;
			}
		}
		ItemBase item = FindActionItem();
		int mask = ActionBase.ComputeConditionMask(m_Player, t, item);
		array<Object> sources = new array<Object>;
		if (t && t.GetObject())
			sources.Insert(t.GetObject());
		if (t && t.GetParent())
			sources.Insert(t.GetParent());
		if (item)
			sources.Insert(item);
		foreach (ActionInput ai : m_OrederedAllActionInput)
		{
			foreach (Object src : sources)
			{
				array<ActionBase_Basic> acts;
				src.GetActions(ai.Type(), acts);
				if (!acts)
					continue;
				foreach (ActionBase_Basic ab : acts)
				{
					ActionBase a = ActionBase.Cast(ab);
					if (!a)
						continue;
					AB_JObj o = new AB_JObj;
					o.Str("action", a.ClassName()).Str("input", ai.ClassName()).Str("from", src.GetType());
					string why = "";
					if ((mask & a.m_ConditionMask) != mask)
						why = "condition mask (stance/vehicle/water state)";
					else if (a.HasTarget() && a.m_ConditionTarget && !a.m_ConditionTarget.Can(m_Player, t))
						why = "target condition (" + a.m_ConditionTarget.ClassName() + ": distance/visibility)";
					else if (a.m_ConditionItem && !a.m_ConditionItem.Can(m_Player, item))
						why = "item condition (" + a.m_ConditionItem.ClassName() + ")";
					else if (!a.AB_ActionCondition(m_Player, t, item))
						why = "ActionCondition";
					else if (!a.Can(m_Player, t, item, mask))
						why = "stance or other";
					o.Bool("possible", why == "");
					if (why != "")
						o.Str("fails", why);
					outList.Obj(o);
				}
			}
		}
	}

	// why actions might not be offered (for the actions command)
	void AB_Diagnose(AB_JObj o)
	{
		o.Bool("actions_enabled", m_ActionsEnabled).Bool("action_possible", m_ActionPossible);
		o.Bool("actions_available", m_ActionsAvaibale).Bool("hand_reservation", HasHandInventoryReservation());
		o.Bool("inventory_open", g_Game.IsInventoryOpen()).Int("command_id", m_Player.GetCurrentCommandID());
		o.Int("updates", m_AB_Updates).Int("finds", m_AB_Finds).Str("last_skip", m_AB_Skip).Bool("current_action", m_CurrentActionData != null);
		ActionTarget t = FindActionTarget();
		if (t && t.GetObject())
			o.Str("found_target", t.GetObject().GetType());
		if (m_OrederedAllActionInput)
		{
			AB_JArr ins = new AB_JArr;
			foreach (ActionInput ai : m_OrederedAllActionInput)
			{
				AB_JObj io = new AB_JObj;
				io.Str("input", ai.ClassName()).Int("count", ai.GetPossibleActionsCount()).Bool("enabled", ai.m_Enabled);
				if (ai.GetPossibleAction())
					io.Str("selected", ai.GetPossibleAction().ClassName());
				if (ai.m_ForcedTarget != null && ai.m_ForcedTarget.GetObject() != null)
					io.Str("forced_target", ai.m_ForcedTarget.GetObject().GetType());
				if (ai.m_Target != null && ai.m_Target.GetObject() != null)
					io.Str("target", ai.m_Target.GetObject().GetType());
				ins.Obj(io);
			}
			o.Arr("inputs", ins);
		}
	}
}

// Scripted movement and turning. Input overrides only act on the machine that sets them (the
// client doesn't send them), so the drive state lives on the player on BOTH sides: the client
// sets it and mirrors it to the server with one RPC, and each side feeds it to its own input
// controller in CommandHandler (where the game's own dev bot applies its overrides). The server
// only accepts the RPC when it was started with -autobot, so the mod can't be used to cheat.

const int AB_RPC_DRIVE = 47110001;

class AB_Drive
{
	static int s_ServerEnabled = -1;

	static bool ServerEnabled()
	{
		if (s_ServerEnabled < 0)
		{
			string port;
			s_ServerEnabled = 0;
			if (GetCLIParam("autobot", port) && port.ToInt() > 0)
				s_ServerEnabled = 1;
		}
		return s_ServerEnabled == 1;
	}

	// DayZ heading (radians): 0 = north, positive = west; heading = -atan2(dir.x, dir.z)
	static float HeadingOf(vector dir)
	{
		return -Math.Atan2(dir[0], dir[2]);
	}

	static float Wrap(float a)
	{
		while (a > Math.PI)
			a -= Math.PI2;
		while (a < -Math.PI)
			a += Math.PI2;
		return a;
	}
}

modded class PlayerBase
{
	bool m_AB_Moving;
	float m_AB_Speed;
	float m_AB_Angle;
	bool m_AB_Turning;
	float m_AB_Heading;
	float m_AB_AimY;		// client only: one-shot camera pitch change
	protected float m_AB_LastSent = -1;
	protected float m_AB_SentHeading;
	protected bool m_AB_SentMoving;
	protected bool m_AB_SentTurning;
	protected float m_AB_SentSpeed;
	protected float m_AB_SentAngle;

	void AB_SetMove(bool moving, float speed, float angle)
	{
		m_AB_Moving = moving;
		m_AB_Speed = speed;
		m_AB_Angle = angle;
		if (moving && !m_AB_Turning)
		{
			// hold the current heading so the server walks the same way the client looks
			m_AB_Turning = true;
			m_AB_Heading = GetInputController().GetHeadingAngle();
		}
		AB_Sync(false);
	}

	void AB_SetHeading(bool active, float heading)
	{
		m_AB_Turning = active;
		m_AB_Heading = heading;
		AB_Sync(false);
	}

	void AB_Release()
	{
		m_AB_Moving = false;
		m_AB_Turning = false;
		m_AB_AimY = 0;
		AB_Sync(true);
	}

	float AB_HeadingError()
	{
		return AB_Drive.Wrap(m_AB_Heading - GetInputController().GetHeadingAngle());
	}

	protected void AB_Sync(bool force)
	{
		if (!GetGame().IsMultiplayer() || GetGame().IsServer())
			return;
		float now = GetGame().GetTickTime();
		bool flags = m_AB_Moving != m_AB_SentMoving || m_AB_Turning != m_AB_SentTurning || m_AB_Speed != m_AB_SentSpeed || m_AB_Angle != m_AB_SentAngle;
		bool turned = Math.AbsFloat(AB_Drive.Wrap(m_AB_Heading - m_AB_SentHeading)) > 0.01;
		if (!force && !flags && !(turned && now - m_AB_LastSent > 0.1))
			return;
		ScriptRPC rpc = new ScriptRPC();
		rpc.Write(m_AB_Moving);
		rpc.Write(m_AB_Speed);
		rpc.Write(m_AB_Angle);
		rpc.Write(m_AB_Turning);
		rpc.Write(m_AB_Heading);
		rpc.Send(this, AB_RPC_DRIVE, true, null);
		if (m_AB_LastSent < 0)
			Print("[Autobot] drive: first state sent to the server");
		m_AB_LastSent = now;
		m_AB_SentMoving = m_AB_Moving;
		m_AB_SentTurning = m_AB_Turning;
		m_AB_SentSpeed = m_AB_Speed;
		m_AB_SentAngle = m_AB_Angle;
		m_AB_SentHeading = m_AB_Heading;
	}

	override void OnRPC(PlayerIdentity sender, int rpc_type, ParamsReadContext ctx)
	{
		if (rpc_type != AB_RPC_DRIVE)
		{
			super.OnRPC(sender, rpc_type, ctx);
			return;
		}
		// only a test server started with -autobot listens, and only to the player's own client
		if (!GetGame().IsServer() || !AB_Drive.ServerEnabled())
			return;
		if (!sender || !GetIdentity() || sender.GetId() != GetIdentity().GetId())
		{
			Print("[Autobot] drive: ignored an RPC that isn't from the player's own client");
			return;
		}
		bool moving, turning;
		float speed, angle, heading;
		if (!ctx.Read(moving) || !ctx.Read(speed) || !ctx.Read(angle) || !ctx.Read(turning) || !ctx.Read(heading))
			return;
		if (m_AB_LastSent < 0)
			Print("[Autobot] drive: first state received from " + sender.GetName());
		m_AB_LastSent = 0;
		m_AB_Moving = moving;
		m_AB_Speed = speed;
		m_AB_Angle = angle;
		m_AB_Turning = turning;
		m_AB_Heading = heading;
	}

	override void CommandHandler(float pDt, int pCurrentCommandID, bool pCurrentCommandFinished)
	{
		if (m_AB_Moving || m_AB_Turning || m_AB_AimY != 0)
			AB_ApplyDrive();
		super.CommandHandler(pDt, pCurrentCommandID, pCurrentCommandFinished);
	}

	protected void AB_ApplyDrive()
	{
		HumanInputController hic = GetInputController();
		if (!hic)
			return;
		if (m_AB_Moving)
		{
			hic.OverrideMovementSpeed(HumanInputControllerOverrideType.ONE_FRAME, m_AB_Speed);
			hic.OverrideMovementAngle(HumanInputControllerOverrideType.ONE_FRAME, m_AB_Angle);
		}
		if (m_AB_Turning)
		{
			// a positive aim change lowers the heading
			float err = AB_HeadingError();
			if (Math.AbsFloat(err) > 0.002)
				hic.OverrideAimChangeX(HumanInputControllerOverrideType.ONE_FRAME, Math.Clamp(-err * 0.5, -0.25, 0.25));
		}
		if (m_AB_AimY != 0)
		{
			hic.OverrideAimChangeY(HumanInputControllerOverrideType.ONE_FRAME, m_AB_AimY);
			m_AB_AimY = 0;
		}
	}
}

modded class ActionBase
{
	bool AB_ActionCondition(PlayerBase player, ActionTarget target, ItemBase item)
	{
		return ActionCondition(player, target, item);
	}
}
