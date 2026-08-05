using UnrealBuildTool;

/**
 * The engine-status snapshot, split out so it can load at PostConfigInit -
 * long before the bridge module (PostEngineInit) exists. Everything the
 * editor does between those two points (asset registry scan, startup shader
 * compilation, map load, the prompts that fire during them) used to be
 * invisible to ue-mcp because nothing of ours was loaded yet.
 *
 * Dependencies are deliberately minimal: Core carries the slow-task scope
 * stack, the application heartbeat and the threading needed to publish the
 * snapshot. Slate and Engine are NOT dependencies - they are not reliably
 * up this early, so the bridge module injects those richer sources
 * (modal-dialog description, shader/asset compile counts) when it loads.
 */
public class UE_MCP_BridgeStatus : ModuleRules
{
	public UE_MCP_BridgeStatus(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"Json",
			}
		);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Projects",
			}
		);
	}
}
