using UnrealBuildTool;

public class ue_mcp : ModuleRules
{
	// Touched when MCPStructKeyMapFixture.h/.cpp were added for #820 and MCPProbeArena.h/.cpp for #2097:
	// UBT caches the module's file list and will not pick up a new file until this changes.
	public ue_mcp(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "GameplayTags", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore" });

		PrivateDependencyModuleNames.AddRange(new string[] {  });
	}
}
