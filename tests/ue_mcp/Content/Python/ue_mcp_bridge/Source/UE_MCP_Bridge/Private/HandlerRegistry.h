#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FMCPHandlerRegistry
{
public:
	// Handler function signature: takes params JSON object, returns result JSON value
	using FHandlerFunction = TFunction<TSharedPtr<FJsonValue>(const TSharedPtr<FJsonObject>& Params)>;

	// Python handler info
	struct FPythonHandlerInfo
	{
		FString ScriptPath;
		FString HandlerName;
	};

	FMCPHandlerRegistry();
	~FMCPHandlerRegistry();

	// Register a C++ handler
	void RegisterHandler(const FString& MethodName, FHandlerFunction Handler);

	// Register a Python handler
	void RegisterPythonHandler(const FString& MethodName, const FString& PythonScriptPath);

	// Execute a handler
	TSharedPtr<FJsonValue> ExecuteHandler(const FString& MethodName, const TSharedPtr<FJsonObject>& Params);

	// Check if handler exists
	bool HasHandler(const FString& MethodName) const;

	// Get all registered handler names
	TArray<FString> GetHandlerNames() const;

	// Clear all handlers
	void Clear();

private:
	// C++ handlers
	TMap<FString, FHandlerFunction> CppHandlers;

	// Python handlers
	TMap<FString, FPythonHandlerInfo> PythonHandlers;

	// Execute Python handler
	TSharedPtr<FJsonValue> ExecutePythonHandler(const FString& MethodName, const TSharedPtr<FJsonObject>& Params);
};
