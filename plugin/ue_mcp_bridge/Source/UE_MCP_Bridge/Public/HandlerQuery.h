#pragma once

// Shared query machinery: typed property reads, dotted path resolution, and
// server-side predicates.
//
// Two handlers need exactly this and for the same reason. level
// (query_components) and asset(bulk_read_properties) both exist because a
// level-wide or library-wide question used to mean shipping every candidate to
// the client and filtering it there. Filtering in the editor needs a predicate
// language, and one predicate language is better than two that drift.
//
// It lives in a header rather than in either .cpp because the module is a
// unity build: a file-local helper copied into a second translation unit is a
// redefinition (C2084) as soon as UBT groups the two files, and the grouping is
// not stable across machines.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/EnumProperty.h"
#include "UObject/UnrealType.h"

namespace MCPQuery
{
	/** "EComponentMobility::Movable" and "EComponentMobility.Movable" both
	 *  reduce to "Movable". UEnum renders a namespaced enum either way
	 *  depending on how it was declared, and a caller comparing against
	 *  "Movable" should not have to know which. */
	inline FString ShortEnumName(const FString& Raw)
	{
		int32 Index = INDEX_NONE;
		if (Raw.FindLastChar(TEXT(':'), Index) || Raw.FindLastChar(TEXT('.'), Index))
		{
			return Raw.RightChop(Index + 1);
		}
		return Raw;
	}

