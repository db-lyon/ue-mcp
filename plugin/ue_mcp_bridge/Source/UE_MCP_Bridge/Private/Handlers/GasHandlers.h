#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Templates/Function.h"

// FGameplayAbilitySpec is named by a declaration inside namespace MCPGas below.
// The real type has to be visible BEFORE that namespace opens, otherwise an
// elaborated-type-specifier there ("const struct FGameplayAbilitySpec&")
// declares a brand new MCPGas::FGameplayAbilitySpec that shadows the engine
// type for every use inside the namespace, in this header and in every .cpp
// that includes it first.
#include "GameplayAbilitySpec.h"

class AActor;
class UAbilitySystemComponent;
class UClass;

/**
 * Attribute set registration on a live AbilitySystemComponent (#956).
 *
 * Declared here rather than kept file-local so the automation tests can pin
 * the one invariant the live-attribute actions stand on, and so there is
 * exactly one copy: the module is a unity build and a second file-local copy
 * of a helper is a redefinition on some grouping.
 */
namespace MCPGas
{

/**
 * Register the actor's own UAttributeSet subobjects on its ASC, the way
 * UAbilitySystemComponent::InitializeComponent does at BeginPlay.
 *
 * This exists because an actor spawned into the pure editor world never runs
 * that scan: there is no BeginPlay and no InitializeComponent in a world that
 * has not begun play. The ASC therefore has ZERO registered sets while the
 * actor plainly owns one, and the obvious fallback at that point ("the ASC has
 * none, so construct one") produces a SECOND, disconnected instance that no
 * gameplay code will ever consult. Adopting the actor's existing subobject is
 * the only answer that leaves ASC->GetSet<T>() pointing at the real one.
 *
 * Nested subobjects are included, unlike the engine's own scan, so a set
 * created as a subobject of the ASC rather than of the actor is found too.
 * Idempotent. Returns how many were newly registered.
 */
int32 AdoptOwnerAttributeSets(
	UAbilitySystemComponent* ASC,
	AActor* Actor,
	TArray<FString>& OutAdoptedClassNames);

/**
 * The FGameplayAttributeData property named on one attribute set class.
 * Accepts a bare property name ("Mana") and the qualified spellings
 * ("MySet.Mana" / "MySet:Mana") the other GAS actions take. Null on a miss.
 */
FStructProperty* FindAttributeDataProperty(UClass* SetClass, const FString& Name);

/** Every FGameplayAttributeData property name on a set class, comma separated. */
FString ListAttributeDataPropertyNames(UClass* SetClass);

/**
 * Actor + AbilitySystemComponent resolution for a runtime GAS call.
 *
 * Defined once, in GasHandlers_Runtime.cpp, and declared here rather than
 * copied: the module is a unity build, so a second file-local copy of this is
 * a redefinition on some grouping, and the grouping shifts with file count and
 * file order. Reads actorLabel / actorPath / world / pieInstance out of Params
 * exactly as the runtime actions already document them, and writes a
 * structured error to OutError on any failure.
 */
UAbilitySystemComponent* ResolveActorASC(
	const TSharedPtr<FJsonObject>& Params,
	AActor*& OutActor,
	TSharedPtr<FJsonValue>& OutError);

/** Resolve a UGameplayAbility subclass from a Blueprint path, a generated
 *  class path or a native class name. Error names the accepted spellings. */
UClass* ResolveGameplayAbilityClass(const FString& Spec, TSharedPtr<FJsonValue>& OutError);

/** Resolve any class deriving from Base from a content path or short name. */
UClass* ResolveClassDerivingFrom(const FString& Spec, UClass* Base);

/** One granted ability spec as JSON: handle, class, level, inputID, active. */
TSharedPtr<FJsonObject> DescribeAbilitySpec(const FGameplayAbilitySpec& Spec);

}

class FGasHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

