using UnrealBuildTool;

// Live streaming for pie(stream_*), split out of UE_MCP_Bridge because it has
// to link Pixel Streaming 2, which ships disabled and which a project may leave
// disabled. A module importing that plugin's DLLs cannot load in a project
// without it, so this one is declared with LoadingPhase "None": nothing loads
// it at startup, and the stream handlers load it on first use, only once the
// plugin manager reports PixelStreaming2 enabled. It registers its backend with
// UE_MCP_Bridge (Public/HandlerStreamBackend.h) and the handlers call it
// through that.
//
// The descriptor does not list PixelStreaming2 as a plugin dependency on
// purpose: listing it would enable the plugin in every project that has the
// bridge. UBT notes the unlisted dependency with a warning.
public class UE_MCP_BridgeStream : ModuleRules
{
	public UE_MCP_BridgeStream(ReadOnlyTargetRules Target) : base(Target)
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

		// The streamer, video producer and input handler interfaces this module
		// drives took their current shape in 5.7. Older engines, and an engine
		// installed without the plugin, compile this module as a stub that tells
		// the handlers streaming is unavailable.
		string PluginSource = System.IO.Path.Combine(EngineDirectory, "Plugins", "Media", "PixelStreaming2", "Source");
		bool bPresent =
			System.IO.File.Exists(System.IO.Path.Combine(PluginSource, "PixelStreaming2Core", "Public", "IPixelStreaming2Streamer.h")) &&
			System.IO.File.Exists(System.IO.Path.Combine(PluginSource, "PixelStreaming2", "Internal", "VideoProducerPIEViewport.h")) &&
			System.IO.File.Exists(System.IO.Path.Combine(PluginSource, "PixelStreaming2Editor", "Internal", "VideoProducerLevelEditor.h")) &&
			System.IO.File.Exists(System.IO.Path.Combine(PluginSource, "PixelStreaming2Editor", "Internal", "VideoProducerBackBufferComposited.h"));
		bool bEngineHasApi = Target.Version.MajorVersion > 5 || (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 7);
		bool bPlatform = Target.Platform == UnrealTargetPlatform.Win64 || Target.Platform == UnrealTargetPlatform.Linux || Target.Platform == UnrealTargetPlatform.Mac;
		if (bPresent && bEngineHasApi && bPlatform && Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"ApplicationCore",
				"InputCore",
				"LevelEditor",
				"MainFrame",
				"PixelStreaming2",
				"PixelStreaming2Core",
				"PixelStreaming2Editor",
				"PixelStreaming2Input",
				"Slate",
				"SlateCore",
				"UnrealEd",
			});
			// The video producers are declared in the plugin's Internal folders, which UBT
			// shares only with engine modules. Their classes are exported, so naming the
			// folders is all a plugin outside the engine needs to use them.
			PrivateIncludePaths.Add(System.IO.Path.Combine(PluginSource, "PixelStreaming2", "Internal"));
			PrivateIncludePaths.Add(System.IO.Path.Combine(PluginSource, "PixelStreaming2Editor", "Internal"));
			PrivateDefinitions.Add("WITH_UEMCP_PIXELSTREAMING2=1");
		}
		else
		{
			PrivateDefinitions.Add("WITH_UEMCP_PIXELSTREAMING2=0");
		}
	}
}
