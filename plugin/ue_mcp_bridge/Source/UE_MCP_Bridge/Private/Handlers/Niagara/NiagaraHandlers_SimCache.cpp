// Per-frame particle attribute readback through UNiagaraSimCache (#1394).
// capture_niagara_sim_cache records frames off a placed component; read_niagara_sim_cache
// reports the layout, instance counts and attribute values, and never answers a
// missing name or an empty frame with a bare [].

#include "NiagaraHandlers.h"
#include "HandlerUtils.h"
#include "NiagaraComponent.h"
#include "NiagaraSimCache.h"
#include "NiagaraSystem.h"
#include "NiagaraTypes.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"

namespace
{
	// Captures live in the transient package, rooted so a later read can find
	// them. The oldest is released once this many are held.
	constexpr int32 MCPMaxHeldSimCaches = 8;
	TArray<TWeakObjectPtr<UNiagaraSimCache>> GMCPHeldSimCaches;

	void MCPHoldSimCache(UNiagaraSimCache* Cache)
	{
		GMCPHeldSimCaches.RemoveAll([](const TWeakObjectPtr<UNiagaraSimCache>& Held) { return !Held.IsValid(); });
		Cache->AddToRoot();
		GMCPHeldSimCaches.Add(Cache);
		while (GMCPHeldSimCaches.Num() > MCPMaxHeldSimCaches)
		{
			if (UNiagaraSimCache* Oldest = GMCPHeldSimCaches[0].Get())
			{
				Oldest->RemoveFromRoot();
			}
			GMCPHeldSimCaches.RemoveAt(0);
		}
	}

	struct FMCPSimCacheAttribute
	{
		FName Name;
		FString TypeName;
		int32 FloatCount = 0;
		int32 HalfCount = 0;
		int32 IntCount = 0;
		bool bPosition = false;
	};

	TArray<FMCPSimCacheAttribute> MCPSimCacheLayout(const UNiagaraSimCache* Cache, int32 EmitterIndex)
	{
		TArray<FMCPSimCacheAttribute> Out;
		Cache->ForEachEmitterAttribute(EmitterIndex, [&Out](const FNiagaraSimCacheVariable& Var)
		{
			FMCPSimCacheAttribute Attr;
			Attr.Name = Var.Variable.GetName();
			Attr.TypeName = Var.Variable.GetType().GetName();
			Attr.FloatCount = Var.FloatCount;
			Attr.HalfCount = Var.HalfCount;
			Attr.IntCount = Var.Int32Count;
			Attr.bPosition = Var.Variable.GetType() == FNiagaraTypeDefinition::GetPositionDef();
			Out.Add(Attr);
			return true;
		});
		return Out;
	}

	// The layout stores bare names. A qualified name (Particles.Age, Emitter.X)
	// falls back to its last segment, then to a case-insensitive match.
	const FMCPSimCacheAttribute* MCPFindSimCacheAttribute(const TArray<FMCPSimCacheAttribute>& Layout, const FString& Requested)
	{
		auto Find = [&Layout](const FString& Name, ESearchCase::Type Case) -> const FMCPSimCacheAttribute*
		{
			for (const FMCPSimCacheAttribute& Attr : Layout)
			{
				if (Attr.Name.ToString().Equals(Name, Case)) return &Attr;
			}
			return nullptr;
		};
		if (const FMCPSimCacheAttribute* Hit = Find(Requested, ESearchCase::CaseSensitive)) return Hit;
		FString Bare = Requested;
		int32 Dot = INDEX_NONE;
		if (Bare.FindLastChar(TEXT('.'), Dot))
		{
			Bare = Bare.Mid(Dot + 1);
			if (const FMCPSimCacheAttribute* Hit = Find(Bare, ESearchCase::CaseSensitive)) return Hit;
		}
		return Find(Bare, ESearchCase::IgnoreCase);
	}

	TArray<TSharedPtr<FJsonValue>> MCPAttributeNamesJson(const TArray<FMCPSimCacheAttribute>& Layout)
	{
		TArray<TSharedPtr<FJsonValue>> Names;
		for (const FMCPSimCacheAttribute& Attr : Layout)
		{
			Names.Add(MakeShared<FJsonValueString>(Attr.Name.ToString()));
		}
		return Names;
	}

