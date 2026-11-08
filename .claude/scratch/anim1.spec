TSharedPtr<FJsonValue> FAnimationHandlers::CreateAnimBlueprint(
TSharedPtr<FJsonValue> FAnimationHandlers::CreateAnimBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("AnimBlueprint"), TEXT("/Game/Animations") });
	if (auto Err = Create.Read()) return Err;
	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;

	USkeleton* Skeleton = Cast<USkeleton>(MCPLoadAssetObject(SkeletonPath));
	if (!Skeleton)
	{
		return MCPError(FString::Printf(TEXT("Failed to load Skeleton at '%s'"), *SkeletonPath));
	}
	// A parentClass that does not resolve to an AnimInstance is refused, not ignored.
	TSharedPtr<FJsonValue> ParentErr;
	UClass* Parent = MCPCreatePreset::ResolveParent(Params, UAnimInstance::StaticClass(), TEXT("AnimBlueprint"), ParentErr);
	if (!Parent) return ParentErr;

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->TargetSkeleton = Skeleton;
	Factory->ParentClass = Parent;
	UAnimBlueprint* AnimBP = nullptr;
	if (auto Early = Create.Make(AnimBP, UAnimBlueprint::StaticClass(), Factory)) return MCPCreatePreset::DescribeExisting(Early);

	TSharedPtr<FJsonObject> Result = Create.Finish();
	MCPCreatePreset::DescribeBlueprint(Result, AnimBP);
	Result->SetStringField(TEXT("skeletonPath"), Skeleton->GetPathName());
	return MCPResult(Result);
}
=====
TSharedPtr<FJsonValue> FAnimationHandlers::CreateMontage(
TSharedPtr<FJsonValue> FAnimationHandlers::CreateMontage(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("AnimMontage"), TEXT("/Game/Animations") });
	if (auto Err = Create.Read()) return Err;
	FString AnimSequencePath;
	if (auto Err = RequireString(Params, TEXT("animSequencePath"), AnimSequencePath)) return Err;

	UAnimSequence* SourceSequence = LoadAssetByPath<UAnimSequence>(AnimSequencePath);
	if (!SourceSequence)
	{
		return MCPError(FString::Printf(TEXT("Failed to load AnimSequence at '%s'"), *AnimSequencePath));
	}

	UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
	Factory->TargetSkeleton = SourceSequence->GetSkeleton();
	Factory->SourceAnimation = SourceSequence;
	UAnimMontage* Montage = nullptr;
	if (auto Early = Create.Make(Montage, UAnimMontage::StaticClass(), Factory)) return Early;
	return MCPResult(Create.Finish());
}
=====
TSharedPtr<FJsonValue> FAnimationHandlers::CreateBlendspace(
TSharedPtr<FJsonValue> FAnimationHandlers::CreateBlendspace(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("BlendSpace"), TEXT("/Game/Animations") });
	if (auto Err = Create.Read()) return Err;
	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;

	const FString AxisHorizontal = OptionalString(Params, TEXT("axisHorizontal"), TEXT("Speed"));
	const FString AxisVertical = OptionalString(Params, TEXT("axisVertical"), TEXT("Direction"));
	const double HorizontalMin = OptionalNumber(Params, TEXT("horizontalMin"), 0.0);
	const double HorizontalMax = OptionalNumber(Params, TEXT("horizontalMax"), 500.0);
	const double VerticalMin = OptionalNumber(Params, TEXT("verticalMin"), -180.0);
	const double VerticalMax = OptionalNumber(Params, TEXT("verticalMax"), 180.0);

	USkeleton* Skeleton = Cast<USkeleton>(MCPLoadAssetObject(SkeletonPath));
	if (!Skeleton)
	{
		return MCPError(FString::Printf(TEXT("Failed to load Skeleton at '%s'"), *SkeletonPath));
	}

	UBlendSpaceFactoryNew* Factory = NewObject<UBlendSpaceFactoryNew>();
	Factory->TargetSkeleton = Skeleton;
	UBlendSpace* BlendSpace = nullptr;
	if (auto Early = Create.Make(BlendSpace, UBlendSpace::StaticClass(), Factory)) return Early;

	FBlendParameter& BlendParam0 = const_cast<FBlendParameter&>(BlendSpace->GetBlendParameter(0));
	BlendParam0.DisplayName = AxisHorizontal;
	BlendParam0.Min = HorizontalMin;
	BlendParam0.Max = HorizontalMax;
	FBlendParameter& BlendParam1 = const_cast<FBlendParameter&>(BlendSpace->GetBlendParameter(1));
	BlendParam1.DisplayName = AxisVertical;
	BlendParam1.Min = VerticalMin;
	BlendParam1.Max = VerticalMax;

	TSharedPtr<FJsonObject> Result = Create.Finish();
	Result->SetStringField(TEXT("axisHorizontal"), AxisHorizontal);
	Result->SetStringField(TEXT("axisVertical"), AxisVertical);
	return MCPResult(Result);
}
=====
TSharedPtr<FJsonValue> FAnimationHandlers::CreateBlendspace1D(
TSharedPtr<FJsonValue> FAnimationHandlers::CreateBlendspace1D(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("BlendSpace1D"), TEXT("/Game/Animations") });
	if (auto Err = Create.Read()) return Err;
	FString SkeletonPath;
	if (auto Err = RequireString(Params, TEXT("skeletonPath"), SkeletonPath)) return Err;

	const FString AxisName = OptionalString(Params, TEXT("axisName"), TEXT("Speed"));
	const double AxisMin = OptionalNumber(Params, TEXT("axisMin"), 0.0);
	const double AxisMax = OptionalNumber(Params, TEXT("axisMax"), 500.0);
	const int32 GridNum = (int32)OptionalNumber(Params, TEXT("gridNum"), 4.0);

	USkeleton* Skeleton = Cast<USkeleton>(MCPLoadAssetObject(SkeletonPath));
	if (!Skeleton) return MCPError(FString::Printf(TEXT("Failed to load Skeleton at '%s'"), *SkeletonPath));

	UBlendSpaceFactory1D* Factory = NewObject<UBlendSpaceFactory1D>();
	Factory->TargetSkeleton = Skeleton;
	UBlendSpace1D* BS = nullptr;
	if (auto Early = Create.Make(BS, UBlendSpace1D::StaticClass(), Factory)) return Early;

	FBlendParameter& BlendParam0 = const_cast<FBlendParameter&>(BS->GetBlendParameter(0));
	BlendParam0.DisplayName = AxisName;
	BlendParam0.Min = AxisMin;
	BlendParam0.Max = AxisMax;
	BlendParam0.GridNum = GridNum;

	TSharedPtr<FJsonObject> Result = Create.Finish();
	Result->SetStringField(TEXT("axisName"), AxisName);
	return MCPResult(Result);
}
