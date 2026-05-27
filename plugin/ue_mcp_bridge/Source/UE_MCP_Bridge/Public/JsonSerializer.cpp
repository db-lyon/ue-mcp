#include "JsonSerializer.h"
#include "HandlerJsonProperty.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyPortFlags.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

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
	else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Property))
	{
		UObject* ObjValue = ObjProp->GetObjectPropertyValue(Value);
		if (ObjValue)
		{
			return MakeShared<FJsonValueString>(ObjValue->GetPathName());
		}
		return MakeShared<FJsonValueNull>();
	}
	else if (FSoftObjectProperty* SoftObjProp = CastField<FSoftObjectProperty>(Property))
	{
		const FSoftObjectPtr& SoftPtr = SoftObjProp->GetPropertyValue(Value);
		return MakeShared<FJsonValueString>(SoftPtr.ToString());
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

	// Fallback: use ExportText for any remaining property types
	FString StringValue;
	Property->ExportText_Direct(StringValue, Value, Value, nullptr, PPF_None);
	return MakeShared<FJsonValueString>(StringValue);
}

bool FMCPJsonSerializer::DeserializeValue(FProperty* Property, void* ValueAddr, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
{
	return MCPJsonProperty::SetJsonOnProperty(Property, ValueAddr, JsonValue, OutError);
}
