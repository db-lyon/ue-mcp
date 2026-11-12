#include "PIEHandlers.h"
#include "HandlerUtils.h"
#include "Handlers/Level/LevelTrace.h"
#include "Handlers/PIE/PIEWorldResolver.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

TSharedPtr<FJsonValue> FPIEHandlers::ViewTrace(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("playerIndex"), TEXT("pieInstance"), TEXT("distance"), TEXT("channel"),
		TEXT("sweepRadius"), TEXT("traceComplex"), TEXT("ignoreActors"), TEXT("ignorePawn") });
	const double PlayerIndex = OptionalNumber(Params, TEXT("playerIndex"), 0);
	const double Instance = OptionalNumber(Params, TEXT("pieInstance"), INDEX_NONE);
	if (!FMath::IsFinite(PlayerIndex) || PlayerIndex < 0 || PlayerIndex > MAX_int32 || PlayerIndex != FMath::FloorToDouble(PlayerIndex))
		return MCPError(TEXT("playerIndex must be a non-negative integer"));
	if (HasParam(Params, TEXT("pieInstance")) && (!FMath::IsFinite(Instance) || Instance < 0 ||
		Instance > MAX_int32 || Instance != FMath::FloorToDouble(Instance)))
		return MCPError(TEXT("pieInstance must be a non-negative integer"));
	const double Distance = OptionalNumber(Params, TEXT("distance"), 1000);
	const double Radius = OptionalNumber(Params, TEXT("sweepRadius"), 0);
	if (!FMath::IsFinite(Distance) || Distance <= 0)
		return MCPError(TEXT("distance must be finite and positive"));
	if (!FMath::IsFinite(Radius) || Radius < 0 || Radius > MAX_flt)
		return MCPError(TEXT("sweepRadius must be finite and non-negative"));

	MCPPIE::PIEWorldResolver::FPlayerTarget Target;
	FString Error;
	if (!MCPPIE::PIEWorldResolver::ResolvePlayer(static_cast<int32>(Instance), static_cast<int32>(PlayerIndex), Target, Error))
		return MCPError(FString::Printf(TEXT("PIE view unavailable: %s. A running PIE session with the selected local player is required."), *Error));
	FVector Location;
	FRotator Rotation;
	Target.PlayerController->GetPlayerViewPoint(Location, Rotation);
	const FVector Direction = Rotation.Vector();
	// Forward trace options; the live viewpoint owns the ray endpoints.
	auto TraceParams = MakeShared<FJsonObject>();
	TraceParams->SetStringField(TEXT("channel"), OptionalString(Params, TEXT("channel"), TEXT("Visibility")));
	TraceParams->SetBoolField(TEXT("traceComplex"), OptionalBool(Params, TEXT("traceComplex"), false));
	const TArray<TSharedPtr<FJsonValue>>* IgnoreActors = nullptr;
	if (TryGetArrayParam(Params, TEXT("ignoreActors"), IgnoreActors) && IgnoreActors)
		TraceParams->SetArrayField(TEXT("ignoreActors"), *IgnoreActors);
	TraceParams->SetObjectField(TEXT("start"), MCPVec3ToJsonObject(Location));
	TraceParams->SetObjectField(TEXT("end"), MCPVec3ToJsonObject(Location + Direction * Distance));
	const AActor* Ignore = OptionalBool(Params, TEXT("ignorePawn"), true) ? Target.PlayerController->GetPawn() : nullptr;
	const auto Line = MCPTrace::Execute(Target.World, TraceParams, false, Ignore, true)->AsObject();
	if (!Line->GetBoolField(TEXT("success"))) return MCPResult(Line);
	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("viewLocation"), MCPVec3ToJsonObject(Location));
	Result->SetObjectField(TEXT("viewRotation"), MCPRotatorToJsonObject(Rotation));
	Result->SetObjectField(TEXT("viewDirection"), MCPVec3ToJsonObject(Direction));
	Result->SetNumberField(TEXT("playerIndex"), PlayerIndex);
	Result->SetNumberField(TEXT("pieInstance"), Target.PIEInstance);
	Result->SetObjectField(TEXT("line"), Line);
	if (Radius > 0)
	{
		TraceParams->SetStringField(TEXT("shape"), TEXT("sphere"));
		TraceParams->SetNumberField(TEXT("radius"), Radius);
		const auto Sweep = MCPTrace::Execute(Target.World, TraceParams, true, Ignore, true)->AsObject();
		if (!Sweep->GetBoolField(TEXT("success"))) return MCPResult(Sweep);
		Result->SetObjectField(TEXT("sweep"), Sweep);
	}
	return MCPResult(Result);
}
