using System.Linq;
using EpicGames.Core;
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
				// Public/HandlerUtils.h includes EditorAssetLibrary.h, the Asset
				// Registry and Kismet2 headers, so a module including it needs these.
				"AssetRegistry",
				"EditorScriptingUtilities",
				"UnrealEd",
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
				// UAnimPoseExtensions / FAnimPoseEvaluationOptions (AnimPose.h),
				// the engine's own pose evaluator, used by animation(sample_pose)
				// and animation(measure_natural_speed).
				"AnimationBlueprintLibrary",
				"AnimationModifiers",
				"AssetTools",
				// ISourceControlModule / ISourceControlProvider, behind source_control(*).
				"SourceControl",
				"AudioEditor",
				"AudioMixer",
				"MetasoundEngine",
				"MetasoundFrontend",
				"MetasoundGraphCore",
				"Synthesis",
				"BSPUtils",
				"BlueprintEditorLibrary",
				"BlueprintGraph",
				"Blutility",
				"Chooser",
				"ControlRig",
				"ControlRigDeveloper",
				"ControlRigEditor",
				"RigVMDeveloper",
				"DataValidation",
				"EditorSubsystem",
				"EnhancedInput",
				"Foliage",
				"GameProjectGeneration",
				"GameplayAbilities",
				// IGameplayTagsEditorModule::AddNewGameplayTagToINI, which
				// reflection(create_tag) uses to register a tag without a restart.
				"GameplayTagsEditor",
				"IKRig",
				"IKRigEditor",
				"ImageWrapper",
				"InputCore",
				"Landscape",
				"LevelEditor",
				// Localization targets and their gather/compile configs
				// (LocalizationSettings, LocalizationConfigurationScript), and the
				// GatherText commandlet runner the Localization Dashboard uses.
				"Localization",
				"LocalizationCommandletExecution",
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
				// UPCGLevelToAsset, which pcg(export_level_to_asset) drives.
				"PCGEditor",
				// UBodySetupCore::GetCollisionTraceFlag resolves the project default trace flag.
				"PhysicsCore",
				"PoseSearch",
				"PoseSearchEditor",
				"PropertyBindingUtils",
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
				// UE::Trace::IsChannel, ToggleChannel, EnumerateChannels and
				// GetStatistics, which editor(start_trace) and its channel actions
				// call. Core includes the header but does not re-export these.
				"TraceLog",
				// ITraceServicesModule and its analysis providers (frames,
				// timers, threads, bookmarks), which editor(analyze_trace)
				// reads a recorded .utrace through. A Developer module, as the
				// editor modules above are; Unreal Insights is built on it.
				"TraceServices",
				// The engine-status snapshot, in its own module so it can load
				// at PostConfigInit and cover the startup window that exists
				// before this module does.
				"UE_MCP_BridgeStatus",
				"RenderCore",
				"RHI",
				"UMG",
				"UMGEditor",
				// The pie category: its toolbar Record button and PIE Studio panel, and the
				// runtime half (input injector, remote-player control, viewport capture)
				// that separate-process PIE clients load.
				"ToolMenus",
				"WorkspaceMenuStructure",
				"EditorStyle",
				"Kismet",
				// Checkpoint asset diffs: FGraphDiffControl and FRevisionInfo.
				"GraphEditor",
				"AssetDefinition",
				"UE_MCP_BridgeRuntime",
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

		// pie(uat_run) validates scenario regex patterns with ICU, the engine's regex backend.
		if (Target.bCompileICU)
		{
			AddEngineThirdPartyPrivateStaticDependencies(Target, "ICU");
		}

		// LiveCoding is Windows-only (Developer/Windows/LiveCoding)
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.Add("LiveCoding");
		}

		// Fab is Epic's marketplace plugin. It is not part of the engine install:
		// the launcher delivers it as its own catalog item ("Fab UE Plugin", app
		// FabPlugin_<version>, flagged to install with the engine) into
		// Engine/Plugins/Fab. An engine installed without it, a source build, or
		// a project that disables it has no Fab, so we do not hard depend on it:
		// detect the plugin on disk and only then link its native import/cache
		// API, guarding those code paths with WITH_FAB_PLUGIN. When absent, the
		// Fab handlers still register, fall back to console-command paths
		// (login/sync/clear) when the module is loaded, and otherwise answer
		// PLUGIN_NOT_AVAILABLE with the install step.
		// Test for the header this module actually includes, not just the
		// plugin directory: 5.4 ships a Fab plugin whose public surface is
		// FabModule.h alone, with no Importers/ or Utilities/ headers, so a
		// directory check turns WITH_FAB_PLUGIN on and the compile then fails
		// on the missing include. Fab is enabled by default, so only a project
		// that turns it off explicitly compiles it out.
		bool bFabPluginPresent = FindFabPlugin(EngineDirectory) != null;
		if (bFabPluginPresent && Target.bBuildEditor && ProjectEnables(Target, "Fab", true))
		{
			PrivateDependencyModuleNames.Add("Fab");
			PublicDefinitions.Add("WITH_FAB_PLUGIN=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_FAB_PLUGIN=0");
		}

		// Epic's Unreal MCP plugin (5.8+, off by default). Headers only, never
		// linked: the handlers reach its module through virtual calls once it is
		// loaded, so this DLL loads whether or not a project enables the plugin.
		bool bEpicMCPPresent = System.IO.File.Exists(System.IO.Path.Combine(
			EngineDirectory, "Plugins", "Experimental", "ModelContextProtocol", "Source", "ModelContextProtocol", "Public", "IModelContextProtocolResourceProvider.h"));
		// Its headers include UHT-generated ones, which exist only when the project
		// being built enables the plugin; elsewhere the handlers compile out (ProjectEnables).
		if (bEpicMCPPresent && Target.bBuildEditor && ProjectEnables(Target, "ModelContextProtocol"))
		{
			PrivateIncludePathModuleNames.Add("ModelContextProtocol");
			PrivateDefinitions.Add("WITH_UEMCP_EPIC_MCP=1");
		}
		else
		{
			PrivateDefinitions.Add("WITH_UEMCP_EPIC_MCP=0");
		}

		// Epic's ToolsetRegistry in 5.8 keeps its process-wide registry as a
		// member of UToolsetRegistrySubsystem, declared in this module's public
		// headers. Headers only, never linked: epic(set_toolset_enabled) reads the
		// member's address from the layout and calls the registry's exported
		// methods by name. 5.9 moved the subsystem to an editor module and added
		// FToolsetRegistry::Get(), so the header is not needed there.
		bool bToolsetRegistrySubsystemHeader = System.IO.File.Exists(System.IO.Path.Combine(
			EngineDirectory, "Plugins", "Experimental", "ToolsetRegistry", "Source", "ToolsetRegistry", "Public", "ToolsetRegistry", "ToolsetRegistrySubsystem.h"));
		if (bToolsetRegistrySubsystemHeader && Target.bBuildEditor && ProjectEnables(Target, "ToolsetRegistry"))
		{
			PrivateIncludePathModuleNames.Add("ToolsetRegistry");
			PrivateDefinitions.Add("WITH_UEMCP_TOOLSET_REGISTRY_SUBSYSTEM=1");
		}
		else
		{
			PrivateDefinitions.Add("WITH_UEMCP_TOOLSET_REGISTRY_SUBSYSTEM=0");
		}

		// Epic's Semantic Search plugin (5.8+, experimental, off by default).
		// Headers only, never linked: asset(semantic_search) reaches it through
		// virtual calls on its module interface and embedding provider, so this
		// DLL loads whether or not a project enables the plugin.
		bool bSemanticSearchPresent = System.IO.File.Exists(System.IO.Path.Combine(
			EngineDirectory, "Plugins", "Experimental", "SemanticSearch", "Source", "SemanticSearch", "Public", "ISemanticSearchModule.h"));
		if (bSemanticSearchPresent && Target.bBuildEditor && ProjectEnables(Target, "SemanticSearch"))
		{
			PrivateIncludePathModuleNames.Add("SemanticSearch");
			PrivateDefinitions.Add("WITH_UEMCP_SEMANTIC_SEARCH=1");
		}
		else
		{
			PrivateDefinitions.Add("WITH_UEMCP_SEMANTIC_SEARCH=0");
		}

		// The Neural Network Engine core is an engine module, not a plugin, so it
		// links like any other runtime module where the engine has it. The
		// runtimes (NNERuntimeORT, NNERuntimeLiteRT, ...) are plugins that
		// register with it at load and are never linked.
		bool bNNEPresent = System.IO.File.Exists(System.IO.Path.Combine(
			EngineDirectory, "Source", "Runtime", "NNE", "Public", "NNERuntimeCPU.h"));
		if (bNNEPresent)
		{
			PrivateDependencyModuleNames.Add("NNE");
			PrivateDefinitions.Add("WITH_UEMCP_NNE=1");
		}
		else
		{
			PrivateDefinitions.Add("WITH_UEMCP_NNE=0");
		}
	}

	/// <summary>
	/// Whether the project being built enables an Epic plugin that is off by
	/// default and whose headers this module includes. Such a plugin's
	/// UHT-generated headers exist only when it is part of the build, so
	/// compiling against it in a project that leaves it off fails. Read from the
	/// target's own plugin lists, then the .uproject. A build with no project
	/// (an engine build) counts it as on. A plugin enabled by default (Fab)
	/// passes bEnabledByDefault, so only an explicit "Enabled": false turns it
	/// off.
	/// </summary>
	private static bool ProjectEnables(ReadOnlyTargetRules Target, string Plugin, bool bEnabledByDefault = false)
	{
		if (Target.DisablePlugins.Contains(Plugin)) return false;
		if (Target.EnablePlugins.Contains(Plugin)) return true;
		if (Target.ProjectFile == null) return true;
		JsonObject Project;
		JsonObject[] Plugins;
		if (!JsonObject.TryRead(Target.ProjectFile, out Project) || !Project.TryGetObjectArrayField("Plugins", out Plugins)) return bEnabledByDefault;
		foreach (JsonObject Entry in Plugins)
		{
			string Name;
			bool Enabled;
			if (Entry.TryGetStringField("Name", out Name) && Name == Plugin) return Entry.TryGetBoolField("Enabled", out Enabled) && Enabled;
		}
		return bEnabledByDefault;
	}

	/// <summary>
	/// The directory of a Fab plugin whose native import/cache headers this
	/// module can compile against, or null. The launcher installs the "Fab UE
	/// Plugin" item into Engine/Plugins/Fab; an older vault install or a copy
	/// made by hand sits under Engine/Plugins/Marketplace instead, in a folder
	/// of any name holding Fab.uplugin.
	/// </summary>
	private static string FindFabPlugin(string EngineDir)
	{
		System.Collections.Generic.List<string> Candidates = new System.Collections.Generic.List<string>();
		Candidates.Add(System.IO.Path.Combine(EngineDir, "Plugins", "Fab"));
		string Marketplace = System.IO.Path.Combine(EngineDir, "Plugins", "Marketplace");
		if (System.IO.Directory.Exists(Marketplace))
		{
			Candidates.AddRange(System.IO.Directory.GetDirectories(Marketplace));
		}
		foreach (string Dir in Candidates)
		{
			if (System.IO.File.Exists(System.IO.Path.Combine(Dir, "Fab.uplugin"))
				&& System.IO.File.Exists(System.IO.Path.Combine(Dir, "Source", "Fab", "Public", "Importers", "GenericAssetImporter.h")))
			{
				return Dir;
			}
		}
		return null;
	}
}
