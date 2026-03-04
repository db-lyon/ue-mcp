#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FEditorHandlers
{
public:
	// Register all editor handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Handler implementations
	static TSharedPtr<FJsonValue> ExecuteCommand(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ExecutePython(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetProperty(const TSharedPtr<FJsonObject>& Params);
};
