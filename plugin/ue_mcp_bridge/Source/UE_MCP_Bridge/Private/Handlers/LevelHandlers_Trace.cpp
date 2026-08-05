// World-space line trace + actor floor snap.
// Translation-unit partition of FLevelHandlers - registration stays in
// LevelHandlers.cpp::RegisterHandlers.

#include "LevelHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace
{
	/**
	 * Channel display names come from the project's collision settings, so a project
	 * that renamed GameTraceChannel1 to "Weapon" can be traced by that name. The
	 * built-in table is the fallback for the case where the profile config has not
	 * been loaded yet and every lookup would otherwise fail.
	 */
	static bool ResolveTraceChannel(const FString& InName, ECollisionChannel& OutChannel, FString& OutResolvedName)
	{
		FString Name = InName.TrimStartAndEnd();
		if (Name.StartsWith(TEXT("ECC_"))) Name = Name.RightChop(4);
		if (Name.IsEmpty()) return false;

		if (const UCollisionProfile* Profile = UCollisionProfile::Get())
		{
			for (int32 Index = 0; Index < ECC_MAX; ++Index)
			{
				const FName ChannelName = Profile->ReturnChannelNameFromContainerIndex(Index);
				if (ChannelName.IsNone()) continue;
				if (ChannelName.ToString().Equals(Name, ESearchCase::IgnoreCase))
				{
					OutChannel = static_cast<ECollisionChannel>(Index);
					OutResolvedName = ChannelName.ToString();
					return true;
				}
			}
		}

		struct FBuiltInChannel { const TCHAR* Name; ECollisionChannel Channel; };
		static const FBuiltInChannel BuiltIns[] = {
			{ TEXT("WorldStatic"),  ECC_WorldStatic },
			{ TEXT("WorldDynamic"), ECC_WorldDynamic },
			{ TEXT("Pawn"),         ECC_Pawn },
			{ TEXT("Visibility"),   ECC_Visibility },
			{ TEXT("Camera"),       ECC_Camera },
			{ TEXT("PhysicsBody"),  ECC_PhysicsBody },
			{ TEXT("Vehicle"),      ECC_Vehicle },
			{ TEXT("Destructible"), ECC_Destructible },
		};
		for (const FBuiltInChannel& Entry : BuiltIns)
		{
			if (Name.Equals(Entry.Name, ESearchCase::IgnoreCase))
			{
				OutChannel = Entry.Channel;
				OutResolvedName = Entry.Name;
				return true;
			}
		}
		return false;
	}

	/** Channel names this project accepts, for the "unknown channel" error. */
	static FString DescribeTraceChannels()
	{
		TArray<FString> Names;
		if (const UCollisionProfile* Profile = UCollisionProfile::Get())
		{
			for (int32 Index = 0; Index < ECC_MAX; ++Index)
			{
				const FName ChannelName = Profile->ReturnChannelNameFromContainerIndex(Index);
				if (!ChannelName.IsNone()) Names.Add(ChannelName.ToString());
			}
		}
		if (Names.Num() == 0)
		{
			Names = { TEXT("WorldStatic"), TEXT("WorldDynamic"), TEXT("Pawn"), TEXT("Visibility"),
					  TEXT("Camera"), TEXT("PhysicsBody"), TEXT("Vehicle"), TEXT("Destructible") };
		}
		return FString::Join(Names, TEXT(", "));
	}

	static void EmitHitFields(TSharedPtr<FJsonObject> Result, const FHitResult& Hit)
	{
		AActor* HitActor = Hit.GetActor();
		UPrimitiveComponent* HitComp = Hit.GetComponent();
		if (HitActor)
		{
			Result->SetStringField(TEXT("actorLabel"), HitActor->GetActorLabel());
			Result->SetStringField(TEXT("actorClass"), HitActor->GetClass()->GetName());
		}
		if (HitComp)
		{
			Result->SetStringField(TEXT("componentName"), HitComp->GetName());
			Result->SetStringField(TEXT("componentClass"), HitComp->GetClass()->GetName());
		}
		Result->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Hit.Location));
		Result->SetObjectField(TEXT("impactPoint"), MCPVec3ToJsonObject(Hit.ImpactPoint));
		Result->SetObjectField(TEXT("normal"), MCPVec3ToJsonObject(Hit.Normal));
		Result->SetObjectField(TEXT("impactNormal"), MCPVec3ToJsonObject(Hit.ImpactNormal));
		Result->SetNumberField(TEXT("distance"), Hit.Distance);
		// Only a per-triangle (complex) hit carries a face index. Emitting -1 for a
		// simple hit invites callers to read it as a real triangle.
		if (Hit.FaceIndex != INDEX_NONE) Result->SetNumberField(TEXT("faceIndex"), Hit.FaceIndex);
		if (Hit.BoneName != NAME_None) Result->SetStringField(TEXT("boneName"), Hit.BoneName.ToString());
		if (Hit.PhysMaterial.IsValid()) Result->SetStringField(TEXT("physicalMaterial"), Hit.PhysMaterial->GetPathName());
	}
}


