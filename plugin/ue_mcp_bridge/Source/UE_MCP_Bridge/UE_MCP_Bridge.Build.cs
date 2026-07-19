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
				"AnimationModifiers",
				"AssetRegistry",
				"AssetTools",
				"AudioEditor",
				"AudioMixer",
				"AudioExtensions",
				"MetasoundEngine",
				"MetasoundFrontend",
				"MetasoundGraphCore",
				"Synthesis",
				"BSPUtils",
				"BlueprintEditorLibrary",
				"BlueprintGraph",
				"Blutility",
				"Chooser",
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
				"GameProjectGeneration",
				"GameplayAbilities",
				"GameplayTasks",
				"HTTP",
				"IKRig",
				"IKRigDeveloper",
				"IKRigEditor",
				"ImageWrapper",
				"InputCore",
				"Kismet",
				"KismetCompiler",
				"Landscape",
				"LevelEditor",
				"LevelSequence",
				"LevelSequenceEditor",
				"MaterialEditor",
				"MovieScene",
				"MovieSceneTracks",
				"MeshDescription",
				"NavigationSystem",
				"Niagara",
				"NiagaraEditor",
				"PCG",
				"PCGEditor",
				"PoseSearch",
				"PoseSearchEditor",
				"PropertyBindingUtils",
				"PropertyEditor",
				"PythonScriptPlugin",
				"Sequencer",
				"Settings",
				"Slate",
				"SlateCore",
				"StateTreeModule",
				"StateTreeEditorModule",
				"StaticMeshDescription",
				"ClothingSystemRuntimeCommon",
				"ClothingSystemRuntimeInterface",
				"StructUtils",
				"SubobjectDataInterface",
				"ToolMenus",
				"RenderCore",
				"RHI",
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

		// Fab is Epic's marketplace plugin. It ships enabled by default on UE 5.8
		// but is absent on older engines and can be disabled, so we do not hard
		// depend on it: detect the plugin on disk and only then link its native
		// import/cache API, guarding those code paths with WITH_FAB_PLUGIN. When
		// absent, the Fab handlers still register and fall back to console-command
		// paths (login/sync/clear) or return a clean "not available" error.
		bool bFabPluginPresent = System.IO.Directory.Exists(
			System.IO.Path.Combine(EngineDirectory, "Plugins", "Fab"));
		if (bFabPluginPresent && Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("Fab");
			PublicDefinitions.Add("WITH_FAB_PLUGIN=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_FAB_PLUGIN=0");
		}
	}
}
