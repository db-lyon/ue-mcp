#pragma once

#include "CoreMinimal.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "Misc/OutputDeviceNull.h"
#include "Misc/DefaultValueHelper.h"

// Structured UE export-text import for container properties (#820).
//
// FMapProperty exports its pairs as "((Key, Value),(Key2, Value2))". Handing
// that text back to a whole-struct ImportText parses zero pairs when the key is
// a struct, reports success anyway, and leaves the destination map empty: a
// read-then-write round trip silently destroys the data.
//
// The helpers here split export text structurally (nesting, quotes and escapes
// respected), import every map key and value on its own so the key never has to
// survive the engine's pair parser, and verify the pair count afterwards. A
// dropped pair is a hard error instead of an empty map. Callers snapshot the
// destination before calling and restore it when a call fails, so a rejected
// write never leaves a partially written container behind.
namespace MCPPropertyText
{
	// How deep the type walks below go before giving up. Value types cannot
	// contain themselves, so this only bounds pathological nesting.
	static constexpr int32 MaxTypeDepth = 12;

	// True when Text is a single "(...)" group: the first '(' closes on the
	// last character. Quoted sections and backslash escapes are skipped.
	inline bool IsParenGroup(const FString& InText)
	{
		const FString T = InText.TrimStartAndEnd();
		if (T.Len() < 2 || T[0] != TEXT('(') || T[T.Len() - 1] != TEXT(')')) return false;

		int32 Depth = 0;
		bool bInQuotes = false;
		for (int32 i = 0; i < T.Len(); ++i)
		{
			const TCHAR C = T[i];
			if (bInQuotes)
			{
				if (C == TEXT('\\')) ++i;
				else if (C == TEXT('"')) bInQuotes = false;
				continue;
			}
			if (C == TEXT('"')) { bInQuotes = true; continue; }
			if (C == TEXT('(')) { ++Depth; continue; }
			if (C == TEXT(')'))
			{
				--Depth;
				if (Depth == 0) return i == T.Len() - 1;
				if (Depth < 0) return false;
			}
		}
		return false;
	}

	// Split the body of a "(a,b,(c,d))" group into its top-level tokens:
	// ["a", "b", "(c,d)"]. "()" yields no tokens.
	inline bool SplitTopLevel(const FString& InText, TArray<FString>& Out, FString& OutError)
	{
		const FString T = InText.TrimStartAndEnd();
		if (!IsParenGroup(T))
		{
			OutError = FString::Printf(TEXT("expected a parenthesised group, got '%s'"), *T);
			return false;
		}

		const FString Body = T.Mid(1, T.Len() - 2);
		int32 Depth = 0;
		bool bInQuotes = false;
		int32 Start = 0;
		for (int32 i = 0; i < Body.Len(); ++i)
		{
			const TCHAR C = Body[i];
			if (bInQuotes)
			{
				if (C == TEXT('\\')) ++i;
				else if (C == TEXT('"')) bInQuotes = false;
				continue;
			}
			if (C == TEXT('"')) { bInQuotes = true; continue; }
			if (C == TEXT('(')) { ++Depth; continue; }
			if (C == TEXT(')')) { --Depth; continue; }
			if (C == TEXT(',') && Depth == 0)
			{
				Out.Add(Body.Mid(Start, i - Start).TrimStartAndEnd());
				Start = i + 1;
			}
		}

		const FString Last = Body.Mid(Start).TrimStartAndEnd();
		if (!Last.IsEmpty() || Out.Num() > 0)
		{
			Out.Add(Last);
		}
		return true;
	}

