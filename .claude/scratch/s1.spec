	FMCPCreate Create(Params, { FriendlyType, TEXT("/Game/Blueprints/GameFramework"), /*bOnConflict=*/false });
-----
	FMCPCreate Create(Params, { FriendlyType, TEXT("/Game/Blueprints/GameFramework") });
=====
	return MCPCreatePreset::BlueprintSubclass(Create, Parent, [](UBlueprint*) {},
		[FriendlyType](const TSharedPtr<FJsonObject>& Result) { Result->SetStringField(TEXT("type"), FriendlyType); });
-----
	return MCPCreatePreset::BlueprintSubclass(Create, Parent);
=====
#include "Families/MCPPropertyWrite.h"
-----
#include "Families/MCPPropertyWrite.h"
#include "Families/MCPCreate.h"
