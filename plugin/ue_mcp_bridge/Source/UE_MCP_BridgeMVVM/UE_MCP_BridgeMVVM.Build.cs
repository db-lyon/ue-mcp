using UnrealBuildTool;

// Viewmodel authoring for widget(*viewmodel*), split out of UE_MCP_Bridge
// because it has to link the UMG Viewmodel (ModelViewViewModel) plugin, which
// ships disabled and which a project may leave disabled. A module importing
// that plugin's DLLs cannot load in a project without it, so this one is
// declared with LoadingPhase "None": nothing loads it at startup, and the
// widget handlers load it on first use, only once the plugin manager reports
// ModelViewViewModel enabled. It registers its backend with UE_MCP_Bridge
// (Public/HandlerMVVMBackend.h) and the handlers call it through that.
//
// The descriptor does not list ModelViewViewModel as a plugin dependency on
// purpose: listing it would enable the plugin in every project that has the
// bridge. UBT notes the unlisted dependency with a warning.
public class UE_MCP_BridgeMVVM : ModuleRules
{
	public UE_MCP_BridgeMVVM(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"UE_MCP_Bridge",
		});

		// Present in every engine the bridge supports (5.4 to 5.9). Checked anyway
		// so an engine built without it still compiles this module, as a stub
		// that tells the handlers the plugin is unavailable.
		bool bMVVMPresent = System.IO.File.Exists(System.IO.Path.Combine(
			EngineDirectory, "Plugins", "Runtime", "ModelViewViewModel", "Source", "ModelViewViewModelEditor", "Public", "MVVMEditorSubsystem.h"));
		if (bMVVMPresent && Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"BlueprintGraph",
				"FieldNotification",
				"Kismet",
				"ModelViewViewModel",
				"ModelViewViewModelBlueprint",
				"ModelViewViewModelEditor",
				"Slate",
				"SlateCore",
				"UMG",
				"UMGEditor",
				"UnrealEd",
			});
			PrivateDefinitions.Add("WITH_UEMCP_MVVM=1");
		}
		else
		{
			PrivateDefinitions.Add("WITH_UEMCP_MVVM=0");
		}
	}
}
