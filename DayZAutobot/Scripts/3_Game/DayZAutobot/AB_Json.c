// Minimal JSON writer. Values are kept as already-encoded JSON text, so objects and arrays
// nest by passing one builder into another (Obj/Arr take a builder and embed its text).

class AB_J
{
	static string Esc(string s)
	{
		s.Replace("\\", "\\\\");
		s.Replace("\"", "\\\"");
		s.Replace("\n", "\\n");
		s.Replace("\r", "\\r");
		s.Replace("\t", "\\t");
		return "\"" + s + "\"";
	}

	static string Num(float f)
	{
		if (f != f || f > 1e30 || f < -1e30)
			return "null";
		// three decimals is plenty for positions/health and keeps output short
		float r = Math.Round(f * 1000) / 1000;
		return r.ToString();
	}

	static string Int(int i)
	{
		return i.ToString();
	}

	static string Bool(bool b)
	{
		if (b)
			return "true";
		return "false";
	}

	static string Vec(vector v)
	{
		return "[" + Num(v[0]) + "," + Num(v[1]) + "," + Num(v[2]) + "]";
	}
}

class AB_JObj : Managed
{
	protected ref array<string> m_Parts = new array<string>;

	AB_JObj Raw(string key, string json)
	{
		m_Parts.Insert(AB_J.Esc(key) + ":" + json);
		return this;
	}

	AB_JObj Str(string key, string v)	{ return Raw(key, AB_J.Esc(v)); }
	AB_JObj Num(string key, float v)	{ return Raw(key, AB_J.Num(v)); }
	AB_JObj Int(string key, int v)		{ return Raw(key, AB_J.Int(v)); }
	AB_JObj Bool(string key, bool v)	{ return Raw(key, AB_J.Bool(v)); }
	AB_JObj Vec(string key, vector v)	{ return Raw(key, AB_J.Vec(v)); }
	AB_JObj Obj(string key, AB_JObj o)	{ return Raw(key, o.Text()); }
	AB_JObj Arr(string key, AB_JArr a)	{ return Raw(key, a.Text()); }
	AB_JObj Null(string key)			{ return Raw(key, "null"); }

	bool IsEmpty()
	{
		return m_Parts.Count() == 0;
	}

	string Text()
	{
		string s = "{";
		for (int i = 0; i < m_Parts.Count(); i++)
		{
			if (i > 0)
				s += ",";
			s += m_Parts[i];
		}
		return s + "}";
	}
}

class AB_JArr : Managed
{
	protected ref array<string> m_Items = new array<string>;

	AB_JArr Raw(string json)	{ m_Items.Insert(json); return this; }
	AB_JArr Str(string v)		{ return Raw(AB_J.Esc(v)); }
	AB_JArr Num(float v)		{ return Raw(AB_J.Num(v)); }
	AB_JArr Int(int v)			{ return Raw(AB_J.Int(v)); }
	AB_JArr Vec(vector v)		{ return Raw(AB_J.Vec(v)); }
	AB_JArr Obj(AB_JObj o)		{ return Raw(o.Text()); }

	int Count()
	{
		return m_Items.Count();
	}

	string Text()
	{
		string s = "[";
		for (int i = 0; i < m_Items.Count(); i++)
		{
			if (i > 0)
				s += ",";
			s += m_Items[i];
		}
		return s + "]";
	}
}
