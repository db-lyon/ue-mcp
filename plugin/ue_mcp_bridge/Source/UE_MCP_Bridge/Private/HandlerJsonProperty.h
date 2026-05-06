#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/SoftObjectPtr.h"

// Shared recursive JSON→FProperty setter. Originally written for PCG
// set_pcg_node_settings (#149); also used by set_component_property on
// Blueprint component templates (#152) and set_water_body_property (#151-ish).
//
// Handles TArray, TSet, nested struct objects, UObject/class references by
// path, and soft references. Falls back to ImportText for scalars.
namespace MCPJsonProperty
{
	inline bool SetJsonOnProperty(FProperty* Prop, void* ValueAddr, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		if (!Prop || !Value.IsValid() || !ValueAddr) { OutError = TEXT("null property/value/addr"); return false; }

		// TArray
		if (FArrayProperty* ArrProp = CastField<FArrayProperty>(Prop))
		{
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (!Value->TryGetArray(Items) || !Items) { OutError = TEXT("expected JSON array"); return false; }
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

		// TSet
		if (FSetProperty* SetProp = CastField<FSetProperty>(Prop))
		{
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (!Value->TryGetArray(Items) || !Items) { OutError = TEXT("expected JSON array for TSet"); return false; }
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

		// Struct: recurse on JSON object fields; otherwise fall through to ImportText
		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
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
				if (!Loaded) { OutError = FString::Printf(TEXT("class not found: %s"), *Path); return false; }
				ClassProp->SetObjectPropertyValue(ValueAddr, Loaded);
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

	// Walk dotted property names into nested structs before assigning.
	// Enables "SplineMeshDescriptor.StaticMesh" style keys.
	inline bool SetDottedPropertyFromJson(UObject* Owner, const FString& DottedName, const TSharedPtr<FJsonValue>& Value, FString& OutError)
	{
		TArray<FString> Parts;
		DottedName.ParseIntoArray(Parts, TEXT("."));
		if (Parts.Num() == 0) { OutError = TEXT("empty property name"); return false; }

		void* Container = Owner;
		UStruct* ContainerStruct = Owner->GetClass();
		FProperty* Prop = nullptr;
		for (int32 i = 0; i < Parts.Num(); ++i)
		{
			Prop = ContainerStruct->FindPropertyByName(FName(*Parts[i]));
			if (!Prop) { OutError = FString::Printf(TEXT("property '%s' not found at '%s'"), *Parts[i], *DottedName); return false; }
			if (i < Parts.Num() - 1)
			{
				FStructProperty* SP = CastField<FStructProperty>(Prop);
				if (!SP) { OutError = FString::Printf(TEXT("'%s' is not a struct — cannot descend"), *Parts[i]); return false; }
				Container = SP->ContainerPtrToValuePtr<void>(Container);
				ContainerStruct = SP->Struct;
			}
		}
		void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(Container);
		return SetJsonOnProperty(Prop, ValueAddr, Value, OutError);
	}
}
