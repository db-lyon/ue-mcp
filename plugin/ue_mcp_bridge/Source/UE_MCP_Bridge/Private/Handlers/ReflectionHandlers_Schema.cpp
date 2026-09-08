// reflect_instance - the per-instance writable schema (T1).
//
// reflect_class answers the CLASS question: what does a UMyThing have. This
// answers the INSTANCE question: given this concrete asset, CDO or subobject,
// what may I write on it right now, what type does each write take, and what
// will the engine accept as a value.
//
// That difference is the whole point. A class-level answer says a property is
// `EditAnywhere` and has a ClampMin; it cannot say that this particular
// instance has the property greyed out because its EditCondition is currently
// false, that the object reference only accepts three classes, that the array
// is EditFixedSize, or what the value is at this moment. Every one of those is
// a write that fails, or silently does nothing, if the caller has to find out
// by trying.
//
// The output shape and vocabulary deliberately mirror reflect_class: the same
// `properties` array, the same `name` / `type` / `tooltip` / `category` /
// `displayName` / `editCondition` / `clampMin` / `clampMax` / `uiMin` /
// `uiMax` / `units` fields, and the same `flags` array spelled the way a
// UPROPERTY author would have typed them. Everything this action adds is
// instance state that reflect_class has no way to know.
//
// Paged through Public/HandlerPagination.h, because a UObject with inherited
// properties runs to several hundred rows.

#include "ReflectionHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "HandlerJsonProperty.h"
#include "JsonSerializer.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/PropertyPortFlags.h"
#include "Engine/Blueprint.h"
#include "EditorAssetLibrary.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// A NAMED namespace, not an anonymous one. The module is a unity build, so an
// anonymous namespace here merges with every other .cpp in the same blob and a
// helper sharing a name with one of theirs is a redefinition (C2084). The
// grouping shifts with file count and file order, so that failure appears on
// another machine rather than this one.
namespace MCPReflectionSchema
{
	/** Which half of the editable-flag pair applies to the object in hand.
	 *  EditDefaultsOnly is writable on a CDO and greyed out on a placed
	 *  instance; EditInstanceOnly is the reverse. Answering that needs to know
	 *  which of the two the caller resolved. */
	enum class EInstanceContext : uint8
	{
		Defaults,
		Instance,
	};

	inline const TCHAR* ContextName(EInstanceContext Context)
	{
		return Context == EInstanceContext::Defaults ? TEXT("defaults") : TEXT("instance");
	}

	inline void SchemaAddIfNonEmpty(TSharedPtr<FJsonObject> Obj, const TCHAR* Key, const FString& Value)
	{
		if (!Value.IsEmpty())
		{
			Obj->SetStringField(Key, Value);
		}
	}

	inline FString SchemaMeta(const FProperty* Prop, const TCHAR* Key)
	{
#if WITH_EDITOR
		return Prop ? Prop->GetMetaData(Key) : FString();
#else
		return FString();
#endif
	}

	/** Presence of a bare metadata key. EditConditionHides and friends are
	 *  written with no value, so an empty-string test would read them as
	 *  absent. */
	inline bool SchemaHasMeta(const FProperty* Prop, const TCHAR* Key)
	{
#if WITH_EDITOR
		return Prop && Prop->HasMetaData(Key);
#else
		return false;
#endif
	}

	/** The UPROPERTY specifier names for a property's flags. Same vocabulary
	 *  reflect_class emits, so one result can be read against the other. */
	inline TArray<TSharedPtr<FJsonValue>> SchemaEncodePropertyFlags(const FProperty* Prop)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		if (!Prop) return Out;
		const EPropertyFlags F = Prop->PropertyFlags;
		auto Push = [&Out](const TCHAR* Name) { Out.Add(MakeShared<FJsonValueString>(Name)); };

