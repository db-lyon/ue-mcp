	Registry.RegisterHandler(TEXT("add_pcg_volume"), &SpawnPCGVolume, {
		MCPParam::Optional(TEXT("graphPath"), EType::String, TEXT("PCGGraph to assign to the volume's component")),
		MCPParam::Optional(TEXT("location"), EType::Vec3, TEXT("World location {x,y,z} (default origin)")),
		MCPParam::Optional(TEXT("extent"), EType::Vec3, TEXT("Half-size of the volume box {x,y,z} (default 500 on each axis)")),
		MCPParam::Optional(TEXT("label"), EType::String, TEXT("Editor label. Also the idempotency key: an existing actor with this label is reported rather than duplicated")),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("When the label exists: skip (default, report it) | error")),
	}, MCPSpec::ContractExempt(TEXT("Spawns a volume under the contract values; nothing it reads fails first")));
-----
	Registry.RegisterHandler(TEXT("add_pcg_volume"), &SpawnPCGVolume, FMCPSpawn::ParamsWith({
		MCPParam::Optional(TEXT("graphPath"), EType::String, TEXT("PCGGraph to assign to the volume's component; refused when it does not load")),
		MCPParam::Optional(TEXT("extent"), EType::Vec3, TEXT("Half-size of the volume box {x,y,z}, positive on every axis (default 500 on each axis)")),
	}, TEXT("PCG volume")), MCPSpec::ContractExempt(TEXT("Spawns a volume under the contract values; nothing it reads fails first")));
