// Split from WidgetHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FWidgetHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in WidgetHandlers.cpp::RegisterHandlers.

#include "WidgetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "HandlerQuery.h"
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

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

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


// get_widget_properties -- full reflected property dump for a named widget,
// unlike get_widget_details which returns only a curated subset. Returns every
// UPROPERTY (RenderOpacity, Visibility, ColorAndOpacity, Border padding/colors,
// Image brush fields, fonts, etc.) plus the slot block, so visual bugs can be
// diagnosed without execute_python reflection. Optional includeSubtree walks
// children. (#547)
TSharedPtr<FJsonValue> FWidgetHandlers::GetWidgetFullProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	const bool bIncludeSubtree = OptionalBool(Params, TEXT("includeSubtree"), false);

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;
	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

	UWidget* FoundWidget = nullptr;
	WidgetBP->WidgetTree->ForEachWidget([&](UWidget* Widget)
	{
		if (Widget && Widget->GetName() == WidgetName) FoundWidget = Widget;
	});
	if (!FoundWidget)
	{
		return MCPError(FString::Printf(TEXT("Widget not found: '%s'"), *WidgetName));
	}

	// Reflect every UPROPERTY on a widget (and its slot) into a JSON object.
	auto DumpWidget = [](UWidget* W) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("name"), W->GetName());
		Obj->SetStringField(TEXT("class"), W->GetClass()->GetName());

		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(W->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			FString ValueStr;
			const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(W);
			Prop->ExportText_Direct(ValueStr, ValuePtr, ValuePtr, W, PPF_None);
			Props->SetStringField(Prop->GetName(), ValueStr);
		}
		Obj->SetObjectField(TEXT("properties"), Props);

		if (UPanelSlot* Slot = W->Slot)
		{
			TSharedPtr<FJsonObject> SlotObj = MakeShared<FJsonObject>();
			SlotObj->SetStringField(TEXT("class"), Slot->GetClass()->GetName());
			TSharedPtr<FJsonObject> SlotProps = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(Slot->GetClass()); It; ++It)
			{
				FProperty* Prop = *It;
				FString ValueStr;
				const void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Slot);
				Prop->ExportText_Direct(ValueStr, ValuePtr, ValuePtr, Slot, PPF_None);
				SlotProps->SetStringField(Prop->GetName(), ValueStr);
			}
			SlotObj->SetObjectField(TEXT("properties"), SlotProps);
			Obj->SetObjectField(TEXT("slot"), SlotObj);
		}
		return Obj;
	};

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetObjectField(TEXT("widget"), DumpWidget(FoundWidget));

	if (bIncludeSubtree)
	{
		TArray<TSharedPtr<FJsonValue>> Children;
		if (UPanelWidget* Panel = Cast<UPanelWidget>(FoundWidget))
		{
			TArray<UWidget*> Stack;
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				if (UWidget* C = Panel->GetChildAt(i)) Stack.Add(C);
			}
			while (Stack.Num() > 0)
			{
				UWidget* W = Stack.Pop();
				Children.Add(MakeShared<FJsonValueObject>(DumpWidget(W)));
				if (UPanelWidget* CP = Cast<UPanelWidget>(W))
				{
					for (int32 i = 0; i < CP->GetChildrenCount(); ++i)
					{
						if (UWidget* GC = CP->GetChildAt(i)) Stack.Add(GC);
					}
				}
			}
		}
		Result->SetArrayField(TEXT("subtree"), Children);
	}

	return MCPResult(Result);
}