TSharedPtr<FJsonValue> FLevelHandlers::LineTrace(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const FVector Start = OptionalVec3(Params, TEXT("start"));
	FVector End;
	if (Params->HasField(TEXT("end")))
	{
		End = OptionalVec3(Params, TEXT("end"));
	}
	else if (Params->HasField(TEXT("direction")))
	{
		FVector Dir = OptionalVec3(Params, TEXT("direction"));
		if (!Dir.Normalize())
		{
			return MCPError(TEXT("'direction' must be a non-zero vector"));
		}
		const double Distance = OptionalNumber(Params, TEXT("distance"), 200000.0);
		End = Start + Dir * Distance;
	}
	else
	{
		return MCPError(TEXT("Pass either 'end' (Vec3) or 'direction' (Vec3) + 'distance?'"));
	}

	// Gameplay traces run against simple collision unless they ask otherwise, so a
	// trace taken here to verify in-game behaviour has to do the same by default.
	// Complex geometry and its simple hull can be far apart, and a per-triangle hit
	// the running game never produces reads as a confirmed impact point.
	const bool bTraceComplex = OptionalBool(Params, TEXT("traceComplex"), false);

	// Visibility is the editor picking channel. A gameplay trace usually runs on
	// another one, and blocking differs per channel, so the channel has to be
	// selectable for the result to mean anything about the game.
	ECollisionChannel Channel = ECC_Visibility;
	FString ChannelName = TEXT("Visibility");
	const FString RequestedChannel = OptionalString(Params, TEXT("channel"));
	if (!RequestedChannel.IsEmpty() && !ResolveTraceChannel(RequestedChannel, Channel, ChannelName))
	{
		return MCPError(FString::Printf(
			TEXT("Unknown collision channel '%s'. Available channels: %s"),
			*RequestedChannel, *DescribeTraceChannels()));
	}

	FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPLineTrace), bTraceComplex);
	Query.bReturnPhysicalMaterial = true;
	Query.bReturnFaceIndex = bTraceComplex;

	const TArray<TSharedPtr<FJsonValue>>* IgnoreArr = nullptr;
	if (Params->TryGetArrayField(TEXT("ignoreActors"), IgnoreArr) && IgnoreArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *IgnoreArr)
		{
			FString Label;
			if (!V->TryGetString(Label)) continue;
			if (AActor* A = FindActorByLabel(World, Label)) Query.AddIgnoredActor(A);
		}
	}

	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Start, End, Channel, Query);

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("hit"), bHit);
	Result->SetObjectField(TEXT("start"), MCPVec3ToJsonObject(Start));
	Result->SetObjectField(TEXT("end"), MCPVec3ToJsonObject(End));
	// Report the collision semantics the result was produced under, so a caller
	// comparing against the game can see which one it got.
	Result->SetBoolField(TEXT("traceComplex"), bTraceComplex);
	Result->SetStringField(TEXT("channel"), ChannelName);
	if (bHit) EmitHitFields(Result, Hit);
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FLevelHandlers::SnapActorToFloor(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	FString ActorLabel;
	if (auto Err = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Err;

	AActor* Actor = FindActorByLabel(World, ActorLabel);
	if (!Actor) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));

	const double Offset = OptionalNumber(Params, TEXT("floorOffset"), 0.0);
	const double MaxDistance = OptionalNumber(Params, TEXT("maxDistance"), 100000.0);

	FVector Origin, Extent;
	Actor->GetActorBounds(/*bOnlyCollidingComponents*/ false, Origin, Extent);
	const FVector Top = Origin + FVector(0, 0, Extent.Z + 10.0);
	const FVector End = Top - FVector(0, 0, MaxDistance);

	FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPSnapToFloor), /*bTraceComplex*/ true);
	Query.AddIgnoredActor(Actor);

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Top, End, ECC_Visibility, Query))
	{
		return MCPError(FString::Printf(TEXT("No floor hit within %.1f cm below '%s'"), MaxDistance, *ActorLabel));
	}

	const FVector ActorLoc = Actor->GetActorLocation();
	const double BoundsBottomZ = (Origin.Z - Extent.Z);
	const double DeltaZ = (Hit.ImpactPoint.Z + Offset) - BoundsBottomZ;
	const FVector NewLoc = ActorLoc + FVector(0, 0, DeltaZ);

	const FVector PrevLoc = ActorLoc;
	Actor->Modify();
	Actor->SetActorLocation(NewLoc, /*bSweep*/ false, /*OutSweepHitResult*/ nullptr, ETeleportType::TeleportPhysics);

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetObjectField(TEXT("from"), MCPVec3ToJsonObject(PrevLoc));
	Result->SetObjectField(TEXT("to"), MCPVec3ToJsonObject(NewLoc));
	Result->SetObjectField(TEXT("impactPoint"), MCPVec3ToJsonObject(Hit.ImpactPoint));
	if (AActor* HitActor = Hit.GetActor()) Result->SetStringField(TEXT("hitActor"), HitActor->GetActorLabel());
	Result->SetNumberField(TEXT("dropDistance"), Hit.Distance);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
	Loc->SetNumberField(TEXT("x"), PrevLoc.X);
	Loc->SetNumberField(TEXT("y"), PrevLoc.Y);
	Loc->SetNumberField(TEXT("z"), PrevLoc.Z);
	Payload->SetObjectField(TEXT("location"), Loc);
	MCPSetRollback(Result, TEXT("move_actor"), Payload);
	return MCPResult(Result);
}
