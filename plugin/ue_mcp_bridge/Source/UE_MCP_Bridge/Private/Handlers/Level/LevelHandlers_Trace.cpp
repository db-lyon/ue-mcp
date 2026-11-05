// World-space line and shape traces, box region queries, shared collision inspection, and floor snap.
// Translation-unit partition of FLevelHandlers - registration stays in
// LevelHandlers.cpp::RegisterHandlers.

#include "LevelHandlers.h"
#include "LevelTrace.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Components/PrimitiveComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

namespace MCPTrace
{
	bool MakeShape(const FString& Shape, double Radius, FVector HalfExtent, double HalfHeight,
		FCollisionShape& OutShape, FString& OutError)
	{
		const bool bRadiusValid = FMath::IsFinite(Radius) && Radius > 0 && Radius <= MAX_flt;
		if (Shape == TEXT("sphere") && bRadiusValid)
			OutShape = FCollisionShape::MakeSphere(static_cast<float>(Radius));
		else if (Shape == TEXT("box") && !HalfExtent.ContainsNaN() &&
			HalfExtent.X > 0 && HalfExtent.Y > 0 && HalfExtent.Z > 0 && HalfExtent.GetMax() <= MAX_flt)
			OutShape = FCollisionShape::MakeBox(HalfExtent);
		else if (Shape == TEXT("capsule") && bRadiusValid && FMath::IsFinite(HalfHeight) &&
			HalfHeight >= Radius && HalfHeight <= MAX_flt)
			OutShape = FCollisionShape::MakeCapsule(static_cast<float>(Radius), static_cast<float>(HalfHeight));
		else
		{
			OutError = TEXT("shape must be sphere (positive radius), box (positive halfExtent xyz), or capsule (positive radius and halfHeight >= radius); dimensions must be finite");
			return false;
		}
		return true;
	}

	static FString ChannelLabel(int32 Channel)
	{
		const UCollisionProfile* Profile = UCollisionProfile::Get();
		return Profile ? Profile->ReturnChannelNameFromContainerIndex(Channel).ToString() : FString::FromInt(Channel);
	}

	static const TCHAR* ResponseName(ECollisionResponse Response)
	{
		switch (Response)
		{
			case ECR_Ignore: return TEXT("Ignore");
			case ECR_Overlap: return TEXT("Overlap");
			case ECR_Block: return TEXT("Block");
			default: return TEXT("Unknown");
		}
	}

	static const TCHAR* EnabledName(ECollisionEnabled::Type Enabled)
	{
		switch (Enabled)
		{
			case ECollisionEnabled::NoCollision: return TEXT("NoCollision");
			case ECollisionEnabled::QueryOnly: return TEXT("QueryOnly");
			case ECollisionEnabled::PhysicsOnly: return TEXT("PhysicsOnly");
			case ECollisionEnabled::QueryAndPhysics: return TEXT("QueryAndPhysics");
			case ECollisionEnabled::ProbeOnly: return TEXT("ProbeOnly");
			case ECollisionEnabled::QueryAndProbe: return TEXT("QueryAndProbe");
			default: return TEXT("Unknown");
		}
	}