// list_widget_bindings -- enumerate the designer property bindings stored on a
// WidgetBlueprint (UWidgetBlueprint::Bindings), which the UE 5.7 Python API
// keeps protected. Returns {widgetName, propertyName, functionName,
// bindingType}. Optional filterWidgetName / filterProperty narrow the list.
// (#530)
TSharedPtr<FJsonValue> FWidgetHandlers::ListWidgetBindings(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	const FString FilterWidget = OptionalString(Params, TEXT("filterWidgetName"));
	const FString FilterProperty = OptionalString(Params, TEXT("filterProperty"));

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	TArray<TSharedPtr<FJsonValue>> BindingsArr;
	for (const FDelegateEditorBinding& B : WidgetBP->Bindings)
	{
		const FString WidgetObj = B.ObjectName;
		const FString PropName = B.PropertyName.ToString();
		if (!FilterWidget.IsEmpty() && !WidgetObj.Equals(FilterWidget, ESearchCase::IgnoreCase)) continue;
		if (!FilterProperty.IsEmpty() && !PropName.Equals(FilterProperty, ESearchCase::IgnoreCase)) continue;

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("widgetName"), WidgetObj);
		Obj->SetStringField(TEXT("propertyName"), PropName);
		Obj->SetStringField(TEXT("functionName"), B.FunctionName.ToString());
		Obj->SetStringField(TEXT("bindingType"), B.Kind == EBindingKind::Function ? TEXT("Function") : TEXT("Property"));
		BindingsArr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("bindingCount"), BindingsArr.Num());
	Result->SetArrayField(TEXT("bindings"), BindingsArr);
	return MCPResult(Result);
}