	// Split "(Name=Value,Other=Value)" into its field pairs. Field values keep
	// their own nesting, so "(A=(B=1),C=2)" yields [("A","(B=1)"), ("C","2")].
	inline bool SplitStructFields(const FString& InText, TArray<TPair<FString, FString>>& Out, FString& OutError)
	{
		TArray<FString> Tokens;
		if (!SplitTopLevel(InText, Tokens, OutError)) return false;

		for (const FString& Token : Tokens)
		{
			// Find the assignment at nesting depth zero and outside quotes.
			int32 Depth = 0;
			bool bInQuotes = false;
			int32 EqPos = INDEX_NONE;
			for (int32 i = 0; i < Token.Len(); ++i)
			{
				const TCHAR C = Token[i];
				if (bInQuotes)
				{
					if (C == TEXT('\\')) ++i;
					else if (C == TEXT('"')) bInQuotes = false;
					continue;
				}
				if (C == TEXT('"')) { bInQuotes = true; continue; }
				if (C == TEXT('(')) { ++Depth; continue; }
				if (C == TEXT(')')) { --Depth; continue; }
				if (C == TEXT('=') && Depth == 0) { EqPos = i; break; }
			}
			if (EqPos == INDEX_NONE)
			{
				OutError = FString::Printf(TEXT("struct field '%s' is not in Name=Value form"), *Token);
				return false;
			}
			Out.Emplace(Token.Left(EqPos).TrimStartAndEnd(), Token.Mid(EqPos + 1).TrimStartAndEnd());
		}
		return true;
	}

	// Split one exported map pair "(Key, Value)" into its two halves.
	inline bool SplitMapPair(const FString& InText, FString& OutKey, FString& OutValue, FString& OutError)
	{
		TArray<FString> Halves;
		if (!SplitTopLevel(InText, Halves, OutError)) return false;
		if (Halves.Num() != 2)
		{
			OutError = FString::Printf(TEXT("map pair '%s' has %d parts, expected 2"), *InText, Halves.Num());
			return false;
		}
		OutKey = Halves[0];
		OutValue = Halves[1];
		return true;
	}

