using System.IO;
using UnrealBuildTool;

public class UEMCPEpicMCPFixture : ModuleRules
{
	public UEMCPEpicMCPFixture(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Json",
			"JsonUtilities",
			"ModelContextProtocol",
			"ModelContextProtocolEngine",
		});

		// The legacy MCP Tool Library exists through 5.8 and is gone from 5.9. Probe for
		// its header rather than the version number, so a build that drops it early or
		// keeps it late still compiles.
		string LegacyHeader = Path.Combine(EngineDirectory, "Plugins", "Experimental", "ModelContextProtocol",
			"Source", "ModelContextProtocolEngine", "Public", "ModelContextProtocolToolLibrary.h");
		bool bHasLegacyToolLibrary = File.Exists(LegacyHeader);
		if (bHasLegacyToolLibrary)
		{
			DynamicallyLoadedModuleNames.Add("UEMCPEpicMCPFixtureLegacy");
		}
		PrivateDefinitions.Add("UEMCP_FIXTURE_LEGACY_TOOL_LIBRARY=" + (bHasLegacyToolLibrary ? "1" : "0"));
	}
}
