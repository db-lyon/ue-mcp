using UnrealBuildTool;

// UBT caches this module's file list and rescans only when this file changes.
// scripts/deploy.mjs touches it when it deploys a new source file; bump the
// counter to force a rescan by hand. Rescan counter: 1

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
				// #889: authoring a BehaviorTree means driving UBehaviorTreeGraph
				// and its node classes, then calling UpdateAsset to compile the
				// graph into the runnable UBTCompositeNode tree. That orchestration
				// lives only in BehaviorTreeEditor (with FGraphNodeClassData and
				// UAIGraphNode::AddSubNode coming from AIGraph beneath it) and is
				// not exposed to script, so no reflected runtime API can stand in
				// for it. Both are editor modules, which this plugin already is:
				// its .uplugin declares Type "Editor" and UnrealEd, UMGEditor and
				// MaterialEditor are already linked the same way.
				"AIGraph",
				"BehaviorTreeEditor",
				"MessageLog",
				"AnimationCore",
				"AnimGraph",
				"AnimationEditor",
				// UAnimPoseExtensions / FAnimPoseEvaluationOptions (AnimPose.h),
				// the engine's own pose evaluator, used by animation(sample_pose)
				// and animation(measure_natural_speed).
				"AnimationBlueprintLibrary",
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
				"ControlRigEditor",
				"RigVMDeveloper",
				"DataValidation",
				"EditorScriptingUtilities",
				"EditorStyle",
				"EditorSubsystem",
				"EditorWidgets",
				"EnhancedInput",
				"Foliage",
				"GameProjectGeneration",
				"GameplayAbilities",
				// IGameplayTagsEditorModule::AddNewGameplayTagToINI, which
				// reflection(create_tag) uses to register a tag without a restart.
				"GameplayTagsEditor",
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
				"MainFrame",
				"MaterialEditor",
				"MovieScene",
				"MovieSceneTracks",
				"MeshDescription",
				"NavigationSystem",
				"Niagara",
				"NiagaraEditor",
				// FNiagaraCompileEventSeverity lives here, and reading a compile
				// event's severity through StaticEnum needs the module that
				// generated its reflection data, not just the header.
				"NiagaraShader",
				"PCG",
				"PCGEditor",
				"PoseSearch",
				"PoseSearchEditor",
				"PropertyBindingUtils",
				"PropertyEditor",
				// IPluginManager, IProjectManager and FProjectDescriptor, which
				// project(enable_plugin) writes and widget(audit_commonui) reads.
				// UnrealEd exposes these headers transitively but does not export
				// the symbols, so the include compiles and the link fails.
				"Projects",
				"PythonScriptPlugin",
				"Sequencer",
				"Settings",
				"SkeletalMeshEditor",
				"SkeletalMeshDescription",
				"Slate",
				"SlateCore",
				"StateTreeModule",
				"StateTreeEditorModule",
				"StaticMeshDescription",
				"ClothingSystemRuntimeCommon",
				"ClothingSystemRuntimeInterface",
				"SubobjectDataInterface",
				"ToolMenus",
				// UE::Trace::IsChannel, ToggleChannel, EnumerateChannels and
				// GetStatistics, which editor(start_trace) and its channel actions
				// call. Core includes the header but does not re-export these.
				"TraceLog",
				// The engine-status snapshot, in its own module so it can load
				// at PostConfigInit and cover the startup window that exists
				// before this module does.
				"UE_MCP_BridgeStatus",
				"RenderCore",
				"RHI",
				"UMG",
				"UMGEditor",
				"UnrealEd",
				"WebSockets",
				"WorkspaceMenuStructure",
			}
		);

		// UE 5.4 keeps StructUtils - FInstancedStruct, FStructView, the property
		// bag - in its own experimental plugin module; 5.5 folded all of it into
		// CoreUObject. Chooser and StateTreeModule bring its headers along as
		// public dependencies, so the code compiles on 5.4 without this, and
		// then fails to link: the symbols live in the module itself. Linked
		// explicitly below 5.5, where the module no longer exists to link.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion < 5)
		{
			PrivateDependencyModuleNames.Add("StructUtils");
		}

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
		// Test for the header this module actually includes, not just the
		// plugin directory: 5.4 ships a Fab plugin whose public surface is
		// FabModule.h alone, with no Importers/ or Utilities/ headers, so a
		// directory check turns WITH_FAB_PLUGIN on and the compile then fails
		// on the missing include.
		bool bFabPluginPresent = System.IO.File.Exists(System.IO.Path.Combine(
			EngineDirectory, "Plugins", "Fab", "Source", "Fab", "Public", "Importers", "GenericAssetImporter.h"));
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
