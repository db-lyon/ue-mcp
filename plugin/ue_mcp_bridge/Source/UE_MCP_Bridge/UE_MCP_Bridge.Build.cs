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
				"AIModule",
				"AnimGraph",
				"AnimationEditor",
				"AssetRegistry",
				"AssetTools",
				"AudioEditor",
				"BlueprintGraph",
				"Blutility",
				"ContentBrowser",
				"DataValidation",
				"EditorScriptingUtilities",
				"EditorStyle",
				"EditorSubsystem",
				"EditorWidgets",
				"EnhancedInput",
				"Foliage",
				"GameplayAbilities",
				"GameplayTasks",
				"ControlRig",
				"ControlRigDeveloper",
				"HTTP",
				"IKRig",
				"IKRigEditor",
				"IKRigDeveloper",
				"InputCore",
				"Kismet",
				"KismetCompiler",
				"Landscape",
				"LevelEditor",
				"LevelSequence",
				"LiveCoding",
				"MaterialEditor",
				"MovieScene",
				"MovieSceneTracks",
				"NavigationSystem",
				"Niagara",
				"NiagaraEditor",
				"PCG",
				"PropertyEditor",
				"PythonScriptPlugin",
				"Sequencer",
				"Slate",
				"SlateCore",
				"SubobjectDataInterface",
				"ToolMenus",
				"UMG",
				"UMGEditor",
				"UnrealEd",
				"WebSockets",
				"WorkspaceMenuStructure",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
			}
		);
	}
}
