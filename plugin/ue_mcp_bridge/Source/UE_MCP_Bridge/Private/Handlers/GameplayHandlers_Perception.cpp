// AI Perception: read what a perception setup actually is, remove a sense from
// it, and read/drive it at runtime.
//
// The bridge could add an AIPerceptionComponent and append a sense config to
// it. Nothing could read one back, nothing could remove one, and nothing could
// answer the only question a perception setup exists to answer: does this AI
// see that actor right now. A surface that can only write is a surface whose
// output nobody can check.
//
// There are deliberately NO per-parameter setters here. SightRadius,
// LoseSightRadius, PeripheralVisionAngleDegrees, HearingRange,
// DetectionByAffiliation and every other tunable is a UPROPERTY on the sense
// config object; read_perception returns each config's objectPath, and
// editor(set_property) writes a property at an object path. A handler earns a
// place in this file only when it must call an engine function that reflection
// cannot reach:
//
//   * remove_sense       - SensesConfig is an Instanced TArray<UAISenseConfig*>.
//                          No generic remove-array-element action exists, and
//                          set_property cannot mint or destroy the instanced
//                          subobjects the array holds.
//   * get_perceived_actors - reads PerceptualData, a bare C++ TMap that is not
//                          a UPROPERTY at all, through the component's own
//                          GetCurrentlyPerceivedActors / GetKnownPerceivedActors.
//   * check_perception   - HasAnyActiveStimulus / HasActiveStimulus /
//                          GetYoungestStimulusAge are plain C++ methods, not
//                          UFUNCTIONs, so editor(invoke_function) cannot reach
//                          them.
//   * report_noise_event - UAISense_Hearing::ReportNoiseEvent and
//                          UAISense_Damage::ReportDamageEvent are the only way
//                          to inject a stimulus without a real game event.
//
// Everything runtime needs a world with an AI system, which the pure editor
// world does not have, so those actions take world/pieInstance the same way
// run_eqs_query does.

#include "GameplayHandlers.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Engine/Blueprint.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Controller.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/UnrealType.h"
#include "UObject/EnumProperty.h"
#include "UObject/TextProperty.h"
#include "UObject/UObjectIterator.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionStimuliSourceComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISense.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Damage.h"
#include "Perception/AISenseConfig.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"

namespace
{
	// Every helper in this file carries the PerceptionLocal prefix on purpose.
	// The module is a unity build: two translation units that share a blob
	// merge their anonymous namespaces, and a helper whose name collides with
	// one in a neighbouring handler file is a redefinition (C2084) that shows
	// up on whichever machine's adaptive-unity working set groups them
	// together, not necessarily this one.

	/** Short label for a sense or sense-config class: "AISenseConfig_Sight"
	 *  and "AISense_Sight" both read as "Sight", which is what the editor's
	 *  dropdown shows and what every senseType parameter here accepts. */
	FString PerceptionLocalShortSenseName(const UClass* Cls)
	{
		if (!Cls) return FString(TEXT("none"));
		const FString Name = Cls->GetName();
		int32 Underscore = INDEX_NONE;
		if (Name.FindChar(TEXT('_'), Underscore)) return Name.RightChop(Underscore + 1);
		return Name;
	}