private:
	// Shared flow used by every GAS "create blueprint by parent class" handler.
	// Requires `name`, reads `packagePath` / `onConflict`, runs the existence
	// check + asset-tools create + compile + save + rollback record, and
	// invokes ExtraResultFields (if provided) to stamp handler-specific fields
	// onto the result before returning.
	static TSharedPtr<FJsonValue> CreateGasBlueprint(
		const TSharedPtr<FJsonObject>& Params,
		const FString& DefaultPackagePath,
		class UClass* ParentClass,
		const FString& FriendlyType,
		TFunction<void(TSharedPtr<FJsonObject>&)> ExtraResultFields = nullptr);

	static TSharedPtr<FJsonValue> CreateGameplayEffect(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetGasInfo(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateGameplayAbility(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateAttributeSet(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CreateGameplayCue(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddAbilitySystemComponent(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddAttribute(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetAbilityTags(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetEffectModifier(const TSharedPtr<FJsonObject>& Params);

	// Wire an AttributeSet (with optional init DataTable) onto a Blueprint's ASC
	// component template via DefaultStartingData. Authoring; in GasHandlers.cpp.
	static TSharedPtr<FJsonValue> SetAscDefaults(const TSharedPtr<FJsonObject>& Params);

	// Runtime GAS control (operates on a live actor's AbilitySystemComponent,
	// PIE by default). Implemented in GasHandlers_Runtime.cpp.
	static TSharedPtr<FJsonValue> ApplyEffect(const TSharedPtr<FJsonObject>& Params);
	// The inverse of ApplyEffect: takes an active GameplayEffect back off the
	// ASC by the handle that call returned, or by effect class.
	static TSharedPtr<FJsonValue> RemoveEffect(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetAttribute(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetAttribute(const TSharedPtr<FJsonObject>& Params);
	// InitAbilityActorInfo + optionally GetOrCreateAttributeSubobject on a live
	// actor, so a bridge-authored GAS actor has live attributes to test against.
	static TSharedPtr<FJsonValue> InitAsc(const TSharedPtr<FJsonObject>& Params);
	// #587: introspect a live ASC - granted ability specs (class, level, input,
	// active, dynamic tags) + owned gameplay tags.
	static TSharedPtr<FJsonValue> GetAscState(const TSharedPtr<FJsonObject>& Params);

	// #956: read and write the CURRENT value of one FGameplayAttributeData on
	// the attribute set instance actually REGISTERED on a live actor's ASC.
	// get_attribute / set_attribute above only see sets the ASC already knows
	// about, and an actor spawned into the editor world has none, because the
	// DSO scan that registers them runs in InitializeComponent and a world that
	// has not begun play never gets there. These two name the set explicitly
	// and reach the registered instance, adopting the actor's own subobject
	// when the ASC has not registered it yet. Implemented in
	// GasHandlers_Runtime.cpp.
	static TSharedPtr<FJsonValue> GetLiveAttributeValue(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SetLiveAttributeValue(const TSharedPtr<FJsonObject>& Params);

	// Granting and diagnosis. An ability the ASC has never been given cannot
	// activate, and when activation fails GAS reports it only to the log, in a
	// form nothing can read back. These are deliberately not property writes:
	// an ability's CONFIGURATION is reachable through asset(set_property) on
	// its Blueprint CDO, but nothing there can call GiveAbility, read the
	// active effect container, or ask an ability whether it would activate and
	// why not. Implemented in GasHandlers_Runtime.cpp.
	static TSharedPtr<FJsonValue> GrantAbility(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RevokeAbility(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetActiveEffects(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> TraceAbilityActivation(const TSharedPtr<FJsonObject>& Params);

	// ── T4 remainder: input, cues, attribute diagnosis ──────────────────
	// Implemented in GasHandlers_Abilities.cpp.
	//
	// Input binding is a live-spec write plus MarkAbilitySpecDirty, which is
	// runtime state no property write reaches: FGameplayAbilitySpec lives in a
	// fast-array serialiser on a live component, not on any asset.
	// Cue linking is a CDO array write that a property write COULD make, and it
	// earns a handler anyway because the tag has to resolve through the tag
	// manager, has to sit under the GameplayCue root to be routed at all, and is
	// worth nothing if no notify answers it. The handler checks all three.
	// The attribute audit walks a set class and reports what is provable about
	// clamping and replication; it writes nothing.
	static TSharedPtr<FJsonValue> BindAbilityInput(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ClearAbilityInput(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> SendAbilityInput(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AddEffectCue(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RemoveEffectCue(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ValidateCueCoverage(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> AuditAttributeSet(const TSharedPtr<FJsonObject>& Params);

	// ── T5 remainder: snapshot and diff ─────────────────────────────────
	// Implemented in GasHandlers_Snapshot.cpp. A snapshot is the whole
	// ability-system state of one actor at one instant; the diff names each
	// change rather than handing back two blobs for the caller to compare.
	static TSharedPtr<FJsonValue> CaptureGasState(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> CompareGasStates(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListGasSnapshots(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> DeleteGasSnapshot(const TSharedPtr<FJsonObject>& Params);
};
