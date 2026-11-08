TSharedPtr<FJsonValue> FLevelHandlers::AddComponentToActor(
TSharedPtr<FJsonValue> FLevelHandlers::AddComponentToActor(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentClass"), TEXT("componentName"), TEXT("onConflict"),
	});

	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	FString ComponentClass;
	if (auto Err = RequireString(Params, TEXT("componentClass"), ComponentClass)) return Err;
	FString ComponentName;
	if (auto Err = RequireString(Params, TEXT("componentName"), ComponentName)) return Err;
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	REQUIRE_EDITOR_WORLD(World);

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

	auto Describe = [&](const TSharedPtr<FJsonObject>& Out, const UActorComponent* Component)
	{
		Out->SetStringField(TEXT("actorLabel"), ActorLabel);
		Out->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		Out->SetStringField(TEXT("componentName"), Component->GetName());
		Out->SetStringField(TEXT("componentClass"), Component->GetClass()->GetName());
	};

	// Idempotency: a component of that name already on the actor.
	const FName CompName(*ComponentName);
	for (UActorComponent* Existing : Actor->GetComponents())
	{
		if (!Existing || Existing->GetFName() != CompName) continue;
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("Component '%s' already exists on '%s'"), *ComponentName, *ActorLabel));
		}
		auto ExistingResult = MCPSuccess();
		MCPSetExisted(ExistingResult);
		Describe(ExistingResult, Existing);
		return MCPResult(ExistingResult);
	}

	// (#137) A full path, a short name, or an engine-module implicit lookup.
	UClass* CompClass = MCPWorldSpawn::ResolveComponentClass(ComponentClass);
	if (!CompClass)
	{
		return MCPError(FString::Printf(TEXT("Component class not found: %s. Try the short name (e.g. 'StaticMeshComponent') or the full path ('/Script/Engine.StaticMeshComponent')."), *ComponentClass));
	}
	if (!CompClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("Class '%s' is not an ActorComponent"), *ComponentClass));
	}

	FScopedTransaction Transaction(TEXT("UE_MCP"), FText::FromString(TEXT("add_component_to_actor")), Actor);
	UActorComponent* NewComponent = MCPWorldSpawn::AddInstanceComponent(Actor, CompClass, CompName);
	if (!NewComponent)
	{
		Transaction.Cancel();
		return MCPError(TEXT("Failed to create component"));
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Describe(Result, NewComponent);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetStringField(TEXT("componentName"), NewComponent->GetName());
	MCPSetRollback(Result, TEXT("remove_component_from_actor"), Payload);
	return MCPResult(Result);
}