	void EmitCollision(const TSharedPtr<FJsonObject>& Result, const UPrimitiveComponent* Component, int32 Channel)
	{
		if (!Component)
		{
			for (const TCHAR* Key : { TEXT("collisionEnabled"), TEXT("objectType"), TEXT("responseOnChannel"), TEXT("blocksVisibility") })
				Result->SetField(Key, MakeShared<FJsonValueNull>());
			return;
		}
		Result->SetStringField(TEXT("collisionEnabled"), EnabledName(Component->GetCollisionEnabled()));
		Result->SetStringField(TEXT("objectType"), ChannelLabel(Component->GetCollisionObjectType()));
		Result->SetBoolField(TEXT("blocksVisibility"), Component->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block);
		if (AActor* Owner = Component->GetOwner())
		{
			Result->SetStringField(TEXT("ownerActorLabel"), Owner->GetActorLabel());
			Result->SetStringField(TEXT("ownerActorClass"), Owner->GetClass()->GetName());
		}
		if (Channel != INDEX_NONE)
		{
			Result->SetStringField(TEXT("collisionChannel"), ChannelLabel(Channel));
			Result->SetStringField(TEXT("responseOnChannel"), ResponseName(
				Component->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Channel))));
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> Responses;
			int32 Total = 0;
			for (int32 Index = 0; Index < ECC_OverlapAll_Deprecated; ++Index)
			{
				const FString Name = ChannelLabel(Index);
				if (Name.IsEmpty() || Name == TEXT("None")) continue;
				const ECollisionResponse Response = Component->GetCollisionResponseToChannel(static_cast<ECollisionChannel>(Index));
				if (Response == ECR_Block) continue;
				++Total;
				if (Responses.Num() >= 32) continue;
				auto Row = MakeShared<FJsonObject>();
				Row->SetStringField(TEXT("channel"), Name);
				Row->SetStringField(TEXT("response"), ResponseName(Response));
				Responses.Add(MakeShared<FJsonValueObject>(Row));
			}
			Result->SetStringField(TEXT("defaultResponse"), TEXT("Block"));
			Result->SetArrayField(TEXT("responses"), Responses);
			Result->SetBoolField(TEXT("responsesTruncated"), Total > 32);
		}
	}

	/**
	 * #933: the world a trace runs in, taken from the request's own `world`
	 * (editor|pie|game|auto) plus `pieInstance`, through ResolveWorldFromParams -
	 * the one resolver every other world-scoped action in this category already
	 * goes through. Written once here because the trace actions need it and
	 * a second copy of the resolution is how the two would drift apart again.
	 *
	 * On failure OutError carries the response to return, so the caller does not
	 * restate the message and the actions cannot disagree about it.
	 */
	UWorld* ResolveTraceWorld(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonValue>& OutError)
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

	/** Shared query implementation for line, shape, bulk and live view traces. */
	TSharedPtr<FJsonValue> Execute(UWorld* World, const TSharedPtr<FJsonObject>& Params,
		bool bShape, const AActor* IgnoredActor, bool bCollision)
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
			if (bShape && (!FMath::IsFinite(Distance) || Distance <= 0.0))
				return MCPError(TEXT("'distance' must be finite and positive"));
			End = Start + Dir * Distance;
		}
		else
		{
			return MCPError(TEXT("Pass either 'end' (Vec3) or 'direction' (Vec3) + 'distance?'"));
		}

		if (Start.ContainsNaN() || End.ContainsNaN())
			return MCPError(TEXT("Trace vectors must be finite"));
		FCollisionShape Shape;
		if (bShape)
		{
			FString Error;
			if (!MakeShape(OptionalString(Params, TEXT("shape")), OptionalNumber(Params, TEXT("radius"), 0),
				OptionalVec3(Params, TEXT("halfExtent")), OptionalNumber(Params, TEXT("halfHeight"), 0), Shape, Error))
				return MCPError(Error);
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
		if (!RequestedChannel.IsEmpty() && !MCPResolveCollisionChannel(RequestedChannel, Channel, ChannelName))
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

		if (IgnoredActor) Query.AddIgnoredActor(IgnoredActor);
		FHitResult Hit;
		TArray<FHitResult> Hits;
		// #1564: multi returns every overlap up to and including the first blocker,
		// for lines as well as sweeps. hit stays the engine's blocking-hit result.
		const bool bMulti = OptionalBool(Params, TEXT("multi"), false);
		bool bHit = false;
		if (bShape)
			bHit = bMulti ? World->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, Channel, Shape, Query)
				: World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, Channel, Shape, Query);
		else
			bHit = bMulti ? World->LineTraceMultiByChannel(Hits, Start, End, Channel, Query)
				: World->LineTraceSingleByChannel(Hit, Start, End, Channel, Query);

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
		if (bMulti)
		{
			TArray<TSharedPtr<FJsonValue>> Rows;
			for (int32 Index = 0; Index < FMath::Min(Hits.Num(), 64); ++Index)
			{
				auto Row = MakeShared<FJsonObject>();
				Row->SetBoolField(TEXT("blockingHit"), Hits[Index].bBlockingHit);
				Row->SetBoolField(TEXT("bStartPenetrating"), Hits[Index].bStartPenetrating);
				EmitHitFields(Row, Hits[Index]);
				if (bCollision) EmitCollision(Row, Hits[Index].GetComponent(), Channel);
				Rows.Add(MakeShared<FJsonValueObject>(Row));
			}
			Result->SetArrayField(TEXT("hits"), Rows);
			Result->SetNumberField(TEXT("totalHits"), Hits.Num());
			Result->SetBoolField(TEXT("truncated"), Hits.Num() > 64);
		}
		else if (bHit)
		{
			EmitHitFields(Result, Hit);
			if (bShape) Result->SetBoolField(TEXT("bStartPenetrating"), Hit.bStartPenetrating);
			if (bCollision) EmitCollision(Result, Hit.GetComponent(), Channel);
		}
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
	MCPReadParamsAhead(Params, {
		TEXT("start"), TEXT("end"), TEXT("direction"), TEXT("distance"), TEXT("traceComplex"), TEXT("channel"),
		TEXT("ignoreActors"), TEXT("world"), TEXT("pieInstance"), TEXT("multi"),
	});

	TSharedPtr<FJsonValue> WorldError;
	UWorld* World = MCPTrace::ResolveTraceWorld(Params, WorldError);
	if (!World) return WorldError;
	return MCPTrace::Execute(World, Params);
}


