// Bounded batch projection for ISMC/HISMC instances.
// Registration stays in LevelHandlers.cpp::RegisterHandlers.

#include "LevelHandlers.h"
#include "LevelHandlers_InstanceProjection_Internal.h"
#include "HandlerUtils.h"
#include "Editor.h"
#include "ScopedTransaction.h"
#include "CollisionQueryParams.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"

namespace UEMCPInstanceProjection
{
	bool ParseMissPolicy(const FString& InValue, EMissPolicy& OutPolicy)
	{
		const FString Value = InValue.TrimStartAndEnd();
		if (Value.Equals(TEXT("error"), ESearchCase::IgnoreCase))
		{
			OutPolicy = EMissPolicy::Error;
			return true;
		}
		if (Value.Equals(TEXT("skip"), ESearchCase::IgnoreCase))
		{
			OutPolicy = EMissPolicy::Skip;
			return true;
		}
		return false;
	}

	FVector ApplySurfaceOffset(const FVector& ImpactPoint, const FVector& ImpactNormal, double SurfaceOffset)
	{
		return ImpactPoint + ImpactNormal.GetSafeNormal() * SurfaceOffset;
	}
}

namespace
{
	using namespace UEMCPInstanceProjection;

	UInstancedStaticMeshComponent* ResolveProjectionComponent(AActor* Actor, const FString& ComponentName, FString& OutError)
	{
		if (!Actor)
		{
			OutError = TEXT("Target actor is null");
			return nullptr;
		}

		TArray<UInstancedStaticMeshComponent*> Matches;
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (UInstancedStaticMeshComponent* ISMC = Cast<UInstancedStaticMeshComponent>(Component))
			{
				if (ComponentName.IsEmpty() || ISMC->GetName() == ComponentName)
				{
					Matches.Add(ISMC);
				}
			}
		}

		if (Matches.Num() == 1) return Matches[0];
		if (Matches.IsEmpty())
		{
			OutError = ComponentName.IsEmpty()
				? FString::Printf(TEXT("Actor '%s' has no InstancedStaticMeshComponent"), *Actor->GetActorLabel())
				: FString::Printf(TEXT("Actor '%s' has no InstancedStaticMeshComponent named '%s'"), *Actor->GetActorLabel(), *ComponentName);
			return nullptr;
		}

