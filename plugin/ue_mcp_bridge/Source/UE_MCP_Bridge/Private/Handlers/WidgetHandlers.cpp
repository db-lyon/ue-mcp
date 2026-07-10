#include "WidgetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "WidgetBlueprint.h"
#include "WidgetBlueprintFactory.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Components/Overlay.h"
#include "Components/GridPanel.h"
#include "Components/UniformGridPanel.h"
#include "Components/WidgetSwitcher.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/Border.h"
#include "Components/Spacer.h"
#include "Components/RichTextBlock.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"
#include "Editor.h"
#include "EditorUtilitySubsystem.h"
#include "EditorUtilityWidget.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "EditorUtilityBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"

void FWidgetHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_widget_blueprints"), &ListWidgetBlueprints);
	Registry.RegisterHandler(TEXT("create_widget_blueprint"), &CreateWidgetBlueprint);
	Registry.RegisterHandler(TEXT("read_widget_tree"), &ReadWidgetTree);
	Registry.RegisterHandler(TEXT("create_editor_utility_widget"), &CreateEditorUtilityWidget);
	Registry.RegisterHandler(TEXT("create_editor_utility_blueprint"), &CreateEditorUtilityBlueprint);
	Registry.RegisterHandler(TEXT("get_widget_details"), &GetWidgetProperties);
	Registry.RegisterHandler(TEXT("get_widget_properties"), &GetWidgetFullProperties);
	Registry.RegisterHandler(TEXT("list_widget_bindings"), &ListWidgetBindings);
	Registry.RegisterHandler(TEXT("clear_widget_binding"), &ClearWidgetBinding);
	Registry.RegisterHandler(TEXT("set_widget_property"), &SetWidgetProperty);
	Registry.RegisterHandler(TEXT("set_widget_style"), &SetWidgetStyle);
	Registry.RegisterHandler(TEXT("bulk_set_widget_properties"), &BulkSetWidgetProperties);
	Registry.RegisterHandler(TEXT("read_widget_animations"), &ReadWidgetAnimations);
	Registry.RegisterHandler(TEXT("run_editor_utility_widget"), &RunEditorUtilityWidget);
	Registry.RegisterHandler(TEXT("run_editor_utility_blueprint"), &RunEditorUtilityBlueprint);
	Registry.RegisterHandler(TEXT("add_widget"), &AddWidget);
	Registry.RegisterHandler(TEXT("remove_widget"), &RemoveWidget);
	Registry.RegisterHandler(TEXT("move_widget"), &MoveWidget);
	Registry.RegisterHandler(TEXT("set_root_widget"), &SetRoot);
	Registry.RegisterHandler(TEXT("wrap_root_widget"), &WrapRoot);
	Registry.RegisterHandler(TEXT("list_widget_classes"), &ListWidgetClasses);
	Registry.RegisterHandler(TEXT("list_runtime_widgets"), &ListRuntimeWidgets);
	Registry.RegisterHandler(TEXT("get_runtime_widget"), &GetRuntimeWidget);
	// #161: Runtime delegate inspection
	Registry.RegisterHandler(TEXT("get_runtime_delegates"), &GetRuntimeDelegates);
	Registry.RegisterHandler(TEXT("add_to_viewport"), &AddWidgetToViewport);
	Registry.RegisterHandler(TEXT("invoke_runtime_function"), &InvokeRuntimeWidgetFunction);
}

UWidget* FWidgetHandlers::FindWidgetByNameRecursive(UWidget* Root, const FString& WidgetName)
{
	if (!Root) return nullptr;

	if (Root->GetName() == WidgetName)
	{
		return Root;
	}

	UPanelWidget* PanelWidget = Cast<UPanelWidget>(Root);
	if (PanelWidget)
	{
		for (int32 i = 0; i < PanelWidget->GetChildrenCount(); ++i)
		{
			UWidget* Child = PanelWidget->GetChildAt(i);
			UWidget* Found = FindWidgetByNameRecursive(Child, WidgetName);
			if (Found)
			{
				return Found;
			}
		}
	}

	return nullptr;
}