		if (F & CPF_Edit)
		{
			if (F & CPF_DisableEditOnInstance) Push(TEXT("EditDefaultsOnly"));
			else if (F & CPF_DisableEditOnTemplate) Push(TEXT("EditInstanceOnly"));
			else Push(TEXT("EditAnywhere"));
		}
		if (F & CPF_EditConst)       Push(TEXT("VisibleAnywhere"));
		if (F & CPF_BlueprintVisible)
		{
			if (F & CPF_BlueprintReadOnly) Push(TEXT("BlueprintReadOnly"));
			else Push(TEXT("BlueprintReadWrite"));
		}
		if (F & CPF_Net)             Push(TEXT("Replicated"));
		if (F & CPF_RepNotify)       Push(TEXT("RepNotify"));
		if (F & CPF_Transient)       Push(TEXT("Transient"));
		if (F & CPF_Config)          Push(TEXT("Config"));
		if (F & CPF_GlobalConfig)    Push(TEXT("GlobalConfig"));
		if (F & CPF_SaveGame)        Push(TEXT("SaveGame"));
		if (F & CPF_Interp)          Push(TEXT("Interp"));
		if (F & CPF_AdvancedDisplay) Push(TEXT("AdvancedDisplay"));
		if (F & CPF_Deprecated)      Push(TEXT("Deprecated"));
		if (F & CPF_NoClear)         Push(TEXT("NoClear"));
		if (F & CPF_ExposeOnSpawn)   Push(TEXT("ExposeOnSpawn"));
		if (F & CPF_EditFixedSize)   Push(TEXT("EditFixedSize"));
		if (F & CPF_InstancedReference) Push(TEXT("Instanced"));
		return Out;
	}

	// ── EditCondition ────────────────────────────────────────────────────────
	//
	// The details panel greys a property out when its EditCondition metadata
	// evaluates false against the object in front of it. That verdict is
	// instance state, so it is exactly what this action exists to report.
	//
	// The engine's own parser lives in PropertyEditor's PRIVATE headers, and
	// the one public entry point (EditConditionEvaluator::IsPropertyEditable)
	// is UE 5.8-only, marked experimental, and takes a UScriptStruct rather
	// than a UObject. This plugin builds from 5.4, so the expression is
	// evaluated here instead, over the forms UPROPERTY authors actually write:
	//
	//   bEnabled            !bEnabled
	//   Mode == EMode::Two  Mode != EMode::Two
	//   Count > 0           Scale <= 1.0
	//   bA && Mode == EMode::Two          bA || bB
	//
	// Anything else - a function call, parentheses, a mix of && and || in one
	// expression, an operand that is not a property on this container - FAILS
	// OPEN: `editConditionMet` comes back true and `editConditionEvaluated`
	// comes back false with a note saying why. Reporting a property as locked
	// on the strength of an expression this code did not understand would send
	// a caller away from a write that works, which is worse than admitting the
	// expression was not read.

	struct FEditConditionVerdict
	{
		bool bMet = true;
		bool bEvaluated = false;
		FString Note;
	};

	/** Read a property on this container as a number: bools as 0/1, enums as
	 *  their underlying integer, everything numeric as itself. */
	inline bool SchemaReadOperand(
		const UStruct* Struct, const void* Container, const FString& Name,
		double& OutValue, FProperty*& OutProp)
	{
		if (!Struct || !Container) return false;
		FProperty* Prop = Struct->FindPropertyByName(FName(*Name));
		if (!Prop) return false;
		const void* Addr = Prop->ContainerPtrToValuePtr<void>(Container);
		if (!Addr) return false;

		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			OutValue = BoolProp->GetPropertyValue(Addr) ? 1.0 : 0.0;
			OutProp = Prop;
			return true;
		}
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			OutValue = static_cast<double>(
				EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(Addr));
			OutProp = Prop;
			return true;
		}
		if (const FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
		{
			OutValue = NumProp->IsFloatingPoint()
				? NumProp->GetFloatingPointPropertyValue(Addr)
				: static_cast<double>(NumProp->GetSignedIntPropertyValue(Addr));
			OutProp = Prop;
			return true;
		}
		return false;
	}

	/** The UEnum behind an enum-typed property, in either of its two shapes. */
	inline UEnum* SchemaEnumOf(const FProperty* Prop)
	{
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop)) return EnumProp->GetEnum();
		if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop)) return ByteProp->Enum;
		return nullptr;
	}

	/** Turn the right-hand side of a comparison into a number. Accepts a
	 *  numeric literal, true/false, and an enumerator written either fully
	 *  qualified (EMode::Two) or bare (Two). */
	inline bool SchemaReadLiteral(const FProperty* LhsProp, const FString& InLiteral, double& OutValue)
	{
		const FString Literal = InLiteral.TrimStartAndEnd();
		if (Literal.IsEmpty()) return false;
		if (Literal.Equals(TEXT("true"), ESearchCase::IgnoreCase))  { OutValue = 1.0; return true; }
		if (Literal.Equals(TEXT("false"), ESearchCase::IgnoreCase)) { OutValue = 0.0; return true; }
		if (Literal.IsNumeric()) { OutValue = FCString::Atod(*Literal); return true; }

		if (UEnum* Enum = SchemaEnumOf(LhsProp))
		{
			int64 Value = Enum->GetValueByNameString(Literal);
			if (Value == INDEX_NONE)
			{
				int32 Sep = INDEX_NONE;
				if (Literal.FindLastChar(TEXT(':'), Sep) && Sep + 1 < Literal.Len())
				{
					Value = Enum->GetValueByNameString(Literal.Mid(Sep + 1));
				}
			}
			if (Value != INDEX_NONE) { OutValue = static_cast<double>(Value); return true; }
		}
		return false;
	}

	/** One comparison or one bare/negated bool. Returns false when the atom
	 *  could not be read, which fails the whole expression open. */
	inline bool SchemaEvaluateAtom(
		const FString& InAtom, const UStruct* Struct, const void* Container,
		bool& OutValue, FString& OutWhy)
	{
		FString Atom = InAtom.TrimStartAndEnd();
		if (Atom.IsEmpty()) { OutWhy = TEXT("empty term"); return false; }
		if (Atom.Contains(TEXT("(")) || Atom.Contains(TEXT(")")))
		{
			OutWhy = TEXT("parentheses and function calls are not evaluated here");
			return false;
		}

		static const TCHAR* Operators[] = { TEXT("=="), TEXT("!="), TEXT(">="), TEXT("<="), TEXT(">"), TEXT("<") };
		for (const TCHAR* Op : Operators)
		{
			const int32 At = Atom.Find(Op, ESearchCase::CaseSensitive, ESearchDir::FromStart, 0);
			if (At <= 0) continue;

			const FString Lhs = Atom.Left(At).TrimStartAndEnd();
			const FString Rhs = Atom.Mid(At + FCString::Strlen(Op)).TrimStartAndEnd();

			double LhsValue = 0.0;
			FProperty* LhsProp = nullptr;
			if (!SchemaReadOperand(Struct, Container, Lhs, LhsValue, LhsProp))
			{
				OutWhy = FString::Printf(TEXT("'%s' is not a readable property on this object"), *Lhs);
				return false;
			}
			double RhsValue = 0.0;
			if (!SchemaReadLiteral(LhsProp, Rhs, RhsValue))
			{
				double RhsProperty = 0.0;
				FProperty* Ignored = nullptr;
				if (!SchemaReadOperand(Struct, Container, Rhs, RhsProperty, Ignored))
				{
					OutWhy = FString::Printf(TEXT("'%s' is neither a literal nor a readable property"), *Rhs);
					return false;
				}
				RhsValue = RhsProperty;
			}

			if (FCString::Strcmp(Op, TEXT("==")) == 0)      OutValue = FMath::IsNearlyEqual(LhsValue, RhsValue);
			else if (FCString::Strcmp(Op, TEXT("!=")) == 0) OutValue = !FMath::IsNearlyEqual(LhsValue, RhsValue);
			else if (FCString::Strcmp(Op, TEXT(">=")) == 0) OutValue = LhsValue >= RhsValue;
			else if (FCString::Strcmp(Op, TEXT("<=")) == 0) OutValue = LhsValue <= RhsValue;
			else if (FCString::Strcmp(Op, TEXT(">")) == 0)  OutValue = LhsValue > RhsValue;
			else                                            OutValue = LhsValue < RhsValue;
			return true;
		}

		bool bNegate = false;
		while (Atom.StartsWith(TEXT("!")))
		{
			bNegate = !bNegate;
			Atom = Atom.RightChop(1).TrimStartAndEnd();
		}
		double Value = 0.0;
		FProperty* Prop = nullptr;
		if (!SchemaReadOperand(Struct, Container, Atom, Value, Prop))
		{
			OutWhy = FString::Printf(TEXT("'%s' is not a readable property on this object"), *Atom);
			return false;
		}
		OutValue = bNegate ? (Value == 0.0) : (Value != 0.0);
		return true;
	}

	inline FEditConditionVerdict SchemaEvaluateEditCondition(
		const FString& Expression, const UStruct* Struct, const void* Container)
	{
		FEditConditionVerdict Verdict;
		const FString Expr = Expression.TrimStartAndEnd();
		if (Expr.IsEmpty()) return Verdict;

		const bool bHasOr = Expr.Contains(TEXT("||"));
		const bool bHasAnd = Expr.Contains(TEXT("&&"));
		if (bHasOr && bHasAnd)
		{
			Verdict.Note = TEXT("EditCondition mixes && and || in one expression, which is not evaluated here; ")
				TEXT("treated as met. Read 'editCondition' and decide before writing.");
			return Verdict;
		}

		TArray<FString> Terms;
		Expr.ParseIntoArray(Terms, bHasOr ? TEXT("||") : TEXT("&&"));
		if (Terms.Num() == 0)
		{
			Verdict.Note = TEXT("EditCondition could not be split into terms; treated as met.");
			return Verdict;
		}

		bool bResult = !bHasOr;
		for (const FString& Term : Terms)
		{
			bool bTerm = false;
			FString Why;
			if (!SchemaEvaluateAtom(Term, Struct, Container, bTerm, Why))
			{
				Verdict.Note = FString::Printf(
					TEXT("EditCondition '%s' was not evaluated (%s); treated as met."), *Expr, *Why);
				return Verdict;
			}
			bResult = bHasOr ? (bResult || bTerm) : (bResult && bTerm);
		}

		Verdict.bEvaluated = true;
		Verdict.bMet = bResult;
		if (!bResult)
		{
			Verdict.Note = FString::Printf(
				TEXT("EditCondition '%s' is false on this object, so the details panel greys this property out. ")
				TEXT("Satisfy the condition first, or write it anyway with editor(set_property), which goes ")
				TEXT("through reflection and does not consult the condition."), *Expr);
		}
		return Verdict;
	}

	// ── Type and constraint description ──────────────────────────────────────

	/** Every enumerator of an enum-typed property, in the shape reflect_enum
	 *  returns them, so one result reads like the other. */
	inline TArray<TSharedPtr<FJsonValue>> SchemaEnumValues(UEnum* Enum)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		if (!Enum) return Out;
		const int32 Num = Enum->NumEnums();
		for (int32 i = 0; i < Num - 1; ++i) // the trailing _MAX entry is not a value
		{
			const FString Name = Enum->GetNameStringByIndex(i);
			if (Name.IsEmpty() || Name.EndsWith(TEXT("_MAX"))) continue;
#if WITH_EDITOR
			if (Enum->HasMetaData(TEXT("Hidden"), i)) continue;
#endif
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("name"), Name);
			Entry->SetNumberField(TEXT("value"), static_cast<double>(Enum->GetValueByIndex(i)));
			Entry->SetStringField(TEXT("displayName"), Enum->GetDisplayNameTextByIndex(i).ToString());
			Out.Add(MakeShared<FJsonValueObject>(Entry));
		}
		return Out;
	}

	/** Split a comma-separated metadata class list into JSON strings. */
	inline TArray<TSharedPtr<FJsonValue>> SchemaSplitClassList(const FString& List)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		if (List.IsEmpty()) return Out;
		TArray<FString> Names;
		List.ParseIntoArray(Names, TEXT(","));
		for (FString& Name : Names)
		{
			Name.TrimStartAndEndInline();
			if (!Name.IsEmpty()) Out.Add(MakeShared<FJsonValueString>(Name));
		}
		return Out;
	}

	/**
	 * What a write to this property has to look like: the type, and everything
	 * that narrows the set of values the engine will accept.
	 *
	 * Recurses into containers (array element, set element, map key and value)
	 * and into struct field layout, bounded by MaxDepth so a self-referential
	 * struct cannot run away. At the bound the descriptor still names the type;
	 * only the expansion stops, and `fieldsTruncated` says so.
	 */
	inline TSharedPtr<FJsonObject> SchemaDescribeType(FProperty* Prop, int32 Depth, int32 MaxDepth)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		if (!Prop) return Obj;

		Obj->SetStringField(TEXT("type"), Prop->GetCPPType());
		Obj->SetStringField(TEXT("kind"), Prop->GetClass()->GetName());
		if (MCPPropertyIsFixedArray(Prop))
		{
			// A C-style `int32 Foo[3]` is ONE property with ArrayDim 3, and its
			// elements are addressed as Foo[0]..Foo[2] by set_property.
			Obj->SetNumberField(TEXT("fixedArrayDim"), Prop->ArrayDim);
		}

		if (UEnum* Enum = SchemaEnumOf(Prop))
		{
			Obj->SetStringField(TEXT("enumName"), Enum->GetName());
			Obj->SetStringField(TEXT("enumPath"), Enum->GetPathName());
			Obj->SetArrayField(TEXT("enumValues"), SchemaEnumValues(Enum));
		}

		if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
		{
			if (ObjProp->PropertyClass)
			{
				Obj->SetStringField(TEXT("objectClass"), ObjProp->PropertyClass->GetName());
				Obj->SetStringField(TEXT("objectClassPath"), ObjProp->PropertyClass->GetPathName());
			}
			if (const FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
			{
				if (ClassProp->MetaClass) Obj->SetStringField(TEXT("metaClass"), ClassProp->MetaClass->GetPathName());
			}
			else if (const FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Prop))
			{
				if (SoftClassProp->MetaClass) Obj->SetStringField(TEXT("metaClass"), SoftClassProp->MetaClass->GetPathName());
			}
			const TArray<TSharedPtr<FJsonValue>> Allowed = SchemaSplitClassList(SchemaMeta(Prop, TEXT("AllowedClasses")));
			if (Allowed.Num() > 0) Obj->SetArrayField(TEXT("allowedClasses"), Allowed);
			const TArray<TSharedPtr<FJsonValue>> Disallowed = SchemaSplitClassList(SchemaMeta(Prop, TEXT("DisallowedClasses")));
			if (Disallowed.Num() > 0) Obj->SetArrayField(TEXT("disallowedClasses"), Disallowed);
		}
		else if (const FInterfaceProperty* InterfaceProp = CastField<FInterfaceProperty>(Prop))
		{
			if (InterfaceProp->InterfaceClass)
			{
				Obj->SetStringField(TEXT("interfaceClass"), InterfaceProp->InterfaceClass->GetPathName());
			}
		}

		if (Depth >= MaxDepth)
		{
			if (Prop->IsA<FArrayProperty>() || Prop->IsA<FSetProperty>() ||
				Prop->IsA<FMapProperty>() || Prop->IsA<FStructProperty>())
			{
				Obj->SetBoolField(TEXT("fieldsTruncated"), true);
			}
			return Obj;
		}

		if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			Obj->SetObjectField(TEXT("elementType"), SchemaDescribeType(ArrayProp->Inner, Depth + 1, MaxDepth));
		}
		else if (FSetProperty* SetProp = CastField<FSetProperty>(Prop))
		{
			Obj->SetObjectField(TEXT("elementType"), SchemaDescribeType(SetProp->ElementProp, Depth + 1, MaxDepth));
		}
		else if (FMapProperty* MapProp = CastField<FMapProperty>(Prop))
		{
			Obj->SetObjectField(TEXT("keyType"), SchemaDescribeType(MapProp->KeyProp, Depth + 1, MaxDepth));
			Obj->SetObjectField(TEXT("valueType"), SchemaDescribeType(MapProp->ValueProp, Depth + 1, MaxDepth));
		}
		else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			if (StructProp->Struct)
			{
				Obj->SetStringField(TEXT("structName"), StructProp->Struct->GetName());
				Obj->SetStringField(TEXT("structPath"), StructProp->Struct->GetPathName());
				TArray<TSharedPtr<FJsonValue>> Fields;
				for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
				{
					TSharedPtr<FJsonObject> Field = SchemaDescribeType(*It, Depth + 1, MaxDepth);
					Field->SetStringField(TEXT("name"), It->GetName());
					Fields.Add(MakeShared<FJsonValueObject>(Field));
				}
				Obj->SetArrayField(TEXT("fields"), Fields);
			}
		}

		return Obj;
	}

	/** True when the reflection setter behind asset/editor(set_property) has no
	 *  way to write this property at all. Delegates are bound in a Blueprint
	 *  graph or in C++, never by value, and the import-text fallback cannot
	 *  make one. Everything else the setter reaches. */
	inline bool SchemaIsSettableKind(const FProperty* Prop)
	{
		return Prop
			&& !Prop->IsA<FDelegateProperty>()
			&& !Prop->IsA<FMulticastDelegateProperty>();
	}

	/** How many entries a container currently holds, or INDEX_NONE when the
	 *  property is not a container. */
	inline int32 SchemaElementCount(FProperty* Prop, const void* ValueAddr)
	{
		if (!Prop || !ValueAddr) return INDEX_NONE;
		if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
		{
			return FScriptArrayHelper(ArrayProp, ValueAddr).Num();
		}
		if (FSetProperty* SetProp = CastField<FSetProperty>(Prop))
		{
			return FScriptSetHelper(SetProp, ValueAddr).Num();
		}
		if (FMapProperty* MapProp = CastField<FMapProperty>(Prop))
		{
			return FScriptMapHelper(MapProp, ValueAddr).Num();
		}
		return INDEX_NONE;
	}

	/**
	 * One property, as this instance has it right now.
	 *
	 * `Container` is the memory the property is addressed against and
	 * `ContainerStruct` its layout: the resolved UObject and its UClass at the
	 * top level, or a nested struct's storage when the caller scoped the read
	 * with propertyPath.
	 */
	inline TSharedPtr<FJsonObject> SchemaDescribeInstanceProperty(
		FProperty* Prop,
		const UStruct* ContainerStruct,
		const void* Container,
		UObject* Owner,
		EInstanceContext Context,
		const FString& PathPrefix,
		bool bIncludeValues,
		int32 MaxDepth)
	{
		TSharedPtr<FJsonObject> Obj = SchemaDescribeType(Prop, 0, MaxDepth);
		Obj->SetStringField(TEXT("name"), Prop->GetName());
		Obj->SetStringField(TEXT("path"), PathPrefix.IsEmpty()
			? Prop->GetName()
			: PathPrefix + TEXT(".") + Prop->GetName());

#if WITH_EDITOR
		SchemaAddIfNonEmpty(Obj, TEXT("tooltip"),       Prop->GetMetaData(TEXT("ToolTip")).TrimStartAndEnd());
		SchemaAddIfNonEmpty(Obj, TEXT("category"),      Prop->GetMetaData(TEXT("Category")));
		SchemaAddIfNonEmpty(Obj, TEXT("displayName"),   Prop->GetMetaData(TEXT("DisplayName")));
		SchemaAddIfNonEmpty(Obj, TEXT("clampMin"),      Prop->GetMetaData(TEXT("ClampMin")));
		SchemaAddIfNonEmpty(Obj, TEXT("clampMax"),      Prop->GetMetaData(TEXT("ClampMax")));
		SchemaAddIfNonEmpty(Obj, TEXT("uiMin"),         Prop->GetMetaData(TEXT("UIMin")));
		SchemaAddIfNonEmpty(Obj, TEXT("uiMax"),         Prop->GetMetaData(TEXT("UIMax")));
		SchemaAddIfNonEmpty(Obj, TEXT("units"),         Prop->GetMetaData(TEXT("Units")));
#endif

		const TArray<TSharedPtr<FJsonValue>> Flags = SchemaEncodePropertyFlags(Prop);
		if (Flags.Num() > 0) Obj->SetArrayField(TEXT("flags"), Flags);

		// ── the edit condition, against this object ──
		const FString EditCondition = SchemaMeta(Prop, TEXT("EditCondition"));
		FEditConditionVerdict Verdict;
		if (!EditCondition.IsEmpty())
		{
			Obj->SetStringField(TEXT("editCondition"), EditCondition);
			Obj->SetBoolField(TEXT("editConditionHides"), SchemaHasMeta(Prop, TEXT("EditConditionHides")));
			Verdict = SchemaEvaluateEditCondition(EditCondition, ContainerStruct, Container);
			Obj->SetBoolField(TEXT("editConditionMet"), Verdict.bMet);
			Obj->SetBoolField(TEXT("editConditionEvaluated"), Verdict.bEvaluated);
			SchemaAddIfNonEmpty(Obj, TEXT("editConditionNote"), Verdict.Note);
		}

		// ── editable, here, now ──
		//
		// `editable` is the details-panel verdict for THIS object: the flags
		// plus the edit condition. `settable` is the separate and larger
		// question of whether the bridge's reflection setter can write it at
		// all, which is what a caller planning an asset(set_property) actually
		// needs. They differ on purpose, and the reason is reported.
		FString NotEditable;
		if (!Prop->HasAnyPropertyFlags(CPF_Edit))
		{
			NotEditable = TEXT("not marked EditAnywhere / EditDefaultsOnly / EditInstanceOnly, so the details panel never shows it");
		}
		else if (Prop->HasAnyPropertyFlags(CPF_EditConst))
		{
			NotEditable = TEXT("VisibleAnywhere / EditConst: shown but read-only in the details panel");
		}
		else if (Context == EInstanceContext::Defaults && Prop->HasAnyPropertyFlags(CPF_DisableEditOnTemplate))
		{
			NotEditable = TEXT("EditInstanceOnly, and this path resolved to class defaults rather than a placed instance");
		}
		else if (Context == EInstanceContext::Instance && Prop->HasAnyPropertyFlags(CPF_DisableEditOnInstance))
		{
			NotEditable = TEXT("EditDefaultsOnly, and this path resolved to an instance rather than the class defaults");
		}
		else if (!Verdict.bMet)
		{
			NotEditable = Verdict.Note;
		}
		Obj->SetBoolField(TEXT("editable"), NotEditable.IsEmpty());
		SchemaAddIfNonEmpty(Obj, TEXT("notEditableReason"), NotEditable);

		const bool bSettable = SchemaIsSettableKind(Prop);
		Obj->SetBoolField(TEXT("settable"), bSettable);
		if (!bSettable)
		{
			Obj->SetStringField(TEXT("notSettableReason"),
				TEXT("delegate properties are bound in a Blueprint graph or in C++, never written as a value"));
		}
		if (Prop->HasAnyPropertyFlags(CPF_Deprecated))
		{
			Obj->SetStringField(TEXT("deprecatedNote"),
				TEXT("marked Deprecated: it still writes, but the engine no longer reads it"));
		}
		if (Prop->HasAnyPropertyFlags(CPF_EditFixedSize))
		{
			Obj->SetStringField(TEXT("fixedSizeNote"),
				TEXT("EditFixedSize: element values are writable but the element count is not"));
		}

		// ── the value, right now ──
		const void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(Container);
		const int32 ElementCount = SchemaElementCount(Prop, ValueAddr);
		if (ElementCount != INDEX_NONE)
		{
			Obj->SetNumberField(TEXT("elementCount"), ElementCount);
		}

		if (bIncludeValues && ValueAddr)
		{
			Obj->SetField(TEXT("value"), FMCPJsonSerializer::SerializeValue(ValueAddr, Prop));
			Obj->SetField(TEXT("valueText"), MCPExportPropertyValue(Prop, Container));

			// An instanced subobject is where most of the configurable surface
			// of a modern asset lives (a Niagara module, an EQS test, a
			// component template). Handing back its object path is what lets a
			// caller aim the next reflect_instance or set_property at it
			// instead of guessing the dotted route.
			if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
			{
				if (UObject* Referenced = ObjProp->GetObjectPropertyValue(ValueAddr))
				{
					Obj->SetStringField(TEXT("valueObjectPath"), Referenced->GetPathName());
					Obj->SetStringField(TEXT("valueObjectClass"), Referenced->GetClass()->GetPathName());
					if (Owner && Referenced->IsIn(Owner))
					{
						Obj->SetBoolField(TEXT("valueIsSubobject"), true);
					}
				}
			}
		}

		return Obj;
	}
}