		TArray<FString> Names;
		for (const UInstancedStaticMeshComponent* Match : Matches) Names.Add(Match->GetName());
		OutError = FString::Printf(
			TEXT("Actor '%s' has %d instanced mesh components (%s); pass componentName to select exactly one"),
			*Actor->GetActorLabel(), Matches.Num(), *FString::Join(Names, TEXT(", ")));
		return nullptr;
	}

	bool ResolveProjectionTraceChannel(const FString& InName, ECollisionChannel& OutChannel, FString& OutResolvedName)
	{
		FString Name = InName.TrimStartAndEnd();
		if (Name.StartsWith(TEXT("ECC_"))) Name = Name.RightChop(4);
		if (Name.IsEmpty()) Name = TEXT("Visibility");

		if (const UCollisionProfile* Profile = UCollisionProfile::Get())
		{
			for (int32 Index = 0; Index < ECC_MAX; ++Index)
			{
				const FName ChannelName = Profile->ReturnChannelNameFromContainerIndex(Index);
				if (!ChannelName.IsNone() && ChannelName.ToString().Equals(Name, ESearchCase::IgnoreCase))
				{
					OutChannel = static_cast<ECollisionChannel>(Index);
					OutResolvedName = ChannelName.ToString();
					return true;
				}
			}
		}

		struct FBuiltInChannel { const TCHAR* Name; ECollisionChannel Channel; };
		static const FBuiltInChannel BuiltIns[] = {
			{ TEXT("WorldStatic"), ECC_WorldStatic }, { TEXT("WorldDynamic"), ECC_WorldDynamic },
			{ TEXT("Pawn"), ECC_Pawn }, { TEXT("Visibility"), ECC_Visibility },
			{ TEXT("Camera"), ECC_Camera }, { TEXT("PhysicsBody"), ECC_PhysicsBody },
			{ TEXT("Vehicle"), ECC_Vehicle }, { TEXT("Destructible"), ECC_Destructible },
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

	TSharedPtr<FJsonValue> ParseInstanceIndices(
		const TSharedPtr<FJsonObject>& Params,
		int32 InstanceCount,
		int32 MaxInstances,
		TArray<int32>& OutIndices)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Params->HasField(TEXT("instanceIndices")))
		{
			if (InstanceCount > MaxInstances)
			{
				return MCPError(FString::Printf(
					TEXT("Refusing to project all %d instances because maxInstances is %d. Pass a bounded instanceIndices subset or explicitly raise maxInstances up to %d."),
					InstanceCount, MaxInstances, HardMaxInstances));
			}
			OutIndices.Reserve(InstanceCount);
			for (int32 Index = 0; Index < InstanceCount; ++Index) OutIndices.Add(Index);
			return nullptr;
		}
		if (!Params->TryGetArrayField(TEXT("instanceIndices"), Values) || !Values)
		{
			return MCPError(TEXT("instanceIndices must be an array of integers"));
		}

		if (Values->IsEmpty()) return MCPError(TEXT("instanceIndices must not be empty"));
		if (Values->Num() > MaxInstances)
		{
			return MCPError(FString::Printf(
				TEXT("Refusing to project %d requested instances because maxInstances is %d. Raise maxInstances only for an intentionally bounded batch (hard limit %d)."),
				Values->Num(), MaxInstances, HardMaxInstances));
		}
		TSet<int32> Seen;
		OutIndices.Reserve(Values->Num());
		for (int32 Position = 0; Position < Values->Num(); ++Position)
		{
			double Raw = 0.0;
			if (!(*Values)[Position].IsValid()
				|| !(*Values)[Position]->TryGetNumber(Raw)
				|| !FMath::IsFinite(Raw)
				|| Raw != FMath::RoundToDouble(Raw))
			{
				return MCPError(FString::Printf(TEXT("instanceIndices[%d] must be an integer"), Position));
			}
			const int64 WideIndex = FMath::RoundToInt64(Raw);
			if (WideIndex < 0 || WideIndex >= InstanceCount)
			{
				return MCPError(FString::Printf(
					TEXT("instanceIndices[%d] is %lld, outside the valid range 0..%d"),
					Position, WideIndex, InstanceCount - 1));
			}
			const int32 Index = static_cast<int32>(WideIndex);
			if (Seen.Contains(Index))
			{
				return MCPError(FString::Printf(TEXT("instanceIndices contains duplicate index %d"), Index));
			}
			Seen.Add(Index);
			OutIndices.Add(Index);
		}
		OutIndices.Sort();
		return nullptr;
	}

	bool HitMatchesSurface(
		const FHitResult& Hit,
		UClass* RequiredClass,
		const TSet<FString>& RequiredLabels)
	{
		AActor* HitActor = Hit.GetActor();
		if (!HitActor) return false;
		if (RequiredClass && !HitActor->IsA(RequiredClass)) return false;
		if (!RequiredLabels.IsEmpty() && !RequiredLabels.Contains(HitActor->GetActorLabel())) return false;
		return true;
	}

	bool FindMatchingSurfaceHit(
		UWorld* World,
		const FVector& Start,
		const FVector& End,
		ECollisionChannel Channel,
		bool bTraceComplex,
		AActor* SourceActor,
		UClass* RequiredClass,
		const TSet<FString>& RequiredLabels,
		FHitResult& OutHit,
		bool& bOutRejectedHit,
		bool& bOutReachedFilterLimit)
	{
		bOutRejectedHit = false;
		bOutReachedFilterLimit = false;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(MCPSnapInstancesToSurface), bTraceComplex);
		Query.bReturnFaceIndex = bTraceComplex;
		Query.AddIgnoredActor(SourceActor);

		// A channel trace stops at its closest blocking hit. Iteratively ignore a
		// rejected blocker so a requested landscape or named surface behind it can
		// still be reached without executing an unbounded trace loop.
		for (int32 Attempt = 0; Attempt < MaxFilteredSurfaceHits; ++Attempt)
		{
			FHitResult Candidate;
			if (!World->LineTraceSingleByChannel(Candidate, Start, End, Channel, Query))
			{
				return false;
			}
			if (HitMatchesSurface(Candidate, RequiredClass, RequiredLabels))
			{
				OutHit = Candidate;
				return true;
			}

			bOutRejectedHit = true;
			if (AActor* RejectedActor = Candidate.GetActor())
			{
				Query.AddIgnoredActor(RejectedActor);
			}
			else if (UPrimitiveComponent* RejectedComponent = Candidate.GetComponent())
			{
				Query.AddIgnoredComponent(RejectedComponent);
			}
			else
			{
				return false;
			}
		}

		bOutReachedFilterLimit = true;
		return false;
	}

	struct FProjectionRun
	{
		int32 StartIndex = INDEX_NONE;
		TArray<FTransform> Before;
		TArray<FTransform> After;
	};

	TArray<FProjectionRun> BuildProjectionRuns(const FProjectionPlan& Plan)
	{
		TArray<FProjectionRun> Runs;
		for (const FProjectionPlanEntry& Entry : Plan.Entries)
		{
			if (!Entry.bHit) continue;
			if (Runs.IsEmpty() || Entry.InstanceIndex != Runs.Last().StartIndex + Runs.Last().After.Num())
			{
				FProjectionRun& NewRun = Runs.AddDefaulted_GetRef();
				NewRun.StartIndex = Entry.InstanceIndex;
			}
			Runs.Last().Before.Add(Entry.Before);
			Runs.Last().After.Add(Entry.After);
		}
		return Runs;
	}

	TSharedPtr<FJsonObject> MakeProjectionSample(const FProjectionPlanEntry& Entry)
	{
		TSharedPtr<FJsonObject> Sample = MakeShared<FJsonObject>();
		Sample->SetNumberField(TEXT("index"), Entry.InstanceIndex);
		Sample->SetBoolField(TEXT("hit"), Entry.bHit);
		Sample->SetObjectField(TEXT("from"), MCPVec3ToJsonObject(Entry.Before.GetLocation()));
		if (Entry.bHit)
		{
			Sample->SetObjectField(TEXT("to"), MCPVec3ToJsonObject(Entry.After.GetLocation()));
			Sample->SetStringField(TEXT("hitActorLabel"), Entry.HitActorLabel);
			Sample->SetStringField(TEXT("hitActorClass"), Entry.HitActorClass);
		}
		else
		{
			Sample->SetStringField(TEXT("missReason"), Entry.MissReason);
		}
		return Sample;
	}
}

