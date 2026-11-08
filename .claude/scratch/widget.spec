static TSharedPtr<FJsonValue> WidgetCreateTargetFromParams(
=====
TSharedPtr<FJsonValue> FWidgetHandlers::CreateWidgetBlueprint(
TSharedPtr<FJsonValue> FWidgetHandlers::CreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("WidgetBlueprint"), TEXT("/Game/UI/Widgets"), /*bOnConflict=*/true, /*bAssetPath=*/true });
	const FString ParentClassName = OptionalString(Params, TEXT("parentClass"), TEXT("UserWidget"));
	if (auto Err = Create.Read()) return Err;

	// (#134) Resolve parentClass string - accept short names ("UserWidget"),
	// short names with U prefix, and full class paths. Default to UUserWidget
	// only when the caller didn't pass a parentClass.
	UClass* ParentClass = MCPResolveClassOfType(ParentClassName, UUserWidget::StaticClass());
	if (!ParentClass)
	{
		UClass* Other = MCPResolveClass(ParentClassName);
		if (!Other) return MCPClassNotFoundError(ParentClassName, TEXT("parentClass"));
		ParentClass = Other;
	}
	if (!ParentClass->IsChildOf(UUserWidget::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("parentClass '%s' is not a UUserWidget subclass"), *ParentClassName));
	}

	UWidgetBlueprintFactory* WidgetFactory = NewObject<UWidgetBlueprintFactory>();
	WidgetFactory->ParentClass = ParentClass;
	UWidgetBlueprint* WidgetBP = nullptr;
	if (auto Early = Create.Make(WidgetBP, UWidgetBlueprint::StaticClass(), WidgetFactory)) return MCPCreatePreset::DescribeExisting(Early);

	// #728: a project can name a default root widget class, so the factory can
	// hand back a blueprint that already owns a widget. Give it its entry in
	// WidgetVariableNameToGuidMap before the asset reaches disk, rather than
	// leaving the first compile to report the missing one.
	const MCPWidgetGuidMap::FSyncReport GuidSync = MCPWidgetGuidMap::Sync(WidgetBP);

	TSharedPtr<FJsonObject> Result = Create.Finish();
	MCPCreatePreset::DescribeBlueprint(Result, WidgetBP);
	MCPSetWidgetGuidOutcome(Result, GuidSync, WidgetBP->GetPathName());
	return MCPResult(Result);
}
=====
TSharedPtr<FJsonValue> FWidgetHandlers::CreateEditorUtilityWidget(
TSharedPtr<FJsonValue> FWidgetHandlers::CreateEditorUtilityWidget(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("EditorUtilityWidgetBlueprint"), TEXT("/Game/EditorUtilities"), /*bOnConflict=*/true, /*bAssetPath=*/true });
	if (auto Err = Create.Read()) return Err;

	UClass* EUWBClass = FindObject<UClass>(nullptr, TEXT("/Script/Blutility.EditorUtilityWidgetBlueprint"));
	if (!EUWBClass)
	{
		return MCPError(TEXT("EditorUtilityWidgetBlueprint class not found. Enable Blutility plugin."));
	}

	// The editor spawns a utility widget's tab as an EditorUtilityWidget, so
	// a UserWidget parent makes an asset that run_utility_widget cannot open.
	UClass* EUWClass = FindObject<UClass>(nullptr, TEXT("/Script/Blutility.EditorUtilityWidget"));
	if (!EUWClass)
	{
		return MCPError(TEXT("EditorUtilityWidget class not found. Enable Blutility plugin."));
	}

	UWidgetBlueprintFactory* WidgetFactory = NewObject<UWidgetBlueprintFactory>();
	WidgetFactory->ParentClass = EUWClass;
	WidgetFactory->BlueprintType = BPTYPE_Normal;
	UObject* Created = nullptr;
	if (auto Early = Create.Make(Created, EUWBClass, WidgetFactory)) return MCPCreatePreset::DescribeExisting(Early);

	// #728: an editor utility widget is a WidgetBlueprint too, and a project
	// that names a default root widget class has the factory build one here.
	MCPWidgetGuidMap::FSyncReport GuidSync;
	if (UWidgetBlueprint* CreatedWidgetBP = Cast<UWidgetBlueprint>(Created))
	{
		GuidSync = MCPWidgetGuidMap::Sync(CreatedWidgetBP);
	}

	TSharedPtr<FJsonObject> Result = Create.Finish();
	MCPCreatePreset::DescribeBlueprint(Result, Cast<UBlueprint>(Created));
	MCPSetWidgetGuidOutcome(Result, GuidSync, Created->GetPathName());
	return MCPResult(Result);
}
=====
TSharedPtr<FJsonValue> FWidgetHandlers::CreateEditorUtilityBlueprint(
TSharedPtr<FJsonValue> FWidgetHandlers::CreateEditorUtilityBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FMCPCreate Create(Params, { TEXT("EditorUtilityBlueprint"), TEXT("/Game/EditorUtilities"), /*bOnConflict=*/true, /*bAssetPath=*/true });
	if (auto Err = Create.Read()) return Err;

	UClass* EUBClass = FindObject<UClass>(nullptr, TEXT("/Script/Blutility.EditorUtilityBlueprint"));
	if (!EUBClass)
	{
		return MCPError(TEXT("EditorUtilityBlueprint class not found. Enable Blutility plugin."));
	}

	UObject* Created = nullptr;
	if (auto Early = Create.Make(Created, EUBClass)) return MCPCreatePreset::DescribeExisting(Early);
	TSharedPtr<FJsonObject> Result = Create.Finish();
	MCPCreatePreset::DescribeBlueprint(Result, Cast<UBlueprint>(Created));
	return MCPResult(Result);
}
