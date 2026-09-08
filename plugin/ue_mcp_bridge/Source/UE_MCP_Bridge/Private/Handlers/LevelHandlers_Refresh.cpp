// Refresh and inspect actions for state the editor caches (#944, #915, #914).
//
// Each of these is a case where the editor is holding a stale answer and there
// was no way to ask it to recompute, or no way to see the number it was
// actually using.
//
//   #944  A placed Blueprint instance keeps the components and defaults it was
//         constructed with. Changing the SCS or the construction script does
//         not touch it. blueprint(run_construction_script) only runs against a
//         TEMPORARY actor, and Python cannot help because
//         Actor.rerun_construction_scripts() is missing from the wrapper.
//   #915  Collision built from mesh data the mesh no longer has produced a
//         line_trace that reported a MISS through solid geometry. A false
//         negative is the worst answer a trace can give, because nothing about
//         it looks wrong.
//   #914  A UBoxComponent's authored BoxExtent was not readable anywhere, only
//         its scaled bounds, and there was no way to ask whether two
//         components actually overlap without a physics query that depends on
//         the collision state #915 is about.

#include "LevelHandlers.h"

#include "Components/ActorComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

namespace
{
	constexpr int32 MCPRefreshMaxActors = 5000;
	constexpr int32 MCPRefreshMaxResultRows = 500;
	constexpr int32 MCPRefreshDefaultMaxComponents = 2000;
	constexpr int32 MCPRefreshHardMaxComponents = 20000;
	constexpr int32 MCPRefreshMaxSamples = 25;

	const TCHAR* MCPRefreshCollisionName(ECollisionEnabled::Type Value)
	{
		switch (Value)
		{
		case ECollisionEnabled::NoCollision:     return TEXT("NoCollision");
		case ECollisionEnabled::QueryOnly:       return TEXT("QueryOnly");
		case ECollisionEnabled::PhysicsOnly:     return TEXT("PhysicsOnly");
		case ECollisionEnabled::QueryAndPhysics: return TEXT("QueryAndPhysics");
		case ECollisionEnabled::ProbeOnly:       return TEXT("ProbeOnly");
		case ECollisionEnabled::QueryAndProbe:   return TEXT("QueryAndProbe");
		default:                                 return TEXT("Unknown");
		}
	}

	/** An oriented box in world space, built from a component's LOCAL bounds so
	 *  it keeps the component's orientation. The world bounds are axis-aligned
	 *  and would answer a different question. */
	struct FMCPOrientedBox
	{
		FVector Center = FVector::ZeroVector;
		FVector Axis[3] = { FVector::ForwardVector, FVector::RightVector, FVector::UpVector };
		FVector Extent = FVector::ZeroVector;
	};

	FMCPOrientedBox MCPBuildOrientedBox(USceneComponent* Component)
	{
		FMCPOrientedBox Box;
		if (!Component)
		{
			return Box;
		}
		const FTransform Transform = Component->GetComponentTransform();
		const FBoxSphereBounds LocalBounds = Component->GetLocalBounds();
		Box.Center = Transform.TransformPosition(LocalBounds.Origin);
		const FQuat Rotation = Transform.GetRotation();
		Box.Axis[0] = Rotation.GetAxisX();
		Box.Axis[1] = Rotation.GetAxisY();
		Box.Axis[2] = Rotation.GetAxisZ();
		const FVector Scale = Transform.GetScale3D();
		Box.Extent = FVector(
			LocalBounds.BoxExtent.X * FMath::Abs(Scale.X),
			LocalBounds.BoxExtent.Y * FMath::Abs(Scale.Y),
			LocalBounds.BoxExtent.Z * FMath::Abs(Scale.Z));
		return Box;
	}

	/** Projected radius of an oriented box onto a unit axis. */
	double MCPProjectedRadius(const FMCPOrientedBox& Box, const FVector& Axis)
	{
		return
			Box.Extent.X * FMath::Abs(FVector::DotProduct(Box.Axis[0], Axis)) +
			Box.Extent.Y * FMath::Abs(FVector::DotProduct(Box.Axis[1], Axis)) +
			Box.Extent.Z * FMath::Abs(FVector::DotProduct(Box.Axis[2], Axis));
	}

