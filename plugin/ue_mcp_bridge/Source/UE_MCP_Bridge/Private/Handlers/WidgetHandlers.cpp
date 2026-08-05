#include "WidgetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerAssetCreate.h"
#include <type_traits>
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
#include "Components/PanelSlot.h"
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
#include "Layout/SlateRect.h"
#include "Misc/App.h"
#include "UObject/UnrealType.h"
#include "Editor.h"
#include "EditorUtilitySubsystem.h"
#include "EditorUtilityWidget.h"
#include "EditorUtilityWidgetBlueprint.h"
#include "EditorUtilityBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Engine/Texture2D.h"
#include "Engine/GameViewportClient.h"
#include "Materials/MaterialInterface.h"
#include "EngineUtils.h"
#include "Widgets/SViewport.h"

void FWidgetHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_widget_blueprints"), &ListWidgetBlueprints);
	Registry.RegisterHandler(TEXT("create_widget_blueprint"), &CreateWidgetBlueprint);
	Registry.RegisterHandler(TEXT("read_widget_tree"), &ReadWidgetTree);
	Registry.RegisterHandler(TEXT("extract_widget_subtree"), &ExtractWidgetSubtree);
	Registry.RegisterHandler(TEXT("create_editor_utility_widget"), &CreateEditorUtilityWidget);
	Registry.RegisterHandler(TEXT("create_editor_utility_blueprint"), &CreateEditorUtilityBlueprint);
	Registry.RegisterHandler(TEXT("get_widget_details"), &GetWidgetProperties);
	Registry.RegisterHandler(TEXT("get_widget_properties"), &GetWidgetFullProperties);
	Registry.RegisterHandler(TEXT("list_widget_bindings"), &ListWidgetBindings);
	Registry.RegisterHandler(TEXT("clear_widget_binding"), &ClearWidgetBinding);
	Registry.RegisterHandler(TEXT("set_widget_property"), &SetWidgetProperty);
	Registry.RegisterHandler(TEXT("set_widget_style"), &SetWidgetStyle);
	Registry.RegisterHandler(TEXT("bulk_set_widget_properties"), &BulkSetWidgetProperties);
	Registry.RegisterHandler(TEXT("reorder_child"), &ReorderChild);
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
	Registry.RegisterHandler(TEXT("inspect_runtime_instances"), &InspectRuntimeInstances);
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

	// No rollback: destructive/external - opens a dockable tab in the editor.
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

	// No rollback: destructive/external - runs an editor utility script.
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

// ── Widget variable GUID metadata (#728, #799) ───────────────────────────────
// UWidgetBlueprint keeps a WidgetVariableNameToGuidMap so external references
// survive a widget rename. The WidgetBlueprintCompiler checks it both ways:
// every widget variable must own a GUID, and every GUID must still name a live
// variable. Registering an entry without ever dropping it leaves the map
// pointing at names nothing answers to, and the next compile of that asset
// raises "Variable [X] was deleted but still has a GUID referenced by
// WidgetBlueprint [Y]" and keeps raising it on every later compile.
//
// The map is editor-only data whose presence has moved around across engine
// versions, so it is detected at compile time here rather than tracked with a
// hand-maintained version window.
namespace MCPWidgetGuidMap
{
	template <typename T, typename = void>
	struct THasMap : std::false_type {};

	template <typename T>
	struct THasMap<T, std::void_t<decltype(T::WidgetVariableNameToGuidMap)>> : std::true_type {};

	/** Give a widget/animation variable a GUID entry when it has none. */
	template <typename TWidgetBP>
	void Register(TWidgetBP* WidgetBP, const FName& VariableName)
	{
		if constexpr (THasMap<TWidgetBP>::value)
		{
			if (WidgetBP && !VariableName.IsNone() && !WidgetBP->WidgetVariableNameToGuidMap.Contains(VariableName))
			{
				WidgetBP->WidgetVariableNameToGuidMap.Add(VariableName, FGuid::NewGuid());
			}
		}
	}

	/**
	 * Drop every entry whose name no longer resolves to a widget in the tree,
	 * an animation, or a blueprint variable. Returns how many were dropped.
	 * This is the set the compiler builds when it validates the map, so an
	 * entry outside it is dead metadata by definition.
	 */
	template <typename TWidgetBP>
	int32 PruneStale(TWidgetBP* WidgetBP)
	{
		if constexpr (THasMap<TWidgetBP>::value)
		{
			if (!WidgetBP) return 0;

			TSet<FName> Live;
			if (WidgetBP->WidgetTree)
			{
				WidgetBP->WidgetTree->ForEachWidget([&Live](UWidget* Widget)
				{
					if (Widget) Live.Add(Widget->GetFName());
				});
			}
			for (const auto& Animation : WidgetBP->Animations)
			{
				if (Animation) Live.Add(Animation->GetFName());
			}
			for (const auto& Variable : WidgetBP->NewVariables)
			{
				Live.Add(Variable.VarName);
			}

			TArray<FName> Stale;
			for (const auto& Entry : WidgetBP->WidgetVariableNameToGuidMap)
			{
				if (!Live.Contains(Entry.Key)) Stale.Add(Entry.Key);
			}
			for (const FName& Name : Stale)
			{
				WidgetBP->WidgetVariableNameToGuidMap.Remove(Name);
			}
			return Stale.Num();
		}
		else
		{
			return 0;
		}
	}
}

/**
 * Compile state of a Widget Blueprint as a stable string (#799). A caller that
 * gets `created: true` still needs to know whether the asset it just changed
 * compiles, so the mutation handlers report this alongside the outcome.
 */
