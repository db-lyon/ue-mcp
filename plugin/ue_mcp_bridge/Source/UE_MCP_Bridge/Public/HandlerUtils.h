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

// ── Widget variable GUID metadata (#728, #799) ───────────────────────────────
//
// A UWidgetBlueprint keeps WidgetVariableNameToGuidMap so external references
// to a widget variable survive a rename of that widget. The
// WidgetBlueprintCompiler validates the map on every compile and raises
//
//   "Widget [X] was added but did not get a GUID"
//
// for any widget variable it is about to generate that owns no entry. That is
// an engine ensure, so the editor survives it, but the asset is left in a state
// the UMG editor does not consider valid and every later compile repeats it.
//
// The set the compiler validates is the blueprint's SOURCE widgets, which
// UBaseWidgetBlueprint gathers without walking the hierarchy. Its own header
// says the accessor "avoids calling virtual functions on instances and is
// therefore safe to use throughout compilation", so the set is every UWidget
// the WidgetTree still OWNS, not the widgets a walk down from RootWidget
// reaches. The two part company the moment a widget is detached from its parent
// without being moved out of the tree: the walk stops seeing it while the
// compiler still generates a variable for it. Bookkeeping driven by the walk
// alone therefore deletes the GUID of a widget that still needs one, which is
// how the ensure fires on a widget that was just removed. The enumeration below
// is by outer for that reason, with the walk folded in as a union rather than
// used on its own.
//
// The map is editor-only data that landed in 5.5, so its presence is detected
// at compile time here rather than tracked with a hand-maintained version
// window.
namespace MCPWidgetGuidMap
{
	template <typename T, typename = void>
	struct THasMap : std::false_type {};

	template <typename T>
	struct THasMap<T, std::void_t<decltype(T::WidgetVariableNameToGuidMap)>> : std::true_type {};

	/** What a sync changed, and what it could not fix. */
	struct FSyncReport
	{
		/** False when this engine has no WidgetVariableNameToGuidMap at all. */
		bool bSupported = false;
		/** True once CompileChecked has actually run the compile. */
		bool bCompiled = false;
		int32 Added = 0;
		int32 Pruned = 0;
		/** Widgets moved out of the tree because nothing reached them. */
		int32 Evicted = 0;
		/** Entries whose stored GUID was not a valid GUID and was replaced. */
		int32 Repaired = 0;
		/** Variables whose map entry the compiler will refuse, after the sync
		 *  has done everything it can. A non-empty list means the next compile
		 *  of this asset ensures. */
		TArray<FName> Unusable;
		/** What is actually wrong, one clause per defect, for the message. */
		TArray<FString> Defects;

		bool IsClean() const { return Unusable.Num() == 0; }

		FString DefectList() const
		{
			return Defects.Num() ? FString::Join(Defects, TEXT("; ")) : FString(TEXT("(none)"));
		}

