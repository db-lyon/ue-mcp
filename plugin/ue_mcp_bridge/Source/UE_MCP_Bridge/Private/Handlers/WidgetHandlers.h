#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FWidgetHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	static TSharedPtr<FJsonValue> ListWidgetBlueprints(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateWidgetBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadWidgetTree(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateEditorUtilityWidget(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateEditorUtilityBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetWidgetProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetWidgetFullProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListWidgetBindings(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ClearWidgetBinding(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetWidgetProperty(const TSharedPtr<FJsonObject>& Params);
	// #563: set a full/nested style struct (FButtonStyle, FEditableTextBoxStyle,
	// FSlateFontInfo, ...) on a widget from JSON, and a bulk multi-widget variant.
	static TSharedPtr<FJsonValue> SetWidgetStyle(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> BulkSetWidgetProperties(const TSharedPtr<FJsonObject>& Params);
	// #635/#21: reorder a widget among its parent panel's children by index.
	static TSharedPtr<FJsonValue> ReorderChild(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadWidgetAnimations(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RunEditorUtilityWidget(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RunEditorUtilityBlueprint(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddWidget(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveWidget(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> MoveWidget(const TSharedPtr<FJsonObject>& Params);
	// #365: root-widget swap + "Wrap With" container insertion. Required to
	// reshape an existing WBP root without rebuilding the whole tree.
	static TSharedPtr<FJsonValue> SetRoot(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> WrapRoot(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListWidgetClasses(const TSharedPtr<FJsonObject>& Params);

	// Runtime (PIE) widget inspection (#160)
	static TSharedPtr<FJsonValue> ListRuntimeWidgets(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetRuntimeWidget(const TSharedPtr<FJsonObject>& Params);
	// #161: Runtime delegate inspection
	static TSharedPtr<FJsonValue> GetRuntimeDelegates(const TSharedPtr<FJsonObject>& Params);
	// #602: instantiate a WidgetBlueprint into the live PIE viewport.
	static TSharedPtr<FJsonValue> AddWidgetToViewport(const TSharedPtr<FJsonObject>& Params);
	// #559: fire a UFUNCTION / button click on a live PIE UUserWidget.
	static TSharedPtr<FJsonValue> InvokeRuntimeWidgetFunction(const TSharedPtr<FJsonObject>& Params);

	// Helper: recursively search for a widget by name in the tree
	static class UWidget* FindWidgetByNameRecursive(class UWidget* Root, const FString& WidgetName);
};
