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

void FWidgetHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("list_widget_blueprints"), &ListWidgetBlueprints);
	Registry.RegisterHandler(TEXT("create_widget_blueprint"), &CreateWidgetBlueprint);
	Registry.RegisterHandler(TEXT("read_widget_tree"), &ReadWidgetTree);
	Registry.RegisterHandler(TEXT("create_editor_utility_widget"), &CreateEditorUtilityWidget);
	Registry.RegisterHandler(TEXT("create_editor_utility_blueprint"), &CreateEditorUtilityBlueprint);
	Registry.RegisterHandler(TEXT("search_widget_by_name"), &SearchWidgetByName);
	Registry.RegisterHandler(TEXT("get_widget_properties"), &GetWidgetProperties);
	Registry.RegisterHandler(TEXT("get_widget_details"), &GetWidgetProperties);
	Registry.RegisterHandler(TEXT("set_widget_property"), &SetWidgetProperty);
	Registry.RegisterHandler(TEXT("read_widget_animations"), &ReadWidgetAnimations);
	Registry.RegisterHandler(TEXT("run_editor_utility_widget"), &RunEditorUtilityWidget);
	Registry.RegisterHandler(TEXT("run_editor_utility_blueprint"), &RunEditorUtilityBlueprint);
	Registry.RegisterHandler(TEXT("add_widget"), &AddWidget);
	Registry.RegisterHandler(TEXT("remove_widget"), &RemoveWidget);
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
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
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
	if (!Params->TryGetStringField(TEXT("path"), Path) && !Params->TryGetStringField(TEXT("assetPath"), Path))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
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
	if (!Params->TryGetStringField(TEXT("path"), Path) && !Params->TryGetStringField(TEXT("assetPath"), Path))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
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

