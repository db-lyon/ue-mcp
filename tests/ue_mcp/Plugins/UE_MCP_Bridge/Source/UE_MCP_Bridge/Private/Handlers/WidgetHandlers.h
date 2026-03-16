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
	static TSharedPtr<FJsonValue> SearchWidgetByName(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetWidgetProperties(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetWidgetProperty(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReadWidgetAnimations(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RunEditorUtilityWidget(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RunEditorUtilityBlueprint(const TSharedPtr<FJsonObject>& Params);

	// Helper: recursively search for a widget by name in the tree
	static class UWidget* FindWidgetByNameRecursive(class UWidget* Root, const FString& WidgetName);
};