	/**
	 * Separating axis test between two oriented boxes.
	 *
	 * Deliberately geometric rather than a physics overlap query: the whole
	 * reason #914 was filed alongside #915 is that a physics answer depends on
	 * collision state, and stale collision state is what produced the wrong
	 * answer in the first place. This reads the transforms and the bounds and
	 * nothing else.
	 *
	 * Returns true when the boxes intersect. OutSeparation is the largest gap
	 * found along any tested axis when they do not, and OutPenetration is the
	 * smallest overlap along any tested axis when they do.
	 */
	bool MCPOrientedBoxesOverlap(
		const FMCPOrientedBox& A,
		const FMCPOrientedBox& B,
		double& OutSeparation,
		double& OutPenetration,
		FVector& OutAxis)
	{
		OutSeparation = 0.0;
		OutPenetration = TNumericLimits<double>::Max();
		OutAxis = FVector::ZeroVector;

		TArray<FVector, TInlineAllocator<15>> Axes;
		for (int32 Index = 0; Index < 3; ++Index) Axes.Add(A.Axis[Index]);
		for (int32 Index = 0; Index < 3; ++Index) Axes.Add(B.Axis[Index]);
		for (int32 IndexA = 0; IndexA < 3; ++IndexA)
		{
			for (int32 IndexB = 0; IndexB < 3; ++IndexB)
			{
				const FVector Cross = FVector::CrossProduct(A.Axis[IndexA], B.Axis[IndexB]);
				// Parallel edge pairs produce a degenerate axis. The six face
				// normals already cover that case, so skipping it is correct
				// rather than an approximation.
				if (Cross.SizeSquared() > UE_KINDA_SMALL_NUMBER)
				{
					Axes.Add(Cross.GetSafeNormal());
				}
			}
		}

		const FVector Delta = B.Center - A.Center;
		bool bOverlapping = true;
		for (const FVector& Axis : Axes)
		{
			if (Axis.IsNearlyZero())
			{
				continue;
			}
			const FVector Unit = Axis.GetSafeNormal();
			const double Distance = FMath::Abs(FVector::DotProduct(Delta, Unit));
			const double Reach = MCPProjectedRadius(A, Unit) + MCPProjectedRadius(B, Unit);
			const double Gap = Distance - Reach;
			if (Gap > 0.0)
			{
				bOverlapping = false;
				if (Gap > OutSeparation)
				{
					OutSeparation = Gap;
					OutAxis = Unit;
				}
			}
			else if (bOverlapping && -Gap < OutPenetration)
			{
				OutPenetration = -Gap;
				OutAxis = Unit;
			}
		}

		if (!bOverlapping)
		{
			OutPenetration = 0.0;
		}
		else if (OutPenetration == TNumericLimits<double>::Max())
		{
			// Two zero-extent boxes overlap with every separating axis
			// degenerate, so no axis ever narrowed the seed. Reporting the seed
			// would hand the caller 1.79e308 as a penetration depth in
			// centimetres, which reads as a number rather than as "there was
			// nothing to measure".
			OutPenetration = 0.0;
			OutAxis = FVector::ZeroVector;
		}
		return bOverlapping;
	}

	UActorComponent* MCPRefreshFindComponent(AActor* Actor, const FString& ComponentName)
	{
		if (!Actor) return nullptr;
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (Component && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				return Component;
			}
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> MCPRefreshDescribeBounds(const FBoxSphereBounds& Bounds)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetObjectField(TEXT("origin"), MCPVec3ToJsonObject(Bounds.Origin));
		Object->SetObjectField(TEXT("boxExtent"), MCPVec3ToJsonObject(Bounds.BoxExtent));
		Object->SetNumberField(TEXT("sphereRadius"), Bounds.SphereRadius);
		return Object;
	}

