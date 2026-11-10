// Spline generators.
//
// level(set_spline_points) edits a spline that already exists; nothing made
// one, and nothing filled one with geometry, so a road, fence, pipe or cable
// run was a Blueprint to write first. These handlers spawn a spline actor from
// points, fill a spline with a chain of USplineMeshComponents, and scatter
// meshes along it as instances or actors.
//
// Output is regenerable. Every component or actor a generator makes carries
// the tag "SplineGen:<owner object name>:<generator name>", and the parameters
// it ran with are kept on the spline's actor as one record tag
// ("SplineGenParams:<generator name>=<json>", see LevelGeneratorRecords.h). A
// second run with the same generator name removes the first run's output
// before building, so it replaces rather than duplicates, and rebuild=true
// reuses the stored parameters with any passed ones on top.

#include "LevelHandlers_SplineGen.h"
#include "LevelGeneratorRecords.h"
#include "LevelSpawnFinalize.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "ScopedTransaction.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Math/RandomStream.h"

namespace
{
	const TCHAR* const MCPSpgParamsPrefixRoot = TEXT("SplineGenParams:");

	FString MCPSpgParamsPrefix(const FString& Generator)
	{
		return FString(MCPSpgParamsPrefixRoot) + Generator + TEXT("=");
	}

	FName MCPSpgOutputTag(const AActor* Owner, const FString& Generator)
	{
		return FName(*FString::Printf(TEXT("SplineGen:%s:%s"), *Owner->GetName(), *Generator));
	}

	/** Index of the forward axis, and the cross-section scale the spline mesh
	 *  expects from a 3D scale: the two other axes in X, Y, Z cyclic order
	 *  after the forward one, as USplineMeshComponent lays its slice out. */
	int32 MCPSpgAxisIndex(ESplineMeshAxis::Type Axis)
	{
		return Axis == ESplineMeshAxis::Y ? 1 : (Axis == ESplineMeshAxis::Z ? 2 : 0);
	}

	FVector2D MCPSpgCrossScale(const FVector& Scale, ESplineMeshAxis::Type Axis)
	{
		switch (Axis)
		{
		case ESplineMeshAxis::Y: return FVector2D(Scale.Z, Scale.X);
		case ESplineMeshAxis::Z: return FVector2D(Scale.X, Scale.Y);
		default: return FVector2D(Scale.Y, Scale.Z);
		}
	}

