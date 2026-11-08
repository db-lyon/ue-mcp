TSharedPtr<FJsonValue> FAnimationHandlers::CreateSequence(
TSharedPtr<FJsonValue> FAnimationHandlers::CreateSequence(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("AnimSequence"), TEXT("/Game/Animations") });
	if (auto Err = Create.Read()) return Err;
	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;
	const double FrameRate = OptionalNumber(Params, TEXT("frameRate"), 30.0);
	const double NumFrames = OptionalNumber(Params, TEXT("numFrames"), 30.0);

	UObject* SkeletonAsset = MCPLoadAssetObject(SkeletonPath);
	USkeleton* Skeleton = Cast<USkeleton>(SkeletonAsset);
	if (!Skeleton)
	{
		if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(SkeletonAsset))
		{
			Skeleton = SkelMesh->GetSkeleton();
		}
	}
	if (!Skeleton)
	{
		return MCPError(FString::Printf(TEXT("Failed to load Skeleton at '%s'"), *SkeletonPath));
	}

	UAnimSequence* NewSeq = nullptr;
	if (auto Early = Create.MakeNewObject(NewSeq)) return Early;
	NewSeq->SetSkeleton(Skeleton);

	// Frame count and duration go through the data controller, whose model is initialised first.
	IAnimationDataController& Controller = NewSeq->GetController();
	Controller.InitializeModel();
	Controller.OpenBracket(NSLOCTEXT("MCP", "CreateSequence", "MCP Create Sequence"));
	Controller.SetFrameRate(FFrameRate(static_cast<int32>(FrameRate), 1));
	Controller.SetNumberOfFrames(static_cast<int32>(NumFrames));
	Controller.NotifyPopulated();
	Controller.CloseBracket(false);

	// Clear any lingering transactions to prevent "transaction still pending" crashes
	// when users later interact with the asset in the editor (e.g. bake to control rig)
	GEditor->ResetTransaction(NSLOCTEXT("MCP", "CreateSequenceReset", "MCP Create Sequence Complete"));

	NewSeq->PostEditChange();
	NewSeq->MarkPackageDirty();

	TSharedPtr<FJsonObject> Result = Create.Finish();
	Result->SetStringField(TEXT("skeletonPath"), Skeleton->GetPathName());
	Result->SetNumberField(TEXT("numFrames"), NumFrames);
	Result->SetNumberField(TEXT("frameRate"), FrameRate);
	Result->SetNumberField(TEXT("sequenceLength"), NewSeq->GetPlayLength());
	return MCPResult(Result);
}
=====
TSharedPtr<FJsonValue> FAnimationHandlers::CreateAnimComposite(
TSharedPtr<FJsonValue> FAnimationHandlers::CreateAnimComposite(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("AnimComposite"), TEXT("/Game/Animations") });
	if (auto Err = Create.Read()) return Err;
	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;

	USkeleton* Skeleton = LoadAssetByPath<USkeleton>(SkeletonPath);
	if (!Skeleton) return MCPError(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));

	UAnimComposite* Composite = nullptr;
	if (auto Early = Create.MakeNewObject(Composite)) return Early;
	Composite->SetSkeleton(Skeleton);

	TSharedPtr<FJsonObject> Result = Create.Finish();
	Result->SetStringField(TEXT("skeletonPath"), Skeleton->GetPathName());
	return MCPResult(Result);
}
