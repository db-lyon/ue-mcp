#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/SoftObjectPtr.h"
#include "GameplayTagContainer.h"
#include "GameplayTagsManager.h"
#include "Engine/Blueprint.h"

// Shared recursive JSON→FProperty setter. Originally written for PCG
// set_pcg_node_settings (#149); also used by set_component_property on
// Blueprint component templates (#152) and set_water_body_property (#151-ish).
//
// Handles TArray, TSet, nested struct objects, UObject/class references by
// path, and soft references. Falls back to ImportText for scalars.
namespace MCPJsonProperty
{
	inline bool SetJsonOnProperty(FProperty* Prop, void* ValueAddr, const TSharedPtr<FJsonValue>& InValue, FString& OutError)
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
			if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
			{
				OP->SetObjectPropertyValue(ValueAddr, nullptr);
				return true;
			}
			if (FSoftObjectProperty* SOP = CastField<FSoftObjectProperty>(Prop))
			{
				SOP->SetPropertyValue(ValueAddr, FSoftObjectPtr());
				return true;
			}
			if (FWeakObjectProperty* WOP = CastField<FWeakObjectProperty>(Prop))
			{
				WOP->SetObjectPropertyValue(ValueAddr, nullptr);
				return true;
			}
			if (FClassProperty* CP = CastField<FClassProperty>(Prop))
			{
				CP->SetObjectPropertyValue(ValueAddr, nullptr);
				return true;
			}
			if (FInterfaceProperty* IP = CastField<FInterfaceProperty>(Prop))
			{
				FScriptInterface Empty;
				IP->SetPropertyValue(ValueAddr, Empty);
				return true;
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
				return true;
			}
			// else: fall through to ImportText fallback below.
		}

		// Struct: recurse on JSON object fields; otherwise fall through to ImportText
		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			// #503: FGameplayTag (and FGameplayTagContainer) can't be built
			// from Python in 5.7 and ImportText("(TagName=\"X\")") is the only
			// portable path. Coerce a plain string ("X.Y") or array of strings
			// into the right runtime tag value via the GameplayTagsManager so
			// callers don't have to format ImportText themselves.
			static const FName GameplayTagName(TEXT("GameplayTag"));
			static const FName GameplayTagContainerName(TEXT("GameplayTagContainer"));
			const FName StructName = StructProp->Struct->GetFName();
			if (StructName == GameplayTagName)
			{
				FString TagStr;
				if (Value->TryGetString(TagStr))
				{
					FGameplayTag* TagPtr = static_cast<FGameplayTag*>(ValueAddr);
					if (TagStr.IsEmpty())
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
			else if (StructName == GameplayTagContainerName)
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

		// Hard UObject ref — accept asset path
		if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
		{
			FString Path;
			if (Value->TryGetString(Path) && !Path.IsEmpty())
			{
				UObject* Loaded = StaticLoadObject(ObjProp->PropertyClass, nullptr, *Path);
				if (!Loaded) { OutError = FString::Printf(TEXT("asset not found: %s"), *Path); return false; }
				ObjProp->SetObjectPropertyValue(ValueAddr, Loaded);
				return true;
			}
		}

		// Hard UClass ref — accept class path
		if (FClassProperty* ClassProp = CastField<FClassProperty>(Prop))
		{
			FString Path;
			if (Value->TryGetString(Path) && !Path.IsEmpty())
			{
				UClass* Loaded = LoadClass<UObject>(nullptr, *Path);
				if (!Loaded)
				{
					// #489: callers commonly pass a Blueprint asset path
					// (/Game/Foo/BP_GameMode.BP_GameMode) for a TSubclassOf
					// field. Retry with the generated-class suffix.
					if (!Path.EndsWith(TEXT("_C")))
					{
						const FString WithSuffix = Path + TEXT("_C");
						Loaded = LoadClass<UObject>(nullptr, *WithSuffix);
					}
				}
				if (!Loaded)
				{
					// Last-ditch: load the asset as UBlueprint and grab its
					// GeneratedClass. Covers paths that omit the .Name suffix.
					if (UBlueprint* BP = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *Path)))
					{
						Loaded = BP->GeneratedClass;
					}
				}
				if (!Loaded) { OutError = FString::Printf(TEXT("class not found: %s"), *Path); return false; }
				ClassProp->SetObjectPropertyValue(ValueAddr, Loaded);
				return true;
			}
		}

		// Soft class ref — accept class path string, same Blueprint suffix tolerance.
		if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Prop))
		{
			FString Path;
			if (Value->TryGetString(Path))
			{
				if (Path.IsEmpty())
				{
					SoftClassProp->SetPropertyValue(ValueAddr, FSoftObjectPtr());
					return true;
				}
				if (!Path.EndsWith(TEXT("_C")))
				{
					Path += TEXT("_C");
				}
				FSoftObjectPath PathObj(Path);
				SoftClassProp->SetPropertyValue(ValueAddr, FSoftObjectPtr(PathObj));
				return true;
			}
		}

		// Soft object ref — accept path string
		if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Prop))
		{
			FString Path;
			if (Value->TryGetString(Path))
			{
				FSoftObjectPath PathObj(Path);
				FSoftObjectPtr Ptr(PathObj);
				SoftObjProp->SetPropertyValue(ValueAddr, Ptr);
				return true;
			}
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
			// 3. Friendly fallback — match each enumerator's display name and
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

		// Fallback: coerce JSON to string, run ImportText_Direct
		FString Str;
		if (Value->TryGetString(Str)) {}
		else if (Value->Type == EJson::Number) Str = FString::SanitizeFloat(Value->AsNumber());
		else if (Value->Type == EJson::Boolean) Str = Value->AsBool() ? TEXT("true") : TEXT("false");
		else Str = Value->AsString();

		const TCHAR* R = Prop->ImportText_Direct(*Str, ValueAddr, nullptr, PPF_None);
		if (R == nullptr) { OutError = FString::Printf(TEXT("ImportText failed for '%s'"), *Str); return false; }
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
		UStruct* ContainerStruct = Root->GetClass();
		UObject* Owner = Root;

		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			FString Token = Parts[i];
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
				FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop);
				if (!ArrProp) { OutError = FString::Printf(TEXT("'%s' is not an array but was indexed [%d]"), *Token, Index); return false; }
				FScriptArrayHelper H(ArrProp, ValueAddr);
				if (Index < 0 || Index >= H.Num()) { OutError = FString::Printf(TEXT("index %d out of range on '%s' (num=%d)"), Index, *Token, H.Num()); return false; }
				Prop = ArrProp->Inner;
				ValueAddr = H.GetRawPtr(Index);
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
				Container = ValueAddr;
				ContainerStruct = SP->Struct;
			}
			else if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
			{
				UObject* Sub = OP->GetObjectPropertyValue(ValueAddr);
				if (!Sub) { OutError = FString::Printf(TEXT("'%s' object reference is null — cannot descend"), *Token); return false; }
				Container = Sub;
				ContainerStruct = Sub->GetClass();
				Owner = Sub;
			}
			else { OutError = FString::Printf(TEXT("'%s' is not a struct or object reference — cannot descend"), *Token); return false; }
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