	TSharedPtr<FJsonObject> MCPDescribeSimCacheEmitter(const UNiagaraSimCache* Cache, int32 EmitterIndex, bool bWithTypes)
	{
		TSharedPtr<FJsonObject> Emitter = MakeShared<FJsonObject>();
		Emitter->SetStringField(TEXT("name"), Cache->GetEmitterName(EmitterIndex).ToString());
		TArray<TSharedPtr<FJsonValue>> Counts;
		for (int32 Frame = 0; Frame < Cache->GetNumFrames(); ++Frame)
		{
			Counts.Add(MakeShared<FJsonValueNumber>(Cache->GetEmitterNumInstances(EmitterIndex, Frame)));
		}
		Emitter->SetArrayField(TEXT("numInstancesPerFrame"), Counts);
		const TArray<FMCPSimCacheAttribute> Layout = MCPSimCacheLayout(Cache, EmitterIndex);
		if (bWithTypes)
		{
			TArray<TSharedPtr<FJsonValue>> Attrs;
			for (const FMCPSimCacheAttribute& Attr : Layout)
			{
				TSharedPtr<FJsonObject> A = MakeShared<FJsonObject>();
				A->SetStringField(TEXT("name"), Attr.Name.ToString());
				A->SetStringField(TEXT("type"), Attr.TypeName);
				A->SetNumberField(TEXT("components"), Attr.FloatCount + Attr.HalfCount + Attr.IntCount);
				Attrs.Add(MakeShared<FJsonValueObject>(A));
			}
			Emitter->SetArrayField(TEXT("attributes"), Attrs);
		}
		else
		{
			Emitter->SetArrayField(TEXT("attributeNames"), MCPAttributeNamesJson(Layout));
		}
		return Emitter;
	}

	TArray<TSharedPtr<FJsonValue>> MCPEmitterNamesJson(const UNiagaraSimCache* Cache)
	{
		TArray<TSharedPtr<FJsonValue>> Names;
		for (const FName& Name : Cache->GetEmitterNames())
		{
			Names.Add(MakeShared<FJsonValueString>(Name.ToString()));
		}
		return Names;
	}
}

