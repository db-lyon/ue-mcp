using UnrealBuildTool;

public class UE_MCP_Bridge : ModuleRules
{
	public UE_MCP_Bridge(ReadOnlyTargetRules Target) : base(Target)
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
				"ControlRig",
				"ControlRigDeveloper",
				"DataValidation",
				"EditorScriptingUtilities",
				"EditorStyle",
				"EditorSubsystem",
				"EditorWidgets",
				"EnhancedInput",
				"Foliage",
				"GameplayAbilities",
				"GameplayTasks",
				"HTTP",
				"IKRig",
				"IKRigDeveloper",
				"IKRigEditor",
				"InputCore",
				"Kismet",
				"KismetCompiler",
				"Landscape",
				"LevelEditor",
				"LevelSequence",
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

		// LiveCoding is Windows-only (Developer/Windows/LiveCoding)
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.Add("LiveCoding");
		}
	}
}
