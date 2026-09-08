// Split from LevelHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FLevelHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in LevelHandlers.cpp::RegisterHandlers.

#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerPagination.h"
#include "HandlerJsonProperty.h"
#include "Misc/OutputDeviceNull.h"
#include "JsonSerializer.h"
#include "VolumeHelpers_Internal.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Volume.h"
#include "GameFramework/PainCausingVolume.h"
#include "Engine/BlockingVolume.h"
#include "Engine/TriggerVolume.h"
#include "Engine/PostProcessVolume.h"
#include "Materials/MaterialInterface.h"
#include "Sound/AudioVolume.h"
#include "Lightmass/LightmassImportanceVolume.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Engine/BrushBuilder.h"
#include "Engine/Polys.h"
#include "Model.h"
#include "Builders/CubeBuilder.h"
#include "BSPOps.h"
#include "Components/BrushComponent.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"


TSharedPtr<FJsonValue> FLevelHandlers::ListVolumes(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	FString VolumeType = OptionalString(Params, TEXT("volumeType"));

	// T3: paged, so a level whose blocking and audio volumes run to the
	// thousands is walkable rather than a single response.
	MCPPagination::FPageRequest Page;
	if (auto Err = MCPPagination::ReadPageRequest(
			Params,
			FString::Printf(TEXT("list_volumes|volumeType=%s"), *VolumeType),
			/*DefaultLimit*/ 200, /*MaxLimit*/ 5000, Page))
	{
		return Err;
	}

	TArray<MCPPagination::FPageRow> Rows;
	for (TActorIterator<AVolume> ActorIt(World); ActorIt; ++ActorIt)
	{
		AVolume* Volume = *ActorIt;
		if (!Volume) continue;

		FString ClassName = Volume->GetClass()->GetName();
		if (!VolumeType.IsEmpty() && !ClassName.Contains(VolumeType))
		{
			continue;
		}

		TSharedPtr<FJsonObject> VolumeObj = MakeShared<FJsonObject>();
		VolumeObj->SetStringField(TEXT("name"), Volume->GetName());
		VolumeObj->SetStringField(TEXT("label"), Volume->GetActorLabel());
		VolumeObj->SetStringField(TEXT("class"), ClassName);
		VolumeObj->SetStringField(TEXT("path"), Volume->GetPathName());

		FVector Location = Volume->GetActorLocation();
		TSharedPtr<FJsonObject> LocObj = MakeShared<FJsonObject>();
		LocObj->SetNumberField(TEXT("x"), Location.X);
		LocObj->SetNumberField(TEXT("y"), Location.Y);
		LocObj->SetNumberField(TEXT("z"), Location.Z);
		VolumeObj->SetObjectField(TEXT("location"), LocObj);

		// The actor path is the anchor: two volumes can share a label, and a
		// page boundary has to name exactly one of them.
		Rows.Add({ Volume->GetPathName(), MakeShared<FJsonValueObject>(VolumeObj) });
	}

	// TActorIterator walks the level's actor arrays, whose order is not a
	// contract and which a spawn or a delete reshuffles, so the rows are sorted
	// before paging. A cursor over an unordered enumeration is not resumable.
	Rows.Sort([](const MCPPagination::FPageRow& A, const MCPPagination::FPageRow& B)
		{ return A.Id < B.Id; });

	auto Result = MCPSuccess();
	MCPPagination::EmitPage(Page, Rows, TEXT("volumes"), Result);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SpawnVolume(const TSharedPtr<FJsonObject>& Params)
{
	FString VolumeType;
	if (auto Err = RequireString(Params, TEXT("volumeType"), VolumeType)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	const FString Label = OptionalString(Params, TEXT("label"));

	if (auto Existing = MCPCheckActorLabelExists(World, Label, OnConflict, TEXT("Volume")))
	{
		return Existing;
	}

	const FVector Location = OptionalVec3(Params, TEXT("location"));
	const FVector Extent = OptionalVec3(Params, TEXT("extent"), FVector(100.0, 100.0, 100.0));

	// Determine volume class
	UClass* VolumeClass = nullptr;
	if (VolumeType.Equals(TEXT("BlockingVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("blocking"), ESearchCase::IgnoreCase))
	{
		VolumeClass = ABlockingVolume::StaticClass();
	}
	else if (VolumeType.Equals(TEXT("TriggerVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("trigger"), ESearchCase::IgnoreCase))
	{
		VolumeClass = ATriggerVolume::StaticClass();
	}
	else if (VolumeType.Equals(TEXT("PostProcessVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("postprocess"), ESearchCase::IgnoreCase))
	{
		VolumeClass = APostProcessVolume::StaticClass();
	}
	else if (VolumeType.Equals(TEXT("AudioVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("audio"), ESearchCase::IgnoreCase))
	{
		VolumeClass = AAudioVolume::StaticClass();
	}
	else if (VolumeType.Equals(TEXT("LightmassImportanceVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("lightmass"), ESearchCase::IgnoreCase))
	{
		VolumeClass = ALightmassImportanceVolume::StaticClass();
	}
	else if (VolumeType.Equals(TEXT("CullDistanceVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("culldistance"), ESearchCase::IgnoreCase))
	{
		VolumeClass = FindClassByShortName(TEXT("CullDistanceVolume"));
	}
	else if (VolumeType.Equals(TEXT("NavMeshBoundsVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("navmesh"), ESearchCase::IgnoreCase))
	{
		VolumeClass = ANavMeshBoundsVolume::StaticClass();
	}
	else if (VolumeType.Equals(TEXT("PainCausingVolume"), ESearchCase::IgnoreCase) || VolumeType.Equals(TEXT("pain"), ESearchCase::IgnoreCase))
	{
		VolumeClass = APainCausingVolume::StaticClass();
	}
	else
	{
		// Try broad class lookup
		VolumeClass = FindClassByShortName(VolumeType);
	}

	if (!VolumeClass)
	{
		return MCPError(FString::Printf(TEXT("Volume class not found: %s"), *VolumeType));
	}

	FTransform VolumeTransform(FRotator::ZeroRotator, Location);
	AActor* NewVolume = World->SpawnActor<AActor>(VolumeClass, VolumeTransform);
	if (!NewVolume)
	{
		return MCPError(TEXT("Failed to spawn volume actor"));
	}

	if (!Label.IsEmpty())
	{
		NewVolume->SetActorLabel(Label);
	}

	// #238: AVolume subclasses ship with an empty UModel by default - actor
	// scale alone leaves bounds at zero, which silently breaks downstream
	// systems (PCG samplers, navmesh bounds, audio queries). Build an actual
	// cube and run FBSPOps::HandleVolumeShapeChanged to prep + re-register.
	// Non-Volume actors keep the old scale-based behavior.
	if (AVolume* Volume = Cast<AVolume>(NewVolume))
	{
		UEMCP::BuildVolumeAsCube(World, Volume, Extent);
	}
	else
	{
		NewVolume->SetActorScale3D(Extent / 100.0);
	}

	const FString FinalLabel = NewVolume->GetActorLabel();

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), FinalLabel);
	Result->SetStringField(TEXT("actorPath"), NewVolume->GetPathName());
	Result->SetStringField(TEXT("actorName"), NewVolume->GetName());
	Result->SetStringField(TEXT("volumeType"), VolumeType);

	// #238: when spawning a PCGVolume, accept and bind a graphPath so callers
	// don't have to make a follow-up set_actor_property call.
	FString GraphPath;
	if (Params->TryGetStringField(TEXT("graphPath"), GraphPath) && !GraphPath.IsEmpty())
	{
		if (UPCGComponent* PCGComp = NewVolume->FindComponentByClass<UPCGComponent>())
		{
			if (UPCGGraph* Graph = LoadObject<UPCGGraph>(nullptr, *GraphPath))
			{
				PCGComp->SetGraph(Graph);
				Result->SetStringField(TEXT("graphPath"), GraphPath);
				Result->SetStringField(TEXT("graphName"), Graph->GetName());
			}
			else
			{
				Result->SetStringField(TEXT("warning"), FString::Printf(TEXT("PCGGraph not found: %s - volume spawned without graph"), *GraphPath));
			}
		}
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), FinalLabel);
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);

	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FLevelHandlers::SetVolumeProperties(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	FMCPActorSelector ActorSel;
	ActorSel.Match = EMCPActorMatch::LabelOrName;
	TSharedPtr<FJsonValue> ActorErr;
	AActor* TargetActor = MCPResolveActor(World, Params, ActorErr, ActorSel);
	if (!TargetActor) return ActorErr;
	ActorLabel = TargetActor->GetActorLabel();

	TArray<TSharedPtr<FJsonValue>> Changes;
	TArray<TSharedPtr<FJsonValue>> Skipped;
	TSharedPtr<FJsonObject> PreviousValues = MakeShared<FJsonObject>();

	// #238: callers pass either flat (BrushExtent:{...} at top-level) or
	// wrapped ({properties:{BrushExtent:{...}}}). The TS schema documents
	// the wrapped form; the original handler only walked top-level keys
	// and silently dropped wrapped writes. Walk both.
	TArray<TPair<FString, TSharedPtr<FJsonValue>>> Pairs;
	for (auto& Pair : Params->Values)
	{
		// actorPath joins the selector keys that are never property writes (#983).
		if (Pair.Key == TEXT("actorLabel") || Pair.Key == TEXT("actorPath")
			|| Pair.Key == TEXT("action") || Pair.Key == TEXT("properties"))
			continue;
		Pairs.Emplace(FString(*Pair.Key), Pair.Value);
	}
	const TSharedPtr<FJsonObject>* PropsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropsObj) && PropsObj && (*PropsObj).IsValid())
	{
		for (auto& Pair : (*PropsObj)->Values)
		{
			Pairs.Emplace(FString(*Pair.Key), Pair.Value);
		}
	}
	for (auto& Pair : Pairs)
	{

		// #238: BrushExtent isn't a real UPROPERTY on AVolume - it's a synthetic
		// property the bridge owns. Rebuild the cube via UCubeBuilder and run
		// FBSPOps so the new geometry is actually applied (the prior path
		// silently no-op'd on FindPropertyByName).
		if (Pair.Key.Equals(TEXT("BrushExtent"), ESearchCase::IgnoreCase) || Pair.Key.Equals(TEXT("brushExtent"), ESearchCase::IgnoreCase))
		{
			AVolume* Volume = Cast<AVolume>(TargetActor);
			if (!Volume)
			{
				Skipped.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s: actor is not an AVolume"), *Pair.Key)));
				continue;
			}
			const TSharedPtr<FJsonObject>* ExtObj = nullptr;
			FVector NewExtent = FVector::ZeroVector;
			bool bGotExtent = false;
			if (Pair.Value->TryGetObject(ExtObj) && ExtObj && (*ExtObj).IsValid())
			{
				double V = 0;
				if ((*ExtObj)->TryGetNumberField(TEXT("X"), V) || (*ExtObj)->TryGetNumberField(TEXT("x"), V)) { NewExtent.X = V; bGotExtent = true; }
				if ((*ExtObj)->TryGetNumberField(TEXT("Y"), V) || (*ExtObj)->TryGetNumberField(TEXT("y"), V)) { NewExtent.Y = V; bGotExtent = true; }
				if ((*ExtObj)->TryGetNumberField(TEXT("Z"), V) || (*ExtObj)->TryGetNumberField(TEXT("z"), V)) { NewExtent.Z = V; bGotExtent = true; }
			}
			if (!bGotExtent || NewExtent.X <= 0 || NewExtent.Y <= 0 || NewExtent.Z <= 0)
			{
				Skipped.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s: expected {X,Y,Z} object with positive values"), *Pair.Key)));
				continue;
			}
			UEMCP::BuildVolumeAsCube(World, Volume, NewExtent);
			Changes.Add(MakeShared<FJsonValueString>(Pair.Key));
			continue;
		}

		// #466: dotted paths like "Settings.VignetteIntensity" descend into
		// nested structs (PostProcessVolume.Settings is the marquee case). When
		// the key writes into FPostProcessSettings, also auto-flip the matching
		// bOverride_* flag so the change actually takes effect.
		if (Pair.Key.Contains(TEXT(".")))
		{
			FString SetErr;
			TArray<FString> Parts;
			Pair.Key.ParseIntoArray(Parts, TEXT("."));
			if (MCPJsonProperty::SetDottedPropertyFromJson(TargetActor, Pair.Key, Pair.Value, SetErr))
			{
				if (Parts.Num() >= 2)
				{
					const FString& Container = Parts[0];
					const FString& Leaf = Parts.Last();
					if (!Leaf.StartsWith(TEXT("bOverride_")))
					{
						const FString OverrideKey = FString::Printf(TEXT("%s.bOverride_%s"), *Container, *Leaf);
						FString OverrideErr;
						TSharedPtr<FJsonValue> True = MakeShared<FJsonValueBoolean>(true);
						MCPJsonProperty::SetDottedPropertyFromJson(TargetActor, OverrideKey, True, OverrideErr);
					}
				}
				Changes.Add(MakeShared<FJsonValueString>(Pair.Key));
				continue;
			}
			Skipped.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s: %s"), *Pair.Key, *SetErr)));
			continue;
		}

		FProperty* Prop = TargetActor->GetClass()->FindPropertyByName(*Pair.Key);
		if (!Prop)
		{
			Skipped.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s: not a property on %s"), *Pair.Key, *TargetActor->GetClass()->GetName())));
			continue;
		}

		FString PrevStr;
		Prop->ExportText_Direct(PrevStr, Prop->ContainerPtrToValuePtr<void>(TargetActor),
			Prop->ContainerPtrToValuePtr<void>(TargetActor), TargetActor, PPF_None);

		// #466: route every value through MCPJsonProperty so JSON dicts (e.g.
		// {x,y,z,w} for FVector4) reach struct properties, not just strings.
		void* ValueAddr = Prop->ContainerPtrToValuePtr<void>(TargetActor);
		FString SetErr;
		bool bApplied = MCPJsonProperty::SetJsonOnProperty(Prop, ValueAddr, Pair.Value, SetErr);
		if (bApplied)
		{
			Changes.Add(MakeShared<FJsonValueString>(Pair.Key));
			PreviousValues->SetStringField(Pair.Key, PrevStr);
		}
		else
		{
			Skipped.Add(MakeShared<FJsonValueString>(FString::Printf(TEXT("%s: %s"), *Pair.Key, *SetErr)));
		}
	}

	auto Result = MCPSuccess();
	if (Changes.Num() > 0) MCPSetUpdated(Result); else MCPSetExisted(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), TargetActor->GetPathName());
	Result->SetArrayField(TEXT("changes"), Changes);
	if (Skipped.Num() > 0)
	{
		Result->SetArrayField(TEXT("skipped"), Skipped);
	}

	if (Changes.Num() > 0 && PreviousValues->Values.Num() > 0)
	{
		// Self-inverse for property-only changes; BrushExtent rebuild has no
		// reversible recipe (no prior extent recorded), so omit from rollback.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		for (auto& Prev : PreviousValues->Values)
		{
			Payload->SetField(Prev.Key, Prev.Value);
		}
		MCPSetRollback(Result, TEXT("set_volume_properties"), Payload);
	}

	return MCPResult(Result);
}

// #666: add a material blendable to a PostProcessVolume's WeightedBlendables.
// PostProcess materials (and MIDs) implement IBlendableInterface, so this is
// the native path for post-process material stacks (e.g. a toon/outline pass).
TSharedPtr<FJsonValue> FLevelHandlers::AddPostProcessBlendable(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;
	FString MaterialPath;
	if (auto Err = RequireStringAlt(Params, TEXT("materialPath"), TEXT("material"), MaterialPath)) return Err;

	// #983: this ran its own label loop and took the first PostProcessVolume
	// it reached, so a second volume with the same label was a coin flip over
	// which one got the blendable.
	TSharedPtr<FJsonValue> ActorErr;
	AActor* VolumeActor = MCPResolveActor(World, Params, ActorErr);
	if (!VolumeActor) return ActorErr;
	ActorLabel = VolumeActor->GetActorLabel();
	APostProcessVolume* Volume = Cast<APostProcessVolume>(VolumeActor);
	if (!Volume)
	{
		return MCPError(FString::Printf(
			TEXT("Actor '%s' is a %s, not a PostProcessVolume"), *ActorLabel, *VolumeActor->GetClass()->GetName()));
	}

	UMaterialInterface* Material = LoadAssetByPath<UMaterialInterface>(MaterialPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));

	const float Weight = (float)OptionalNumber(Params, TEXT("weight"), 1.0);

	// AddOrUpdateBlendable either appends an entry or reweights one that is
	// already there, and the two invert differently, so which one happened has
	// to be established first.
	bool bAlreadyPresent = false;
	float PreviousWeight = 0.0f;
	for (const FWeightedBlendable& Existing : Volume->Settings.WeightedBlendables.Array)
	{
		if (Existing.Object == static_cast<UObject*>(Material))
		{
			bAlreadyPresent = true;
			PreviousWeight = Existing.Weight;
			break;
		}
	}

	// An append has an inverse too: WeightedBlendables is a reflected field of
	// FPostProcessSettings, which set_post_process_settings writes like any
	// other, so the array as it stands right now IS the value that undoes the
	// append. Captured as export text before the write, which is the form that
	// action imports.
	//
	// But the replay path for that text is a SINGLE engine ImportText_Direct:
	// SetJsonOnProperty skips the JSON re-parse for a string starting with '(',
	// the struct branch wants an object and falls through, and
	// ImportTextIntoProperty sees ContainsMap == false for FWeightedBlendables
	// and drops to ImportTextRaw. None of the per-element splitting, resize
	// guarding or count verification the repo added for exactly this hazard is
	// in play. So the round trip is not asserted here, it is MEASURED: the text
	// is imported into a scratch FWeightedBlendables and compared against a
	// copy of the array taken before the write. A rollback is emitted only when
	// that comparison holds, and the result says which way it went.
	FString PreviousBlendables;
	bool bBlendablesRoundTrip = false;
	FString RoundTripFailure;
	const FWeightedBlendables OriginalBlendables = Volume->Settings.WeightedBlendables;
	const FProperty* BlendablesProp =
		FPostProcessSettings::StaticStruct()->FindPropertyByName(TEXT("WeightedBlendables"));
	if (!BlendablesProp)
	{
		RoundTripFailure = TEXT("FPostProcessSettings does not expose a WeightedBlendables property on this engine, so the array could not be read back at all.");
	}
	else
	{
		// Exported with NO parent, for the same reason the probe imports with
		// none: the parent scopes how object references are written, and text
		// captured relative to this volume is text the ownerless replay may not
		// resolve. Both sides of the round trip now stand in exactly the
		// conditions the restore will run in.
		BlendablesProp->ExportTextItem_Direct(
			PreviousBlendables,
			BlendablesProp->ContainerPtrToValuePtr<void>(&Volume->Settings),
			nullptr, nullptr, PPF_None);

		// Proof, not assertion. The scratch copy is what the inverse would
		// produce; Identical against the pre-write array is whether it is the
		// same data. This also settles the empty-array case, where the exported
		// literal is ambiguous, without having to reason about it.
		FWeightedBlendables Scratch;
		// The owner is nullptr, matching the replay exactly. The real restore
		// reaches ImportText_Direct through ImportTextRaw with no owner, and a
		// blendable outered to this volume or its level (a material instance
		// created here, say) can resolve against Volume and fail against
		// nullptr. Probing with Volume would pass where the replay fails, which
		// is the one direction rollbackVerified must never claim.
		//
		// Silenced for the same reason ImportTextRaw silences it: the fifth
		// parameter defaults to GWarn, and a probe that is expected to fail
		// sometimes must not spray the editor log on every call.
		FOutputDeviceNull Silent;
		const TCHAR* Consumed = BlendablesProp->ImportText_Direct(
			*PreviousBlendables, &Scratch, nullptr, PPF_None, &Silent);
		if (Consumed == nullptr)
		{
			RoundTripFailure = FString::Printf(
				TEXT("re-importing the captured array text failed outright (text was '%s')"), *PreviousBlendables);
		}
		else if (!BlendablesProp->Identical(&Scratch, &OriginalBlendables, PPF_None))
		{
			// Identical walks count, Weight and Object, so an equal count still
			// fails on a drifted weight or an unresolved blendable. Saying
			// "produced 2 entries against the 2 that were there" would read as
			// nonsense, so the count and content cases are reported apart.
			RoundTripFailure = Scratch.Array.Num() != OriginalBlendables.Array.Num()
				? FString::Printf(
					TEXT("re-importing the captured array text produced %d entries against the %d that were there, so the text does not reproduce the array"),
					Scratch.Array.Num(), OriginalBlendables.Array.Num())
				: FString::Printf(
					TEXT("re-importing the captured array text produced the right number of entries (%d) but not the same ones: a blendable weight did not survive the export text's precision, or a blendable object did not resolve without an owner, which is how the restore would run"),
					Scratch.Array.Num());
		}
		else
		{
			bBlendablesRoundTrip = true;
		}
	}

	Volume->Modify();
	Volume->AddOrUpdateBlendable(Material, Weight);
	Volume->PostEditChange();
	Volume->MarkPackageDirty();

	auto Result = MCPSuccess();
	const bool bBlendableChanged = !bAlreadyPresent || PreviousWeight != Weight;
	if (bAlreadyPresent) MCPSetExisted(Result); else MCPSetCreated(Result);
	// `updated` was emitted unconditionally before this handler learned to tell
	// an append from a reweight. It stays present in BOTH branches so a
	// consumer branching on it reads a bool rather than undefined.
	Result->SetBoolField(TEXT("updated"), bBlendableChanged);
	Result->SetBoolField(TEXT("unchanged"), !bBlendableChanged);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Volume->GetPathName());
	Result->SetStringField(TEXT("material"), Material->GetPathName());
	Result->SetNumberField(TEXT("weight"), Weight);
	if (bAlreadyPresent) Result->SetNumberField(TEXT("previousWeight"), PreviousWeight);
	Result->SetNumberField(TEXT("blendableCount"), Volume->Settings.WeightedBlendables.Array.Num());

	if (bAlreadyPresent && PreviousWeight != Weight)
	{
		// The blendable was already on the volume, so this was a reweight and
		// the same call with the old weight undoes it exactly, in place, with
		// no effect on any other entry in the array.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Volume->GetPathName());
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("materialPath"), Material->GetPathName());
		Payload->SetNumberField(TEXT("weight"), PreviousWeight);
		MCPSetRollback(Result, TEXT("add_post_process_blendable"), Payload);
	}
	else if (!bAlreadyPresent && bBlendablesRoundTrip)
	{
		// The append is undone by restoring the array to what it was, through
		// the general post-process setter. WeightedBlendables carries no
		// bOverride_ bit, so enableOverrides is off and nothing else is
		// touched. propertyName pins the struct: APostProcessVolume declares
		// its FPostProcessSettings as Settings.
		//
		// Reached only when the import-and-compare above actually held.
		TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
		Settings->SetStringField(TEXT("WeightedBlendables"), PreviousBlendables);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("actorPath"), Volume->GetPathName());
		Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
		Payload->SetStringField(TEXT("propertyName"), TEXT("Settings"));
		Payload->SetObjectField(TEXT("settings"), Settings);
		Payload->SetBoolField(TEXT("enableOverrides"), false);
		MCPSetRollback(Result, TEXT("set_post_process_settings"), Payload);
		Result->SetBoolField(TEXT("rollbackVerified"), true);
		Result->SetBoolField(TEXT("rollbackLossy"), true);
		Result->SetStringField(TEXT("rollbackNote"),
			TEXT("The captured array text was re-imported into a scratch copy and compared against the array as it stood before the write, and it matched, so this inverse reproduces the entries it names. It restores the WHOLE WeightedBlendables array, so it also reverts any other change made to that array in between rather than removing just this entry. The restore goes through one engine text import rather than the per-element path used for maps, so if a blendable asset has been deleted by the time it replays, the import fails and the whole restore reports applied:false rather than putting back the entries that still resolve."));
	}
	else if (!bAlreadyPresent)
	{
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetBoolField(TEXT("rollbackVerified"), false);
		Result->SetStringField(TEXT("rollbackNote"), FString::Printf(
			TEXT("The blendable was appended, and the only inverse available is rewriting the whole WeightedBlendables array through set_post_process_settings. That was tested against this volume's own data before being offered and it does not hold: %s. Restoring from it would write an array that is not the one that was here, so no inverse is named."),
			*RoundTripFailure));
	}
	return MCPResult(Result);
}
