using UnrealBuildTool;

// The PIE input injector, the remote-player control component and the game
// viewport reader. They run in every process a PIE session uses, including
// separate-process clients (`-game`), which load no Editor module.
// DeveloperTool keeps the module out of shipping builds.
public class UE_MCP_BridgeRuntime : ModuleRules
{
	public UE_MCP_BridgeRuntime(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"EnhancedInput",
			"InputCore",
			"RenderCore",
			"RHI",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ImageWrapper",
		});
	}
}