static FString WidgetCompileStatusString(const UWidgetBlueprint* WidgetBP)
{
	if (!WidgetBP) return TEXT("unknown");
	switch (WidgetBP->Status.GetValue())
	{
	case BS_UpToDate:             return TEXT("upToDate");
	case BS_UpToDateWithWarnings: return TEXT("upToDateWithWarnings");
	case BS_Dirty:                return TEXT("dirty");
	case BS_Error:                return TEXT("error");
	case BS_BeingCreated:         return TEXT("beingCreated");
	default:                      return TEXT("unknown");
	}
}

/** Stamp compile state onto a mutation result, and withdraw the success claim
 *  when the blueprint no longer compiles (#799). */
static void MCPSetWidgetCompileOutcome(
	TSharedPtr<FJsonObject> Result,
	const UWidgetBlueprint* WidgetBP,
	const FString& AssetPath,
	const FString& WhatHappened)
{
	const FString CompileStatus = WidgetCompileStatusString(WidgetBP);
	Result->SetStringField(TEXT("compileStatus"), CompileStatus);
	if (CompileStatus == TEXT("error"))
	{
		// The mutation is already on disk, so keep reporting what landed, but a
		// blueprint that no longer compiles is not a success.
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%s and saved, but '%s' no longer compiles - open the blueprint's compiler results for the cause."),
			*WhatHappened, *AssetPath));
	}
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

	// ── Resolve the UClass ──
	UClass* WClass = ResolveWidgetClass(WidgetClassName);
	if (!WClass)
	{
		return MCPError(FString::Printf(TEXT("Unknown widget class '%s'. Use short names like TextBlock, CanvasPanel, Image, Button, etc."), *WidgetClassName));
	}

	// Idempotency by assetPath + widgetName: a caller that retries after an
	// ambiguous result (a client-side timeout on a call the editor actually
	// completed) gets the same answer instead of a duplicate widget (#799).
	// The class is compared too, so a name that already belongs to something
	// else is reported rather than passed off as the requested widget.
	if (!WidgetName.IsEmpty())
	{
		UWidget* Existing = nullptr;
		WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (Widget && Widget->GetName() == WidgetName) Existing = Widget;
		});
		if (Existing)
		{
			if (Existing->GetClass() != WClass)
			{
				return MCPError(FString::Printf(
					TEXT("Widget '%s' already exists in '%s' as a %s, not a %s. Pick another widgetName or remove the existing widget first."),
					*WidgetName, *AssetPath, *Existing->GetClass()->GetName(), *WClass->GetName()));
			}

			auto ExistingResult = MCPSuccess();
			MCPSetExisted(ExistingResult);
			ExistingResult->SetStringField(TEXT("widgetName"), WidgetName);
			ExistingResult->SetStringField(TEXT("requestedWidgetName"), WidgetName);
			ExistingResult->SetStringField(TEXT("persistedWidgetName"), WidgetName);
			ExistingResult->SetBoolField(TEXT("renamed"), false);
			ExistingResult->SetStringField(TEXT("widgetClass"), Existing->GetClass()->GetName());
			ExistingResult->SetStringField(TEXT("assetPath"), AssetPath);
			if (UPanelWidget* ExistingParent = Existing->GetParent())
			{
				ExistingResult->SetStringField(TEXT("parentWidgetName"), ExistingParent->GetName());
			}
			ExistingResult->SetBoolField(TEXT("isRoot"), WidgetBP->WidgetTree->RootWidget == Existing);
			return MCPResult(ExistingResult);
		}
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
		// No root yet - make this the root widget
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

	// #728: the WidgetBlueprintCompiler ensures every added widget has an entry in
	// WidgetVariableNameToGuidMap ("Widget [X] was added but did not get a GUID").
	// #799: it ensures the other way too, so drop entries the tree no longer
	// backs before compiling instead of accumulating them.
	MCPWidgetGuidMap::Register(WidgetBP, NewWidget->GetFName());
	MCPWidgetGuidMap::PruneStale(WidgetBP);

	// ── Save ──
	// Read the name back off the widget after the compile, not before: the
	// compile is what settles the name the asset is saved with (#799).
	TWeakObjectPtr<UWidget> AddedWidget(NewWidget);
	FString PersistedName = NewWidget->GetName();

	WidgetBP->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);

	// The compile can rename a widget whose requested name collided with an
	// existing variable. Re-point the metadata at the tree as it stands now, so
	// the name that reaches disk is the name that owns the GUID (#799).
	if (AddedWidget.IsValid())
	{
		PersistedName = AddedWidget->GetName();
		MCPWidgetGuidMap::Register(WidgetBP, AddedWidget->GetFName());
	}
	MCPWidgetGuidMap::PruneStale(WidgetBP);

	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("widgetName"), PersistedName);
	// Both names, always: the requested one is what a caller retries with, the
	// persisted one is what the asset actually holds (#799).
	Result->SetStringField(TEXT("persistedWidgetName"), PersistedName);
	if (!WidgetName.IsEmpty())
	{
		Result->SetStringField(TEXT("requestedWidgetName"), WidgetName);
		Result->SetBoolField(TEXT("renamed"), !WidgetName.Equals(PersistedName, ESearchCase::CaseSensitive));
	}
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("widgetClass"), WClass->GetName());
	Result->SetBoolField(TEXT("isRoot"), bIsRoot);
	if (!ParentWidgetName.IsEmpty())
	{
		Result->SetStringField(TEXT("parentWidgetName"), ParentWidgetName);
	}
	MCPSetWidgetCompileOutcome(Result, WidgetBP, AssetPath,
		FString::Printf(TEXT("Widget '%s' was added"), *PersistedName));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("widgetName"), PersistedName);
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
		// Idempotent: nothing to delete. An asset last touched by an older build
		// can still carry the GUID entry of a widget that is already gone, and
		// this is the call an agent makes after the compiler complains about
		// that name, so clear the dead metadata here too (#799).
		const int32 PrunedOnly = MCPWidgetGuidMap::PruneStale(WidgetBP);
		if (PrunedOnly > 0)
		{
			WidgetBP->MarkPackageDirty();
			UEditorAssetLibrary::SaveAsset(AssetPath);
		}

		auto AlreadyResult = MCPSuccess();
		AlreadyResult->SetBoolField(TEXT("alreadyDeleted"), true);
		AlreadyResult->SetStringField(TEXT("widgetName"), WidgetName);
		AlreadyResult->SetStringField(TEXT("assetPath"), AssetPath);
		AlreadyResult->SetNumberField(TEXT("prunedGuidEntries"), PrunedOnly);
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

	// Remove from widget tree (takes the whole subtree with it)
	WidgetBP->WidgetTree->RemoveWidget(FoundWidget);

	// #799: the removed widget and every descendant it took with it still own
	// entries in WidgetVariableNameToGuidMap. Drop them before the compile that
	// validates the map, otherwise this asset ensures on every later compile
	// and lookups keep resolving to widgets that no longer exist.
	const int32 PrunedGuids = MCPWidgetGuidMap::PruneStale(WidgetBP);

	WidgetBP->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);
	MCPWidgetGuidMap::PruneStale(WidgetBP);
	UEditorAssetLibrary::SaveAsset(AssetPath);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("deleted"), true);
	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("widgetClass"), RemovedClass);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("prunedGuidEntries"), PrunedGuids);
	MCPSetWidgetCompileOutcome(Result, WidgetBP, AssetPath,
		FString::Printf(TEXT("Widget '%s' was removed"), *WidgetName));
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
			TEXT("Cannot move the root widget '%s' via move_widget - use widget(set_root) or widget(wrap_root) instead"),
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

	// #799: the previous root and its descendants left the tree, so their GUID
	// entries are dead metadata. Drop them before the compile validates the map.
	MCPWidgetGuidMap::PruneStale(WidgetBP);

	WidgetBP->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);
	MCPWidgetGuidMap::PruneStale(WidgetBP);
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

	// #728: register the new wrapper's GUID so the WidgetBlueprintCompiler ensure
	// does not fire (see add_widget), and #799: prune whatever the reshuffle
	// orphaned so the map matches the tree that is about to be saved.
	MCPWidgetGuidMap::Register(WidgetBP, Wrapper->GetFName());
	MCPWidgetGuidMap::PruneStale(WidgetBP);

	TWeakObjectPtr<UPanelWidget> AddedWrapper(Wrapper);

	WidgetBP->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);
	if (AddedWrapper.IsValid())
	{
		MCPWidgetGuidMap::Register(WidgetBP, AddedWrapper->GetFName());
	}
	MCPWidgetGuidMap::PruneStale(WidgetBP);
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
// #160  Runtime widget inspection - live PIE UUserWidget probing
// ─────────────────────────────────────────────────────────────
namespace WidgetRuntime_Internal
{
	struct FDerivedClipState
	{
		bool bHasRect = false;
		bool bAlwaysClip = false;
		FSlateRect Rect;
		FString SourcePath;
	};