TSharedPtr<FJsonValue> FWidgetHandlers::ListWidgetBlueprints(const TSharedPtr<FJsonObject>& Params)
{
	bool bRecursive = OptionalBool(Params, TEXT("recursive"), true);

	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/UMGEditor"), TEXT("WidgetBlueprint")), AssetDataList, bRecursive);

	TArray<TSharedPtr<FJsonValue>> AssetsArray;
	for (const FAssetData& AssetData : AssetDataList)
	{
		TSharedPtr<FJsonObject> AssetObj = MakeShared<FJsonObject>();
		AssetObj->SetStringField(TEXT("name"), AssetData.AssetName.ToString());
		AssetObj->SetStringField(TEXT("path"), AssetData.GetObjectPathString());
		AssetObj->SetStringField(TEXT("packagePath"), AssetData.PackagePath.ToString());
		AssetsArray.Add(MakeShared<FJsonValueObject>(AssetObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("assets"), AssetsArray);
	Result->SetNumberField(TEXT("count"), AssetsArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::CreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Err = RequireString(Params, TEXT("name"), Name)) return Err;

	FString PackagePath = OptionalString(Params, TEXT("packagePath"), TEXT("/Game/UI/Widgets"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	FString ParentClassName = OptionalString(Params, TEXT("parentClass"), TEXT("UserWidget"));

	// (#134) Resolve parentClass string - accept short names ("UserWidget"),
	// short names with U prefix, and full class paths. Default to UUserWidget
	// only when the caller didn't pass a parentClass.
	UClass* ParentClass = nullptr;
	ParentClass = FindClassByShortName(ParentClassName);
	if (!ParentClass)
	{
		ParentClass = LoadObject<UClass>(nullptr, *ParentClassName);
	}
	if (!ParentClass)
	{
		ParentClass = UUserWidget::StaticClass();
	}
	if (!ParentClass->IsChildOf(UUserWidget::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("parentClass '%s' is not a UUserWidget subclass"), *ParentClassName));
	}

	UWidgetBlueprintFactory* WidgetFactory = NewObject<UWidgetBlueprintFactory>();
	WidgetFactory->ParentClass = ParentClass;

	auto Created = MCPCreateAssetIdempotent<UWidgetBlueprint>(Name, PackagePath, OnConflict, TEXT("WidgetBlueprint"), WidgetFactory);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UEditorAssetLibrary::SaveAsset(Created.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetStringField(TEXT("parentClass"), ParentClass->GetPathName());
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::ReadWidgetTree(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP)
	{
		return MCPError(FString::Printf(TEXT("Failed to load WidgetBlueprint at '%s'"), *AssetPath));
	}

	auto Result = MCPSuccess();

	// Recursive lambda to build widget hierarchy
	TFunction<TSharedPtr<FJsonObject>(UWidget*)> BuildWidgetJson = [&](UWidget* Widget) -> TSharedPtr<FJsonObject>
	{
		if (!Widget) return nullptr;

		TSharedPtr<FJsonObject> WidgetObj = MakeShared<FJsonObject>();
		WidgetObj->SetStringField(TEXT("name"), Widget->GetName());
		WidgetObj->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
		WidgetObj->SetBoolField(TEXT("isVisible"), Widget->IsVisible());

		// If it's a panel widget, recurse into children
		UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget);
		if (PanelWidget)
		{
			TArray<TSharedPtr<FJsonValue>> ChildrenArray;
			for (int32 i = 0; i < PanelWidget->GetChildrenCount(); ++i)
			{
				UWidget* Child = PanelWidget->GetChildAt(i);
				TSharedPtr<FJsonObject> ChildObj = BuildWidgetJson(Child);
				if (ChildObj.IsValid())
				{
					ChildrenArray.Add(MakeShared<FJsonValueObject>(ChildObj));
				}
			}
			WidgetObj->SetArrayField(TEXT("children"), ChildrenArray);
		}

		return WidgetObj;
	};

	// Get the root widget from the WidgetTree
	UWidget* RootWidget = WidgetBP->WidgetTree ? WidgetBP->WidgetTree->RootWidget : nullptr;
	if (RootWidget)
	{
		TSharedPtr<FJsonObject> TreeObj = BuildWidgetJson(RootWidget);
		Result->SetObjectField(TEXT("widgetTree"), TreeObj);
	}
	else
	{
		Result->SetStringField(TEXT("widgetTree"), TEXT("empty"));
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::CreateEditorUtilityWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString Path;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), Path)) return Err;

	FString PackagePath;
	FString AssetName;
	Path.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (AssetName.IsEmpty())
	{
		return MCPError(TEXT("Invalid path format. Expected '/Game/.../AssetName'"));
	}

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UClass* EUWBClass = FindObject<UClass>(nullptr, TEXT("/Script/Blutility.EditorUtilityWidgetBlueprint"));
	if (!EUWBClass)
	{
		return MCPError(TEXT("EditorUtilityWidgetBlueprint class not found. Enable Blutility plugin."));
	}

	UWidgetBlueprintFactory* WidgetFactory = NewObject<UWidgetBlueprintFactory>();
	WidgetFactory->ParentClass = UUserWidget::StaticClass();
	WidgetFactory->BlueprintType = BPTYPE_Normal;

	auto Created = MCPCreateAssetIdempotent<UObject>(AssetName, PackagePath, OnConflict, TEXT("EditorUtilityWidgetBlueprint"), EUWBClass, WidgetFactory);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UEditorAssetLibrary::SaveAsset(Created.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), AssetName);
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::CreateEditorUtilityBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString Path;
	if (auto Err = RequireStringAlt(Params, TEXT("path"), TEXT("assetPath"), Path)) return Err;

	FString PackagePath;
	FString AssetName;
	Path.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (AssetName.IsEmpty())
	{
		return MCPError(TEXT("Invalid path format. Expected '/Game/.../AssetName'"));
	}

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));

	UClass* EUBClass = FindObject<UClass>(nullptr, TEXT("/Script/Blutility.EditorUtilityBlueprint"));
	if (!EUBClass)
	{
		return MCPError(TEXT("EditorUtilityBlueprint class not found. Enable Blutility plugin."));
	}

	auto Created = MCPCreateAssetIdempotent<UObject>(AssetName, PackagePath, OnConflict, TEXT("EditorUtilityBlueprint"), EUBClass, nullptr);
	if (Created.EarlyReturn) return Created.EarlyReturn;

	UEditorAssetLibrary::SaveAsset(Created.Asset->GetPathName());

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("path"), Created.Asset->GetPathName());
	Result->SetStringField(TEXT("name"), AssetName);
	MCPSetDeleteAssetRollback(Result, Created.Asset->GetPathName());

	return MCPResult(Result);
}
TSharedPtr<FJsonValue> FWidgetHandlers::RunEditorUtilityWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UEditorUtilityWidgetBlueprint* EUWidget = Cast<UEditorUtilityWidgetBlueprint>(LoadedAsset);
	if (!EUWidget)
	{
		return MCPError(FString::Printf(TEXT("Failed to load EditorUtilityWidgetBlueprint at '%s'"), *AssetPath));
	}

	UEditorUtilitySubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
	if (!Subsystem)
	{
		return MCPError(TEXT("EditorUtilitySubsystem not available"));
	}

	// No rollback: destructive/external — opens a dockable tab in the editor.
	Subsystem->SpawnAndRegisterTab(EUWidget);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), EUWidget->GetName());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::RunEditorUtilityBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UEditorUtilityBlueprint* EUBlueprint = Cast<UEditorUtilityBlueprint>(LoadedAsset);
	if (!EUBlueprint)
	{
		return MCPError(FString::Printf(TEXT("Failed to load EditorUtilityBlueprint at '%s'"), *AssetPath));
	}

	UEditorUtilitySubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
	if (!Subsystem)
	{
		return MCPError(TEXT("EditorUtilitySubsystem not available"));
	}

	// No rollback: destructive/external — runs an editor utility script.
	Subsystem->TryRun(LoadedAsset);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), EUBlueprint->GetName());

	return MCPResult(Result);
}

