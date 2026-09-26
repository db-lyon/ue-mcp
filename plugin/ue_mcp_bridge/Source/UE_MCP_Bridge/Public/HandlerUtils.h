#pragma once

// The umbrella every handler, and every plugin built against the bridge, includes.
// The helpers live in the focused headers below; include one of them directly
// when a file needs only that part.

#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "HAL/FileManager.h"
#include "HAL/CriticalSection.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Misc/OutputDevice.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/FeedbackContext.h"
#include "Misc/PackageName.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Engine/CollisionProfile.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EditorAssetLibrary.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/Guid.h"
#include "UObject/UObjectHash.h"
#include "Templates/Casts.h"

#include <type_traits>

#include "HandlerEngineVersion.h"
#include "HandlerResult.h"
#include "HandlerParams.h"
#include "HandlerJsonConvert.h"
#include "HandlerWorld.h"
#include "HandlerAssetResolve.h"
#include "HandlerActorResolve.h"
#include "HandlerClassResolve.h"
#include "HandlerPackageSave.h"
#include "HandlerObjectUtils.h"

// ── Function and script arguments (#811, #1057) ──────────────────────────────
//
// `args` on the invoke_* actions and run_python_file is declared with value
// forms: a name -> value map, an entry list, a list of strings, or a string.
// These normalizers are the one place each form is accepted or refused, so
// every handler taking `args` answers the same shape the same way.

namespace MCPArgsDetail
{
	/** Decode JSON text holding exactly one value of any kind. Wrapped in an
	 *  array so a bare scalar decodes the same on every supported engine. */
	inline bool ParseOneJsonValue(const FString& Text, TSharedPtr<FJsonValue>& Out)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(FString(TEXT("[")) + Text + TEXT("]"));
		if (!FJsonSerializer::Deserialize(Reader, Values) || Values.Num() != 1 || !Values[0].IsValid())
		{
			return false;
		}
		Out = Values[0];
		return true;
	}

	/** A value as compact JSON text, a whole number without a fraction. */
	inline FString CompactJson(const TSharedPtr<FJsonValue>& Value)
	{
		if (!Value.IsValid() || Value->Type == EJson::None || Value->Type == EJson::Null)
		{
			return TEXT("null");
		}
		if (Value->Type == EJson::Number)
		{
			const double Number = Value->AsNumber();
			if (FMath::IsFinite(Number) && Number == FMath::RoundToDouble(Number) && FMath::Abs(Number) < 1e15)
			{
				return FString::Printf(TEXT("%lld"), static_cast<long long>(Number));
			}
			return FString::SanitizeFloat(Number);
		}
		FString Text;
		TArray<TSharedPtr<FJsonValue>> Wrapper;
		Wrapper.Add(Value);
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Text);
		FJsonSerializer::Serialize(Wrapper, Writer);
		return Text.Len() >= 2 ? Text.Mid(1, Text.Len() - 2) : Text;
	}

	inline TSharedPtr<FJsonValue> FunctionArgsError(const FString& Label, const TCHAR* Detail)
	{
		return MCPError(FString::Printf(
			TEXT("%s %s. Pass an object mapping parameter name to value, e.g. {\"bEnabled\": true}."), *Label, Detail));
	}
}

/**
 * Reduce a function-call `args` value to the name -> value map a UFUNCTION's
 * parameters are marshalled from. Accepts the map itself, an entry list
 * ([{name, value}]), and a JSON string of either. Returns an error value when
 * the value is none of those, else nullptr; OutArgs is left invalid when there
 * is nothing to pass (absent, null, or a blank string).
 */