	bool MCPSpgReadVector(const TSharedPtr<FJsonObject>& Bag, const TCHAR* Key, FVector& Out)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Bag.IsValid() || !Bag->TryGetObjectField(Key, Obj) || !Obj || !Obj->IsValid()) return false;
		FVector Value = Out;
		ReadVec3Fields(*Obj, Value);
		if (Value.ContainsNaN()) return false;
		Out = Value;
		return true;
	}

	/** Remove what a generator made last time: its tagged components on the
	 *  owner and its tagged actors anywhere in the world. */
	void MCPSpgRemoveOutputs(UWorld* World, AActor* Owner, FName Tag, TArray<FString>& OutComponents, TArray<FString>& OutActors)
	{
		TArray<UActorComponent*> Components;
		Owner->GetComponents(Components);
		for (UActorComponent* Component : Components)
		{
			if (!Component || !Component->ComponentHasTag(Tag)) continue;
			OutComponents.Add(Component->GetName());
			Owner->Modify();
			Component->Modify();
			Owner->RemoveInstanceComponent(Component);
			Component->DestroyComponent();
		}
		TArray<AActor*> Actors;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (*It && *It != Owner && It->ActorHasTag(Tag)) Actors.Add(*It);
		}
		for (AActor* Actor : Actors)
		{
			OutActors.Add(Actor->GetPathName());
			World->DestroyActor(Actor);
		}
	}

	struct FMCPSpgTarget
	{
		AActor* Owner = nullptr;
		USplineComponent* Spline = nullptr;
		FString Generator;
		TSharedPtr<FJsonObject> Previous;
		TSharedPtr<FJsonObject> Effective;
	};

	/** Resolve the spline, the generator name and the parameters this run uses:
	 *  the stored ones when rebuild is set, overridden key by key by the call. */
	TSharedPtr<FJsonValue> MCPSpgResolveTarget(
		UWorld* World, const TSharedPtr<FJsonObject>& Params, const TCHAR* Kind, const TCHAR* DefaultGenerator,
		std::initializer_list<const TCHAR*> Keys, FMCPSpgTarget& Out)
	{
		TSharedPtr<FJsonValue> Err;
		Out.Owner = MCPResolveActor(World, Params, Err);
		if (!Out.Owner) return Err;
		Out.Generator = OptionalString(Params, TEXT("generatorName"), DefaultGenerator);
		if (Out.Generator.IsEmpty() || Out.Generator.Contains(TEXT("=")) || Out.Generator.Contains(TEXT(":")))
		{
			return MCPError(TEXT("generatorName must be non-empty and contain neither '=' nor ':'"));
		}
		Out.Previous = MCPLevelRecords::Read(Out.Owner, MCPSpgParamsPrefix(Out.Generator));
		const bool bRebuild = OptionalBool(Params, TEXT("rebuild"), false);
		Out.Effective = MakeShared<FJsonObject>();
		if (bRebuild)
		{
			if (!Out.Previous.IsValid())
			{
				return MCPError(FString::Printf(TEXT("rebuild: '%s' has no stored parameters for generator '%s'; run it once with its parameters first"),
					*Out.Owner->GetActorLabel(), *Out.Generator));
			}
			FString PreviousKind;
			Out.Previous->TryGetStringField(TEXT("kind"), PreviousKind);
			if (PreviousKind != Kind)
			{
				return MCPError(FString::Printf(TEXT("rebuild: generator '%s' was a %s run, not %s; use the matching action or another generatorName"),
					*Out.Generator, *PreviousKind, Kind));
			}
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Out.Previous->Values) Out.Effective->SetField(Pair.Key, Pair.Value);
		}
		for (const TCHAR* Key : Keys)
		{
			if (TSharedPtr<FJsonValue> Value = TryGetParam(Params, Key)) Out.Effective->SetField(Key, Value);
		}
		Out.Effective->SetStringField(TEXT("kind"), Kind);

		const FString ComponentName = OptionalString(Out.Effective, TEXT("componentName"));
		if (!ComponentName.IsEmpty())
		{
			Out.Spline = MCPFindComponentByName<USplineComponent>(Out.Owner, ComponentName);
			if (!Out.Spline)
			{
				return MCPError(FString::Printf(TEXT("'%s' has no SplineComponent named '%s'"), *Out.Owner->GetActorLabel(), *ComponentName));
			}
		}
		else
		{
			Out.Spline = Out.Owner->FindComponentByClass<USplineComponent>();
			if (!Out.Spline) return MCPError(FString::Printf(TEXT("'%s' has no SplineComponent"), *Out.Owner->GetActorLabel()));
		}
		if (Out.Spline->GetNumberOfSplinePoints() < 2)
		{
			return MCPError(FString::Printf(TEXT("Spline '%s' on '%s' has fewer than two points"), *Out.Spline->GetName(), *Out.Owner->GetActorLabel()));
		}
		return nullptr;
	}

	/** Start and end distances after the offsets, or an error when they cross. */
	TSharedPtr<FJsonValue> MCPSpgRange(const TSharedPtr<FJsonObject>& Effective, const USplineComponent* Spline, double& OutStart, double& OutEnd)
	{
		const double Length = Spline->GetSplineLength();
		OutStart = FMath::Max(0.0, OptionalNumber(Effective, TEXT("startOffset"), 0.0));
		OutEnd = Length - FMath::Max(0.0, OptionalNumber(Effective, TEXT("endOffset"), 0.0));
		if (OutEnd - OutStart <= KINDA_SMALL_NUMBER)
		{
			return MCPError(FString::Printf(TEXT("startOffset and endOffset leave nothing of the %.1f cm spline"), Length));
		}
		return nullptr;
	}

	/** Store this run's parameters, run the rollback bookkeeping and finish the result. */
	void MCPSpgFinish(const FMCPSpgTarget& Target, const TCHAR* Method, int32 RemovedCount, const TSharedPtr<FJsonObject>& Result)
	{
		Target.Owner->Modify();
		FString StoreError;
		const bool bStored = MCPLevelRecords::Write(Target.Owner, MCPSpgParamsPrefix(Target.Generator), Target.Effective, StoreError);
		Result->SetBoolField(TEXT("paramsStored"), bStored);
		if (!bStored) Result->SetStringField(TEXT("paramsNote"), StoreError + TEXT("; rebuild needs the parameters passed again."));
		Result->SetObjectField(TEXT("params"), Target.Effective);
		Result->SetStringField(TEXT("generatorName"), Target.Generator);
		Result->SetStringField(TEXT("actorLabel"), Target.Owner->GetActorLabel());
		Result->SetStringField(TEXT("actorPath"), Target.Owner->GetPathName());
		Result->SetStringField(TEXT("componentName"), Target.Spline->GetName());
		Result->SetStringField(TEXT("outputTag"), MCPSpgOutputTag(Target.Owner, Target.Generator).ToString());
		Result->SetNumberField(TEXT("replacedPrevious"), RemovedCount);
		if (RemovedCount > 0 || Target.Previous.IsValid()) MCPSetUpdated(Result);
		else MCPSetCreated(Result);

		if (Target.Previous.IsValid())
		{
			// Running the previous parameters again puts the previous output back.
			FString PreviousKind;
			Target.Previous->TryGetStringField(TEXT("kind"), PreviousKind);
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Target.Previous->Values)
			{
				if (Pair.Key != TEXT("kind")) Payload->SetField(Pair.Key, Pair.Value);
			}
			Payload->SetStringField(TEXT("actorPath"), Target.Owner->GetPathName());
			Payload->SetStringField(TEXT("generatorName"), Target.Generator);
			MCPSetRollback(Result, PreviousKind == TEXT("scatter") ? TEXT("scatter_along_spline") : TEXT("build_spline_meshes"), Payload);
		}
		else
		{
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
			Payload->SetStringField(TEXT("actorPath"), Target.Owner->GetPathName());
			Payload->SetStringField(TEXT("generatorName"), Target.Generator);
			MCPSetRollback(Result, TEXT("clear_spline_generated"), Payload);
		}
		(void)Method;
	}

	TArray<TSharedPtr<FJsonValue>> MCPSpgReadPoints(const TSharedPtr<FJsonObject>& Params, TArray<FVector>& OutPoints, FString& OutError)
	{
		TArray<TSharedPtr<FJsonValue>> Raw;
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!TryGetArrayParam(Params, TEXT("splinePoints"), Rows) || !Rows)
		{
			OutError = TEXT("splinePoints is required: at least two {x, y, z} positions");
			return Raw;
		}
		for (int32 Index = 0; Index < Rows->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			FVector Point;
			if (!(*Rows)[Index].IsValid() || !(*Rows)[Index]->TryGetObject(Row) || !Row || !ReadVec3FieldsStrict(*Row, Point) || Point.ContainsNaN())
			{
				OutError = FString::Printf(TEXT("splinePoints[%d] must be {x, y, z} with every axis a finite number"), Index);
				return Raw;
			}
			OutPoints.Add(Point);
			Raw.Add((*Rows)[Index]);
		}
		if (OutPoints.Num() < 2) OutError = TEXT("splinePoints needs at least two positions");
		return Raw;
	}

	bool MCPSpgParsePointType(const FString& Name, ESplinePointType::Type& Out)
	{
		if (Name.IsEmpty() || Name == TEXT("Curve")) { Out = ESplinePointType::Curve; return true; }
		if (Name == TEXT("Linear")) { Out = ESplinePointType::Linear; return true; }
		if (Name == TEXT("CurveClamped")) { Out = ESplinePointType::CurveClamped; return true; }
		if (Name == TEXT("Constant")) { Out = ESplinePointType::Constant; return true; }
		return false;
	}

	void MCPSpgWritePoints(USplineComponent* Spline, const TArray<FVector>& Points, bool bLocal, ESplinePointType::Type Type, bool bClosed)
	{
		Spline->Modify();
		Spline->ClearSplinePoints(false);
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			Spline->AddSplinePoint(Points[Index], bLocal ? ESplineCoordinateSpace::Local : ESplineCoordinateSpace::World, false);
			Spline->SetSplinePointType(Index, Type, false);
		}
		Spline->SetClosedLoop(bClosed, false);
		Spline->bSplineHasBeenEdited = true;
		Spline->UpdateSpline();
	}

	TArray<TSharedPtr<FJsonValue>> MCPSpgWorldPoints(const USplineComponent* Spline)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 Index = 0; Index < Spline->GetNumberOfSplinePoints(); ++Index)
		{
			Out.Add(MakeShared<FJsonValueObject>(MCPVec3ToJsonObject(Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World))));
		}
		return Out;
	}
}

void FLevelSplineGenHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("level"));
	using EType = EMCPParamType;

	const FMCPParamSpec SpecActorLabel = MCPParam::Optional(TEXT("actorLabel"), EType::String, TEXT("Label of the actor that owns the spline; pass actorLabel or actorPath"));
	const FMCPParamSpec SpecActorPath = MCPParam::Optional(TEXT("actorPath"), EType::String, TEXT("Full path of the actor that owns the spline; wins over actorLabel"));
	const FMCPSpecRules OneActor = MCPSpec::ExactlyOne({ { TEXT("actorLabel") }, { TEXT("actorPath") } });
	const FMCPParamSpec SpecComponent = MCPParam::Optional(TEXT("componentName"), EType::String, TEXT("Spline component, when the actor has several (default the first)"));
	const FMCPParamSpec SpecRebuild = MCPParam::Optional(TEXT("rebuild"), EType::Boolean, TEXT("Reuse the parameters stored by the last run of this generator; passed parameters override them"));
	const FMCPParamSpec SpecStartOffset = MCPParam::Optional(TEXT("startOffset"), EType::Number, TEXT("Distance along the spline to leave empty at its start, in centimetres"));
	const FMCPParamSpec SpecEndOffset = MCPParam::Optional(TEXT("endOffset"), EType::Number, TEXT("Distance along the spline to leave empty at its end, in centimetres"));

	Registry.RegisterHandler(TEXT("create_spline_actor"), &CreateSplineActor, {
		MCPParam::Required(TEXT("splinePoints"), EType::Array, TEXT("At least two positions, world space unless coordinateSpace is local")).Items(EType::Object).WithFields({
			MCPParam::RequiredField(TEXT("x"), EType::Number, TEXT("X")),
			MCPParam::RequiredField(TEXT("y"), EType::Number, TEXT("Y")),
			MCPParam::RequiredField(TEXT("z"), EType::Number, TEXT("Z")),
		}),
		MCPParam::Optional(TEXT("label"), EType::String, TEXT("Actor label (default SplineActor); an existing actor with this label is reported rather than duplicated")),
		MCPParam::Optional(TEXT("location"), EType::Vec3, TEXT("Actor location (default the first point)")),
		MCPParam::Optional(TEXT("closedLoop"), EType::Boolean, TEXT("Close the spline (default false)")),
		MCPParam::Optional(TEXT("pointType"), EType::String, TEXT("Curve (default) | Linear | CurveClamped | Constant, for every point")),
		MCPParam::Optional(TEXT("coordinateSpace"), EType::String, TEXT("world (default) | local: whether points are relative to the actor location")),
		MCPParam::Optional(TEXT("tags"), EType::Array, TEXT("Actor tags to add")).Items(EType::String),
		MCPParam::Optional(TEXT("onConflict"), EType::String, TEXT("When the label is taken: skip (default) | update (rewrite its spline points) | error")),
	});
	Registry.RegisterHandler(TEXT("build_spline_meshes"), &BuildSplineMeshes, {
		SpecActorLabel, SpecActorPath, SpecComponent,
		MCPParam::Optional(TEXT("generatorName"), EType::String, TEXT("Names this output so a later run replaces it (default SplineMeshes)")),
		MCPParam::Optional(TEXT("staticMesh"), EType::String, TEXT("Mesh laid along the spline; required unless rebuild is set")),
		MCPParam::Optional(TEXT("spacing"), EType::Number, TEXT("Length of each piece in centimetres (default the mesh length along forwardAxis times its scale)")),
		MCPParam::Optional(TEXT("forwardAxis"), EType::String, TEXT("X (default) | Y | Z: the mesh axis laid along the spline")),
		MCPParam::Optional(TEXT("scale"), EType::Vec3, TEXT("Mesh scale; the forwardAxis component scales the default spacing, the other two the cross-section")),
		MCPParam::Optional(TEXT("roll"), EType::Number, TEXT("Roll of every piece around the spline, in degrees")),
		SpecStartOffset, SpecEndOffset,
		MCPParam::Optional(TEXT("stretchToFit"), EType::Boolean, TEXT("Stretch the pieces so a whole number of them fills the spline exactly (default true)")),
		MCPParam::Optional(TEXT("collisionProfile"), EType::String, TEXT("Collision profile for the pieces, e.g. BlockAll or NoCollision")),
		SpecRebuild,
	}, OneActor);
	Registry.RegisterHandler(TEXT("scatter_along_spline"), &ScatterAlongSpline, {
		SpecActorLabel, SpecActorPath, SpecComponent,
		MCPParam::Optional(TEXT("generatorName"), EType::String, TEXT("Names this output so a later run replaces it (default SplineScatter)")),
		MCPParam::Optional(TEXT("meshes"), EType::Array, TEXT("StaticMesh paths to scatter; required unless rebuild is set")).Items(EType::String),
		MCPParam::Optional(TEXT("weights"), EType::Array, TEXT("Relative pick weight per mesh (default equal)")).Items(EType::Number),
		MCPParam::Optional(TEXT("mode"), EType::String, TEXT("instances (default, one HierarchicalInstancedStaticMeshComponent per mesh on the spline actor) | actors (StaticMeshActors)")),
		MCPParam::Optional(TEXT("interval"), EType::Number, TEXT("Distance between placements in centimetres (default 200); ignored when count is given")),
		MCPParam::Optional(TEXT("count"), EType::Integer, TEXT("Number of placements spread evenly over the spline")),
		SpecStartOffset, SpecEndOffset,
		MCPParam::Optional(TEXT("offset"), EType::Vec3, TEXT("Constant offset in the spline's frame: x along it, y to its right, z up")),
		MCPParam::Optional(TEXT("positionJitter"), EType::Vec3, TEXT("Random offset range per axis in the spline's frame, in centimetres")),
		MCPParam::Optional(TEXT("yawJitter"), EType::Number, TEXT("Random yaw range in degrees, either way")),
		MCPParam::Optional(TEXT("scaleMin"), EType::Number, TEXT("Smallest uniform scale (default 1)")),
		MCPParam::Optional(TEXT("scaleMax"), EType::Number, TEXT("Largest uniform scale (default scaleMin)")),
		MCPParam::Optional(TEXT("alignToSpline"), EType::Boolean, TEXT("Rotate each placement to the spline's direction (default true)")),
		MCPParam::Optional(TEXT("seed"), EType::Integer, TEXT("Random seed; the same seed and parameters place the same result (default 0)")),
		MCPParam::Optional(TEXT("maxCount"), EType::Integer, TEXT("Refuse more placements than this (default 10000)")),
		SpecRebuild,
	}, OneActor);
	Registry.RegisterHandler(TEXT("clear_spline_generated"), &ClearSplineGenerated, {
		SpecActorLabel, SpecActorPath,
		MCPParam::Required(TEXT("generatorName"), EType::String, TEXT("The generator whose output and stored parameters to remove")),
	}, OneActor);
}

