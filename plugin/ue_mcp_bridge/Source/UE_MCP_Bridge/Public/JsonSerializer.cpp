#include "JsonSerializer.h"
#include "HandlerJsonProperty.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "GameplayTagContainer.h"

namespace
{
	// #820: keys that a JSON object can name directly. Anything else (a struct
	// key above all) is emitted as a [{key, value}] array instead, because a
	// JSON field name cannot carry it and its export text does not read back.
	bool KeyFitsAJsonFieldName(const FProperty* KeyProp)
	{
		if (!KeyProp) return false;
		if (KeyProp->IsA<FStrProperty>() || KeyProp->IsA<FNameProperty>()) return true;
		if (KeyProp->IsA<FBoolProperty>()) return true;
		if (KeyProp->IsA<FEnumProperty>()) return true;
		if (KeyProp->IsA<FNumericProperty>()) return true;
		if (const FStructProperty* StructProp = CastField<FStructProperty>(KeyProp))
		{
			// A tag (or a struct deriving from one) reads and writes as its name.
			return StructProp->Struct->IsChildOf(FGameplayTag::StaticStruct());
		}
		return false;
	}

	// The JSON object field name for a key value, in the exact form the setter
	// reads back: a tag as its name, everything else as its plain text.
	FString KeyToFieldName(const FProperty* KeyProp, const void* KeyAddr)
	{
		if (const FStructProperty* StructProp = CastField<FStructProperty>(KeyProp))
		{
			if (StructProp->Struct->IsChildOf(FGameplayTag::StaticStruct()))
			{
				// The tag lives in the FGameplayTag base at offset 0, so a
				// struct deriving from it reads the same way. An unset tag is
				// an empty string, never "None": the setter reads "" back as
				// the empty tag and would reject "None" as unknown.
				const FGameplayTag& Tag = *static_cast<const FGameplayTag*>(KeyAddr);
				return Tag.IsValid() ? Tag.GetTagName().ToString() : FString();
			}
		}

		FString Text;
		KeyProp->ExportTextItem_Direct(Text, KeyAddr, nullptr, nullptr, PPF_None);
		Text.TrimStartAndEndInline();
		if (Text.Len() >= 2 && Text.StartsWith(TEXT("\"")) && Text.EndsWith(TEXT("\"")))
		{
			Text = Text.Mid(1, Text.Len() - 2);
		}
		return Text;
	}
}

TSharedPtr<FJsonValue> FMCPJsonSerializer::SerializeValue(const void* Value, FProperty* Property)
{
	if (!Property || !Value)
	{
		return MakeShared<FJsonValueNull>();
	}

	return SerializePropertyValue(Value, Property);
}

TSharedPtr<FJsonValue> FMCPJsonSerializer::SerializeVector(const FVector& Vector)
{
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetNumberField(TEXT("x"), Vector.X);
	JsonObject->SetNumberField(TEXT("y"), Vector.Y);
	JsonObject->SetNumberField(TEXT("z"), Vector.Z);
	return MakeShared<FJsonValueObject>(JsonObject);
}

TSharedPtr<FJsonValue> FMCPJsonSerializer::SerializeRotator(const FRotator& Rotator)
{
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetNumberField(TEXT("pitch"), Rotator.Pitch);
	JsonObject->SetNumberField(TEXT("yaw"), Rotator.Yaw);
	JsonObject->SetNumberField(TEXT("roll"), Rotator.Roll);
	return MakeShared<FJsonValueObject>(JsonObject);
}

TSharedPtr<FJsonValue> FMCPJsonSerializer::SerializeTransform(const FTransform& Transform)
{
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	TSharedPtr<FJsonValue> TranslationValue = SerializeVector(Transform.GetTranslation());
	TSharedPtr<FJsonValue> RotationValue = SerializeRotator(Transform.GetRotation().Rotator());
	TSharedPtr<FJsonValue> ScaleValue = SerializeVector(Transform.GetScale3D());
	JsonObject->SetObjectField(TEXT("translation"), TranslationValue->AsObject());
	JsonObject->SetObjectField(TEXT("rotation"), RotationValue->AsObject());
	JsonObject->SetObjectField(TEXT("scale"), ScaleValue->AsObject());
	return MakeShared<FJsonValueObject>(JsonObject);
}

TSharedPtr<FJsonValue> FMCPJsonSerializer::SerializeLinearColor(const FLinearColor& Color)
{
	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	JsonObject->SetNumberField(TEXT("r"), Color.R);
	JsonObject->SetNumberField(TEXT("g"), Color.G);
	JsonObject->SetNumberField(TEXT("b"), Color.B);
	JsonObject->SetNumberField(TEXT("a"), Color.A);
	return MakeShared<FJsonValueObject>(JsonObject);
}

TSharedPtr<FJsonValue> FMCPJsonSerializer::SerializeString(const FString& String)
{
	return MakeShared<FJsonValueString>(String);
}

TSharedPtr<FJsonValue> FMCPJsonSerializer::SerializeObjectProperty(UObject* Object, FProperty* Property)
{
	if (!Object || !Property)
	{
		return MakeShared<FJsonValueNull>();
	}

	const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Object);
	return SerializePropertyValue(ValuePtr, Property);
}

