#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "UObject/UnrealType.h"

class FMCPJsonSerializer
{
public:
	// Serialize UE type to JSON value
	static TSharedPtr<FJsonValue> SerializeValue(const void* Value, FProperty* Property);

	// Serialize FVector to JSON
	static TSharedPtr<FJsonValue> SerializeVector(const FVector& Vector);

	// Serialize FRotator to JSON
	static TSharedPtr<FJsonValue> SerializeRotator(const FRotator& Rotator);

	// Serialize FTransform to JSON
	static TSharedPtr<FJsonValue> SerializeTransform(const FTransform& Transform);

	// Serialize FLinearColor to JSON
	static TSharedPtr<FJsonValue> SerializeLinearColor(const FLinearColor& Color);

	// Serialize FString to JSON
	static TSharedPtr<FJsonValue> SerializeString(const FString& String);

	// Serialize TArray to JSON
	template<typename T>
	static TSharedPtr<FJsonValue> SerializeArray(const TArray<T>& Array);

	// Serialize UObject property to JSON
	static TSharedPtr<FJsonValue> SerializeObjectProperty(UObject* Object, FProperty* Property);

	// Serialize UObject to JSON (basic properties)
	static TSharedPtr<FJsonObject> SerializeObject(UObject* Object);

	// Deserialize a JSON value into a UE property (recursive; handles TArray,
	// nested structs, UObject refs, soft refs, FGameplayTag, etc.).
	// Delegates to MCPJsonProperty::SetJsonOnProperty — see HandlerJsonProperty.h.
	static bool DeserializeValue(FProperty* Property, void* ValueAddr, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError);

private:
	// Helper to serialize property value
	static TSharedPtr<FJsonValue> SerializePropertyValue(const void* Value, FProperty* Property);
};
