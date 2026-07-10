// Split from LevelHandlers.cpp to keep that file under 3k lines.
// All functions below are still members of FLevelHandlers - this file is a
// translation-unit partition, not a new class. Handler registration
// stays in LevelHandlers.cpp::RegisterHandlers.

#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
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

	TArray<TSharedPtr<FJsonValue>> VolumesArray;
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

		VolumesArray.Add(MakeShared<FJsonValueObject>(VolumeObj));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("volumes"), VolumesArray);
	Result->SetNumberField(TEXT("count"), VolumesArray.Num());

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
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	REQUIRE_EDITOR_WORLD(World);

	AActor* TargetActor = FindActorByLabelOrName(World, ActorLabel);
	if (!TargetActor)
	{
		return MCPError(FString::Printf(TEXT("Volume not found: %s"), *ActorLabel));
	}

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
		if (Pair.Key == TEXT("actorLabel") || Pair.Key == TEXT("action") || Pair.Key == TEXT("properties"))
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
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;
	FString MaterialPath;
	if (auto Err = RequireStringAlt(Params, TEXT("materialPath"), TEXT("material"), MaterialPath)) return Err;

	APostProcessVolume* Volume = nullptr;
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		if (It->GetActorLabel() == ActorLabel) { Volume = *It; break; }
	}
	if (!Volume) return MCPError(FString::Printf(TEXT("PostProcessVolume not found: %s"), *ActorLabel));

	UMaterialInterface* Material = LoadAssetByPath<UMaterialInterface>(MaterialPath);
	if (!Material) return MCPError(FString::Printf(TEXT("Material not found: %s"), *MaterialPath));

	const float Weight = (float)OptionalNumber(Params, TEXT("weight"), 1.0);
	Volume->Modify();
	Volume->AddOrUpdateBlendable(Material, Weight);
	Volume->PostEditChange();
	Volume->MarkPackageDirty();

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("material"), Material->GetPathName());
	Result->SetNumberField(TEXT("weight"), Weight);
	Result->SetNumberField(TEXT("blendableCount"), Volume->Settings.WeightedBlendables.Array.Num());
	return MCPResult(Result);
}