	// True when this property is, or holds, a TMap. Object references are not
	// followed: they are a separate object's data, not this value's.
	inline bool ContainsMap(const FProperty* Prop, int32 Depth = 0)
	{
		if (!Prop || Depth > MaxTypeDepth) return false;
		if (Prop->IsA<FMapProperty>()) return true;
		if (const FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop)) return ContainsMap(ArrProp->Inner, Depth + 1);
		if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop)) return ContainsMap(SetProp->ElementProp, Depth + 1);
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
			{
				if (ContainsMap(*It, Depth + 1)) return true;
			}
		}
		return false;
	}

	// Total number of map pairs held anywhere inside this value. Used to prove
	// that a text write kept every pair it was given.
	inline int32 CountMapPairs(const FProperty* Prop, const void* ValueAddr, int32 Depth = 0)
	{
		if (!Prop || !ValueAddr || Depth > MaxTypeDepth) return 0;

		if (const FMapProperty* MapProp = CastField<FMapProperty>(Prop))
		{
			FScriptMapHelper H(MapProp, ValueAddr);
			int32 Total = H.Num();
			for (FScriptMapHelper::FIterator It = H.CreateIterator(); It; ++It)
			{
				Total += CountMapPairs(MapProp->ValueProp, H.GetValuePtr(It), Depth + 1);
			}
			return Total;
		}
		if (const FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
		{
			if (!ContainsMap(ArrProp->Inner, Depth + 1)) return 0;
			FScriptArrayHelper H(ArrProp, ValueAddr);
			int32 Total = 0;
			for (int32 i = 0; i < H.Num(); ++i) Total += CountMapPairs(ArrProp->Inner, H.GetRawPtr(i), Depth + 1);
			return Total;
		}
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			int32 Total = 0;
			for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
			{
				FProperty* Field = *It;
				if (!ContainsMap(Field, Depth + 1)) continue;
				Total += CountMapPairs(Field, Field->ContainerPtrToValuePtr<void>(ValueAddr), Depth + 1);
			}
			return Total;
		}
		return 0;
	}

	// Engine text import, with the engine's own warnings swallowed: a null
	// return is the failure signal the callers act on.
	inline bool ImportTextRaw(const FProperty* Prop, void* ValueAddr, const FString& Text, UObject* Owner, int32 PortFlags, FString& OutError)
	{
		// The engine's numeric import reads "banana" as 0 and reports success.
		if (const FNumericProperty* Num = CastField<const FNumericProperty>(Prop))
		{
			if (!Num->IsEnum())
			{
				const FString Trimmed = Text.TrimStartAndEnd();
				const bool bValid = Num->IsInteger()
					? FDefaultValueHelper::IsStringValidInteger(Trimmed)
					: (FDefaultValueHelper::IsStringValidFloat(Trimmed) || FDefaultValueHelper::IsStringValidInteger(Trimmed));
				if (!bValid)
				{
					OutError = FString::Printf(TEXT("'%s' is not a number"), *Text);
					return false;
				}
			}
		}

		FOutputDeviceNull Silent;
		const TCHAR* Rest = Prop->ImportText_Direct(*Text, ValueAddr, Owner, PortFlags, &Silent);
		if (Rest == nullptr)
		{
			OutError = FString::Printf(TEXT("ImportText failed for '%s'"), *Text);
			return false;
		}
		return true;
	}

	inline bool ImportTextIntoProperty(FProperty* Prop, void* ValueAddr, const FString& Text, UObject* Owner, FString& OutError, int32 Depth = 0);

	// Rebuild a TMap from its exported "((Key, Value),...)" text. Each key and
	// each value is imported on its own, duplicate keys are rejected, and the
	// finished map must hold exactly as many pairs as the text listed.
	inline bool ImportMapText(FMapProperty* MapProp, void* ValueAddr, const FString& Text, UObject* Owner, FString& OutError, int32 Depth)
	{
		TArray<FString> PairTexts;
		if (!SplitTopLevel(Text, PairTexts, OutError))
		{
			OutError = FString::Printf(TEXT("map '%s': %s"), *MapProp->GetName(), *OutError);
			return false;
		}

		FScriptMapHelper Helper(MapProp, ValueAddr);
		Helper.EmptyValues();

		int32 Requested = 0;
		for (const FString& PairText : PairTexts)
		{
			FString KeyText, ValueText;
			if (!SplitMapPair(PairText, KeyText, ValueText, OutError))
			{
				OutError = FString::Printf(TEXT("map '%s': %s"), *MapProp->GetName(), *OutError);
				return false;
			}

			FDefaultConstructedPropertyElement TempKey(MapProp->KeyProp);
			if (!ImportTextIntoProperty(MapProp->KeyProp, TempKey.GetObjAddress(), KeyText, Owner, OutError, Depth + 1))
			{
				OutError = FString::Printf(TEXT("map '%s' key '%s': %s"), *MapProp->GetName(), *KeyText, *OutError);
				return false;
			}
			if (Helper.Num() > 0 && Helper.FindValueFromHash(TempKey.GetObjAddress()) != nullptr)
			{
				OutError = FString::Printf(TEXT("map '%s': duplicate key '%s'"), *MapProp->GetName(), *KeyText);
				return false;
			}

			void* ValuePtr = Helper.FindOrAdd(TempKey.GetObjAddress());
			if (!ImportTextIntoProperty(MapProp->ValueProp, ValuePtr, ValueText, Owner, OutError, Depth + 1))
			{
				OutError = FString::Printf(TEXT("map '%s' value for key '%s': %s"), *MapProp->GetName(), *KeyText, *OutError);
				return false;
			}
			++Requested;
		}

		if (Helper.Num() != Requested)
		{
			OutError = FString::Printf(
				TEXT("map '%s' would have lost entries: %d pair(s) given, %d stored. Refusing the write"),
				*MapProp->GetName(), Requested, Helper.Num());
			return false;
		}
		return true;
	}

	// Import UE export text into a property. Anything that holds a TMap is
	// parsed here, pair by pair; everything else goes straight to the engine
	// importer, which is correct for it.
	inline bool ImportTextIntoProperty(FProperty* Prop, void* ValueAddr, const FString& Text, UObject* Owner, FString& OutError, int32 Depth)
	{
		if (!Prop || !ValueAddr) { OutError = TEXT("null property/address"); return false; }

		// Container elements are exported delimited (quoted strings, commas as
		// separators), and that is the flag set the engine reads them back with.
		const int32 PortFlags = Depth > 0 ? PPF_Delimited : PPF_None;

		if (!ContainsMap(Prop) || Depth > MaxTypeDepth)
		{
			return ImportTextRaw(Prop, ValueAddr, Text, Owner, PortFlags, OutError);
		}

		if (FMapProperty* MapProp = CastField<FMapProperty>(Prop))
		{
			return ImportMapText(MapProp, ValueAddr, Text, Owner, OutError, Depth);
		}

		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			TArray<TPair<FString, FString>> Fields;
			if (!SplitStructFields(Text, Fields, OutError))
			{
				OutError = FString::Printf(TEXT("struct '%s': %s"), *StructProp->Struct->GetName(), *OutError);
				return false;
			}
			for (const TPair<FString, FString>& Field : Fields)
			{
				FProperty* SubProp = StructProp->Struct->FindPropertyByName(FName(*Field.Key));
				if (!SubProp)
				{
					OutError = FString::Printf(TEXT("struct '%s' has no field '%s'"), *StructProp->Struct->GetName(), *Field.Key);
					return false;
				}
				if (!ImportTextIntoProperty(SubProp, SubProp->ContainerPtrToValuePtr<void>(ValueAddr), Field.Value, Owner, OutError, Depth + 1))
				{
					OutError = FString::Printf(TEXT("%s.%s: %s"), *StructProp->Struct->GetName(), *Field.Key, *OutError);
					return false;
				}
			}
			return true;
		}

		if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
		{
			TArray<FString> Elements;
			if (!SplitTopLevel(Text, Elements, OutError))
			{
				OutError = FString::Printf(TEXT("array '%s': %s"), *ArrProp->GetName(), *OutError);
				return false;
			}
			FScriptArrayHelper Helper(ArrProp, ValueAddr);
			Helper.Resize(Elements.Num());
			for (int32 i = 0; i < Elements.Num(); ++i)
			{
				if (!ImportTextIntoProperty(ArrProp->Inner, Helper.GetRawPtr(i), Elements[i], Owner, OutError, Depth + 1))
				{
					OutError = FString::Printf(TEXT("%s[%d]: %s"), *ArrProp->GetName(), i, *OutError);
					return false;
				}
			}
			return true;
		}

		if (FSetProperty* SetProp = CastField<FSetProperty>(Prop))
		{
			TArray<FString> Elements;
			if (!SplitTopLevel(Text, Elements, OutError))
			{
				OutError = FString::Printf(TEXT("set '%s': %s"), *SetProp->GetName(), *OutError);
				return false;
			}
			FScriptSetHelper Helper(SetProp, ValueAddr);
			Helper.EmptyElements();
			for (const FString& Element : Elements)
			{
				const int32 Index = Helper.AddDefaultValue_Invalid_NeedsRehash();
				if (!ImportTextIntoProperty(SetProp->ElementProp, Helper.GetElementPtr(Index), Element, Owner, OutError, Depth + 1))
				{
					OutError = FString::Printf(TEXT("set '%s': %s"), *SetProp->GetName(), *OutError);
					return false;
				}
			}
			Helper.Rehash();
			if (Helper.Num() != Elements.Num())
			{
				OutError = FString::Printf(
					TEXT("set '%s' would have lost entries: %d given, %d stored. Refusing the write"),
					*SetProp->GetName(), Elements.Num(), Helper.Num());
				return false;
			}
			return true;
		}

		return ImportTextRaw(Prop, ValueAddr, Text, Owner, PortFlags, OutError);
	}

	// Does this property's exported text read back as the same value? Only
	// worth asking for map-bearing properties, where the engine's pair format
	// is the thing that can quietly lose data. Read handlers report the answer
	// so a caller knows whether it may copy `valueText` back into a setter.
	inline bool ExportedTextRoundTrips(FProperty* Prop, const void* ValueAddr, UObject* Owner)
	{
		if (!Prop || !ValueAddr) return true;
		if (!ContainsMap(Prop)) return true;

		FString Text;
		Prop->ExportText_Direct(Text, ValueAddr, ValueAddr, Owner, PPF_None);

		// The probe imports with no owner on purpose. This runs inside a READ,
		// and an owner is what lets the importer construct instanced subobjects
		// under the real asset: a question about the text must not touch it.
		FDefaultConstructedPropertyElement Probe(Prop);
		FString Error;
		if (!ImportTextIntoProperty(Prop, Probe.GetObjAddress(), Text, nullptr, Error)) return false;
		return CountMapPairs(Prop, Probe.GetObjAddress()) == CountMapPairs(Prop, ValueAddr);
	}
}
