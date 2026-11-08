TSharedPtr<FJsonValue> FAssetHandlers::CreateDataAsset(
TSharedPtr<FJsonValue> FAssetHandlers::CreateDataAsset(const TSharedPtr<FJsonObject>& Params)
{
	return CreateAssetOfClass(Params, UDataAsset::StaticClass());
}
=====
TSharedPtr<FJsonValue> FAssetHandlers::CreateAssetByClass(
TSharedPtr<FJsonValue> FAssetHandlers::CreateAssetByClass(const TSharedPtr<FJsonObject>& Params)
{
	return CreateAssetOfClass(Params, nullptr);
}

// create_asset_by_class and its create_data_asset preset: resolve className
// (within Base when given), create it with a null factory (IAssetTools
// NewObjects the exact class), apply the optional properties and save (#726).
TSharedPtr<FJsonValue> FAssetHandlers::CreateAssetOfClass(const TSharedPtr<FJsonObject>& Params, UClass* Base)
{
	FMCPCreate Create(Params, { Base ? TEXT("DataAsset") : TEXT("Asset"), TEXT("/Game") });
	if (auto Err = Create.Read()) return Err;
	FString ClassName;
	if (auto Err = RequireString(Params, TEXT("className"), ClassName)) return Err;
	// Every parameter is read before anything can fail (#1057).
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	const bool bHasProps = TryGetObjectParam(Params, TEXT("properties"), PropsObj);

	// #823: shared resolution, so the prefixed C++ spelling ("UMyConfig",
	// "/Script/MyGame.UMyConfig") resolves to the class UE registered as
	// "MyConfig" instead of reporting the class missing.
	UClass* AssetClass = MCPResolveClass(ClassName);
	if (Base)
	{
		if (auto Err = MCPCheckClassUsable(ClassName, AssetClass, Base)) return Err;
	}
	else
	{
		if (auto Err = MCPCheckClassUsable(ClassName, AssetClass)) return Err;
		// Classes that cannot be standalone assets need a specialized flow.
		if (AssetClass->IsChildOf(AActor::StaticClass()) || AssetClass->IsChildOf(UActorComponent::StaticClass()))
		{
			return MCPClassUnusableError(ClassName, AssetClass, TEXT("not_an_asset"),
				TEXT("it is an actor or component class, not an asset. Use level(place_actor) or level(add_component_to_actor)."));
		}
	}

	UObject* NewAsset = nullptr;
	if (auto Early = Create.Make(NewAsset, AssetClass)) return Early;

	// The recursive JSON-to-property setter, so TArray<FStruct> with nested
	// UObject refs, FGameplayTag and the like all work (#196, #199).
	int32 SetCount = 0;
	TArray<FString> PropErrors;
	if (bHasProps && PropsObj && (*PropsObj).IsValid())
	{
		for (const auto& Pair : (*PropsObj)->Values)
		{
			FProperty* Prop = AssetClass->FindPropertyByName(FName(*Pair.Key));
			if (!Prop)
			{
				PropErrors.Add(FString::Printf(TEXT("Property not found: %s"), *Pair.Key));
				continue;
			}
			FString SetErr;
			if (MCPJsonProperty::SetJsonOnProperty(Prop, Prop->ContainerPtrToValuePtr<void>(NewAsset), Pair.Value, SetErr))
			{
				SetCount++;
			}
			else
			{
				PropErrors.Add(FString::Printf(TEXT("Failed to set %s: %s"), *Pair.Key, *SetErr));
			}
		}
	}

	TSharedPtr<FJsonObject> Result = Create.Finish();
	Result->SetNumberField(TEXT("propertiesSet"), SetCount);
	if (PropErrors.Num() > 0) Result->SetArrayField(TEXT("propertyErrors"), MCPStringListToJson(PropErrors));
	return MCPResult(Result);
}
=====
TSharedPtr<FJsonValue> FAssetHandlers::CreateInterchangePipeline(
TSharedPtr<FJsonValue> FAssetHandlers::CreateInterchangePipeline(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("InterchangePipeline"), TEXT("/Game/Import"), /*bOnConflict=*/true, /*bAssetPath=*/true });
	if (auto Err = Create.Read()) return Err;
	const FString MeshType = OptionalString(Params, TEXT("meshType"), TEXT("skeletal")).ToLower();

	UClass* PipelineClass = FindObject<UClass>(nullptr, TEXT("/Script/InterchangePipelines.InterchangeGenericAssetsPipeline"));
	if (!PipelineClass)
	{
		return MCPError(TEXT("InterchangeGenericAssetsPipeline class not found. Enable the Interchange Editor plugin."));
	}

	UObject* NewAsset = nullptr;
	if (auto Early = Create.Make(NewAsset, PipelineClass)) return Early;

	// Default mesh-pipeline settings. We write through SetJsonOnProperty so
	// every field stays in sync with the asset's UPROPERTY layout.
	auto SetSubProp = [&](const FString& SubPath, const TCHAR* Field, const TSharedPtr<FJsonValue>& Val) -> bool
	{
		TArray<FString> Parts;
		SubPath.ParseIntoArray(Parts, TEXT("."));
		UStruct* Cur = NewAsset->GetClass();
		void* Container = NewAsset;
		for (const FString& Part : Parts)
		{
			FProperty* P = Cur->FindPropertyByName(FName(*Part));
			if (!P) return false;
			if (FObjectProperty* OP = CastField<FObjectProperty>(P))
			{
				UObject* Sub = OP->GetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(Container));
				if (!Sub) return false;
				Sub->Modify();
				Container = Sub;
				Cur = Sub->GetClass();
			}
			else if (FStructProperty* SP = CastField<FStructProperty>(P))
			{
				Container = SP->ContainerPtrToValuePtr<void>(Container);
				Cur = SP->Struct;
			}
			else { return false; }
		}
		FProperty* Leaf = Cur->FindPropertyByName(FName(Field));
		if (!Leaf) return false;
		FString E;
		return MCPJsonProperty::SetJsonOnProperty(Leaf, Leaf->ContainerPtrToValuePtr<void>(Container), Val, E);
	};

	auto JBool = [](bool B) { return MakeShared<FJsonValueBoolean>(B); };
	auto JStr  = [](const TCHAR* S) { return MakeShared<FJsonValueString>(S); };

	// Common mesh pipeline defaults.
	const bool bSkeletal = (MeshType == TEXT("skeletal"));
	SetSubProp(TEXT("CommonMeshesProperties"), TEXT("bRecomputeNormals"), JBool(false));
	SetSubProp(TEXT("CommonMeshesProperties"), TEXT("bRecomputeTangents"), JBool(false));
	SetSubProp(TEXT("CommonMeshesProperties"), TEXT("bUseMikkTSpace"), JBool(true));
	SetSubProp(TEXT("CommonMeshesProperties"), TEXT("bUseHighPrecisionTangentBasis"), JBool(true));
	SetSubProp(TEXT("CommonMeshesProperties"), TEXT("bRemoveDegenerates"), JBool(true));
	SetSubProp(TEXT("CommonMeshesProperties"), TEXT("ForceAllMeshAsType"),
		JStr(bSkeletal ? TEXT("SkeletalMesh") : TEXT("StaticMesh")));
	SetSubProp(TEXT("MeshPipeline"), TEXT("bBuildNanite"), JBool(false));
	SetSubProp(TEXT("MeshPipeline"), TEXT("bImportSkeletalMeshes"), JBool(bSkeletal));
	SetSubProp(TEXT("MeshPipeline"), TEXT("bImportStaticMeshes"), JBool(!bSkeletal));
	SetSubProp(TEXT("MeshPipeline"), TEXT("bCreatePhysicsAsset"), JBool(false));

	// Caller-supplied overrides: dotted keys such as "MeshPipeline.bImportSkeletalMeshes".
	const TSharedPtr<FJsonObject>* OptionsObj = nullptr;
	int32 OverridesApplied = 0;
	TArray<FString> OverrideFailures;
	if (TryGetObjectParam(Params, TEXT("options"), OptionsObj) && OptionsObj && (*OptionsObj).IsValid())
	{
		for (const auto& Pair : (*OptionsObj)->Values)
		{
			const FString Key(*Pair.Key);
			int32 Dot = INDEX_NONE;
			Key.FindLastChar('.', Dot);
			const bool bSet = Dot == INDEX_NONE
				? SetSubProp(TEXT(""), *Key, Pair.Value)
				: SetSubProp(Key.Left(Dot), *Key.RightChop(Dot + 1), Pair.Value);
			if (bSet) ++OverridesApplied;
			else OverrideFailures.Add(Key);
		}
	}
	NewAsset->PostEditChange();

	TSharedPtr<FJsonObject> Result = Create.Finish();
	Result->SetStringField(TEXT("meshType"), MeshType);
	Result->SetNumberField(TEXT("overridesApplied"), OverridesApplied);
	if (OverrideFailures.Num() > 0) Result->SetArrayField(TEXT("overrideFailures"), MCPStringListToJson(OverrideFailures));
	return MCPResult(Result);
}
