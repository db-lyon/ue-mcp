		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder, used with name")),
		MCPParam::Optional(TEXT("parentClass"), EType::String, TEXT("Parent class: short name or full path (default Actor)")),
		SpecOnConflict,
	}, MCPSpec::ExactlyOne({ { TEXT("assetPath") }, { TEXT("name"), TEXT("packagePath") } }));
-----
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder, used with name (default /Game/Blueprints)")),
		MCPParam::Optional(TEXT("parentClass"), EType::String, TEXT("Parent class: short name or full path (default Actor)")),
		SpecOnConflict,
	}, MCPSpec::ExactlyOne({ { TEXT("assetPath") }, { TEXT("name") } }));
=====
		MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("Full destination of the new Blueprint Interface")).Alias(TEXT("path")),
		SpecOnConflict,
	}, MCPSpec::ContractExempt(TEXT("Creates and saves an interface at the contract path; nothing it reads fails first")));
-----
		MCPParam::Optional(TEXT("assetPath"), EType::String, TEXT("Full destination of the new Blueprint Interface")).Alias(TEXT("path")),
		MCPParam::Optional(TEXT("name"), EType::String, TEXT("Asset name; with packagePath, the same destination as assetPath")),
		MCPParam::Optional(TEXT("packagePath"), EType::String, TEXT("Destination folder, used with name (default /Game/Blueprints)")),
		SpecOnConflict,
	}, MCPSpec::ExactlyOne({ { TEXT("assetPath") }, { TEXT("name") } }).ContractExempt(TEXT("Creates and saves an interface at the contract path; nothing it reads fails first")));
=====
#include "Families/MCPPropertyWrite.h"
-----
#include "Families/MCPPropertyWrite.h"
#include "Families/MCPCreate.h"
