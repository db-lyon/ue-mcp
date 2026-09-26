#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

// ── Parameter specs (#1057) ──────────────────────────────────────────────────
//
// A handler registered with a spec declares its parameter contract once, here
// in C++. The registry publishes it in get_bridge_capabilities, resolves its
// aliases before the handler runs, and the TS surface for the action is
// generated from a recording of it (tests/golden/handler-specs.json).

/** Wire type of one parameter. Published as the lowercase name MCPParamTypeName returns. */
enum class EMCPParamType : uint8
{
	String,
	Number,
	Integer,
	Boolean,
	Object,
	Array,
	Vec3,
	Rotator,
	Any,
	/** {r, g, b, a?} */
	Color,
};

/**
 * A named shape of a value that takes one of several forms. Each form has one
 * fixed, fully typed schema on the TS side, so a value that accepts a parameter
 * map, an entry list or a string is declared without an untyped member (#811).
 * Published as the name ValueFormName returns.
 */
enum class EMCPValueForm : uint8
{
	/** { name: value }, each value a scalar, a struct object, or an array of those. */
	ArgMap,
	/** [{ name, value? }]: the same map, written as a list of entries. */
	ArgEntryList,
	/** An array of strings. */
	StringList,
	/** One string. */
	String,
};

/** One field of an object parameter, or of each element of an array of objects. */
struct FMCPParamField
{
	FString Name;
	EMCPParamType Type = EMCPParamType::String;
	bool bRequired = false;
	FString Description;
	/** Element type of an Array field. Any on every other type. */
	EMCPParamType ItemType = EMCPParamType::Any;
	/** The forms an Any field takes, when it takes one of several named shapes. */
	TArray<EMCPValueForm> Forms;

	FMCPParamField Items(EMCPParamType InItemType) const
	{
		FMCPParamField Copy = *this;
		Copy.ItemType = InItemType;
		return Copy;
	}

	FMCPParamField OneOfForms(const TArray<EMCPValueForm>& InForms) const
	{
		FMCPParamField Copy = *this;
		Copy.Forms = InForms;
		return Copy;
	}
};

/** One shape of a tagged union: the value of its tag field, and the fields that shape has. */
struct FMCPParamVariant
{
	FString Tag;
	FString Description;
	TArray<FMCPParamField> Fields;
};

/** One declared parameter of a handler. */
struct FMCPParamSpec
{
	FString Name;
	EMCPParamType Type = EMCPParamType::String;
	bool bRequired = false;
	FString Description;
	/** Other names a caller may send it under. The registry renames them to Name before dispatch. */
	TArray<FString> Aliases;
	/** Element type of an Array parameter. Any on every other type. */
	EMCPParamType ItemType = EMCPParamType::Any;
	/** JSON null is a value of its own, such as "clear the reference". */
	bool bNullable = false;
	/** Further types the value may take instead of Type: a number or a colour. */
	TArray<EMCPParamType> OrTypes;
	/** The one value the parameter accepts, when it is a flag with a single legal value. */
	TSharedPtr<FJsonValue> LiteralValue;
	/** The shape of an Object parameter, or of each element of an Array of objects. */
	TArray<FMCPParamField> Fields;
	/** The forms an Any parameter takes, when it takes one of several named shapes. */
	TArray<EMCPValueForm> Forms;
	/** The field whose value selects a tagged union's variant. Set together with Variants. */
	FString VariantKey;
	/** The shapes of a tagged Object parameter, or of each element of a tagged Array of objects. */
	TArray<FMCPParamVariant> Variants;

	FMCPParamSpec Alias(const TCHAR* InAlias) const
	{
		FMCPParamSpec Copy = *this;
		Copy.Aliases.Add(InAlias);
		return Copy;
	}

	FMCPParamSpec Items(EMCPParamType InItemType) const
	{
		FMCPParamSpec Copy = *this;
		Copy.ItemType = InItemType;
		return Copy;
	}

	FMCPParamSpec Nullable() const
	{
		FMCPParamSpec Copy = *this;
		Copy.bNullable = true;
		return Copy;
	}

	FMCPParamSpec Or(EMCPParamType InType) const
	{
		FMCPParamSpec Copy = *this;
		Copy.OrTypes.Add(InType);
		return Copy;
	}

	FMCPParamSpec Literal(bool bValue) const
	{
		FMCPParamSpec Copy = *this;
		Copy.LiteralValue = MakeShared<FJsonValueBoolean>(bValue);
		return Copy;
	}

	FMCPParamSpec Literal(const TCHAR* Value) const
	{
		FMCPParamSpec Copy = *this;
		Copy.LiteralValue = MakeShared<FJsonValueString>(FString(Value));
		return Copy;
	}

	FMCPParamSpec WithFields(const TArray<FMCPParamField>& InFields) const
	{
		FMCPParamSpec Copy = *this;
		Copy.Fields = InFields;
		return Copy;
	}

	/** `.OneOfForms({ EMCPValueForm::ArgMap, EMCPValueForm::String })` on an Any parameter. */
	FMCPParamSpec OneOfForms(const TArray<EMCPValueForm>& InForms) const
	{
		FMCPParamSpec Copy = *this;
		Copy.Forms = InForms;
		return Copy;
	}

	/** A tagged union: the field named Key holds a tag that picks one of InVariants. */
	FMCPParamSpec Tagged(const TCHAR* Key, const TArray<FMCPParamVariant>& InVariants) const
	{
		FMCPParamSpec Copy = *this;
		Copy.VariantKey = Key;
		Copy.Variants = InVariants;
		return Copy;
	}
};