	/** Every concrete UAISense subclass, by short name, sorted. Used to make a
	 *  bad senseType say what the good ones are instead of just refusing. */
	FString PerceptionLocalListSenseNames()
	{
		TArray<FString> Names;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Candidate = *It;
			if (Candidate == UAISense::StaticClass() || !Candidate->IsChildOf(UAISense::StaticClass())) continue;
			if (Candidate->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)) continue;
			Names.Add(PerceptionLocalShortSenseName(Candidate));
		}
		Names.Sort();
		return Names.Num() > 0 ? FString::Join(Names, TEXT(", ")) : FString(TEXT("(none registered)"));
	}

	/**
	 * Resolve a senseType token to the UAISense subclass it names.
	 *
	 * Accepts "Sight", "AISense_Sight", "AISenseConfig_Sight" and a full class
	 * path, because those are the four spellings that appear in this bridge's
	 * own results. A config class is translated through its
	 * GetSenseImplementation(), which is the mapping the engine itself uses.
	 *
	 * An EMPTY spec is not an error: it means "every sense", and the callers
	 * pass the resulting nullptr straight through to the engine, which reads a
	 * null TSubclassOf as exactly that.
	 */
	UClass* PerceptionLocalResolveSenseClass(const FString& Spec, TSharedPtr<FJsonValue>& OutError)
	{
		OutError.Reset();
		FString Token = Spec;
		Token.TrimStartAndEndInline();
		if (Token.IsEmpty()) return nullptr;

		UClass* Resolved = LoadObject<UClass>(nullptr, *Token);
		if (!Resolved) Resolved = FindClassByShortName(Token);
		if (!Resolved) Resolved = FindClassByShortName(TEXT("AISense_") + Token);
		if (!Resolved) Resolved = FindClassByShortName(TEXT("AISenseConfig_") + Token);

		if (Resolved && Resolved->IsChildOf(UAISenseConfig::StaticClass()))
		{
			const UAISenseConfig* ConfigCDO = Cast<UAISenseConfig>(Resolved->GetDefaultObject());
			UClass* Implementation = ConfigCDO ? ConfigCDO->GetSenseImplementation().Get() : nullptr;
			if (Implementation) return Implementation;
			OutError = MCPError(FString::Printf(
				TEXT("senseType '%s' resolved to the config class %s, but that config declares no sense ")
				TEXT("implementation, so there is no sense to filter by. Pass the sense name instead. Available: %s."),
				*Spec, *Resolved->GetName(), *PerceptionLocalListSenseNames()));
			return nullptr;
		}
		if (Resolved && Resolved->IsChildOf(UAISense::StaticClass())) return Resolved;

		OutError = MCPError(FString::Printf(
			TEXT("senseType '%s' does not name a UAISense subclass. Searched it directly, as a class ")
			TEXT("path, as 'AISense_%s' and as 'AISenseConfig_%s'. Available: %s. Omit senseType ")
			TEXT("entirely to mean every sense."),
			*Spec, *Spec, *Spec, *PerceptionLocalListSenseNames()));
		return nullptr;
	}

	/**
	 * The AIPerceptionComponent that answers for this actor.
	 *
	 * A Pawn almost never owns its own perception component: the component
	 * lives on the AAIController that possesses it, and the pawn is only the
	 * body the component senses from. Searching the actor alone answers "no
	 * perception component" for the single most common setup in the engine, so
	 * the controller is searched too and the result says where it was found.
	 */
	UAIPerceptionComponent* PerceptionLocalFindComponent(AActor* Actor, FString& OutOwnerDescription)
	{
		OutOwnerDescription.Reset();
		if (!IsValid(Actor)) return nullptr;

		if (UAIPerceptionComponent* Direct = Actor->FindComponentByClass<UAIPerceptionComponent>())
		{
			OutOwnerDescription = FString::Printf(TEXT("on the actor '%s' itself"), *Actor->GetActorLabel());
			return Direct;
		}
		if (APawn* Pawn = Cast<APawn>(Actor))
		{
			if (AController* Controller = Pawn->GetController())
			{
				if (UAIPerceptionComponent* ViaController = Controller->FindComponentByClass<UAIPerceptionComponent>())
				{
					OutOwnerDescription = FString::Printf(
						TEXT("on the controller '%s' possessing the pawn '%s'"),
						*Controller->GetName(), *Actor->GetActorLabel());
					return ViaController;
				}
			}
		}
		return nullptr;
	}

	/** The refusal for an actor with no perception component anywhere. Names
	 *  both places that were searched, because "the pawn has none" is not the
	 *  same answer as "neither the pawn nor its controller has one". */
	TSharedPtr<FJsonValue> PerceptionLocalNoComponentError(AActor* Actor, const TCHAR* Role)
	{
		const bool bIsPawn = Actor && Actor->IsA<APawn>();
		const APawn* Pawn = Cast<APawn>(Actor);
		const bool bPossessed = Pawn && Pawn->GetController() != nullptr;

		FString Where = TEXT("Searched the actor's own components");
		if (bIsPawn)
		{
			Where += bPossessed
				? TEXT(" and the components of the controller possessing it")
				: TEXT("; it is an unpossessed Pawn, so there is no controller to search");
		}

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), false);
		Obj->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%s '%s' has no AIPerceptionComponent. %s. In Unreal the component normally lives on ")
			TEXT("the AAIController, not the Pawn, so name the controller directly if the pawn is not ")
			TEXT("possessed yet. gameplay(add_perception_component) puts one on a Blueprint, and ")
			TEXT("gameplay(read_perception) reports what a Blueprint or a live actor currently has."),
			Role, Actor ? *Actor->GetActorLabel() : TEXT("(null)"), *Where));
		if (Actor)
		{
			Obj->SetStringField(TEXT("actorPath"), Actor->GetPathName());
			Obj->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetName());
			Obj->SetBoolField(TEXT("isPawn"), bIsPawn);
			Obj->SetBoolField(TEXT("isPossessed"), bPossessed);
		}
		return MakeShared<FJsonValueObject>(Obj);
	}

	/** Locate the AIPerceptionComponent template on a Blueprint's construction
	 *  script, honouring an optional componentName. */
	UAIPerceptionComponent* PerceptionLocalFindTemplate(
		UBlueprint* Blueprint,
		const FString& ComponentName,
		USCS_Node*& OutNode,
		TArray<FString>& OutAllNames)
	{
		OutNode = nullptr;
		OutAllNames.Reset();
		if (!Blueprint || !Blueprint->SimpleConstructionScript) return nullptr;

		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (!Node || !Node->ComponentTemplate) continue;
			UAIPerceptionComponent* Candidate = Cast<UAIPerceptionComponent>(Node->ComponentTemplate);
			if (!Candidate) continue;
			OutAllNames.Add(Node->GetVariableName().ToString());
			if (!ComponentName.IsEmpty() && Node->GetVariableName() != FName(*ComponentName)) continue;
			if (!OutNode)
			{
				OutNode = Node;
			}
		}
		return OutNode ? Cast<UAIPerceptionComponent>(OutNode->ComponentTemplate) : nullptr;
	}

	/** The affiliation filter on a sense config, or null when that sense has
	 *  none (Touch and Prediction do not filter by team). */
	const FAISenseAffiliationFilter* PerceptionLocalFindAffiliation(const UAISenseConfig* Config)
	{
		if (!Config) return nullptr;
		for (TFieldIterator<FProperty> It(Config->GetClass()); It; ++It)
		{
			const FStructProperty* StructProp = CastField<FStructProperty>(*It);
			if (!StructProp || StructProp->Struct != FAISenseAffiliationFilter::StaticStruct()) continue;
			return StructProp->ContainerPtrToValuePtr<FAISenseAffiliationFilter>(Config);
		}
		return nullptr;
	}

	/** Read a float UPROPERTY off a config by name. Sense configs declare
	 *  MaxAge protected, so the getter's "0 means never" translation is the
	 *  only public view of it and the raw authored number is not reachable
	 *  except through reflection. */
	bool PerceptionLocalReadFloat(const UObject* Object, const TCHAR* PropertyName, float& OutValue)
	{
		if (!Object) return false;
		FProperty* Prop = Object->GetClass()->FindPropertyByName(FName(PropertyName));
		if (const FFloatProperty* FloatProp = CastField<FFloatProperty>(Prop))
		{
			OutValue = FloatProp->GetPropertyValue_InContainer(Object);
			return true;
		}
		if (const FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Prop))
		{
			OutValue = static_cast<float>(DoubleProp->GetPropertyValue_InContainer(Object));
			return true;
		}
		return false;
	}

	/** Location as the wire shape every other action in this bridge uses. */
	TSharedPtr<FJsonObject> PerceptionLocalVectorToJson(const FVector& V)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), V.X);
		Obj->SetNumberField(TEXT("y"), V.Y);
		Obj->SetNumberField(TEXT("z"), V.Z);
		return Obj;
	}

	/** One perceived actor, with enough to act on it without a second call. */
	TSharedPtr<FJsonObject> PerceptionLocalDescribePerceived(
		const UAIPerceptionComponent* Component,
		AActor* Perceiver,
		AActor* Target)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		if (!Target) return Row;
		Row->SetStringField(TEXT("actorLabel"), Target->GetActorLabel());
		Row->SetStringField(TEXT("actorPath"), Target->GetPathName());
		Row->SetStringField(TEXT("actorClass"), Target->GetClass()->GetName());
		Row->SetObjectField(TEXT("location"), PerceptionLocalVectorToJson(Target->GetActorLocation()));
		if (Perceiver)
		{
			Row->SetNumberField(TEXT("distance"),
				FVector::Dist(Perceiver->GetActorLocation(), Target->GetActorLocation()));
		}
		if (Component)
		{
			if (const FActorPerceptionInfo* Info = Component->GetActorInfo(*Target))
			{
				float Age = FAIStimulus::NeverHappenedAge;
				const FVector LastKnown = Info->GetLastStimulusLocation(&Age);
				Row->SetBoolField(TEXT("currentlySensed"), Info->HasAnyCurrentStimulus());
				Row->SetBoolField(TEXT("remembered"), Info->HasAnyKnownStimulus());
				Row->SetBoolField(TEXT("isHostile"), Info->bIsHostile != 0);
				Row->SetBoolField(TEXT("isFriendly"), Info->bIsFriendly != 0);
				const bool bEverSensed = Age < FAIStimulus::NeverHappenedAge;
				Row->SetBoolField(TEXT("everSensed"), bEverSensed);
				if (bEverSensed)
				{
					Row->SetNumberField(TEXT("stimulusAgeSeconds"), Age);
					if (LastKnown != FAISystem::InvalidLocation)
					{
						Row->SetObjectField(TEXT("lastKnownLocation"), PerceptionLocalVectorToJson(LastKnown));
					}
				}
			}
		}
		return Row;
	}

	/** Capture one property's value as a JSON value that
	 *  MCPJsonProperty::SetJsonOnProperty can write back.
	 *
	 *  Returns an unset pointer for anything it cannot round-trip, and names
	 *  that property in OutUncaptured. A rollback that silently dropped a
	 *  property would be worse than no rollback at all: the caller would
	 *  believe the tuning came back. */
	TSharedPtr<FJsonValue> PerceptionLocalCaptureValue(
		const FProperty* Prop,
		const void* Container,
		int32 Depth,
		const FString& PathPrefix,
		TArray<FString>& OutUncaptured)
	{
		const FString Path = PathPrefix.IsEmpty() ? Prop->GetName() : PathPrefix + TEXT(".") + Prop->GetName();

		if (Prop->ArrayDim > 1 || Depth > 3)
		{
			OutUncaptured.Add(Path);
			return TSharedPtr<FJsonValue>();
		}

		const void* Value = Prop->ContainerPtrToValuePtr<void>(Container);

		if (const FBoolProperty* BoolProp = CastField<FBoolProperty>(Prop))
		{
			return MakeShared<FJsonValueBoolean>(BoolProp->GetPropertyValue(Value));
		}
		if (const FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
		{
			const FNumericProperty* Underlying = EnumProp->GetUnderlyingProperty();
			const int64 Raw = Underlying->GetSignedIntPropertyValue(Value);
			return MakeShared<FJsonValueString>(EnumProp->GetEnum()->GetNameStringByValue(Raw));
		}
		if (const FByteProperty* ByteProp = CastField<FByteProperty>(Prop))
		{
			if (ByteProp->Enum)
			{
				return MakeShared<FJsonValueString>(
					ByteProp->Enum->GetNameStringByValue(ByteProp->GetSignedIntPropertyValue(Value)));
			}
			return MakeShared<FJsonValueNumber>(ByteProp->GetSignedIntPropertyValue(Value));
		}
		if (const FNumericProperty* NumProp = CastField<FNumericProperty>(Prop))
		{
			return MakeShared<FJsonValueNumber>(
				NumProp->IsFloatingPoint()
					? NumProp->GetFloatingPointPropertyValue(Value)
					: static_cast<double>(NumProp->GetSignedIntPropertyValue(Value)));
		}
		if (CastField<FStrProperty>(Prop) || CastField<FNameProperty>(Prop) || CastField<FTextProperty>(Prop))
		{
			FString Text;
			Prop->ExportTextItem_Direct(Text, Value, nullptr, nullptr, PPF_None);
			return MakeShared<FJsonValueString>(Text);
		}
		if (const FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
		{
			// A reference is restored by path, which is what SetJsonOnProperty
			// accepts. A null one has no path to write, so it is reported
			// rather than guessed at.
			if (const UObject* Referenced = ObjProp->GetObjectPropertyValue(Value))
			{
				return MakeShared<FJsonValueString>(Referenced->GetPathName());
			}
			OutUncaptured.Add(Path);
			return TSharedPtr<FJsonValue>();
		}
		if (const FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			TSharedPtr<FJsonObject> Nested = MakeShared<FJsonObject>();
			bool bAny = false;
			for (TFieldIterator<FProperty> It(StructProp->Struct); It; ++It)
			{
				TSharedPtr<FJsonValue> Inner =
					PerceptionLocalCaptureValue(*It, Value, Depth + 1, Path, OutUncaptured);
				if (Inner.IsValid())
				{
					Nested->SetField(It->GetName(), Inner);
					bAny = true;
				}
			}
			if (bAny) return MakeShared<FJsonValueObject>(Nested);
		}

		OutUncaptured.Add(Path);
		return TSharedPtr<FJsonValue>();
	}

	/** Every value on a sense config that differs from its class default: the
	 *  tuning a caller would lose when the config object goes away. */
	TSharedPtr<FJsonObject> PerceptionLocalCaptureTunedSettings(
		const UAISenseConfig* Config,
		TArray<FString>& OutUncaptured,
		int32& OutTunedCount)
	{
		TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
		OutTunedCount = 0;
		if (!Config) return Settings;

		const UObject* Defaults = Config->GetClass()->GetDefaultObject();
		if (!Defaults) return Settings;

		for (TFieldIterator<FProperty> It(Config->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_DuplicateTransient)) continue;
			if (Prop->Identical_InContainer(Config, Defaults, 0, PPF_None)) continue;

			++OutTunedCount;
			TSharedPtr<FJsonValue> Captured =
				PerceptionLocalCaptureValue(Prop, Config, 0, FString(), OutUncaptured);
			if (Captured.IsValid())
			{
				Settings->SetField(Prop->GetName(), Captured);
			}
		}
		return Settings;
	}

	/** One sense config as JSON: identity, the two settings every sense
	 *  shares, its own parameters, and the objectPath that makes all of them
	 *  writable through editor(set_property). */
	TSharedPtr<FJsonObject> PerceptionLocalDescribeSense(const UAISenseConfig* Config, int32 Index)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("index"), Index);
		if (!Config)
		{
			Entry->SetBoolField(TEXT("null"), true);
			return Entry;
		}

		Entry->SetStringField(TEXT("senseType"), PerceptionLocalShortSenseName(Config->GetClass()));
		Entry->SetStringField(TEXT("configClass"), Config->GetClass()->GetName());
		// The reason no typed setters exist: this path plus editor(set_property)
		// writes every tunable on this sense.
		Entry->SetStringField(TEXT("objectPath"), Config->GetPathName());

		const UClass* Implementation = Config->GetSenseImplementation().Get();
		Entry->SetStringField(TEXT("senseImplementation"),
			Implementation ? Implementation->GetName() : TEXT("none"));

		float RawMaxAge = 0.f;
		if (PerceptionLocalReadFloat(Config, TEXT("MaxAge"), RawMaxAge))
		{
			Entry->SetNumberField(TEXT("maxAge"), RawMaxAge);
			// 0 is not "expires instantly", it is "never expires", and reading
			// it the other way round is how a stale memory gets diagnosed as a
			// perception failure.
			Entry->SetBoolField(TEXT("maxAgeMeansNeverExpires"), RawMaxAge == 0.f);
		}
		Entry->SetBoolField(TEXT("startsEnabled"), Config->GetStartsEnabled());

		const FAISenseID SenseID = Config->GetSenseID();
		Entry->SetBoolField(TEXT("registered"), SenseID.IsValid());
		if (SenseID.IsValid()) Entry->SetNumberField(TEXT("senseId"), SenseID.Index);

		if (const FAISenseAffiliationFilter* Filter = PerceptionLocalFindAffiliation(Config))
		{
			TSharedPtr<FJsonObject> Affiliation = MakeShared<FJsonObject>();
			Affiliation->SetBoolField(TEXT("detectEnemies"), Filter->bDetectEnemies != 0);
			Affiliation->SetBoolField(TEXT("detectNeutrals"), Filter->bDetectNeutrals != 0);
			Affiliation->SetBoolField(TEXT("detectFriendlies"), Filter->bDetectFriendlies != 0);
			Entry->SetObjectField(TEXT("detectionByAffiliation"), Affiliation);
		}

		// Every UPROPERTY on the config, exported as text, so a caller can see
		// the current value of anything it is about to set_property.
		TSharedPtr<FJsonObject> Parameters = MakeShared<FJsonObject>();
		for (TFieldIterator<FProperty> It(Config->GetClass()); It; ++It)
		{
			Parameters->SetField(It->GetName(), MCPExportPropertyValue(*It, Config));
		}
		Entry->SetObjectField(TEXT("parameters"), Parameters);
		return Entry;
	}
}