// ── Well-known short names → UClass lookup ────────────────────────────
static UClass* ResolveWidgetClass(const FString& ClassName)
{
	// Try well-known short names first (case-insensitive matching)
	static const TMap<FString, FString> ShortNames = {
		// Panels / containers
		{ TEXT("canvaspanel"),       TEXT("/Script/UMG.CanvasPanel") },
		{ TEXT("horizontalbox"),     TEXT("/Script/UMG.HorizontalBox") },
		{ TEXT("verticalbox"),       TEXT("/Script/UMG.VerticalBox") },
		{ TEXT("overlay"),           TEXT("/Script/UMG.Overlay") },
		{ TEXT("gridpanel"),         TEXT("/Script/UMG.GridPanel") },
		{ TEXT("uniformgridpanel"),  TEXT("/Script/UMG.UniformGridPanel") },
		{ TEXT("widgetswitcher"),    TEXT("/Script/UMG.WidgetSwitcher") },
		{ TEXT("scrollbox"),         TEXT("/Script/UMG.ScrollBox") },
		{ TEXT("sizebox"),           TEXT("/Script/UMG.SizeBox") },
		{ TEXT("scalebox"),          TEXT("/Script/UMG.ScaleBox") },
		{ TEXT("border"),            TEXT("/Script/UMG.Border") },
		// Common widgets
		{ TEXT("textblock"),         TEXT("/Script/UMG.TextBlock") },
		{ TEXT("image"),             TEXT("/Script/UMG.Image") },
		{ TEXT("button"),            TEXT("/Script/UMG.Button") },
		{ TEXT("progressbar"),       TEXT("/Script/UMG.ProgressBar") },
		{ TEXT("checkbox"),          TEXT("/Script/UMG.CheckBox") },
		{ TEXT("slider"),            TEXT("/Script/UMG.Slider") },
		{ TEXT("editabletextbox"),   TEXT("/Script/UMG.EditableTextBox") },
		{ TEXT("comboboxstring"),    TEXT("/Script/UMG.ComboBoxString") },
		{ TEXT("spacer"),            TEXT("/Script/UMG.Spacer") },
		{ TEXT("richtextblock"),     TEXT("/Script/UMG.RichTextBlock") },
	};

	FString Key = ClassName.ToLower();
	if (const FString* FullPath = ShortNames.Find(Key))
	{
		UClass* Found = FindObject<UClass>(nullptr, **FullPath);
		if (Found) return Found;
	}

	// Try as full class path  e.g. /Script/UMG.CanvasPanel
	UClass* FullPathClass = FindObject<UClass>(nullptr, *ClassName);
	if (FullPathClass && FullPathClass->IsChildOf(UWidget::StaticClass()))
	{
		return FullPathClass;
	}

	// Try /Script/UMG.<ClassName>
	FString Guess = FString::Printf(TEXT("/Script/UMG.%s"), *ClassName);
	UClass* GuessClass = FindObject<UClass>(nullptr, *Guess);
	if (GuessClass && GuessClass->IsChildOf(UWidget::StaticClass()))
	{
		return GuessClass;
	}

	// #576: custom user widget BP classes live in content and aren't loaded yet,
	// so FindObject misses them. LoadObject a content path (with or without the
	// generated-class _C suffix), or load the WidgetBlueprint and take its class.
	if (ClassName.StartsWith(TEXT("/")))
	{
		if (UClass* PathClass = LoadObject<UClass>(nullptr, *ClassName))
		{
			if (PathClass->IsChildOf(UWidget::StaticClass())) return PathClass;
		}
		const FString WithC = ClassName.EndsWith(TEXT("_C")) ? ClassName : (ClassName + TEXT("_C"));
		if (UClass* GenClass = LoadObject<UClass>(nullptr, *WithC))
		{
			if (GenClass->IsChildOf(UWidget::StaticClass())) return GenClass;
		}
		if (UObject* Asset = LoadObject<UObject>(nullptr, *ClassName))
		{
			if (UBlueprint* BP = Cast<UBlueprint>(Asset))
			{
				if (BP->GeneratedClass && BP->GeneratedClass->IsChildOf(UWidget::StaticClass()))
				{
					return BP->GeneratedClass;
				}
			}
		}
	}

	return nullptr;
}

TSharedPtr<FJsonValue> FWidgetHandlers::AddWidget(const TSharedPtr<FJsonObject>& Params)
{
	// ── Required: assetPath ──
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	// ── Required: widgetClass (e.g. "TextBlock", "CanvasPanel") ──
	FString WidgetClassName;
	if (auto Err = RequireStringAlt(Params, TEXT("widgetClass"), TEXT("typeName"), WidgetClassName)) return Err;

	// ── Optional: widgetName, parentWidgetName ──
	FString WidgetName = OptionalString(Params, TEXT("widgetName"));
	if (WidgetName.IsEmpty()) WidgetName = OptionalString(Params, TEXT("name"));

	FString ParentWidgetName = OptionalString(Params, TEXT("parentWidgetName"));

	// ── Load the WidgetBlueprint ──
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP)
	{
		return MCPError(FString::Printf(TEXT("Failed to load WidgetBlueprint at '%s'"), *AssetPath));
	}

	if (!WidgetBP->WidgetTree)
	{
		return MCPError(TEXT("WidgetTree is null"));
	}

	// Idempotency: if widget with this name already exists, return existed
	if (!WidgetName.IsEmpty())
	{
		UWidget* Existing = nullptr;
		WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (Widget && Widget->GetName() == WidgetName) Existing = Widget;
		});
		if (Existing)
		{
			auto ExistingResult = MCPSuccess();
			MCPSetExisted(ExistingResult);
			ExistingResult->SetStringField(TEXT("widgetName"), WidgetName);
			ExistingResult->SetStringField(TEXT("widgetClass"), Existing->GetClass()->GetName());
			ExistingResult->SetStringField(TEXT("assetPath"), AssetPath);
			return MCPResult(ExistingResult);
		}
	}

	// ── Resolve the UClass ──
	UClass* WClass = ResolveWidgetClass(WidgetClassName);
	if (!WClass)
	{
		return MCPError(FString::Printf(TEXT("Unknown widget class '%s'. Use short names like TextBlock, CanvasPanel, Image, Button, etc."), *WidgetClassName));
	}

	// ── Construct the widget ──
	UWidget* NewWidget = WidgetBP->WidgetTree->ConstructWidget<UWidget>(WClass, WidgetName.IsEmpty() ? NAME_None : FName(*WidgetName));
	if (!NewWidget)
	{
		return MCPError(FString::Printf(TEXT("Failed to construct widget of class '%s'"), *WidgetClassName));
	}

	// ── Place in hierarchy ──
	bool bIsRoot = false;
	if (!ParentWidgetName.IsEmpty())
	{
		// Find specified parent
		UWidget* ParentRaw = nullptr;
		WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (Widget && Widget->GetName() == ParentWidgetName)
			{
				ParentRaw = Widget;
			}
		});

		if (!ParentRaw)
		{
			return MCPError(FString::Printf(TEXT("Parent widget '%s' not found"), *ParentWidgetName));
		}

		UPanelWidget* ParentPanel = Cast<UPanelWidget>(ParentRaw);
		if (!ParentPanel)
		{
			return MCPError(FString::Printf(TEXT("Parent widget '%s' (%s) is not a panel widget and cannot have children"), *ParentWidgetName, *ParentRaw->GetClass()->GetName()));
		}

		UPanelSlot* Slot = ParentPanel->AddChild(NewWidget);
		if (!Slot)
		{
			return MCPError(FString::Printf(TEXT("Failed to add '%s' as child of '%s'"), *NewWidget->GetName(), *ParentWidgetName));
		}
	}
	else if (WidgetBP->WidgetTree->RootWidget == nullptr)
	{
		// No root yet — make this the root widget
		WidgetBP->WidgetTree->RootWidget = NewWidget;
		bIsRoot = true;
	}
	else
	{
		// Root exists, try to add as child of root if it's a panel
		UPanelWidget* RootPanel = Cast<UPanelWidget>(WidgetBP->WidgetTree->RootWidget);
		if (RootPanel)
		{
			RootPanel->AddChild(NewWidget);
		}
		else
		{
			return MCPError(TEXT("Root widget is not a panel. Specify parentWidgetName or set a panel as root first."));
		}
	}

	// UE 5.4 exposed this map; UE 5.5 removed it from UWidgetBlueprint.
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION == 4
	if (!WidgetBP->WidgetVariableNameToGuidMap.Contains(NewWidget->GetFName()))
	{
		WidgetBP->WidgetVariableNameToGuidMap.Add(NewWidget->GetFName(), FGuid::NewGuid());
	}
