TSharedPtr<FJsonValue> FGameplayHandlers::CreateSmartObjectDefinition(
TSharedPtr<FJsonValue> FGameplayHandlers::CreateSmartObjectDefinition(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("SmartObjectDefinition"), TEXT("/Game/AI/SmartObjects") });
	if (auto Err = Create.Read()) return Err;

	UClass* SmartObjectDefClass = FindObject<UClass>(nullptr, TEXT("/Script/SmartObjectsModule.SmartObjectDefinition"));
	if (!SmartObjectDefClass)
	{
		return MCPError(TEXT("SmartObjectDefinition class not found. Enable SmartObjects plugin."));
	}

	UObject* Definition = nullptr;
	if (auto Early = Create.Make(Definition, SmartObjectDefClass)) return Early;

	// #833: a definition with slots and no behavior definition anywhere fails
	// the editor's asset check. Setting the default here is the one-call way to
	// make every slot added later legal, and it had no route before.
	const FString DefaultBehavior = OptionalString(Params, TEXT("defaultBehaviorClass"));
	FString DefaultBehaviorClass;
	int32 Index = INDEX_NONE;
	if (!DefaultBehavior.IsEmpty())
	{
		const TSharedPtr<FJsonObject>* InstanceProps = nullptr;
		TryGetObjectParam(Params, TEXT("instanceProperties"), InstanceProps);
		UObject* Instance = nullptr;
		Definition->Modify();
		const FString Err = AppendBehaviorDefinition(
			Definition, Definition->GetClass(), Definition,
			TEXT("DefaultBehaviorDefinitions"), DefaultBehavior, InstanceProps, Instance, Index);
		// A definition whose default silently failed is the broken shape, so it goes.
		if (!Err.IsEmpty()) return Create.Abandon(MCPError(Err));
		Definition->PostEditChange();
		DefaultBehaviorClass = Instance->GetClass()->GetPathName();
	}

	TSharedPtr<FJsonObject> Result = Create.Finish();
	if (!DefaultBehaviorClass.IsEmpty())
	{
		Result->SetStringField(TEXT("defaultBehavior"), DefaultBehaviorClass);
		Result->SetNumberField(TEXT("defaultBehaviorIndex"), Index);
	}
	Result->SetNumberField(TEXT("defaultBehaviorCount"), CountDefaultBehaviorDefinitions(Definition));
	// No slots yet, so the definition is valid either way; the field is set so
	// a caller reads the same key on create as on every slot write.
	Result->SetBoolField(TEXT("definitionValid"), true);
	if (DefaultBehavior.IsEmpty())
	{
		Result->SetStringField(TEXT("note"),
			TEXT("This definition has no default behavior definition. That is legal while it has no slots, but the ")
			TEXT("editor's asset check rejects any slot added later that carries no behavior of its own. Pass ")
			TEXT("defaultBehaviorClass here, or behaviorClass on gameplay(add_smart_object_slot)."));
	}
	return MCPResult(Result);
}
=====
TSharedPtr<FJsonValue> FGameplayHandlers::CreateBlackboard(
TSharedPtr<FJsonValue> FGameplayHandlers::CreateBlackboard(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("BlackboardData"), TEXT("/Game/AI") });
	if (auto Err = Create.Read()) return Err;

	UClass* BlackboardClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.BlackboardData"));
	if (!BlackboardClass)
	{
		return MCPError(TEXT("BlackboardData class not found."));
	}

	UObject* Blackboard = nullptr;
	if (auto Early = Create.Make(Blackboard, BlackboardClass)) return Early;
	return MCPResult(Create.Finish());
}
=====
TSharedPtr<FJsonValue> FGameplayHandlers::CreateBehaviorTree(
TSharedPtr<FJsonValue> FGameplayHandlers::CreateBehaviorTree(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("BehaviorTree"), TEXT("/Game/AI") });
	if (auto Err = Create.Read()) return Err;
	// Read before anything can fail (#1057).
	const FString BlackboardPath = OptionalString(Params, TEXT("blackboardPath"));

	UClass* BTClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.BehaviorTree"));
	if (!BTClass)
	{
		return MCPError(TEXT("BehaviorTree class not found."));
	}

	// Resolved before the asset exists, so a bad path creates nothing.
	UBlackboardData* BB = nullptr;
	if (!BlackboardPath.IsEmpty())
	{
		BB = LoadAssetByPath<UBlackboardData>(BlackboardPath);
		if (!BB) return MCPAssetLoadError(BlackboardPath, TEXT("UBlackboardData"));
	}

	UObject* Created = nullptr;
	if (auto Early = Create.Make(Created, BTClass)) return Early;
	if (BB)
	{
		if (UBehaviorTree* BT = Cast<UBehaviorTree>(Created))
		{
			BT->BlackboardAsset = BB;
			BT->PostEditChange();
		}
	}

	TSharedPtr<FJsonObject> Result = Create.Finish();
	if (BB) Result->SetStringField(TEXT("blackboardPath"), BB->GetPathName());
	return MCPResult(Result);
}
=====
TSharedPtr<FJsonValue> FGameplayHandlers::CreateEqsQuery(
TSharedPtr<FJsonValue> FGameplayHandlers::CreateEqsQuery(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("EnvironmentQuery"), TEXT("/Game/AI/EQS") });
	if (auto Err = Create.Read()) return Err;

	// The asset type reads as "Environment Query" in the editor, but the UClass
	// is UEnvQuery. Looking up the display name found nothing, so this action
	// refused every call it was ever given.
	UClass* EQSClass = FindObject<UClass>(nullptr, TEXT("/Script/AIModule.EnvQuery"));
	if (!EQSClass)
	{
		return MCPError(TEXT(
			"UEnvQuery class not found: the AIModule is not loaded in this editor. "
			"EQS lives in AIModule, which a project with no AI content may never load."));
	}

	UObject* Query = nullptr;
	if (auto Early = Create.Make(Query, EQSClass)) return Early;
	return MCPResult(Create.Finish());
}
=====
TSharedPtr<FJsonValue> FGameplayHandlers::CreateStateTree(
TSharedPtr<FJsonValue> FGameplayHandlers::CreateStateTree(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("StateTree"), TEXT("/Game/AI") });
	if (auto Err = Create.Read()) return Err;

	UClass* STClass = FindObject<UClass>(nullptr, TEXT("/Script/StateTreeModule.StateTree"));
	if (!STClass)
	{
		return MCPError(TEXT("StateTree class not found. Enable StateTree plugin."));
	}

	UObject* Created = nullptr;
	if (auto Early = Create.Make(Created, STClass)) return Early;

	// #653/#681: a bare UStateTree has no EditorData, so every statetree(*)
	// authoring action failed with "EditorData not found" and nothing could be
	// persisted. Mirror UStateTreeFactory: attach a UStateTreeEditorData with a
	// concrete schema and a root state, then compile - so states/tasks are
	// authorable AND serialize with the asset (survive save/load).
	UStateTree* StateTree = Cast<UStateTree>(Created);
	if (!StateTree)
	{
		return MCPError(TEXT("Created asset is not a UStateTree"));
	}

	// #833: the schema is not optional dressing. Without one the compiler bails
	// with "does not have a schema" and the asset is written dead, so a schema
	// that does not resolve is a failed create and the tree goes again.
	MCPStateTreeSchema::FResolution Schema = MCPStateTreeSchema::Resolve(
		OptionalString(Params, TEXT("schema")));
	if (!Schema.SchemaClass) return Create.Abandon(MCPStateTreeSchema::UnresolvedError(Schema));

	const MCPStateTreeSchema::FAttachOutcome Attach =
		MCPStateTreeSchema::AttachSchema(StateTree, Schema.SchemaClass);

	FStateTreeCompilerLog Log;