TSharedPtr<FJsonValue> FGameplayHandlers::ReadPerception(const TSharedPtr<FJsonObject>& Params)
{
	const FString BlueprintPath = OptionalString(Params, TEXT("blueprintPath"));
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	const bool bHasActorSelector =
		Params->HasField(TEXT("actorLabel")) || Params->HasField(TEXT("actorPath"));

	if (BlueprintPath.IsEmpty() && !bHasActorSelector)
	{
		return MCPError(TEXT(
			"Missing target. Pass 'blueprintPath' to read the AIPerceptionComponent template on a "
			"Blueprint, or 'actorLabel'/'actorPath' (with world=\"pie\") to read a live actor's "
			"component. Those are the only two things that hold a perception setup."));
	}

	// The world is needed for the live-actor route, and for the stimuli-source
	// audit on either route. "auto" prefers PIE, which is where perception
	// actually runs.
	UWorld* World = ResolveWorldFromParams(Params, TEXT("auto"));

	UAIPerceptionComponent* Component = nullptr;
	AActor* Actor = nullptr;
	FString OwnerDescription;
	FString ResolvedComponentName;
	TArray<FString> AvailableComponents;
	FString Source;

	if (!BlueprintPath.IsEmpty())
	{
		Source = TEXT("blueprint");
		UBlueprint* Blueprint = Cast<UBlueprint>(MCPLoadAssetObject(BlueprintPath));
		if (!Blueprint)
		{
			if (UObject* Found = MCPLoadAssetObject(BlueprintPath))
			{
				return MCPAssetWrongTypeError(BlueprintPath, Found, TEXT("Blueprint"));
			}
			return MCPAssetNotFoundError(BlueprintPath, TEXT("Blueprint"));
		}

		USCS_Node* Node = nullptr;
		Component = PerceptionLocalFindTemplate(Blueprint, ComponentName, Node, AvailableComponents);
		if (!Component)
		{
			return MCPError(FString::Printf(
				TEXT("No AIPerceptionComponent%s on '%s'. Its construction script holds %d perception "
					 "component(s): [%s]. Add one with gameplay(add_perception_component)."),
				ComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" named '%s'"), *ComponentName),
				*BlueprintPath,
				AvailableComponents.Num(),
				AvailableComponents.Num() > 0 ? *FString::Join(AvailableComponents, TEXT(", ")) : TEXT("none")));
		}
		ResolvedComponentName = Node ? Node->GetVariableName().ToString() : TEXT("");
		OwnerDescription = FString::Printf(TEXT("on the Blueprint construction script of '%s'"), *BlueprintPath);
	}
	else
	{
		Source = TEXT("actor");
		if (!World)
		{
			return MCPError(TEXT(
				"No world available for the actor lookup. Start Play-In-Editor and pass world=\"pie\", "
				"or read the Blueprint template instead by passing 'blueprintPath'."));
		}
		TSharedPtr<FJsonValue> Error;
		FMCPActorSelector Selector;
		Selector.Match = EMCPActorMatch::LabelNameOrPath;
		Selector.WorldLabel = World->IsPlayInEditor() ? TEXT("PIE") : TEXT("editor");
		Actor = MCPResolveActor(World, Params, Error, Selector);
		if (!Actor) return Error;

		Component = PerceptionLocalFindComponent(Actor, OwnerDescription);
		if (!Component) return PerceptionLocalNoComponentError(Actor, TEXT("Actor"));
		ResolvedComponentName = Component->GetName();
	}

	// -- The senses themselves -----------------------------------------------

	TArray<TSharedPtr<FJsonValue>> Senses;
	TArray<FString> Problems;
	int32 SenseCount = 0;
	int32 EnabledCount = 0;
	bool bHasSight = false;

	for (auto It = Component->GetSensesConfigIterator(); It; ++It)
	{
		const UAISenseConfig* Config = *It;
		Senses.Add(MakeShared<FJsonValueObject>(PerceptionLocalDescribeSense(Config, SenseCount)));
		if (!Config)
		{
			Problems.Add(FString::Printf(
				TEXT("sense config %d is null, so that slot senses nothing; remove it with "
					 "gameplay(remove_sense) at index %d"), SenseCount, SenseCount));
			++SenseCount;
			continue;
		}
		if (Config->GetStartsEnabled()) ++EnabledCount;

		if (const UAISenseConfig_Sight* Sight = Cast<UAISenseConfig_Sight>(Config))
		{
			bHasSight = true;
			// LoseSightRadius below SightRadius means a target is dropped the
			// instant it is acquired, which reads in game as flickering
			// detection rather than as a misconfiguration.
			if (Sight->LoseSightRadius < Sight->SightRadius)
			{
				Problems.Add(FString::Printf(
					TEXT("sense %d (Sight): LoseSightRadius (%.1f) is below SightRadius (%.1f), so a "
						 "target is forgotten at a shorter range than it is acquired and detection "
						 "flickers. Raise LoseSightRadius with editor(set_property) at that sense's objectPath."),
					SenseCount, Sight->LoseSightRadius, Sight->SightRadius));
			}
			if (Sight->SightRadius <= 0.f)
			{
				Problems.Add(FString::Printf(
					TEXT("sense %d (Sight): SightRadius is %.1f, so nothing is ever within sight range."),
					SenseCount, Sight->SightRadius));
			}
		}

		if (const FAISenseAffiliationFilter* Filter = PerceptionLocalFindAffiliation(Config))
		{
			if (!Filter->bDetectEnemies && !Filter->bDetectNeutrals && !Filter->bDetectFriendlies)
			{
				Problems.Add(FString::Printf(
					TEXT("sense %d (%s): every DetectionByAffiliation flag is false, so this sense "
						 "rejects every actor regardless of range. Set at least one of "
						 "DetectionByAffiliation.bDetectEnemies / bDetectNeutrals / bDetectFriendlies "
						 "with editor(set_property) at that sense's objectPath. An actor with no team "
						 "interface reads as Neutral."),
					SenseCount, *PerceptionLocalShortSenseName(Config->GetClass())));
			}
		}
		++SenseCount;
	}

	if (SenseCount == 0)
	{
		// The component exists, is registered as a listener, and perceives
		// nothing at all. Nothing about the component itself says so.
		Problems.Add(TEXT(
			"the component has no sense configs, so it perceives nothing. Add one with "
			"gameplay(configure_ai_perception_sense)."));
	}
	else if (EnabledCount == 0)
	{
		Problems.Add(TEXT(
			"every configured sense has bStartsEnabled false, so the component perceives nothing "
			"until something calls SetSenseEnabled at runtime."));
	}

	// -- Can sight ever fire in this world -----------------------------------

	TSharedPtr<FJsonObject> StimuliAudit = MakeShared<FJsonObject>();
	bool bStimuliAuditRan = false;
	if (bHasSight)
	{
		const UAISense_Sight* SightCDO = GetDefault<UAISense_Sight>();
		const bool bAutoRegisterPawns = SightCDO && SightCDO->ShouldAutoRegisterAllPawnsAsSources();
		StimuliAudit->SetBoolField(TEXT("sightAutoRegistersAllPawns"), bAutoRegisterPawns);

		if (World)
		{
			bStimuliAuditRan = true;
			int32 SourceComponents = 0;
			int32 PawnCount = 0;
			TArray<TSharedPtr<FJsonValue>> SourceRows;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				AActor* Candidate = *It;
				if (!IsValid(Candidate)) continue;
				if (Candidate->IsA<APawn>()) ++PawnCount;
				if (Candidate->FindComponentByClass<UAIPerceptionStimuliSourceComponent>())
				{
					++SourceComponents;
					if (SourceRows.Num() < 10)
					{
						TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
						Row->SetStringField(TEXT("actorLabel"), Candidate->GetActorLabel());
						Row->SetStringField(TEXT("actorPath"), Candidate->GetPathName());
						SourceRows.Add(MakeShared<FJsonValueObject>(Row));
					}
				}
			}
			StimuliAudit->SetNumberField(TEXT("stimuliSourceComponents"), SourceComponents);
			StimuliAudit->SetNumberField(TEXT("pawnsInWorld"), PawnCount);
			StimuliAudit->SetArrayField(TEXT("stimuliSources"), SourceRows);
			StimuliAudit->SetStringField(TEXT("world"),
				World->IsPlayInEditor() ? TEXT("pie") : TEXT("editor"));

			if (SourceComponents == 0 && !bAutoRegisterPawns)
			{
				Problems.Add(FString::Printf(
					TEXT("Sight is configured but nothing in the %s world registers as a sight stimuli "
						 "source: no actor carries an AIPerceptionStimuliSourceComponent, and the Sight "
						 "sense's bAutoRegisterAllPawnsAsSources is false, so sight can never fire. "
						 "Either add an AIPerceptionStimuliSourceComponent registered for AISense_Sight "
						 "to the targets, or set bAutoRegisterAllPawnsAsSources on AISense_Sight in "
						 "project settings."),
					World->IsPlayInEditor() ? TEXT("PIE") : TEXT("editor")));
			}
			else if (SourceComponents == 0 && bAutoRegisterPawns && PawnCount == 0)
			{
				Problems.Add(TEXT(
					"Sight is configured and AISense_Sight auto-registers pawns, but the world holds no "
					"pawns and no actor carries an AIPerceptionStimuliSourceComponent, so there is "
					"nothing for it to see."));
			}
		}
		else
		{
			StimuliAudit->SetStringField(TEXT("note"), TEXT(
				"No world was resolved, so the stimuli-source audit did not run. Pass world=\"pie\" "
				"with Play-In-Editor running to check whether anything in the level can be seen."));
		}
	}

	// -- Result --------------------------------------------------------------

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("source"), Source);
	if (!BlueprintPath.IsEmpty()) Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	if (Actor)
	{
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	}
	Result->SetStringField(TEXT("component"), ResolvedComponentName);
	// The component's own objectPath: DominantSense lives here, not on a sense.
	Result->SetStringField(TEXT("componentObjectPath"), Component->GetPathName());
	Result->SetStringField(TEXT("componentFoundOn"), OwnerDescription);
	if (AvailableComponents.Num() > 1)
	{
		Result->SetArrayField(TEXT("otherPerceptionComponents"), MCPStringListToJson(AvailableComponents));
	}

	const UClass* Dominant = Component->GetDominantSense().Get();
	Result->SetStringField(TEXT("dominantSense"),
		Dominant ? PerceptionLocalShortSenseName(Dominant) : TEXT("none"));

	Result->SetNumberField(TEXT("senseCount"), SenseCount);
	Result->SetNumberField(TEXT("sensesStartingEnabled"), EnabledCount);
	Result->SetArrayField(TEXT("senses"), Senses);
	if (bHasSight)
	{
		Result->SetObjectField(TEXT("stimuliSourceAudit"), StimuliAudit);
		Result->SetBoolField(TEXT("stimuliSourceAuditRan"), bStimuliAuditRan);
	}

	Result->SetArrayField(TEXT("problems"), MCPStringListToJson(Problems));
	Result->SetBoolField(TEXT("perceivable"), Problems.Num() == 0);
	Result->SetStringField(TEXT("note"), TEXT(
		"Every sense parameter is a UPROPERTY. Tune one with editor(set_property) at that sense's "
		"objectPath - there are no per-parameter setters. gameplay(get_perceived_actors) and "
		"gameplay(check_perception) report what this component actually senses at runtime."));
	if (World) MCPNoteLoadedOnlyEnumeration(World, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::RemoveSense(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintPath;
	if (auto Err = RequireString(Params, TEXT("blueprintPath"), BlueprintPath)) return Err;

	if (MCPIsProtectedAssetPath(BlueprintPath))
	{
		return MCPProtectedPathError(BlueprintPath);
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(MCPLoadAssetObject(BlueprintPath));
	if (!Blueprint)
	{
		if (UObject* Found = MCPLoadAssetObject(BlueprintPath))
		{
			return MCPAssetWrongTypeError(BlueprintPath, Found, TEXT("Blueprint"));
		}
		return MCPAssetNotFoundError(BlueprintPath, TEXT("Blueprint"));
	}

	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	USCS_Node* Node = nullptr;
	TArray<FString> AvailableComponents;
	UAIPerceptionComponent* Template =
		PerceptionLocalFindTemplate(Blueprint, ComponentName, Node, AvailableComponents);
	if (!Template)
	{
		return MCPError(FString::Printf(
			TEXT("No AIPerceptionComponent%s on '%s'. Its construction script holds %d perception "
				 "component(s): [%s]. gameplay(read_perception) reports what is there."),
			ComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" named '%s'"), *ComponentName),
			*BlueprintPath,
			AvailableComponents.Num(),
			AvailableComponents.Num() > 0 ? *FString::Join(AvailableComponents, TEXT(", ")) : TEXT("none")));
	}
	const FString ResolvedComponentName = Node ? Node->GetVariableName().ToString() : Template->GetName();

	// SensesConfig is protected and Instanced, so reflection is the only clean
	// way in - the same route configure_ai_perception_sense uses to add to it.
	FArrayProperty* SensesProp =
		CastField<FArrayProperty>(Template->GetClass()->FindPropertyByName(TEXT("SensesConfig")));
	FObjectPropertyBase* ElementProp = SensesProp ? CastField<FObjectPropertyBase>(SensesProp->Inner) : nullptr;
	if (!SensesProp || !ElementProp)
	{
		return MCPError(TEXT(
			"SensesConfig object-array property not found on AIPerceptionComponent (engine drift). "
			"Nothing was changed."));
	}
	FScriptArrayHelper Helper(SensesProp, SensesProp->ContainerPtrToValuePtr<void>(Template));

	// Two selectors: 'index' is exact, 'senseType' is what a caller has when it
	// only knows it wants the Sight one gone.
	const bool bHasIndex = Params->HasField(TEXT("index"));
	const int32 RequestedIndex = bHasIndex
		? static_cast<int32>(OptionalNumber(Params, TEXT("index"), -1.0)) : INDEX_NONE;
	const FString SenseType = OptionalString(Params, TEXT("senseType"));

	if (!bHasIndex && SenseType.IsEmpty())
	{
		TArray<FString> Present;
		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			UObject* Existing = ElementProp->GetObjectPropertyValue(Helper.GetRawPtr(i));
			Present.Add(FString::Printf(TEXT("%d=%s"), i,
				Existing ? *PerceptionLocalShortSenseName(Existing->GetClass()) : TEXT("null")));
		}
		return MCPError(FString::Printf(
			TEXT("Missing selector: pass 'index' (exact) or 'senseType' (e.g. \"Sight\"). '%s' currently "
				 "has %d sense config(s): [%s]. gameplay(read_perception) lists them with their indices."),
			*ResolvedComponentName, Helper.Num(),
			Present.Num() > 0 ? *FString::Join(Present, TEXT(", ")) : TEXT("none")));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	Result->SetStringField(TEXT("component"), ResolvedComponentName);

	// Resolve the selector to a concrete index.
	int32 TargetIndex = INDEX_NONE;
	if (bHasIndex)
	{
		TargetIndex = RequestedIndex;
		Result->SetNumberField(TEXT("index"), RequestedIndex);
		if (RequestedIndex < 0)
		{
			return MCPError(FString::Printf(
				TEXT("'index' must be zero or greater; got %d. '%s' has %d sense config(s)."),
				RequestedIndex, *ResolvedComponentName, Helper.Num()));
		}
	}
	else
	{
		Result->SetStringField(TEXT("senseType"), SenseType);
		TSharedPtr<FJsonValue> ResolveError;
		UClass* WantedSense = PerceptionLocalResolveSenseClass(SenseType, ResolveError);
		if (!WantedSense) return ResolveError;

		for (int32 i = 0; i < Helper.Num(); ++i)
		{
			UObject* Existing = ElementProp->GetObjectPropertyValue(Helper.GetRawPtr(i));
			const UAISenseConfig* Config = Cast<UAISenseConfig>(Existing);
			if (Config && Config->GetSenseImplementation().Get() == WantedSense)
			{
				TargetIndex = i;
				break;
			}
		}
		if (TargetIndex == INDEX_NONE)
		{
			// Idempotent: the sense is not there, which is the state the caller
			// asked for. A rollback that replays this must not fail.
			MCPSetExisted(Result);
			Result->SetBoolField(TEXT("alreadyRemoved"), true);
			Result->SetNumberField(TEXT("remainingSenses"), Helper.Num());
			Result->SetStringField(TEXT("note"), FString::Printf(
				TEXT("'%s' has no %s sense config, so there was nothing to remove."),
				*ResolvedComponentName, *SenseType));
			return MCPResult(Result);
		}
		Result->SetNumberField(TEXT("index"), TargetIndex);
	}

	// Idempotent on the index route too, for the same reason.
	if (!Helper.IsValidIndex(TargetIndex))
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		Result->SetNumberField(TEXT("remainingSenses"), Helper.Num());
		Result->SetStringField(TEXT("note"), FString::Printf(
			TEXT("Index %d is past the end; '%s' has %d sense config(s), so there was nothing to remove."),
			TargetIndex, *ResolvedComponentName, Helper.Num()));
		return MCPResult(Result);
	}

	// -- Capture what is about to be destroyed, before destroying it ---------

	UObject* DoomedObject = ElementProp->GetObjectPropertyValue(Helper.GetRawPtr(TargetIndex));
	const UAISenseConfig* Doomed = Cast<UAISenseConfig>(DoomedObject);
	const FString RemovedClass = DoomedObject ? DoomedObject->GetClass()->GetName() : FString(TEXT("null"));
	const FString RemovedSenseType = DoomedObject
		? PerceptionLocalShortSenseName(DoomedObject->GetClass()) : FString(TEXT("null"));

	TArray<FString> Uncaptured;
	int32 TunedCount = 0;
	TSharedPtr<FJsonObject> CapturedSettings =
		PerceptionLocalCaptureTunedSettings(Doomed, Uncaptured, TunedCount);
	const bool bWasLastElement = (TargetIndex == Helper.Num() - 1);

	Helper.RemoveValues(TargetIndex, 1);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	SaveAssetPackage(Blueprint);

	Result->SetBoolField(TEXT("alreadyRemoved"), false);
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("removedSenseType"), RemovedSenseType);
	Result->SetStringField(TEXT("removedConfigClass"), RemovedClass);
	Result->SetNumberField(TEXT("remainingSenses"), Helper.Num());
	Result->SetStringField(TEXT("note"), TEXT(
		"Sense indices after the removed one have shifted down by one. The removed config object is "
		"gone, so its objectPath no longer resolves."));

	// -- Rollback, and an honest account of what it cannot restore -----------
	//
	// configure_ai_perception_sense is the inverse, and it only knows the seven
	// built-in sense names. A custom UAISenseConfig subclass cannot be restored
	// by it at all, and saying so is the whole point of this block.
	static const TCHAR* RestorableSenses[] = {
		TEXT("Sight"), TEXT("Hearing"), TEXT("Damage"), TEXT("Touch"),
		TEXT("Team"), TEXT("Prediction"), TEXT("Blueprint") };
	bool bRestorable = false;
	for (const TCHAR* Name : RestorableSenses)
	{
		if (RemovedSenseType.Equals(Name, ESearchCase::CaseSensitive)) { bRestorable = true; break; }
	}

	if (!bRestorable)
	{
		Result->SetBoolField(TEXT("rollbackAvailable"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(TEXT(
			"NO ROLLBACK WAS EMITTED. gameplay(configure_ai_perception_sense) can only re-create the "
			"seven built-in sense configs (Sight, Hearing, Damage, Touch, Team, Prediction, Blueprint), "
			"and the removed config was a %s. Re-adding it requires an action that can instantiate an "
			"arbitrary UAISenseConfig subclass, which does not exist yet. The %d tuned value(s) on it "
			"are recorded in 'removedSettings' so they can be re-applied by hand."),
			*RemovedClass, TunedCount));
		Result->SetObjectField(TEXT("removedSettings"), CapturedSettings);
		if (Uncaptured.Num() > 0)
		{
			Result->SetArrayField(TEXT("removedSettingsNotCaptured"), MCPStringListToJson(Uncaptured));
		}
		return MCPResult(Result);
	}

	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("blueprintPath"), BlueprintPath);
	RollbackPayload->SetStringField(TEXT("componentName"), ResolvedComponentName);
	RollbackPayload->SetStringField(TEXT("senseType"), RemovedSenseType);
	RollbackPayload->SetObjectField(TEXT("settings"), CapturedSettings);
	MCPSetRollback(Result, TEXT("configure_ai_perception_sense"), RollbackPayload);

	Result->SetBoolField(TEXT("rollbackAvailable"), true);
	Result->SetNumberField(TEXT("tunedPropertyCount"), TunedCount);
	Result->SetObjectField(TEXT("rollbackSettings"), CapturedSettings);

	// A rollback that restores the class but silently drops tuning, or silently
	// changes the sense order, is worse than one that says so. Both facts are
	// reported as their own fields rather than buried in prose.
	const bool bLossy = Uncaptured.Num() > 0;
	Result->SetBoolField(TEXT("rollbackIsLossy"), bLossy);
	Result->SetBoolField(TEXT("rollbackRestoresAtEndOfList"), !bWasLastElement);
	if (bLossy)
	{
		Result->SetArrayField(TEXT("rollbackUncapturedProperties"), MCPStringListToJson(Uncaptured));
	}

	FString RollbackNote = FString::Printf(TEXT(
		"The rollback re-adds a %s sense config and re-applies the %d value(s) that differed from the "
		"class defaults."), *RemovedSenseType, TunedCount);
	if (bLossy)
	{
		RollbackNote += FString::Printf(TEXT(
			" IT IS LOSSY: %d property value(s) could not be represented and will come back at their "
			"class defaults - see rollbackUncapturedProperties."), Uncaptured.Num());
	}
	if (!bWasLastElement)
	{
		RollbackNote += TEXT(
			" The restored config is appended at the END of SensesConfig, so its index changes. "
			"Nothing in the engine depends on sense order except DominantSense, which is addressed by "
			"class rather than by index.");
	}
	Result->SetStringField(TEXT("rollbackNote"), RollbackNote);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::GetPerceivedActors(const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = ResolveWorldFromParams(Params, TEXT("auto"));
	if (!World)
	{
		return MCPError(TEXT(
			"No world available. Perception only produces data in a world with an AI system, which the "
			"pure editor world does not have: start Play-In-Editor and pass world=\"pie\"."));
	}

	TSharedPtr<FJsonValue> Error;
	FMCPActorSelector Selector;
	Selector.Match = EMCPActorMatch::LabelNameOrPath;
	Selector.WorldLabel = World->IsPlayInEditor() ? TEXT("PIE") : TEXT("editor");
	AActor* Actor = MCPResolveActor(World, Params, Error, Selector);
	if (!Actor) return Error;

	FString OwnerDescription;
	UAIPerceptionComponent* Component = PerceptionLocalFindComponent(Actor, OwnerDescription);
	if (!Component) return PerceptionLocalNoComponentError(Actor, TEXT("Actor"));

	const FString SenseSpec = OptionalString(Params, TEXT("senseType"));
	UClass* SenseClass = PerceptionLocalResolveSenseClass(SenseSpec, Error);
	if (!SenseSpec.IsEmpty() && !SenseClass) return Error;
	const TSubclassOf<UAISense> SenseFilter(SenseClass);

	AActor* Body = Component->GetMutableBodyActor();
	AActor* Origin = Body ? Body : Actor;

	// GetCurrentlyPerceivedActors and GetKnownPerceivedActors both read
	// PerceptualData, which is a bare TMap on the component and not a
	// UPROPERTY, so there is no reflection path to any of this.
	TArray<AActor*> Current;
	Component->GetCurrentlyPerceivedActors(SenseFilter, Current);
	TArray<AActor*> Known;
	Component->GetKnownPerceivedActors(SenseFilter, Known);
	TArray<AActor*> Hostile;
	Component->GetPerceivedHostileActors(Hostile);

	const auto ToRows = [Component, Origin](const TArray<AActor*>& Actors)
	{
		TArray<TSharedPtr<FJsonValue>> Rows;
		Rows.Reserve(Actors.Num());
		for (AActor* Target : Actors)
		{
			if (!IsValid(Target)) continue;
			Rows.Add(MakeShared<FJsonValueObject>(
				PerceptionLocalDescribePerceived(Component, Origin, Target)));
		}
		return Rows;
	};

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("component"), Component->GetName());
	Result->SetStringField(TEXT("componentObjectPath"), Component->GetPathName());
	Result->SetStringField(TEXT("componentFoundOn"), OwnerDescription);
	if (Body)
	{
		// The body is what the senses measure from, and it is often not the
		// actor the caller named.
		Result->SetStringField(TEXT("bodyActorLabel"), Body->GetActorLabel());
		Result->SetStringField(TEXT("bodyActorPath"), Body->GetPathName());
	}
	Result->SetStringField(TEXT("world"), World->IsPlayInEditor() ? TEXT("pie") : TEXT("editor"));
	Result->SetStringField(TEXT("senseFilter"),
		SenseClass ? PerceptionLocalShortSenseName(SenseClass) : TEXT("all"));

	Result->SetNumberField(TEXT("currentlyPerceivedCount"), Current.Num());
	Result->SetArrayField(TEXT("currentlyPerceived"), ToRows(Current));
	Result->SetNumberField(TEXT("knownCount"), Known.Num());
	Result->SetArrayField(TEXT("known"), ToRows(Known));
	Result->SetNumberField(TEXT("hostileCount"), Hostile.Num());
	Result->SetArrayField(TEXT("hostile"), ToRows(Hostile));

	// Configured senses, so an empty answer can be read against what the
	// component was even capable of sensing.
	TArray<FString> ConfiguredSenses;
	int32 EnabledCount = 0;
	for (auto It = Component->GetSensesConfigIterator(); It; ++It)
	{
		const UAISenseConfig* Config = *It;
		if (!Config) continue;
		const FString Short = PerceptionLocalShortSenseName(Config->GetClass());
		const bool bEnabled = Component->IsSenseEnabled(Config->GetSenseImplementation());
		if (bEnabled) ++EnabledCount;
		ConfiguredSenses.Add(FString::Printf(TEXT("%s (%s)"), *Short,
			bEnabled ? TEXT("enabled") : TEXT("disabled")));
	}
	Result->SetArrayField(TEXT("configuredSenses"), MCPStringListToJson(ConfiguredSenses));

	if (Current.Num() == 0 && Known.Num() == 0)
	{
		// An empty perception result is the normal failure and it is not an
		// error, so it has to explain itself or the caller has nothing to do.
		Result->SetStringField(TEXT("note"), FString::Printf(TEXT(
			"Nothing perceived. This component has %d sense config(s), %d of them enabled. The usual "
			"causes: PIE is not actually ticking, nothing registers as a stimuli source (run "
			"gameplay(read_perception) - its problems[] checks that), the targets are out of range, or "
			"DetectionByAffiliation rejects them. gameplay(report_noise_event) injects a stimulus to "
			"prove the pipeline works end to end."),
			ConfiguredSenses.Num(), EnabledCount));
	}
	MCPNoteLoadedOnlyEnumeration(World, Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::CheckPerception(const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = ResolveWorldFromParams(Params, TEXT("auto"));
	if (!World)
	{
		return MCPError(TEXT(
			"No world available. Perception only produces data in a world with an AI system, which the "
			"pure editor world does not have: start Play-In-Editor and pass world=\"pie\"."));
	}
	const TCHAR* WorldLabel = World->IsPlayInEditor() ? TEXT("PIE") : TEXT("editor");

	TSharedPtr<FJsonValue> Error;

	FMCPActorSelector PerceiverSelector;
	PerceiverSelector.LabelKey = TEXT("perceiverLabel");
	PerceiverSelector.PathKey = TEXT("perceiverPath");
	PerceiverSelector.Match = EMCPActorMatch::LabelNameOrPath;
	PerceiverSelector.WorldLabel = WorldLabel;
	AActor* Perceiver = MCPResolveActor(World, Params, Error, PerceiverSelector);
	if (!Perceiver) return Error;

	FMCPActorSelector TargetSelector;
	TargetSelector.LabelKey = TEXT("targetLabel");
	TargetSelector.PathKey = TEXT("targetPath");
	TargetSelector.Match = EMCPActorMatch::LabelNameOrPath;
	TargetSelector.WorldLabel = WorldLabel;
	AActor* Target = MCPResolveActor(World, Params, Error, TargetSelector);
	if (!Target) return Error;

	FString OwnerDescription;
	UAIPerceptionComponent* Component = PerceptionLocalFindComponent(Perceiver, OwnerDescription);
	if (!Component) return PerceptionLocalNoComponentError(Perceiver, TEXT("Perceiver"));

	// HasAnyActiveStimulus, HasActiveStimulus and GetYoungestStimulusAge are
	// plain C++ methods rather than UFUNCTIONs, so editor(invoke_function)
	// cannot reach any of them.
	const bool bHasActive = Component->HasAnyActiveStimulus(*Target);
	const bool bHasCurrent = Component->HasAnyCurrentStimulus(*Target);
	const float YoungestAge = Component->GetYoungestStimulusAge(*Target);
	const bool bEverSensed = YoungestAge < FAIStimulus::NeverHappenedAge;

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("perceiverLabel"), Perceiver->GetActorLabel());
	Result->SetStringField(TEXT("perceiverPath"), Perceiver->GetPathName());
	Result->SetStringField(TEXT("targetLabel"), Target->GetActorLabel());
	Result->SetStringField(TEXT("targetPath"), Target->GetPathName());
	Result->SetStringField(TEXT("component"), Component->GetName());
	Result->SetStringField(TEXT("componentObjectPath"), Component->GetPathName());
	Result->SetStringField(TEXT("componentFoundOn"), OwnerDescription);
	Result->SetStringField(TEXT("world"), World->IsPlayInEditor() ? TEXT("pie") : TEXT("editor"));

	Result->SetBoolField(TEXT("perceives"), bHasActive);
	Result->SetBoolField(TEXT("currentlySensed"), bHasCurrent);
	Result->SetBoolField(TEXT("everSensed"), bEverSensed);
	if (bEverSensed) Result->SetNumberField(TEXT("youngestStimulusAgeSeconds"), YoungestAge);

	AActor* Body = Component->GetMutableBodyActor();
	AActor* Origin = Body ? Body : Perceiver;
	Result->SetNumberField(TEXT("distance"),
		FVector::Dist(Origin->GetActorLocation(), Target->GetActorLocation()));
	Result->SetObjectField(TEXT("targetLocation"),
		PerceptionLocalVectorToJson(Target->GetActorLocation()));

	if (const FActorPerceptionInfo* Info = Component->GetActorInfo(*Target))
	{
		float LastAge = FAIStimulus::NeverHappenedAge;
		const FVector LastKnown = Info->GetLastStimulusLocation(&LastAge);
		Result->SetBoolField(TEXT("isHostile"), Info->bIsHostile != 0);
		Result->SetBoolField(TEXT("isFriendly"), Info->bIsFriendly != 0);
		if (LastKnown != FAISystem::InvalidLocation)
		{
			Result->SetObjectField(TEXT("lastKnownLocation"), PerceptionLocalVectorToJson(LastKnown));
			Result->SetNumberField(TEXT("lastKnownLocationAgeSeconds"), LastAge);
			// The gap between where the AI believes the target is and where it
			// actually is IS the interesting number in a stealth or search bug.
			Result->SetNumberField(TEXT("lastKnownLocationError"),
				FVector::Dist(LastKnown, Target->GetActorLocation()));
		}
	}
	else
	{
		Result->SetBoolField(TEXT("targetInPerceptualData"), false);
	}

	// Per-sense breakdown: which sense is carrying the detection, and which
	// configured sense is not firing. A single boolean cannot say that.
	TArray<TSharedPtr<FJsonValue>> PerSense;
	int32 SenseIndex = 0;
	for (auto It = Component->GetSensesConfigIterator(); It; ++It)
	{
		const UAISenseConfig* Config = *It;
		if (!Config) { ++SenseIndex; continue; }
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetNumberField(TEXT("index"), SenseIndex);
		Row->SetStringField(TEXT("senseType"), PerceptionLocalShortSenseName(Config->GetClass()));
		Row->SetStringField(TEXT("objectPath"), Config->GetPathName());
		Row->SetBoolField(TEXT("enabled"), Component->IsSenseEnabled(Config->GetSenseImplementation()));

		const FAISenseID SenseID = Config->GetSenseID();
		Row->SetBoolField(TEXT("registered"), SenseID.IsValid());
		if (SenseID.IsValid())
		{
			Row->SetBoolField(TEXT("hasActiveStimulus"), Component->HasActiveStimulus(*Target, SenseID));
		}
		else
		{
			// An unregistered sense has no channel, so it can never report a
			// stimulus, and that is a setup fault rather than a miss.
			Row->SetStringField(TEXT("note"), TEXT(
				"This sense has no registered FAISenseID in this world, so it can never report a "
				"stimulus. It was configured but never registered with the AIPerceptionSystem."));
		}
		PerSense.Add(MakeShared<FJsonValueObject>(Row));
		++SenseIndex;
	}
	Result->SetArrayField(TEXT("senses"), PerSense);
	Result->SetNumberField(TEXT("senseCount"), SenseIndex);

	if (SenseIndex == 0)
	{
		Result->SetStringField(TEXT("note"), TEXT(
			"The perceiver's component has no sense configs at all, so 'perceives' is false for every "
			"target regardless of position. gameplay(read_perception) reports the full setup."));
	}
	else if (!bEverSensed)
	{
		Result->SetStringField(TEXT("note"), TEXT(
			"The target has never appeared in this component's perceptual data. Run "
			"gameplay(read_perception) on the perceiver: its problems[] checks for the setup faults "
			"that make this permanent (no stimuli source, affiliation flags all false, LoseSightRadius "
			"below SightRadius) rather than a matter of position."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::ReportNoiseEvent(const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = ResolveWorldFromParams(Params, TEXT("auto"));
	if (!World)
	{
		return MCPError(TEXT(
			"No world available. A perception event needs a world with an AI system, which the pure "
			"editor world does not have: start Play-In-Editor and pass world=\"pie\"."));
	}

	UAIPerceptionSystem* PerceptionSystem = UAIPerceptionSystem::GetCurrent(*World);
	if (!PerceptionSystem)
	{
		return MCPError(TEXT(
			"This world has no AIPerceptionSystem, so a reported event has nowhere to go. Perception "
			"runs in a world with an AI system; run this in PIE (world=\"pie\")."));
	}

	FString SenseType = OptionalString(Params, TEXT("senseType"), TEXT("hearing"));
	SenseType.TrimStartAndEndInline();
	const bool bHearing = SenseType.Equals(TEXT("hearing"), ESearchCase::IgnoreCase);
	const bool bDamage = SenseType.Equals(TEXT("damage"), ESearchCase::IgnoreCase);
	if (!bHearing && !bDamage)
	{
		return MCPError(FString::Printf(
			TEXT("senseType '%s' is not one of: hearing, damage. Those are the two senses the engine "
				 "lets an outside caller inject an event into (UAISense_Hearing::ReportNoiseEvent and "
				 "UAISense_Damage::ReportDamageEvent). Sight and Touch are driven by geometry and "
				 "cannot be reported."),
			*SenseType));
	}

	TSharedPtr<FJsonValue> Error;

	// Optional instigator, resolved before anything is reported.
	AActor* Instigator = nullptr;
	if (Params->HasField(TEXT("instigatorLabel")) || Params->HasField(TEXT("instigatorPath")))
	{
		FMCPActorSelector InstigatorSelector;
		InstigatorSelector.LabelKey = TEXT("instigatorLabel");
		InstigatorSelector.PathKey = TEXT("instigatorPath");
		InstigatorSelector.Match = EMCPActorMatch::LabelNameOrPath;
		InstigatorSelector.WorldLabel = World->IsPlayInEditor() ? TEXT("PIE") : TEXT("editor");
		Instigator = MCPResolveActor(World, Params, Error, InstigatorSelector);
		if (!Instigator) return Error;
	}

	// The event location. Falls back to the instigator's own location, which
	// is what a caller reporting "this actor made a noise" means.
	FVector Location = FVector::ZeroVector;
	bool bHaveLocation = false;
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (Params->TryGetObjectField(TEXT("location"), LocationObj) && LocationObj)
	{
		bHaveLocation = ReadVec3Fields(*LocationObj, Location);
	}
	if (!bHaveLocation && Instigator)
	{
		Location = Instigator->GetActorLocation();
		bHaveLocation = true;
	}

	const FString TagString = OptionalString(Params, TEXT("tag"));
	const FName Tag = TagString.IsEmpty() ? NAME_None : FName(*TagString);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("senseType"), bHearing ? TEXT("hearing") : TEXT("damage"));
	Result->SetStringField(TEXT("world"), World->IsPlayInEditor() ? TEXT("pie") : TEXT("editor"));
	if (Instigator)
	{
		Result->SetStringField(TEXT("instigatorLabel"), Instigator->GetActorLabel());
		Result->SetStringField(TEXT("instigatorPath"), Instigator->GetPathName());
	}
	if (!TagString.IsEmpty()) Result->SetStringField(TEXT("tag"), TagString);

	UClass* ReportedSense = nullptr;

	if (bHearing)
	{
		if (!bHaveLocation)
		{
			return MCPError(TEXT(
				"Missing 'location' ({x,y,z}) and no instigator to take one from. A noise event is "
				"heard by distance, so it has to happen somewhere: pass 'location', or pass "
				"'instigatorLabel'/'instigatorPath' and the instigator's own location is used."));
		}
		const double Loudness = OptionalNumber(Params, TEXT("loudness"), 1.0);
		const double MaxRange = OptionalNumber(Params, TEXT("maxRange"), 0.0);
		if (Loudness <= 0.0)
		{
			return MCPError(FString::Printf(
				TEXT("'loudness' is %f. Loudness multiplies the audible range, so a value of zero or "
					 "less is a noise nothing can hear. Pass a positive number (1.0 is the engine default)."),
				Loudness));
		}

		UAISense_Hearing::ReportNoiseEvent(
			World, Location, static_cast<float>(Loudness), Instigator,
			static_cast<float>(MaxRange), Tag);

		ReportedSense = UAISense_Hearing::StaticClass();
		Result->SetObjectField(TEXT("location"), PerceptionLocalVectorToJson(Location));
		Result->SetNumberField(TEXT("loudness"), Loudness);
		Result->SetNumberField(TEXT("maxRange"), MaxRange);
		if (MaxRange <= 0.0)
		{
			Result->SetStringField(TEXT("maxRangeNote"), TEXT(
				"maxRange 0 means the noise itself has no range limit; each listener is still bound by "
				"its own HearingRange, scaled by loudness."));
		}
	}
	else
	{
		FMCPActorSelector TargetSelector;
		TargetSelector.LabelKey = TEXT("targetLabel");
		TargetSelector.PathKey = TEXT("targetPath");
		TargetSelector.Match = EMCPActorMatch::LabelNameOrPath;
		TargetSelector.WorldLabel = World->IsPlayInEditor() ? TEXT("PIE") : TEXT("editor");
		AActor* DamagedActor = MCPResolveActor(World, Params, Error, TargetSelector);
		if (!DamagedActor)
		{
			// The selector's own "missing parameter" message names the keys but
			// not why damage needs them, so say that here.
			if (!Params->HasField(TEXT("targetLabel")) && !Params->HasField(TEXT("targetPath")))
			{
				return MCPError(TEXT(
					"senseType \"damage\" needs 'targetLabel' or 'targetPath': a damage event is "
					"reported ABOUT the actor that took the damage, and that actor is what the AI "
					"perceives. Use senseType \"hearing\" for an event with no victim."));
			}
			return Error;
		}

		if (!Params->HasField(TEXT("amount")))
		{
			return MCPError(TEXT(
				"senseType \"damage\" needs 'amount': the damage taken. Zero-damage events are NOT "
				"ignored by the engine, so pass 0 deliberately if that is what you mean."));
		}
		const double Amount = OptionalNumber(Params, TEXT("amount"), 0.0);

		// The engine's own fallback chain: event location, then hit location,
		// then the damaged actor's location.
		if (!bHaveLocation) Location = DamagedActor->GetActorLocation();

		FVector HitLocation = FAISystem::InvalidLocation;
		const TSharedPtr<FJsonObject>* HitObj = nullptr;
		if (Params->TryGetObjectField(TEXT("hitLocation"), HitObj) && HitObj)
		{
			FVector Parsed = FVector::ZeroVector;
			if (ReadVec3Fields(*HitObj, Parsed)) HitLocation = Parsed;
		}

		UAISense_Damage::ReportDamageEvent(
			World, DamagedActor, Instigator, static_cast<float>(Amount), Location, HitLocation, Tag);

		ReportedSense = UAISense_Damage::StaticClass();
		Result->SetStringField(TEXT("targetLabel"), DamagedActor->GetActorLabel());
		Result->SetStringField(TEXT("targetPath"), DamagedActor->GetPathName());
		Result->SetNumberField(TEXT("amount"), Amount);
		Result->SetObjectField(TEXT("location"), PerceptionLocalVectorToJson(Location));
		if (HitLocation != FAISystem::InvalidLocation)
		{
			Result->SetObjectField(TEXT("hitLocation"), PerceptionLocalVectorToJson(HitLocation));
		}
	}

	// A one-shot stimulus, and the two statements that follow from that.
	//
	// There is no inverse. The event is already queued on the perception system
	// and an AI that receives it has heard it; nothing in the engine or in this
	// surface un-hears a stimulus or drains the queue behind one.
	//
	// It is also not idempotent, and `changed` is unconditionally true for that
	// reason. A replay after a timeout does not converge on a state the way a
	// property write does: reporting the same noise twice queues two events,
	// each one perceived on its own. A caller retrying this call is making a
	// second noise, not repeating the first.
	Result->SetBoolField(TEXT("changed"), true);
	MCPSetNoRollback(Result, TEXT(
		"The stimulus is already queued on the world's AIPerceptionSystem and is delivered on its "
		"next update. There is no call that un-hears a reported event or withdraws one from the "
		"queue, so a flow cannot undo this step; it can only let the perception age out."));

	// Reporting an event is fire-and-forget, so the only way to know it could
	// possibly land is to say who was listening for that sense when it went
	// out, and how far away they were.
	int32 ListenersForSense = 0;
	TArray<TSharedPtr<FJsonValue>> ListenerRows;
	for (TObjectIterator<UAIPerceptionComponent> It; It; ++It)
	{
		UAIPerceptionComponent* Listener = *It;
		if (!IsValid(Listener) || Listener->GetWorld() != World) continue;

		const UAISenseConfig* MatchingConfig = nullptr;
		for (auto ConfigIt = Listener->GetSensesConfigIterator(); ConfigIt; ++ConfigIt)
		{
			const UAISenseConfig* Config = *ConfigIt;
			if (Config && Config->GetSenseImplementation().Get() == ReportedSense)
			{
				MatchingConfig = Config;
				break;
			}
		}
		if (!MatchingConfig) continue;
		++ListenersForSense;
		if (ListenerRows.Num() >= 10) continue;

		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("componentObjectPath"), Listener->GetPathName());
		Row->SetBoolField(TEXT("senseEnabled"),
			Listener->IsSenseEnabled(MatchingConfig->GetSenseImplementation()));
		if (AActor* ListenerBody = Listener->GetMutableBodyActor())
		{
			Row->SetStringField(TEXT("actorLabel"), ListenerBody->GetActorLabel());
			Row->SetStringField(TEXT("actorPath"), ListenerBody->GetPathName());
			const double Distance = FVector::Dist(ListenerBody->GetActorLocation(), Location);
			Row->SetNumberField(TEXT("distance"), Distance);
			if (const UAISenseConfig_Hearing* HearingConfig = Cast<UAISenseConfig_Hearing>(MatchingConfig))
			{
				Row->SetNumberField(TEXT("hearingRange"), HearingConfig->HearingRange);
				Row->SetBoolField(TEXT("withinHearingRange"), Distance <= HearingConfig->HearingRange);
			}
		}
		ListenerRows.Add(MakeShared<FJsonValueObject>(Row));
	}

	Result->SetNumberField(TEXT("listenersConfiguredForSense"), ListenersForSense);
	Result->SetArrayField(TEXT("listeners"), ListenerRows);
	if (ListenersForSense == 0)
	{
		Result->SetStringField(TEXT("warning"), FString::Printf(TEXT(
			"The event was reported, but NO AIPerceptionComponent in this world has a %s sense "
			"configured, so nothing can receive it. gameplay(read_perception) reports what a component "
			"has, and gameplay(configure_ai_perception_sense) adds a sense to a Blueprint."),
			ReportedSense ? *PerceptionLocalShortSenseName(ReportedSense) : TEXT("matching")));
	}
	Result->SetStringField(TEXT("note"), TEXT(
		"The event is queued on the AIPerceptionSystem and delivered on its next update, so the world "
		"has to tick before anything perceives it. Read the outcome with "
		"gameplay(check_perception) or gameplay(get_perceived_actors)."));
	return MCPResult(Result);
}