#endif

	// ── Save ──
	WidgetBP->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("widgetName"), NewWidget->GetName());
	Result->SetStringField(TEXT("widgetClass"), WClass->GetName());
	Result->SetBoolField(TEXT("isRoot"), bIsRoot);
	if (!ParentWidgetName.IsEmpty())
	{
		Result->SetStringField(TEXT("parentWidgetName"), ParentWidgetName);
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("widgetName"), NewWidget->GetName());
	MCPSetRollback(Result, TEXT("remove_widget"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::RemoveWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP)
	{
		return MCPError(FString::Printf(TEXT("Failed to load WidgetBlueprint at '%s'"), *AssetPath));
	}

	if (!WidgetBP->WidgetTree)
	{
		return MCPError(TEXT("WidgetTree is null"));
	}

	// Find the widget
	UWidget* FoundWidget = nullptr;
	WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		if (Widget && Widget->GetName() == WidgetName)
		{
			FoundWidget = Widget;
		}
	});

	if (!FoundWidget)
	{
		// Idempotent: nothing to delete
		auto AlreadyResult = MCPSuccess();
		AlreadyResult->SetBoolField(TEXT("alreadyDeleted"), true);
		AlreadyResult->SetStringField(TEXT("widgetName"), WidgetName);
		AlreadyResult->SetStringField(TEXT("assetPath"), AssetPath);
		return MCPResult(AlreadyResult);
	}

	FString RemovedClass = FoundWidget->GetClass()->GetName();

	// Remove from parent if parented
	UPanelWidget* Parent = FoundWidget->GetParent();
	if (Parent)
	{
		Parent->RemoveChild(FoundWidget);
	}

	// If this was the root widget, clear it
	if (WidgetBP->WidgetTree->RootWidget == FoundWidget)
	{
		WidgetBP->WidgetTree->RootWidget = nullptr;
	}

	// Remove from widget tree
	WidgetBP->WidgetTree->RemoveWidget(FoundWidget);

	WidgetBP->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("widgetClass"), RemovedClass);
	// No rollback: remove_widget is destructive (would need to snapshot widget tree to reverse).

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::MoveWidget(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	FString NewParentName;
	if (auto Err = RequireStringAlt(Params, TEXT("newParentWidgetName"), TEXT("parentWidgetName"), NewParentName)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return MCPError(FString::Printf(TEXT("Failed to load WidgetBlueprint at '%s'"), *AssetPath));
	}

	// Find the widget to move
	UWidget* WidgetToMove = nullptr;
	UWidget* NewParentRaw = nullptr;
	WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		if (Widget && Widget->GetName() == WidgetName) WidgetToMove = Widget;
		if (Widget && Widget->GetName() == NewParentName) NewParentRaw = Widget;
	});

	if (!WidgetToMove)
	{
		return MCPError(FString::Printf(TEXT("Widget not found: '%s'"), *WidgetName));
	}

	if (!NewParentRaw)
	{
		return MCPError(FString::Printf(TEXT("New parent not found: '%s'"), *NewParentName));
	}

	UPanelWidget* NewParentPanel = Cast<UPanelWidget>(NewParentRaw);
	if (!NewParentPanel)
	{
		return MCPError(FString::Printf(TEXT("New parent '%s' (%s) is not a panel widget"), *NewParentName, *NewParentRaw->GetClass()->GetName()));
	}

	// #315: refuse self-parenting and cyclic moves. Walking the WBP root chain
	// down from the new parent and stopping at WidgetToMove would let the move
	// succeed silently while orphaning the entire subtree (read_tree returns
	// empty, the asset cannot reload). Reject before mutating.
	if (NewParentPanel == WidgetToMove)
	{
		return MCPError(FString::Printf(
			TEXT("Refusing cyclic move: cannot reparent '%s' into itself"), *WidgetName));
	}
	{
		UWidget* Ancestor = NewParentPanel;
		while (Ancestor)
		{
			if (Ancestor == WidgetToMove)
			{
				return MCPError(FString::Printf(
					TEXT("Refusing cyclic move: '%s' is an ancestor of '%s' (would create a cycle)"),
					*WidgetName, *NewParentName));
			}
			Ancestor = Ancestor->GetParent();
		}
	}

	// #315: moving the root widget into any other panel orphans the tree (the
	// move clears RootWidget then adds it as a child with no root above it).
	// Use the dedicated wrap/set_root action for that workflow (#365).
	if (WidgetBP->WidgetTree->RootWidget == WidgetToMove)
	{
		return MCPError(FString::Printf(
			TEXT("Cannot move the root widget '%s' via move_widget — use widget(set_root) or widget(wrap_root) instead"),
			*WidgetName));
	}

	// Idempotency: already child of the target parent?
	UPanelWidget* OldParent = WidgetToMove->GetParent();
	FString OldParentName = OldParent ? OldParent->GetName() : TEXT("(root)");
	if (OldParent == NewParentPanel)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("widgetName"), WidgetName);
		Noop->SetStringField(TEXT("oldParent"), OldParentName);
		Noop->SetStringField(TEXT("newParent"), NewParentName);
		return MCPResult(Noop);
	}

	// Remove from current parent
	if (OldParent)
	{
		OldParent->RemoveChild(WidgetToMove);
	}

	// Add to new parent
	NewParentPanel->AddChild(WidgetToMove);

	WidgetBP->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("oldParent"), OldParentName);
	Result->SetStringField(TEXT("newParent"), NewParentName);

	// Rollback: move back to old parent if it was a panel
	if (OldParent)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetStringField(TEXT("widgetName"), WidgetName);
		Payload->SetStringField(TEXT("newParentWidgetName"), OldParentName);
		MCPSetRollback(Result, TEXT("move_widget"), Payload);
	}

	return MCPResult(Result);
}

