using UnrealBuildTool;
using System.Collections.Generic;

public class ue_mcpEditorTarget : TargetRules
{
	public ue_mcpEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		ExtraModuleNames.Add("ue_mcp");
	}
}
