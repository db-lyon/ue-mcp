// Split from WidgetHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FWidgetHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in WidgetHandlers.cpp::RegisterHandlers.

#include "WidgetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "WidgetBlueprint.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/CheckBox.h"
#include "Components/Slider.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/CanvasPanel.h"
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
#include "Components/OverlaySlot.h"
#include "Animation/WidgetAnimation.h"
#include "MovieScene.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSpawnable.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "EditorAssetLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"


TSharedPtr<FJsonValue> FWidgetHandlers::GetWidgetProperties(const TSharedPtr<FJsonObject>& Params)
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
		return MCPError(FString::Printf(TEXT("Widget not found: '%s'"), *WidgetName));
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

	// #107: dump Slot layout properties (anchors, position, padding, alignment, etc.) via reflection
	if (UPanelSlot* Slot = FoundWidget->Slot)
	{
		TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
		SlotObj->SetStringField(TEXT("class"), Slot->GetClass()->GetName());

		TSharedPtr<FJsonObject> SlotProps = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Slot->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop) continue;
			// Skip CPF_Edit check - include all reflected slot properties
			FString ValueStr;
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Slot);
			Prop->ExportText_Direct(ValueStr, ValuePtr, ValuePtr, Slot, PPF_None);
			if (!ValueStr.IsEmpty())
			{
				SlotProps->SetStringField(Prop->GetName(), ValueStr);
			}
		}
		SlotObj->SetObjectField(TEXT("properties"), SlotProps);
		PropsObj->SetObjectField(TEXT("slot"), SlotObj);
	}

	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("properties"), PropsObj);

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FWidgetHandlers::SetWidgetProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;

	FString PropertyValue;
	if (auto Err = RequireStringAlt(Params, TEXT("propertyValue"), TEXT("value"), PropertyValue)) return Err;

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
		return MCPError(FString::Printf(TEXT("Widget not found: '%s'"), *WidgetName));
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
		// (#159, #364) Brush fields — ImageSize, Tint, DrawAs, Tiling, Margin, ResourceObject.
		// Case-insensitive so "Brush.ImageSize" works as well as "brush.imageSize".
		else if (PropertyName.StartsWith(TEXT("brush."), ESearchCase::IgnoreCase))
		{
			FString Field = PropertyName.Mid(6); // strip "brush."
			FSlateBrush Brush = Image->GetBrush();
			if (Field == TEXT("imageSize") || Field == TEXT("ImageSize"))
			{
				TArray<FString> Parts;
				PropertyValue.ParseIntoArray(Parts, TEXT(","));
				if (Parts.Num() >= 2)
				{
					Brush.ImageSize = FVector2D(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]));
					Image->SetBrush(Brush);
					bPropertySet = true;
				}
			}
			else if (Field == TEXT("tint") || Field == TEXT("Tint") || Field == TEXT("tintColor"))
			{
				TArray<FString> Parts;
				PropertyValue.ParseIntoArray(Parts, TEXT(","));
				if (Parts.Num() >= 3)
				{
					float R = FCString::Atof(*Parts[0]);
					float G = FCString::Atof(*Parts[1]);
					float B = FCString::Atof(*Parts[2]);
					float A = Parts.Num() >= 4 ? FCString::Atof(*Parts[3]) : 1.0f;
					Brush.TintColor = FSlateColor(FLinearColor(R, G, B, A));
					Image->SetBrush(Brush);
					bPropertySet = true;
				}
			}
			else if (Field == TEXT("drawAs") || Field == TEXT("DrawAs"))
			{
				const FString V = PropertyValue.ToLower();
				if (V == TEXT("image"))         { Brush.DrawAs = ESlateBrushDrawType::Image; bPropertySet = true; }
				else if (V == TEXT("box"))      { Brush.DrawAs = ESlateBrushDrawType::Box;   bPropertySet = true; }
				else if (V == TEXT("border"))   { Brush.DrawAs = ESlateBrushDrawType::Border; bPropertySet = true; }
				else if (V == TEXT("noddrawtype") || V == TEXT("none") || V == TEXT("notype")) { Brush.DrawAs = ESlateBrushDrawType::NoDrawType; bPropertySet = true; }
				if (bPropertySet) Image->SetBrush(Brush);
			}
			else if (Field == TEXT("tiling") || Field == TEXT("Tiling"))
			{
				const FString V = PropertyValue.ToLower();
				if (V == TEXT("notile") || V == TEXT("none")) { Brush.Tiling = ESlateBrushTileType::NoTile; bPropertySet = true; }
				else if (V == TEXT("horizontal") || V == TEXT("h")) { Brush.Tiling = ESlateBrushTileType::Horizontal; bPropertySet = true; }
				else if (V == TEXT("vertical") || V == TEXT("v"))   { Brush.Tiling = ESlateBrushTileType::Vertical;   bPropertySet = true; }
				else if (V == TEXT("both") || V == TEXT("xy"))      { Brush.Tiling = ESlateBrushTileType::Both;       bPropertySet = true; }
				if (bPropertySet) Image->SetBrush(Brush);
			}
			else if (Field == TEXT("margin") || Field == TEXT("Margin"))
			{
				TArray<FString> Parts;
				PropertyValue.ParseIntoArray(Parts, TEXT(","));
				if (Parts.Num() == 1)
				{
					float V = FCString::Atof(*Parts[0]);
					Brush.Margin = FMargin(V);
					Image->SetBrush(Brush);
					bPropertySet = true;
				}
				else if (Parts.Num() >= 4)
				{
					Brush.Margin = FMargin(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]),
					                        FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3]));
					Image->SetBrush(Brush);
					bPropertySet = true;
				}
			}
			else if (Field == TEXT("resourceObject") || Field == TEXT("ResourceObject") || Field == TEXT("texture"))
			{
				// Accept a texture/material asset path.
				UObject* Resource = LoadObject<UObject>(nullptr, *PropertyValue);
				if (Resource)
				{
					if (UTexture2D* Tex = Cast<UTexture2D>(Resource))
					{
						Image->SetBrushFromTexture(Tex, false);
						bPropertySet = true;
					}
					else if (UMaterialInterface* Mat = Cast<UMaterialInterface>(Resource))
					{
						Image->SetBrushFromMaterial(Mat);
						bPropertySet = true;
					}
					else
					{
						Brush.SetResourceObject(Resource);
						Image->SetBrush(Brush);
						bPropertySet = true;
					}
				}
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
	// (#135) SizeBox overrides: UMG 5.1+ requires the Set*Override accessors so the
	// paired bOverride_ flag is toggled on — ImportText on the raw property doesn't do this.
	if (!bPropertySet)
	{
		if (USizeBox* SizeBox = Cast<USizeBox>(FoundWidget))
		{
			const float V = FCString::Atof(*PropertyValue);
			const FString& N = PropertyName;
			if (N == TEXT("WidthOverride") || N == TEXT("widthOverride"))       { SizeBox->SetWidthOverride(V);       bPropertySet = true; }
			else if (N == TEXT("HeightOverride") || N == TEXT("heightOverride")) { SizeBox->SetHeightOverride(V);      bPropertySet = true; }
			else if (N == TEXT("MinDesiredWidth") || N == TEXT("minDesiredWidth"))   { SizeBox->SetMinDesiredWidth(V);   bPropertySet = true; }
			else if (N == TEXT("MinDesiredHeight") || N == TEXT("minDesiredHeight")) { SizeBox->SetMinDesiredHeight(V);  bPropertySet = true; }
			else if (N == TEXT("MaxDesiredWidth") || N == TEXT("maxDesiredWidth"))   { SizeBox->SetMaxDesiredWidth(V);   bPropertySet = true; }
			else if (N == TEXT("MaxDesiredHeight") || N == TEXT("maxDesiredHeight")) { SizeBox->SetMaxDesiredHeight(V);  bPropertySet = true; }
			else if (N == TEXT("clearWidthOverride"))  { SizeBox->ClearWidthOverride();  bPropertySet = true; }
			else if (N == TEXT("clearHeightOverride")) { SizeBox->ClearHeightOverride(); bPropertySet = true; }
		}
	}

	// ── Slot properties (slot.anchors, slot.alignment, slot.position, slot.autoSize, slot.*) ──
	// Case-insensitive: "Slot.padding" and "slot.padding" both route here (#364).
	if (!bPropertySet && PropertyName.StartsWith(TEXT("slot."), ESearchCase::IgnoreCase))
	{
		UPanelSlot* Slot = FoundWidget->Slot;
		if (Slot)
		{
			// #200: slot mutations were getting overwritten when the
			// subsequent CompileBlueprint regenerated the widget tree without
			// the source slot ever being marked dirty. Modify() the chain so
			// the transaction system records the slot before we touch it.
			WidgetBP->Modify();
			if (WidgetBP->WidgetTree) WidgetBP->WidgetTree->Modify();
			FoundWidget->Modify();
			Slot->Modify();

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

			// ── HorizontalBoxSlot / VerticalBoxSlot ──
			auto TryBoxSlotProps = [&](UPanelSlot* BoxSlot) -> bool
			{
				if (SlotPropName == TEXT("padding") || SlotPropName == TEXT("Padding"))
				{
					// "L,T,R,B" or uniform "N"
					TArray<FString> Parts;
					PropertyValue.ParseIntoArray(Parts, TEXT(","));
					FMargin Margin;
					if (Parts.Num() == 1)
					{
						float V = FCString::Atof(*Parts[0]);
						Margin = FMargin(V);
					}
					else if (Parts.Num() >= 4)
					{
						Margin = FMargin(FCString::Atof(*Parts[0]), FCString::Atof(*Parts[1]),
										  FCString::Atof(*Parts[2]), FCString::Atof(*Parts[3]));
					}
					else return false;

					if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(BoxSlot))
						HSlot->SetPadding(Margin);
					else if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(BoxSlot))
						VSlot->SetPadding(Margin);
					else if (UOverlaySlot* OSlot = Cast<UOverlaySlot>(BoxSlot))
						OSlot->SetPadding(Margin);
					else return false;
					return true;
				}
				if (SlotPropName == TEXT("hAlign") || SlotPropName == TEXT("HorizontalAlignment") || SlotPropName == TEXT("horizontalAlignment"))
				{
					EHorizontalAlignment Align = EHorizontalAlignment::HAlign_Fill;
					FString Val = PropertyValue.ToLower();
					if (Val == TEXT("left"))        Align = EHorizontalAlignment::HAlign_Left;
					else if (Val == TEXT("center"))  Align = EHorizontalAlignment::HAlign_Center;
					else if (Val == TEXT("right"))   Align = EHorizontalAlignment::HAlign_Right;
					else if (Val == TEXT("fill"))    Align = EHorizontalAlignment::HAlign_Fill;

					if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(BoxSlot))
						HSlot->SetHorizontalAlignment(Align);
					else if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(BoxSlot))
						VSlot->SetHorizontalAlignment(Align);
					else if (UOverlaySlot* OSlot = Cast<UOverlaySlot>(BoxSlot))
						OSlot->SetHorizontalAlignment(Align);
					else return false;
					return true;
				}
				if (SlotPropName == TEXT("vAlign") || SlotPropName == TEXT("VerticalAlignment") || SlotPropName == TEXT("verticalAlignment"))
				{
					EVerticalAlignment Align = EVerticalAlignment::VAlign_Fill;
					FString Val = PropertyValue.ToLower();
					if (Val == TEXT("top"))          Align = EVerticalAlignment::VAlign_Top;
					else if (Val == TEXT("center"))  Align = EVerticalAlignment::VAlign_Center;
					else if (Val == TEXT("bottom"))  Align = EVerticalAlignment::VAlign_Bottom;
					else if (Val == TEXT("fill"))    Align = EVerticalAlignment::VAlign_Fill;

					if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(BoxSlot))
						HSlot->SetVerticalAlignment(Align);
					else if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(BoxSlot))
						VSlot->SetVerticalAlignment(Align);
					else if (UOverlaySlot* OSlot = Cast<UOverlaySlot>(BoxSlot))
						OSlot->SetVerticalAlignment(Align);
					else return false;
					return true;
				}
				if (SlotPropName == TEXT("sizeRule") || SlotPropName == TEXT("SizeRule"))
				{
					FString Val = PropertyValue.ToLower();
					ESlateSizeRule::Type Rule = (Val == TEXT("fill")) ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic;
					if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(BoxSlot))
					{
						FSlateChildSize Size = HSlot->GetSize();
						Size.SizeRule = Rule;
						HSlot->SetSize(Size);
					}
					else if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(BoxSlot))
					{
						FSlateChildSize Size = VSlot->GetSize();
						Size.SizeRule = Rule;
						VSlot->SetSize(Size);
					}
					else return false;
					return true;
				}
				if (SlotPropName == TEXT("sizeValue") || SlotPropName == TEXT("SizeValue") || SlotPropName == TEXT("fillWeight"))
				{
					float Value = FCString::Atof(*PropertyValue);
					if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(BoxSlot))
					{
						FSlateChildSize Size = HSlot->GetSize();
						Size.Value = Value;
						HSlot->SetSize(Size);
					}
					else if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(BoxSlot))
					{
						FSlateChildSize Size = VSlot->GetSize();
						Size.Value = Value;
						VSlot->SetSize(Size);
					}
					else return false;
					return true;
				}
				// #200: combined size accessor for box slots. Accepts either a
				// "value,rule" string ("1,fill" / "1.5,automatic") or an
				// "automatic"/"fill" word for "value=1, rule=...".
				if (SlotPropName == TEXT("size") || SlotPropName == TEXT("Size"))
				{
					FString RuleText = PropertyValue.ToLower();
					float Value = 1.0f;
					if (PropertyValue.Contains(TEXT(",")))
					{
						TArray<FString> Parts;
						PropertyValue.ParseIntoArray(Parts, TEXT(","));
						if (Parts.Num() >= 2)
						{
							Value = FCString::Atof(*Parts[0]);
							RuleText = Parts[1].ToLower().TrimStartAndEnd();
						}
					}
					ESlateSizeRule::Type Rule = (RuleText.Contains(TEXT("fill"))) ? ESlateSizeRule::Fill : ESlateSizeRule::Automatic;
					FSlateChildSize NewSize;
					NewSize.SizeRule = Rule;
					NewSize.Value = Value;
					if (UHorizontalBoxSlot* HSlot = Cast<UHorizontalBoxSlot>(BoxSlot))
					{
						HSlot->SetSize(NewSize);
					}
					else if (UVerticalBoxSlot* VSlot = Cast<UVerticalBoxSlot>(BoxSlot))
					{
						VSlot->SetSize(NewSize);
					}
					else return false;
					return true;
				}
				return false;
			};

			if (!bPropertySet && (Cast<UHorizontalBoxSlot>(Slot) || Cast<UVerticalBoxSlot>(Slot) || Cast<UOverlaySlot>(Slot)))
			{
				bPropertySet = TryBoxSlotProps(Slot);
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

	// Fallback: try to set via UObject reflection. Supports dotted paths
	// (#364) so "Brush.ImageSize" / "ColorAndOpacity.SpecifiedColor.R" /
	// "Padding.Left" all drill into FStructProperty fields cleanly. The
	// previous flat lookup quietly failed because FProperty names never
	// contain dots, so the parent struct was never written.
	if (!bPropertySet)
	{
		TArray<FString> PathParts;
		PropertyName.ParseIntoArray(PathParts, TEXT("."));

		UStruct* CurrentStruct = FoundWidget->GetClass();
		void* CurrentContainer = FoundWidget;
		FProperty* FinalProp = nullptr;

		for (int32 i = 0; i < PathParts.Num(); i++)
		{
			FProperty* Prop = CurrentStruct->FindPropertyByName(FName(*PathParts[i]));
			if (!Prop) break;
			if (i < PathParts.Num() - 1)
			{
				FStructProperty* StructProp = CastField<FStructProperty>(Prop);
				if (!StructProp) break;
				CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
				CurrentStruct = StructProp->Struct;
			}
			else
			{
				FinalProp = Prop;
			}
		}

		if (FinalProp)
		{
			void* ValuePtr = FinalProp->ContainerPtrToValuePtr<void>(CurrentContainer);
			if (FinalProp->ImportText_Direct(*PropertyValue, ValuePtr, FoundWidget, PPF_None))
			{
				FoundWidget->PostEditChange();
				bPropertySet = true;
			}
			else
			{
				return MCPError(FString::Printf(
					TEXT("Value '%s' is not valid for property '%s' (type %s). Use UE's text format (e.g. `(X=64,Y=64)` for FVector2D)."),
					*PropertyValue, *FinalProp->GetName(), *FinalProp->GetCPPType()));
			}
		}
	}

	if (bPropertySet)
	{
		// Mark package dirty and save
		WidgetBP->MarkPackageDirty();
		FKismetEditorUtilities::CompileBlueprint(WidgetBP);
		UEditorAssetLibrary::SaveAsset(AssetPath);

		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("widgetName"), WidgetName);
		Result->SetStringField(TEXT("propertyName"), PropertyName);
		Result->SetStringField(TEXT("propertyValue"), PropertyValue);

		return MCPResult(Result);
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Failed to set property '%s' on widget '%s'. Property not found or value format invalid."), *PropertyName, *WidgetName));
	}
}


TSharedPtr<FJsonValue> FWidgetHandlers::ReadWidgetAnimations(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UObject* LoadedAsset = UEditorAssetLibrary::LoadAsset(AssetPath);
	UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(LoadedAsset);
	if (!WidgetBP)
	{
		return MCPError(FString::Printf(TEXT("Failed to load WidgetBlueprint at '%s'"), *AssetPath));
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

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("animations"), AnimationsArray);
	Result->SetNumberField(TEXT("count"), AnimationsArray.Num());

	return MCPResult(Result);
}