		FString UnusableList() const
		{
			TArray<FString> Names;
			Names.Reserve(Unusable.Num());
			for (const FName& Name : Unusable)
			{
				Names.Add(Name.ToString());
			}
			return Names.Num() ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none)"));
		}
	};

	/** Every name the compiler will generate a variable for: each widget the
	 *  WidgetTree owns (reachable from the root or not), plus each animation. */
	template <typename TWidgetBP>
	TSet<FName> RequiredNames(TWidgetBP* WidgetBP)
	{
		TSet<FName> Names;
		if (!WidgetBP)
		{
			return Names;
		}

		// UWidgetTree and UWidget named without naming them. This header is
		// included by every handler and must not drag the UMG headers into all
		// of them, and a type spelled off the template parameter stays
		// dependent, so the lookup happens where UMG is included.
		using FTreeType = std::remove_reference_t<decltype(*WidgetBP->WidgetTree)>;
		using FWidgetType = std::remove_reference_t<decltype(*WidgetBP->WidgetTree->RootWidget)>;

		if (FTreeType* Tree = WidgetBP->WidgetTree)
		{
			TArray<UObject*> Owned;
			MCPGetDirectSubobjects(Tree, Owned);
			for (UObject* Object : Owned)
			{
				if (FWidgetType* Widget = Cast<FWidgetType>(Object))
				{
					Names.Add(Widget->GetFName());
				}
			}

			Tree->ForEachWidget([&Names](FWidgetType* Widget)
			{
				if (Widget)
				{
					Names.Add(Widget->GetFName());
				}
			});
		}

		// TObjectPtr::GetFName reads the name off the object handle, so it does
		// not need UWidgetAnimation defined. WidgetBlueprint.h only forward
		// declares that class, and dereferencing the pointer here would force
		// every translation unit that includes this header to pull in UMG.
		for (const auto& Animation : WidgetBP->Animations)
		{
			const FName AnimationName = Animation.GetFName();
			if (!AnimationName.IsNone())
			{
				Names.Add(AnimationName);
			}
		}
		return Names;
	}

	/**
	 * Make the map say exactly what the compiler is about to check: give every
	 * required name the GUID it lacks, and drop every entry no live name backs.
	 * Call immediately before a compile, and again after, because the compile
	 * itself can rename a widget whose name collided.
	 */
	template <typename TWidgetBP>
	FSyncReport Sync(TWidgetBP* WidgetBP)
	{
		FSyncReport Report;
		if constexpr (THasMap<TWidgetBP>::value)
		{
			if (!WidgetBP)
			{
				return Report;
			}
			Report.bSupported = true;

			const TSet<FName> Required = RequiredNames(WidgetBP);
			for (const FName& Name : Required)
			{
				if (Name.IsNone())
				{
					continue;
				}
				if (!WidgetBP->WidgetVariableNameToGuidMap.Contains(Name))
				{
					WidgetBP->WidgetVariableNameToGuidMap.Add(Name, FGuid::NewGuid());
					++Report.Added;
				}
			}

			// Pruning is measured against a wider set: an entry naming one of
			// the blueprint's own declared variables is not proof of drift, so
			// it is left alone rather than dropped and re-added.
			TSet<FName> Known = Required;
			for (const auto& Variable : WidgetBP->NewVariables)
			{
				Known.Add(Variable.VarName);
			}

			TArray<FName> Stale;
			for (const auto& Entry : WidgetBP->WidgetVariableNameToGuidMap)
			{
				if (!Known.Contains(Entry.Key))
				{
					Stale.Add(Entry.Key);
				}
			}
			for (const FName& Name : Stale)
			{
				WidgetBP->WidgetVariableNameToGuidMap.Remove(Name);
			}
			Report.Pruned = Stale.Num();

			// What the compiler actually checks, and which half of it the fill
			// above cannot cover.
			//
			// FWidgetBlueprintCompilerContext::ValidateAndFixUpVariableGuids
			// raises ensureAlways on three conditions:
			//   1. a source widget or animation with no entry;
			//   2. an entry whose stored GUID is not valid;
			//   3. two entries carrying the same GUID.
			//
			// Condition 1 cannot survive the fill loop above, which adds a
			// fresh GUID for every required name and prunes nothing from that
			// set, so re-testing it here would be a guard that can never fire.
			// The two the fill loop says nothing about are the ones worth
			// checking, because both arrive with the asset rather than with
			// the mutation:
			//
			// Condition 2 is repairable. An invalid GUID references nothing, so
			// replacing it breaks nothing; the engine replaces it too, after
			// ensuring.
			//
			// Condition 3 is not. The GUID is how external assets refer to a
			// variable across a rename, so reassigning one of a colliding pair
			// silently breaks whichever references chose that one. The engine
			// does not repair it either - its own message tells the user to
			// delete and recreate one of the two variables - so this is the
			// state in which the map cannot be made to match what the compiler
			// checks, and it is what stops the compile.
			TMap<FGuid, FName> GuidOwners;
			for (auto& Entry : WidgetBP->WidgetVariableNameToGuidMap)
			{
				if (!Entry.Value.IsValid())
				{
					Entry.Value = FGuid::NewGuid();
					++Report.Repaired;
				}
				if (const FName* Owner = GuidOwners.Find(Entry.Value))
				{
					Report.Unusable.Add(Entry.Key);
					Report.Defects.Add(FString::Printf(
						TEXT("'%s' carries the same variable GUID as '%s'"),
						*Entry.Key.ToString(), *Owner->ToString()));
					continue;
				}
				GuidOwners.Add(Entry.Value, Entry.Key);
			}
		}
		return Report;
	}

	/**
	 * Move every widget the tree no longer reaches out of the WidgetTree and
	 * into the transient package, and report the ones that would not move.
	 *
	 * Detaching a widget from its parent panel does not end its membership of
	 * the blueprint: ownership is what makes the compiler generate a variable
	 * for it, so a detached widget still outered to the tree is still compiled,
	 * still needs a GUID, and still holds its name against a later add. Only a
	 * handler whose contract is removal may call this, since it is destructive
	 * by design.
	 */
	template <typename TWidgetBP>
	TArray<FName> EvictUnreachableWidgets(TWidgetBP* WidgetBP, int32& OutEvicted)
	{
		OutEvicted = 0;
		TArray<FName> Stuck;
		if (!WidgetBP || !WidgetBP->WidgetTree)
		{
			return Stuck;
		}

		using FTreeType = std::remove_reference_t<decltype(*WidgetBP->WidgetTree)>;
		using FWidgetType = std::remove_reference_t<decltype(*WidgetBP->WidgetTree->RootWidget)>;

		FTreeType* Tree = WidgetBP->WidgetTree;

		TSet<const UObject*> Reachable;
		Tree->ForEachWidget([&Reachable](FWidgetType* Widget)
		{
			if (Widget)
			{
				Reachable.Add(Widget);
			}
		});
		// Named slot content hangs off the tree rather than off the root, so it
		// is reachable in every sense that matters even where the root walk
		// never visits it. Evicting it would delete authored content.
		for (const auto& Binding : Tree->NamedSlotBindings)
		{
			if (Binding.Value)
			{
				Reachable.Add(Binding.Value);
				FTreeType::ForWidgetAndChildren(Binding.Value, [&Reachable](FWidgetType* Widget)
				{
					if (Widget)
					{
						Reachable.Add(Widget);
					}
				});
			}
		}

		TArray<UObject*> Owned;
		MCPGetDirectSubobjects(Tree, Owned);
		for (UObject* Object : Owned)
		{
			FWidgetType* Widget = Cast<FWidgetType>(Object);
			if (!Widget || Reachable.Contains(Widget))
			{
				continue;
			}

			// A unique name rather than the one it has: the transient package is
			// shared, and the same widget name is evicted from the same asset
			// on every run of a script that adds and removes it.
			const FName EvictedName = Widget->GetFName();
			const FName ParkedName = MakeUniqueObjectName(
				GetTransientPackage(), Widget->GetClass(), EvictedName);
			Widget->Rename(*ParkedName.ToString(), GetTransientPackage(),
				REN_DontCreateRedirectors | REN_NonTransactional);
			if (Widget->GetOuter() == Tree)
			{
				Stuck.Add(EvictedName);
			}
			else
			{
				++OutEvicted;
			}
		}
		return Stuck;
	}

	/**
	 * Sync, compile, sync again.
	 *
	 * When the map cannot be made to match what the compiler checks - which in
	 * practice means two variables carrying one GUID, the one defect neither
	 * the sync nor the engine can repair without picking which set of external
	 * references to break - the compile does NOT run and bCompiled stays false.
	 * The ensure inside the compiler is the thing this exists to prevent, and a
	 * handler that compiled anyway would be reporting success on an asset it
	 * had just broken.
	 */
	template <typename TWidgetBP>
	FSyncReport CompileChecked(TWidgetBP* WidgetBP)
	{
		FSyncReport Report = Sync(WidgetBP);
		if (!Report.IsClean())
		{
			return Report;
		}

		FKismetEditorUtilities::CompileBlueprint(WidgetBP);

		const FSyncReport After = Sync(WidgetBP);
		Report.Added += After.Added;
		Report.Pruned += After.Pruned;
		Report.Repaired += After.Repaired;
		Report.Unusable = After.Unusable;
		Report.Defects = After.Defects;
		Report.bCompiled = true;
		return Report;
	}

	/** The error a handler returns when CompileChecked refused to compile. */
	inline TSharedPtr<FJsonValue> BlockedError(const FString& AssetPath, const FSyncReport& Report)
	{
		return MCPError(FString::Printf(
			TEXT("Refusing to compile '%s': %s. Two widget variables cannot share a GUID - the UMG ")
			TEXT("compiler reports it and external assets that reference either one by GUID resolve to ")
			TEXT("whichever it reaches first, so the bridge will not reassign one behind your back. ")
			TEXT("Nothing was compiled or saved. Open the asset in the UMG editor and delete and ")
			TEXT("recreate one of %s."),
			*AssetPath, *Report.DefectList(), *Report.UnusableList()));
	}
}