// clear_widget_binding -- remove designer binding(s) matching widgetName (and
// optional propertyName) from a WidgetBlueprint, without opening the editor.
// Idempotent: removing a non-existent binding reports removed=0. (#530)
TSharedPtr<FJsonValue> FWidgetHandlers::ClearWidgetBinding(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;

	const FString PropertyName = OptionalString(Params, TEXT("propertyName"));

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	WidgetBP->Modify();
	const int32 Removed = WidgetBP->Bindings.RemoveAll([&](const FDelegateEditorBinding& B)
	{
		if (!FString(B.ObjectName).Equals(WidgetName, ESearchCase::IgnoreCase)) return false;
		if (!PropertyName.IsEmpty() && !B.PropertyName.ToString().Equals(PropertyName, ESearchCase::IgnoreCase)) return false;
		return true;
	});

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetNumberField(TEXT("removed"), Removed);
	if (Removed == 0)
	{
		MCPSetExisted(Result);
		return MCPResult(Result);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBP);
	// #728: every compile of a WidgetBlueprint is a chance for the compiler to
	// meet a widget it generates a variable for that owns no entry in
	// WidgetVariableNameToGuidMap, which it reports as a failure. CompileChecked
	// makes the map match first and refuses to compile if it cannot.
	const MCPWidgetGuidMap::FSyncReport GuidSync = MCPWidgetGuidMap::CompileChecked(WidgetBP);
	if (!GuidSync.bCompiled)
	{
		return MCPWidgetGuidMap::BlockedError(AssetPath, GuidSync);
	}
	UEditorAssetLibrary::SaveAsset(AssetPath);
	MCPSetUpdated(Result);
	MCPSetWidgetGuidOutcome(Result, GuidSync, AssetPath);
	// No action puts a designer binding back on THIS blueprint. One action does
	// write Bindings - extract_widget_subtree copies the source blueprint's
	// bindings onto the new blueprint it lifts a subtree into - but it only
	// ever writes the destination it just created, so it cannot restore a
	// binding here. Naming it as the inverse would send a flow at a call that
	// would build a second asset instead of undoing anything.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("No action restores a designer property binding on this blueprint: widget(list_bindings) reads them and widget(clear_binding) ")
		TEXT("removes them. widget(extract_subtree) is the only action that writes Bindings, and it writes them onto the NEW blueprint it ")
		TEXT("creates, never back onto this one. Record what widget(list_bindings) reports before clearing, and re-create the binding in ")
		TEXT("the UMG editor if it is needed again."));
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

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

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

	// The previous value, in the SAME text form this action accepts, and only
	// when the write goes through one of the three reflection routes below,
	// whose input format is UE export text either way. The typed convenience
	// branches take bespoke formats ("R,G,B,A", a bare font size, a plain
	// string for Text) that export text does not round-trip through, so a
	// captured value replayed at one of those would write something else. That
	// is reported as having no rollback rather than given a wrong one.
	FString PreviousPropertyValue;
	bool bCapturedPreviousValue = false;
	// True only when the capture came off a SINGLE-SEGMENT property on the
	// widget itself, which is the one shape widget(set_style) can also address.
	// That matters because an empty previous value cannot travel through this
	// action at all: FStrProperty::ExportText_Internal appends nothing for an
	// empty string when PPF_Delimited is not set, and this action's
	// propertyValue is read with RequireStringAlt, which rejects an empty
	// string as a missing parameter. set_style takes its value as JSON and
	// accepts an empty one, so it is the rollback for that case.
	bool bCapturedOnFlatWidgetProperty = false;
	// The single path segment the capture came off, which is what the set_style
	// rollback has to be handed. It is NOT always the incoming propertyName:
	// ParseIntoArray drops empty segments, so "Foo." parses to one part and
	// still counts as flat, while the raw string would be looked up verbatim by
	// FindPropertyByName and would not be found.
	FString FlatWidgetPropertyName;
	// True when the flat capture came off an FStrProperty. That is the one
	// property kind whose empty-string round trip through set_style is
	// established rather than assumed (see the rollback note below).
	bool bFlatCaptureWasStringProperty = false;

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
		// (#159, #364) Brush fields - ImageSize, Tint, DrawAs, Tiling, Margin, ResourceObject.
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
	// paired bOverride_ flag is toggled on - ImportText on the raw property doesn't do this.
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

			// #532: a UE struct-text value ("(Value=2,SizeRule=Fill)",
			// "(Left=26,Top=22,Right=26,Bottom=24)") or a nested field path
			// ("Size.Value", "Padding.Left") must write through the real struct,
			// not the positional comma-parsers below - those split struct text on
			// commas and silently wrote 0 to the numeric fields while reporting
			// success. Resolve the path rooted at the SLOT and ImportText into the
			// struct so every field persists. A genuine parse failure is surfaced
			// as an error instead of falling through to the lossy parser.
			{
				const FString TrimmedVal = PropertyValue.TrimStartAndEnd();
				const bool bStructText = TrimmedVal.StartsWith(TEXT("("));
				const bool bNestedPath = SlotPropName.Contains(TEXT("."));
				if (bStructText || bNestedPath)
				{
					TArray<FString> SlotParts;
					SlotPropName.ParseIntoArray(SlotParts, TEXT("."));
					UStruct* CurStruct = Slot->GetClass();
					void* CurContainer = Slot;
					FProperty* LeafProp = nullptr;
					for (int32 i = 0; i < SlotParts.Num(); ++i)
					{
						FProperty* P = CurStruct->FindPropertyByName(FName(*SlotParts[i]));
						if (!P) { LeafProp = nullptr; break; }
						if (i < SlotParts.Num() - 1)
						{
							FStructProperty* SP = CastField<FStructProperty>(P);
							if (!SP) { LeafProp = nullptr; break; }
							CurContainer = SP->ContainerPtrToValuePtr<void>(CurContainer);
							CurStruct = SP->Struct;
						}
						else
						{
							LeafProp = P;
						}
					}
					if (LeafProp)
					{
						void* LeafAddr = LeafProp->ContainerPtrToValuePtr<void>(CurContainer);
						FString PreviousText;
						LeafProp->ExportText_Direct(PreviousText, LeafAddr, LeafAddr, Slot, PPF_None);
						if (LeafProp->ImportText_Direct(*PropertyValue, LeafAddr, Slot, PPF_None))
						{
							bPropertySet = true;
							PreviousPropertyValue = PreviousText;
							bCapturedPreviousValue = true;
						}
						else
						{
							return MCPError(FString::Printf(
								TEXT("Value '%s' is not valid for slot property '%s' (type %s). Use UE struct text, e.g. `(Value=1.0,SizeRule=Fill)` for Size or `(Left=8,Top=8,Right=8,Bottom=8)` for Padding."),
								*PropertyValue, *SlotPropName, *LeafProp->GetCPPType()));
						}
					}
				}
			}

			// Well-known CanvasPanelSlot properties
			UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot);
			if (!bPropertySet && CanvasSlot)
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
					FString PreviousText;
					SlotProp->ExportText_Direct(PreviousText, SlotValuePtr, SlotValuePtr, Slot, PPF_None);
					if (SlotProp->ImportText_Direct(*PropertyValue, SlotValuePtr, Slot, PPF_None))
					{
						bPropertySet = true;
						PreviousPropertyValue = PreviousText;
						bCapturedPreviousValue = true;
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
			FString PreviousText;
			FinalProp->ExportText_Direct(PreviousText, ValuePtr, ValuePtr, FoundWidget, PPF_None);
			if (FinalProp->ImportText_Direct(*PropertyValue, ValuePtr, FoundWidget, PPF_None))
			{
				FoundWidget->PostEditChange();
				bPropertySet = true;
				PreviousPropertyValue = PreviousText;
				bCapturedPreviousValue = true;
				bCapturedOnFlatWidgetProperty = (PathParts.Num() == 1);
				if (bCapturedOnFlatWidgetProperty)
				{
					FlatWidgetPropertyName = PathParts[0];
					bFlatCaptureWasStringProperty = FinalProp->IsA<FStrProperty>();
				}
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
		// #728: see ClearWidgetBinding. A property write compiles the blueprint,
		// and the compile is where a missing widget variable GUID surfaces.
		const MCPWidgetGuidMap::FSyncReport GuidSync = MCPWidgetGuidMap::CompileChecked(WidgetBP);
		if (!GuidSync.bCompiled)
		{
			return MCPWidgetGuidMap::BlockedError(AssetPath, GuidSync);
		}
		UEditorAssetLibrary::SaveAsset(AssetPath);

		auto Result = MCPSuccess();
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("widgetName"), WidgetName);
		Result->SetStringField(TEXT("propertyName"), PropertyName);
		Result->SetStringField(TEXT("propertyValue"), PropertyValue);
		MCPSetWidgetGuidOutcome(Result, GuidSync, AssetPath);

		if (bCapturedPreviousValue) { Result->SetStringField(TEXT("previousPropertyValue"), PreviousPropertyValue); }
		if (bCapturedPreviousValue && !PreviousPropertyValue.IsEmpty())
		{
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("assetPath"), AssetPath);
			Payload->SetStringField(TEXT("widgetName"), WidgetName);
			Payload->SetStringField(TEXT("propertyName"), PropertyName);
			Payload->SetStringField(TEXT("propertyValue"), PreviousPropertyValue);
			MCPSetRollback(Result, TEXT("set_widget_property"), Payload);
			Result->SetBoolField(TEXT("rollbackLossy"), false);
		}
		else if (bCapturedPreviousValue && bCapturedOnFlatWidgetProperty)
		{
			// The property held a value that exports to the empty string - an empty
			// FString is the case that reaches here - and this action's propertyValue
			// is required and non-empty, so replaying it here would come back
			// "Missing required parameter 'propertyValue'". widget(set_style) takes
			// its value as JSON, accepts an empty string, and reaches the same engine
			// importer entry point at the same port flags (PPF_None, since that write
			// is at depth 0). It addresses one top-level UPROPERTY on the widget,
			// which is what bCapturedOnFlatWidgetProperty guarantees this is.
			//
			// The two routes are NOT identical, so the note claims only what holds.
			// set_style imports with no owner object where the write above passed
			// FoundWidget, and it does not call PostEditChange where the reflection
			// path does. Neither matters to FStrProperty, which is the kind whose
			// empty-string round trip is established. For any other flat kind whose
			// empty value exports to the empty string, the importer may refuse the
			// empty text, in which case the rollback reports that error rather than
			// writing a wrong value.
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("assetPath"), AssetPath);
			Payload->SetStringField(TEXT("widgetName"), WidgetName);
			Payload->SetStringField(TEXT("propertyName"), FlatWidgetPropertyName);
			Payload->SetStringField(TEXT("value"), PreviousPropertyValue);
			MCPSetRollback(Result, TEXT("set_widget_style"), Payload);
			Result->SetBoolField(TEXT("rollbackLossy"), false);
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("'%s' held a value that exports to the empty string, which set_widget_property cannot be handed back because its ")
				TEXT("propertyValue is a required non-empty parameter. The rollback therefore goes through widget(set_style), which ")
				TEXT("carries the value as JSON and reaches the same engine importer at the same port flags. %s"),
				*FlatWidgetPropertyName,
				bFlatCaptureWasStringProperty
					? TEXT("This is a string property, so the restored value is exact.")
					: TEXT("This is not a string property, so whether the engine importer accepts the empty text for this kind is not ")
					  TEXT("established here: the rollback either restores the value exactly or fails with the importer's own error, and ")
					  TEXT("it cannot write a different value. Read the property back afterwards to confirm which happened.")));
		}
		else if (bCapturedPreviousValue)
		{
			Result->SetBoolField(TEXT("rollbackPossible"), false);
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("'%s' held a value that exports to the empty string, and neither route can put it back: set_widget_property ")
				TEXT("requires a non-empty propertyValue, and widget(set_style) addresses one top-level UPROPERTY on the widget, ")
				TEXT("which a slot property or a dotted path is not. Set it by hand once the flow has unwound."), *PropertyName));
		}
		else
		{
			Result->SetBoolField(TEXT("rollbackPossible"), false);
			Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
				TEXT("'%s' was written through one of this action's typed convenience paths, whose input format (comma-separated colours and ")
				TEXT("sizes, a bare number, a plain string) is not the UE export text the previous value reads back as, so replaying the old ")
				TEXT("value here would write something else. Read it with widget(get_properties) and set it back by its full UPROPERTY name, ")
				TEXT("which routes through the reflection path and does carry an exact rollback. widget(set_style) also takes JSON by ")
				TEXT("UPROPERTY name and rolls back exactly."), *PropertyName));
		}

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

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;

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

