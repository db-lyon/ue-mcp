using UnrealBuildTool;
using System.Collections.Generic;

public class ue_mcpTarget : TargetRules
{
	public ue_mcpTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V5;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("ue_mcp");
	}
}