TSharedPtr<FJsonValue> FWidgetHandlers::SearchWidgetByName(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widgetName"), WidgetName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'widgetName' parameter"));
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

	if (!WidgetBP->WidgetTree)
	{
		Result->SetStringField(TEXT("error"), TEXT("WidgetTree is null"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// Search recursively from root
	UWidget* RootWidget = WidgetBP->WidgetTree->RootWidget;
	UWidget* FoundWidget = FindWidgetByNameRecursive(RootWidget, WidgetName);

	// Also search all widgets in the tree (handles named widgets not in visual tree)
	if (!FoundWidget)
	{
		WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
		{
			if (Widget && Widget->GetName() == WidgetName)
			{
				FoundWidget = Widget;
			}
		});
	}

	if (!FoundWidget)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget not found: '%s'"), *WidgetName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	TSharedPtr<FJsonObject> WidgetObj = MakeShared<FJsonObject>();
	WidgetObj->SetStringField(TEXT("name"), FoundWidget->GetName());
	WidgetObj->SetStringField(TEXT("class"), FoundWidget->GetClass()->GetName());
	WidgetObj->SetBoolField(TEXT("isVisible"), FoundWidget->IsVisible());

	// Check if it has a parent
	UPanelWidget* Parent = FoundWidget->GetParent();
	if (Parent)
	{
		WidgetObj->SetStringField(TEXT("parent"), Parent->GetName());
		WidgetObj->SetStringField(TEXT("parentClass"), Parent->GetClass()->GetName());
	}

	// Check if it's a panel and report child count
	UPanelWidget* AsPanel = Cast<UPanelWidget>(FoundWidget);
	if (AsPanel)
	{
		WidgetObj->SetNumberField(TEXT("childCount"), AsPanel->GetChildrenCount());
	}

	Result->SetObjectField(TEXT("widget"), WidgetObj);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::GetWidgetProperties(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widgetName"), WidgetName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'widgetName' parameter"));
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

	if (!WidgetBP->WidgetTree)
	{
		Result->SetStringField(TEXT("error"), TEXT("WidgetTree is null"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget not found: '%s'"), *WidgetName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	TSharedPtr<FJsonObject> PropsObj = MakeShared<FJsonObject>();
	PropsObj->SetStringField(TEXT("name"), FoundWidget->GetName());
	PropsObj->SetStringField(TEXT("class"), FoundWidget->GetClass()->GetName());
	PropsObj->SetBoolField(TEXT("isVisible"), FoundWidget->IsVisible());

	// Type-specific properties
	if (UTextBlock* TextBlock = Cast<UTextBlock>(FoundWidget))
	{
		PropsObj->SetStringField(TEXT("text"), TextBlock->GetText().ToString());
		PropsObj->SetStringField(TEXT("widgetType"), TEXT("TextBlock"));

		// Font info
		FSlateFontInfo FontInfo = TextBlock->GetFont();
		PropsObj->SetStringField(TEXT("fontFamily"), FontInfo.FontObject ? FontInfo.FontObject->GetName() : TEXT(""));
		PropsObj->SetNumberField(TEXT("fontSize"), FontInfo.Size);

		// Color
		FLinearColor Color = TextBlock->GetColorAndOpacity().GetSpecifiedColor();
		TSharedPtr<FJsonObject> ColorObj = MakeShared<FJsonObject>();
		ColorObj->SetNumberField(TEXT("r"), Color.R);
		ColorObj->SetNumberField(TEXT("g"), Color.G);
		ColorObj->SetNumberField(TEXT("b"), Color.B);
		ColorObj->SetNumberField(TEXT("a"), Color.A);
		PropsObj->SetObjectField(TEXT("color"), ColorObj);
	}
	else if (UImage* Image = Cast<UImage>(FoundWidget))
	{
		PropsObj->SetStringField(TEXT("widgetType"), TEXT("Image"));

		// Brush info
		const FSlateBrush& Brush = Image->GetBrush();
		TSharedPtr<FJsonObject> BrushObj = MakeShared<FJsonObject>();
		BrushObj->SetStringField(TEXT("resourceName"), Brush.GetResourceName().ToString());
		BrushObj->SetNumberField(TEXT("imageSizeX"), Brush.ImageSize.X);
		BrushObj->SetNumberField(TEXT("imageSizeY"), Brush.ImageSize.Y);
		BrushObj->SetStringField(TEXT("drawAs"), StaticEnum<ESlateBrushDrawType::Type>()->GetNameStringByValue((int64)Brush.DrawAs));
		BrushObj->SetStringField(TEXT("tiling"), StaticEnum<ESlateBrushTileType::Type>()->GetNameStringByValue((int64)Brush.Tiling));
		PropsObj->SetObjectField(TEXT("brush"), BrushObj);

		// Color tint
		FLinearColor Tint = Image->GetColorAndOpacity();
		TSharedPtr<FJsonObject> TintObj = MakeShared<FJsonObject>();
		TintObj->SetNumberField(TEXT("r"), Tint.R);
		TintObj->SetNumberField(TEXT("g"), Tint.G);
		TintObj->SetNumberField(TEXT("b"), Tint.B);
		TintObj->SetNumberField(TEXT("a"), Tint.A);
		PropsObj->SetObjectField(TEXT("colorAndOpacity"), TintObj);
	}
	else if (UButton* Button = Cast<UButton>(FoundWidget))
	{
		PropsObj->SetStringField(TEXT("widgetType"), TEXT("Button"));

		// Button style
		const FButtonStyle& Style = Button->GetStyle();
		TSharedPtr<FJsonObject> StyleObj = MakeShared<FJsonObject>();

		// Normal brush
		StyleObj->SetStringField(TEXT("normalResourceName"), Style.Normal.GetResourceName().ToString());
		StyleObj->SetStringField(TEXT("hoveredResourceName"), Style.Hovered.GetResourceName().ToString());
		StyleObj->SetStringField(TEXT("pressedResourceName"), Style.Pressed.GetResourceName().ToString());

		PropsObj->SetObjectField(TEXT("style"), StyleObj);

		// Color
		FLinearColor BtnColor = Button->GetColorAndOpacity();
		TSharedPtr<FJsonObject> BtnColorObj = MakeShared<FJsonObject>();
		BtnColorObj->SetNumberField(TEXT("r"), BtnColor.R);
		BtnColorObj->SetNumberField(TEXT("g"), BtnColor.G);
		BtnColorObj->SetNumberField(TEXT("b"), BtnColor.B);
		BtnColorObj->SetNumberField(TEXT("a"), BtnColor.A);
		PropsObj->SetObjectField(TEXT("colorAndOpacity"), BtnColorObj);
	}
	else if (UProgressBar* ProgressBar = Cast<UProgressBar>(FoundWidget))
	{
		PropsObj->SetStringField(TEXT("widgetType"), TEXT("ProgressBar"));
		PropsObj->SetNumberField(TEXT("percent"), ProgressBar->GetPercent());

		// Fill color
		FLinearColor FillColor = ProgressBar->GetFillColorAndOpacity();
		TSharedPtr<FJsonObject> FillObj = MakeShared<FJsonObject>();
		FillObj->SetNumberField(TEXT("r"), FillColor.R);
		FillObj->SetNumberField(TEXT("g"), FillColor.G);
		FillObj->SetNumberField(TEXT("b"), FillColor.B);
		FillObj->SetNumberField(TEXT("a"), FillColor.A);
		PropsObj->SetObjectField(TEXT("fillColor"), FillObj);
	}
	else if (UCheckBox* CheckBox = Cast<UCheckBox>(FoundWidget))
	{
		PropsObj->SetStringField(TEXT("widgetType"), TEXT("CheckBox"));
		PropsObj->SetBoolField(TEXT("isChecked"), CheckBox->IsChecked());
	}
	else if (USlider* Slider = Cast<USlider>(FoundWidget))
	{
		PropsObj->SetStringField(TEXT("widgetType"), TEXT("Slider"));
		PropsObj->SetNumberField(TEXT("value"), Slider->GetValue());
		PropsObj->SetNumberField(TEXT("minValue"), Slider->GetMinValue());
		PropsObj->SetNumberField(TEXT("maxValue"), Slider->GetMaxValue());
	}
	else if (UEditableTextBox* EditableText = Cast<UEditableTextBox>(FoundWidget))
	{
		PropsObj->SetStringField(TEXT("widgetType"), TEXT("EditableTextBox"));
		PropsObj->SetStringField(TEXT("text"), EditableText->GetText().ToString());
		PropsObj->SetStringField(TEXT("hintText"), EditableText->GetHintText().ToString());
	}
	else if (UComboBoxString* ComboBox = Cast<UComboBoxString>(FoundWidget))
	{
		PropsObj->SetStringField(TEXT("widgetType"), TEXT("ComboBoxString"));
		PropsObj->SetStringField(TEXT("selectedOption"), ComboBox->GetSelectedOption());
		PropsObj->SetNumberField(TEXT("optionCount"), ComboBox->GetOptionCount());

		TArray<TSharedPtr<FJsonValue>> OptionsArray;
		for (int32 i = 0; i < ComboBox->GetOptionCount(); ++i)
		{
			OptionsArray.Add(MakeShared<FJsonValueString>(ComboBox->GetOptionAtIndex(i)));
		}
		PropsObj->SetArrayField(TEXT("options"), OptionsArray);
	}
	else
	{
		PropsObj->SetStringField(TEXT("widgetType"), FoundWidget->GetClass()->GetName());
	}

	// Common slot info via reflection
	UPanelWidget* ParentWidget = FoundWidget->GetParent();
	if (ParentWidget)
	{
		PropsObj->SetStringField(TEXT("parentName"), ParentWidget->GetName());
		PropsObj->SetStringField(TEXT("parentClass"), ParentWidget->GetClass()->GetName());
	}

	Result->SetObjectField(TEXT("properties"), PropsObj);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::SetWidgetProperty(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widgetName"), WidgetName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'widgetName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PropertyName;
	if (!Params->TryGetStringField(TEXT("propertyName"), PropertyName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'propertyName' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString PropertyValue;
	if (!Params->TryGetStringField(TEXT("propertyValue"), PropertyValue) && !Params->TryGetStringField(TEXT("value"), PropertyValue))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'propertyValue' or 'value' parameter"));
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

	if (!WidgetBP->WidgetTree)
	{
		Result->SetStringField(TEXT("error"), TEXT("WidgetTree is null"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget not found: '%s'"), *WidgetName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	bool bPropertySet = false;

	// Handle well-known properties by type
	if (UTextBlock* TextBlock = Cast<UTextBlock>(FoundWidget))
	{
		if (PropertyName == TEXT("text") || PropertyName == TEXT("Text"))
		{
			TextBlock->SetText(FText::FromString(PropertyValue));
			bPropertySet = true;
		}
		else if (PropertyName == TEXT("fontSize"))
		{
			FSlateFontInfo FontInfo = TextBlock->GetFont();
			FontInfo.Size = FCString::Atoi(*PropertyValue);
			TextBlock->SetFont(FontInfo);
			bPropertySet = true;
		}
	}
	else if (UImage* Image = Cast<UImage>(FoundWidget))
	{
		if (PropertyName == TEXT("colorAndOpacity") || PropertyName == TEXT("tint"))
		{
			// Expect "R,G,B,A" format
			TArray<FString> Components;
			PropertyValue.ParseIntoArray(Components, TEXT(","));
			if (Components.Num() >= 3)
			{
				float R = FCString::Atof(*Components[0]);
				float G = FCString::Atof(*Components[1]);
				float B = FCString::Atof(*Components[2]);
				float A = Components.Num() >= 4 ? FCString::Atof(*Components[3]) : 1.0f;
				Image->SetColorAndOpacity(FLinearColor(R, G, B, A));
				bPropertySet = true;
			}
		}
	}
	else if (UProgressBar* ProgressBar = Cast<UProgressBar>(FoundWidget))
	{
		if (PropertyName == TEXT("percent") || PropertyName == TEXT("Percent"))
		{
			ProgressBar->SetPercent(FCString::Atof(*PropertyValue));
			bPropertySet = true;
		}
		else if (PropertyName == TEXT("fillColor") || PropertyName == TEXT("FillColorAndOpacity"))
		{
			TArray<FString> Components;
			PropertyValue.ParseIntoArray(Components, TEXT(","));
			if (Components.Num() >= 3)
			{
				float R = FCString::Atof(*Components[0]);
				float G = FCString::Atof(*Components[1]);
				float B = FCString::Atof(*Components[2]);
				float A = Components.Num() >= 4 ? FCString::Atof(*Components[3]) : 1.0f;
				ProgressBar->SetFillColorAndOpacity(FLinearColor(R, G, B, A));
				bPropertySet = true;
			}
		}
	}
	else if (UCheckBox* CheckBox = Cast<UCheckBox>(FoundWidget))
	{
		if (PropertyName == TEXT("isChecked") || PropertyName == TEXT("IsChecked"))
		{
			bool bChecked = PropertyValue.ToBool();
			CheckBox->SetIsChecked(bChecked);
			bPropertySet = true;
		}
	}
	else if (USlider* Slider = Cast<USlider>(FoundWidget))
	{
		if (PropertyName == TEXT("value") || PropertyName == TEXT("Value"))
		{
			Slider->SetValue(FCString::Atof(*PropertyValue));
			bPropertySet = true;
		}
	}
	else if (UEditableTextBox* EditableText = Cast<UEditableTextBox>(FoundWidget))
	{
		if (PropertyName == TEXT("text") || PropertyName == TEXT("Text"))
		{
			EditableText->SetText(FText::FromString(PropertyValue));
			bPropertySet = true;
		}
	}

	// ── Slot properties (slot.anchors, slot.alignment, slot.position, slot.autoSize, slot.*) ──
	if (!bPropertySet && PropertyName.StartsWith(TEXT("slot.")))
	{
		UPanelSlot* Slot = FoundWidget->Slot;
		if (Slot)
		{
			FString SlotPropName = PropertyName.Mid(5); // strip "slot."

			// Well-known CanvasPanelSlot properties
			UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot);
			if (CanvasSlot)
			{
				if (SlotPropName == TEXT("anchors") || SlotPropName == TEXT("Anchors"))
				{
					// Format: "minX,minY,maxX,maxY"  e.g. "0.5,0.5,0.5,0.5" for center
					TArray<FString> Parts;
					PropertyValue.ParseIntoArray(Parts, TEXT(","));
					if (Parts.Num() >= 2)
					{
						FAnchors Anchors;
						Anchors.Minimum = FVector2D(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]));
						Anchors.Maximum = Parts.Num() >= 4
							? FVector2D(FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3]))
							: Anchors.Minimum;
						CanvasSlot->SetAnchors(Anchors);
						bPropertySet = true;
					}
				}
				else if (SlotPropName == TEXT("alignment") || SlotPropName == TEXT("Alignment"))
				{
					// Format: "x,y"  e.g. "0.5,0.5"
					TArray<FString> Parts;
					PropertyValue.ParseIntoArray(Parts, TEXT(","));
					if (Parts.Num() >= 2)
					{
						CanvasSlot->SetAlignment(FVector2D(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1])));
						bPropertySet = true;
					}
				}
				else if (SlotPropName == TEXT("position") || SlotPropName == TEXT("Position"))
				{
					// Format: "x,y"
					TArray<FString> Parts;
					PropertyValue.ParseIntoArray(Parts, TEXT(","));
					if (Parts.Num() >= 2)
					{
						CanvasSlot->SetPosition(FVector2D(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1])));
						bPropertySet = true;
					}
				}
				else if (SlotPropName == TEXT("size") || SlotPropName == TEXT("Size"))
				{
					// Format: "x,y"
					TArray<FString> Parts;
					PropertyValue.ParseIntoArray(Parts, TEXT(","));
					if (Parts.Num() >= 2)
					{
						CanvasSlot->SetSize(FVector2D(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1])));
						bPropertySet = true;
					}
				}
				else if (SlotPropName == TEXT("autoSize") || SlotPropName == TEXT("AutoSize"))
				{
					CanvasSlot->SetAutoSize(PropertyValue.ToBool());
					bPropertySet = true;
				}
				else if (SlotPropName == TEXT("zOrder") || SlotPropName == TEXT("ZOrder"))
				{
					CanvasSlot->SetZOrder(FCString::Atoi(*PropertyValue));
					bPropertySet = true;
				}
			}

			// Generic slot reflection fallback
			if (!bPropertySet)
			{
				FProperty* SlotProp = Slot->GetClass()->FindPropertyByName(FName(*SlotPropName));
				if (SlotProp)
				{
					void* SlotValuePtr = SlotProp->ContainerPtrToValuePtr<void>(Slot);
					if (SlotProp->ImportText_Direct(*PropertyValue, SlotValuePtr, Slot, PPF_None))
					{
						bPropertySet = true;
					}
				}
			}
		}
	}

	// Fallback: try to set via UObject reflection
	if (!bPropertySet)
	{
		FProperty* Prop = FoundWidget->GetClass()->FindPropertyByName(FName(*PropertyName));
		if (Prop)
		{
			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(FoundWidget);
			if (Prop->ImportText_Direct(*PropertyValue, ValuePtr, FoundWidget, PPF_None))
			{
				bPropertySet = true;
			}
		}
	}

	if (bPropertySet)
	{
		// Mark package dirty and save
		WidgetBP->MarkPackageDirty();
		FKismetEditorUtilities::CompileBlueprint(WidgetBP);
		UEditorAssetLibrary::SaveAsset(AssetPath);

		Result->SetStringField(TEXT("widgetName"), WidgetName);
		Result->SetStringField(TEXT("propertyName"), PropertyName);
		Result->SetStringField(TEXT("propertyValue"), PropertyValue);
		Result->SetBoolField(TEXT("success"), true);
	}
	else
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to set property '%s' on widget '%s'. Property not found or value format invalid."), *PropertyName, *WidgetName));
		Result->SetBoolField(TEXT("success"), false);
	}

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::ReadWidgetAnimations(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
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

	TArray<TSharedPtr<FJsonValue>> AnimationsArray;

	for (UWidgetAnimation* Animation : WidgetBP->Animations)
	{
		if (!Animation) continue;

		TSharedPtr<FJsonObject> AnimObj = MakeShared<FJsonObject>();
		AnimObj->SetStringField(TEXT("name"), Animation->GetName());
		AnimObj->SetStringField(TEXT("displayName"), Animation->GetDisplayLabel().IsEmpty() ? Animation->GetName() : Animation->GetDisplayLabel());

		UMovieScene* MovieScene = Animation->GetMovieScene();
		if (MovieScene)
		{
			// Duration / range
			FFrameRate TickResolution = MovieScene->GetTickResolution();
			FFrameRate DisplayRate = MovieScene->GetDisplayRate();
			TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();

			if (PlaybackRange.HasLowerBound() && PlaybackRange.HasUpperBound())
			{
				double StartSeconds = TickResolution.AsSeconds(PlaybackRange.GetLowerBoundValue());
				double EndSeconds = TickResolution.AsSeconds(PlaybackRange.GetUpperBoundValue());
				AnimObj->SetNumberField(TEXT("startTime"), StartSeconds);
				AnimObj->SetNumberField(TEXT("endTime"), EndSeconds);
				AnimObj->SetNumberField(TEXT("duration"), EndSeconds - StartSeconds);
			}

			AnimObj->SetNumberField(TEXT("displayRate"), DisplayRate.Numerator);

			// Tracks (bindings)
			TArray<TSharedPtr<FJsonValue>> BindingsArray;
			const UMovieScene* ConstMovieScene = MovieScene;
			const TArray<FMovieSceneBinding>& Bindings = ConstMovieScene->GetBindings();
			for (const FMovieSceneBinding& Binding : Bindings)
			{
				TSharedPtr<FJsonObject> BindingObj = MakeShared<FJsonObject>();

				// FMovieSceneBinding::GetName() is deprecated; look up the name from possessable/spawnable instead
				FGuid ObjectGuid = Binding.GetObjectGuid();
				FString BindingName;
				FMovieScenePossessable* Possessable = MovieScene->FindPossessable(ObjectGuid);
				if (Possessable)
				{
					BindingName = Possessable->GetName();
				}
				else
				{
					FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(ObjectGuid);
					if (Spawnable)
					{
						BindingName = Spawnable->GetName();
					}
				}

				BindingObj->SetStringField(TEXT("name"), BindingName);
				BindingObj->SetStringField(TEXT("id"), ObjectGuid.ToString());

				TArray<TSharedPtr<FJsonValue>> TracksArray;
				for (UMovieSceneTrack* Track : Binding.GetTracks())
				{
					if (!Track) continue;
					TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
					TrackObj->SetStringField(TEXT("name"), Track->GetDisplayName().ToString());
					TrackObj->SetStringField(TEXT("class"), Track->GetClass()->GetName());
					TrackObj->SetNumberField(TEXT("sectionCount"), Track->GetAllSections().Num());
					TracksArray.Add(MakeShared<FJsonValueObject>(TrackObj));
				}
				BindingObj->SetArrayField(TEXT("tracks"), TracksArray);

				BindingsArray.Add(MakeShared<FJsonValueObject>(BindingObj));
			}
			AnimObj->SetArrayField(TEXT("bindings"), BindingsArray);

			// Master tracks (non-bound tracks)
			TArray<TSharedPtr<FJsonValue>> MasterTracksArray;
			for (UMovieSceneTrack* Track : MovieScene->GetTracks())
			{
				if (!Track) continue;
				TSharedPtr<FJsonObject> TrackObj = MakeShared<FJsonObject>();
				TrackObj->SetStringField(TEXT("name"), Track->GetDisplayName().ToString());
				TrackObj->SetStringField(TEXT("class"), Track->GetClass()->GetName());
				MasterTracksArray.Add(MakeShared<FJsonValueObject>(TrackObj));
			}
			AnimObj->SetArrayField(TEXT("masterTracks"), MasterTracksArray);
		}

		AnimationsArray.Add(MakeShared<FJsonValueObject>(AnimObj));
	}

	Result->SetArrayField(TEXT("animations"), AnimationsArray);
	Result->SetNumberField(TEXT("count"), AnimationsArray.Num());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::RunEditorUtilityWidget(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UEditorUtilityWidgetBlueprint* EUWidget = Cast<UEditorUtilityWidgetBlueprint>(LoadedAsset);
	if (!EUWidget)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load EditorUtilityWidgetBlueprint at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorUtilitySubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
	if (!Subsystem)
	{
		Result->SetStringField(TEXT("error"), TEXT("EditorUtilitySubsystem not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Subsystem->SpawnAndRegisterTab(EUWidget);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), EUWidget->GetName());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::RunEditorUtilityBlueprint(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'path' or 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UEditorUtilityBlueprint* EUBlueprint = Cast<UEditorUtilityBlueprint>(LoadedAsset);
	if (!EUBlueprint)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load EditorUtilityBlueprint at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	UEditorUtilitySubsystem* Subsystem = GEditor->GetEditorSubsystem<UEditorUtilitySubsystem>();
	if (!Subsystem)
	{
		Result->SetStringField(TEXT("error"), TEXT("EditorUtilitySubsystem not available"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	Subsystem->TryRun(LoadedAsset);

	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("name"), EUBlueprint->GetName());
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
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

	return nullptr;
}

TSharedPtr<FJsonValue> FWidgetHandlers::AddWidget(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// ── Required: assetPath ──
	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// ── Required: widgetClass (e.g. "TextBlock", "CanvasPanel") ──
	FString WidgetClassName;
	if (!Params->TryGetStringField(TEXT("widgetClass"), WidgetClassName) && !Params->TryGetStringField(TEXT("typeName"), WidgetClassName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'widgetClass' parameter (e.g. TextBlock, CanvasPanel, Image)"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// ── Optional: widgetName, parentWidgetName ──
	FString WidgetName;
	Params->TryGetStringField(TEXT("widgetName"), WidgetName);
	if (WidgetName.IsEmpty()) Params->TryGetStringField(TEXT("name"), WidgetName);

	FString ParentWidgetName;
	Params->TryGetStringField(TEXT("parentWidgetName"), ParentWidgetName);

	// ── Load the WidgetBlueprint ──
	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to load WidgetBlueprint at '%s'"), *AssetPath));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	if (!WidgetBP->WidgetTree)
	{
		Result->SetStringField(TEXT("error"), TEXT("WidgetTree is null"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// ── Resolve the UClass ──
	UClass* WClass = ResolveWidgetClass(WidgetClassName);
	if (!WClass)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown widget class '%s'. Use short names like TextBlock, CanvasPanel, Image, Button, etc."), *WidgetClassName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	// ── Construct the widget ──
	UWidget* NewWidget = WidgetBP->WidgetTree->ConstructWidget<UWidget>(WClass, WidgetName.IsEmpty() ? NAME_None : FName(*WidgetName));
	if (!NewWidget)
	{
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to construct widget of class '%s'"), *WidgetClassName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Parent widget '%s' not found"), *ParentWidgetName));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}

		UPanelWidget* ParentPanel = Cast<UPanelWidget>(ParentRaw);
		if (!ParentPanel)
		{
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Parent widget '%s' (%s) is not a panel widget and cannot have children"), *ParentWidgetName, *ParentRaw->GetClass()->GetName()));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}

		UPanelSlot* Slot = ParentPanel->AddChild(NewWidget);
		if (!Slot)
		{
			Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to add '%s' as child of '%s'"), *NewWidget->GetName(), *ParentWidgetName));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
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
			Result->SetStringField(TEXT("error"), TEXT("Root widget is not a panel. Specify parentWidgetName or set a panel as root first."));
			Result->SetBoolField(TEXT("success"), false);
			return MakeShared<FJsonValueObject>(Result);
		}
	}

	// ── Save ──
	WidgetBP->MarkPackageDirty();
	FKismetEditorUtilities::CompileBlueprint(WidgetBP);
	UEditorAssetLibrary::SaveAsset(AssetPath);

	Result->SetStringField(TEXT("widgetName"), NewWidget->GetName());
	Result->SetStringField(TEXT("widgetClass"), WClass->GetName());
	Result->SetBoolField(TEXT("isRoot"), bIsRoot);
	if (!ParentWidgetName.IsEmpty())
	{
		Result->SetStringField(TEXT("parentWidgetName"), ParentWidgetName);
	}
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}

TSharedPtr<FJsonValue> FWidgetHandlers::RemoveWidget(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString AssetPath;
	if (!Params->TryGetStringField(TEXT("assetPath"), AssetPath) && !Params->TryGetStringField(TEXT("path"), AssetPath))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'assetPath' parameter"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
	}

	FString WidgetName;
	if (!Params->TryGetStringField(TEXT("widgetName"), WidgetName))
	{
		Result->SetStringField(TEXT("error"), TEXT("Missing 'widgetName' parameter"));
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

	if (!WidgetBP->WidgetTree)
	{
		Result->SetStringField(TEXT("error"), TEXT("WidgetTree is null"));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Widget not found: '%s'"), *WidgetName));
		Result->SetBoolField(TEXT("success"), false);
		return MakeShared<FJsonValueObject>(Result);
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

	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("widgetClass"), RemovedClass);
	Result->SetBoolField(TEXT("success"), true);

	return MakeShared<FJsonValueObject>(Result);
}