TSharedPtr<FJsonValue> FLevelHandlers::BulkLineTrace(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("traces"), TEXT("world"), TEXT("pieInstance") });

	// #933: one world for the whole batch, chosen by the top-level `world`. A
	// per-item scope would let one batch straddle two worlds and report the
	// results in one array as though they were comparable.
	TSharedPtr<FJsonValue> WorldError;
	UWorld* World = MCPTrace::ResolveTraceWorld(Params, WorldError);
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
		Results.Add(MCPTrace::Execute(World, Item));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("results"), Results);
	Result->SetNumberField(TEXT("count"), Results.Num());
	return MCPResult(Result);
}


TSharedPtr<FJsonValue> FLevelHandlers::SnapActorToFloor(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("floorOffset"), TEXT("maxDistance"), TEXT("world"),
		TEXT("pieInstance"),
	});

	// #933: same defect as line_trace. The actor is looked up in this world and
	// the downward trace runs in it, so both halves have to agree on which one.
	TSharedPtr<FJsonValue> WorldError;
	UWorld* World = MCPTrace::ResolveTraceWorld(Params, WorldError);
	if (!World) return WorldError;
	FString ActorLabel;

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

	// A World Partition HLOD actor stands in for cells that are not loaded. Its
	// mesh is a simplified copy that sits above the real ground, so landing on it
	// leaves the actor floating once the cell streams in. Skip each one and trace
	// again, and say which were skipped.
	constexpr int32 MaxStandIns = 16;
	TArray<TSharedPtr<FJsonValue>> SkippedHlod;
	FHitResult Hit;
	bool bHit = false;
	for (int32 Attempt = 0; Attempt <= MaxStandIns; ++Attempt)
	{
		bHit = World->LineTraceSingleByChannel(Hit, Top, End, ECC_Visibility, Query);
		AActor* HitActor = bHit ? Hit.GetActor() : nullptr;
		if (!HitActor || !HitActor->IsA<AWorldPartitionHLOD>()) break;
		SkippedHlod.Add(MakeShared<FJsonValueString>(HitActor->GetPathName()));
		Query.AddIgnoredActor(HitActor);
		bHit = false;
	}
	if (!bHit)
	{
		if (SkippedHlod.Num() > 0)
		{
			return MCPError(FString::Printf(
				TEXT("Only World Partition HLOD stand-ins lie within %.1f cm below '%s', so the real ground there is in a cell that is not loaded. ")
				TEXT("Snapping onto an HLOD mesh would leave the actor floating once the cell streams in. Load the cell with level(load_actor_descs) and snap again."),
				MaxDistance, *ActorLabel));
		}
		return MCPError(FString::Printf(TEXT("No floor hit within %.1f cm below '%s'"), MaxDistance, *ActorLabel));
	}

	const FVector ActorLoc = Actor->GetActorLocation();
	const double BoundsBottomZ = (Origin.Z - Extent.Z);
	const double DeltaZ = (Hit.ImpactPoint.Z + Offset) - BoundsBottomZ;
	const FVector NewLoc = ActorLoc + FVector(0, 0, DeltaZ);

	// The same as move_actor: an editor snap is undoable and dirties the actor's package.
	const FVector PrevLoc = ActorLoc;
	const bool bEditorWorld = !World->IsGameWorld();
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Snap Actor To Floor")), bEditorWorld);
		Actor->Modify();
		Actor->SetActorLocation(NewLoc, /*bSweep*/ false, /*OutSweepHitResult*/ nullptr, ETeleportType::TeleportPhysics);
		if (bEditorWorld) Actor->MarkPackageDirty();
	}

	auto Result = MCPSuccess();
	MCPSetUpdated(Result);
	if (bEditorWorld)
	{
		UPackage* ActorPackage = Actor->GetPackage();
		Result->SetBoolField(TEXT("packageDirty"), ActorPackage && ActorPackage->IsDirty());
		if (ActorPackage) Result->SetStringField(TEXT("package"), ActorPackage->GetName());
	}
	if (SkippedHlod.Num() > 0)
	{
		Result->SetArrayField(TEXT("skippedHlodActors"), SkippedHlod);
	}
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
	TSharedPtr<FJsonObject> Loc = MCPVec3ToJsonObject(PrevLoc);
	Payload->SetObjectField(TEXT("location"), Loc);
	MCPSetRollback(Result, TEXT("move_actor"), Payload);

	// Last, as in move_actor: OnActorMoved listeners (PCG, splines) see the move.
	if (bEditorWorld)
	{
		Actor->PostEditMove(true);
		MCPNotifyEditorMove(Actor);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::ShapeTrace(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("shape"), TEXT("radius"), TEXT("halfExtent"), TEXT("halfHeight"),
		TEXT("start"), TEXT("end"), TEXT("direction"), TEXT("distance"), TEXT("traceComplex"), TEXT("channel"),
		TEXT("ignoreActors"), TEXT("world"), TEXT("pieInstance"), TEXT("multi") });
	TSharedPtr<FJsonValue> Error;
	UWorld* World = MCPTrace::ResolveTraceWorld(Params, Error);
	return World ? MCPTrace::Execute(World, Params, true) : Error;
}

