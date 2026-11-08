TSharedPtr<FJsonValue> FAnimationHandlers::CreateIKRig(
TSharedPtr<FJsonValue> FAnimationHandlers::CreateIKRig(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("IKRigDefinition"), TEXT("/Game") });
	if (auto Err = Create.Read()) return Err;
	FString SkeletalMeshPath;
	if (auto Err = RequireString(Params, TEXT("skeletalMeshPath"), SkeletalMeshPath)) return Err;
	// Read before the load can fail (#1057).
	FString RetargetRoot;
	TryGetStringParam(Params, TEXT("retargetRoot"), RetargetRoot);
	const TArray<TSharedPtr<FJsonValue>>* ChainsArr = nullptr;
	TryGetArrayParam(Params, TEXT("chains"), ChainsArr);

	USkeletalMesh* SkelMesh = LoadAssetByPath<USkeletalMesh>(SkeletalMeshPath);
	if (!SkelMesh)
	{
		return MCPError(FString::Printf(TEXT("Failed to load SkeletalMesh at '%s'"), *SkeletalMeshPath));
	}

	UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FindObject<UClass>(nullptr, TEXT("/Script/IKRigEditor.IKRigDefinitionFactory")));
	if (!Factory)
	{
		return MCPError(TEXT("IKRigDefinitionFactory not found - is the IKRig plugin enabled?"));
	}

	UIKRigDefinition* IKRig = nullptr;
	if (auto Early = Create.Make(IKRig, UIKRigDefinition::StaticClass(), Factory)) return Early;

	// Prefer IKRigController to atomically configure the rig (#97, #103)
	int32 ChainsAdded = 0;
	TArray<FString> ChainErrors;
	if (UIKRigController* Controller = UIKRigController::GetController(IKRig))
	{
		Controller->SetSkeletalMesh(SkelMesh);
		if (!RetargetRoot.IsEmpty())
		{
			Controller->SetRetargetRoot(FName(*RetargetRoot));
		}
		if (ChainsArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *ChainsArr)
			{
				if (!V.IsValid() || V->Type != EJson::Object) continue;
				auto O = V->AsObject();
				FString CName = O->GetStringField(TEXT("name"));
				FString SBone = O->GetStringField(TEXT("startBone"));
				FString EBone = O->GetStringField(TEXT("endBone"));
				FString Goal;
				O->TryGetStringField(TEXT("goal"), Goal);
				if (CName.IsEmpty() || SBone.IsEmpty() || EBone.IsEmpty())
				{
					ChainErrors.Add(FString::Printf(TEXT("Chain missing name/startBone/endBone: %s"), *CName));
					continue;
				}
				const FName AddedName = Controller->AddRetargetChain(
					FName(*CName), FName(*SBone), FName(*EBone), Goal.IsEmpty() ? NAME_None : FName(*Goal));
				if (AddedName != NAME_None) ChainsAdded++;
				else ChainErrors.Add(FString::Printf(TEXT("Failed to add chain: %s"), *CName));
			}
		}
	}
	else
	{
		// Fallback if controller not available: basic preview mesh only
		IKRig->SetPreviewMesh(SkelMesh, false);
	}
	IKRig->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = Create.Finish();
	Result->SetStringField(TEXT("skeletalMeshPath"), SkelMesh->GetPathName());
	if (!RetargetRoot.IsEmpty()) Result->SetStringField(TEXT("retargetRoot"), RetargetRoot);
	Result->SetNumberField(TEXT("chainsAdded"), ChainsAdded);
	if (ChainErrors.Num() > 0)
	{
		Result->SetArrayField(TEXT("chainErrors"), MCPStringListToJson(ChainErrors));
	}
	return MCPResult(Result);
}
=====
TSharedPtr<FJsonValue> FAnimationHandlers::CreatePoseSearchDatabase(
TSharedPtr<FJsonValue> FAnimationHandlers::CreatePoseSearchDatabase(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("PoseSearchDatabase"), TEXT("/Game/MotionMatching") });
	if (auto Err = Create.Read()) return Err;
	const FString Name = Create.Name();
	const FString PackagePath = Create.PackagePath();
	const FString SchemaPath = OptionalString(Params, TEXT("schemaPath"), TEXT(""));
	// #833: a database indexes THROUGH its schema, so a database whose schema
	// is missing or unsamplable fails BuildIndex and the editor reports the
	// database as the invalid asset. Naming a skeleton is enough to author the
	// schema here, so the one call produces something that can actually index.
	const FString SkeletonPath = OptionalString(Params, TEXT("skeletonPath"), TEXT(""));

	// Resolve (or author) the schema BEFORE the database exists, so a bad
	// schema argument leaves no half-built database behind.
	UPoseSearchSchema* Schema = nullptr;
	FString ResolvedSchemaPath = SchemaPath;
	bool bSchemaCreated = false;
	if (!SchemaPath.IsEmpty())
	{
		Schema = Cast<UPoseSearchSchema>(MCPLoadAssetObject(SchemaPath));
		if (!Schema) return MCPError(FString::Printf(TEXT("Schema not found: %s"), *SchemaPath));
	}
	else if (!SkeletonPath.IsEmpty())
	{
		ResolvedSchemaPath = PackagePath + TEXT("/") + Name + TEXT("_Schema");
		Schema = Cast<UPoseSearchSchema>(MCPLoadAssetObject(ResolvedSchemaPath));
		if (!Schema)
		{
			// Same authoring routine animation(create_pose_search_schema) runs,
			// called rather than reimplemented, so the schema this makes is the
			// schema that action makes.
			TSharedPtr<FJsonObject> SchemaParams = MakeShared<FJsonObject>();
			SchemaParams->SetStringField(TEXT("name"), Name + TEXT("_Schema"));
			SchemaParams->SetStringField(TEXT("packagePath"), PackagePath);
			SchemaParams->SetStringField(TEXT("skeletonPath"), SkeletonPath);
			SchemaParams->SetStringField(TEXT("onConflict"), TEXT("skip"));
			TSharedPtr<FJsonValue> SchemaResult = CreatePoseSearchSchema(SchemaParams);
			Schema = Cast<UPoseSearchSchema>(MCPLoadAssetObject(ResolvedSchemaPath));
			if (!Schema) return SchemaResult;
			bSchemaCreated = true;
		}
	}

	if (Schema)
	{
		const FString Problems = MCPPoseSearch::DescribeProblems(Schema);
		if (!Problems.IsEmpty())
		{
			return MCPError(FString::Printf(
				TEXT("Schema '%s' cannot index because %s. A database pointed at it fails BuildIndex and the editor ")
				TEXT("reports the database as invalid, so no database was created."),
				*Schema->GetPathName(), *Problems));
		}
	}

	UPoseSearchDatabase* Database = nullptr;
	if (auto Early = Create.MakeNewObject(Database)) return Early;
	if (Schema)
	{
		Database->Schema = Schema;
	}

	TSharedPtr<FJsonObject> Res = Create.Finish();
	Res->SetStringField(TEXT("schemaPath"), Schema ? Schema->GetPathName() : ResolvedSchemaPath);
	Res->SetBoolField(TEXT("schemaCreated"), bSchemaCreated);
	Res->SetBoolField(TEXT("indexable"), Schema != nullptr);
	if (!Schema)
	{
		Res->SetStringField(TEXT("note"),
			TEXT("This database has no schema, so it cannot be indexed and animation(add_pose_search_sequence) will ")
			TEXT("refuse to add clips to it. Give it one with animation(set_pose_search_schema), or pass skeletonPath ")
			TEXT("to this action and it authors the schema alongside the database."));
	}
	return MCPResult(Res);
}