TSharedPtr<FJsonValue> UEMCPInstanceProjection::SnapInstancesToSurfaceInWorld(
	UWorld* World,
	const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCPInstanceProjection;
	if (!World) return MCPError(TEXT("World is not available"));
	if (!Params) return MCPError(TEXT("params must be an object"));

	FString ActorLabel;
	if (TSharedPtr<FJsonValue> Error = RequireString(Params, TEXT("actorLabel"), ActorLabel)) return Error;
	AActor* Actor = FindActorByLabel(World, ActorLabel);
	if (!Actor) return MCPError(FString::Printf(TEXT("Actor not found: %s"), *ActorLabel));

	FString ComponentName;
	if (Params->HasField(TEXT("componentName"))
		&& !Params->TryGetStringField(TEXT("componentName"), ComponentName))
	{
		return MCPError(TEXT("componentName must be a string"));
	}
	FString ComponentError;
	UInstancedStaticMeshComponent* ISMC = ResolveProjectionComponent(
		Actor, ComponentName, ComponentError);
	if (!ISMC) return MCPError(ComponentError);

	const int32 InstanceCount = ISMC->GetInstanceCount();
	if (InstanceCount <= 0) return MCPError(FString::Printf(TEXT("Component '%s' has no instances"), *ISMC->GetName()));

	int32 MaxInstances = DefaultMaxInstances;
	if (Params->HasField(TEXT("maxInstances")))
	{
		double RawMaxInstances = 0.0;
		if (!Params->TryGetNumberField(TEXT("maxInstances"), RawMaxInstances)
			|| !FMath::IsFinite(RawMaxInstances)
			|| RawMaxInstances != FMath::RoundToDouble(RawMaxInstances))
		{
			return MCPError(TEXT("maxInstances must be an integer"));
		}
		if (RawMaxInstances < 1.0 || RawMaxInstances > HardMaxInstances)
		{
			return MCPError(FString::Printf(
				TEXT("maxInstances must be between 1 and %d (default %d)"),
				HardMaxInstances, DefaultMaxInstances));
		}
		MaxInstances = static_cast<int32>(RawMaxInstances);
	}

	TArray<int32> Indices;
	if (TSharedPtr<FJsonValue> Error = ParseInstanceIndices(
		Params, InstanceCount, MaxInstances, Indices))
	{
		return Error;
	}
	if (Indices.Num() > MaxInstances)
	{
		return MCPError(FString::Printf(
			TEXT("Refusing to project %d instances because maxInstances is %d. Pass a bounded instanceIndices subset or explicitly raise maxInstances up to %d."),
			Indices.Num(), MaxInstances, HardMaxInstances));
	}

	FVector Direction(0.0, 0.0, -1.0);
	if (Params->HasField(TEXT("direction")))
	{
		if (TSharedPtr<FJsonValue> Error = RequireVec3(Params, TEXT("direction"), Direction)) return Error;
	}
	if (Direction.ContainsNaN() || !Direction.Normalize())
	{
		return MCPError(TEXT("direction must be a finite, non-zero vector"));
	}
	double TraceStartOffset = 1000.0;
	double TraceDistance = 200000.0;
	double SurfaceOffset = 0.0;
	if (Params->HasField(TEXT("traceStartOffset"))
		&& !Params->TryGetNumberField(TEXT("traceStartOffset"), TraceStartOffset))
	{
		return MCPError(TEXT("traceStartOffset must be a number"));
	}
	if (Params->HasField(TEXT("traceDistance"))
		&& !Params->TryGetNumberField(TEXT("traceDistance"), TraceDistance))
	{
		return MCPError(TEXT("traceDistance must be a number"));
	}
	if (Params->HasField(TEXT("surfaceOffset"))
		&& !Params->TryGetNumberField(TEXT("surfaceOffset"), SurfaceOffset))
	{
		return MCPError(TEXT("surfaceOffset must be a number"));
	}
	if (!FMath::IsFinite(TraceStartOffset) || TraceStartOffset < 0.0)
	{
		return MCPError(TEXT("traceStartOffset must be a finite number greater than or equal to zero"));
	}
	if (!FMath::IsFinite(TraceDistance) || TraceDistance <= 0.0)
	{
		return MCPError(TEXT("traceDistance must be a finite number greater than zero"));
	}
	if (!FMath::IsFinite(SurfaceOffset)) return MCPError(TEXT("surfaceOffset must be finite"));

	EMissPolicy MissPolicy = EMissPolicy::Error;
	FString MissPolicyText = TEXT("error");
	if (Params->HasField(TEXT("onMiss"))
		&& !Params->TryGetStringField(TEXT("onMiss"), MissPolicyText))
	{
		return MCPError(TEXT("onMiss must be a string"));
	}
	if (!ParseMissPolicy(MissPolicyText, MissPolicy))
	{
		return MCPError(FString::Printf(TEXT("Unknown onMiss policy '%s'; expected 'error' or 'skip'"), *MissPolicyText));
	}

	UClass* SurfaceClass = nullptr;
	FString SurfaceClassSpec;
	if (Params->HasField(TEXT("surfaceActorClass"))
		&& !Params->TryGetStringField(TEXT("surfaceActorClass"), SurfaceClassSpec))
	{
		return MCPError(TEXT("surfaceActorClass must be a string"));
	}
	if (!SurfaceClassSpec.IsEmpty())
	{
		SurfaceClass = MCPResolveClassOfType(SurfaceClassSpec, AActor::StaticClass());
		if (!SurfaceClass) return MCPClassNotFoundError(SurfaceClassSpec, TEXT("surfaceActorClass"));
	}

	TSet<FString> SurfaceActorLabels;
	const TArray<TSharedPtr<FJsonValue>>* LabelValues = nullptr;
	if (Params->HasField(TEXT("surfaceActorLabels"))
		&& (!Params->TryGetArrayField(TEXT("surfaceActorLabels"), LabelValues) || !LabelValues))
	{
		return MCPError(TEXT("surfaceActorLabels must be an array of non-empty strings"));
	}
	if (LabelValues && LabelValues->IsEmpty())
	{
		return MCPError(TEXT("surfaceActorLabels must not be empty when provided; omit it for no label filter"));
	}
	if (LabelValues)
	{
		for (int32 Position = 0; Position < LabelValues->Num(); ++Position)
		{
			FString Label;
			if (!(*LabelValues)[Position].IsValid() || !(*LabelValues)[Position]->TryGetString(Label))
			{
				return MCPError(FString::Printf(TEXT("surfaceActorLabels[%d] must be a non-empty string"), Position));
			}
			Label = Label.TrimStartAndEnd();
			if (Label.IsEmpty())
			{
				return MCPError(FString::Printf(TEXT("surfaceActorLabels[%d] must be a non-empty string"), Position));
			}
			SurfaceActorLabels.Add(Label);
		}
	}

	ECollisionChannel Channel = ECC_Visibility;
	FString ChannelName;
	FString RequestedChannel = TEXT("Visibility");
	if (Params->HasField(TEXT("channel"))
		&& !Params->TryGetStringField(TEXT("channel"), RequestedChannel))
	{
		return MCPError(TEXT("channel must be a string"));
	}
	if (!ResolveProjectionTraceChannel(RequestedChannel, Channel, ChannelName))
	{
		return MCPError(FString::Printf(TEXT("Unknown collision channel '%s'"), *RequestedChannel));
	}
	bool bTraceComplex = false;
	bool bDryRun = true;
	if (Params->HasField(TEXT("traceComplex"))
		&& !Params->TryGetBoolField(TEXT("traceComplex"), bTraceComplex))
	{
		return MCPError(TEXT("traceComplex must be a boolean"));
	}
	if (Params->HasField(TEXT("dryRun"))
		&& !Params->TryGetBoolField(TEXT("dryRun"), bDryRun))
	{
		return MCPError(TEXT("dryRun must be a boolean"));
	}

	FProjectionPlan Plan;
	Plan.Entries.Reserve(Indices.Num());
	for (const int32 Index : Indices)
	{
		FProjectionPlanEntry Entry;
		Entry.InstanceIndex = Index;
		if (!ISMC->GetInstanceTransform(Index, Entry.Before, true))
		{
			return MCPError(FString::Printf(TEXT("Failed to read instance transform at index %d"), Index));
		}
		if (Entry.Before.ContainsNaN())
		{
			return MCPError(FString::Printf(TEXT("Instance transform at index %d is not finite"), Index));
		}

		const FVector Start = Entry.Before.GetLocation() - Direction * TraceStartOffset;
		const FVector End = Start + Direction * (TraceStartOffset + TraceDistance);
		if (Start.ContainsNaN() || End.ContainsNaN())
		{
			return MCPError(FString::Printf(
				TEXT("Trace bounds overflowed for instance %d; reduce traceStartOffset or traceDistance"),
				Index));
		}
		FHitResult AcceptedHit;
		bool bRejectedHit = false;
		bool bReachedFilterLimit = false;
		if (!FindMatchingSurfaceHit(
			World,
			Start,
			End,
			Channel,
			bTraceComplex,
			Actor,
			SurfaceClass,
			SurfaceActorLabels,
			AcceptedHit,
			bRejectedHit,
			bReachedFilterLimit))
		{
			Entry.MissReason = bReachedFilterLimit
				? TEXT("surface_filter_limit_reached")
				: (bRejectedHit ? TEXT("no_hit_matched_surface_filter") : TEXT("trace_missed"));
			++Plan.MissCount;
		}
		else
		{
			Entry.bHit = true;
			Entry.After = Entry.Before;
			Entry.After.SetLocation(ApplySurfaceOffset(AcceptedHit.ImpactPoint, AcceptedHit.ImpactNormal, SurfaceOffset));
			if (Entry.After.ContainsNaN())
			{
				return MCPError(FString::Printf(
					TEXT("Projected transform overflowed for instance %d; reduce surfaceOffset"),
					Index));
			}
			if (AActor* HitActor = AcceptedHit.GetActor())
			{
				Entry.HitActorLabel = HitActor->GetActorLabel();
				Entry.HitActorClass = HitActor->GetClass()->GetPathName();
			}
			++Plan.HitCount;
			Plan.MinDistance = FMath::Min(Plan.MinDistance, static_cast<double>(AcceptedHit.Distance));
			Plan.MaxDistance = FMath::Max(Plan.MaxDistance, static_cast<double>(AcceptedHit.Distance));
		}
		Plan.Entries.Add(MoveTemp(Entry));
	}

	TArray<TSharedPtr<FJsonValue>> Samples;
	const int32 SampleCount = FMath::Min(Plan.Entries.Num(), MaxReportedSamples);
	Samples.Reserve(SampleCount);
	for (int32 Position = 0; Position < SampleCount; ++Position)
	{
		Samples.Add(MakeShared<FJsonValueObject>(MakeProjectionSample(Plan.Entries[Position])));
	}

	if (MissPolicy == EMissPolicy::Error && Plan.MissCount > 0)
	{
		auto Result = MCPSuccess();
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Preflight found %d misses; onMiss='error' prevents any mutation."), Plan.MissCount));
		Result->SetBoolField(TEXT("dryRun"), bDryRun);
		Result->SetBoolField(TEXT("preflightPassed"), false);
		Result->SetBoolField(TEXT("mutationPerformed"), false);
		Result->SetNumberField(TEXT("requestedCount"), Plan.Entries.Num());
		Result->SetNumberField(TEXT("hitCount"), Plan.HitCount);
		Result->SetNumberField(TEXT("missCount"), Plan.MissCount);
		Result->SetNumberField(TEXT("sampleCount"), Samples.Num());
		Result->SetBoolField(TEXT("samplesTruncated"), Plan.Entries.Num() > Samples.Num());
		Result->SetArrayField(TEXT("samples"), Samples);
		return MCPResult(Result);
	}

	bool bMutationPerformed = false;
	if (!bDryRun && Plan.HitCount > 0)
	{
		const bool bShouldActuallyTransact = GEditor && World->WorldType == EWorldType::Editor;
		FScopedTransaction Transaction(
			NSLOCTEXT("UEMCP", "SnapInstancesToSurface", "Snap Instanced Meshes To Surface"),
			bShouldActuallyTransact);
		UPackage* Package = ISMC->GetOutermost();
		const bool bPackageWasDirty = Package && Package->IsDirty();
		Actor->Modify();
		ISMC->Modify();
		const TArray<FProjectionRun> Runs = BuildProjectionRuns(Plan);
		int32 AppliedRunCount = 0;
		for (const FProjectionRun& Run : Runs)
		{
			if (!ISMC->BatchUpdateInstancesTransforms(
				Run.StartIndex,
				Run.After,
				true,
				false,
				true))
			{
				// Treat a false batch result as potentially partial. Restore the
				// current run as well as every previously accepted run.
				bool bRollbackSucceeded = ISMC->BatchUpdateInstancesTransforms(
					Run.StartIndex,
					Run.Before,
					true,
					false,
					true);
				for (int32 RollbackIndex = AppliedRunCount - 1; RollbackIndex >= 0; --RollbackIndex)
				{
					const FProjectionRun& AppliedRun = Runs[RollbackIndex];
					bRollbackSucceeded &= ISMC->BatchUpdateInstancesTransforms(
						AppliedRun.StartIndex,
						AppliedRun.Before,
						true,
						false,
						true);
				}
				ISMC->MarkRenderStateDirty();
				if (Package && !bPackageWasDirty) Package->ClearDirtyFlag();
				Transaction.Cancel();
				return MCPError(FString::Printf(
					TEXT("Unexpected apply failure at instance %d after a successful preflight; rollback %s"),
					Run.StartIndex,
					bRollbackSucceeded ? TEXT("succeeded") : TEXT("failed")));
			}
			++AppliedRunCount;
		}
		ISMC->MarkRenderStateDirty();
		if (Package && !Package->HasAnyPackageFlags(PKG_Transient)) ISMC->MarkPackageDirty();
		bMutationPerformed = true;
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetBoolField(TEXT("preflightPassed"), true);
	Result->SetBoolField(TEXT("mutationPerformed"), bMutationPerformed);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("componentName"), ISMC->GetName());
	Result->SetStringField(TEXT("componentClass"), ISMC->GetClass()->GetPathName());
	Result->SetStringField(TEXT("channel"), ChannelName);
	Result->SetStringField(TEXT("onMiss"), MissPolicy == EMissPolicy::Error ? TEXT("error") : TEXT("skip"));
	Result->SetNumberField(TEXT("requestedCount"), Plan.Entries.Num());
	Result->SetNumberField(TEXT("hitCount"), Plan.HitCount);
	Result->SetNumberField(TEXT("missCount"), Plan.MissCount);
	Result->SetNumberField(TEXT("updatedCount"), bMutationPerformed ? Plan.HitCount : 0);
	Result->SetNumberField(TEXT("sampleCount"), Samples.Num());
	Result->SetBoolField(TEXT("samplesTruncated"), Plan.Entries.Num() > Samples.Num());
	Result->SetArrayField(TEXT("samples"), Samples);
	if (Plan.HitCount > 0)
	{
		Result->SetNumberField(TEXT("minHitDistance"), Plan.MinDistance);
		Result->SetNumberField(TEXT("maxHitDistance"), Plan.MaxDistance);
	}
	if (bMutationPerformed)
	{
		MCPSetUpdated(Result);
		Result->SetStringField(TEXT("dirtyPackage"), ISMC->GetOutermost()->GetName());
		Result->SetStringField(TEXT("note"), TEXT("Changes are dirty in memory and were not saved."));
	}
	else if (bDryRun)
	{
		Result->SetStringField(TEXT("note"), TEXT("Dry run only; re-call with dryRun=false to apply the preflighted projection."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::SnapInstancesToSurface(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);
	return UEMCPInstanceProjection::SnapInstancesToSurfaceInWorld(World, Params);
}