// #365: replace the WBP's RootWidget with an existing widget by name. The
// previous root is removed from the tree along with its descendants. Used
// when an authoring step needs to swap a placeholder root (e.g. the
// auto-created CanvasPanel) for a different layout.
TSharedPtr<FJsonValue> FWidgetHandlers::SetRoot(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return MCPError(FString::Printf(TEXT("Failed to load WidgetBlueprint at '%s'"), *AssetPath));
	}

	UWidget* NewRoot = nullptr;
	WidgetBP->WidgetTree->ForEachWidget([&](UWidget* W)
	{
		if (W && W->GetName() == WidgetName) NewRoot = W;
	});
	if (!NewRoot)
	{
		return MCPError(FString::Printf(TEXT("Widget not found: '%s'"), *WidgetName));
	}

	UWidget* OldRoot = WidgetBP->WidgetTree->RootWidget;
	if (OldRoot == NewRoot)
	{
		auto Noop = MCPSuccess();
		MCPSetExisted(Noop);
		Noop->SetStringField(TEXT("rootWidget"), WidgetName);
		return MCPResult(Noop);
	}

	WidgetBP->Modify();
	WidgetBP->WidgetTree->Modify();

	// Detach NewRoot from its current parent so the engine doesn't keep it as
	// a descendant of whatever was hosting it (avoids leaving the new root
	// double-parented when AddChild later reassigns it elsewhere).
	if (UPanelWidget* CurrentParent = NewRoot->GetParent())
	{
		CurrentParent->RemoveChild(NewRoot);
	}

	WidgetBP->WidgetTree->RootWidget = NewRoot;

	WidgetBP->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("rootWidget"), WidgetName);
	Result->SetStringField(TEXT("previousRoot"), OldRoot ? OldRoot->GetName() : TEXT("(none)"));
	return MCPResult(Result);
}