	/** Everything #914 asked to see about one side of the comparison. */
	TSharedPtr<FJsonObject> MCPRefreshDescribeComponent(USceneComponent* Component)
	{
		TSharedPtr<FJsonObject> Object = MakeShared<FJsonObject>();
		Object->SetStringField(TEXT("componentName"), Component->GetName());
		Object->SetStringField(TEXT("componentClass"), Component->GetClass()->GetName());

		const FTransform Transform = Component->GetComponentTransform();
		TSharedPtr<FJsonObject> TransformObject = MakeShared<FJsonObject>();
		TransformObject->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Transform.GetLocation()));
		TransformObject->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Transform.Rotator()));
		TransformObject->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Transform.GetScale3D()));
		Object->SetObjectField(TEXT("worldTransform"), TransformObject);

		Object->SetObjectField(TEXT("localBounds"), MCPRefreshDescribeBounds(Component->GetLocalBounds()));
		Object->SetObjectField(TEXT("worldBounds"), MCPRefreshDescribeBounds(MCPComponentWorldBounds(*Component)));

		// #914's original ask: the UNSCALED authored extent, which is what the
		// details panel shows and what no existing read returned.
		if (const UBoxComponent* BoxComponent = Cast<UBoxComponent>(Component))
		{
			Object->SetObjectField(TEXT("boxExtentUnscaled"),
				MCPVec3ToJsonObject(BoxComponent->GetUnscaledBoxExtent()));
			Object->SetObjectField(TEXT("boxExtentScaled"),
				MCPVec3ToJsonObject(BoxComponent->GetScaledBoxExtent()));
		}
		return Object;
	}
}

// ---------------------------------------------------------------------------
// rerun_construction_scripts (#944)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FLevelHandlers::RerunConstruction(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));
	}

	TArray<FString> ActorLabels;
	{
		const TArray<TSharedPtr<FJsonValue>>* LabelValues = nullptr;
		if (Params->TryGetArrayField(TEXT("actorLabels"), LabelValues) && LabelValues)
		{
			ActorLabels = JsonArrayToStringList(LabelValues);
		}
	}
	const FString ClassSpec = OptionalString(Params, TEXT("className"));
	if (ActorLabels.IsEmpty() && ClassSpec.IsEmpty())
	{
		// Rerunning every construction script in a map destroys and rebuilds
		// every generated component in it. That is not a default.
		return MCPError(TEXT("Pass 'actorLabels' or 'className'. This rebuilds generated components, so it will not run against a whole level implicitly."));
	}

	UClass* ActorClass = nullptr;
	FString ClassFallback;
	if (!ClassSpec.IsEmpty())
	{
		ActorClass = MCPResolveClassOfType(ClassSpec, AActor::StaticClass(), true);
		if (!ActorClass)
		{
			ClassFallback = ClassSpec;
		}
	}
	const bool bMatchSubclasses = OptionalBool(Params, TEXT("matchSubclasses"), true);

	TArray<AActor*> Targets;
	TSet<FString> WantedLabels(ActorLabels);
	TSet<FString> FoundLabels;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;

		bool bMatches = false;
		const FString Label = Actor->GetActorLabel();
		if (WantedLabels.Contains(Label))
		{
			bMatches = true;
			FoundLabels.Add(Label);
		}
		if (!bMatches && ActorClass)
		{
			bMatches = bMatchSubclasses ? Actor->IsA(ActorClass) : (Actor->GetClass() == ActorClass);
		}
		if (!bMatches && !ClassFallback.IsEmpty())
		{
			bMatches = Actor->GetClass()->GetName().Contains(ClassFallback, ESearchCase::IgnoreCase);
		}
		if (bMatches)
		{
			Targets.Add(Actor);
		}
	}
	if (Targets.Num() > MCPRefreshMaxActors)
	{
		return MCPError(FString::Printf(
			TEXT("Matched %d actors, over the maximum of %d. Narrow the selector."),
			Targets.Num(), MCPRefreshMaxActors));
	}
	Targets.Sort([](const AActor& A, const AActor& B) { return A.GetPathName() < B.GetPathName(); });

	int32 Reran = 0;
	int32 WithoutScript = 0;
	TArray<FString> Failed;
	TArray<FString> Samples;

#if WITH_EDITOR
	for (AActor* Actor : Targets)
	{
		if (!IsValid(Actor))
		{
			// Do not call GetName() on the pointer the guard just rejected. A
			// garbage-but-unfreed UObject happens to answer today, which is
			// exactly why this would survive testing and then not survive a
			// real collection.
			Failed.Add(TEXT("an actor in the selection is pending destruction"));
			continue;
		}
		// An actor with no user construction script still gets its generated
		// components rebuilt, so this is reported rather than skipped: a
		// caller expecting a Blueprint change to land needs to know it hit an
		// actor whose class has no script to run.
		if (!Actor->HasNonTrivialUserConstructionScript())
		{
			++WithoutScript;
		}
		Actor->Modify();
		Actor->RerunConstructionScripts();
		++Reran;
		if (Samples.Num() < MCPRefreshMaxSamples)
		{
			Samples.Add(FString::Printf(TEXT("%s (%s)"), *Actor->GetActorLabel(), *Actor->GetClass()->GetName()));
		}
	}
