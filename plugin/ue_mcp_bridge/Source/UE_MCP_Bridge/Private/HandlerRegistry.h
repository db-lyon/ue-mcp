#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "MCPHandlerSpec.h"

class FMCPHandlerRegistry
{
public:
	// Handler function signature: takes params JSON object, returns result JSON value
	using FHandlerFunction = TFunction<TSharedPtr<FJsonValue>(const TSharedPtr<FJsonObject>& Params)>;

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

	// Register a C++ handler
	void RegisterHandler(const FString& MethodName, FHandlerFunction Handler);

	// Register a C++ handler together with its parameter spec (#1057). The
	// handler is registered either way; a spec that fails ValidateHandlerSpec is
	// logged and dropped, and the call returns false.
	bool RegisterHandler(const FString& MethodName, FHandlerFunction Handler, const TArray<FMCPParamSpec>& Params);

	// The same, with the spec's choices and contract exemption (MCPSpec::ExactlyOne,
	// MCPSpec::AtLeastOne, MCPSpec::ContractExempt).
	bool RegisterHandler(const FString& MethodName, FHandlerFunction Handler, const TArray<FMCPParamSpec>& Params, const FMCPSpecRules& Rules);

	// Why a parameter list cannot be published, or empty when it can. Refuses
	// the dispatcher's routing names, duplicates across names and aliases, an
	// item type on anything but an array, and a nullable, union, literal or
	// field shape that does not fit its type.
	static FString ValidateParamSpecs(const TArray<FMCPParamSpec>& Params);

	// ValidateParamSpecs, plus the choices: each has two branches or more, names
	// only declared, optional parameters, and names each of them once across
	// every choice.
	static FString ValidateHandlerSpec(const FMCPHandlerSpec& Spec);

	// Why one parameter's nullable, union, literal, field, form or variant shape
	// does not fit its type, or empty when it does.
	static FString ValidateValueShape(const FMCPParamSpec& Param);

	// Why one object field does not fit its declared type, or empty when it
	// does. Owner names what holds it, for the message.
	static FString ValidateField(const FString& Owner, const FMCPParamField& Field);

	// Why an enum or numeric range does not fit the type it is declared on, or
	// empty when it does. Shared by parameters and fields; Owner names the value.
	static FString ValidateValueRules(const FString& Owner, EMCPParamType Type, EMCPParamType ItemType,
		const TArray<FString>& EnumValues, const TOptional<double>& Minimum, const TOptional<double>& Maximum);

	// True for a name a spec may declare: letters, digits and underscores, not
	// starting with a digit.
	static bool IsParamIdentifier(const FString& Name);

	// Lowercase wire name of a value form: argMap, argEntryList, stringList, string, scalarMap.
	static const TCHAR* ValueFormName(EMCPValueForm Form);

	// Lowercase wire name of a choice mode: exactlyOne, atLeastOne.
	static const TCHAR* ChoiceModeName(EMCPChoiceMode Mode);

	// Lowercase wire name of a parameter type.
	static const TCHAR* ParamTypeName(EMCPParamType Type);

	// Specs of every handler registered with one, keyed by method name.
	const TMap<FString, FMCPHandlerSpec>& GetHandlerSpecs() const { return HandlerSpecs; }

	// { method: { category?, params: [...], choices?, contractExempt? } } for the capabilities payload.
	TSharedPtr<FJsonObject> BuildHandlerSpecsJson() const;
	// The same for every external handler registered with a contract (#1282), uncategorised.
	static TSharedPtr<FJsonObject> BuildExternalHandlerSpecsJson();

	// Params with every declared alias renamed to its parameter's name. Returns
	// Params itself when nothing needs renaming. An alias sent alongside the
	// name it stands for is left in place, so it reports as not read.
	static TSharedPtr<FJsonObject> ResolveParamAliases(const FMCPHandlerSpec& Spec, const TSharedPtr<FJsonObject>& Params);

	// The registered C++ handler, or nullptr. Calling it bypasses alias
	// resolution and read tracking, which is what the contract test needs.
	const FHandlerFunction* FindCppHandler(const FString& MethodName) const { return CppHandlers.Find(MethodName); }

	// Register a C++ handler with a non-default game-thread execution timeout.
	// Most handlers finish in milliseconds, but a few (create_cpp_class
	// regenerates IDE project files; build-related ops) legitimately need
	// minutes. Pass TimeoutSeconds explicitly for those.
	void RegisterHandlerWithTimeout(const FString& MethodName, FHandlerFunction Handler, float TimeoutSeconds);

	// The same, with a parameter spec (#1057). Returns what RegisterHandler with a spec returns.
	bool RegisterHandlerWithTimeout(const FString& MethodName, FHandlerFunction Handler, float TimeoutSeconds, const TArray<FMCPParamSpec>& Params);

	// The same, with the spec's choices and contract exemption.
	bool RegisterHandlerWithTimeout(const FString& MethodName, FHandlerFunction Handler, float TimeoutSeconds, const TArray<FMCPParamSpec>& Params, const FMCPSpecRules& Rules);

	// Look up a per-handler timeout. Returns 0 if no override registered,
	// in which case the caller should use its default.
	float GetHandlerTimeout(const FString& MethodName) const;

	// Execute a handler
	TSharedPtr<FJsonValue> ExecuteHandler(const FString& MethodName, const TSharedPtr<FJsonObject>& Params);

	// The registry whose ExecuteHandler is running on this thread, or nullptr
	// outside one. execute_batch dispatches its steps through this, so a batch
	// runs over whichever registry dispatched the batch itself: the bridge
	// server's, or the one an automation test built. Nothing is recorded at
	// registration time, so no test registry can repoint the live server.
	static FMCPHandlerRegistry* Executing();

	// Check if handler exists
	bool HasHandler(const FString& MethodName) const;

	// Get all registered handler names
	TArray<FString> GetHandlerNames() const;

	// Clear all handlers
	void Clear();

private:
	// C++ handlers
	TMap<FString, FHandlerFunction> CppHandlers;

	// Per-handler game-thread timeouts (seconds). Absent = use default.
	TMap<FString, float> HandlerTimeouts;

	// Category each C++ handler was registered under, when one was set.
	TMap<FString, FString> HandlerCategories;
	FString RegistrationCategory;

	// Declared parameter contracts (#1057). Written during registration only,
	// so the socket thread may read it for the capabilities payload.
	TMap<FString, FMCPHandlerSpec> HandlerSpecs;

	void TagCategory(const FString& MethodName);

	static TSharedPtr<FJsonObject> SpecsToJson(const TMap<FString, FMCPHandlerSpec>& Specs, const TMap<FString, FString>& Categories);
};
