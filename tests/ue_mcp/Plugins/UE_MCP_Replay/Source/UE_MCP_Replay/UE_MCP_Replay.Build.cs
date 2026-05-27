using UnrealBuildTool;

public class UE_MCP_Replay : ModuleRules
{
	public UE_MCP_Replay(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Json",
				"JsonUtilities",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetRegistry",
				"EditorStyle",
				"EnhancedInput",
				"InputCore",
				"Kismet",
				"LevelEditor",
				"RenderCore",
				"RHI",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UE_MCP_Bridge",
				"UnrealEd",
				"WorkspaceMenuStructure",
			}
		);
	}
}