namespace MCPParam
{
	inline FMCPParamSpec Required(const TCHAR* Name, EMCPParamType Type, const TCHAR* Description)
	{
		FMCPParamSpec Spec;
		Spec.Name = Name;
		Spec.Type = Type;
		Spec.bRequired = true;
		Spec.Description = Description;
		return Spec;
	}

	inline FMCPParamSpec Optional(const TCHAR* Name, EMCPParamType Type, const TCHAR* Description)
	{
		FMCPParamSpec Spec = Required(Name, Type, Description);
		Spec.bRequired = false;
		return Spec;
	}

	inline FMCPParamField RequiredField(const TCHAR* Name, EMCPParamType Type, const TCHAR* Description)
	{
		FMCPParamField Field;
		Field.Name = Name;
		Field.Type = Type;
		Field.bRequired = true;
		Field.Description = Description;
		return Field;
	}

	inline FMCPParamField OptionalField(const TCHAR* Name, EMCPParamType Type, const TCHAR* Description)
	{
		FMCPParamField Field = RequiredField(Name, Type, Description);
		Field.bRequired = false;
		return Field;
	}

	/** One variant of a tagged union: `MCPParam::Variant(TEXT("set"), TEXT("..."), { fields })`. */
	inline FMCPParamVariant Variant(const TCHAR* Tag, const TCHAR* Description, const TArray<FMCPParamField>& Fields)
	{
		FMCPParamVariant Result;
		Result.Tag = Tag;
		Result.Description = Description;
		Result.Fields = Fields;
		return Result;
	}
}

/** How many branches of a choice a call supplies. */
enum class EMCPChoiceMode : uint8
{
	/** One branch, never two: `actorLabel OR actorPath`. */
	ExactlyOne,
	/** One branch or more: `at least one of labelPrefix/tag`. */
	AtLeastOne,
};

/**
 * A required choice between parameters. Each branch is a set of names that go
 * together, so `settings OR propertyName + propertyValue` has the branches
 * {settings} and {propertyName, propertyValue}. Every name is a declared,
 * optional parameter: what is required is the group.
 */
struct FMCPParamChoice
{
	EMCPChoiceMode Mode = EMCPChoiceMode::ExactlyOne;
	TArray<TArray<FString>> Branches;
};

/**
 * What a spec says beyond its parameter list: its choices, and whether the
 * contract test may call it. Passed as the last argument of RegisterHandler.
 */
struct FMCPSpecRules
{
	TArray<FMCPParamChoice> Choices;
	/** Why the contract test must not call this handler. Empty when it may. */
	FString ContractExemptReason;

	FMCPSpecRules ExactlyOne(const TArray<TArray<FString>>& Branches) const
	{
		FMCPSpecRules Copy = *this;
		FMCPParamChoice Choice;
		Choice.Mode = EMCPChoiceMode::ExactlyOne;
		Choice.Branches = Branches;
		Copy.Choices.Add(MoveTemp(Choice));
		return Copy;
	}

	FMCPSpecRules AtLeastOne(const TArray<TArray<FString>>& Branches) const
	{
		FMCPSpecRules Copy = *this;
		FMCPParamChoice Choice;
		Choice.Mode = EMCPChoiceMode::AtLeastOne;
		Choice.Branches = Branches;
		Copy.Choices.Add(MoveTemp(Choice));
		return Copy;
	}

	/** The contract values would reach a create, spawn, save or run before anything
	 *  failed. The spec is still published and generates the surface; the unit test
	 *  holds the handler source to it instead of the contract call. */
	FMCPSpecRules ContractExempt(const TCHAR* Reason) const
	{
		FMCPSpecRules Copy = *this;
		Copy.ContractExemptReason = Reason;
		return Copy;
	}
};

namespace MCPSpec
{
	/** `MCPSpec::ExactlyOne({ { TEXT("settings") }, { TEXT("propertyName"), TEXT("propertyValue") } })` */
	inline FMCPSpecRules ExactlyOne(const TArray<TArray<FString>>& Branches)
	{
		return FMCPSpecRules().ExactlyOne(Branches);
	}

	inline FMCPSpecRules AtLeastOne(const TArray<TArray<FString>>& Branches)
	{
		return FMCPSpecRules().AtLeastOne(Branches);
	}

	inline FMCPSpecRules ContractExempt(const TCHAR* Reason)
	{
		return FMCPSpecRules().ContractExempt(Reason);
	}
}

/** A handler's declared parameters. Present, even when empty, only for a handler registered with one. */
struct FMCPHandlerSpec
{
	TArray<FMCPParamSpec> Params;
	TArray<FMCPParamChoice> Choices;
	/** Non-empty when the contract test must not call the handler, saying why. */
	FString ContractExemptReason;
};

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

	// Categories whose handlers report the parameters they never read (#1057).
	static bool ReportsUnreadParams(const FString& Category);

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

	// True for a name a spec may declare: letters, digits and underscores, not
	// starting with a digit.
	static bool IsParamIdentifier(const FString& Name);

	// Lowercase wire name of a value form: argMap, argEntryList, stringList, string.
	static const TCHAR* ValueFormName(EMCPValueForm Form);

	// Lowercase wire name of a choice mode: exactlyOne, atLeastOne.
	static const TCHAR* ChoiceModeName(EMCPChoiceMode Mode);

	// Lowercase wire name of a parameter type.
	static const TCHAR* ParamTypeName(EMCPParamType Type);

	// Specs of every handler registered with one, keyed by method name.
	const TMap<FString, FMCPHandlerSpec>& GetHandlerSpecs() const { return HandlerSpecs; }

	// { method: { category?, params: [...], choices?, contractExempt? } } for the capabilities payload.
	TSharedPtr<FJsonObject> BuildHandlerSpecsJson() const;

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
};
