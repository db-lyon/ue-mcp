#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

// Public so plugin modules can declare the same contract for UEMCP::RegisterExternalHandler (#1282).

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
	/** { name: value }, each value a string, number, boolean or null. */
	ScalarMap,
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
	/** The only strings a String field (or each element of an Array of strings) accepts. */
	TArray<FString> EnumValues;
	/** Inclusive bounds of a Number or Integer field (or of each element of an Array of them). */
	TOptional<double> Minimum;
	TOptional<double> Maximum;
	/** The shape of an Object field, or of each element of an Array of objects. */
	TArray<FMCPParamField> Fields;

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

	FMCPParamField Enum(const TArray<FString>& InValues) const
	{
		FMCPParamField Copy = *this;
		Copy.EnumValues = InValues;
		return Copy;
	}

	FMCPParamField Min(double InMinimum) const
	{
		FMCPParamField Copy = *this;
		Copy.Minimum = InMinimum;
		return Copy;
	}

	FMCPParamField Max(double InMaximum) const
	{
		FMCPParamField Copy = *this;
		Copy.Maximum = InMaximum;
		return Copy;
	}

	FMCPParamField Range(double InMinimum, double InMaximum) const
	{
		return Min(InMinimum).Max(InMaximum);
	}

	FMCPParamField WithFields(const TArray<FMCPParamField>& InFields) const
	{
		FMCPParamField Copy = *this;
		Copy.Fields = InFields;
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
	/** The only strings a String parameter (or each element of an Array of strings) accepts. */
	TArray<FString> EnumValues;
	/** Inclusive bounds of a Number or Integer parameter (or of each element of an Array of them). */
	TOptional<double> Minimum;
	TOptional<double> Maximum;
	/** What the handler uses when the parameter is omitted. Published so the client never restates it. */
	TSharedPtr<FJsonValue> DefaultValue;

	FMCPParamSpec Enum(const TArray<FString>& InValues) const
	{
		FMCPParamSpec Copy = *this;
		Copy.EnumValues = InValues;
		return Copy;
	}

	FMCPParamSpec Default(bool bValue) const
	{
		FMCPParamSpec Copy = *this;
		Copy.DefaultValue = MakeShared<FJsonValueBoolean>(bValue);
		return Copy;
	}

	FMCPParamSpec Default(double Value) const
	{
		FMCPParamSpec Copy = *this;
		Copy.DefaultValue = MakeShared<FJsonValueNumber>(Value);
		return Copy;
	}

	FMCPParamSpec Default(int32 Value) const
	{
		return Default((double)Value);
	}

	FMCPParamSpec Default(const TCHAR* Value) const
	{
		FMCPParamSpec Copy = *this;
		Copy.DefaultValue = MakeShared<FJsonValueString>(FString(Value));
		return Copy;
	}

	/** The same parameter with a description that fits the handler declaring it. */
	FMCPParamSpec Describe(const FString& InDescription) const
	{
		FMCPParamSpec Copy = *this;
		Copy.Description = InDescription;
		return Copy;
	}

	FMCPParamSpec Min(double InMinimum) const
	{
		FMCPParamSpec Copy = *this;
		Copy.Minimum = InMinimum;
		return Copy;
	}

	FMCPParamSpec Max(double InMaximum) const
	{
		FMCPParamSpec Copy = *this;
		Copy.Maximum = InMaximum;
		return Copy;
	}

	FMCPParamSpec Range(double InMinimum, double InMaximum) const
	{
		return Min(InMinimum).Max(InMaximum);
	}

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
 * How the bridge runs an action, declared where it registers and published in
 * get_bridge_capabilities.actionFacts, so the client reads it instead of keeping
 * a copy. Every field's zero value is the common case.
 */
struct FMCPActionFacts
{
	/** Game-thread budget in seconds. 0 is the bridge default. */
	float TimeoutSeconds = 0.0f;
	/** Runs while a modal dialog holds the game thread: it reads or answers that dialog. */
	bool bModalSafe = false;
	/** Answered on the socket thread, before the dialog gate and the game thread. */
	bool bServedOffGameThread = false;
	/** Left out of the parameter echo, so reading the echo does not change it. */
	bool bNotEchoed = false;
	/** Why execute_batch refuses to carry this action. Empty when it may. */
	FString RefusedInBatchReason;
	/** Who calls this action instead of a tool. Empty for an action a tool exposes. */
	FString ServerInternalCaller;

	bool IsDefault() const
	{
		return TimeoutSeconds <= 0.0f && !bModalSafe && !bServedOffGameThread && !bNotEchoed
			&& RefusedInBatchReason.IsEmpty() && ServerInternalCaller.IsEmpty();
	}
};

/**
 * What a spec says beyond its parameter list: its choices, whether the
 * contract test may call it, and its action facts. Passed as the last argument
 * of RegisterHandler.
 */
struct FMCPSpecRules
{
	TArray<FMCPParamChoice> Choices;
	/** Why the contract test must not call this handler. Empty when it may. */
	FString ContractExemptReason;
	FMCPActionFacts Facts;

	FMCPSpecRules Timeout(float Seconds) const
	{
		FMCPSpecRules Copy = *this;
		Copy.Facts.TimeoutSeconds = Seconds;
		return Copy;
	}

	FMCPSpecRules ModalSafe() const
	{
		FMCPSpecRules Copy = *this;
		Copy.Facts.bModalSafe = true;
		return Copy;
	}

	FMCPSpecRules ServedOffGameThread() const
	{
		FMCPSpecRules Copy = *this;
		Copy.Facts.bServedOffGameThread = true;
		return Copy;
	}

	FMCPSpecRules NotEchoed() const
	{
		FMCPSpecRules Copy = *this;
		Copy.Facts.bNotEchoed = true;
		return Copy;
	}

	FMCPSpecRules RefusedInBatch(const TCHAR* Reason) const
	{
		FMCPSpecRules Copy = *this;
		Copy.Facts.RefusedInBatchReason = Reason;
		return Copy;
	}

	/** `ServerInternal(TEXT("the server's checkpoint service"))`: no tool exposes it. */
	FMCPSpecRules ServerInternal(const TCHAR* Caller) const
	{
		FMCPSpecRules Copy = *this;
		Copy.Facts.ServerInternalCaller = Caller;
		return Copy;
	}

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

	inline FMCPSpecRules Timeout(float Seconds)
	{
		return FMCPSpecRules().Timeout(Seconds);
	}

	inline FMCPSpecRules ModalSafe()
	{
		return FMCPSpecRules().ModalSafe();
	}

	inline FMCPSpecRules RefusedInBatch(const TCHAR* Reason)
	{
		return FMCPSpecRules().RefusedInBatch(Reason);
	}

	inline FMCPSpecRules ServerInternal(const TCHAR* Caller)
	{
		return FMCPSpecRules().ServerInternal(Caller);
	}
}

/** A handler's declared parameters. Present, even when empty, only for a handler registered with one. */
struct FMCPHandlerSpec
{
	TArray<FMCPParamSpec> Params;
	TArray<FMCPParamChoice> Choices;
	/** Non-empty when the contract test must not call the handler, saying why. */
	FString ContractExemptReason;
	FMCPActionFacts Facts;
};
