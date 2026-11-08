TSharedPtr<FJsonValue> FFoliageHandlers::CreateFoliageType(
TSharedPtr<FJsonValue> FFoliageHandlers::CreateFoliageType(const TSharedPtr<FJsonObject>& Params)
{
	FString MeshPath;
	if (auto Err = RequireString(Params, TEXT("meshPath"), MeshPath)) return Err;

	// Every parameter is read before anything can fail (#1057).
	const FString RequestedName = OptionalString(Params, TEXT("name"));
	const FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/Foliage"));
	const TSharedPtr<FJsonObject>* SettingsObj = nullptr;
	const bool bHasSettings = TryGetObjectParam(Params, TEXT("settings"), SettingsObj) && SettingsObj && (*SettingsObj).IsValid();
	FMCPCreate Create(Params, { TEXT("FoliageType"), TEXT("/Game/Foliage") });

	UStaticMesh* Mesh = LoadAssetByPath<UStaticMesh>(MeshPath);
	if (!Mesh) return MCPAssetLoadError(MeshPath, TEXT("StaticMesh"));

	// The name defaults to FT_<mesh>, so it is known only once the mesh is.
	Create.Target(RequestedName.IsEmpty() ? FString::Printf(TEXT("FT_%s"), *Mesh->GetName()) : RequestedName, PackagePath);
	UFoliageType_InstancedStaticMesh* FoliageType = nullptr;
	if (auto Early = Create.MakeNewObject(FoliageType)) return Early;
	FoliageType->Mesh = Mesh;

	// Optional settings, as export text per property.
	if (bHasSettings)
	{
		for (const auto& KV : (*SettingsObj)->Values)
		{
			FString PropertyValue;
			if (KV.Value->Type == EJson::String) PropertyValue = KV.Value->AsString();
			else if (KV.Value->Type == EJson::Number) PropertyValue = FString::SanitizeFloat(KV.Value->AsNumber());
			else if (KV.Value->Type == EJson::Boolean) PropertyValue = KV.Value->AsBool() ? TEXT("True") : TEXT("False");

			if (FProperty* Property = FoliageType->GetClass()->FindPropertyByName(FName(*KV.Key)))
			{
				Property->ImportText_Direct(*PropertyValue, Property->ContainerPtrToValuePtr<void>(FoliageType), FoliageType, PPF_None);
			}
		}
	}

	TSharedPtr<FJsonObject> Result = Create.Finish();
	Result->SetStringField(TEXT("meshPath"), Mesh->GetPathName());
	Result->SetStringField(TEXT("meshName"), Mesh->GetName());
	return MCPResult(Result);
}