	struct FRuntimeLayoutSample
	{
		FVector2D DesiredSize = FVector2D::ZeroVector;
		FVector2D LocalSize = FVector2D::ZeroVector;
		FVector2D AbsoluteSize = FVector2D::ZeroVector;
		FVector2D AbsolutePosition = FVector2D::ZeroVector;
		FSlateRect RenderRect;
		FString SlotSignature;
		bool bHasCanvasSlot = false;
		bool bCanvasAutoSize = false;
		FAnchors CanvasAnchors;
		FMargin CanvasOffsets;
	};

	// Per-call state for the optional layout pass. Bundled into one struct so the
	// recursive walk keeps a readable signature, and so a call that did not ask
	// for layout can skip the whole block by checking a single flag.
	struct FRuntimeScanContext
	{
		bool bIncludeLayout = false;
		TOptional<FSlateRect> ViewportRect;
		const TMap<FString, FRuntimeLayoutSample>* PreviousSamples = nullptr;
		TMap<FString, FRuntimeLayoutSample> CurrentSamples;
		int32 WarningCount = 0;
		int32 ChangedNodeCount = 0;
	};

	static TMap<FString, TMap<FString, FRuntimeLayoutSample>> PreviousLayoutCaptures;
	static TMap<FString, uint64> PreviousLayoutCaptureFrames;
	static uint64 LayoutCaptureSequence = 0;

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