// #563: set a full or nested style struct on a widget from JSON. Uses the
// generic JSON->property setter so FButtonStyle / FEditableTextBoxStyle /
// FSlateFontInfo / FSlateColor and their nested brushes are all expressible,
// which the scalar set_widget_property path cannot do.
static UWidget* FindWidgetByName(UWidgetBlueprint* WidgetBP, const FString& Name)
{
	UWidget* Found = nullptr;
	if (WidgetBP && WidgetBP->WidgetTree)
	{
		WidgetBP->WidgetTree->ForEachWidget([&](UWidget* W)
		{
			if (W && W->GetName() == Name && !Found) Found = W;
		});
	}
	return Found;
}

TSharedPtr<FJsonValue> FWidgetHandlers::SetWidgetStyle(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;
	FString PropertyName;
	if (auto Err = RequireString(Params, TEXT("propertyName"), PropertyName)) return Err;
	TSharedPtr<FJsonValue> ValueField = Params->TryGetField(TEXT("value"));
	if (!ValueField.IsValid()) return MCPError(TEXT("Missing 'value' (a JSON object/scalar for the style)"));

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;
	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

	UWidget* Widget = FindWidgetByName(WidgetBP, WidgetName);
	if (!Widget) return MCPError(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

	FProperty* Prop = Widget->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop) return MCPError(FString::Printf(TEXT("Property '%s' not found on %s"), *PropertyName, *Widget->GetClass()->GetName()));

	Widget->Modify();
	void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Widget);

	// The value that is there now, read before the write. A style struct comes
	// back as UE export text and a scalar comes back typed; SetJsonOnProperty
	// accepts both on the way back in, so the captured value replays through
	// this same action.
	const TSharedPtr<FJsonValue> PreviousValue = MCPQuery::PropertyToJson(Prop, ValuePtr);

	FString SetErr;
	if (!MCPJsonProperty::SetJsonOnProperty(Prop, ValuePtr, ValueField, SetErr))
	{
		return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *PropertyName, *SetErr));
	}
	// #728: see ClearWidgetBinding.
	const MCPWidgetGuidMap::FSyncReport GuidSync = MCPWidgetGuidMap::CompileChecked(WidgetBP);
	if (!GuidSync.bCompiled)
	{
		return MCPWidgetGuidMap::BlockedError(AssetPath, GuidSync);
	}
	SaveAssetPackage(WidgetBP);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	MCPSetWidgetGuidOutcome(Result, GuidSync, AssetPath);

	// MCPQuery::PropertyToJson answers for every property kind - a scalar
	// typed, anything else as its exported text - so the capture is always
	// there and this is unconditional. `value` is read with TryGetField
	// rather than a required non-empty string, so an empty previous value
	// replays as readily as any other.
	Result->SetField(TEXT("previousValue"), PreviousValue);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("widgetName"), WidgetName);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	Payload->SetField(TEXT("value"), PreviousValue);
	MCPSetRollback(Result, TEXT("set_widget_style"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Result);
}

