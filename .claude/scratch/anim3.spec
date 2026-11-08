TSharedPtr<FJsonValue> FAnimationHandlers::CreatePoseSearchSchema(
TSharedPtr<FJsonValue> FAnimationHandlers::CreatePoseSearchSchema(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("PoseSearchSchema"), TEXT("/Game/MotionMatching") });
	if (auto Err = Create.Read()) return Err;
	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;
	// Every parameter is read before anything can fail (#1057).
	const FString MirrorPath = OptionalString(Params, TEXT("mirrorDataTablePath"));
	int32 SampleRate = 0;
	const bool bHasSampleRate = TryGetNumberParam(Params, TEXT("sampleRate"), SampleRate);
	const bool bAddDefaultChannels = OptionalBool(Params, TEXT("addDefaultChannels"), true);

	USkeleton* Skeleton = LoadAssetByPath<USkeleton>(SkeletonPath);
	if (!Skeleton) return MCPError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));

	UMirrorDataTable* MirrorTable = nullptr;
	if (!MirrorPath.IsEmpty())
	{
		MirrorTable = LoadAssetByPath<UMirrorDataTable>(MirrorPath);
		if (!MirrorTable) return MCPError(FString::Printf(TEXT("MirrorDataTable not found: %s"), *MirrorPath));
	}

	UPoseSearchSchema* Schema = nullptr;
	if (auto Early = Create.MakeNewObject(Schema)) return Early;
	Schema->AddSkeleton(Skeleton, MirrorTable);
	if (bHasSampleRate && SampleRate > 0)
	{
		Schema->SampleRate = SampleRate;
	}
	// Default channels (Trajectory + Pose on the root bone) give a schema you can
	// immediately build an index against; add_pose_search_schema_*_channel refines it.
	if (bAddDefaultChannels)
	{
		Schema->AddDefaultChannels();
	}
	MCPPoseSearch::Finalize(Schema);

	TSharedPtr<FJsonObject> Res = Create.Finish();
	Res->SetStringField(TEXT("skeletonPath"), Skeleton->GetPathName());
	Res->SetStringField(TEXT("mirrorDataTablePath"), MirrorTable ? MirrorTable->GetPathName() : FString());
	Res->SetNumberField(TEXT("sampleRate"), Schema->SampleRate);
	Res->SetNumberField(TEXT("channelCount"), Schema->GetChannels().Num());
	return MCPResult(Res);
}
=====
TSharedPtr<FJsonValue> FAnimationHandlers::CreateMirrorDataTable(
TSharedPtr<FJsonValue> FAnimationHandlers::CreateMirrorDataTable(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("MirrorDataTable"), TEXT("/Game/MotionMatching") });
	if (auto Err = Create.Read()) return Err;
	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;
	// Read before the load can fail (#1057).
	MCPReadParamsAhead(Params, { TEXT("expressions"), TEXT("mirrorAxis"), TEXT("mirrorRootMotion") });

	USkeleton* Skeleton = LoadAssetByPath<USkeleton>(SkeletonPath);
	if (!Skeleton) return MCPError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));

	// mirrorAxis was accepted and ignored, always producing an X-axis table.
	// A skeleton authored down Y mirrors to nonsense with no error anywhere.
	// Checked before the asset exists, so a bad axis creates nothing.
	const FString AxisStr = OptionalString(Params, TEXT("mirrorAxis"), TEXT("X")).ToUpper();
	EAxis::Type Axis = EAxis::X;
	if      (AxisStr == TEXT("X")) Axis = EAxis::X;
	else if (AxisStr == TEXT("Y")) Axis = EAxis::Y;
	else if (AxisStr == TEXT("Z")) Axis = EAxis::Z;
	else
	{
		return MCPError(FString::Printf(TEXT("Unknown mirrorAxis '%s'. Expected X, Y or Z."), *AxisStr));
	}

	UMirrorDataTable* Table = nullptr;
	if (auto Early = Create.MakeNewObject(Table)) return Early;
	Table->RowStruct = FMirrorTableRow::StaticStruct();
	Table->Skeleton = Skeleton;
	Table->MirrorAxis = Axis;
	Table->bMirrorRootMotion = OptionalBool(Params, TEXT("mirrorRootMotion"), true);

	// Build find/replace expressions. Default matches the UE mannequin (_l <-> _r suffix).
	auto MethodFromString = [](const FString& M) -> EMirrorFindReplaceMethod::Type
	{
		if (M.Equals(TEXT("prefix"), ESearchCase::IgnoreCase)) return EMirrorFindReplaceMethod::Prefix;
		if (M.Equals(TEXT("regex"), ESearchCase::IgnoreCase) || M.Equals(TEXT("regularexpression"), ESearchCase::IgnoreCase)) return EMirrorFindReplaceMethod::RegularExpression;
		return EMirrorFindReplaceMethod::Suffix;
	};

	Table->MirrorFindReplaceExpressions.Empty();
	const TArray<TSharedPtr<FJsonValue>>* Exprs = nullptr;
	if (TryGetArrayParam(Params, TEXT("expressions"), Exprs) && Exprs && Exprs->Num() > 0)
	{
		for (const TSharedPtr<FJsonValue>& V : *Exprs)
		{
			const TSharedPtr<FJsonObject>* E = nullptr;
			if (!V->TryGetObject(E) || !E || !(*E).IsValid()) continue;
			const FString Find = OptionalString(*E, TEXT("find"));
			const FString Replace = OptionalString(*E, TEXT("replace"));
			if (Find.IsEmpty() || Replace.IsEmpty()) continue;
			Table->MirrorFindReplaceExpressions.Add(FMirrorFindReplaceExpression(FName(*Find), FName(*Replace), MethodFromString(OptionalString(*E, TEXT("method"), TEXT("suffix")))));
		}
	}
	if (Table->MirrorFindReplaceExpressions.Num() == 0)
	{
		Table->MirrorFindReplaceExpressions.Add(FMirrorFindReplaceExpression(TEXT("_l"), TEXT("_r"), EMirrorFindReplaceMethod::Suffix));
		Table->MirrorFindReplaceExpressions.Add(FMirrorFindReplaceExpression(TEXT("_r"), TEXT("_l"), EMirrorFindReplaceMethod::Suffix));
	}

	// UMirrorDataTable::UpdateFromFindReplaceExpressions(FFindReplaceOptions) is
	// UE 5.8+. On 5.7 the expressions are stored on the table above but the
	// mirror rows are not auto-synced from them here; the asset editor's Sync
	// button (or manual row entry) fills them in.
