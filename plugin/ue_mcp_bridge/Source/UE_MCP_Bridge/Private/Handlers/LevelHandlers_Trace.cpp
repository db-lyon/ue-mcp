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
	 * #933: the world a trace runs in, taken from the request's own `world`
	 * (editor|pie|game|auto) plus `pieInstance`, through ResolveWorldFromParams -
	 * the one resolver every other world-scoped action in this category already
	 * goes through. Written once here because all three trace actions need it and
	 * a second copy of the resolution is how the two would drift apart again.
	 *
	 * On failure OutError carries the response to return, so the caller does not
	 * restate the message and the three actions cannot disagree about it.
	 */
	static UWorld* ResolveTraceWorld(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonValue>& OutError)
	{
		const FString Scope = OptionalString(Params, TEXT("world"), TEXT("editor"));
		UWorld* World = ResolveWorldFromParams(Params, *Scope);
		if (!World)
		{
			OutError = MCPError(FString::Printf(
				TEXT("World not available for scope '%s'. world='pie' needs a running PIE session; see editor(list_pie_instances)."),
				*Scope));
		}
		return World;
	}

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

	/** Which world answered: "editor", "pie", "game" or "other". */
	static FString DescribeTracedWorld(const UWorld* World)
	{
		if (!World) return TEXT("none");
		switch (World->WorldType)
		{
			case EWorldType::Editor:        return TEXT("editor");
			case EWorldType::EditorPreview: return TEXT("editorPreview");
			case EWorldType::PIE:           return TEXT("pie");
			case EWorldType::Game:          return TEXT("game");
			default:                        return TEXT("other");
		}
	}

	static void EmitHitFields(TSharedPtr<FJsonObject> Result, const FHitResult& Hit)
	{
		AActor* HitActor = Hit.GetActor();
		UPrimitiveComponent* HitComp = Hit.GetComponent();
		if (HitActor)
		{
			Result->SetStringField(TEXT("actorLabel"), HitActor->GetActorLabel());
			// #983: a trace hit is exactly where a caller needs the precise
			// selector, since it did not choose the actor at all.
			Result->SetStringField(TEXT("actorPath"), HitActor->GetPathName());
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

	/** One line_trace. Shared by the single-item handler and bulk_line_trace. */
	static TSharedPtr<FJsonValue> ExecuteLineTrace(UWorld* World, const TSharedPtr<FJsonObject>& Params)
	{
		if (!HasParam(Params, TEXT("start")))
		{
			return MCPError(TEXT("Missing 'start' vector"));
		}
		const FVector Start = OptionalVec3(Params, TEXT("start"));
		FVector End;
		if (HasParam(Params, TEXT("end")))
		{
			End = OptionalVec3(Params, TEXT("end"));
		}
		else if (HasParam(Params, TEXT("direction")))
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
		if (TryGetArrayParam(Params, TEXT("ignoreActors"), IgnoreArr) && IgnoreArr)
		{
			for (const TSharedPtr<FJsonValue>& V : *IgnoreArr)
			{
				FString Label;
				if (!V->TryGetString(Label)) continue;
				// #983: an ignore list is the plural case. A label naming
				// three actors ignores all three, which is what the caller
				// meant and what ignoring one of them silently was not.
				TArray<AActor*> Matches;
				MCPCollectActorsByToken(World, Label, EMCPActorMatch::LabelNameOrPath, Matches);
				for (AActor* A : Matches) Query.AddIgnoredActor(A);
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
		// #933: name the world that actually answered. A caller comparing a PIE
		// trace against an editor trace has no other way to tell them apart, and
		// silently answering from the wrong one is the bug this closes.
		Result->SetStringField(TEXT("world"), DescribeTracedWorld(World));
		if (bHit) EmitHitFields(Result, Hit);
		return MCPResult(Result);
	}
}


// #933: `world` selects the world the trace runs against, the same way every
// other PIE-aware action in this category does. REQUIRE_EDITOR_WORLD ignored the
// parameter, so a trace asked for during PIE answered from the editor world and
// reported the pre-play geometry as though it were the running game's. That is
// the worst shape of wrong answer: nothing about it looks wrong.
TSharedPtr<FJsonValue> FLevelHandlers::LineTrace(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonValue> WorldError;
	UWorld* World = ResolveTraceWorld(Params, WorldError);
	if (!World) return WorldError;
	return ExecuteLineTrace(World, Params);
}


TSharedPtr<FJsonValue> FLevelHandlers::BulkLineTrace(const TSharedPtr<FJsonObject>& Params)
{
	// #933: one world for the whole batch, chosen by the top-level `world`. A
	// per-item scope would let one batch straddle two worlds and report the
	// results in one array as though they were comparable.
	TSharedPtr<FJsonValue> WorldError;
	UWorld* World = ResolveTraceWorld(Params, WorldError);
	if (!World) return WorldError;

	constexpr int32 MaxBulkLineTraces = 256;
	const TArray<TSharedPtr<FJsonValue>>* Traces = nullptr;
	if (!TryGetArrayParam(Params, TEXT("traces"), Traces) || !Traces)
	{
		return MCPError(TEXT("Missing 'traces' array"));
	}
	if (Traces->Num() == 0)
	{
		return MCPError(TEXT("'traces' must contain at least one line trace"));
	}
	if (Traces->Num() > MaxBulkLineTraces)
	{
		return MCPError(FString::Printf(
			TEXT("'traces' exceeds the maximum batch size of %d (received %d)"),
			MaxBulkLineTraces, Traces->Num()));
	}

	TArray<TSharedPtr<FJsonValue>> Results;
	Results.Reserve(Traces->Num());
	for (int32 Index = 0; Index < Traces->Num(); ++Index)
	{
		const TSharedPtr<FJsonValue>& Entry = (*Traces)[Index];
		const TSharedPtr<FJsonObject> Item = Entry.IsValid() ? Entry->AsObject() : nullptr;
		if (!Item.IsValid())
		{
			Results.Add(MCPError(FString::Printf(TEXT("traces[%d] must be an object"), Index)));
			continue;
		}
		Results.Add(ExecuteLineTrace(World, Item));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("results"), Results);
	Result->SetNumberField(TEXT("count"), Results.Num());
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FLevelHandlers::SnapActorToFloor(const TSharedPtr<FJsonObject>& Params)
{
	// #933: same defect as line_trace. The actor is looked up in this world and
	// the downward trace runs in it, so both halves have to agree on which one.
	TSharedPtr<FJsonValue> WorldError;
	UWorld* World = ResolveTraceWorld(Params, WorldError);
	if (!World) return WorldError;
	FString ActorLabel;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Err;

	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();

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
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetObjectField(TEXT("from"), MCPVec3ToJsonObject(PrevLoc));
	Result->SetObjectField(TEXT("to"), MCPVec3ToJsonObject(NewLoc));
	Result->SetObjectField(TEXT("impactPoint"), MCPVec3ToJsonObject(Hit.ImpactPoint));
	if (AActor* HitActor = Hit.GetActor()) Result->SetStringField(TEXT("hitActor"), HitActor->GetActorLabel());
	Result->SetNumberField(TEXT("dropDistance"), Hit.Distance);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorLabel"), ActorLabel);
	Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	TSharedPtr<FJsonObject> Loc = MakeShared<FJsonObject>();
	Loc->SetNumberField(TEXT("x"), PrevLoc.X);
	Loc->SetNumberField(TEXT("y"), PrevLoc.Y);
	Loc->SetNumberField(TEXT("z"), PrevLoc.Z);
	Payload->SetObjectField(TEXT("location"), Loc);
	MCPSetRollback(Result, TEXT("move_actor"), Payload);
	return MCPResult(Result);
}