TSharedPtr<FJsonValue> FNiagaraHandlers::CaptureNiagaraSimCache(const TSharedPtr<FJsonObject>& Params)
{
	// Every parameter is read before anything can fail (#1057).
	MCPReadParamsAhead(Params, { TEXT("actorLabel"), TEXT("actorPath"), TEXT("world"), TEXT("pieInstance") });
	const FString ComponentName = OptionalString(Params, TEXT("componentName"));
	const int32 Frames = OptionalInt(Params, TEXT("frames"), 1);
	const bool bAdvance = OptionalBool(Params, TEXT("advanceSimulation"), false);
	const double TickDelta = OptionalNumber(Params, TEXT("tickDelta"), 1.0 / 60.0);
	const FString CaptureMode = OptionalString(Params, TEXT("attributeCaptureMode"), TEXT("All"));
	TArray<FString> Explicit;
	const TArray<TSharedPtr<FJsonValue>>* ExplicitJson = nullptr;
	if (TryGetArrayParam(Params, TEXT("explicitAttributes"), ExplicitJson) && ExplicitJson)
	{
		for (const TSharedPtr<FJsonValue>& V : *ExplicitJson)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) Explicit.Add(S);
		}
	}
	UWorld* World = ResolveWorldFromParams(Params);
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = World ? MCPResolveActor(World, Params, ActorErr) : nullptr;
	if (!World) return MCPError(TEXT("The requested world is not available (world=pie needs a running PIE session)"));
	if (!Actor) return ActorErr;

	if (Frames < 1 || Frames > 120) return MCPError(TEXT("frames must be between 1 and 120"));
	if (!(TickDelta > 0.0)) return MCPError(TEXT("tickDelta must be greater than 0"));

	FNiagaraSimCacheCreateParameters CreateParams;
	if (CaptureMode.Equals(TEXT("All"), ESearchCase::IgnoreCase))
	{
		CreateParams.AttributeCaptureMode = ENiagaraSimCacheAttributeCaptureMode::All;
	}
	else if (CaptureMode.Equals(TEXT("RenderingOnly"), ESearchCase::IgnoreCase))
	{
		CreateParams.AttributeCaptureMode = ENiagaraSimCacheAttributeCaptureMode::RenderingOnly;
	}
	else if (CaptureMode.Equals(TEXT("ExplicitAttributes"), ESearchCase::IgnoreCase))
	{
		if (Explicit.Num() == 0) return MCPError(TEXT("attributeCaptureMode ExplicitAttributes needs explicitAttributes, written Emitter.Particles.Attribute"));
		CreateParams.AttributeCaptureMode = ENiagaraSimCacheAttributeCaptureMode::ExplicitAttributes;
		for (const FString& Name : Explicit) CreateParams.ExplicitCaptureAttributes.Add(FName(*Name));
	}
	else
	{
		return MCPError(FString::Printf(TEXT("Unknown attributeCaptureMode '%s': All | RenderingOnly | ExplicitAttributes"), *CaptureMode));
	}

	TArray<UNiagaraComponent*> Components;
	Actor->GetComponents<UNiagaraComponent>(Components);
	UNiagaraComponent* Component = nullptr;
	for (UNiagaraComponent* Candidate : Components)
	{
		if (ComponentName.IsEmpty() || Candidate->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
		{
			Component = Candidate;
			break;
		}
	}
	if (!Component)
	{
		TArray<FString> Names;
		for (UNiagaraComponent* Candidate : Components) Names.Add(Candidate->GetName());
		return MCPError(FString::Printf(TEXT("No NiagaraComponent%s on actor '%s'. Present: [%s]"),
			ComponentName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" named '%s'"), *ComponentName),
			*Actor->GetActorLabel(), *FString::Join(Names, TEXT(", "))));
	}
	if (!Component->GetAsset()) return MCPError(TEXT("The NiagaraComponent has no system assigned"));
	if (!Component->IsActive()) return MCPError(TEXT("The NiagaraComponent is not active, so a sim cache would record nothing. A system authored this session stays inactive until it is compiled: niagara(compile) it, then niagara(reactivate) the actor and capture again."));

	UNiagaraSimCache* Cache = NewObject<UNiagaraSimCache>(GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UNiagaraSimCache::StaticClass(), TEXT("MCPSimCache")));
	if (!Cache->BeginWrite(CreateParams, Component))
	{
		return MCPError(TEXT("UNiagaraSimCache::BeginWrite refused the component (the system may not be compiled or ready)"));
	}
	int32 Written = 0;
	int32 Refused = 0;
	for (int32 Frame = 0; Frame < Frames; ++Frame)
	{
		if (bAdvance || Frame > 0)
		{
			Component->AdvanceSimulation(1, static_cast<float>(TickDelta));
		}
		if (Cache->WriteFrame(Component)) ++Written;
		else ++Refused;
	}
	Cache->EndWrite();
	if (Written == 0 || Cache->GetNumFrames() == 0)
	{
		return MCPError(TEXT("The sim cache recorded no frames; the component produced no simulation data to write"));
	}
	MCPHoldSimCache(Cache);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("simCachePath"), Cache->GetPathName());
	Result->SetStringField(TEXT("actorLabel"), Actor->GetActorLabel());
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("componentName"), Component->GetName());
	Result->SetStringField(TEXT("systemPath"), Component->GetAsset()->GetPathName());
	Result->SetNumberField(TEXT("frames"), Cache->GetNumFrames());
	Result->SetNumberField(TEXT("framesRefused"), Refused);
	Result->SetStringField(TEXT("attributeCaptureMode"), CaptureMode);
	TArray<TSharedPtr<FJsonValue>> Emitters;
	const int32 NumEmitters = Cache->GetEmitterNames().Num();
	for (int32 EmitterIndex = 0; EmitterIndex < NumEmitters; ++EmitterIndex)
	{
		Emitters.Add(MakeShared<FJsonValueObject>(MCPDescribeSimCacheEmitter(Cache, EmitterIndex, false)));
	}
	Result->SetArrayField(TEXT("emitters"), Emitters);
	Result->SetStringField(TEXT("note"), FString::Printf(
		TEXT("Held in memory until the editor exits or %d newer captures replace it. Attribute names in the layout are bare (Position, not Particles.Position)."),
		MCPMaxHeldSimCaches));

	// A transient cache object is the only thing created; nothing in the world or on disk changed.
	MCPSetNoRollback(Result, TEXT("Capturing writes a transient in-memory sim cache only; the component, level and assets are unchanged except that advanceSimulation or frames > 1 ticks the simulation forward, which cannot be rewound."));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FNiagaraHandlers::ReadNiagaraSimCache(const TSharedPtr<FJsonObject>& Params)
{
	FString SimCachePath;
	if (auto Err = RequireString(Params, TEXT("simCachePath"), SimCachePath)) return Err;
	const FString EmitterName = OptionalString(Params, TEXT("emitterName"));
	const int32 FrameIndex = OptionalInt(Params, TEXT("frameIndex"), 0);
	const int32 Limit = OptionalInt(Params, TEXT("limit"), 16);
	const bool bSummarize = OptionalBool(Params, TEXT("summarize"), true);
	TArray<FString> Requested;
	const TArray<TSharedPtr<FJsonValue>>* RequestedJson = nullptr;
	if (TryGetArrayParam(Params, TEXT("attributes"), RequestedJson) && RequestedJson)
	{
		for (const TSharedPtr<FJsonValue>& V : *RequestedJson)
		{
			FString S;
			if (V.IsValid() && V->TryGetString(S) && !S.IsEmpty()) Requested.Add(S);
		}
	}

	UNiagaraSimCache* Cache = FindObject<UNiagaraSimCache>(nullptr, *SimCachePath);
	if (!Cache) Cache = LoadObject<UNiagaraSimCache>(nullptr, *SimCachePath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Cache)
	{
		return MCPError(FString::Printf(TEXT("No NiagaraSimCache at '%s'. Transient captures are released after %d newer ones or an editor restart; capture again."), *SimCachePath, MCPMaxHeldSimCaches));
	}
	if (Limit < 0) return MCPError(TEXT("limit must be 0 or more"));

	const int32 NumFrames = Cache->GetNumFrames();
	const TArray<FName> EmitterNames = Cache->GetEmitterNames();
	if (NumFrames == 0 || EmitterNames.Num() == 0)
	{
		auto Empty = MCPErrorObject(TEXT("The sim cache holds no frames or no emitters"));
		Empty->SetNumberField(TEXT("frames"), NumFrames);
		Empty->SetArrayField(TEXT("emitters"), MCPEmitterNamesJson(Cache));
		return MakeShared<FJsonValueObject>(Empty);
	}
	if (FrameIndex < 0 || FrameIndex >= NumFrames)
	{
		return MCPError(FString::Printf(TEXT("frameIndex %d is out of range: the cache holds %d frame(s), 0..%d"), FrameIndex, NumFrames, NumFrames - 1));
	}

	int32 EmitterIndex = INDEX_NONE;
	if (EmitterName.IsEmpty())
	{
		if (EmitterNames.Num() != 1)
		{
			auto Err = MCPErrorObject(TEXT("The cache holds several emitters; pass emitterName"));
			Err->SetArrayField(TEXT("emitters"), MCPEmitterNamesJson(Cache));
			return MakeShared<FJsonValueObject>(Err);
		}
		EmitterIndex = 0;
	}
	else
	{
		for (int32 i = 0; i < EmitterNames.Num(); ++i)
		{
			if (EmitterNames[i].ToString().Equals(EmitterName, ESearchCase::IgnoreCase)) { EmitterIndex = i; break; }
		}
		if (EmitterIndex == INDEX_NONE)
		{
			auto Err = MCPErrorObject(FString::Printf(TEXT("No emitter '%s' in the sim cache"), *EmitterName));
			Err->SetArrayField(TEXT("emitters"), MCPEmitterNamesJson(Cache));
			return MakeShared<FJsonValueObject>(Err);
		}
	}
	const FName ResolvedEmitter = EmitterNames[EmitterIndex];
	const int32 NumInstances = Cache->GetEmitterNumInstances(EmitterIndex, FrameIndex);
	const TArray<FMCPSimCacheAttribute> Layout = MCPSimCacheLayout(Cache, EmitterIndex);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("simCachePath"), Cache->GetPathName());
	Result->SetStringField(TEXT("emitterName"), ResolvedEmitter.ToString());
	Result->SetNumberField(TEXT("frames"), NumFrames);
	Result->SetNumberField(TEXT("frameIndex"), FrameIndex);
	Result->SetNumberField(TEXT("numInstances"), NumInstances);
	Result->SetObjectField(TEXT("layout"), MCPDescribeSimCacheEmitter(Cache, EmitterIndex, true));

	if (Requested.Num() == 0)
	{
		return MCPResult(Result);
	}

	TArray<FString> Missing;
	TArray<TSharedPtr<FJsonValue>> AttributesOut;
	for (const FString& Name : Requested)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("requestedName"), Name);
		const FMCPSimCacheAttribute* Attr = MCPFindSimCacheAttribute(Layout, Name);
		Entry->SetBoolField(TEXT("found"), Attr != nullptr);
		if (!Attr)
		{
			Missing.Add(Name);
			AttributesOut.Add(MakeShared<FJsonValueObject>(Entry));
			continue;
		}
		Entry->SetStringField(TEXT("name"), Attr->Name.ToString());
		Entry->SetStringField(TEXT("type"), Attr->TypeName);
		const int32 Components = Attr->FloatCount + Attr->HalfCount + Attr->IntCount;
		Entry->SetNumberField(TEXT("components"), Components);
		Entry->SetNumberField(TEXT("count"), NumInstances);

		// Instance-major rows: Values[i * Components + c].
		TArray<double> Values;
		if (NumInstances > 0)
		{
			if (Attr->bPosition)
			{
				TArray<FVector> Positions;
				Cache->ReadPositionAttribute(Positions, Attr->Name, ResolvedEmitter, /*bLocalSpaceToWorld=*/true, FrameIndex);
				for (const FVector& P : Positions) { Values.Add(P.X); Values.Add(P.Y); Values.Add(P.Z); }
				Entry->SetBoolField(TEXT("worldSpace"), true);
			}
			else
			{
				TArray<float> Floats;
				TArray<FFloat16> Halfs;
				TArray<int32> Ints;
				Cache->ReadAttribute(Floats, Halfs, Ints, Attr->Name, ResolvedEmitter, FrameIndex);
				Values.SetNumZeroed(NumInstances * Components);
				for (int32 i = 0; i < NumInstances; ++i)
				{
					int32 c = 0;
					for (int32 f = 0; f < Attr->FloatCount; ++f, ++c)
					{
						const int32 Src = f * NumInstances + i;
						if (Floats.IsValidIndex(Src)) Values[i * Components + c] = Floats[Src];
					}
					for (int32 h = 0; h < Attr->HalfCount; ++h, ++c)
					{
						const int32 Src = h * NumInstances + i;
						if (Halfs.IsValidIndex(Src)) Values[i * Components + c] = Halfs[Src].GetFloat();
					}
					for (int32 n = 0; n < Attr->IntCount; ++n, ++c)
					{
						const int32 Src = n * NumInstances + i;
						if (Ints.IsValidIndex(Src)) Values[i * Components + c] = Ints[Src];
					}
				}
			}
		}
		const int32 Rows = Components > 0 ? Values.Num() / Components : 0;

		TArray<TSharedPtr<FJsonValue>> ValuesJson;
		for (int32 i = 0; i < FMath::Min(Rows, Limit); ++i)
		{
			if (Components == 1)
			{
				ValuesJson.Add(MakeShared<FJsonValueNumber>(Values[i]));
			}
			else
			{
				TArray<TSharedPtr<FJsonValue>> Row;
				for (int32 c = 0; c < Components; ++c) Row.Add(MakeShared<FJsonValueNumber>(Values[i * Components + c]));
				ValuesJson.Add(MakeShared<FJsonValueArray>(Row));
			}
		}
		Entry->SetArrayField(TEXT("values"), ValuesJson);
		Entry->SetBoolField(TEXT("truncated"), Rows > Limit);

		if (bSummarize && Rows > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Mins, Maxs, Avgs;
			for (int32 c = 0; c < Components; ++c)
			{
				double Min = TNumericLimits<double>::Max(), Max = TNumericLimits<double>::Lowest(), Sum = 0.0;
				for (int32 i = 0; i < Rows; ++i)
				{
					const double V = Values[i * Components + c];
					Min = FMath::Min(Min, V);
					Max = FMath::Max(Max, V);
					Sum += V;
				}
				Mins.Add(MakeShared<FJsonValueNumber>(Min));
				Maxs.Add(MakeShared<FJsonValueNumber>(Max));
				Avgs.Add(MakeShared<FJsonValueNumber>(Sum / Rows));
			}
			Entry->SetArrayField(TEXT("min"), Mins);
			Entry->SetArrayField(TEXT("max"), Maxs);
			Entry->SetArrayField(TEXT("avg"), Avgs);
		}
		if (NumInstances == 0)
		{
			Entry->SetStringField(TEXT("note"), TEXT("The attribute is in the layout but this frame holds zero instances"));
		}
		AttributesOut.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("attributes"), AttributesOut);

	if (Missing.Num() > 0)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Not in emitter '%s' layout: %s. Readable names are listed under layout.attributes."),
			*ResolvedEmitter.ToString(), *FString::Join(Missing, TEXT(", "))));
	}
	return MCPResult(Result);
}
