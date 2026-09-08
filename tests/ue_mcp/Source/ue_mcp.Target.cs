using UnrealBuildTool;
using System.Collections.Generic;

public class ue_mcpTarget : TargetRules
{
	public ue_mcpTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ue_mcp");
	}
}