// level(create_spline_actor): an actor whose root is a spline through the given points.
TSharedPtr<FJsonValue> FLevelSplineGenHandlers::CreateSplineActor(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("splinePoints"), TEXT("label"), TEXT("location"), TEXT("closedLoop"), TEXT("pointType"), TEXT("coordinateSpace"),
		TEXT("tags"), TEXT("onConflict"),
	});
	REQUIRE_EDITOR_WORLD(World);

	TArray<FVector> Points;
	FString PointError;
	MCPSpgReadPoints(Params, Points, PointError);
	if (!PointError.IsEmpty()) return MCPError(PointError);
	const FString Label = OptionalString(Params, TEXT("label"), TEXT("SplineActor"));
	const FString OnConflict = OptionalString(Params, TEXT("onConflict"), TEXT("skip"));
	if (OnConflict != TEXT("skip") && OnConflict != TEXT("update") && OnConflict != TEXT("error"))
	{
		return MCPError(FString::Printf(TEXT("onConflict must be skip, update or error, not '%s'"), *OnConflict));
	}
	ESplinePointType::Type PointType = ESplinePointType::Curve;
	const FString PointTypeName = OptionalString(Params, TEXT("pointType"));
	if (!MCPSpgParsePointType(PointTypeName, PointType))
	{
		return MCPError(FString::Printf(TEXT("pointType must be Curve, Linear, CurveClamped or Constant, not '%s'"), *PointTypeName));
	}
	const FString Space = OptionalString(Params, TEXT("coordinateSpace"), TEXT("world"));
	if (Space != TEXT("world") && Space != TEXT("local"))
	{
		return MCPError(FString::Printf(TEXT("coordinateSpace must be world or local, not '%s'"), *Space));
	}
	const bool bLocal = Space == TEXT("local");
	const bool bClosed = OptionalBool(Params, TEXT("closedLoop"), false);
	FVector Location = bLocal ? FVector::ZeroVector : Points[0];
	MCPSpgReadVector(Params, TEXT("location"), Location);

	// An existing actor of this label: report it, refuse, or rewrite its points.
	TArray<AActor*> Existing;
	MCPCollectActorsByToken(World, Label, EMCPActorMatch::Label, Existing);
	if (Existing.Num() > 0)
	{
		if (OnConflict == TEXT("error"))
		{
			return MCPError(FString::Printf(TEXT("An actor labelled '%s' already exists: %s"), *Label, *Existing[0]->GetPathName()));
		}
		if (Existing.Num() > 1) return MCPAmbiguousActorError(Label, TEXT("label"), TEXT("actorPath"), TEXT("label"), Existing);
		AActor* Actor = Existing[0];
		USplineComponent* Spline = Actor->FindComponentByClass<USplineComponent>();
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
		Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
		if (OnConflict == TEXT("skip") || !Spline)
		{
			if (!Spline && OnConflict == TEXT("update"))
			{
				return MCPError(FString::Printf(TEXT("'%s' exists but has no SplineComponent to update"), *Label));
			}
			MCPSetExisted(Result);
			if (Spline)
			{
				Result->SetStringField(TEXT("componentName"), Spline->GetName());
				Result->SetNumberField(TEXT("pointCount"), Spline->GetNumberOfSplinePoints());
			}
			return MCPResult(Result);
		}
		// update: rewrite the points, with the previous ones as the inverse.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("label"), Actor->GetActorLabel());
		Payload->SetStringField(TEXT("onConflict"), TEXT("update"));
		Payload->SetArrayField(TEXT("splinePoints"), MCPSpgWorldPoints(Spline));
		Payload->SetBoolField(TEXT("closedLoop"), Spline->IsClosedLoop());
		{
			FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "SplineActorUpdate", "Update Spline Actor Points"));
			Actor->Modify();
			TArray<FVector> WorldPoints = Points;
			if (bLocal)
			{
				for (FVector& P : WorldPoints) P = Actor->GetActorTransform().TransformPosition(P);
			}
			MCPSpgWritePoints(Spline, WorldPoints, false, PointType, bClosed);
		}
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("componentName"), Spline->GetName());
		Result->SetNumberField(TEXT("pointCount"), Spline->GetNumberOfSplinePoints());
		Result->SetNumberField(TEXT("splineLength"), Spline->GetSplineLength());
		Result->SetArrayField(TEXT("splinePoints"), MCPSpgWorldPoints(Spline));
		MCPSetRollback(Result, TEXT("create_spline_actor"), Payload);
		return MCPResult(Result);
	}

	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "SplineActorCreate", "Create Spline Actor"));
	FActorSpawnParameters SpawnParams;
	SpawnParams.ObjectFlags |= RF_Transactional;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location), SpawnParams);
	if (!Actor) return MCPError(TEXT("SpawnActor returned no actor"));
	USplineComponent* Spline = NewObject<USplineComponent>(Actor, TEXT("Spline"), RF_Transactional);
	Actor->SetRootComponent(Spline);
	Actor->AddInstanceComponent(Spline);
	Spline->RegisterComponent();
	Spline->SetWorldLocation(Location);
	Actor->SetActorLabel(Label);
	const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
	if (TryGetArrayParam(Params, TEXT("tags"), Tags) && Tags)
	{
		for (const TSharedPtr<FJsonValue>& Tag : *Tags)
		{
			FString TagText;
			if (Tag.IsValid() && Tag->TryGetString(TagText) && !TagText.IsEmpty()) Actor->Tags.AddUnique(FName(*TagText));
		}
	}
	TArray<FVector> WorldPoints = Points;
	if (bLocal)
	{
		for (FVector& P : WorldPoints) P = Actor->GetActorTransform().TransformPosition(P);
	}
	MCPSpgWritePoints(Spline, WorldPoints, false, PointType, bClosed);
	MCPSpawnFinalize::FinishEditorSpawn(Actor);

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), Spline->GetName());
	Result->SetNumberField(TEXT("pointCount"), Spline->GetNumberOfSplinePoints());
	Result->SetNumberField(TEXT("splineLength"), Spline->GetSplineLength());
	Result->SetBoolField(TEXT("closedLoop"), Spline->IsClosedLoop());
	Result->SetArrayField(TEXT("splinePoints"), MCPSpgWorldPoints(Spline));
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	MCPSetRollback(Result, TEXT("delete_actor"), Payload);
	return MCPResult(Result);
}

