#pragma once

// Shared property-path parsing and array selection for JSON writes and query
// reads. Keep quoted values, key matching and ambiguity errors consistent.
#include "CoreMinimal.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "MCPEngineCompat.h"
#include "GameplayTagContainer.h"

namespace MCPJsonProperty
{
	// One segment of a property path: a property name, optionally followed by
	// one selector, either a numeric index ("Traits[2]") or a key
	// ("WeaponProfiles[WeaponId=SGK_Stone_Axe]").
	struct FPropertyPathSegment
	{
		FString Name;
		bool bHasIndex = false;
		int32 Index = INDEX_NONE;
		bool bHasKey = false;
		FString KeyField;
		FString KeyValue;
	};

	// What ResolveDottedPath worked out beyond the leaf, for callers that ask.
	struct FResolvedPathInfo
	{
		// The path with every [Field=Value] selector replaced by the index it
		// matched, e.g. "WeaponProfiles[12].Presentation.Scale3D".
		FString IndexedPath;
		// True when at least one segment was addressed by key.
		bool bUsedKeySelector = false;
	};

	// Split a property path on '.', except inside [...] and inside a quoted key
	// value, so "Rows[Id=\"a.b\"].Value" is two segments rather than three.
	// Empty segments are dropped, as ParseIntoArray did before selectors
	// existed.
	inline bool SplitPropertyPath(const FString& Path, TArray<FString>& OutParts, FString& OutError)
	{
		OutParts.Reset();
		FString Current;
		int32 BracketDepth = 0;
		bool bInQuote = false;
		for (int32 i = 0; i < Path.Len(); ++i)
		{
			const TCHAR Ch = Path[i];
			if (bInQuote)
			{
				Current.AppendChar(Ch);
				if (Ch == TEXT('\\') && i + 1 < Path.Len()) { Current.AppendChar(Path[++i]); continue; }
				if (Ch == TEXT('"')) bInQuote = false;
				continue;
			}
			if (Ch == TEXT('"') && BracketDepth > 0) bInQuote = true;
			else if (Ch == TEXT('[')) ++BracketDepth;
			else if (Ch == TEXT(']')) --BracketDepth;
			else if (Ch == TEXT('.') && BracketDepth == 0)
			{
				if (!Current.IsEmpty()) OutParts.Add(Current);
				Current.Reset();
				continue;
			}
			if (BracketDepth < 0)
			{
				OutError = FString::Printf(TEXT("unmatched ']' in property path '%s'"), *Path);
				return false;
			}
			Current.AppendChar(Ch);
		}
		if (bInQuote || BracketDepth != 0)
		{
			OutError = FString::Printf(TEXT("unterminated %s in property path '%s'"),
				bInQuote ? TEXT("quoted key value") : TEXT("'['"), *Path);
			return false;
		}
		if (!Current.IsEmpty()) OutParts.Add(Current);
		return true;
	}