	static TSharedPtr<FJsonObject> VectorJson(const FVector2D& Value)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), Value.X);
		Obj->SetNumberField(TEXT("y"), Value.Y);
		return Obj;
	}

	static TSharedPtr<FJsonObject> RectJson(const FSlateRect& Rect)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("left"), Rect.Left);
		Obj->SetNumberField(TEXT("top"), Rect.Top);
		Obj->SetNumberField(TEXT("right"), Rect.Right);
		Obj->SetNumberField(TEXT("bottom"), Rect.Bottom);
		Obj->SetNumberField(TEXT("width"), Rect.Right - Rect.Left);
		Obj->SetNumberField(TEXT("height"), Rect.Bottom - Rect.Top);
		Obj->SetBoolField(TEXT("valid"), Rect.IsValid());
		return Obj;
	}

	static TSharedPtr<FJsonObject> MarginJson(const FMargin& Margin)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("left"), Margin.Left);
		Obj->SetNumberField(TEXT("top"), Margin.Top);
		Obj->SetNumberField(TEXT("right"), Margin.Right);
		Obj->SetNumberField(TEXT("bottom"), Margin.Bottom);
		return Obj;
	}

	static FString ClippingToString(EWidgetClipping Clipping)
	{
		if (const UEnum* Enum = StaticEnum<EWidgetClipping>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Clipping));
		}
		return TEXT("Unknown");
	}

	static TSharedPtr<FJsonObject> BuildSlotJson(UWidget* Widget, FString& OutSignature)
	{
		UPanelSlot* Slot = Widget ? Widget->Slot : nullptr;
		if (!Slot)
		{
			OutSignature.Reset();
			return nullptr;
		}

		TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
		SlotObj->SetStringField(TEXT("class"), Slot->GetClass()->GetName());

		TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
		TArray<FString> SignatureParts;
		for (TFieldIterator<FProperty> It(Slot->GetClass()); It; ++It)
		{
			FProperty* Property = *It;
			FString Value;
			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Slot);
			Property->ExportText_Direct(Value, ValuePtr, ValuePtr, Slot, PPF_None);
			Properties->SetStringField(Property->GetName(), Value);
			SignatureParts.Add(Property->GetName() + TEXT("=") + Value);
		}
		SignatureParts.Sort();
		OutSignature = FString::Join(SignatureParts, TEXT("|"));
		SlotObj->SetObjectField(TEXT("properties"), Properties);

		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
		{
			const FAnchors Anchors = CanvasSlot->GetAnchors();
			const FMargin Offsets = CanvasSlot->GetOffsets();
			const FVector2D Alignment = CanvasSlot->GetAlignment();
			TSharedPtr<FJsonObject> Canvas = MakeShared<FJsonObject>();
			TSharedPtr<FJsonObject> AnchorsObj = MakeShared<FJsonObject>();
			AnchorsObj->SetObjectField(TEXT("minimum"), VectorJson(Anchors.Minimum));
			AnchorsObj->SetObjectField(TEXT("maximum"), VectorJson(Anchors.Maximum));
			AnchorsObj->SetBoolField(TEXT("stretchedHorizontally"), !FMath::IsNearlyEqual(Anchors.Minimum.X, Anchors.Maximum.X));
			AnchorsObj->SetBoolField(TEXT("stretchedVertically"), !FMath::IsNearlyEqual(Anchors.Minimum.Y, Anchors.Maximum.Y));
			Canvas->SetObjectField(TEXT("anchors"), AnchorsObj);
			Canvas->SetObjectField(TEXT("offsets"), MarginJson(Offsets));
			Canvas->SetObjectField(TEXT("alignment"), VectorJson(Alignment));
			Canvas->SetBoolField(TEXT("autoSize"), CanvasSlot->GetAutoSize());
			Canvas->SetNumberField(TEXT("zOrder"), CanvasSlot->GetZOrder());
			SlotObj->SetObjectField(TEXT("canvas"), Canvas);
		}

		return SlotObj;
	}

	static void AddWarning(
		TArray<TSharedPtr<FJsonValue>>& Warnings,
		const FString& Code,
		const FString& Severity,
		const FString& Message)
	{
		TSharedPtr<FJsonObject> Warning = MakeShared<FJsonObject>();
		Warning->SetStringField(TEXT("code"), Code);
		Warning->SetStringField(TEXT("severity"), Severity);
		Warning->SetStringField(TEXT("message"), Message);
		Warnings.Add(MakeShared<FJsonValueObject>(Warning));
	}

	static bool VectorNearlyEqual(const FVector2D& A, const FVector2D& B, double Tolerance = 0.05)
	{
		return A.Equals(B, Tolerance);
	}

	static FDerivedClipState ResolveClipState(
		UWidget* Widget,
		const FString& WidgetPath,
		const FGeometry& Geometry,
		const FVector2D& DesiredSize,
		const FDerivedClipState& ParentClip)
	{
		FDerivedClipState Result = ParentClip;
		const EWidgetClipping Clipping = Widget->GetClipping();
		const FSlateRect WidgetBounds = Geometry.GetRenderBoundingRect();

		bool bApplyOwnBounds = false;
		bool bIntersectParent = true;
		bool bAlwaysClip = ParentClip.bAlwaysClip;
		switch (Clipping)
		{
			case EWidgetClipping::ClipToBounds:
				bApplyOwnBounds = true;
				break;
			case EWidgetClipping::ClipToBoundsWithoutIntersecting:
				bApplyOwnBounds = true;
				bIntersectParent = ParentClip.bAlwaysClip;
				break;
			case EWidgetClipping::ClipToBoundsAlways:
				bApplyOwnBounds = true;
				bAlwaysClip = true;
				break;
			case EWidgetClipping::OnDemand:
			{
				const FVector2D LocalSize = Geometry.GetLocalSize();
				bApplyOwnBounds = DesiredSize.X > LocalSize.X + 0.05 || DesiredSize.Y > LocalSize.Y + 0.05;
				break;
			}
			case EWidgetClipping::Inherit:
			default:
				break;
		}

		if (bApplyOwnBounds)
		{
			Result.bHasRect = true;
			Result.bAlwaysClip = bAlwaysClip;
			Result.SourcePath = WidgetPath;
			if (ParentClip.bHasRect && bIntersectParent)
			{
				Result.Rect = ParentClip.Rect.IntersectionWith(WidgetBounds);
			}
			else
			{
				Result.Rect = WidgetBounds;
			}
		}
		return Result;
	}

	// Seed lets the caller start from the hosting UUserWidget's clip state, which
	// is not reachable through GetParent() from a widget-tree root.
	static FDerivedClipState ResolveAncestorClipState(
		UWidget* Widget,
		const FDerivedClipState& Seed = FDerivedClipState(),
		const FString& PathPrefix = FString())
	{
		TArray<UWidget*> Ancestors;
		for (UPanelWidget* Parent = Widget ? Widget->GetParent() : nullptr; Parent; Parent = Parent->GetParent())
		{
			Ancestors.Add(Parent);
		}

		FDerivedClipState Result = Seed;
		FString AncestorPath = PathPrefix;
		for (int32 Index = Ancestors.Num() - 1; Index >= 0; --Index)
		{
			UWidget* Ancestor = Ancestors[Index];
			AncestorPath += TEXT("/") + Ancestor->GetName();
			Result = ResolveClipState(
				Ancestor,
				AncestorPath,
				Ancestor->GetCachedGeometry(),
				Ancestor->GetDesiredSize(),
				Result);
		}
		return Result;
	}

	static double ResolveAncestorOpacity(UWidget* Widget, double Seed = 1.0)
	{
		double Result = Seed;
		for (UPanelWidget* Parent = Widget ? Widget->GetParent() : nullptr; Parent; Parent = Parent->GetParent())
		{
			Result *= Parent->GetRenderOpacity();
		}
		return Result;
	}

	static TSharedPtr<FJsonObject> BuildRuntimeNode(
		UWidget* Widget,
		int32 Depth,
		int32 MaxDepth,
		const FString& WidgetPath,
		const FDerivedClipState& ParentClip,
		double ParentEffectiveOpacity,
		FRuntimeScanContext& Ctx)
	{
		if (!Widget) return nullptr;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), Widget->GetName());
		Obj->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
		if (Ctx.bIncludeLayout)
		{
			// Named widgetPath, not path: everywhere else in this category
			// "path" is the asset path, and one name meaning two things inside
			// the same tool is what #798 was filed about.
			Obj->SetStringField(TEXT("widgetPath"), WidgetPath);
		}
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

		// Layout diagnostics are opt-in: the geometry, slot reflection and delta
		// blocks below multiply the size of a get_runtime payload, and only a caller
		// debugging layout needs them.
		FDerivedClipState EffectiveClip = ParentClip;
		double EffectiveOpacity = ParentEffectiveOpacity;
		if (Ctx.bIncludeLayout)
		{
			const bool bHasCachedSlateWidget = Widget->GetCachedWidget().IsValid();
			const FGeometry& Geometry = Widget->GetCachedGeometry();
			const FVector2D DesiredSize = Widget->GetDesiredSize();
			const FVector2D LocalSize = Geometry.GetLocalSize();
			const FVector2D AbsoluteSize = Geometry.GetAbsoluteSize();
			const FSlateRect LayoutRect = Geometry.GetLayoutBoundingRect();
			const FSlateRect RenderRect = Geometry.GetRenderBoundingRect();
			const FVector2D AbsolutePosition(RenderRect.Left, RenderRect.Top);
			const FWidgetTransform& RenderTransform = Widget->GetRenderTransform();
			EffectiveClip = ResolveClipState(Widget, WidgetPath, Geometry, DesiredSize, ParentClip);
			EffectiveOpacity = ParentEffectiveOpacity * Widget->GetRenderOpacity();

			TSharedPtr<FJsonObject> GeometryObj = MakeShared<FJsonObject>();
			GeometryObj->SetBoolField(TEXT("hasCachedSlateWidget"), bHasCachedSlateWidget);
			GeometryObj->SetObjectField(TEXT("desiredSize"), VectorJson(DesiredSize));
			GeometryObj->SetObjectField(TEXT("localSize"), VectorJson(LocalSize));
			GeometryObj->SetObjectField(TEXT("absoluteSize"), VectorJson(AbsoluteSize));
			GeometryObj->SetObjectField(TEXT("absolutePosition"), VectorJson(AbsolutePosition));
			GeometryObj->SetObjectField(TEXT("layoutBoundingRect"), RectJson(LayoutRect));
			GeometryObj->SetObjectField(TEXT("renderBoundingRect"), RectJson(RenderRect));
			GeometryObj->SetNumberField(TEXT("accumulatedLayoutScale"), Geometry.GetAccumulatedLayoutTransform().GetScale());
			Obj->SetObjectField(TEXT("geometry"), GeometryObj);

			TSharedPtr<FJsonObject> TransformObj = MakeShared<FJsonObject>();
			TransformObj->SetObjectField(TEXT("translation"), VectorJson(RenderTransform.Translation));
			TransformObj->SetObjectField(TEXT("scale"), VectorJson(RenderTransform.Scale));
			TransformObj->SetObjectField(TEXT("shear"), VectorJson(RenderTransform.Shear));
			TransformObj->SetNumberField(TEXT("angleDegrees"), RenderTransform.Angle);
			TransformObj->SetObjectField(TEXT("pivot"), VectorJson(Widget->GetRenderTransformPivot()));
			Obj->SetObjectField(TEXT("renderTransform"), TransformObj);

			TSharedPtr<FJsonObject> ClipObj = MakeShared<FJsonObject>();
			ClipObj->SetStringField(TEXT("authoredMode"), ClippingToString(Widget->GetClipping()));
			ClipObj->SetBoolField(TEXT("hasDerivedEffectiveRect"), EffectiveClip.bHasRect);
			ClipObj->SetBoolField(TEXT("alwaysClip"), EffectiveClip.bAlwaysClip);
			if (EffectiveClip.bHasRect)
			{
				ClipObj->SetObjectField(TEXT("derivedEffectiveRect"), RectJson(EffectiveClip.Rect));
				ClipObj->SetStringField(TEXT("sourcePath"), EffectiveClip.SourcePath);
				bool bOverlapping = false;
				const FSlateRect VisibleRect = RenderRect.IntersectionWith(EffectiveClip.Rect, bOverlapping);
				const bool bFullyClipped = !bOverlapping || VisibleRect.IsEmpty();
				const bool bPartiallyClipped = !bFullyClipped && VisibleRect.GetArea() + 0.05f < RenderRect.GetArea();
				ClipObj->SetBoolField(TEXT("fullyClipped"), bFullyClipped);
				ClipObj->SetBoolField(TEXT("partiallyClipped"), bPartiallyClipped);
				ClipObj->SetObjectField(TEXT("visibleRect"), RectJson(VisibleRect));
			}
			else
			{
				ClipObj->SetBoolField(TEXT("fullyClipped"), false);
				ClipObj->SetBoolField(TEXT("partiallyClipped"), false);
			}
			ClipObj->SetStringField(
				TEXT("derivation"),
				TEXT("Computed from UMG clipping modes and cached render bounds; use a native Widget Reflector snapshot for paint-element clip stacks."));
			Obj->SetObjectField(TEXT("clipping"), ClipObj);

			TSharedPtr<FJsonObject> ViewportObj = MakeShared<FJsonObject>();
			ViewportObj->SetBoolField(TEXT("available"), Ctx.ViewportRect.IsSet());
			if (Ctx.ViewportRect.IsSet())
			{
				ViewportObj->SetObjectField(TEXT("rect"), RectJson(Ctx.ViewportRect.GetValue()));
				bool bOverlapsViewport = false;
				const FSlateRect ViewportIntersection =
					RenderRect.IntersectionWith(Ctx.ViewportRect.GetValue(), bOverlapsViewport);
				const bool bOutsideViewport = !bOverlapsViewport || ViewportIntersection.IsEmpty();
				const bool bPartiallyOutsideViewport =
					!bOutsideViewport && ViewportIntersection.GetArea() + 0.05f < RenderRect.GetArea();
				ViewportObj->SetBoolField(TEXT("overlaps"), bOverlapsViewport);
				ViewportObj->SetBoolField(TEXT("fullyOutside"), bOutsideViewport);
				ViewportObj->SetBoolField(TEXT("partiallyOutside"), bPartiallyOutsideViewport);
				ViewportObj->SetObjectField(TEXT("intersectionRect"), RectJson(ViewportIntersection));
			}
			Obj->SetObjectField(TEXT("viewport"), ViewportObj);

			if (UPanelWidget* Parent = Widget->GetParent())
			{
				const FSlateRect ParentRect = Parent->GetCachedGeometry().GetRenderBoundingRect();
				TSharedPtr<FJsonObject> ParentLayout = MakeShared<FJsonObject>();
				ParentLayout->SetStringField(TEXT("name"), Parent->GetName());
				ParentLayout->SetStringField(TEXT("class"), Parent->GetClass()->GetName());
				ParentLayout->SetObjectField(TEXT("renderBoundingRect"), RectJson(ParentRect));
				bool bOverlapsParent = false;
				RenderRect.IntersectionWith(ParentRect, bOverlapsParent);
				ParentLayout->SetBoolField(TEXT("overlapsParentBounds"), bOverlapsParent);
				ParentLayout->SetBoolField(
					TEXT("extendsOutsideParentBounds"),
					RenderRect.Left < ParentRect.Left - 0.05f ||
					RenderRect.Top < ParentRect.Top - 0.05f ||
					RenderRect.Right > ParentRect.Right + 0.05f ||
					RenderRect.Bottom > ParentRect.Bottom + 0.05f);
				Obj->SetObjectField(TEXT("parentLayout"), ParentLayout);
			}

			FString SlotSignature;
			if (TSharedPtr<FJsonObject> SlotObj = BuildSlotJson(Widget, SlotSignature))
			{
				Obj->SetObjectField(TEXT("slot"), SlotObj);
			}

			FRuntimeLayoutSample Sample;
			Sample.DesiredSize = DesiredSize;
			Sample.LocalSize = LocalSize;
			Sample.AbsoluteSize = AbsoluteSize;
			Sample.AbsolutePosition = AbsolutePosition;
			Sample.RenderRect = RenderRect;
			Sample.SlotSignature = SlotSignature;
			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Widget->Slot))
			{
				Sample.bHasCanvasSlot = true;
				Sample.bCanvasAutoSize = CanvasSlot->GetAutoSize();
				Sample.CanvasAnchors = CanvasSlot->GetAnchors();
				Sample.CanvasOffsets = CanvasSlot->GetOffsets();
			}
			Ctx.CurrentSamples.Add(WidgetPath, Sample);

			TArray<TSharedPtr<FJsonValue>> Warnings;
			if (!bHasCachedSlateWidget)
			{
				AddWarning(
					Warnings,
					TEXT("geometry_unavailable"),
					TEXT("warning"),
					TEXT("The Slate widget has not been constructed or painted, so cached geometry may be empty or stale."));
			}
			if (DesiredSize.X > LocalSize.X + 0.5 || DesiredSize.Y > LocalSize.Y + 0.5)
			{
				AddWarning(
					Warnings,
					TEXT("desired_size_exceeds_allocation"),
					TEXT("info"),
					FString::Printf(
						TEXT("Desired size %.2fx%.2f exceeds allocated local size %.2fx%.2f; clipping or compression may occur."),
						DesiredSize.X,
						DesiredSize.Y,
						LocalSize.X,
						LocalSize.Y));
			}
			if (Sample.bHasCanvasSlot)
			{
				const bool bStretchX = !FMath::IsNearlyEqual(Sample.CanvasAnchors.Minimum.X, Sample.CanvasAnchors.Maximum.X);
				const bool bStretchY = !FMath::IsNearlyEqual(Sample.CanvasAnchors.Minimum.Y, Sample.CanvasAnchors.Maximum.Y);
				if (!Sample.bCanvasAutoSize && bStretchX && !FMath::IsNearlyZero(Sample.CanvasOffsets.Right))
				{
					AddWarning(
						Warnings,
						TEXT("stretched_canvas_right_is_margin"),
						TEXT("info"),
						TEXT("This Canvas slot is horizontally stretched: Offsets.Right is a right margin, not a width."));
				}
				if (!Sample.bCanvasAutoSize && bStretchY && !FMath::IsNearlyZero(Sample.CanvasOffsets.Bottom))
				{
					AddWarning(
						Warnings,
						TEXT("stretched_canvas_bottom_is_margin"),
						TEXT("warning"),
						TEXT("This Canvas slot is vertically stretched: Offsets.Bottom is a bottom margin, not a height. SetSize can therefore make height position-dependent."));
				}
			}

			TSharedPtr<FJsonObject> DeltaObj = MakeShared<FJsonObject>();
			bool bChanged = false;
			if (Ctx.PreviousSamples)
			{
				if (const FRuntimeLayoutSample* Previous = Ctx.PreviousSamples->Find(WidgetPath))
				{
					const FVector2D PositionDelta = Sample.AbsolutePosition - Previous->AbsolutePosition;
					const FVector2D LocalSizeDelta = Sample.LocalSize - Previous->LocalSize;
					const FVector2D AbsoluteSizeDelta = Sample.AbsoluteSize - Previous->AbsoluteSize;
					const FVector2D DesiredSizeDelta = Sample.DesiredSize - Previous->DesiredSize;
					const bool bSlotChanged = Sample.SlotSignature != Previous->SlotSignature;
					bChanged =
						!VectorNearlyEqual(PositionDelta, FVector2D::ZeroVector) ||
						!VectorNearlyEqual(LocalSizeDelta, FVector2D::ZeroVector) ||
						!VectorNearlyEqual(AbsoluteSizeDelta, FVector2D::ZeroVector) ||
						!VectorNearlyEqual(DesiredSizeDelta, FVector2D::ZeroVector) ||
						bSlotChanged;
					DeltaObj->SetBoolField(TEXT("hasPreviousCapture"), true);
					DeltaObj->SetBoolField(TEXT("changed"), bChanged);
					DeltaObj->SetObjectField(TEXT("absolutePositionDelta"), VectorJson(PositionDelta));
					DeltaObj->SetObjectField(TEXT("localSizeDelta"), VectorJson(LocalSizeDelta));
					DeltaObj->SetObjectField(TEXT("absoluteSizeDelta"), VectorJson(AbsoluteSizeDelta));
					DeltaObj->SetObjectField(TEXT("desiredSizeDelta"), VectorJson(DesiredSizeDelta));
					DeltaObj->SetBoolField(TEXT("slotPropertiesChanged"), bSlotChanged);
					if (bChanged)
					{
						++Ctx.ChangedNodeCount;
					}

					if (Sample.bHasCanvasSlot && Previous->bHasCanvasSlot)
					{
						const bool bStretchY =
							!FMath::IsNearlyEqual(Sample.CanvasAnchors.Minimum.Y, Sample.CanvasAnchors.Maximum.Y);
						const bool bMovedVertically = !FMath::IsNearlyZero(PositionDelta.Y, 0.25);
						const bool bHeightChanged = !FMath::IsNearlyZero(LocalSizeDelta.Y, 0.25);
						const bool bInverseMovement =
							FMath::IsNearlyEqual(LocalSizeDelta.Y, -PositionDelta.Y, 1.0);
						if (bStretchY && !Sample.bCanvasAutoSize && bMovedVertically && bHeightChanged && bInverseMovement)
						{
							AddWarning(
								Warnings,
								TEXT("position_dependent_canvas_height"),
								TEXT("error"),
								FString::Printf(
									TEXT("Moving the widget by %.2f px changed its height by %.2f px in the opposite direction. A vertically stretched Canvas slot is treating Bottom as a margin."),
									PositionDelta.Y,
									LocalSizeDelta.Y));
						}
					}
				}
				else
				{
					DeltaObj->SetBoolField(TEXT("hasPreviousCapture"), false);
					DeltaObj->SetBoolField(TEXT("changed"), false);
					DeltaObj->SetStringField(TEXT("reason"), TEXT("Widget path was not present in the previous capture."));
				}
			}
			else
			{
				DeltaObj->SetBoolField(TEXT("hasPreviousCapture"), false);
				DeltaObj->SetBoolField(TEXT("changed"), false);
				DeltaObj->SetStringField(TEXT("reason"), TEXT("This is the baseline capture for the runtime widget instance."));
			}
			Obj->SetObjectField(TEXT("deltaSincePreviousCapture"), DeltaObj);
			Obj->SetNumberField(TEXT("effectiveRenderOpacity"), EffectiveOpacity);
			Obj->SetArrayField(TEXT("diagnostics"), Warnings);
			Ctx.WarningCount += Warnings.Num();
		}

		if (Depth >= MaxDepth) return Obj;

		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			TArray<TSharedPtr<FJsonValue>> ChildrenArr;
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				UWidget* Child = Panel->GetChildAt(i);
				const FString ChildPath = WidgetPath + TEXT("/") + (Child ? Child->GetName() : FString::Printf(TEXT("child_%d"), i));
				TSharedPtr<FJsonObject> ChildObj = BuildRuntimeNode(
					Child,
					Depth + 1,
					MaxDepth,
					ChildPath,
					EffectiveClip,
					EffectiveOpacity,
					Ctx);
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
				UWidget* RootWidget = User->WidgetTree->RootWidget;
				const FString RootPath = WidgetPath + TEXT("/root:") + RootWidget->GetName();
				TSharedPtr<FJsonObject> RootObj = BuildRuntimeNode(
					RootWidget,
					Depth + 1,
					MaxDepth,
					RootPath,
					EffectiveClip,
					EffectiveOpacity,
					Ctx);
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
	const bool bIncludeLayout = OptionalBool(Params, TEXT("includeLayout"), false);

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

	// `tree` stays rooted at the widget-tree root (or the named child) exactly as
	// before, so existing consumers keep indexing the same node and maxDepth keeps
	// counting from the same place. The hosting UUserWidget is reported separately
	// under `host` when layout diagnostics are requested.
	UWidget* ScanRoot = nullptr;
	if (!ChildName.IsEmpty())
	{
		if (!Found->WidgetTree)
		{
			return MCPError(FString::Printf(
				TEXT("Runtime widget '%s' has no UMG WidgetTree, so childName cannot be resolved."),
				*Found->GetName()));
		}

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
	else if (Found->WidgetTree)
	{
		ScanRoot = Found->WidgetTree->RootWidget;
	}

	FRuntimeScanContext Ctx;
	Ctx.bIncludeLayout = bIncludeLayout;

	FString CaptureKey;
	TOptional<uint64> PreviousFrame;
	FDerivedClipState HostClip;
	double HostOpacity = 1.0;
	if (bIncludeLayout)
	{
		Result->SetNumberField(TEXT("instanceId"), Found->GetUniqueID());

		CaptureKey =
			World->GetName() + TEXT("|") + Found->GetPathName() + TEXT("|") +
			FString::FromInt(Found->GetUniqueID()) + TEXT("|") +
			(ChildName.IsEmpty() ? TEXT("<root>") : ChildName);
		Ctx.PreviousSamples = PreviousLayoutCaptures.Find(CaptureKey);
		if (const uint64* Frame = PreviousLayoutCaptureFrames.Find(CaptureKey))
		{
			PreviousFrame = *Frame;
		}

		if (UGameViewportClient* ViewportClient = World->GetGameViewport())
		{
			if (TSharedPtr<SViewport> ViewportWidget = ViewportClient->GetGameViewportWidget())
			{
				Ctx.ViewportRect = ViewportWidget->GetCachedGeometry().GetRenderBoundingRect();
			}
		}

		// The host UUserWidget is not a UPanelWidget parent, so its geometry,
		// clipping and opacity are unreachable from the tree root by GetParent().
		// Capture it once and seed the tree walk with it. Passing MaxDepth as the
		// starting depth stops the walk after this node, so the subtree is not
		// duplicated under `host`.
		TSharedPtr<FJsonObject> HostNode = BuildRuntimeNode(
			Found,
			MaxDepth,
			MaxDepth,
			Found->GetName(),
			ResolveAncestorClipState(Found),
			ResolveAncestorOpacity(Found),
			Ctx);
		if (HostNode.IsValid())
		{
			Result->SetObjectField(TEXT("host"), HostNode);
		}
		HostClip = ResolveClipState(
			Found,
			Found->GetName(),
			Found->GetCachedGeometry(),
			Found->GetDesiredSize(),
			ResolveAncestorClipState(Found));
		HostOpacity = ResolveAncestorOpacity(Found) * Found->GetRenderOpacity();
	}

	if (ScanRoot)
	{
		const FString ScanPath = Found->GetName() + TEXT("/") + ScanRoot->GetName();
		TSharedPtr<FJsonObject> Tree = BuildRuntimeNode(
			ScanRoot,
			0,
			MaxDepth,
			ScanPath,
			ResolveAncestorClipState(ScanRoot, HostClip, Found->GetName()),
			ResolveAncestorOpacity(ScanRoot, HostOpacity),
			Ctx);
		if (Tree.IsValid())
		{
			Result->SetObjectField(TEXT("tree"), Tree);
		}
	}
	else
	{
		Result->SetStringField(TEXT("tree"), TEXT("empty"));
	}

	if (bIncludeLayout)
	{
		TSharedPtr<FJsonObject> Capture = MakeShared<FJsonObject>();
		Capture->SetNumberField(TEXT("sequence"), static_cast<double>(++LayoutCaptureSequence));
		Capture->SetNumberField(TEXT("frame"), static_cast<double>(GFrameCounter));
		Capture->SetNumberField(TEXT("timeSeconds"), FApp::GetCurrentTime());
		Capture->SetBoolField(TEXT("isBaseline"), Ctx.PreviousSamples == nullptr);
		Capture->SetNumberField(TEXT("nodeCount"), Ctx.CurrentSamples.Num());
		Capture->SetNumberField(TEXT("changedNodeCount"), Ctx.ChangedNodeCount);
		Capture->SetNumberField(TEXT("diagnosticCount"), Ctx.WarningCount);
		Capture->SetBoolField(TEXT("hasViewportGeometry"), Ctx.ViewportRect.IsSet());
		if (Ctx.ViewportRect.IsSet())
		{
			Capture->SetObjectField(TEXT("viewportRect"), RectJson(Ctx.ViewportRect.GetValue()));
		}
		if (PreviousFrame.IsSet())
		{
			Capture->SetNumberField(TEXT("previousFrame"), static_cast<double>(PreviousFrame.GetValue()));
		}
		Capture->SetStringField(
			TEXT("usage"),
			TEXT("Call widget.get_runtime again with includeLayout after moving, resizing, toggling, or changing resolution to populate deltaSincePreviousCapture."));
		Result->SetObjectField(TEXT("layoutCapture"), Capture);

		PreviousLayoutCaptures.Add(CaptureKey, MoveTemp(Ctx.CurrentSamples));
		PreviousLayoutCaptureFrames.Add(CaptureKey, GFrameCounter);
		if (PreviousLayoutCaptures.Num() > 64)
		{
			PreviousLayoutCaptures.Reset();
			PreviousLayoutCaptureFrames.Reset();
		}
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
// #559  Fire a UFUNCTION or a child-widget interaction on a live PIE UUserWidget.
//   Params: widgetName|className (locate the UserWidget), functionName
//   (a parameterless UFUNCTION on the widget), OR childName (+ optional value,
//   functionName, commitMethod) to drive an interactive child widget (#812).
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

	// Child-interaction path: childName names an interactive child widget. The
	// simulation lives in WidgetHandlers_Interaction.cpp and covers buttons,
	// checkboxes, sliders, spin boxes, text entry and combo boxes (#812).
	// functionName, when given here, selects which of the child's delegates to
	// fire rather than naming a UFUNCTION on the parent.
	if (!ChildName.IsEmpty())
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

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("widget"), Found->GetName());
		Result->SetStringField(TEXT("child"), ChildName);
		if (TSharedPtr<FJsonValue> Err = SimulateRuntimeChildInteraction(Target, Params, Result))
		{
			return Err;
		}
		return MCPResult(Result);
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
// #161  Runtime delegate inspection - list FMulticastDelegateProperty fields on a live UUserWidget
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
