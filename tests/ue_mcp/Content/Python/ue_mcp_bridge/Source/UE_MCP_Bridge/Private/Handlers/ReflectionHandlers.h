#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class FReflectionHandlers
{
public:
	// Register all reflection handlers
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Handler implementations
	static TSharedPtr<FJsonValue> ReflectClass(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReflectStruct(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ReflectEnum(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListClasses(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListGameplayTags(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateGameplayTag(const TSharedPtr<FJsonObject>& Params);

	// Helper functions
	static UClass* FindClass(const FString& ClassName);
	static UScriptStruct* FindStruct(const FString& StructName);
	static UEnum* FindEnum(const FString& EnumName);
	static TSharedPtr<FJsonValue> SerializeProperty(FProperty* Prop, void* Data);
};