	// Parse one segment produced by SplitPropertyPath. The selector body is a
	// number (an index) or Field=Value (a key). The value may be wrapped in
	// double quotes, which is how a value holding ']' or leading spaces is
	// written; \" and \\ escape inside the quotes.
	inline bool ParsePropertyPathSegment(const FString& Token, FPropertyPathSegment& Out, FString& OutError)
	{
		Out = FPropertyPathSegment();
		int32 OpenPos = INDEX_NONE;
		if (!Token.FindChar(TEXT('['), OpenPos))
		{
			Out.Name = Token;
			return true;
		}
		Out.Name = Token.Left(OpenPos);
		if (Out.Name.IsEmpty())
		{
			OutError = FString::Printf(TEXT("'%s' has a selector but no property name before it"), *Token);
			return false;
		}
		if (!Token.EndsWith(TEXT("]")))
		{
			OutError = FString::Printf(
				TEXT("'%s' has text after its closing ']'. One selector per segment: Name[index] or Name[Field=Value]"), *Token);
			return false;
		}
		// One selector per segment. Brackets inside a quoted value are data.
		bool bInQuote = false;
		for (int32 i = OpenPos + 1; i < Token.Len() - 1; ++i)
		{
			if (bInQuote && Token[i] == TEXT('\\') && i + 1 < Token.Len() - 1) { ++i; continue; }
			if (Token[i] == TEXT('"')) bInQuote = !bInQuote;
			else if (!bInQuote && (Token[i] == TEXT('[') || Token[i] == TEXT(']')))
			{
				OutError = FString::Printf(TEXT("'%s' has multiple or nested selectors. One selector per segment: Name[index] or Name[Field=Value]"), *Token);
				return false;
			}
		}
		const FString Body = Token.Mid(OpenPos + 1, Token.Len() - OpenPos - 2).TrimStartAndEnd();

		// A key: the first '=' separates field from value.
		int32 EqPos = INDEX_NONE;
		if (Body.FindChar(TEXT('='), EqPos))
		{
			Out.bHasKey = true;
			Out.KeyField = Body.Left(EqPos).TrimStartAndEnd();
			FString Value = Body.Mid(EqPos + 1).TrimStartAndEnd();
			if (Out.KeyField.IsEmpty())
			{
				OutError = FString::Printf(TEXT("'%s' has no key field before '='. Write Name[Field=Value]"), *Token);
				return false;
			}
			if (Value.Len() >= 2 && Value.StartsWith(TEXT("\"")) && Value.EndsWith(TEXT("\"")))
			{
				FString Unquoted;
				for (int32 i = 1; i < Value.Len() - 1; ++i)
				{
					if (Value[i] == TEXT('\\') && i + 1 < Value.Len() - 1) ++i;
					Unquoted.AppendChar(Value[i]);
				}
				Value = Unquoted;
			}
			Out.KeyValue = Value;
			return true;
		}

		// An index. Anything else used to go through Atoi and land on element 0.
		bool bNumeric = !Body.IsEmpty();
		for (int32 i = 0; i < Body.Len() && bNumeric; ++i)
		{
			bNumeric = FChar::IsDigit(Body[i]) || (i == 0 && Body[i] == TEXT('-') && Body.Len() > 1);
		}
		if (!bNumeric)
		{
			OutError = FString::Printf(
				TEXT("'[%s]' on '%s' is neither an index nor a key. Write %s[2] for an index or %s[Field=Value] for the one element whose Field equals Value"),
				*Body, *Out.Name, *Out.Name, *Out.Name);
			return false;
		}
		Out.bHasIndex = true;
		Out.Index = FCString::Atoi(*Body);
		return true;
	}

