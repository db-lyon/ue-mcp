using UnrealBuildTool;
using System.Collections.Generic;

public class ue_mcpEditorTarget : TargetRules
{
	public ue_mcpEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V7;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_8;
		ExtraModuleNames.Add("ue_mcp");
	}
}
