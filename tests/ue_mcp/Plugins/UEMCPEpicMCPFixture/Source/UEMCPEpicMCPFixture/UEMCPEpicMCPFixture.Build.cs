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
	}
}
