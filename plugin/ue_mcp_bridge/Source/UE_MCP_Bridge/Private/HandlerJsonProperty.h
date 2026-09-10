#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/SoftObjectPtr.h"
#include "MCPEngineCompat.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Engine/Blueprint.h"
#include "HandlerPropertyText.h"

// Shared recursive JSON→FProperty setter. Originally written for PCG
// set_pcg_node_settings (#149); also used by set_component_property on
// Blueprint component templates (#152) and set_water_body_property (#151-ish).
//
// Handles TArray, TSet, TMap, nested struct objects, UObject/class references
// by path, and soft references. Falls back to export-text import for scalars.
namespace MCPJsonProperty
{
	inline bool SetJsonOnProperty(FProperty* Prop, void* ValueAddr, const TSharedPtr<FJsonValue>& InValue, FString& OutError);

	// The reference-property kinds, as one closed answer per property.
	//
	// The reflection types nest: FClassProperty derives from FObjectProperty,
	// and FSoftClassProperty from FSoftObjectProperty; FWeakObjectProperty and
	// FLazyObjectProperty are siblings of both under FObjectPropertyBase.
	// CastField<Base>() succeeds on a derived instance, so a chain that tests
	// the base first swallows the derived kind and leaves the derived branch
	// unreachable. That is exactly how a TSubclassOf<> field came to be
	// resolved by the generic asset loader instead of the class loader: the
	// class branch sat below the object branch and never ran (#935).
	//
	// Classifying once and switching on the answer is used here in preference
	// to simply reordering the branches. Ordering is invisible at the point of
	// use, is not checkable by a test, and one tidy-up edit that moves a block
	// silently reinstates the bug. A wrong answer here is a single function to
	// read and a single function to test, and no later edit to the call sites
	// can bring the dead branch back.
	enum class ERefKind : uint8
	{
		NotAReference,
		SoftClass,
		SoftObject,
		Class,
		Object,
		WeakObject,
		LazyObject,
		Interface,
	};

	inline ERefKind ClassifyReference(const FProperty* Prop)
	{
		if (!Prop) return ERefKind::NotAReference;
		// Most derived first: each test is reached only once every
		// more-derived test above it has failed.
		if (Prop->IsA<FSoftClassProperty>())  return ERefKind::SoftClass;
		if (Prop->IsA<FSoftObjectProperty>()) return ERefKind::SoftObject;
		if (Prop->IsA<FClassProperty>())      return ERefKind::Class;
		if (Prop->IsA<FObjectProperty>())     return ERefKind::Object;
		if (Prop->IsA<FWeakObjectProperty>()) return ERefKind::WeakObject;
		if (Prop->IsA<FLazyObjectProperty>()) return ERefKind::LazyObject;
		if (Prop->IsA<FInterfaceProperty>())  return ERefKind::Interface;
		return ERefKind::NotAReference;
	}

