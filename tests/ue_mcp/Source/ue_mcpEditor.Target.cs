using UnrealBuildTool;
using System.Collections.Generic;

public class ue_mcpEditorTarget : TargetRules
{
	public ue_mcpEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("ue_mcp");
	}
}
