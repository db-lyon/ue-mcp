#include "WidgetHandlers.h"
#include "HandlerRegistry.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "UObject/SavePackage.h"
#include "EditorScriptingUtilities/Public/EditorAssetLibrary.h"
#include "WidgetBlueprint.h"
// WidgetBlueprintFactory.h - use Factories module instead
#include "Factories/Factory.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

void FWidgetHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_widget_blueprints"), &ListWidgetBlueprints);
	Registry.RegisterHandler(TEXT("create_widget_blueprint"), &CreateWidgetBlueprint);
	Registry.RegisterHandler(TEXT("read_widget_tree"), &ReadWidgetTree);
	Registry.RegisterHandler(TEXT("create_editor_utility_widget"), &CreateEditorUtilityWidget);
	Registry.RegisterHandler(TEXT("create_editor_utility_blueprint"), &CreateEditorUtilityBlueprint);
}

TSharedPtr<FJsonValue> FWidgetHandlers::ListWidgetBlueprints(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	bool bRecursive = true;
	Params->TryGetBoolField(TEXT("recursive"), bRecursive);

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

	Result->SetArrayField(TEXT("assets"), AssetsArray);
	Result->SetNumberField(TEXT("count"), AssetsArray.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::CreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Name;
	if (!Params->TryGetStringField(TEXT("name"), Name))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'name' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PackagePath = TEXT("/Game/UI/Widgets");
	Params->TryGetStringField(TEXT("packagePath"), PackagePath);

	// Delete existing asset if it exists
	FString FullAssetPath = PackagePath + TEXT("/") + Name;
	UEditorAssetLibrary::DeleteAsset(FullAssetPath);

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UWidgetBlueprintFactory* WidgetFactory = NewObject<UWidgetBlueprintFactory>();
	WidgetFactory->ParentClass = UUserWidget::StaticClass();

	UObject* NewAsset = AssetTools.CreateAsset(Name, PackagePath, UWidgetBlueprint::StaticClass(), WidgetFactory);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create WidgetBlueprint"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), Name);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::ReadWidgetTree(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load WidgetBlueprint at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

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

	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::CreateEditorUtilityWidget(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Path;
	if (!Params->TryGetStringField(TEXT("path"), Path))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Split path into package path and asset name
	FString PackagePath;
	FString AssetName;
	Path.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (AssetName.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Invalid path format. Expected '/Game/.../AssetName'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find EditorUtilityWidgetBlueprint class
	UClass* EUWBClass = FindObject<UClass>(nullptr, TEXT("/Script/Blutility.EditorUtilityWidgetBlueprint"));
	if (!EUWBClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("EditorUtilityWidgetBlueprint class not found. Enable Blutility plugin."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Delete existing asset if it exists
	UEditorAssetLibrary::DeleteAsset(Path);

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UWidgetBlueprintFactory* WidgetFactory = NewObject<UWidgetBlueprintFactory>();
	WidgetFactory->ParentClass = UUserWidget::StaticClass();
	WidgetFactory->BlueprintType = BPTYPE_Normal;

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, EUWBClass, WidgetFactory);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create EditorUtilityWidgetBlueprint"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), AssetName);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::CreateEditorUtilityBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString Path;
	if (!Params->TryGetStringField(TEXT("path"), Path))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Split path into package path and asset name
	FString PackagePath;
	FString AssetName;
	Path.Split(TEXT("/"), &PackagePath, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (AssetName.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Invalid path format. Expected '/Game/.../AssetName'"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Find EditorUtilityBlueprint class
	UClass* EUBClass = FindObject<UClass>(nullptr, TEXT("/Script/Blutility.EditorUtilityBlueprint"));
	if (!EUBClass)
	{
		Result->SetStringField(TEXT("error"), TEXT("EditorUtilityBlueprint class not found. Enable Blutility plugin."));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Delete existing asset if it exists
	UEditorAssetLibrary::DeleteAsset(Path);

	FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
	IAssetTools& AssetTools = AssetToolsModule.Get();

	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, EUBClass, nullptr);
	if (!NewAsset)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to create EditorUtilityBlueprint"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorAssetLibrary::SaveAsset(NewAsset->GetPathName());

	Result->SetStringField(TEXT("path"), NewAsset->GetPathName());
	Result->SetStringField(TEXT("name"), AssetName);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}