// #1564: every actor whose bounds meet a world-space box. Bounds are computed
// the way get_actor_bounds computes them, so the two reads agree on an actor.
TSharedPtr<FJsonValue> FLevelHandlers::ActorsInBox(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("min"), TEXT("max"), TEXT("mode"), TEXT("includeHidden"),
		TEXT("classFilter"), TEXT("onlyColliding"), TEXT("limit"), TEXT("world"), TEXT("pieInstance") });

	if (!HasParam(Params, TEXT("min")) || !HasParam(Params, TEXT("max")))
		return MCPError(TEXT("Pass both 'min' and 'max' as world-space vectors"));
	const FVector Min = OptionalVec3(Params, TEXT("min"));
	const FVector Max = OptionalVec3(Params, TEXT("max"));
	if (Min.ContainsNaN() || Max.ContainsNaN())
		return MCPError(TEXT("'min' and 'max' must be finite"));
	if (Min.X > Max.X || Min.Y > Max.Y || Min.Z > Max.Z)
		return MCPError(TEXT("'max' must be at least 'min' on every axis"));
	const FBox Region(Min, Max);

	const FString Mode = OptionalString(Params, TEXT("mode"), TEXT("overlap"));
	if (Mode != TEXT("overlap") && Mode != TEXT("contained"))
		return MCPError(FString::Printf(TEXT("Unknown mode '%s'. Use overlap or contained"), *Mode));
	const bool bContainedOnly = Mode == TEXT("contained");
	const bool bIncludeHidden = OptionalBool(Params, TEXT("includeHidden"), false);
	const bool bOnlyColliding = OptionalBool(Params, TEXT("onlyColliding"), false);
	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"));
	const int32 Limit = FMath::Clamp(OptionalInt(Params, TEXT("limit"), 200), 1, 2000);

	TSharedPtr<FJsonValue> WorldError;
	UWorld* World = MCPTrace::ResolveTraceWorld(Params, WorldError);
	if (!World) return WorldError;
	const bool bGameWorld = World->IsGameWorld();

	struct FRow { FString Label; TSharedPtr<FJsonObject> Object; };
	TArray<FRow> Rows;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor->IsA<AWorldSettings>()) continue;
		if (!ClassFilter.IsEmpty() && !Actor->GetClass()->GetName().Contains(ClassFilter, ESearchCase::IgnoreCase)) continue;

		const bool bHiddenInGame = Actor->IsHidden();