	// The text a key selector compares against, for the field types that can
	// act as a key: name, string, text (its display string), enum (short
	// enumerator name), integer, bool ("true"/"false") and gameplay tag (the tag
	// name), or an object reference (its exported text). Floats are left out
	// on purpose: an exact float match is not a key.
	// Returns false for any other type.
	inline bool PropertyKeyText(const FProperty* Prop, const void* Addr, FString& Out)
	{
		if (const FNameProperty* NameProp = CastField<FNameProperty>(Prop)) { Out = NameProp->GetPropertyValue(Addr).ToString(); return true; }
		if (const FStrProperty* StrProp = CastField<FStrProperty>(Prop)) { Out = StrProp->GetPropertyValue(Addr); return true; }
		if (const FTextProperty* TextProp = CastField<FTextProperty>(Prop)) { Out = TextProp->GetPropertyValue(Addr).ToString(); return true; }
		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop)) { Out = BoolProp->GetPropertyValue(Addr) ? TEXT("true") : TEXT("false"); return true; }
		if (Prop->IsA<FObjectPropertyBase>())
		{
			Out.Reset();
			Prop->ExportText_Direct(Out, Addr, Addr, nullptr, PPF_None);
			return true;
		}
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			const int64 Value = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(Addr);
			Out = EnumProp->GetEnum() ? EnumProp->GetEnum()->GetNameStringByValue(Value) : FString::Printf(TEXT("%lld"), Value);
			return true;
		}
		if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			const uint8 Value = ByteProp->GetPropertyValue(Addr);
			Out = ByteProp->Enum ? ByteProp->Enum->GetNameStringByValue(Value) : FString::FromInt(Value);
			return true;
		}
		if (const FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
		{
			if (!NumProp->IsInteger()) return false;
			Out.Reset();
			NumProp->ExportTextItem_Direct(Out, Addr, nullptr, nullptr, PPF_None);
			return true;
		}
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			// Derived tag structs too, as the setter accepts them (#820); the tag
			// lives in the FGameplayTag base at offset 0.
			if (StructProp->Struct && StructProp->Struct->IsChildOf(FGameplayTag::StaticStruct()))
			{
				Out = static_cast<const FGameplayTag*>(Addr)->GetTagName().ToString();
				return true;
			}
		}
		return false;
	}

	// Exact, case-sensitive, except that a bool compares without case and an
	// enum also answers to its qualified name ("EMySlot::Primary").
	inline bool PropertyKeyMatches(const FProperty* Prop, const void* Addr, const FString& KeyText, const FString& Wanted)
	{
		if (Prop->IsA<FBoolProperty>()) return KeyText.Equals(Wanted, ESearchCase::IgnoreCase);
		if (KeyText.Equals(Wanted, ESearchCase::CaseSensitive)) return true;
		const UEnum* Enum = nullptr;
		int64 Value = 0;
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			Enum = EnumProp->GetEnum();
			Value = EnumProp->GetUnderlyingProperty()->GetSignedIntPropertyValue(Addr);
		}
		else if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			Enum = ByteProp->Enum;
			Value = ByteProp->GetPropertyValue(Addr);
		}
		return Enum && Enum->GetNameByValue(Value).ToString().Equals(Wanted, ESearchCase::CaseSensitive);
	}

	// Pick the one element of a container whose key field equals the wanted
	// value. Handles TArray and C-style fixed arrays of structs, of
	// FInstancedStruct (the key is looked up on each element's payload) and of
	// object references (the key is looked up on each referenced object). Zero
	// or several matches is an error that lists the key values it saw, bounded,
	// because the next thing a caller needs is the value it should have typed.
	inline bool SelectElementByKey(FProperty* Prop, void* Container, void* ValueAddr,
		const FPropertyPathSegment& Seg, const FString& DottedName,
		int32& OutIndex, FString& OutError)
	{
		// Every element as (address, struct to find the key in, memory holding it).
		struct FCandidate { void* ElementAddr; const UStruct* Struct; const void* Memory; };
		TArray<FCandidate> Elements;
		FProperty* ElementProp = nullptr;

		if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
		{
			ElementProp = ArrProp->Inner;
			FScriptArrayHelper H(ArrProp, ValueAddr);
			for (int32 i = 0; i < H.Num(); ++i) Elements.Add({ H.GetRawPtr(i), nullptr, nullptr });
		}
		else if (Prop->ArrayDim > 1)
		{
			ElementProp = Prop;
			for (int32 i = 0; i < Prop->ArrayDim; ++i) Elements.Add({ Prop->ContainerPtrToValuePtr<void>(Container, i), nullptr, nullptr });
		}
		else if (CastField<FMapProperty>(Prop) || CastField<FSetProperty>(Prop))
		{
			OutError = FString::Printf(
				TEXT("'%s' is a %s. [Field=Value] selects an element of an array of structs or objects; map and set entries are not addressable by path, so read or write the whole '%s' instead (at '%s')"),
				*Seg.Name, CastField<FMapProperty>(Prop) ? TEXT("TMap") : TEXT("TSet"), *Seg.Name, *DottedName);
			return false;
		}
		else
		{
			OutError = FString::Printf(TEXT("'%s' is not an array but was selected by key [%s=%s] (at '%s')"),
				*Seg.Name, *Seg.KeyField, *Seg.KeyValue, *DottedName);
			return false;
		}

		FString ElementTypeName = ElementProp->GetCPPType();
		if (FStructProperty* SP = CastField<FStructProperty>(ElementProp))
		{
			const bool bInstanced = SP->Struct == FInstancedStruct::StaticStruct();
			for (FCandidate& E : Elements)
			{
				if (bInstanced)
				{
					const FInstancedStruct* IS = static_cast<const FInstancedStruct*>(E.ElementAddr);
					E.Struct = IS->GetScriptStruct();
					E.Memory = IS->GetMemory();
				}
				else
				{
					E.Struct = SP->Struct;
					E.Memory = E.ElementAddr;
				}
			}
		}
		// FObjectProperty, not the base: descent only follows hard references,
		// so matching a soft or weak one would select an element the rest of
		// the path cannot enter.
		else if (FObjectProperty* OP = CastField<FObjectProperty>(ElementProp))
		{
			for (FCandidate& E : Elements)
			{
				UObject* Obj = OP->GetObjectPropertyValue(E.ElementAddr);
				E.Struct = Obj ? Obj->GetClass() : nullptr;
				E.Memory = Obj;
			}
		}
		else
		{
			OutError = FString::Printf(
				TEXT("'%s' holds %s elements, which have no fields to match [%s=%s] against. Select them by index instead (at '%s')"),
				*Seg.Name, *ElementTypeName, *Seg.KeyField, *Seg.KeyValue, *DottedName);
			return false;
		}

		constexpr int32 MaxListed = 20;
		TArray<int32> Matches;
		TArray<FString> SeenKeys;
		int32 KeyedCount = 0;
		FString CaseOnlyNearMiss;
		TArray<FString> KeyParts;
		Seg.KeyField.ParseIntoArray(KeyParts, TEXT("."), false);
		for (int32 i = 0; i < Elements.Num(); ++i)
		{
			const FCandidate& E = Elements[i];
			if (!E.Struct || !E.Memory) continue;
			// A nested key walks in-place structs only; it never follows a
			// reference to another asset while looking for a matching field.
			const UStruct* KeyStruct = E.Struct;
			const void* KeyAddr = E.Memory;
			FProperty* KeyProp = nullptr;
			for (int32 Part = 0; Part < KeyParts.Num(); ++Part)
			{
				KeyProp = KeyStruct && !KeyParts[Part].IsEmpty()
					? KeyStruct->FindPropertyByName(FName(*KeyParts[Part])) : nullptr;
				if (!KeyProp) break;
				KeyAddr = KeyProp->ContainerPtrToValuePtr<void>(KeyAddr);
				if (Part + 1 < KeyParts.Num())
				{
					const FStructProperty* Nested = CastField<FStructProperty>(KeyProp);
					KeyStruct = Nested && KeyProp->ArrayDim == 1 ? Nested->Struct : nullptr;
					if (KeyStruct == FInstancedStruct::StaticStruct())
					{
						const FInstancedStruct* Instance = static_cast<const FInstancedStruct*>(KeyAddr);
						KeyStruct = Instance->GetScriptStruct();
						KeyAddr = Instance->GetMemory();
					}
				}
			}
			if (!KeyProp || !KeyAddr) continue;
			FString KeyText;
			if (!PropertyKeyText(KeyProp, KeyAddr, KeyText))
			{
				OutError = FString::Printf(
					TEXT("'%s' on %s elements is a %s, which cannot be a key. A key field is a name, string, text, enum, integer, bool, gameplay tag or object reference on the element or a nested struct (at '%s')"),
					*Seg.KeyField, *ElementTypeName, *KeyProp->GetCPPType(), *DottedName);
				return false;
			}
			++KeyedCount;
			if (SeenKeys.Num() < MaxListed) SeenKeys.Add(KeyText);
			if (PropertyKeyMatches(KeyProp, KeyAddr, KeyText, Seg.KeyValue)) Matches.Add(i);
			else if (CaseOnlyNearMiss.IsEmpty() && KeyText.Equals(Seg.KeyValue, ESearchCase::IgnoreCase)) CaseOnlyNearMiss = KeyText;
		}

		if (KeyedCount == 0)
		{
			// Name the fields that could have been meant, from the first element
			// that has any.
			TArray<FString> KeyFields;
			for (const FCandidate& E : Elements)
			{
				if (!E.Struct || !E.Memory) continue;
				for (TFieldIterator<FProperty> It(E.Struct); It && KeyFields.Num() < MaxListed; ++It)
				{
					FString Ignored;
					if (PropertyKeyText(*It, It->ContainerPtrToValuePtr<void>(E.Memory), Ignored)) KeyFields.Add(It->GetName());
				}
				break;
			}
			OutError = Elements.Num() == 0
				? FString::Printf(TEXT("'%s' is empty, so no element has %s == '%s' (at '%s')"),
					*Seg.Name, *Seg.KeyField, *Seg.KeyValue, *DottedName)
				: FString::Printf(TEXT("no element of '%s' (%s) has a field named '%s'. Fields usable as keys: [%s] (at '%s')"),
					*Seg.Name, *ElementTypeName, *Seg.KeyField, *FString::Join(KeyFields, TEXT(", ")), *DottedName);
			return false;
		}

		if (Matches.Num() == 1)
		{
			OutIndex = Matches[0];
			return true;
		}

		if (Matches.Num() == 0)
		{
			const FString More = KeyedCount > SeenKeys.Num()
				? FString::Printf(TEXT(", ... %d more"), KeyedCount - SeenKeys.Num()) : FString();
			const FString NearMiss = CaseOnlyNearMiss.IsEmpty()
				? FString()
				: FString::Printf(TEXT(" '%s' differs only in case; keys match case-sensitively."), *CaseOnlyNearMiss);
			OutError = FString::Printf(
				TEXT("no element of '%s' (%d elements) has %s == '%s'.%s %s values: [%s%s] (at '%s')"),
				*Seg.Name, Elements.Num(), *Seg.KeyField, *Seg.KeyValue, *NearMiss,
				*Seg.KeyField, *FString::Join(SeenKeys, TEXT(", ")), *More, *DottedName);
			return false;
		}

		TArray<FString> MatchIndices;
		for (int32 m = 0; m < Matches.Num() && m < MaxListed; ++m) MatchIndices.Add(FString::FromInt(Matches[m]));
		if (Matches.Num() > MaxListed) MatchIndices.Add(FString::Printf(TEXT("... %d more"), Matches.Num() - MaxListed));
		OutError = FString::Printf(
			TEXT("%s == '%s' matches %d elements of '%s' (indices %s), and a key must name exactly one. Address one by index, e.g. '%s[%d]' (at '%s')"),
			*Seg.KeyField, *Seg.KeyValue, Matches.Num(), *Seg.Name, *FString::Join(MatchIndices, TEXT(", ")),
			*Seg.Name, Matches[0], *DottedName);
		return false;
	}

	// Select one element while keeping the container's own addressing rules
	// for dynamic and C-style fixed arrays (#927).
	inline bool SelectPathElement(FProperty*& Prop, void* Container, void*& ValueAddr,
		const FPropertyPathSegment& Seg, const FString& DottedName, int32& Index, FString& OutError)
	{
		const FString& Token = Seg.Name;
		Index = Seg.bHasIndex ? Seg.Index : INDEX_NONE;
		if (Seg.bHasKey)
		{
			if (!SelectElementByKey(Prop, Container, ValueAddr, Seg, DottedName, Index, OutError)) return false;
		}
		if (Seg.bHasIndex || Seg.bHasKey)
		{
			if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
			{
				FScriptArrayHelper H(ArrProp, ValueAddr);
				if (Index < 0 || Index >= H.Num()) { OutError = FString::Printf(TEXT("index %d out of range on '%s' (num=%d)"), Index, *Token, H.Num()); return false; }
				Prop = ArrProp->Inner;
				ValueAddr = H.GetRawPtr(Index);
			}
			// C-style fixed arrays are one FProperty with ArrayDim > 1.
			// Address each element from its container (#927).
			else if (Prop->ArrayDim > 1)
			{
				if (Index < 0 || Index >= Prop->ArrayDim)
				{
					OutError = FString::Printf(TEXT("index %d out of range on fixed array '%s' (ArrayDim=%d)"), Index, *Token, Prop->ArrayDim);
					return false;
				}
				// Re-address from the container so the element offset is the
				// engine's own, rather than adding a hand-computed stride.
				ValueAddr = Prop->ContainerPtrToValuePtr<void>(Container, Index);
			}
			else
			{
				OutError = FString::Printf(TEXT("'%s' is not an array but was indexed [%d]"), *Token, Index);
				return false;
			}
		}

		return true;
	}
}