inline TSharedPtr<FJsonValue> MCPNormalizeFunctionArgs(const TSharedPtr<FJsonValue>& Raw, const FString& Label, TSharedPtr<FJsonObject>& OutArgs)
{
	OutArgs.Reset();
	if (!Raw.IsValid() || Raw->Type == EJson::None || Raw->Type == EJson::Null)
	{
		return nullptr;
	}
	switch (Raw->Type)
	{
	case EJson::String:
	{
		const FString Text = Raw->AsString().TrimStartAndEnd();
		if (Text.IsEmpty()) return nullptr;
		TSharedPtr<FJsonValue> Parsed;
		if (!MCPArgsDetail::ParseOneJsonValue(Text, Parsed))
		{
			return MCPArgsDetail::FunctionArgsError(Label, TEXT("was a string, but it is not valid JSON"));
		}
		if (Parsed->Type == EJson::String)
		{
			return MCPArgsDetail::FunctionArgsError(Label, TEXT("decoded to a string, not a parameter map"));
		}
		return MCPNormalizeFunctionArgs(Parsed, Label, OutArgs);
	}
	case EJson::Array:
	{
		TSharedPtr<FJsonObject> Map = MakeShared<FJsonObject>();
		for (const TSharedPtr<FJsonValue>& Entry : Raw->AsArray())
		{
			if (!Entry.IsValid() || Entry->Type != EJson::Object || !Entry->AsObject().IsValid())
			{
				return MCPArgsDetail::FunctionArgsError(Label, TEXT("was an array of values, so no parameter name can be resolved"));
			}
			const TSharedPtr<FJsonObject> EntryObject = Entry->AsObject();
			// Typed, not TryGetStringField: that would accept a number as a name.
			const TSharedPtr<FJsonValue> NameValue = EntryObject->TryGetField(TEXT("name"));
			if (!NameValue.IsValid() || NameValue->Type != EJson::String || NameValue->AsString().IsEmpty())
			{
				return MCPArgsDetail::FunctionArgsError(Label, TEXT("was an array whose entries are missing a \"name\" string"));
			}
			const FString Name = NameValue->AsString();
			// An entry with no value passes nothing for that name, and overrides
			// an earlier entry for it, as it would once serialized.
			const TSharedPtr<FJsonValue> Value = EntryObject->TryGetField(TEXT("value"));
			if (Value.IsValid())
			{
				Map->SetField(Name, Value);
			}
			else
			{
				Map->RemoveField(Name);
			}
		}
		OutArgs = Map;
		return nullptr;
	}
	case EJson::Object:
		OutArgs = Raw->AsObject();
		return nullptr;
	case EJson::Number:
		return MCPArgsDetail::FunctionArgsError(Label, TEXT("was a number"));
	case EJson::Boolean:
		return MCPArgsDetail::FunctionArgsError(Label, TEXT("was a boolean"));
	default:
		return nullptr;
	}
}

/** MCPNormalizeFunctionArgs over the parameter `Key`, noted as read. */
inline TSharedPtr<FJsonValue> MCPReadFunctionArgs(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, TSharedPtr<FJsonObject>& OutArgs)
{
	return MCPNormalizeFunctionArgs(TryGetParam(Params, Key), Key, OutArgs);
}

/**
 * Reduce a script `args` value to positional strings. Accepts a list (a
 * non-string entry becomes its compact JSON), a JSON array string, and a lone
 * string as one argument. A map is a function-call shape and is refused, as is
 * any other value. Returns an error value or nullptr; OutArgs is empty when
 * there is nothing to pass.
 */
inline TSharedPtr<FJsonValue> MCPNormalizePythonArgs(const TSharedPtr<FJsonValue>& Raw, const FString& Label, TArray<FString>& OutArgs)
{
	OutArgs.Reset();
	if (!Raw.IsValid() || Raw->Type == EJson::None || Raw->Type == EJson::Null)
	{
		return nullptr;
	}
	if (Raw->Type == EJson::String)
	{
		const FString Text = Raw->AsString().TrimStartAndEnd();
		if (Text.IsEmpty()) return nullptr;
		if (Text.StartsWith(TEXT("[")))
		{
			TSharedPtr<FJsonValue> Parsed;
			if (!MCPArgsDetail::ParseOneJsonValue(Text, Parsed) || Parsed->Type != EJson::Array)
			{
				return MCPError(FString::Printf(
					TEXT("%s looked like a JSON array but does not parse. Pass an array of positional strings."), *Label));
			}
			return MCPNormalizePythonArgs(Parsed, Label, OutArgs);
		}
		OutArgs.Add(Text);
		return nullptr;
	}
	if (Raw->Type == EJson::Array)
	{
		for (const TSharedPtr<FJsonValue>& Entry : Raw->AsArray())
		{
			OutArgs.Add(Entry.IsValid() && Entry->Type == EJson::String ? Entry->AsString() : MCPArgsDetail::CompactJson(Entry));
		}
		return nullptr;
	}
	return MCPError(FString::Printf(
		TEXT("%s must be an array of positional strings for run_python_file, not an object."), *Label));
}

/** MCPNormalizePythonArgs over the parameter `Key`, noted as read. */
inline TSharedPtr<FJsonValue> MCPReadPythonArgs(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, TArray<FString>& OutArgs)
{
	return MCPNormalizePythonArgs(TryGetParam(Params, Key), Key, OutArgs);
}
