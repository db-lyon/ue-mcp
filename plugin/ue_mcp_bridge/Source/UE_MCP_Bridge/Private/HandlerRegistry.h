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

	// #1057: handlers registered while this is alive are tagged with Category.
	class FCategoryScope
	{
	public:
		FCategoryScope(FMCPHandlerRegistry& InRegistry, const FString& Category)
			: Registry(InRegistry)
			, Previous(InRegistry.RegistrationCategory)
		{
			Registry.RegistrationCategory = Category;
		}
		~FCategoryScope()
		{
			Registry.RegistrationCategory = Previous;
		}
		FCategoryScope(const FCategoryScope&) = delete;
		FCategoryScope& operator=(const FCategoryScope&) = delete;
	private:
		FMCPHandlerRegistry& Registry;
		FString Previous;
	};

	// Categories whose handlers report the parameters they never read (#1057).
	static bool ReportsUnreadParams(const FString& Category);

	// Register a C++ handler
	void RegisterHandler(const FString& MethodName, FHandlerFunction Handler);

	// Register a C++ handler with a non-default game-thread execution timeout.
	// Most handlers finish in milliseconds, but a few (create_cpp_class
	// regenerates IDE project files; build-related ops) legitimately need
	// minutes. Pass TimeoutSeconds explicitly for those.
	void RegisterHandlerWithTimeout(const FString& MethodName, FHandlerFunction Handler, float TimeoutSeconds);

	// Look up a per-handler timeout. Returns 0 if no override registered,
	// in which case the caller should use its default.
	float GetHandlerTimeout(const FString& MethodName) const;

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

	// Per-handler game-thread timeouts (seconds). Absent = use default.
	TMap<FString, float> HandlerTimeouts;

	// Category each C++ handler was registered under, when one was set.
	TMap<FString, FString> HandlerCategories;
	FString RegistrationCategory;

	void TagCategory(const FString& MethodName);

	// Execute Python handler
	TSharedPtr<FJsonValue> ExecutePythonHandler(const FString& MethodName, const TSharedPtr<FJsonObject>& Params);
};