/** Stamp GUID bookkeeping onto a widget mutation result, and withdraw the
 *  success claim when a variable the compiler generates was left with a map
 *  entry the compiler will refuse. Silence there is what turns into an editor
 *  ensure the next time anything compiles the asset. */
inline void MCPSetWidgetGuidOutcome(
	const TSharedPtr<FJsonObject>& Result,
	const MCPWidgetGuidMap::FSyncReport& Report,
	const FString& AssetPath)
{
	if (!Result.IsValid())
	{
		return;
	}

	// Only when there was something to report: a property write that changed no
	// metadata should not carry three zeroes describing the metadata it left
	// alone.
	if (Report.Pruned > 0)
	{
		Result->SetNumberField(TEXT("prunedGuidEntries"), Report.Pruned);
	}
	if (Report.Added > 0)
	{
		Result->SetNumberField(TEXT("widgetGuidEntriesAdded"), Report.Added);
	}
	if (Report.Evicted > 0)
	{
		Result->SetNumberField(TEXT("evictedWidgets"), Report.Evicted);
	}
	if (Report.Repaired > 0)
	{
		Result->SetNumberField(TEXT("repairedGuidEntries"), Report.Repaired);
	}
	if (Report.IsClean())
	{
		return;
	}

	TArray<TSharedPtr<FJsonValue>> Missing;
	for (const FName& Name : Report.Unusable)
	{
		Missing.Add(MakeShared<FJsonValueString>(Name.ToString()));
	}
	Result->SetArrayField(TEXT("widgetsWithUnusableGuid"), Missing);
	Result->SetBoolField(TEXT("success"), false);
	Result->SetStringField(TEXT("error"), FString::Printf(
		TEXT("'%s' was changed, but its widget variable GUID bookkeeping is not in a state the UMG ")
		TEXT("compiler accepts: %s. The bridge does not reassign a shared GUID on its own, because ")
		TEXT("external assets reference these variables by GUID across renames. This asset reports the ")
		TEXT("failure on every compile until one of %s is deleted and recreated in the UMG editor."),
		*AssetPath, *Report.DefectList(), *Report.UnusableList()));
}