// #365: insert a new container around the current root - mirrors UMG's
// "Wrap With" context-menu action. The current root becomes a child of the
// new wrapping widget.
TSharedPtr<FJsonValue> FWidgetHandlers::WrapRoot(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString WrapperClassName;
	if (auto Err = RequireStringAlt(Params, TEXT("wrapperClass"), TEXT("widgetClass"), WrapperClassName)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP || !WidgetBP->WidgetTree)
	{
		return MCPError(FString::Printf(TEXT("Failed to load WidgetBlueprint at '%s'"), *AssetPath));
	}

	UWidget* OldRoot = WidgetBP->WidgetTree->RootWidget;
	if (!OldRoot)
	{
		return MCPError(TEXT("WBP has no root widget yet - use add_widget to set a root first"));
	}

	UClass* WrapperCls = FindClassByShortName(WrapperClassName);
	if (!WrapperCls)
	{
		return MCPError(FString::Printf(TEXT("Widget class not found: %s"), *WrapperClassName));
	}
	if (!WrapperCls->IsChildOf(UPanelWidget::StaticClass()))
	{
		return MCPError(FString::Printf(
			TEXT("Wrapper class '%s' is not a UPanelWidget - cannot host children"), *WrapperClassName));
	}

	const FString NewName = OptionalString(Params, TEXT("wrapperName"));

	WidgetBP->Modify();
	WidgetBP->WidgetTree->Modify();

	UPanelWidget* Wrapper = Cast<UPanelWidget>(WidgetBP->WidgetTree->ConstructWidget<UWidget>(
		WrapperCls, NewName.IsEmpty() ? NAME_None : FName(*NewName)));
	if (!Wrapper)
	{
		return MCPError(TEXT("Failed to construct wrapper widget"));
	}

	WidgetBP->WidgetTree->RootWidget = Wrapper;
	Wrapper->AddChild(OldRoot);

	WidgetBP->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("wrapperName"), Wrapper->GetName());
	Result->SetStringField(TEXT("wrapperClass"), WrapperCls->GetName());
	Result->SetStringField(TEXT("wrappedChild"), OldRoot->GetName());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::ListWidgetClasses(const TSharedPtr<FJsonObject>& Params)
{
	struct FWidgetClassInfo { FString Name; FString Category; };
	TArray<FWidgetClassInfo> Classes = {
		// Panels / containers
		{ TEXT("CanvasPanel"),       TEXT("Panel") },
		{ TEXT("HorizontalBox"),     TEXT("Panel") },
		{ TEXT("VerticalBox"),       TEXT("Panel") },
		{ TEXT("Overlay"),           TEXT("Panel") },
		{ TEXT("GridPanel"),         TEXT("Panel") },
		{ TEXT("UniformGridPanel"),  TEXT("Panel") },
		{ TEXT("WidgetSwitcher"),    TEXT("Panel") },
		{ TEXT("ScrollBox"),         TEXT("Panel") },
		{ TEXT("SizeBox"),           TEXT("Panel") },
		{ TEXT("ScaleBox"),          TEXT("Panel") },
		{ TEXT("Border"),            TEXT("Panel") },
		// Common widgets
		{ TEXT("TextBlock"),         TEXT("Common") },
		{ TEXT("RichTextBlock"),     TEXT("Common") },
		{ TEXT("Image"),             TEXT("Common") },
		{ TEXT("Button"),            TEXT("Common") },
		{ TEXT("CheckBox"),          TEXT("Input") },
		{ TEXT("Slider"),            TEXT("Input") },
		{ TEXT("EditableTextBox"),   TEXT("Input") },
		{ TEXT("ComboBoxString"),    TEXT("Input") },
		{ TEXT("ProgressBar"),       TEXT("Common") },
		{ TEXT("Spacer"),            TEXT("Common") },
	};

	TArray<TSharedPtr<FJsonValue>> ClassesArray;
	for (const FWidgetClassInfo& Info : Classes)
	{
		FString FullPath = FString::Printf(TEXT("/Script/UMG.%s"), *Info.Name);
		UClass* WClass = FindObject<UClass>(nullptr, *FullPath);
		bool bIsPanel = WClass && WClass->IsChildOf(UPanelWidget::StaticClass());

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Info.Name);
		Obj->SetStringField(TEXT("category"), Info.Category);
		Obj->SetBoolField(TEXT("isPanel"), bIsPanel);
		Obj->SetBoolField(TEXT("available"), WClass != nullptr);

		// Slot properties hint
		if (bIsPanel)
		{
			if (Info.Name == TEXT("CanvasPanel"))
				Obj->SetStringField(TEXT("slotProperties"), TEXT("slot.anchors, slot.alignment, slot.position, slot.size, slot.autoSize, slot.zOrder"));
			else if (Info.Name == TEXT("HorizontalBox") || Info.Name == TEXT("VerticalBox"))
				Obj->SetStringField(TEXT("slotProperties"), TEXT("slot.padding, slot.hAlign, slot.vAlign, slot.sizeRule (auto|fill), slot.fillWeight"));
			else if (Info.Name == TEXT("Overlay"))
				Obj->SetStringField(TEXT("slotProperties"), TEXT("slot.padding, slot.hAlign, slot.vAlign"));
		}

		ClassesArray.Add(MakeShared<FJsonValueObject>(Obj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("classes"), ClassesArray);
	Result->SetNumberField(TEXT("count"), ClassesArray.Num());

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #160  Runtime widget inspection — live PIE UUserWidget probing
// ─────────────────────────────────────────────────────────────
namespace WidgetRuntime_Internal
{
	static UWorld* ResolveRuntimeWorld()
	{
		if (!GEditor) return nullptr;
		FWorldContext* PIE = GEditor->GetPIEWorldContext();
		return PIE ? PIE->World() : nullptr;
	}

	static FString SafeGetText(UWidget* Widget)
	{
		if (UTextBlock* T = Cast<UTextBlock>(Widget))       return T->GetText().ToString();
		if (URichTextBlock* R = Cast<URichTextBlock>(Widget)) return R->GetText().ToString();
		if (UEditableTextBox* E = Cast<UEditableTextBox>(Widget)) return E->GetText().ToString();
		if (UButton* B = Cast<UButton>(Widget))
		{
			if (B->GetChildrenCount() > 0)
			{
				return SafeGetText(B->GetChildAt(0));
			}
		}
		return FString();
	}

	static FString VisibilityToString(ESlateVisibility V)
	{
		switch (V)
		{
			case ESlateVisibility::Visible: return TEXT("Visible");
			case ESlateVisibility::Collapsed: return TEXT("Collapsed");
			case ESlateVisibility::Hidden: return TEXT("Hidden");
			case ESlateVisibility::HitTestInvisible: return TEXT("HitTestInvisible");
			case ESlateVisibility::SelfHitTestInvisible: return TEXT("SelfHitTestInvisible");
		}
		return TEXT("Unknown");
	}

	static TSharedPtr<FJsonObject> BuildRuntimeNode(UWidget* Widget, int32 Depth, int32 MaxDepth)
	{
		if (!Widget) return nullptr;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Widget->GetName());
		Obj->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
		Obj->SetStringField(TEXT("visibility"), VisibilityToString(Widget->GetVisibility()));
		Obj->SetBoolField(TEXT("isVisible"), Widget->IsVisible());

		FString Text = SafeGetText(Widget);
		if (!Text.IsEmpty())
		{
			Obj->SetStringField(TEXT("text"), Text);
		}

		if (UImage* Image = Cast<UImage>(Widget))
		{
			const FSlateBrush& Brush = Image->GetBrush();
			TSharedPtr<FJsonObject> BrushObj = MakeShared<FJsonObject>();
			BrushObj->SetNumberField(TEXT("imageSizeX"), Brush.ImageSize.X);
			BrushObj->SetNumberField(TEXT("imageSizeY"), Brush.ImageSize.Y);
			if (UObject* Resource = Brush.GetResourceObject())
			{
				BrushObj->SetStringField(TEXT("resource"), Resource->GetPathName());
			}
			Obj->SetObjectField(TEXT("brush"), BrushObj);
		}
		else if (UProgressBar* PB = Cast<UProgressBar>(Widget))
		{
			Obj->SetNumberField(TEXT("percent"), PB->GetPercent());
		}
		else if (UCheckBox* CB = Cast<UCheckBox>(Widget))
		{
			Obj->SetBoolField(TEXT("isChecked"), CB->IsChecked());
		}
		else if (USlider* Slider = Cast<USlider>(Widget))
		{
			Obj->SetNumberField(TEXT("value"), Slider->GetValue());
		}

		// #592: style properties needed to verify visuals at runtime, not just
		// tree/text. RenderOpacity applies to every UWidget; ColorAndOpacity and
		// Border tint are per-type.
		{
			auto ColorJson = [](const FLinearColor& C)
			{
				TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
				O->SetNumberField(TEXT("r"), C.R); O->SetNumberField(TEXT("g"), C.G);
				O->SetNumberField(TEXT("b"), C.B); O->SetNumberField(TEXT("a"), C.A);
				return O;
			};
			Obj->SetNumberField(TEXT("renderOpacity"), Widget->GetRenderOpacity());
			if (UTextBlock* TextW = Cast<UTextBlock>(Widget))
			{
				Obj->SetObjectField(TEXT("colorAndOpacity"), ColorJson(TextW->GetColorAndOpacity().GetSpecifiedColor()));
			}
			else if (UImage* ImgW = Cast<UImage>(Widget))
			{
				Obj->SetObjectField(TEXT("colorAndOpacity"), ColorJson(ImgW->GetColorAndOpacity()));
			}
			else if (UBorder* BorderW = Cast<UBorder>(Widget))
			{
				Obj->SetObjectField(TEXT("brushColor"), ColorJson(BorderW->GetBrushColor()));
				Obj->SetObjectField(TEXT("contentColorAndOpacity"), ColorJson(BorderW->GetContentColorAndOpacity()));
			}
		}

		if (Depth >= MaxDepth) return Obj;

		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			TArray<TSharedPtr<FJsonValue>> ChildrenArr;
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				TSharedPtr<FJsonObject> ChildObj = BuildRuntimeNode(Panel->GetChildAt(i), Depth + 1, MaxDepth);
				if (ChildObj.IsValid())
				{
					ChildrenArr.Add(MakeShared<FJsonValueObject>(ChildObj));
				}
			}
			Obj->SetArrayField(TEXT("children"), ChildrenArr);
		}
		else if (UUserWidget* User = Cast<UUserWidget>(Widget))
		{
			// Nested UUserWidget: descend into its WidgetTree's root.
			if (User->WidgetTree && User->WidgetTree->RootWidget)
			{
				TSharedPtr<FJsonObject> RootObj = BuildRuntimeNode(User->WidgetTree->RootWidget, Depth + 1, MaxDepth);
				if (RootObj.IsValid())
				{
					Obj->SetObjectField(TEXT("root"), RootObj);
				}
			}
		}

		return Obj;
	}
}