	// Resolve a class path the way a caller means it, without inventing a
	// suffix the path did not ask for.
	//
	// #489: a caller naming a Blueprint commonly passes the asset path
	// (/Game/Foo/BP_GameMode.BP_GameMode) for a class-typed field, and the
	// generated class is that path plus "_C".
	// #928: the suffix used to be appended unconditionally on the soft-class
	// path, so a native /Script/Module.ClassName reference was stored as
	// /Script/Module.ClassName_C, which names no class at all, and the write
	// still reported success. The suffix is now only ever used when the path
	// as written does not name a class and the suffixed form does.
	inline UClass* ResolveClassPath(const FString& Path)
	{
		if (Path.IsEmpty()) return nullptr;

		if (UClass* Direct = LoadClass<UObject>(nullptr, *Path))
		{
			return Direct;
		}
		// A native class lives at its /Script/ path and never carries "_C".
		if (!Path.StartsWith(TEXT("/Script/")) && !Path.EndsWith(TEXT("_C")))
		{
			const FString WithSuffix = Path + TEXT("_C");
			if (UClass* Generated = LoadClass<UObject>(nullptr, *WithSuffix))
			{
				return Generated;
			}
		}
		// Last ditch: load the asset as a UBlueprint and take its generated
		// class. Covers paths written without the .Name object suffix.
		if (UBlueprint* BP = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *Path)))
		{
			return BP->GeneratedClass;
		}
		return nullptr;
	}

	// One (key, value) write for a TMap, in whichever JSON shape the caller
	// used. `KeyJson` is null for the object shape, where the field name is the
	// key text.
	struct FMapPairInput
	{
		FString KeyText;
		TSharedPtr<FJsonValue> KeyJson;
		TSharedPtr<FJsonValue> ValueJson;
	};

	// Rebuild a TMap from parsed pairs, keys first, straight onto the key and
	// value properties. #820: the old path could only name keys as JSON object
	// fields, so a struct key had to travel as export text and was dropped in
	// silence. Every pair is now checked in: a duplicate key, an unusable key,
	// or a final element count that does not match what the caller asked for is
	// an error, and the caller's snapshot restores the previous map.
	inline bool SetMapPairs(FMapProperty* MapProp, void* ValueAddr, const TArray<FMapPairInput>& Pairs, FString& OutError)
	{
		FScriptMapHelper Helper(MapProp, ValueAddr);
		Helper.EmptyValues();

		for (const FMapPairInput& Pair : Pairs)
		{
			FDefaultConstructedPropertyElement TempKey(MapProp->KeyProp);
			TSharedPtr<FJsonValue> KeyValue = Pair.KeyJson;
			if (!KeyValue.IsValid())
			{
				KeyValue = MakeShared<FJsonValueString>(Pair.KeyText);
			}

			FString E;
			if (!SetJsonOnProperty(MapProp->KeyProp, TempKey.GetObjAddress(), KeyValue, E))
			{
				OutError = FString::Printf(TEXT("map key '%s': %s"), *Pair.KeyText, *E);
				return false;
			}
			if (Helper.Num() > 0 && Helper.FindValueFromHash(TempKey.GetObjAddress()) != nullptr)
			{
				OutError = FString::Printf(TEXT("map key '%s' appears twice"), *Pair.KeyText);
				return false;
			}

			void* ValuePtr = Helper.FindOrAdd(TempKey.GetObjAddress());
			if (!SetJsonOnProperty(MapProp->ValueProp, ValuePtr, Pair.ValueJson, E))
			{
				OutError = FString::Printf(TEXT("map value for key '%s': %s"), *Pair.KeyText, *E);
				return false;
			}
		}

		if (Helper.Num() != Pairs.Num())
		{
			OutError = FString::Printf(
				TEXT("map would have lost entries: %d pair(s) given, %d stored. Refusing the write"),
				Pairs.Num(), Helper.Num());
			return false;
		}
		return true;
	}

	inline bool SetJsonOnPropertyImpl(FProperty* Prop, void* ValueAddr, const TSharedPtr<FJsonValue>& InValue, FString& OutError)
	{
		if (!Prop || !InValue.IsValid() || !ValueAddr) { OutError = TEXT("null property/value/addr"); return false; }

		TSharedPtr<FJsonValue> Value = InValue;

		// #517/#531: some MCP clients double-encode complex arguments, so an
		// array/object value arrives as a JSON *string* ("[{...}]") rather than a
		// real JSON array/object. For container/struct targets, transparently
		// re-parse a JSON-looking string back into a JSON value so the structured
		// branches below fire instead of bouncing off "expected JSON array". A
		// non-JSON string (e.g. UE export-text "((A=1),(B=2))") is left untouched
		// and handled by the ImportText fallback at the bottom.
		if (Value->Type == EJson::String &&
			(Prop->IsA<FArrayProperty>() || Prop->IsA<FSetProperty>() || Prop->IsA<FMapProperty>() || Prop->IsA<FStructProperty>()))
		{
			const FString Trimmed = Value->AsString().TrimStartAndEnd();
			if (Trimmed.StartsWith(TEXT("[")) || Trimmed.StartsWith(TEXT("{")))
			{
				TSharedPtr<FJsonValue> Reparsed;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Trimmed);
				if (FJsonSerializer::Deserialize(Reader, Reparsed) && Reparsed.IsValid())
				{
					Value = Reparsed;
				}
			}
		}

		// #420: explicit JSON null clears TObjectPtr<>/FSoftObjectPtr/FWeakObjectPtr/
		// UClass*/FScriptInterface. The natural shape for clearing an AnimClass,
		// Override Material, default Pawn class, etc. Previously the call fell through
		// to ImportText and surfaced "asset not found: None".
		if (Value->Type == EJson::Null)
		{
			// One dispatch on the classified kind: FClassProperty used to sit
			// below FObjectProperty here and never be reached (#935). Clearing
			// happens to mean the same thing for both, so this branch was not
			// the one that corrupted data, but leaving the same inverted chain
			// in place invites the next reader to copy it.
			switch (ClassifyReference(Prop))
			{
			case ERefKind::SoftClass:
			case ERefKind::SoftObject:
				CastFieldChecked<FSoftObjectProperty>(Prop)->SetPropertyValue(ValueAddr, FSoftObjectPtr());
				return true;
			case ERefKind::Class:
			case ERefKind::Object:
				CastFieldChecked<FObjectProperty>(Prop)->SetObjectPropertyValue(ValueAddr, nullptr);
				return true;
			case ERefKind::WeakObject:
				CastFieldChecked<FWeakObjectProperty>(Prop)->SetObjectPropertyValue(ValueAddr, nullptr);
				return true;
			case ERefKind::Interface:
			{
				FScriptInterface Empty;
				CastFieldChecked<FInterfaceProperty>(Prop)->SetPropertyValue(ValueAddr, Empty);
				return true;
			}
			default:
				break;
			}
			OutError = FString::Printf(TEXT("property '%s' is not an object/class/interface reference; null value not allowed"), *Prop->GetName());
			return false;
		}

		// TArray. A JSON array maps element-by-element through the recursive
		// setter (struct elements, object/class refs by path, scalars). If the
		// value is not a JSON array even after the string re-parse above, fall
		// through to the ImportText fallback, which accepts UE export-text
		// "(elem,elem)" for arrays.
		if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
		{
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (Value->TryGetArray(Items) && Items)
			{
				FScriptArrayHelper H(ArrProp, ValueAddr);
				H.Resize(Items->Num());
				for (int32 i = 0; i < Items->Num(); ++i)
				{
					FString E;
					if (!SetJsonOnProperty(ArrProp->Inner, H.GetRawPtr(i), (*Items)[i], E))
					{
						OutError = FString::Printf(TEXT("[%d]: %s"), i, *E); return false;
					}
				}
				return true;
			}
			// else: fall through to ImportText fallback below.
		}

		// TSet
		else if (FSetProperty* SetProp = CastField<FSetProperty>(Prop))
		{
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (Value->TryGetArray(Items) && Items)
			{
				FScriptSetHelper H(SetProp, ValueAddr);
				H.EmptyElements();
				for (const TSharedPtr<FJsonValue>& V : *Items)
				{
					const int32 Idx = H.AddDefaultValue_Invalid_NeedsRehash();
					uint8* ElemAddr = H.GetElementPtr(Idx);
					FString E;
					if (!SetJsonOnProperty(SetProp->ElementProp, ElemAddr, V, E)) { OutError = E; return false; }
				}
				H.Rehash();
				// #820: same count guard the map path uses - a set that stored
				// fewer elements than it was handed is a failed write, not a
				// quiet partial one.
				if (H.Num() != Items->Num())
				{
					OutError = FString::Printf(
						TEXT("set would have lost entries: %d given, %d stored (duplicate elements?). Refusing the write"),
						Items->Num(), H.Num());
					return false;
				}
				return true;
			}
			// else: fall through to ImportText fallback below.
		}

		// TMap. Two shapes are accepted, and the read handlers emit whichever one
		// the map's key type needs (#820):
		//   { "Key": Value }                         - keys that are text
		//   [ { "key": <any>, "value": <any> } ]     - any key, struct keys included
		// Both recurse through this setter for keys and values alike, so struct
		// keys, tag keys, object refs and nested containers all land natively
		// instead of travelling as export text.
		else if (FMapProperty* MapProp = CastField<FMapProperty>(Prop))
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (Value->TryGetObject(Obj) && Obj && (*Obj).IsValid())
			{
				TArray<FMapPairInput> Pairs;
				Pairs.Reserve((*Obj)->Values.Num());
				for (const auto& Field : (*Obj)->Values)
				{
					FMapPairInput Pair;
					Pair.KeyText = Field.Key;
					Pair.ValueJson = Field.Value;
					Pairs.Add(MoveTemp(Pair));
				}
				return SetMapPairs(MapProp, ValueAddr, Pairs, OutError);
			}
			if (Value->TryGetArray(Items) && Items)
			{
				TArray<FMapPairInput> Pairs;
				Pairs.Reserve(Items->Num());
				for (int32 i = 0; i < Items->Num(); ++i)
				{
					const TSharedPtr<FJsonObject>* Entry = nullptr;
					if (!(*Items)[i].IsValid() || !(*Items)[i]->TryGetObject(Entry) || !Entry || !(*Entry).IsValid())
					{
						OutError = FString::Printf(TEXT("map entry [%d] must be an object with 'key' and 'value'"), i);
						return false;
					}
					const TSharedPtr<FJsonValue> KeyJson = (*Entry)->TryGetField(TEXT("key"));
					const TSharedPtr<FJsonValue> ValueJson = (*Entry)->TryGetField(TEXT("value"));
					if (!KeyJson.IsValid() || !ValueJson.IsValid())
					{
						OutError = FString::Printf(TEXT("map entry [%d] is missing 'key' or 'value'"), i);
						return false;
					}
					FMapPairInput Pair;
					Pair.KeyJson = KeyJson;
					Pair.ValueJson = ValueJson;
					KeyJson->TryGetString(Pair.KeyText);
					if (Pair.KeyText.IsEmpty()) Pair.KeyText = FString::Printf(TEXT("[%d]"), i);
					Pairs.Add(MoveTemp(Pair));
				}
				return SetMapPairs(MapProp, ValueAddr, Pairs, OutError);
			}
			// else: fall through to the export-text import below, which parses
			// map pairs itself and verifies the stored count.
		}

		// Struct: recurse on JSON object fields; otherwise fall through to ImportText
		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			// #503: FGameplayTag (and FGameplayTagContainer) can't be built
			// from Python in 5.7 and ImportText("(TagName=\"X\")") is the only
			// portable path. Coerce a plain string ("X.Y") or array of strings
			// into the right runtime tag value via the GameplayTagsManager so
			// callers don't have to format ImportText themselves.
			// #820: match derived tag structs too (a USTRUCT deriving from
			// FGameplayTag is the common way to type a tag field, and such a
			// struct is exactly what a struct-keyed TMap tends to be keyed on).
			// The tag lives in the FGameplayTag base, which starts at offset 0.
			const bool bIsTag = StructProp->Struct->IsChildOf(FGameplayTag::StaticStruct());
			const bool bIsTagContainer = StructProp->Struct->IsChildOf(FGameplayTagContainer::StaticStruct());
			if (bIsTag)
			{
				FString TagStr;
				if (Value->TryGetString(TagStr))
				{
					FGameplayTag* TagPtr = static_cast<FGameplayTag*>(ValueAddr);
					// "None" is what an unset FName prints as, so accept it as
					// the same "no tag" the empty string means (#820).
					if (TagStr.IsEmpty() || TagStr == TEXT("None"))
					{
						*TagPtr = FGameplayTag();
						return true;
					}
					FGameplayTag Resolved = UGameplayTagsManager::Get().RequestGameplayTag(FName(*TagStr), /*ErrorIfNotFound*/ false);
					if (!Resolved.IsValid())
					{
						OutError = FString::Printf(TEXT("gameplay tag not found: %s"), *TagStr);
						return false;
					}
					*TagPtr = Resolved;
					return true;
				}
			}
			else if (bIsTagContainer)
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
				FString SingleStr;
				if (Value->TryGetArray(Arr) && Arr)
				{
					FGameplayTagContainer* ContainerPtr = static_cast<FGameplayTagContainer*>(ValueAddr);
					ContainerPtr->Reset();
					for (const TSharedPtr<FJsonValue>& V : *Arr)
					{
						FString S;
						if (!V->TryGetString(S) || S.IsEmpty()) continue;
						FGameplayTag Resolved = UGameplayTagsManager::Get().RequestGameplayTag(FName(*S), false);
						if (!Resolved.IsValid())
						{
							OutError = FString::Printf(TEXT("gameplay tag not found: %s"), *S);
							return false;
						}
						ContainerPtr->AddTag(Resolved);
					}
					return true;
				}
				else if (Value->TryGetString(SingleStr) && !SingleStr.IsEmpty())
				{
					FGameplayTagContainer* ContainerPtr = static_cast<FGameplayTagContainer*>(ValueAddr);
					ContainerPtr->Reset();
					FGameplayTag Resolved = UGameplayTagsManager::Get().RequestGameplayTag(FName(*SingleStr), false);
					if (!Resolved.IsValid())
					{
						OutError = FString::Printf(TEXT("gameplay tag not found: %s"), *SingleStr);
						return false;
					}
					ContainerPtr->AddTag(Resolved);
					return true;
				}
			}

			const TSharedPtr<FJsonObject>* SubObj = nullptr;
			if (Value->TryGetObject(SubObj) && SubObj && (*SubObj).IsValid())
			{
				for (const auto& Pair : (*SubObj)->Values)
				{
					FProperty* SubProp = StructProp->Struct->FindPropertyByName(FName(*Pair.Key));
					if (!SubProp) { OutError = FString::Printf(TEXT("struct field '%s' not found"), *Pair.Key); return false; }
					void* SubAddr = SubProp->ContainerPtrToValuePtr<void>(ValueAddr);
					FString E;
					if (!SetJsonOnProperty(SubProp, SubAddr, Pair.Value, E))
					{
						OutError = FString::Printf(TEXT("%s.%s: %s"), *StructProp->GetName(), *Pair.Key, *E); return false;
					}
				}
				return true;
			}
		}

		// Object, class and soft references, all addressed by path. One
		// dispatch on the classified kind (see ClassifyReference), so the
		// class kinds are handled by the class-aware code rather than being
		// swallowed by their base's branch (#935). A value that is not a
		// string falls out of the switch to the ImportText fallback below,
		// exactly as it did when these were separate `if` blocks.
		switch (ClassifyReference(Prop))
		{
		case ERefKind::Class:
		{
			FString Path;
			if (Value->TryGetString(Path) && !Path.IsEmpty())
			{
				UClass* Loaded = ResolveClassPath(Path);
				if (!Loaded) { OutError = FString::Printf(TEXT("class not found: %s"), *Path); return false; }
				CastFieldChecked<FClassProperty>(Prop)->SetObjectPropertyValue(ValueAddr, Loaded);
				return true;
			}
			break;
		}
		case ERefKind::Object:
		{
			FString Path;
			if (Value->TryGetString(Path) && !Path.IsEmpty())
			{
				FObjectProperty* ObjProp = CastFieldChecked<FObjectProperty>(Prop);
				UObject* Loaded = StaticLoadObject(ObjProp->PropertyClass, nullptr, *Path);
				if (!Loaded) { OutError = FString::Printf(TEXT("asset not found: %s"), *Path); return false; }
				ObjProp->SetObjectPropertyValue(ValueAddr, Loaded);
				return true;
			}
			break;
		}
		case ERefKind::SoftClass:
		{
			FString Path;
			if (Value->TryGetString(Path))
			{
				FSoftClassProperty* SoftClassProp = CastFieldChecked<FSoftClassProperty>(Prop);
				if (Path.IsEmpty())
				{
					SoftClassProp->SetPropertyValue(ValueAddr, FSoftObjectPtr());
					return true;
				}
				// #928: store the path that actually names a class. Only a
				// Blueprint-generated class carries the "_C" suffix, and it is
				// added only when the path as written names nothing and the
				// suffixed form does. A path that resolves to neither is
				// stored verbatim: a soft reference to an asset that does not
				// exist yet is legitimate, and silently rewriting it was how a
				// native /Script/ reference got corrupted.
				if (UClass* Resolved = ResolveClassPath(Path))
				{
					SoftClassProp->SetPropertyValue(ValueAddr, FSoftObjectPtr(FSoftObjectPath(Resolved)));
					return true;
				}
				SoftClassProp->SetPropertyValue(ValueAddr, FSoftObjectPtr(FSoftObjectPath(Path)));
				return true;
			}
			break;
		}
		case ERefKind::SoftObject:
		{
			FString Path;
			if (Value->TryGetString(Path))
			{
				FSoftObjectPath PathObj(Path);
				FSoftObjectPtr Ptr(PathObj);
				CastFieldChecked<FSoftObjectProperty>(Prop)->SetPropertyValue(ValueAddr, Ptr);
				return true;
			}
			break;
		}
		default:
			break;
		}

		// Enum / Byte-with-enum: accept friendly aliases ("center", "Center"),
		// short names ("HAlign_Center"), and full prefixed names
		// ("EHorizontalAlignment::HAlign_Center"). Numeric values still go
		// through ImportText. (#287)
		auto TryResolveEnumValue = [](UEnum* Enum, const FString& InStr, int64& OutValue) -> bool
		{
			if (!Enum) return false;
			// 1. Direct: full or short name as written.
			int64 V = Enum->GetValueByNameString(InStr);
			if (V != INDEX_NONE) { OutValue = V; return true; }
			// 2. With type-prefix joined by ::.
			FString Prefixed = FString::Printf(TEXT("%s::%s"), *Enum->GetName(), *InStr);
			V = Enum->GetValueByNameString(Prefixed);
			if (V != INDEX_NONE) { OutValue = V; return true; }
			// 3. Friendly fallback - match each enumerator's display name and
			//    short-form name case-insensitively. Walks all enumerators so
			//    "center" matches HAlign_Center, "left" matches HAlign_Left,
			//    "EHTA_Center" matches itself, etc.
			const FString InLower = InStr.ToLower();
			for (int32 i = 0; i < Enum->NumEnums() - 1; i++)
			{
				const FName EntryName = Enum->GetNameByIndex(i);
				FString Short = Enum->GetNameStringByIndex(i);
				if (Short.ToLower() == InLower) { OutValue = Enum->GetValueByIndex(i); return true; }
				// Strip prefix up to last '_' and compare ("HAlign_Center" -> "Center").
				int32 UnderscorePos = INDEX_NONE;
				if (Short.FindLastChar(TEXT('_'), UnderscorePos))
				{
					FString Tail = Short.Mid(UnderscorePos + 1);
					if (Tail.ToLower() == InLower) { OutValue = Enum->GetValueByIndex(i); return true; }
				}
				const FText DisplayName = Enum->GetDisplayNameTextByIndex(i);
				if (DisplayName.ToString().ToLower() == InLower) { OutValue = Enum->GetValueByIndex(i); return true; }
			}
			return false;
		};

		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			FString Str;
			if (Value->TryGetString(Str) && !Str.IsEmpty())
			{
				int64 EnumVal;
				if (TryResolveEnumValue(EnumProp->GetEnum(), Str, EnumVal))
				{
					EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValueAddr, EnumVal);
					return true;
				}
				OutError = FString::Printf(TEXT("unknown enum value '%s' for %s"), *Str, *EnumProp->GetEnum()->GetName());
				return false;
			}
		}
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			FString Str;
			if (Value->TryGetString(Str) && !Str.IsEmpty())
			{
				if (UEnum* Enum = ByteProp->Enum)
				{
					int64 EnumVal;
					if (TryResolveEnumValue(Enum, Str, EnumVal))
					{
						ByteProp->SetPropertyValue(ValueAddr, (uint8)EnumVal);
						return true;
					}
					OutError = FString::Printf(TEXT("unknown enum value '%s' for %s"), *Str, *Enum->GetName());
					return false;
				}
			}
		}

		// Numbers, assigned as numbers rather than rendered as text.
		//
		// Everything past this point turns the value into a string and hands it
		// to the import parser, and a JSON number renders through
		// FJsonValueNumber::TryGetString, which is FString::SanitizeFloat, which
		// is Printf("%f"): six fractional digits and no more. A double property
		// asked for 1e-9 stored 0, one asked for 0.123456789 stored 0.123457,
		// and because this setter recurses through here for every component of a
		// struct, a vector or a transform lost the same digits field by field.
		// Assigning through FNumericProperty skips the text entirely, so what
		// lands is what arrived.
		//
		// Three kinds of property stay on the text path deliberately:
		//   - FBoolProperty and FEnumProperty are not FNumericProperty at all,
		//     so neither can be caught here whatever the value is;
		//   - an enum-valued byte (FByteProperty with an Enum) belongs to the
		//     enum branches above, and for a numeric value the import parser is
		//     what checks the number names a real enumerator. An enumerator is a
		//     small exact integer, so it never needed the precision fix;
		//   - a number carried as text (TJsonValueNumberString: its type is
		//     Number but it prefers its string form) keeps every digit it was
		//     written with, and the parser reads all of them.
		if (Value->Type == EJson::Number && !Value->PreferStringRepresentation())
		{
			FNumericProperty* NumProp = CastField<FNumericProperty>(Prop);
			if (NumProp && !NumProp->IsEnum())
			{
				const double Number = Value->AsNumber();
				if (NumProp->IsFloatingPoint())
				{
					// A float target narrows the double to its own precision.
					// That narrowing belongs to the property's type and not to
					// the transport: it is exactly what assigning the value in
					// C++ would do.
					NumProp->SetFloatingPointPropertyValue(ValueAddr, Number);
					return true;
				}
				if (NumProp->IsInteger())
				{
					// An integer target. A JSON number is a double, so every
					// way it can fail to name the integer the caller meant is
					// refused here rather than rounded, clamped or truncated
					// into a different one.
					const FString Rendered = FString::Printf(TEXT("%.17g"), Number);
					if (!FMath::IsFinite(Number) || Number != FMath::TruncToDouble(Number))
					{
						OutError = FString::Printf(
							TEXT("property '%s' is %s and %s is not a whole number"),
							*Prop->GetName(), *Prop->GetCPPType(), *Rendered);
						return false;
					}

					// The width the target actually holds, named property class
					// by property class. This is the whole closed set of
					// integer property types, and naming them is what makes an
					// unsigned target refuse a negative value: asking
					// FNumericProperty::CanHoldValue instead would answer "yes"
					// to -1 into a uint64, because that cast round trips back
					// to -1 and compares equal.
					int64 Lowest = 0;
					int64 Highest = 0;
					bool bKnownWidth = true;
					if (Prop->IsA<FInt8Property>())        { Lowest = TNumericLimits<int8>::Lowest();   Highest = TNumericLimits<int8>::Max(); }
					else if (Prop->IsA<FInt16Property>())  { Lowest = TNumericLimits<int16>::Lowest();  Highest = TNumericLimits<int16>::Max(); }
					else if (Prop->IsA<FIntProperty>())    { Lowest = TNumericLimits<int32>::Lowest();  Highest = TNumericLimits<int32>::Max(); }
					else if (Prop->IsA<FInt64Property>())  { Lowest = TNumericLimits<int64>::Lowest();  Highest = TNumericLimits<int64>::Max(); }
					else if (Prop->IsA<FByteProperty>())   { Lowest = 0; Highest = TNumericLimits<uint8>::Max(); }
					else if (Prop->IsA<FUInt16Property>()) { Lowest = 0; Highest = TNumericLimits<uint16>::Max(); }
					else if (Prop->IsA<FUInt32Property>()) { Lowest = 0; Highest = (int64)TNumericLimits<uint32>::Max(); }
					// A uint64 above int64's maximum is past the point a JSON
					// number carries an integer at all, so int64's maximum is
					// the honest ceiling for it too.
					else if (Prop->IsA<FUInt64Property>()) { Lowest = 0; Highest = TNumericLimits<int64>::Max(); }
					else { bKnownWidth = false; }

					if (bKnownWidth)
					{
						// Compared as doubles, because the cast to int64 is
						// undefined outside int64's own range. Every bound
						// below 2^53 converts exactly; int64's maximum rounds
						// up to 2^63, so a value between the two passes here
						// and is refused a few lines down by the 2^53 test,
						// which is what keeps the cast in range.
						if (Number < (double)Lowest || Number > (double)Highest)
						{
							OutError = FString::Printf(
								TEXT("value %s is out of range for %s property '%s'"),
								*Rendered, *Prop->GetCPPType(), *Prop->GetName());
							return false;
						}

						// Past 2^53 a double no longer carries every integer,
						// so the value here need not be the one the caller
						// wrote: those digits were gone before this code ever
						// saw them, when the JSON was parsed into a double.
						// Refusing says so out loud. The same number sent as a
						// JSON string reaches the import parser below, which
						// reads it at full 64-bit width.
						constexpr double ExactIntegerLimit = 9007199254740992.0; // 2^53
						if (Number > ExactIntegerLimit || Number < -ExactIntegerLimit)
						{
							OutError = FString::Printf(
								TEXT("value %s is beyond the range a JSON number carries integers exactly (2^53), so '%s' would not receive the integer that was written. Send it as a JSON string instead"),
								*Rendered, *Prop->GetName());
							return false;
						}

						const int64 IntValue = (int64)Number;
						NumProp->SetIntPropertyValue(ValueAddr, IntValue);
						return true;
					}
					// An integer width this build does not know about keeps the
					// behaviour it had: the text path below.
				}
			}
		}

		// Fallback: coerce JSON to string and import it as UE export text.
		// #820: map-bearing text goes through MCPPropertyText, which reads the
		// pairs itself and refuses a write that would store fewer entries than
		// the text listed. The engine importer used to accept such text, drop
		// every pair, and report success.
		FString Str;
		if (Value->TryGetString(Str)) {}
		else if (Value->Type == EJson::Number) Str = FString::SanitizeFloat(Value->AsNumber());
		else if (Value->Type == EJson::Boolean) Str = Value->AsBool() ? TEXT("true") : TEXT("false");
		else Str = Value->AsString();

		return MCPPropertyText::ImportTextIntoProperty(Prop, ValueAddr, Str, nullptr, OutError);
	}

	// #820: a rejected write must leave the previous value exactly as it was.
	// Container and struct writes mutate in place (EmptyValues, Resize,
	// field-by-field assignment), so a failure part way through would otherwise
	// leave a half-written or empty container behind, which is how a struct-keyed
	// TMap used to end up wiped. Snapshot the destination on the outermost call
	// and restore it if anything underneath reports an error.
	inline bool SetJsonOnProperty(FProperty* Prop, void* ValueAddr, const TSharedPtr<FJsonValue>& InValue, FString& OutError)
	{
		static thread_local int32 NestDepth = 0;

		const bool bSnapshot = NestDepth == 0 && Prop && ValueAddr &&
			(Prop->IsA<FMapProperty>() || Prop->IsA<FSetProperty>() ||
			 Prop->IsA<FArrayProperty>() || Prop->IsA<FStructProperty>());

		if (!bSnapshot)
		{
			++NestDepth;
			const bool bOk = SetJsonOnPropertyImpl(Prop, ValueAddr, InValue, OutError);
			--NestDepth;
			return bOk;
		}

		FDefaultConstructedPropertyElement Backup(Prop);
		Prop->CopyCompleteValue(Backup.GetObjAddress(), ValueAddr);

		++NestDepth;
		const bool bOk = SetJsonOnPropertyImpl(Prop, ValueAddr, InValue, OutError);
		--NestDepth;

		if (!bOk)
		{
			Prop->CopyCompleteValue(ValueAddr, Backup.GetObjAddress());
		}
		return bOk;
	}

	// ── Readback verification ────────────────────────────────────────────────
	//
	// #935: a write that did not take must not come back as a success. The
	// setter returns true the moment an assignment executes, and nothing used
	// to look at what was actually stored afterwards, so a reference that
	// resolved to the wrong thing, a container that quietly emptied, or a
	// value that never reached the row the caller named all reported
	// success: true with the broken asset saved to disk.

	/** A compact, readable rendering of a JSON value for an error message. */
	inline FString DescribeJsonValue(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid()) return TEXT("(none)");
		switch (Value->Type)
		{
		case EJson::Null:    return TEXT("null");
		case EJson::Boolean: return Value->AsBool() ? TEXT("true") : TEXT("false");
		case EJson::Number:  return FString::SanitizeFloat(Value->AsNumber());
		case EJson::String:  return Value->AsString();
		default:
			break;
		}
		FString Out;
		using FCondensedWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;
		const TSharedRef<FCondensedWriter> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		if (Value->Type == EJson::Object)
		{
			FJsonSerializer::Serialize(Value->AsObject().ToSharedRef(), Writer);
		}
		else
		{
			FJsonSerializer::Serialize(Value->AsArray(), Writer);
		}
		return Out;
	}

	/** True when two independent applications of one request must produce
	 *  values that compare Identical.
	 *
	 *  Two kinds of value are not in that class, and both would report a
	 *  mismatch that is not one:
	 *
	 *  FText identity is per instance. Two imports of the same source string
	 *  carry different keys and are never Identical.
	 *
	 *  An instanced reference is a fresh subobject on every import, so the two
	 *  applications point at two different objects by design.
	 *
	 *  Anything unrecognised, and anything past the depth bound, answers
	 *  "no", so the conservative path is the one that skips the comparison
	 *  rather than the one that fails a write that was fine. */
	inline bool PropertyIsComparableByIdentity(const FProperty* Prop, int32 Depth = 0)
	{
		if (!Prop || Depth > 12) return false;
		if (Prop->HasAnyPropertyFlags(CPF_InstancedReference | CPF_ContainsInstancedReference)) return false;
		if (Prop->IsA<FTextProperty>()) return false;

		if (const FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
		{
			return PropertyIsComparableByIdentity(ArrProp->Inner, Depth + 1);
		}
		if (const FSetProperty* SetProp = CastField<FSetProperty>(Prop))
		{
			return PropertyIsComparableByIdentity(SetProp->ElementProp, Depth + 1);
		}
		if (const FMapProperty* MapProp = CastField<FMapProperty>(Prop))
		{
			return PropertyIsComparableByIdentity(MapProp->KeyProp, Depth + 1)
				&& PropertyIsComparableByIdentity(MapProp->ValueProp, Depth + 1);
		}
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
			{
				if (!PropertyIsComparableByIdentity(*It, Depth + 1)) return false;
			}
		}
		return true;
	}

	/** The comparable form of an asset path.
	 *
	 *  Two normalisations, and only the two the resolver is allowed to apply:
	 *  the "Pkg.Obj" long form of "Pkg" that the content browser writes as
	 *  "Pkg", and the Blueprint generated class "Pkg.Obj_C" of that same
	 *  asset. Both are recognised by the object name repeating the package
	 *  leaf, which is what makes them the same asset; a "_C" that does not
	 *  produce that repetition is part of the name and is left alone, so
	 *  /Script/Engine.Character and /Script/Engine.Character_C stay different
	 *  paths, and an asset genuinely called Foo_C still compares equal to
	 *  itself. A subobject path is compared whole. */
	inline FString NormalizeAssetPathForCompare(const FString& InPath)
	{
		FString Path = InPath.TrimStartAndEnd();
		if (Path.IsEmpty() || Path.Contains(TEXT(":"))) return Path;

		int32 DotPos = INDEX_NONE;
		if (!Path.FindLastChar(TEXT('.'), DotPos)) return Path;

		const FString Package = Path.Left(DotPos);
		const FString ObjectName = Path.Mid(DotPos + 1);

		int32 SlashPos = INDEX_NONE;
		const FString LeafName = Package.FindLastChar(TEXT('/'), SlashPos)
			? Package.Mid(SlashPos + 1)
			: Package;

		if (LeafName.Equals(ObjectName, ESearchCase::IgnoreCase)) return Package;
		if (ObjectName.EndsWith(TEXT("_C")) &&
			LeafName.Equals(ObjectName.LeftChop(2), ESearchCase::IgnoreCase))
		{
			return Package;
		}
		return Path;
	}

	/** Read the stored value back and answer whether it is what was asked for.
	 *
	 *  Three checks, because they fail on different things.
	 *
	 *  A soft reference is never resolved at write time, so its stored text is
	 *  whatever the setter chose to write. Comparing that text against the
	 *  requested path is the only thing that catches a path the setter
	 *  rewrote, which is exactly how a native /Script/ class reference used to
	 *  end up with a "_C" suffix appended (#928).
	 *
	 *  A hard reference that was asked for by path must not have come out
	 *  null. Comparing its path is not useful, because a redirector legally
	 *  answers a different path than the one requested.
	 *
	 *  Everything else is compared by identity against an independent buffer
	 *  the same request was applied to. That is what catches a value that
	 *  never reached the destination: a container that stored fewer entries
	 *  than it was handed, a row copy that dropped the field, a reference
	 *  cleared by a later step. Values whose two applications are not
	 *  required to be identical, text and instanced references, are exempt
	 *  rather than failed.
	 *
	 *  On a mismatch OutDetail carries the requested and the stored value. */
	inline bool VerifyJsonOnProperty(
		FProperty* Prop,
		const void* ValueAddr,
		const TSharedPtr<FJsonValue>& Requested,
		FString& OutDetail)
	{
		if (!Prop || !ValueAddr || !Requested.IsValid()) return true;

		FString StoredText;
		Prop->ExportTextItem_Direct(StoredText, ValueAddr, nullptr, nullptr, PPF_None);

		FString RequestedPath;
		const bool bRequestedPath = Requested->TryGetString(RequestedPath) && !RequestedPath.IsEmpty();
		const ERefKind Kind = ClassifyReference(Prop);

		if (bRequestedPath && (Kind == ERefKind::SoftClass || Kind == ERefKind::SoftObject))
		{
			const FString Stored = CastFieldChecked<FSoftObjectProperty>(Prop)->GetPropertyValue(ValueAddr).ToString();
			if (!NormalizeAssetPathForCompare(Stored).Equals(
					NormalizeAssetPathForCompare(RequestedPath), ESearchCase::IgnoreCase))
			{
				OutDetail = FString::Printf(TEXT("requested '%s', stored '%s'"), *RequestedPath, *Stored);
				return false;
			}
			return true;
		}

		if (bRequestedPath && (Kind == ERefKind::Class || Kind == ERefKind::Object))
		{
			if (CastFieldChecked<FObjectProperty>(Prop)->GetObjectPropertyValue(ValueAddr) == nullptr)
			{
				OutDetail = FString::Printf(TEXT("requested '%s', stored nothing"), *RequestedPath);
				return false;
			}
			return true;
		}

		if (!PropertyIsComparableByIdentity(Prop)) return true;

		FDefaultConstructedPropertyElement Reference(Prop);
		FString Ignored;
		if (!SetJsonOnProperty(Prop, Reference.GetObjAddress(), Requested, Ignored))
		{
			// The same request applied cleanly a moment ago; if it does not
			// apply now the stored value cannot be trusted either.
			OutDetail = FString::Printf(
				TEXT("requested %s, stored '%s' (the request no longer applies: %s)"),
				*DescribeJsonValue(Requested), *StoredText, *Ignored);
			return false;
		}
		if (!Prop->Identical(ValueAddr, Reference.GetObjAddress(), PPF_None))
		{
			FString ReferenceText;
			Prop->ExportTextItem_Direct(ReferenceText, Reference.GetObjAddress(), nullptr, nullptr, PPF_None);
			OutDetail = FString::Printf(
				TEXT("requested %s (which resolves to '%s'), stored '%s'"),
				*DescribeJsonValue(Requested), *ReferenceText, *StoredText);
			return false;
		}
		return true;
	}

	// Resolve a dotted property path that may index arrays ("Traits[2]") and
	// follow object references (instanced subobjects), e.g.
	// "Config.Traits[1].Params.RepresentationActorManagementClass" on a
	// MassEntityConfigAsset. Descends through nested structs (in place), array
	// elements (by index), and FObjectProperty pointers (switching the
	// container to the referenced UObject and its class). On success OutProp is
	// the leaf property, OutValueAddr its value address, and OutOwner the
	// UObject that ultimately owns the leaf (Root, or a followed subobject) so
	// callers can Modify()/MarkPackageDirty the right object. (#527)
	inline bool ResolveDottedPath(UObject* Root, const FString& DottedName,
		FProperty*& OutProp, void*& OutValueAddr, UObject*& OutOwner, FString& OutError)
	{
		if (!Root) { OutError = TEXT("null root object"); return false; }

		TArray<FString> Parts;
		DottedName.ParseIntoArray(Parts, TEXT("."));
		if (Parts.Num() == 0) { OutError = TEXT("empty property name"); return false; }

		void* Container = Root;
		const UStruct* ContainerStruct = Root->GetClass();
		UObject* Owner = Root;

		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			FString Token = Parts[i];
			const FString PathToken = Token;
			int32 Index = INDEX_NONE;
			int32 BracketPos;
			if (Token.FindChar(TEXT('['), BracketPos))
			{
				int32 ClosePos;
				if (Token.FindChar(TEXT(']'), ClosePos) && ClosePos > BracketPos)
				{
					Index = FCString::Atoi(*Token.Mid(BracketPos + 1, ClosePos - BracketPos - 1));
					Token = Token.Left(BracketPos);
				}
			}

			FProperty* Prop = ContainerStruct->FindPropertyByName(FName(*Token));
			if (!Prop) { OutError = FString::Printf(TEXT("property '%s' not found at '%s'"), *Token, *DottedName); return false; }
			void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(Container);

			if (Index != INDEX_NONE)
			{
				if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
				{
					FScriptArrayHelper H(ArrProp, ValueAddr);
					if (Index < 0 || Index >= H.Num()) { OutError = FString::Printf(TEXT("index %d out of range on '%s' (num=%d)"), Index, *Token, H.Num()); return false; }
					Prop = ArrProp->Inner;
					ValueAddr = H.GetRawPtr(Index);
				}
				// #927: a UPROPERTY declared as a C-style fixed array, e.g.
				// `int32 Foo[3]`, is ONE FProperty with ArrayDim == 3 rather than
				// an FArrayProperty. Without this branch every indexed write to
				// one landed on element 0, or was refused as "not an array",
				// lint-prose-allow: tier  RecastNavMesh's own name for its three generation tiers
	// which is how RecastNavMesh's three navmesh generation tiers
				// lint-prose-allow: tier  RecastNavMesh's own name for its three generation tiers
	// became unreachable: only the Low tier could be read or written
				// while the engine generated from Default and High.
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

			if (i == Parts.Num() - 1)
			{
				OutProp = Prop;
				OutValueAddr = ValueAddr;
				OutOwner = Owner;
				return true;
			}

			// Descend for the next token.
			if (FStructProperty* SP = CastField<FStructProperty>(Prop))
			{
				if (SP->Struct == FInstancedStruct::StaticStruct())
				{
					FInstancedStruct* InstancedStruct = static_cast<FInstancedStruct*>(ValueAddr);
					const UScriptStruct* PayloadStruct = InstancedStruct->GetScriptStruct();
					void* PayloadMemory = InstancedStruct->GetMutableMemory();
					if (!PayloadStruct || !PayloadMemory)
					{
						OutError = FString::Printf(TEXT("'%s' FInstancedStruct payload is empty - cannot descend"), *PathToken);
						return false;
					}

					Container = PayloadMemory;
					ContainerStruct = PayloadStruct;
				}
				else
				{
					Container = ValueAddr;
					ContainerStruct = SP->Struct;
				}
			}
			else if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
			{
				UObject* Sub = OP->GetObjectPropertyValue(ValueAddr);
				if (!Sub) { OutError = FString::Printf(TEXT("'%s' object reference is null - cannot descend"), *Token); return false; }
				Container = Sub;
				ContainerStruct = Sub->GetClass();
				Owner = Sub;
			}
			else { OutError = FString::Printf(TEXT("'%s' is not a struct or object reference - cannot descend"), *Token); return false; }
		}

		OutError = TEXT("path resolution fell through");
		return false;
	}

	// Walk dotted property names into nested structs/arrays/subobjects before
	// assigning. Enables "SplineMeshDescriptor.StaticMesh" and
	// "Config.Traits[1].Params.Field" style keys.
	inline bool SetDottedPropertyFromJson(UObject* Owner, const FString& DottedName, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		FProperty* Prop = nullptr;
		void* ValueAddr = nullptr;
		UObject* LeafOwner = nullptr;
		if (!ResolveDottedPath(Owner, DottedName, Prop, ValueAddr, LeafOwner, OutError)) return false;
		return SetJsonOnProperty(Prop, ValueAddr, Value, OutError);
	}
}