// level(build_spline_meshes): a chain of spline mesh pieces along a spline.
TSharedPtr<FJsonValue> FLevelSplineGenHandlers::BuildSplineMeshes(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentName"), TEXT("generatorName"), TEXT("staticMesh"), TEXT("spacing"),
		TEXT("forwardAxis"), TEXT("scale"), TEXT("roll"), TEXT("startOffset"), TEXT("endOffset"), TEXT("stretchToFit"),
		TEXT("collisionProfile"), TEXT("rebuild"),
	});
	REQUIRE_EDITOR_WORLD(World);

	FMCPSpgTarget Target;
	if (auto Err = MCPSpgResolveTarget(World, Params, TEXT("meshes"), TEXT("SplineMeshes"), {
		TEXT("componentName"), TEXT("staticMesh"), TEXT("spacing"), TEXT("forwardAxis"), TEXT("scale"), TEXT("roll"),
		TEXT("startOffset"), TEXT("endOffset"), TEXT("stretchToFit"), TEXT("collisionProfile"),
	}, Target)) return Err;
	const TSharedPtr<FJsonObject>& P = Target.Effective;

	const FString MeshPath = OptionalString(P, TEXT("staticMesh"));
	if (MeshPath.IsEmpty()) return MCPError(TEXT("staticMesh is required"));
	UStaticMesh* Mesh = Cast<UStaticMesh>(MCPLoadAssetObject(MeshPath));
	if (!Mesh) return MCPError(FString::Printf(TEXT("No StaticMesh at '%s'"), *MeshPath));

	ESplineMeshAxis::Type Axis = ESplineMeshAxis::X;
	const FString AxisName = OptionalString(P, TEXT("forwardAxis"), TEXT("X"));
	if (AxisName.Equals(TEXT("Y"), ESearchCase::IgnoreCase)) Axis = ESplineMeshAxis::Y;
	else if (AxisName.Equals(TEXT("Z"), ESearchCase::IgnoreCase)) Axis = ESplineMeshAxis::Z;
	else if (!AxisName.Equals(TEXT("X"), ESearchCase::IgnoreCase)) return MCPError(TEXT("forwardAxis must be X, Y or Z"));

	FVector Scale = FVector::OneVector;
	MCPSpgReadVector(P, TEXT("scale"), Scale);
	const double MeshLength = Mesh->GetBoundingBox().GetSize()[MCPSpgAxisIndex(Axis)];
	double Spacing = OptionalNumber(P, TEXT("spacing"), 0.0);
	if (Spacing <= 0.0) Spacing = MeshLength * FMath::Abs(Scale[MCPSpgAxisIndex(Axis)]);
	if (Spacing < 1.0)
	{
		return MCPError(FString::Printf(TEXT("The piece length is %.3f cm: pass spacing, or a mesh with extent along forwardAxis %s"), Spacing, *AxisName));
	}
	double Start = 0.0, End = 0.0;
	if (auto Err = MCPSpgRange(P, Target.Spline, Start, End)) return Err;
	const double Range = End - Start;
	const bool bStretch = OptionalBool(P, TEXT("stretchToFit"), true);
	const int32 Count = bStretch ? FMath::Max(1, FMath::RoundToInt(Range / Spacing)) : FMath::FloorToInt(Range / Spacing);
	if (Count < 1) return MCPError(FString::Printf(TEXT("A %.1f cm piece does not fit in %.1f cm of spline; set stretchToFit or a shorter spacing"), Spacing, Range));
	if (Count > 5000) return MCPError(FString::Printf(TEXT("%d pieces is more than the 5000 this action builds in one call; raise spacing"), Count));
	const double PieceLength = bStretch ? Range / Count : Spacing;
	const float RollRadians = FMath::DegreesToRadians(static_cast<float>(OptionalNumber(P, TEXT("roll"), 0.0)));
	const FVector2D CrossScale = MCPSpgCrossScale(Scale, Axis);
	const FString Profile = OptionalString(P, TEXT("collisionProfile"));

	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "BuildSplineMeshes", "Build Spline Meshes"));
	const FName Tag = MCPSpgOutputTag(Target.Owner, Target.Generator);
	TArray<FString> RemovedComponents, RemovedActors;
	MCPSpgRemoveOutputs(World, Target.Owner, Tag, RemovedComponents, RemovedActors);

	Target.Owner->Modify();
	TArray<FString> Created;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float D0 = static_cast<float>(Start + Index * PieceLength);
		const float D1 = static_cast<float>(Start + (Index + 1) * PieceLength);
		const FVector P0 = Target.Spline->GetLocationAtDistanceAlongSpline(D0, ESplineCoordinateSpace::Local);
		const FVector P1 = Target.Spline->GetLocationAtDistanceAlongSpline(D1, ESplineCoordinateSpace::Local);
		const FVector T0 = Target.Spline->GetDirectionAtDistanceAlongSpline(D0, ESplineCoordinateSpace::Local) * PieceLength;
		const FVector T1 = Target.Spline->GetDirectionAtDistanceAlongSpline(D1, ESplineCoordinateSpace::Local) * PieceLength;

		const FName Name = MakeUniqueObjectName(Target.Owner, USplineMeshComponent::StaticClass(), FName(*FString::Printf(TEXT("%s_%d"), *Target.Generator, Index)));
		USplineMeshComponent* Piece = NewObject<USplineMeshComponent>(Target.Owner, Name, RF_Transactional);
		Piece->SetMobility(Target.Spline->GetMobility());
		Piece->SetupAttachment(Target.Spline);
		Piece->ComponentTags.Add(Tag);
		Piece->SetStaticMesh(Mesh);
		Piece->SetForwardAxis(Axis, false);
		Piece->SetStartAndEnd(P0, T0, P1, T1, false);
		Piece->SetStartScale(CrossScale, false);
		Piece->SetEndScale(CrossScale, false);
		Piece->SetStartRoll(RollRadians, false);
		Piece->SetEndRoll(RollRadians, false);
		if (!Profile.IsEmpty()) Piece->SetCollisionProfileName(FName(*Profile));
		Target.Owner->AddInstanceComponent(Piece);
		Piece->RegisterComponent();
		Piece->UpdateMesh();
		Created.Add(Piece->GetName());
	}

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("pieceCount"), Created.Num());
	Result->SetNumberField(TEXT("pieceLength"), PieceLength);
	Result->SetNumberField(TEXT("coveredLength"), PieceLength * Created.Num());
	Result->SetNumberField(TEXT("splineLength"), Target.Spline->GetSplineLength());
	Result->SetStringField(TEXT("staticMesh"), Mesh->GetPathName());
	Result->SetArrayField(TEXT("components"), MCPStringListToJson(Created));
	Result->SetArrayField(TEXT("removedComponents"), MCPStringListToJson(RemovedComponents));
	Result->SetArrayField(TEXT("removedActors"), MCPStringListToJson(RemovedActors));
	MCPSpgFinish(Target, TEXT("build_spline_meshes"), RemovedComponents.Num() + RemovedActors.Num(), Result);
	return MCPResult(Result);
}