// #563: apply many {widgetName, propertyName, value} style/property writes to a
// WidgetBlueprint in one call (single compile + save).
TSharedPtr<FJsonValue> FWidgetHandlers::BulkSetWidgetProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Params->TryGetArrayField(TEXT("properties"), Entries) || !Entries)
	{
		return MCPError(TEXT("Missing 'properties' array ([{widgetName, propertyName, value}])"));
	}

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;
	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);

	TArray<TSharedPtr<FJsonValue>> Results;
	// One entry per write that actually landed, holding the value that write
	// replaced. Replayed back through this same action they restore exactly the
	// properties this call changed, in the order it changed them, and touch
	// nothing it did not.
	TArray<TSharedPtr<FJsonValue>> InversePropertyEntries;
	int32 Applied = 0, Failed = 0;
	int32 EntryIndex = -1;
	for (const TSharedPtr<FJsonValue>& EV : *Entries)
	{
		++EntryIndex;
		const TSharedPtr<FJsonObject>* EObj = nullptr;
		if (!EV->TryGetObject(EObj) || !EObj)
		{
			// An entry that is not a JSON object carries no widgetName or
			// propertyName to report it by, so it is reported by its position in
			// the array instead. It still gets a row: `failed` counts it, and a
			// caller told only the count would have no way to learn WHICH entry
			// the batch could not read. Every entry produces exactly one row, so
			// `results` lines up with `properties` index for index.
			TSharedPtr<FJsonObject> Malformed = MakeShared<FJsonObject>();
			Malformed->SetNumberField(TEXT("index"), EntryIndex);
			Malformed->SetBoolField(TEXT("ok"), false);
			Malformed->SetStringField(TEXT("error"),
				TEXT("entry is not a JSON object ({widgetName, propertyName, value} expected)"));
			Results.Add(MakeShared<FJsonValueObject>(Malformed));
			++Failed;
			continue;
		}
		FString WName, PName;
		(*EObj)->TryGetStringField(TEXT("widgetName"), WName);
		(*EObj)->TryGetStringField(TEXT("propertyName"), PName);
		TSharedPtr<FJsonValue> Val = (*EObj)->TryGetField(TEXT("value"));
		TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetNumberField(TEXT("index"), EntryIndex);
		R->SetStringField(TEXT("widgetName"), WName);
		R->SetStringField(TEXT("propertyName"), PName);

		UWidget* Widget = FindWidgetByName(WidgetBP, WName);
		if (!Widget || WName.IsEmpty() || PName.IsEmpty() || !Val.IsValid())
		{
			R->SetBoolField(TEXT("ok"), false);
			R->SetStringField(TEXT("error"), TEXT("widget/property/value missing"));
			Results.Add(MakeShared<FJsonValueObject>(R)); ++Failed; continue;
		}
		FProperty* Prop = Widget->GetClass()->FindPropertyByName(FName(*PName));
		if (!Prop)
		{
			R->SetBoolField(TEXT("ok"), false);
			R->SetStringField(TEXT("error"), TEXT("property not found"));
			Results.Add(MakeShared<FJsonValueObject>(R)); ++Failed; continue;
		}
		Widget->Modify();
		void* EntryValuePtr = Prop->ContainerPtrToValuePtr<void>(Widget);
		const TSharedPtr<FJsonValue> EntryPrevious = MCPQuery::PropertyToJson(Prop, EntryValuePtr);
		FString SetErr;
		if (MCPJsonProperty::SetJsonOnProperty(Prop, EntryValuePtr, Val, SetErr))
		{
			R->SetBoolField(TEXT("ok"), true); ++Applied;
			// PropertyToJson answers for every property kind, so every write that
			// lands contributes exactly one inverse entry and the two counts move
			// together.
			TSharedPtr<FJsonObject> Inverse = MakeShared<FJsonObject>();
			Inverse->SetStringField(TEXT("widgetName"), WName);
			Inverse->SetStringField(TEXT("propertyName"), PName);
			Inverse->SetField(TEXT("value"), EntryPrevious);
			InversePropertyEntries.Add(MakeShared<FJsonValueObject>(Inverse));
		}
		else
		{
			R->SetBoolField(TEXT("ok"), false);
			R->SetStringField(TEXT("error"), SetErr);
			++Failed;
		}
		Results.Add(MakeShared<FJsonValueObject>(R));
	}

	// #728: see ClearWidgetBinding.
	const MCPWidgetGuidMap::FSyncReport GuidSync = MCPWidgetGuidMap::CompileChecked(WidgetBP);
	if (!GuidSync.bCompiled)
	{
		return MCPWidgetGuidMap::BlockedError(AssetPath, GuidSync);
	}
	SaveAssetPackage(WidgetBP);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetNumberField(TEXT("applied"), Applied);
	Result->SetNumberField(TEXT("failed"), Failed);
	Result->SetArrayField(TEXT("results"), Results);
	// A batch where nothing landed changed nothing, which a caller retrying
	// after an ambiguous result has to be able to tell from a batch that did.
	Result->SetBoolField(TEXT("unchanged"), Applied == 0);
	MCPSetWidgetGuidOutcome(Result, GuidSync, AssetPath);

	if (InversePropertyEntries.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		Payload->SetArrayField(TEXT("properties"), InversePropertyEntries);
		MCPSetRollback(Result, TEXT("bulk_set_widget_properties"), Payload);
		Result->SetBoolField(TEXT("rollbackLossy"), false);
	}
	else
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("No write landed, so nothing was changed and there is nothing to restore. Every entry was rejected: `results` says why ")
			TEXT("for each one."));
	}
	return MCPResult(Result);
}