TSharedPtr<FJsonValue> FWidgetHandlers::ListRuntimeWidgets(const TSharedPtr<FJsonObject>& Params)
{
	using namespace WidgetRuntime_Internal;

	UWorld* World = ResolveRuntimeWorld();
	if (!World)
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	// Optional filter: class name (contains) / name prefix
	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"), TEXT(""));
	const FString NamePrefix  = OptionalString(Params, TEXT("namePrefix"), TEXT(""));
	const bool bInViewportOnly = OptionalBool(Params, TEXT("viewportOnly"), false);

	TArray<TSharedPtr<FJsonValue>> WidgetsArr;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget)) continue;
		if (Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) continue;

		UWorld* WidgetWorld = Widget->GetWorld();
		if (WidgetWorld != World) continue;

		const FString ClassName = Widget->GetClass()->GetName();
		const FString Name = Widget->GetName();
		if (!ClassFilter.IsEmpty() && !ClassName.Contains(ClassFilter)) continue;
		if (!NamePrefix.IsEmpty()  && !Name.StartsWith(NamePrefix)) continue;
		if (bInViewportOnly && !Widget->IsInViewport()) continue;

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Name);
		Obj->SetStringField(TEXT("class"), ClassName);
		Obj->SetStringField(TEXT("visibility"), VisibilityToString(Widget->GetVisibility()));
		Obj->SetBoolField(TEXT("isVisible"), Widget->IsVisible());
		Obj->SetBoolField(TEXT("inViewport"), Widget->IsInViewport());
		if (Widget->WidgetTree && Widget->WidgetTree->RootWidget)
		{
			Obj->SetStringField(TEXT("rootWidgetName"), Widget->WidgetTree->RootWidget->GetName());
			Obj->SetStringField(TEXT("rootWidgetClass"), Widget->WidgetTree->RootWidget->GetClass()->GetName());
		}
		WidgetsArr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("world"), World->GetName());
	Result->SetArrayField(TEXT("widgets"), WidgetsArr);
	Result->SetNumberField(TEXT("count"), WidgetsArr.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::GetRuntimeWidget(const TSharedPtr<FJsonObject>& Params)
{
	using namespace WidgetRuntime_Internal;

	UWorld* World = ResolveRuntimeWorld();
	if (!World)
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	FString WidgetName;
	Params->TryGetStringField(TEXT("widgetName"), WidgetName);
	FString ClassFilter;
	Params->TryGetStringField(TEXT("className"), ClassFilter);
	if (WidgetName.IsEmpty() && ClassFilter.IsEmpty())
	{
		return MCPError(TEXT("Provide widgetName (exact instance name) or className (first match)."));
	}

	const int32 MaxDepth = OptionalInt(Params, TEXT("maxDepth"), 6);
	const FString ChildName = OptionalString(Params, TEXT("childName"), TEXT(""));

	UUserWidget* Found = nullptr;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) continue;
		if (Widget->GetWorld() != World) continue;

		if (!WidgetName.IsEmpty() && Widget->GetName() != WidgetName) continue;
		if (!ClassFilter.IsEmpty() && !Widget->GetClass()->GetName().Contains(ClassFilter)) continue;

		Found = Widget;
		break;
	}

	if (!Found)
	{
		return MCPError(TEXT("Runtime widget not found. Try list_runtime_widgets to see available instances."));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("name"), Found->GetName());
	Result->SetStringField(TEXT("class"), Found->GetClass()->GetName());
	Result->SetStringField(TEXT("visibility"), VisibilityToString(Found->GetVisibility()));
	Result->SetBoolField(TEXT("inViewport"), Found->IsInViewport());

	if (Found->WidgetTree && Found->WidgetTree->RootWidget)
	{
		UWidget* ScanRoot = Found->WidgetTree->RootWidget;
		if (!ChildName.IsEmpty())
		{
			// Search the widget tree for the named child.
			UWidget* Target = nullptr;
			Found->WidgetTree->ForEachWidget([&](UWidget* W)
			{
				if (W && W->GetName() == ChildName && !Target)
				{
					Target = W;
				}
			});
			if (!Target)
			{
				return MCPError(FString::Printf(TEXT("Child widget '%s' not found inside '%s'"), *ChildName, *Found->GetName()));
			}
			ScanRoot = Target;
		}

		TSharedPtr<FJsonObject> Tree = BuildRuntimeNode(ScanRoot, 0, MaxDepth);
		if (Tree.IsValid())
		{
			Result->SetObjectField(TEXT("tree"), Tree);
		}
	}
	else
	{
		Result->SetStringField(TEXT("tree"), TEXT("empty"));
	}

	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #602  Instantiate a WidgetBlueprint into the live PIE viewport.
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FWidgetHandlers::AddWidgetToViewport(const TSharedPtr<FJsonObject>& Params)
{
	using namespace WidgetRuntime_Internal;
	UWorld* World = ResolveRuntimeWorld();
	if (!World)
	{
		return MCPError(TEXT("No PIE world available. Start Play-In-Editor first (editor pie_control action=play)."));
	}

	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("widgetBlueprintPath"), AssetPath)) return Err;

	// Resolve the WidgetBlueprint's generated UUserWidget class.
	UClass* WidgetClass = LoadClass<UUserWidget>(nullptr, *AssetPath);
	if (!WidgetClass)
	{
		if (UWidgetBlueprint* WBP = LoadObject<UWidgetBlueprint>(nullptr, *AssetPath))
		{
			WidgetClass = WBP->GeneratedClass;
		}
		else if (!AssetPath.EndsWith(TEXT("_C")))
		{
			WidgetClass = LoadClass<UUserWidget>(nullptr, *(AssetPath + TEXT("_C")));
		}
	}
	if (!WidgetClass || !WidgetClass->IsChildOf(UUserWidget::StaticClass()))
	{
		return MCPError(FString::Printf(TEXT("Could not resolve a UserWidget class from '%s'"), *AssetPath));
	}

	APlayerController* PC = World->GetFirstPlayerController();
	UUserWidget* Widget = PC
		? CreateWidget<UUserWidget>(PC, WidgetClass)
		: CreateWidget<UUserWidget>(World, WidgetClass);
	if (!Widget)
	{
		return MCPError(TEXT("CreateWidget returned null"));
	}
	const int32 ZOrder = OptionalInt(Params, TEXT("zOrder"), 0);
	Widget->AddToViewport(ZOrder);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("instanceName"), Widget->GetName());
	Result->SetStringField(TEXT("class"), WidgetClass->GetName());
	Result->SetBoolField(TEXT("inViewport"), Widget->IsInViewport());
	Result->SetNumberField(TEXT("zOrder"), ZOrder);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #559  Fire a UFUNCTION or button click on a live PIE UUserWidget.