TSharedPtr<FJsonObject> FMCPJsonSerializer::SerializeObject(UObject* Object)
{
	if (!Object)
	{
		return MakeShared<FJsonObject>();
	}

	TSharedPtr<FJsonObject> JsonObject = MakeShared<FJsonObject>();
	
	// Serialize basic object info
	JsonObject->SetStringField(TEXT("name"), Object->GetName());
	JsonObject->SetStringField(TEXT("class"), Object->GetClass()->GetName());
	JsonObject->SetStringField(TEXT("path"), Object->GetPathName());

	// Serialize properties
	for (TFieldIterator<FProperty> PropIt(Object->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		if (Property && Property->HasAnyPropertyFlags(CPF_BlueprintVisible))
		{
			TSharedPtr<FJsonValue> PropValue = SerializeObjectProperty(Object, Property);
			if (PropValue.IsValid())
			{
				JsonObject->SetField(Property->GetName(), PropValue);
			}
		}
	}

	return JsonObject;
}

TSharedPtr<FJsonValue> FMCPJsonSerializer::SerializePropertyValue(const void* Value, FProperty* Property)
{
	if (!Property || !Value)
	{
		return MakeShared<FJsonValueNull>();
	}

	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		return MakeShared<FJsonValueString>(StrProp->GetPropertyValue(Value));
	}
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		return MakeShared<FJsonValueString>(NameProp->GetPropertyValue(Value).ToString());
	}
	else if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		return MakeShared<FJsonValueString>(TextProp->GetPropertyValue(Value).ToString());
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		return MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(Value));
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
	{
		return MakeShared<FJsonValueNumber>(IntProp->GetPropertyValue(Value));
	}
	else if (FInt64Property* Int64Prop = CastField<FInt64Property>(Property))
	{
		return MakeShared<FJsonValueNumber>(Int64Prop->GetPropertyValue(Value));
	}
	else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
	{
		return MakeShared<FJsonValueNumber>(FloatProp->GetPropertyValue(Value));
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
	{
		return MakeShared<FJsonValueNumber>(DoubleProp->GetPropertyValue(Value));
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		if (StructProp->Struct == TBaseStructure<FVector>::Get())
		{
			return SerializeVector(*static_cast<const FVector*>(Value));
		}
		else if (StructProp->Struct == TBaseStructure<FRotator>::Get())
		{
			return SerializeRotator(*static_cast<const FRotator*>(Value));
		}
		else if (StructProp->Struct == TBaseStructure<FTransform>::Get())
		{
			return SerializeTransform(*static_cast<const FTransform*>(Value));
		}
		else if (StructProp->Struct == TBaseStructure<FLinearColor>::Get())
		{
			return SerializeLinearColor(*static_cast<const FLinearColor*>(Value));
		}
		else if (StructProp->Struct->IsChildOf(FGameplayTag::StaticStruct()))
		{
			// #820: a tag reads back as its name, which is the form the setter
			// takes. Emitting {TagName: "..."} instead made a read-then-write
			// round trip go through raw field assignment with no tag lookup.
			// An unset tag is an empty string, which the setter reads as "clear".
			const FGameplayTag& Tag = *static_cast<const FGameplayTag*>(Value);
			return MakeShared<FJsonValueString>(Tag.IsValid() ? Tag.GetTagName().ToString() : FString());
		}
		else if (StructProp->Struct->IsChildOf(FGameplayTagContainer::StaticStruct()))
		{
			TArray<TSharedPtr<FJsonValue>> Tags;
			for (const FGameplayTag& Tag : *static_cast<const FGameplayTagContainer*>(Value))
			{
				Tags.Add(MakeShared<FJsonValueString>(Tag.GetTagName().ToString()));
			}
			return MakeShared<FJsonValueArray>(Tags);
		}
		else
		{
			// Generic struct: recursively serialize each field (#196, #199)
			TSharedPtr<FJsonObject> StructJson = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
			{
				FProperty* FieldProp = *It;
				const void* FieldValue = FieldProp->ContainerPtrToValuePtr<void>(Value);
				TSharedPtr<FJsonValue> FieldJson = SerializePropertyValue(FieldValue, FieldProp);
				if (FieldJson.IsValid())
				{
					StructJson->SetField(FieldProp->GetName(), FieldJson);
				}
			}
			return MakeShared<FJsonValueObject>(StructJson);
		}
	}
	// #820: soft references first. FSoftObjectProperty derives from
	// FObjectPropertyBase, so the hard-reference branch used to claim them and
	// report null for any target that happened to be unloaded, which turned a
	// read-then-write round trip into a cleared reference.
	else if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
	{
		const FSoftObjectPtr& SoftPtr = SoftObjProp->GetPropertyValue(Value);
		return MakeShared<FJsonValueString>(SoftPtr.ToString());
	}
	else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
	{
		UObject* ObjValue = ObjProp->GetObjectPropertyValue(Value);
		if (ObjValue)
		{
			return MakeShared<FJsonValueString>(ObjValue->GetPathName());
		}
		return MakeShared<FJsonValueNull>();
	}
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		FString EnumStr;
		EnumProp->ExportText_Direct(EnumStr, Value, Value, nullptr, PPF_None);
		return MakeShared<FJsonValueString>(EnumStr);
	}
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		if (ByteProp->Enum)
		{
			FString EnumStr;
			ByteProp->ExportText_Direct(EnumStr, Value, Value, nullptr, PPF_None);
			return MakeShared<FJsonValueString>(EnumStr);
		}
		return MakeShared<FJsonValueNumber>(ByteProp->GetPropertyValue(Value));
	}
	// The integer widths the branches above do not name one by one: int8,
	// int16, uint16 and uint32. Without this they reached the export-text
	// fallback and a plain number came back to the caller quoted, which is the
	// read half of the defect the setter had in the other direction. Each of
	// these widths is carried exactly by a double.
	//
	// FUInt64Property is left on the text path on purpose: a uint64 past 2^53
	// has no exact JSON number, and its exported text reads back through the
	// setter without losing a digit. An enum-valued byte cannot reach here at
	// all, since the byte branch above returns its name first; the test is kept
	// so the branch is safe to read on its own.
	else if (FNumericProperty* NumericProp = CastField<FNumericProperty>(Property))
	{
		if (NumericProp->IsInteger() && !NumericProp->IsEnum() && !Property->IsA<FUInt64Property>())
		{
			return MakeShared<FJsonValueNumber>(
				static_cast<double>(NumericProp->GetSignedIntPropertyValue(Value)));
		}
	}
	else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
	{
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		FScriptArrayHelper ArrayHelper(ArrayProp, Value);
		for (int32 i = 0; i < ArrayHelper.Num(); ++i)
		{
			const void* ItemValue = ArrayHelper.GetRawPtr(i);
			TSharedPtr<FJsonValue> ItemJson = SerializePropertyValue(ItemValue, ArrayProp->Inner);
			if (ItemJson.IsValid())
			{
				JsonArray.Add(ItemJson);
			}
		}
		return MakeShared<FJsonValueArray>(JsonArray);
	}
	else if (FSetProperty* SetProp = CastField<FSetProperty>(Property))
	{
		// #820: a set used to fall through to export text, which the setter
		// then had to re-parse. Emit the array the setter already accepts.
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		FScriptSetHelper SetHelper(SetProp, Value);
		for (FScriptSetHelper::FIterator It = SetHelper.CreateIterator(); It; ++It)
		{
			TSharedPtr<FJsonValue> ItemJson = SerializePropertyValue(SetHelper.GetElementPtr(It), SetProp->ElementProp);
			if (ItemJson.IsValid())
			{
				JsonArray.Add(ItemJson);
			}
		}
		return MakeShared<FJsonValueArray>(JsonArray);
	}
	else if (FMapProperty* MapProp = CastField<FMapProperty>(Property))
	{
		// #820: emit a map in the shape its own setter takes back. Keys that a
		// JSON field name can carry become an object; everything else, struct
		// keys above all, becomes a [{key, value}] array. Export text is not an
		// option here: its pair form does not import back for struct keys, so
		// copying it into a setter used to empty the destination map.
		FScriptMapHelper MapHelper(MapProp, Value);
		const bool bObjectShape = KeyFitsAJsonFieldName(MapProp->KeyProp);

		if (bObjectShape)
		{
			TSharedPtr<FJsonObject> MapJson = MakeShared<FJsonObject>();
			for (FScriptMapHelper::FIterator It = MapHelper.CreateIterator(); It; ++It)
			{
				MapJson->SetField(
					KeyToFieldName(MapProp->KeyProp, MapHelper.GetKeyPtr(It)),
					SerializePropertyValue(MapHelper.GetValuePtr(It), MapProp->ValueProp));
			}
			return MakeShared<FJsonValueObject>(MapJson);
		}

		TArray<TSharedPtr<FJsonValue>> Pairs;
		for (FScriptMapHelper::FIterator It = MapHelper.CreateIterator(); It; ++It)
		{
			TSharedPtr<FJsonObject> PairJson = MakeShared<FJsonObject>();
			PairJson->SetField(TEXT("key"), SerializePropertyValue(MapHelper.GetKeyPtr(It), MapProp->KeyProp));
			PairJson->SetField(TEXT("value"), SerializePropertyValue(MapHelper.GetValuePtr(It), MapProp->ValueProp));
			Pairs.Add(MakeShared<FJsonValueObject>(PairJson));
		}
		return MakeShared<FJsonValueArray>(Pairs);
	}

	// Fallback: use ExportText for any remaining property types
	FString StringValue;
	Property->ExportText_Direct(StringValue, Value, Value, nullptr, PPF_None);
	return MakeShared<FJsonValueString>(StringValue);
}

bool FMCPJsonSerializer::DeserializeValue(FProperty* Property, void* ValueAddr, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
{
	return MCPJsonProperty::SetJsonOnProperty(Property, ValueAddr, JsonValue, OutError);
}
