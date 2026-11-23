class CfgPatches
{
	class DayZAutobot
	{
		units[] = {};
		weapons[] = {};
		requiredVersion = 0.1;
		requiredAddons[] = {"DZ_Data", "DZ_Scripts"};
	};
};

class CfgMods
{
	class DayZAutobot
	{
		dir = "DayZAutobot";
		name = "DayZ Autobot";
		author = "tarbaby";
		type = "mod";
		dependencies[] = {"Game", "World", "Mission"};

		class defs
		{
			class gameScriptModule
			{
				value = "";
				files[] = {"DayZAutobot/Scripts/3_Game"};
			};
			class worldScriptModule
			{
				value = "";
				files[] = {"DayZAutobot/Scripts/4_World"};
			};
			class missionScriptModule
			{
				value = "";
				files[] = {"DayZAutobot/Scripts/5_Mission"};
			};
		};
	};
};