//   Params: widgetName|className (locate the UserWidget), functionName
//   (a parameterless UFUNCTION on the widget), OR childName + "OnClicked"
//   to simulate a button click on a child UButton.
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FWidgetHandlers::InvokeRuntimeWidgetFunction(const TSharedPtr<FJsonObject>& Params)
{
	using namespace WidgetRuntime_Internal;
	UWorld* World = ResolveRuntimeWorld();
	if (!World)
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	FString WidgetName = OptionalString(Params, TEXT("widgetName"));
	FString ClassFilter = OptionalString(Params, TEXT("className"));
	if (WidgetName.IsEmpty() && ClassFilter.IsEmpty())
	{
		return MCPError(TEXT("Provide widgetName (exact instance name) or className (first match)."));
	}

	UUserWidget* Found = nullptr;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) continue;
		if (Widget->GetWorld() != World) continue;
		if (!WidgetName.IsEmpty() && Widget->GetName() != WidgetName) continue;
		if (!ClassFilter.IsEmpty() && !Widget->GetClass()->GetName().Contains(ClassFilter)) continue;
		Found = Widget;
		break;
	}
	if (!Found)
	{
		return MCPError(TEXT("Runtime widget not found. Try list_runtime_widgets."));
	}

	const FString ChildName = OptionalString(Params, TEXT("childName"));
	const FString FunctionName = OptionalString(Params, TEXT("functionName"));

	// Button-click path: childName names a UButton, broadcast OnClicked.
	if (!ChildName.IsEmpty() && (FunctionName.IsEmpty() || FunctionName.Equals(TEXT("OnClicked"), ESearchCase::IgnoreCase)))
	{
		UWidget* Target = nullptr;
		if (Found->WidgetTree)
		{
			Found->WidgetTree->ForEachWidget([&](UWidget* W)
			{
				if (W && W->GetName() == ChildName && !Target) Target = W;
			});
		}
		if (!Target)
		{
			return MCPError(FString::Printf(TEXT("Child widget '%s' not found inside '%s'"), *ChildName, *Found->GetName()));
		}
		if (UButton* Button = Cast<UButton>(Target))
		{
			Button->OnClicked.Broadcast();
			auto Result = MCPSuccess();
			Result->SetStringField(TEXT("widget"), Found->GetName());
			Result->SetStringField(TEXT("child"), ChildName);
			Result->SetStringField(TEXT("invoked"), TEXT("OnClicked"));
			return MCPResult(Result);
		}
		return MCPError(FString::Printf(TEXT("Child '%s' is a %s, not a UButton (only OnClicked is supported for click simulation)"), *ChildName, *Target->GetClass()->GetName()));
	}

	// UFUNCTION path: call a parameterless function on the UserWidget.
	if (FunctionName.IsEmpty())
	{
		return MCPError(TEXT("Provide functionName (parameterless UFUNCTION) or childName (button click)."));
	}
	UFunction* Func = Found->FindFunction(FName(*FunctionName));
	if (!Func)
	{
		return MCPError(FString::Printf(TEXT("Function '%s' not found on widget '%s'"), *FunctionName, *Found->GetClass()->GetName()));
	}
	if (Func->NumParms != 0)
	{
		return MCPError(FString::Printf(TEXT("Function '%s' takes %d parameter(s); only parameterless functions are supported here"), *FunctionName, Func->NumParms));
	}
	Found->ProcessEvent(Func, nullptr);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("widget"), Found->GetName());
	Result->SetStringField(TEXT("invoked"), FunctionName);
	return MCPResult(Result);
}

// ─────────────────────────────────────────────────────────────
// #161  Runtime delegate inspection — list FMulticastDelegateProperty fields on a live UUserWidget
// ─────────────────────────────────────────────────────────────
TSharedPtr<FJsonValue> FWidgetHandlers::GetRuntimeDelegates(const TSharedPtr<FJsonObject>& Params)
{
	using namespace WidgetRuntime_Internal;

	UWorld* World = ResolveRuntimeWorld();
	if (!World)
	{
		return MCPError(TEXT("No PIE world available. Is Play-In-Editor running?"));
	}

	FString WidgetName;
	Params->TryGetStringField(TEXT("widgetName"), WidgetName);
	FString ClassFilter;
	Params->TryGetStringField(TEXT("className"), ClassFilter);
	if (WidgetName.IsEmpty() && ClassFilter.IsEmpty())
	{
		return MCPError(TEXT("Provide 'widgetName' (exact instance name) or 'className' (first match)."));
	}

	UUserWidget* Found = nullptr;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || Widget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)) continue;
		if (Widget->GetWorld() != World) continue;

		if (!WidgetName.IsEmpty() && Widget->GetName() != WidgetName) continue;
		if (!ClassFilter.IsEmpty() && !Widget->GetClass()->GetName().Contains(ClassFilter)) continue;

		Found = Widget;
		break;
	}

	if (!Found)
	{
		return MCPError(TEXT("Runtime widget not found. Try list_runtime_widgets to see available instances."));
	}

	TArray<TSharedPtr<FJsonValue>> DelegatesArr;
	for (TFieldIterator<FMulticastDelegateProperty> It(Found->GetClass()); It; ++It)
	{
		FMulticastDelegateProperty* DelegateProp = *It;
		if (!DelegateProp) continue;

		const void* DelegateAddr = DelegateProp->ContainerPtrToValuePtr<void>(Found);
		const FMulticastScriptDelegate* ScriptDelegate = DelegateProp->GetMulticastDelegate(DelegateAddr);

		TSharedPtr<FJsonObject> DelegateObj = MakeShared<FJsonObject>();
		DelegateObj->SetStringField(TEXT("delegateName"), DelegateProp->GetName());

		bool bIsBound = false;
		int32 NumBindings = 0;
		if (ScriptDelegate)
		{
			bIsBound = ScriptDelegate->IsBound();
			// Use export text to estimate the number of bindings
			FString ExportedStr;
			DelegateProp->ExportTextItem_Direct(ExportedStr, DelegateAddr, nullptr, Found, PPF_None);
			if (!ExportedStr.IsEmpty() && bIsBound)
			{
				// Count comma-separated entries in the exported delegate text
				NumBindings = 1;
				for (const TCHAR& Ch : ExportedStr)
				{
					if (Ch == TEXT(',')) ++NumBindings;
				}
			}
		}

		DelegateObj->SetBoolField(TEXT("isBound"), bIsBound);
		DelegateObj->SetNumberField(TEXT("numBindings"), NumBindings);
		DelegatesArr.Add(MakeShared<FJsonValueObject>(DelegateObj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("widgetName"), Found->GetName());
	Result->SetStringField(TEXT("widgetClass"), Found->GetClass()->GetName());
	Result->SetArrayField(TEXT("delegates"), DelegatesArr);
	Result->SetNumberField(TEXT("delegateCount"), DelegatesArr.Num());
	return MCPResult(Result);
}
