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
	static TSharedPtr<FJsonValue> CreateEnum(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetEnumEntries(const TSharedPtr<FJsonObject>& Params);
	// #689: probe whether a UClass / module is currently loaded in the editor.
	static TSharedPtr<FJsonValue> IsClassLoaded(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> IsModuleLoaded(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListLoadedModules(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> InspectSaveGame(const TSharedPtr<FJsonObject>& Params);
	// T1: the per-instance writable schema. reflect_class answers what a CLASS
	// has; this answers what may be written on THIS asset / CDO / subobject
	// right now, with each property's constraints and current value.
	// Implemented in ReflectionHandlers_Schema.cpp.
	static TSharedPtr<FJsonValue> ReflectInstance(const TSharedPtr<FJsonObject>& Params);

	// Helper functions
	static UClass* FindClass(const FString& ClassName);
	static UScriptStruct* FindStruct(const FString& StructName);
	static UEnum* FindEnum(const FString& EnumName);
	static TSharedPtr<FJsonValue> SerializeProperty(FProperty* Prop, void* Data);
};