// level(scatter_along_spline): meshes placed along a spline as instances or actors.
TSharedPtr<FJsonValue> FLevelSplineGenHandlers::ScatterAlongSpline(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("actorLabel"), TEXT("actorPath"), TEXT("componentName"), TEXT("generatorName"), TEXT("meshes"), TEXT("weights"),
		TEXT("mode"), TEXT("interval"), TEXT("count"), TEXT("startOffset"), TEXT("endOffset"), TEXT("offset"), TEXT("positionJitter"),
		TEXT("yawJitter"), TEXT("scaleMin"), TEXT("scaleMax"), TEXT("alignToSpline"), TEXT("seed"), TEXT("maxCount"), TEXT("rebuild"),
	});
	REQUIRE_EDITOR_WORLD(World);

	FMCPSpgTarget Target;
	if (auto Err = MCPSpgResolveTarget(World, Params, TEXT("scatter"), TEXT("SplineScatter"), {
		TEXT("componentName"), TEXT("meshes"), TEXT("weights"), TEXT("mode"), TEXT("interval"), TEXT("count"), TEXT("startOffset"),
		TEXT("endOffset"), TEXT("offset"), TEXT("positionJitter"), TEXT("yawJitter"), TEXT("scaleMin"), TEXT("scaleMax"), TEXT("alignToSpline"),
		TEXT("seed"),
	}, Target)) return Err;
	const TSharedPtr<FJsonObject>& P = Target.Effective;

	TArray<UStaticMesh*> Meshes;
	const TArray<TSharedPtr<FJsonValue>>* MeshRows = nullptr;
	if (!TryGetArrayParam(P, TEXT("meshes"), MeshRows) || !MeshRows || MeshRows->Num() == 0)
	{
		return MCPError(TEXT("meshes is required: one or more StaticMesh paths"));
	}
	for (int32 Index = 0; Index < MeshRows->Num(); ++Index)
	{
		FString MeshPath;
		(*MeshRows)[Index]->TryGetString(MeshPath);
		UStaticMesh* Mesh = Cast<UStaticMesh>(MCPLoadAssetObject(MeshPath));
		if (!Mesh) return MCPError(FString::Printf(TEXT("meshes[%d]: no StaticMesh at '%s'"), Index, *MeshPath));
		Meshes.Add(Mesh);
	}
	TArray<double> Weights;
	const TArray<TSharedPtr<FJsonValue>>* WeightRows = nullptr;
	if (TryGetArrayParam(P, TEXT("weights"), WeightRows) && WeightRows && WeightRows->Num() > 0)
	{
		if (WeightRows->Num() != Meshes.Num()) return MCPError(TEXT("weights needs one entry per mesh"));
		for (const TSharedPtr<FJsonValue>& Row : *WeightRows)
		{
			double W = 0.0;
			if (!Row.IsValid() || !Row->TryGetNumber(W) || W < 0.0) return MCPError(TEXT("weights must be numbers of zero or more"));
			Weights.Add(W);
		}
	}
	else
	{
		Weights.Init(1.0, Meshes.Num());
	}
	double TotalWeight = 0.0;
	for (double W : Weights) TotalWeight += W;
	if (TotalWeight <= 0.0) return MCPError(TEXT("weights add up to zero"));

	const FString Mode = OptionalString(P, TEXT("mode"), TEXT("instances"));
	if (Mode != TEXT("instances") && Mode != TEXT("actors")) return MCPError(TEXT("mode must be instances or actors"));

	double Start = 0.0, End = 0.0;
	if (auto Err = MCPSpgRange(P, Target.Spline, Start, End)) return Err;
	const double Range = End - Start;
	const bool bClosed = Target.Spline->IsClosedLoop();
	TArray<double> Distances;
	const int32 Count = OptionalInt(P, TEXT("count"), 0);
	const int32 MaxCount = FMath::Max(1, OptionalInt(Params, TEXT("maxCount"), 10000));
	if (Count > 0)
	{
		if (Count > MaxCount) return MCPError(FString::Printf(TEXT("count %d is above maxCount %d"), Count, MaxCount));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Count == 1) Distances.Add(Start + Range * 0.5);
			else Distances.Add(Start + Range * Index / (bClosed ? Count : Count - 1));
		}
	}
	else
	{
		const double Interval = OptionalNumber(P, TEXT("interval"), 200.0);
		if (Interval < 1.0) return MCPError(TEXT("interval must be at least 1 cm"));
		const int64 Expected = static_cast<int64>(Range / Interval) + 1;
		if (Expected > MaxCount) return MCPError(FString::Printf(TEXT("interval %.1f over %.1f cm places %lld, above maxCount %d"), Interval, Range, Expected, MaxCount));
		for (double D = Start; D <= End + KINDA_SMALL_NUMBER; D += Interval)
		{
			// A closed loop's end is its start; do not place twice there.
			if (bClosed && Distances.Num() > 0 && FMath::IsNearlyEqual(D, Start + Target.Spline->GetSplineLength(), 0.5)) break;
			Distances.Add(D);
		}
	}

	FVector Offset = FVector::ZeroVector, Jitter = FVector::ZeroVector;
	MCPSpgReadVector(P, TEXT("offset"), Offset);
	MCPSpgReadVector(P, TEXT("positionJitter"), Jitter);
	const double YawJitter = FMath::Abs(OptionalNumber(P, TEXT("yawJitter"), 0.0));
	const double ScaleMin = OptionalNumber(P, TEXT("scaleMin"), 1.0);
	const double ScaleMax = OptionalNumber(P, TEXT("scaleMax"), ScaleMin);
	if (ScaleMin <= 0.0 || ScaleMax < ScaleMin) return MCPError(TEXT("scaleMin must be above zero and scaleMax at least scaleMin"));
	const bool bAlign = OptionalBool(P, TEXT("alignToSpline"), true);
	FRandomStream Stream(OptionalInt(P, TEXT("seed"), 0));

	// Placements, grouped by mesh.
	TArray<TArray<FTransform>> PerMesh;
	PerMesh.SetNum(Meshes.Num());
	for (const double D : Distances)
	{
		double Pick = Stream.FRandRange(0.0, TotalWeight);
		int32 MeshIndex = 0;
		for (; MeshIndex < Weights.Num() - 1; ++MeshIndex)
		{
			if (Pick < Weights[MeshIndex]) break;
			Pick -= Weights[MeshIndex];
		}
		const FVector J(Stream.FRandRange(-Jitter.X, Jitter.X), Stream.FRandRange(-Jitter.Y, Jitter.Y), Stream.FRandRange(-Jitter.Z, Jitter.Z));
		const double Yaw = YawJitter > 0.0 ? Stream.FRandRange(-YawJitter, YawJitter) : 0.0;
		const double UniformScale = ScaleMax > ScaleMin ? Stream.FRandRange(ScaleMin, ScaleMax) : ScaleMin;
		const float Along = static_cast<float>(D);
		const FVector Base = Target.Spline->GetLocationAtDistanceAlongSpline(Along, ESplineCoordinateSpace::World);
		const FVector Forward = Target.Spline->GetDirectionAtDistanceAlongSpline(Along, ESplineCoordinateSpace::World);
		const FVector Right = Target.Spline->GetRightVectorAtDistanceAlongSpline(Along, ESplineCoordinateSpace::World);
		const FVector Up = Target.Spline->GetUpVectorAtDistanceAlongSpline(Along, ESplineCoordinateSpace::World);
		const FVector Location = Base + Forward * (Offset.X + J.X) + Right * (Offset.Y + J.Y) + Up * (Offset.Z + J.Z);
		const FQuat BaseRotation = bAlign ? Target.Spline->GetRotationAtDistanceAlongSpline(Along, ESplineCoordinateSpace::World).Quaternion() : FQuat::Identity;
		const FQuat Rotation = BaseRotation * FRotator(0.0, Yaw, 0.0).Quaternion();
		PerMesh[MeshIndex].Add(FTransform(Rotation, Location, FVector(UniformScale)));
	}

	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "ScatterAlongSpline", "Scatter Along Spline"));
	const FName Tag = MCPSpgOutputTag(Target.Owner, Target.Generator);
	TArray<FString> RemovedComponents, RemovedActors;
	MCPSpgRemoveOutputs(World, Target.Owner, Tag, RemovedComponents, RemovedActors);

	TArray<TSharedPtr<FJsonValue>> PerMeshRows;
	TArray<FString> Components, Actors;
	int32 Placed = 0;
	Target.Owner->Modify();
	for (int32 MeshIndex = 0; MeshIndex < Meshes.Num(); ++MeshIndex)
	{
		const TArray<FTransform>& Transforms = PerMesh[MeshIndex];
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("mesh"), Meshes[MeshIndex]->GetPathName());
		Row->SetNumberField(TEXT("count"), Transforms.Num());
		if (Transforms.Num() > 0 && Mode == TEXT("instances"))
		{
			const FName Name = MakeUniqueObjectName(Target.Owner, UHierarchicalInstancedStaticMeshComponent::StaticClass(), FName(*FString::Printf(TEXT("%s_%d"), *Target.Generator, MeshIndex)));
			UHierarchicalInstancedStaticMeshComponent* Instances = NewObject<UHierarchicalInstancedStaticMeshComponent>(Target.Owner, Name, RF_Transactional);
			Instances->SetMobility(Target.Spline->GetMobility());
			Instances->SetupAttachment(Target.Spline);
			Instances->ComponentTags.Add(Tag);
			Instances->SetStaticMesh(Meshes[MeshIndex]);
			Target.Owner->AddInstanceComponent(Instances);
			Instances->RegisterComponent();
			Instances->AddInstances(Transforms, false, true);
			Components.Add(Instances->GetName());
			Row->SetStringField(TEXT("componentName"), Instances->GetName());
		}
		else if (Mode == TEXT("actors"))
		{
			TArray<TSharedPtr<FJsonValue>> Paths;
			for (const FTransform& Transform : Transforms)
			{
				FActorSpawnParameters SpawnParams;
				SpawnParams.OverrideLevel = Target.Owner->GetLevel();
				SpawnParams.ObjectFlags |= RF_Transactional;
				AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, SpawnParams);
				if (!Actor) continue;
				Actor->SetActorScale3D(Transform.GetScale3D());
				Actor->GetStaticMeshComponent()->SetStaticMesh(Meshes[MeshIndex]);
				Actor->Tags.Add(Tag);
				Actor->SetActorLabel(FString::Printf(TEXT("%s_%d"), *Target.Generator, Placed + Paths.Num()));
				MCPSpawnFinalize::FinishEditorSpawn(Actor);
				Actors.Add(Actor->GetPathName());
				Paths.Add(MakeShared<FJsonValueString>(Actor->GetPathName()));
			}
			Row->SetArrayField(TEXT("actorPaths"), Paths);
		}
		Placed += Transforms.Num();
		PerMeshRows.Add(MakeShared<FJsonValueObject>(Row));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetNumberField(TEXT("placed"), Placed);
	Result->SetArrayField(TEXT("perMesh"), PerMeshRows);
	Result->SetArrayField(TEXT("components"), MCPStringListToJson(Components));
	Result->SetArrayField(TEXT("actors"), MCPStringListToJson(Actors));
	Result->SetArrayField(TEXT("removedComponents"), MCPStringListToJson(RemovedComponents));
	Result->SetArrayField(TEXT("removedActors"), MCPStringListToJson(RemovedActors));
	MCPSpgFinish(Target, TEXT("scatter_along_spline"), RemovedComponents.Num() + RemovedActors.Num(), Result);
	return MCPResult(Result);
}

