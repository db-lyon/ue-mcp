	Registry.RegisterHandler(TEXT("spawn_nav_modifier_volume"), &SpawnNavModifierVolume, {
		MCPParam::Optional(TEXT("location"), EType::Vec3, TEXT("World location of the volume (default origin)")),
		MCPParam::Optional(TEXT("extent"), EType::Vec3, TEXT("Half-size in world units, positive on every axis (default 100)")),
		MCPParam::Optional(TEXT("areaClass"), EType::String, TEXT("UNavArea subclass: NavArea_Null (default, cuts a hole), NavArea_Obstacle, or a /Script path")),
		MCPParam::Optional(TEXT("label"), EType::String, TEXT("Actor label, also what onConflict dedupes on")),
		MCPParam::Optional(TEXT("scale"), EType::Vec3, TEXT("Actor scale, applied after the brush is built")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("skip (default, returns the actor already carrying label) or error when one does")),
	}, MCPSpec::ContractExempt(TEXT("Spawns a NavModifierVolume under the contract values; nothing it reads fails first")));
-----
	Registry.RegisterHandler(TEXT("spawn_nav_modifier_volume"), &SpawnNavModifierVolume, FMCPSpawn::ParamsWith({
		MCPParam::Optional(TEXT("extent"), EType::Vec3, TEXT("Half-size in world units, positive on every axis (default 100)")),
		MCPParam::Optional(TEXT("areaClass"), EType::String, TEXT("UNavArea subclass: NavArea_Null (default, cuts a hole), NavArea_Obstacle, or a /Script path")),
		MCPParam::Optional(TEXT("scale"), EType::Vec3, TEXT("Actor scale, applied after the brush is built")),
	}, TEXT("volume")), MCPSpec::ContractExempt(TEXT("Spawns a NavModifierVolume under the contract values; nothing it reads fails first")));
=====
#include "Families/MCPCreate.h"
-----
#include "Families/MCPCreate.h"
#include "Families/MCPWorldSpawn.h"