// #635/#21: reorder a widget among its parent panel's children (move to a
// specific sibling index). move_widget only reparents; this shifts order,
// e.g. inserting a new row BETWEEN two existing children.
TSharedPtr<FJsonValue> FWidgetHandlers::ReorderChild(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;
	FString WidgetName;
	if (auto Err = RequireString(Params, TEXT("widgetName"), WidgetName)) return Err;
	if (!Params->HasField(TEXT("index"))) return MCPError(TEXT("Missing 'index'"));
	const int32 NewIndex = (int32)Params->GetNumberField(TEXT("index"));

	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP) return ResolveError;
	if (!WidgetBP->WidgetTree) return MCPWidget::MissingWidgetTreeError(AssetPath);
	UWidget* Widget = FindWidgetByName(WidgetBP, WidgetName);
	if (!Widget) return MCPError(FString::Printf(TEXT("Widget not found: %s"), *WidgetName));

	UPanelWidget* Parent = Widget->GetParent();
	if (!Parent) return MCPError(FString::Printf(TEXT("Widget '%s' has no parent panel (is it the root?)"), *WidgetName));

	const int32 Count = Parent->GetChildrenCount();
	const int32 ClampedIndex = FMath::Clamp(NewIndex, 0, Count - 1);
	const int32 OldIndex = Parent->GetChildIndex(Widget);

	Parent->Modify();
	Parent->ShiftChild(ClampedIndex, Widget);
	// #728: see ClearWidgetBinding.
	const MCPWidgetGuidMap::FSyncReport GuidSync = MCPWidgetGuidMap::CompileChecked(WidgetBP);
	if (!GuidSync.bCompiled)
	{
		return MCPWidgetGuidMap::BlockedError(AssetPath, GuidSync);
	}
	SaveAssetPackage(WidgetBP);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("widgetName"), WidgetName);
	Result->SetStringField(TEXT("parent"), Parent->GetName());
	Result->SetNumberField(TEXT("oldIndex"), OldIndex);
	Result->SetNumberField(TEXT("newIndex"), ClampedIndex);
	MCPSetWidgetGuidOutcome(Result, GuidSync, AssetPath);

	// ShiftChild moves one child and slides the rest to close the gap, so
	// shifting it back to the index it held restores the whole order. The
	// reparenting is untouched, so this is exact rather than approximate.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("widgetName"), WidgetName);
	Payload->SetNumberField(TEXT("index"), OldIndex);
	MCPSetRollback(Result, TEXT("reorder_child"), Payload);
	Result->SetBoolField(TEXT("rollbackLossy"), false);
	return MCPResult(Result);
}