#else
	for (AActor* Actor : Targets)
	{
		Failed.Add(FString::Printf(
			TEXT("%s: RerunConstructionScripts is editor-only"), *Actor->GetActorLabel()));
	}
#endif

	TArray<FString> MissingLabels;
	for (const FString& Label : ActorLabels)
	{
		if (!FoundLabels.Contains(Label))
		{
			MissingLabels.Add(Label);
		}
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("success"), Failed.IsEmpty() && MissingLabels.IsEmpty());
	if (!MissingLabels.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("%d requested actor label(s) matched nothing in this world; see missingLabels."),
			MissingLabels.Num()));
	}
	Result->SetNumberField(TEXT("matched"), Targets.Num());
	Result->SetNumberField(TEXT("reran"), Reran);
	Result->SetNumberField(TEXT("actorsWithNoUserConstructionScript"), WithoutScript);
	Result->SetArrayField(TEXT("failed"), MCPStringListToJson(Failed));
	Result->SetArrayField(TEXT("missingLabels"), MCPStringListToJson(MissingLabels));
	Result->SetArrayField(TEXT("samples"), MCPStringListToJson(Samples));
	if (Reran > 0)
	{
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("saveNote"),
			TEXT("Reconstructed actors leave the level dirty and it is NOT saved. Call level(save) when you are done."));
	}
	// Rerunning a construction script rebuilds an actor's generated components
	// from its class and its instance properties. What it replaced was the
	// previous generated output, which nothing captured and no action rebuilds:
	// calling this again produces the CURRENT output, not the old one.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("There is no call that restores the generated components a construction script replaced. Running this again reruns the script and produces its current output. Any edit made directly to a generated component was already discarded by the rerun."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// recreate_physics_state (#915)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FLevelHandlers::RecreatePhysicsState(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));
	}

	TArray<FString> ActorLabels;
	{
		const TArray<TSharedPtr<FJsonValue>>* LabelValues = nullptr;
		if (Params->TryGetArrayField(TEXT("actorLabels"), LabelValues) && LabelValues)
		{
			ActorLabels = JsonArrayToStringList(LabelValues);
		}
	}
	const FString LabelPrefix = OptionalString(Params, TEXT("labelPrefix"));
	const FString Tag = OptionalString(Params, TEXT("tag"));
	const FString ActorClassSpec = OptionalString(Params, TEXT("classFilter"));
	const FString ComponentClassSpec = OptionalString(Params, TEXT("componentClass"));
	const FString ComponentNameContains = OptionalString(Params, TEXT("componentNameContains"));

	if (ActorLabels.IsEmpty() && LabelPrefix.IsEmpty() && Tag.IsEmpty() &&
		ActorClassSpec.IsEmpty() && ComponentClassSpec.IsEmpty() && ComponentNameContains.IsEmpty())
	{
		// A world-wide physics rebuild is a long, disruptive operation, and the
		// bug this action exists for is always localised to the meshes that
		// changed. Refusing an unbounded call is the point.
		return MCPError(TEXT("Pass at least one bound: actorLabels, labelPrefix, tag, classFilter, componentClass or componentNameContains. This never refreshes a whole world implicitly."));
	}

	UClass* ActorClass = nullptr;
	if (!ActorClassSpec.IsEmpty())
	{
		ActorClass = MCPResolveClassOfType(ActorClassSpec, AActor::StaticClass(), true);
		if (!ActorClass)
		{
			return MCPClassNotFoundError(ActorClassSpec, TEXT("classFilter"));
		}
	}
	UClass* ComponentClass = nullptr;
	if (!ComponentClassSpec.IsEmpty())
	{
		ComponentClass = MCPResolveClassOfType(ComponentClassSpec, UActorComponent::StaticClass(), true);
		if (!ComponentClass)
		{
			return MCPClassNotFoundError(ComponentClassSpec, TEXT("componentClass"));
		}
	}

	// dryRun defaults to TRUE: this is the preflight #915 asked for. Seeing
	// which components are about to be rebuilt, and what their collision state
	// is now, is most of the value.
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), true);
	const int32 MaxComponents = FMath::Clamp(
		OptionalInt(Params, TEXT("maxComponents"), MCPRefreshDefaultMaxComponents),
		1, MCPRefreshHardMaxComponents);

	TSet<FString> WantedLabels(ActorLabels);
	TSet<FString> FoundLabels;
	TArray<UPrimitiveComponent*> Targets;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor) continue;
		const FString Label = Actor->GetActorLabel();

		if (!WantedLabels.IsEmpty())
		{
			if (!WantedLabels.Contains(Label)) continue;
			FoundLabels.Add(Label);
		}
		if (!LabelPrefix.IsEmpty() && !Label.StartsWith(LabelPrefix, ESearchCase::CaseSensitive)) continue;
		if (!Tag.IsEmpty() && !Actor->ActorHasTag(FName(*Tag))) continue;
		if (ActorClass && !Actor->IsA(ActorClass)) continue;

		for (UActorComponent* Component : Actor->GetComponents())
		{
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
			if (!Primitive) continue;
			if (ComponentClass && !Primitive->IsA(ComponentClass)) continue;
			if (!ComponentNameContains.IsEmpty() &&
				!Primitive->GetName().Contains(ComponentNameContains, ESearchCase::IgnoreCase))
			{
				continue;
			}
			Targets.Add(Primitive);
		}
	}

	Targets.Sort([](const UPrimitiveComponent& A, const UPrimitiveComponent& B)
	{
		return A.GetPathName() < B.GetPathName();
	});

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetNumberField(TEXT("matchedComponents"), Targets.Num());

	TArray<FString> MissingLabels;
	for (const FString& Label : ActorLabels)
	{
		if (!FoundLabels.Contains(Label)) MissingLabels.Add(Label);
	}
	if (!MissingLabels.IsEmpty())
	{
		Result->SetArrayField(TEXT("missingLabels"), MCPStringListToJson(MissingLabels));
	}

	if (Targets.Num() > MaxComponents)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Matched %d components, over maxComponents (%d). Narrow the bounds or raise it deliberately."),
			Targets.Num(), MaxComponents));
		return MCPResult(Result);
	}

	TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Recreated = 0;
	int32 PhysicsStateAfter = 0;
	int32 NoCollisionCount = 0;
	TMap<FString, int32> CollisionSummary;

	for (UPrimitiveComponent* Primitive : Targets)
	{
		const ECollisionEnabled::Type Before = Primitive->GetCollisionEnabled();
		const bool bStateBefore = Primitive->IsPhysicsStateCreated();

		if (!bDryRun)
		{
			Primitive->RecreatePhysicsState();
			++Recreated;
		}

		const ECollisionEnabled::Type After = Primitive->GetCollisionEnabled();
		const bool bStateAfter = Primitive->IsPhysicsStateCreated();
		if (bStateAfter) ++PhysicsStateAfter;
		if (After == ECollisionEnabled::NoCollision) ++NoCollisionCount;
		++CollisionSummary.FindOrAdd(MCPRefreshCollisionName(After));

		if (Rows.Num() < MCPRefreshMaxResultRows)
		{
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("actorLabel"), Primitive->GetOwner() ? Primitive->GetOwner()->GetActorLabel() : FString());
			Row->SetStringField(TEXT("componentName"), Primitive->GetName());
			Row->SetStringField(TEXT("componentClass"), Primitive->GetClass()->GetName());
			Row->SetStringField(TEXT("status"), bDryRun ? TEXT("would_recreate") : TEXT("recreated"));
			Row->SetStringField(TEXT("collisionEnabledBefore"), MCPRefreshCollisionName(Before));
			Row->SetStringField(TEXT("collisionEnabledAfter"), MCPRefreshCollisionName(After));
			Row->SetStringField(TEXT("collisionProfile"), Primitive->GetCollisionProfileName().ToString());
			Row->SetBoolField(TEXT("physicsStateCreatedBefore"), bStateBefore);
			Row->SetBoolField(TEXT("physicsStateCreatedAfter"), bStateAfter);
			Row->SetBoolField(TEXT("registered"), Primitive->IsRegistered());
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}

	TSharedPtr<FJsonObject> Summary = MakeShared<FJsonObject>();
	for (const TPair<FString, int32>& Pair : CollisionSummary)
	{
		Summary->SetNumberField(Pair.Key, Pair.Value);
	}

	Result->SetNumberField(bDryRun ? TEXT("wouldRecreate") : TEXT("recreated"),
		bDryRun ? Targets.Num() : Recreated);
	Result->SetNumberField(TEXT("physicsStateCreated"), PhysicsStateAfter);
	Result->SetNumberField(TEXT("componentsWithNoCollision"), NoCollisionCount);
	Result->SetObjectField(TEXT("collisionSummary"), Summary);
	Result->SetNumberField(TEXT("returnedResults"), Rows.Num());
	Result->SetBoolField(TEXT("resultsTruncated"), Rows.Num() < Targets.Num());
	Result->SetArrayField(TEXT("results"), Rows);
	if (Targets.IsEmpty())
	{
		Result->SetStringField(TEXT("zeroMatchNote"),
			TEXT("Nothing matched. The bounds are AND-ed: an actor selector and a componentClass must both hold. Check with level(query_components, countOnly:true) using the same filters."));
	}
	if (bDryRun)
	{
		Result->SetStringField(TEXT("dryRunNote"),
			TEXT("dryRun defaults to TRUE for this action: the preflight shows which components would be rebuilt and their current collision state. Pass dryRun=false to rebuild them."));
	}
	else
	{
		Result->SetStringField(TEXT("traceNote"),
			TEXT("Re-run the trace that gave the wrong answer. A false-negative line_trace caused by stale collision looks exactly like a real miss, so the only proof this worked is the trace."));
	}
	Result->SetBoolField(TEXT("unchanged"), bDryRun || Recreated == 0);
	// This rebuilds a component's physics state from its current collision
	// settings. What it replaced was a stale body, and there is no call that
	// puts a stale body back: running this again rebuilds from the same
	// settings and lands on the same state.
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"),
		TEXT("Recreating physics state discards a stale body and rebuilds it from the component's collision settings, which this call does not change. There is nothing to restore and no call that would restore it."));
	return MCPResult(Result);
}

