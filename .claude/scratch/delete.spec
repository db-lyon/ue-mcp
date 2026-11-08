	FString Selector;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), Selector)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	// #983: a duplicate label is refused rather than deleted at random. The
-----
	FString Selector;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), Selector)) return Err;

	// The world a spawn's rollback names, so undoing a PIE spawn deletes in PIE.
	const FMCPRuntimeWorld Where = FMCPRuntimeWorld::Resolve(Params, TEXT("editor"));
	if (auto Err = Where.Require(TEXT("Editor world not available"))) return Err;
	UWorld* World = Where.World;

	// #983: a duplicate label is refused rather than deleted at random. The
=====
	MCPSetRollback(Result, TEXT("place_actor"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"), LossNote);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::GetActorDetails(
-----
	Where.Echo(Payload);
	MCPSetRollback(Result, TEXT("place_actor"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), true);
	Result->SetStringField(TEXT("rollbackNote"), LossNote);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::GetActorDetails(