// ── reflect_instance ─────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FReflectionHandlers::ReflectInstance(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPReflectionSchema;
	MCP_CHECK_GAME_THREAD();

	FString ObjectPath;
	if (auto Err = RequireString(Params, TEXT("objectPath"), ObjectPath)) return Err;

	// Resolution mirrors editor(set_property): the same paths that can be
	// WRITTEN are the paths that can be described, or the schema would be
	// answering about a different object than the write lands on.
	UObject* Object = LoadObject<UObject>(nullptr, *ObjectPath);
	if (!Object) Object = StaticFindObject(UObject::StaticClass(), nullptr, *ObjectPath);
	if (!Object) Object = UEditorAssetLibrary::LoadAsset(ObjectPath);
	if (!Object)
	{
		return MCPError(FString::Printf(
			TEXT("Object not found: %s. Give a full object path (/Game/Path/Asset.Asset), an asset path ")
			TEXT("(/Game/Path/Asset), a Blueprint path (its generated-class defaults are described), or the ")
			TEXT("objectPath of a subobject as returned by a read action."), *ObjectPath));
	}

	FString ResolvedKind = TEXT("object");
	if (UClass* AsClass = Cast<UClass>(Object))
	{
		Object = AsClass->GetDefaultObject();
		ResolvedKind = TEXT("classDefaultObject");
	}
	else if (UBlueprint* AsBlueprint = Cast<UBlueprint>(Object))
	{
		if (!AsBlueprint->GeneratedClass)
		{
			return MCPError(FString::Printf(
				TEXT("Blueprint has no generated class: %s. Compile it first (blueprint(compile)), then read it again."),
				*ObjectPath));
		}
		Object = AsBlueprint->GeneratedClass->GetDefaultObject();
		ResolvedKind = TEXT("blueprintDefaultObject");
	}
	if (!Object)
	{
		return MCPError(FString::Printf(TEXT("Resolved object is null: %s"), *ObjectPath));
	}

	const EInstanceContext Context =
		Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject)
			? EInstanceContext::Defaults
			: EInstanceContext::Instance;

	const FString PropertyPath = OptionalString(Params, TEXT("propertyPath"));
	const FString Filter = OptionalString(Params, TEXT("filter"));
	const bool bIncludeInherited = OptionalBool(Params, TEXT("includeInherited"), true);
	const bool bIncludeValues = OptionalBool(Params, TEXT("includeValues"), true);
	const bool bEditableOnly = OptionalBool(Params, TEXT("editableOnly"), false);

	int32 MaxDepth = OptionalInt(Params, TEXT("maxDepth"), 1);
	if (MaxDepth < 0 || MaxDepth > 5)
	{
		return MCPError(FString::Printf(
			TEXT("'maxDepth' must be between 0 and 5 (got %d). 0 names container and struct types without ")
			TEXT("expanding them; 1 is the default and expands one level of struct fields."), MaxDepth));
	}

	// Scope: the whole object, or the struct / subobject a dotted path names.
	const UStruct* ContainerStruct = Object->GetClass();
	const void* Container = Object;
	UObject* Owner = Object;
	FString ScopeType;

	if (!PropertyPath.IsEmpty())
	{
		FProperty* ScopeProp = nullptr;
		void* ScopeAddr = nullptr;
		UObject* ScopeOwner = nullptr;
		FString ResolveErr;
		if (!MCPJsonProperty::ResolveDottedPath(Object, PropertyPath, ScopeProp, ScopeAddr, ScopeOwner, ResolveErr))
		{
			return MCPError(FString::Printf(
				TEXT("Cannot resolve 'propertyPath' %s on %s: %s. Omit propertyPath to list the object's own ")
				TEXT("properties, then follow the 'path' field of the one you want."),
				*PropertyPath, *Object->GetPathName(), *ResolveErr));
		}

		if (const FStructProperty* StructProp = CastField<FStructProperty>(ScopeProp))
		{
			ContainerStruct = StructProp->Struct;
			Container = ScopeAddr;
			Owner = ScopeOwner ? ScopeOwner : Object;
			ScopeType = StructProp->Struct ? StructProp->Struct->GetPathName() : FString();
		}
		else if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(ScopeProp))
		{
			UObject* Referenced = ObjProp->GetObjectPropertyValue(ScopeAddr);
			if (!Referenced)
			{
				return MCPError(FString::Printf(
					TEXT("'propertyPath' %s is an object reference that is currently null on %s, so it has no ")
					TEXT("instance to describe. Set it first, or point propertyPath at a struct property."),
					*PropertyPath, *Object->GetPathName()));
			}
			ContainerStruct = Referenced->GetClass();
			Container = Referenced;
			Owner = Referenced;
			ScopeType = Referenced->GetClass()->GetPathName();
		}
		else
		{
			return MCPError(FString::Printf(
				TEXT("'propertyPath' %s resolves to a %s, which has no properties of its own. propertyPath ")
				TEXT("must name a struct or an object reference; omit it to describe the object itself."),
				*PropertyPath, *ScopeProp->GetClass()->GetName()));
		}
	}

	// One cursor is only valid for the exact query that issued it, so every
	// parameter that changes which rows are enumerated goes into the key.
	const FString CollectionKey = FString::Printf(
		TEXT("reflect_instance|obj=%s|prop=%s|filter=%s|inherited=%d|editableOnly=%d"),
		*Object->GetPathName(), *PropertyPath, *Filter,
		bIncludeInherited ? 1 : 0, bEditableOnly ? 1 : 0);

	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(Params, CollectionKey, /*Default*/ 100, /*Max*/ 500, Page))
	{
		return Err;
	}

	TArray<MCPPagination::FPageRow> Rows;
	int32 EditableCount = 0;
	int32 SettableCount = 0;
	for (TFieldIterator<FProperty> It(ContainerStruct,
			bIncludeInherited ? EFieldIteratorFlags::IncludeSuper : EFieldIteratorFlags::ExcludeSuper);
		It; ++It)
	{
		FProperty* Prop = *It;
		if (!Filter.IsEmpty() && !Prop->GetName().Contains(Filter, ESearchCase::IgnoreCase)) continue;

		TSharedPtr<FJsonObject> Entry = SchemaDescribeInstanceProperty(
			Prop, ContainerStruct, Container, Owner, Context, PropertyPath, bIncludeValues, MaxDepth);

		bool bEditable = false;
		Entry->TryGetBoolField(TEXT("editable"), bEditable);
		bool bSettable = false;
		Entry->TryGetBoolField(TEXT("settable"), bSettable);
		if (bEditable) ++EditableCount;
		if (bSettable) ++SettableCount;
		if (bEditableOnly && !bEditable) continue;

		// The property name is unique within one container, which is what a
		// page anchor needs: it names the same row across two enumerations
		// even if the field order shifts.
		Rows.Add({ Prop->GetName(), MakeShared<FJsonValueObject>(Entry) });
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("objectPath"), ObjectPath);
	Result->SetStringField(TEXT("resolvedPath"), Object->GetPathName());
	Result->SetStringField(TEXT("resolvedKind"), ResolvedKind);
	Result->SetStringField(TEXT("className"), Object->GetClass()->GetName());
	Result->SetStringField(TEXT("classPath"), Object->GetClass()->GetPathName());
	Result->SetStringField(TEXT("context"), ContextName(Context));
	Result->SetStringField(TEXT("outerPath"), Object->GetOuter() ? Object->GetOuter()->GetPathName() : FString());
	if (!PropertyPath.IsEmpty())
	{
		Result->SetStringField(TEXT("propertyPath"), PropertyPath);
		SchemaAddIfNonEmpty(Result, TEXT("scopeType"), ScopeType);
		// When propertyPath followed an object reference, the rows belong to
		// THAT object. Its path is here so a caller can aim the next call
		// straight at it rather than re-walking the dotted route every time.
		if (Owner && Owner != Object)
		{
			Result->SetStringField(TEXT("scopeObjectPath"), Owner->GetPathName());
		}
	}
	Result->SetNumberField(TEXT("editableCount"), EditableCount);
	Result->SetNumberField(TEXT("settableCount"), SettableCount);
	Result->SetStringField(TEXT("writeWith"),
		TEXT("editor(set_property, objectPath=<resolvedPath>, propertyName=<the row's 'path'>, value=<...>)"));

	MCPPagination::EmitPage(Page, Rows, TEXT("properties"), Result);
	return MCPResult(Result);
}