#if WITH_EDITOR
		const bool bEditorHidden = Actor->IsHiddenEd();
#else
		const bool bEditorHidden = false;
#endif
		// Hidden means hidden in the world being queried: the viewport for the
		// editor world, the game for a PIE world.
		if (!bIncludeHidden && (bGameWorld ? bHiddenInGame : bEditorHidden)) continue;

		// An actor with no primitive (an empty Actor, a bare light) is its location.
		FBox Bounds = Actor->GetComponentsBoundingBox(/*bNonColliding*/ !bOnlyColliding, /*bIncludeFromChildActors*/ true);
		const bool bFromComponents = Bounds.IsValid != 0;
		if (!bFromComponents)
		{
			if (bOnlyColliding) continue;
			Bounds = FBox(Actor->GetActorLocation(), Actor->GetActorLocation());
		}
		const bool bContained = Region.IsInsideOrOn(Bounds.Min) && Region.IsInsideOrOn(Bounds.Max);
		// FBox::Intersect counts touching faces, so a wall flush with the region is in it.
		if (bContainedOnly ? !bContained : !Region.Intersect(Bounds)) continue;

		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("label"), Actor->GetActorLabel());
		Row->SetStringField(TEXT("path"), Actor->GetPathName());
		Row->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		Row->SetStringField(TEXT("folderPath"), MCPActorFolderPath(Actor));
		Row->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Actor->GetActorLocation()));
		Row->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Actor->GetActorRotation()));
		Row->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Actor->GetActorScale3D()));
		auto BoundsObj = MakeShared<FJsonObject>();
		BoundsObj->SetObjectField(TEXT("origin"), MCPVec3ToJsonObject(Bounds.GetCenter()));
		BoundsObj->SetObjectField(TEXT("extent"), MCPVec3ToJsonObject(Bounds.GetExtent()));
		BoundsObj->SetObjectField(TEXT("min"), MCPVec3ToJsonObject(Bounds.Min));
		BoundsObj->SetObjectField(TEXT("max"), MCPVec3ToJsonObject(Bounds.Max));
		BoundsObj->SetStringField(TEXT("source"), bFromComponents ? TEXT("components") : TEXT("location"));
		Row->SetObjectField(TEXT("bounds"), BoundsObj);
		Row->SetBoolField(TEXT("contained"), bContained);
		Row->SetBoolField(TEXT("editorHidden"), bEditorHidden);
		Row->SetBoolField(TEXT("hiddenInGame"), bHiddenInGame);
		Rows.Add({ Actor->GetActorLabel(), Row });
	}
	// Sorted so limit cuts the same rows on every call.
	Rows.Sort([](const FRow& A, const FRow& B) { return A.Label < B.Label; });

	TArray<TSharedPtr<FJsonValue>> Actors;
	for (int32 Index = 0; Index < FMath::Min(Rows.Num(), Limit); ++Index)
		Actors.Add(MakeShared<FJsonValueObject>(Rows[Index].Object));

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("actors"), Actors);
	Result->SetNumberField(TEXT("count"), Actors.Num());
	Result->SetNumberField(TEXT("total"), Rows.Num());
	Result->SetBoolField(TEXT("truncated"), Rows.Num() > Limit);
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetObjectField(TEXT("min"), MCPVec3ToJsonObject(Min));
	Result->SetObjectField(TEXT("max"), MCPVec3ToJsonObject(Max));
	Result->SetStringField(TEXT("world"), MCPTrace::DescribeTracedWorld(World));
	return MCPResult(Result);
}