	/**
	 * A UPROPERTY as typed JSON rather than as exported text.
	 *
	 * Typed matters: a predicate like `CullDistance.Max > 0` has to compare
	 * numbers, and the string "0.000000" is not a number. Anything without a
	 * natural JSON shape falls back to its exported text, so a value is never
	 * simply lost.
	 */
	inline TSharedPtr<FJsonValue> PropertyToJson(FProperty* Property, const void* ValuePtr)
	{
		if (!Property || !ValuePtr)
		{
			return MakeShared<FJsonValueNull>();
		}

		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
		{
			return MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(ValuePtr));
		}
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
		{
			const int64 Value = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			UEnum* Enum = EnumProp->GetEnum();
			return MakeShared<FJsonValueString>(
				Enum ? ShortEnumName(Enum->GetNameStringByValue(Value)) : LexToString(Value));
		}
		if (const FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
		{
			if (UEnum* Enum = NumericProp->GetIntPropertyEnum())
			{
				const int64 Value = NumericProp->GetSignedIntPropertyValue(ValuePtr);
				return MakeShared<FJsonValueString>(ShortEnumName(Enum->GetNameStringByValue(Value)));
			}
			if (NumericProp->IsFloatingPoint())
			{
				return MakeShared<FJsonValueNumber>(NumericProp->GetFloatingPointPropertyValue(ValuePtr));
			}
			return MakeShared<FJsonValueNumber>(
				static_cast<double>(NumericProp->GetSignedIntPropertyValue(ValuePtr)));
		}
		if (const FStrProperty* StrProp = CastField<FStrProperty>(Property))
		{
			return MakeShared<FJsonValueString>(StrProp->GetPropertyValue(ValuePtr));
		}
		if (const FNameProperty* NameProp = CastField<FNameProperty>(Property))
		{
			return MakeShared<FJsonValueString>(NameProp->GetPropertyValue(ValuePtr).ToString());
		}
		if (const FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Property))
		{
			const FString Path = SoftProp->GetPropertyValue(ValuePtr).ToString();
			return Path.IsEmpty()
				? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>())
				: StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(Path));
		}
		if (const FObjectPropertyBase* ObjectProp = CastField<FObjectPropertyBase>(Property))
		{
			UObject* Value = ObjectProp->GetObjectPropertyValue(ValuePtr);
			return Value
				? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueString>(Value->GetPathName()))
				: StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNull>());
		}

		FString Exported;
		Property->ExportText_Direct(Exported, ValuePtr, ValuePtr, nullptr, PPF_None);
		return MakeShared<FJsonValueString>(Exported);
	}

	/**
	 * Resolve a dotted UPROPERTY path such as `CullDistance.Max` on a UObject,
	 * walking nested structs. Returns an invalid pointer when any segment does
	 * not exist, which is how a caller distinguishes "the property is null"
	 * from "this class does not have that property".
	 */
	inline TSharedPtr<FJsonValue> ReadDottedProperty(UObject* Object, const FString& Path, FString& OutResolvedType)
	{
		OutResolvedType.Reset();
		if (!Object || Path.IsEmpty())
		{
			return nullptr;
		}

		TArray<FString> Segments;
		Path.ParseIntoArray(Segments, TEXT("."), true);

		UStruct* CurrentStruct = Object->GetClass();
		const void* CurrentContainer = Object;
		for (int32 Index = 0; Index < Segments.Num(); ++Index)
		{
			if (!CurrentStruct || !CurrentContainer)
			{
				return nullptr;
			}
			FProperty* Property = CurrentStruct->FindPropertyByName(FName(*Segments[Index]));
			if (!Property)
			{
				return nullptr;
			}
			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CurrentContainer);
			if (Index == Segments.Num() - 1)
			{
				OutResolvedType = Property->GetCPPType();
				return PropertyToJson(Property, ValuePtr);
			}
			const FStructProperty* StructProp = CastField<FStructProperty>(Property);
			if (!StructProp)
			{
				// Only structs are walked. Following an object reference would
				// silently read a different asset than the one asked about.
				return nullptr;
			}
			CurrentStruct = StructProp->Struct;
			CurrentContainer = ValuePtr;
		}
		return nullptr;
	}

	/** Resolve a dotted field path inside an already-built JSON row. A numeric
	 *  segment indexes an array, as in materials.0.materialClass (#1303).
	 *  Returns an invalid pointer when the path does not exist, which is what
	 *  the `exists` and `notExists` operators test. */
	inline TSharedPtr<FJsonValue> ResolvePath(const TSharedPtr<FJsonObject>& Row, const FString& Path)
	{
		if (!Row.IsValid() || Path.IsEmpty())
		{
			return nullptr;
		}

		TArray<FString> Segments;
		Path.ParseIntoArray(Segments, TEXT("."), true);
		TSharedPtr<FJsonValue> Current = MakeShared<FJsonValueObject>(Row);
		for (const FString& Segment : Segments)
		{
			if (!Current.IsValid())
			{
				return nullptr;
			}
			if (Current->Type == EJson::Array)
			{
				if (!Segment.IsNumeric())
				{
					return nullptr;
				}
				const TArray<TSharedPtr<FJsonValue>>& Items = Current->AsArray();
				const int32 Item = FCString::Atoi(*Segment);
				if (!Items.IsValidIndex(Item))
				{
					return nullptr;
				}
				Current = Items[Item];
				continue;
			}
			if (Current->Type != EJson::Object)
			{
				return nullptr;
			}
			// FJsonObject::Values is not keyed by FString on UE 5.8, so Find(FString)
			// does not resolve. TryGetField takes an FStringView and is the
			// supported lookup regardless of the map's key type.
			Current = Current->AsObject()->TryGetField(Segment);
		}
		return Current;
	}

	/** One value rendered as the string a group key or a histogram bucket uses. */
	inline FString ValueKey(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid())
		{
			return TEXT("<absent>");
		}
		switch (Value->Type)
		{
		case EJson::Null:    return TEXT("<null>");
		case EJson::Boolean: return Value->AsBool() ? TEXT("true") : TEXT("false");
		case EJson::Number:  return FString::SanitizeFloat(Value->AsNumber());
		case EJson::String:  return Value->AsString();
		default:             return TEXT("<complex>");
		}
	}

	struct FPredicate
	{
		FString Field;
		FString Op;
		TSharedPtr<FJsonValue> Value;
	};

	inline bool ValuesEqual(const TSharedPtr<FJsonValue>& Left, const TSharedPtr<FJsonValue>& Right)
	{
		if (!Left.IsValid() || !Right.IsValid())
		{
			return false;
		}
		if (Left->Type == EJson::Number && Right->Type == EJson::Number)
		{
			return FMath::IsNearlyEqual(Left->AsNumber(), Right->AsNumber(), UE_KINDA_SMALL_NUMBER);
		}
		if (Left->Type == EJson::Boolean || Right->Type == EJson::Boolean)
		{
			return Left->AsBool() == Right->AsBool();
		}
		// Strings compare case-insensitively. An agent that types "movable"
		// should not silently match nothing, which is the exact shape of the
		// client-side filter bug this machinery replaces.
		return ValueKey(Left).Equals(ValueKey(Right), ESearchCase::IgnoreCase);
	}

	/** Every operator this vocabulary understands. Anything else is rejected by
	 *  name rather than evaluated to false. */
	inline bool IsKnownOp(const FString& Op)
	{
		static const TCHAR* const Known[] = {
			TEXT("eq"), TEXT("ne"), TEXT("lt"), TEXT("lte"), TEXT("gt"), TEXT("gte"),
			TEXT("contains"), TEXT("notContains"), TEXT("startsWith"), TEXT("endsWith"),
			TEXT("in"), TEXT("notIn"), TEXT("exists"), TEXT("notExists"),
			TEXT("isNull"), TEXT("isNotNull"), TEXT("isTrue"), TEXT("isFalse"),
		};
		for (const TCHAR* Candidate : Known)
		{
			if (Op.Equals(Candidate, ESearchCase::CaseSensitive))
			{
				return true;
			}
		}
		return false;
	}

	/** The operator list, for an error message that tells the caller what it
	 *  could have said instead. */
	inline const TCHAR* KnownOpList()
	{
		return TEXT("eq, ne, lt, lte, gt, gte, contains, notContains, startsWith, endsWith, in, notIn, exists, notExists, isNull, isNotNull, isTrue, isFalse");
	}

	inline bool Evaluate(const FPredicate& Predicate, const TSharedPtr<FJsonObject>& Row)
	{
		const TSharedPtr<FJsonValue> Actual = ResolvePath(Row, Predicate.Field);

		if (Predicate.Op == TEXT("exists"))    return Actual.IsValid();
		if (Predicate.Op == TEXT("notExists")) return !Actual.IsValid();
		if (Predicate.Op == TEXT("isNull"))    return !Actual.IsValid() || Actual->Type == EJson::Null;
		if (Predicate.Op == TEXT("isNotNull")) return Actual.IsValid() && Actual->Type != EJson::Null;
		if (Predicate.Op == TEXT("isTrue"))    return Actual.IsValid() && Actual->Type == EJson::Boolean && Actual->AsBool();
		if (Predicate.Op == TEXT("isFalse"))   return Actual.IsValid() && Actual->Type == EJson::Boolean && !Actual->AsBool();

		if (!Actual.IsValid())
		{
			// Every remaining operator compares against a value, and an absent
			// field has none. `notExists` above is how a caller asks for that.
			return false;
		}

		if (Predicate.Op == TEXT("eq")) return ValuesEqual(Actual, Predicate.Value);
		if (Predicate.Op == TEXT("ne")) return !ValuesEqual(Actual, Predicate.Value);

		if (Predicate.Op == TEXT("lt") || Predicate.Op == TEXT("lte") ||
			Predicate.Op == TEXT("gt") || Predicate.Op == TEXT("gte"))
		{
			if (Actual->Type != EJson::Number || !Predicate.Value.IsValid() || Predicate.Value->Type != EJson::Number)
			{
				return false;
			}
			const double A = Actual->AsNumber();
			const double B = Predicate.Value->AsNumber();
			if (Predicate.Op == TEXT("lt"))  return A < B;
			if (Predicate.Op == TEXT("lte")) return A <= B;
			if (Predicate.Op == TEXT("gt"))  return A > B;
			return A >= B;
		}

		if (Predicate.Op == TEXT("contains") || Predicate.Op == TEXT("notContains") ||
			Predicate.Op == TEXT("startsWith") || Predicate.Op == TEXT("endsWith"))
		{
			const FString Haystack = ValueKey(Actual);
			const FString Needle = ValueKey(Predicate.Value);
			if (Predicate.Op == TEXT("contains"))    return Haystack.Contains(Needle, ESearchCase::IgnoreCase);
			if (Predicate.Op == TEXT("notContains")) return !Haystack.Contains(Needle, ESearchCase::IgnoreCase);
			if (Predicate.Op == TEXT("startsWith"))  return Haystack.StartsWith(Needle, ESearchCase::IgnoreCase);
			return Haystack.EndsWith(Needle, ESearchCase::IgnoreCase);
		}

		if (Predicate.Op == TEXT("in") || Predicate.Op == TEXT("notIn"))
		{
			bool bFound = false;
			if (Predicate.Value.IsValid() && Predicate.Value->Type == EJson::Array)
			{
				for (const TSharedPtr<FJsonValue>& Candidate : Predicate.Value->AsArray())
				{
					if (ValuesEqual(Actual, Candidate))
					{
						bFound = true;
						break;
					}
				}
			}
			return Predicate.Op == TEXT("in") ? bFound : !bFound;
		}

		return false;
	}

	/**
	 * Parse a `where` array. Returns false and fills OutError with a message
	 * naming the offending index, so a caller with twenty predicates learns
	 * which one is wrong rather than that something is.
	 */
	inline bool ParsePredicates(
		const TArray<TSharedPtr<FJsonValue>>* WhereValues,
		int32 MaxPredicates,
		TArray<FPredicate>& OutPredicates,
		FString& OutError)
	{
		OutPredicates.Reset();
		if (!WhereValues)
		{
			return true;
		}
		if (WhereValues->Num() > MaxPredicates)
		{
			OutError = FString::Printf(TEXT("'where' exceeds the maximum of %d predicates"), MaxPredicates);
			return false;
		}
		for (int32 Index = 0; Index < WhereValues->Num(); ++Index)
		{
			const TSharedPtr<FJsonValue>& Entry = (*WhereValues)[Index];
			if (!Entry.IsValid() || Entry->Type != EJson::Object)
			{
				OutError = FString::Printf(TEXT("'where[%d]' must be an object"), Index);
				return false;
			}
			const TSharedPtr<FJsonObject> EntryObject = Entry->AsObject();
			FPredicate Predicate;
			if (!EntryObject->TryGetStringField(TEXT("field"), Predicate.Field) || Predicate.Field.IsEmpty())
			{
				OutError = FString::Printf(TEXT("'where[%d].field' is required"), Index);
				return false;
			}
			Predicate.Op = EntryObject->HasField(TEXT("op"))
				? EntryObject->GetStringField(TEXT("op"))
				: FString(TEXT("eq"));
			if (!IsKnownOp(Predicate.Op))
			{
				OutError = FString::Printf(
					TEXT("'where[%d].op' is '%s'. Valid: %s"), Index, *Predicate.Op, KnownOpList());
				return false;
			}
			const TSharedPtr<FJsonValue>* Value = EntryObject->Values.Find(TEXT("value"));
			Predicate.Value = Value ? *Value : nullptr;
			OutPredicates.Add(MoveTemp(Predicate));
		}
		return true;
	}

	/** Run a predicate set over one row. `bAll` is the AND/OR switch. */
	inline bool EvaluateAll(const TArray<FPredicate>& Predicates, const TSharedPtr<FJsonObject>& Row, bool bAll)
	{
		if (Predicates.Num() == 0)
		{
			return true;
		}
		for (const FPredicate& Predicate : Predicates)
		{
			const bool bResult = Evaluate(Predicate, Row);
			if (bAll)
			{
				if (!bResult) return false;
			}
			else if (bResult)
			{
				return true;
			}
		}
		return bAll;
	}
}