#if UE_MCP_HAS_5_8_API
	Table->UpdateFromFindReplaceExpressions(UMirrorDataTable::FFindReplaceOptions::Sync());
#endif

	TSharedPtr<FJsonObject> Res = Create.Finish();
	Res->SetStringField(TEXT("skeletonPath"), Skeleton->GetPathName());
	Res->SetNumberField(TEXT("expressionCount"), Table->MirrorFindReplaceExpressions.Num());
	Res->SetNumberField(TEXT("rowCount"), Table->GetRowMap().Num());
	return MCPResult(Res);
}
=====
TSharedPtr<FJsonValue> FAnimationHandlers::CreatePoseSearchNormalizationSet(
TSharedPtr<FJsonValue> FAnimationHandlers::CreatePoseSearchNormalizationSet(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("PoseSearchNormalizationSet"), TEXT("/Game/MotionMatching") });
	if (auto Err = Create.Read()) return Err;

	// Every database is loaded before the set exists, so a bad path creates nothing.
	TArray<UPoseSearchDatabase*> Resolved;
	const TArray<TSharedPtr<FJsonValue>>* Databases = nullptr;
	if (TryGetArrayParam(Params, TEXT("databases"), Databases) && Databases)
	{
		for (const TSharedPtr<FJsonValue>& V : *Databases)
		{
			FString DbPath;
			if (!V->TryGetString(DbPath)) continue;
			UPoseSearchDatabase* Db = LoadAssetByPath<UPoseSearchDatabase>(DbPath);
			if (!Db) return MCPError(FString::Printf(TEXT("PoseSearchDatabase not found: %s"), *DbPath));
			Resolved.Add(Db);
		}
	}

	UPoseSearchNormalizationSet* NormSet = nullptr;
	if (auto Early = Create.MakeNewObject(NormSet)) return Early;
	TArray<TSharedPtr<FJsonValue>> AddedDatabases;
	for (UPoseSearchDatabase* Db : Resolved)
	{
		NormSet->Databases.Add(Db);
		AddedDatabases.Add(MakeShared<FJsonValueString>(Db->GetPathName()));
	}

	TSharedPtr<FJsonObject> Res = Create.Finish();
	Res->SetNumberField(TEXT("databaseCount"), AddedDatabases.Num());
	Res->SetArrayField(TEXT("databases"), AddedDatabases);
	return MCPResult(Res);
}