#if UE_MCP_HAS_5_5_API
	const bool bCompiled = UStateTreeEditingSubsystem::CompileStateTree(StateTree, Log);
#else
	FStateTreeCompiler Compiler(Log);
	const bool bCompiled = Compiler.Compile(*StateTree);
#endif
	StateTree->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = Create.Finish();
	Result->SetStringField(TEXT("schema"), Schema.SchemaClass->GetPathName());
	Result->SetStringField(TEXT("schemaSource"), Schema.Source);
	Result->SetBoolField(TEXT("compiled"), bCompiled);
	Result->SetBoolField(TEXT("createdEditorData"), Attach.bCreatedEditorData);
	if (!Schema.Note.IsEmpty())
	{
		Result->SetStringField(TEXT("schemaNote"), Schema.Note);
		Result->SetArrayField(TEXT("availableSchemas"), MCPStateTreeSchema::ConcreteSchemaPathsJson());
	}
	return MCPResult(Result);
}
=====
static bool ResolveFrameworkParent(
=====
TSharedPtr<FJsonValue> FGameplayHandlers::CreateBlueprintWithParent(
// A Blueprint subclass of the framework class at BasePath, or of the caller's
// parentClass, which must derive from it.
TSharedPtr<FJsonValue> FGameplayHandlers::CreateFrameworkBlueprint(const TSharedPtr<FJsonObject>& Params, const TCHAR* BasePath, const TCHAR* FriendlyType)
{
	FMCPCreate Create(Params, { FriendlyType, TEXT("/Game/Blueprints/GameFramework"), /*bOnConflict=*/false });
	if (auto Err = Create.Read()) return Err;

	UClass* Base = FindObject<UClass>(nullptr, BasePath);
	if (!Base)
	{
		return MCPError(FString::Printf(TEXT("%s class not found: %s"), FriendlyType, BasePath));
	}
	TSharedPtr<FJsonValue> ParentErr;
	UClass* Parent = MCPCreatePreset::ResolveParent(Params, Base, FriendlyType, ParentErr);
	if (!Parent) return ParentErr;

	return MCPCreatePreset::BlueprintSubclass(Create, Parent, [](UBlueprint*) {},
		[FriendlyType](const TSharedPtr<FJsonObject>& Result) { Result->SetStringField(TEXT("type"), FriendlyType); });
}
=====
TSharedPtr<FJsonValue> FGameplayHandlers::CreateGameMode(
TSharedPtr<FJsonValue> FGameplayHandlers::CreateGameMode(const TSharedPtr<FJsonObject>& Params)
{
	return CreateFrameworkBlueprint(Params, TEXT("/Script/Engine.GameModeBase"), TEXT("GameMode"));
}
=====
TSharedPtr<FJsonValue> FGameplayHandlers::CreateGameState(
TSharedPtr<FJsonValue> FGameplayHandlers::CreateGameState(const TSharedPtr<FJsonObject>& Params)
{
	return CreateFrameworkBlueprint(Params, TEXT("/Script/Engine.GameStateBase"), TEXT("GameState"));
}
=====
TSharedPtr<FJsonValue> FGameplayHandlers::CreatePlayerController(
TSharedPtr<FJsonValue> FGameplayHandlers::CreatePlayerController(const TSharedPtr<FJsonObject>& Params)
{
	return CreateFrameworkBlueprint(Params, TEXT("/Script/Engine.PlayerController"), TEXT("PlayerController"));
}
=====
TSharedPtr<FJsonValue> FGameplayHandlers::CreatePlayerState(
TSharedPtr<FJsonValue> FGameplayHandlers::CreatePlayerState(const TSharedPtr<FJsonObject>& Params)
{
	return CreateFrameworkBlueprint(Params, TEXT("/Script/Engine.PlayerState"), TEXT("PlayerState"));
}
=====
TSharedPtr<FJsonValue> FGameplayHandlers::CreateHud(
TSharedPtr<FJsonValue> FGameplayHandlers::CreateHud(const TSharedPtr<FJsonObject>& Params)
{
	return CreateFrameworkBlueprint(Params, TEXT("/Script/Engine.HUD"), TEXT("HUD"));
}
