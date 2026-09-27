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

		// The deployed plugin is git-ignored, and adaptive unity reads every ignored
		// file as the working set, so it compiled each plugin file on its own.
		bUseAdaptiveUnityBuild = false;
	}
}
