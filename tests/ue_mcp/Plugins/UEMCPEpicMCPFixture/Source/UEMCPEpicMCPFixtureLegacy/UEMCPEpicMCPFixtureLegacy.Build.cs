using UnrealBuildTool;

// The legacy MCP Tool Library fixture. Epic removed UModelContextProtocolToolLibrary
// in 5.9, and UHT parses every header in a module whatever #if surrounds it, so the
// class lives in its own module. UEMCPEpicMCPFixture.Build.cs names this module as a
// dynamically loaded dependency only on engines that still ship the base class, and
// it is not listed in the .uplugin, so 5.9 never compiles it.
public class UEMCPEpicMCPFixtureLegacy : ModuleRules
{
	public UEMCPEpicMCPFixtureLegacy(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"ModelContextProtocol",
			"ModelContextProtocolEngine",
		});
	}
}