// level(clear_spline_generated): remove a generator's output and its stored parameters.
TSharedPtr<FJsonValue> FLevelSplineGenHandlers::ClearSplineGenerated(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("actorLabel"), TEXT("actorPath"), TEXT("generatorName") });
	REQUIRE_EDITOR_WORLD(World);
	FString Generator;
	if (auto Err = RequireString(Params, TEXT("generatorName"), Generator)) return Err;
	TSharedPtr<FJsonValue> Err;
	AActor* Owner = MCPResolveActor(World, Params, Err);
	if (!Owner) return Err;

	const FString Prefix = MCPSpgParamsPrefix(Generator);
	const TSharedPtr<FJsonObject> Stored = MCPLevelRecords::Read(Owner, Prefix);
	FScopedTransaction Transaction(NSLOCTEXT("UEMCP", "ClearSplineGenerated", "Clear Spline Generator Output"));
	TArray<FString> RemovedComponents, RemovedActors;
	MCPSpgRemoveOutputs(World, Owner, MCPSpgOutputTag(Owner, Generator), RemovedComponents, RemovedActors);
	Owner->Modify();
	const bool bHadParams = MCPLevelRecords::Remove(Owner, Prefix);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("actorLabel"), Owner->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Owner->GetPathName());
	Result->SetStringField(TEXT("generatorName"), Generator);
	Result->SetArrayField(TEXT("removedComponents"), MCPStringListToJson(RemovedComponents));
	Result->SetArrayField(TEXT("removedActors"), MCPStringListToJson(RemovedActors));
	Result->SetBoolField(TEXT("removedParams"), bHadParams);
	const bool bChanged = bHadParams || RemovedComponents.Num() > 0 || RemovedActors.Num() > 0;
	Result->SetBoolField(TEXT("deleted"), bChanged);
	Result->SetBoolField(TEXT("alreadyDeleted"), !bChanged);
	if (Stored.IsValid())
	{
		FString Kind;
		Stored->TryGetStringField(TEXT("kind"), Kind);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Stored->Values)
		{
			if (Pair.Key != TEXT("kind")) Payload->SetField(Pair.Key, Pair.Value);
		}
		Payload->SetStringField(TEXT("actorPath"), Owner->GetPathName());
		Payload->SetStringField(TEXT("generatorName"), Generator);
		MCPSetRollback(Result, Kind == TEXT("scatter") ? TEXT("scatter_along_spline") : TEXT("build_spline_meshes"), Payload);
	}
	else if (bChanged)
	{
		MCPSetNoRollback(Result, TEXT("No parameters were stored for this generator, so there is nothing to rebuild the removed output from."));
	}
	return MCPResult(Result);
}
