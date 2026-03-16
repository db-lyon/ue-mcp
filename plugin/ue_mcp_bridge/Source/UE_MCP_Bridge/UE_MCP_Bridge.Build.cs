using UnrealBuildTool;

public class UE_MCP_Bridge : ModuleRules
{
	public UE_MCP_Bridge(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.AddRange(
			new string[] {
			}
		);

		PrivateIncludePaths.AddRange(
			new string[] {
			}
		);

			PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"Json",
				"JsonUtilities",
				"GameplayTags",
			}
		);

			PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"BlueprintGraph",
				"KismetCompiler",
				"UnrealEd",
				"EditorStyle",
				"EditorWidgets",
				"PythonScriptPlugin",
				"HTTP",
				"WebSockets",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"AssetTools",
				"AssetRegistry",
				"ContentBrowser",
				"EditorSubsystem",
				"EditorScriptingUtilities",
				"SubobjectDataInterface",
				"PropertyEditor",
				"Kismet",
				"KismetCompiler",
				"BlueprintGraph",
				"MaterialEditor",
				"AnimationEditor",
				"Landscape",
				"PCG",
				"Niagara",
				"NiagaraEditor",
				"UMG",
				"Sequencer",
				"LevelEditor",
				"WorkspaceMenuStructure",
				"NavigationSystem",
				"AIModule",
				"EnhancedInput",
				"GameplayAbilities",
				"InputCore",
				"Foliage",
				"UMGEditor",
				"DataValidation",
				"LevelSequence",
				"MovieScene",
				"MovieSceneTracks",
				"Blutility",
				"GameplayTasks",
				"AudioEditor",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