// ---------------------------------------------------------------------------
// test_component_overlap (#914)
// ---------------------------------------------------------------------------
TSharedPtr<FJsonValue> FLevelHandlers::TestComponentOverlap(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();

	const FString WorldScope = OptionalString(Params, TEXT("world"), TEXT("editor"));
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(FString::Printf(TEXT("World not available for scope '%s'"), *WorldScope));
	}

	FString ActorLabelA;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabelA"), TEXT("actorPathA"), ActorLabelA)) return Err;
	FString ActorLabelB;
	if (auto Err = RequireStringAlt(Params, TEXT("actorLabelB"), TEXT("actorPathB"), ActorLabelB)) return Err;
	const FString ComponentNameA = OptionalString(Params, TEXT("componentNameA"));
	const FString ComponentNameB = OptionalString(Params, TEXT("componentNameB"));

	FString Method = OptionalString(Params, TEXT("method"), TEXT("OBB")).ToUpper();
	if (Method != TEXT("OBB") && Method != TEXT("AABB"))
	{
		return MCPError(TEXT("'method' must be either 'OBB' (oriented, default) or 'AABB' (axis-aligned world bounds)"));
	}

	// #983: each side is selected by its own label / path pair, so an overlap
	// test between two copy-pasted props refuses rather than answering about
	// two actors the caller did not name.
	auto ResolveSide = [&](const TCHAR* LabelKey, const TCHAR* PathKey, const FString& ComponentName,
		USceneComponent*& OutComponent, AActor*& OutActor, TSharedPtr<FJsonValue>& OutError) -> bool
	{
		FMCPActorSelector Selector;
		Selector.LabelKey = LabelKey;
		Selector.PathKey = PathKey;
		AActor* Actor = MCPResolveActor(World, Params, OutError, Selector);
		if (!Actor) return false;
		OutActor = Actor;
		const FString ActorLabel = Actor->GetActorLabel();
		if (ComponentName.IsEmpty())
		{
			OutComponent = Actor->GetRootComponent();
			if (!OutComponent)
			{
				OutError = MCPError(FString::Printf(TEXT("Actor '%s' has no root component"), *ActorLabel));
				return false;
			}
			return true;
		}
		UActorComponent* Component = MCPRefreshFindComponent(Actor, ComponentName);
		OutComponent = Cast<USceneComponent>(Component);
		if (!OutComponent)
		{
			OutError = MCPError(FString::Printf(
				TEXT("No SceneComponent named '%s' on actor '%s'"), *ComponentName, *ActorLabel));
			return false;
		}
		return true;
	};

	USceneComponent* ComponentA = nullptr;
	USceneComponent* ComponentB = nullptr;
	AActor* ActorA = nullptr;
	AActor* ActorB = nullptr;
	TSharedPtr<FJsonValue> ResolveError;
	if (!ResolveSide(TEXT("actorLabelA"), TEXT("actorPathA"), ComponentNameA, ComponentA, ActorA, ResolveError)) return ResolveError;
	if (!ResolveSide(TEXT("actorLabelB"), TEXT("actorPathB"), ComponentNameB, ComponentB, ActorB, ResolveError)) return ResolveError;
	if (ComponentA == ComponentB)
	{
		return MCPError(TEXT("Both sides resolved to the same component; an overlap test between a component and itself has no answer"));
	}
	ActorLabelA = ActorA->GetActorLabel();
	ActorLabelB = ActorB->GetActorLabel();

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("method"), Method);
	Result->SetStringField(TEXT("actorLabelA"), ActorLabelA);
	Result->SetStringField(TEXT("actorPathA"), ActorA->GetPathName());
	Result->SetStringField(TEXT("actorLabelB"), ActorLabelB);
	Result->SetStringField(TEXT("actorPathB"), ActorB->GetPathName());

	TSharedPtr<FJsonObject> SideA = MCPRefreshDescribeComponent(ComponentA);
	SideA->SetStringField(TEXT("actorLabel"), ActorLabelA);
	SideA->SetStringField(TEXT("actorPath"), ActorA->GetPathName());
	TSharedPtr<FJsonObject> SideB = MCPRefreshDescribeComponent(ComponentB);
	SideB->SetStringField(TEXT("actorLabel"), ActorLabelB);
	SideB->SetStringField(TEXT("actorPath"), ActorB->GetPathName());
	Result->SetObjectField(TEXT("a"), SideA);
	Result->SetObjectField(TEXT("b"), SideB);

	bool bOverlap = false;
	double Separation = 0.0;
	double Penetration = 0.0;
	FVector Axis = FVector::ZeroVector;

	if (Method == TEXT("AABB"))
	{
		const FBox BoxA = MCPComponentWorldBounds(*ComponentA).GetBox();
		const FBox BoxB = MCPComponentWorldBounds(*ComponentB).GetBox();
		bOverlap = BoxA.Intersect(BoxB);
		// Per-axis gap or overlap depth, which is what a caller tuning a
		// placement actually wants back.
		FVector PerAxis = FVector::ZeroVector;
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const double Gap = FMath::Max(
				BoxA.Min[Index] - BoxB.Max[Index],
				BoxB.Min[Index] - BoxA.Max[Index]);
			PerAxis[Index] = Gap;
		}
		if (bOverlap)
		{
			// Every axis gap is negative when they intersect; the shallowest
			// is the penetration depth.
			Penetration = -FMath::Max3(PerAxis.X, PerAxis.Y, PerAxis.Z);
		}
		else
		{
			Separation = FMath::Max3(PerAxis.X, PerAxis.Y, PerAxis.Z);
		}
		Result->SetObjectField(TEXT("perAxisGap"), MCPVec3ToJsonObject(PerAxis));
	}
	else
	{
		const FMCPOrientedBox BoxA = MCPBuildOrientedBox(ComponentA);
		const FMCPOrientedBox BoxB = MCPBuildOrientedBox(ComponentB);
		bOverlap = MCPOrientedBoxesOverlap(BoxA, BoxB, Separation, Penetration, Axis);
		Result->SetObjectField(TEXT("separatingAxis"), MCPVec3ToJsonObject(Axis));
	}

	Result->SetBoolField(TEXT("overlap"), bOverlap);
	Result->SetNumberField(TEXT("separation"), bOverlap ? 0.0 : Separation);
	Result->SetNumberField(TEXT("penetration"), bOverlap ? Penetration : 0.0);
	Result->SetStringField(TEXT("note"),
		TEXT("This is a geometric test over the components' bounds and transforms. It deliberately does not run a physics overlap query, because a physics answer depends on collision state and stale collision state is what makes a trace lie. Use level(recreate_physics_state) when you need the physics answer to agree."));
	return MCPResult(Result);
}
