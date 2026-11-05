#include "PIEHandlers.h"
#include "HandlerUtils.h"
#include "Handlers/PIE/PIEPawnDrive.h"
#include "Handlers/PIE/PIEWorldResolver.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "UObject/Package.h"

// pie(drive_pawn): movement input or a navmesh move applied to a PIE player's pawn every frame, with a sampled
// track of where it went. Polled through pie(drive_status) because a handler cannot wait on the game thread (#1565).
namespace MCPPIE
{
	namespace PIEPawnDrive
	{
		enum class EDriveMode : uint8 { Input, Navigate };

		struct FDriveSample
		{
			double Time = 0;
			FVector Location = FVector::ZeroVector;
			FRotator Rotation = FRotator::ZeroRotator;
			FRotator ControlRotation = FRotator::ZeroRotator;
			FVector Velocity = FVector::ZeroVector;
			FString Map;
		};

		struct FDrive
		{
			FString Id;
			EDriveMode Mode = EDriveMode::Input;
			bool bHasTarget = false;
			FVector Direction = FVector::ZeroVector;
			FVector Target = FVector::ZeroVector;
			double Scale = 1;
			double Duration = 5;
			double SampleEvery = 0.1;
			double AcceptanceRadius = 50;
			bool bPlanar = true;
			bool bStopOnMapChange = false;
			int32 PIEInstance = INDEX_NONE;
			int32 PlayerIndex = 0;

			FString State = TEXT("running");
			FString Detail;
			double Elapsed = 0;
			double NextSample = 0;
			double PawnMissingSeconds = 0;
			double DistanceTravelled = 0;
			bool bHasLast = false;
			FVector LastLocation = FVector::ZeroVector;
			bool bHasStart = false;
			FDriveSample Start;
			FDriveSample Last;
			FString CurrentMap;
			TArray<TSharedPtr<FJsonValue>> MapChanges;
			TArray<FDriveSample> Samples;
			bool bSamplesTruncated = false;
			FString NavIssuedMap;
			int32 NavTicksSinceIssue = 0;
			TSharedPtr<FJsonObject> NavPath;
			TWeakObjectPtr<APlayerController> Controller;
		};

		constexpr int32 MaxSamples = 10000;
		constexpr int32 MaxKeptDrives = 16;

		TMap<FString, TSharedPtr<FDrive>> GDrives;
		TArray<FString> GOrder;
		int32 GNextId = 1;
		FTSTicker::FDelegateHandle GTickHandle;
		FDelegateHandle GEndPIEHandle;

		bool IsActive()
		{
			for (const auto& Pair : GDrives)
			{
				if (Pair.Value->State == TEXT("running")) return true;
			}
			return false;
		}

		FString MapOf(UWorld* World)
		{
			return World ? UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()) : FString();
		}

		FDriveSample SampleOf(double Time, APawn* Pawn, APlayerController* PC, UWorld* World)
		{
			FDriveSample S;
			S.Time = Time;
			S.Location = Pawn->GetActorLocation();
			S.Rotation = Pawn->GetActorRotation();
			S.ControlRotation = PC ? PC->GetControlRotation() : S.Rotation;
			S.Velocity = Pawn->GetVelocity();
			S.Map = MapOf(World);
			return S;
		}

		TSharedPtr<FJsonObject> SampleJson(const FDriveSample& S)
		{
			auto O = MakeShared<FJsonObject>();
			O->SetNumberField(TEXT("t"), S.Time);
			O->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(S.Location));
			O->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(S.Rotation));
			O->SetObjectField(TEXT("controlRotation"), MCPRotatorToJsonObject(S.ControlRotation));
			O->SetObjectField(TEXT("velocity"), MCPVec3ToJsonObject(S.Velocity));
			O->SetStringField(TEXT("map"), S.Map);
			return O;
		}

		void AddSample(FDrive& D, const FDriveSample& S)
		{
			if (D.Samples.Num() >= MaxSamples) { D.bSamplesTruncated = true; return; }
			D.Samples.Add(S);
		}

		double DistanceToTarget(const FDrive& D, const FVector& At)
		{
			return D.bPlanar ? FVector::Dist2D(At, D.Target) : FVector::Dist(At, D.Target);
		}

		void Finish(FDrive& D, const TCHAR* State, const FString& Detail = FString())
		{
			if (D.State != TEXT("running")) return;
			D.State = State;
			D.Detail = Detail;
			if (D.Mode == EDriveMode::Navigate)
			{
				if (APlayerController* PC = D.Controller.Get()) PC->StopMovement();
			}
			// The last frame the pawn was seen ends the track whatever the sample cadence.
			if (D.bHasStart && (D.Samples.Num() == 0 || D.Samples.Last().Time < D.Last.Time)) AddSample(D, D.Last);
		}

		TSharedPtr<FJsonObject> PathJson(UNavigationPath* Path)
		{
			auto O = MakeShared<FJsonObject>();
			O->SetBoolField(TEXT("valid"), Path && Path->IsValid());
			O->SetBoolField(TEXT("partial"), Path && Path->IsPartial());
			O->SetNumberField(TEXT("length"), Path ? Path->GetPathLength() : 0.0);
			TArray<TSharedPtr<FJsonValue>> Points;
			if (Path)
			{
				for (const FVector& P : Path->PathPoints) Points.Add(MakeShared<FJsonValueObject>(MCPVec3ToJsonObject(P)));
			}
			O->SetArrayField(TEXT("points"), Points);
			return O;
		}

		// Starts the navmesh move in the pawn's current world; false when there is no path to follow.
		bool IssueNavigate(FDrive& D, UWorld* World, APawn* Pawn, APlayerController* PC)
		{
			UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			if (!NavSys)
			{
				D.NavPath = PathJson(nullptr);
				Finish(D, TEXT("no_path"), FString::Printf(TEXT("map %s has no navigation system"), *MapOf(World)));
				return false;
			}
			UNavigationPath* Path = NavSys->FindPathToLocationSynchronously(World, Pawn->GetActorLocation(), D.Target, Pawn);
			D.NavPath = PathJson(Path);
			if (!Path || !Path->IsValid())
			{
				Finish(D, TEXT("no_path"), FString::Printf(TEXT("no navmesh path from the pawn to the target in %s; check gameplay(find_nav_path, world=pie)"), *MapOf(World)));
				return false;
			}
			UAIBlueprintHelperLibrary::SimpleMoveToLocation(PC, D.Target);
			D.NavIssuedMap = MapOf(World);
			D.NavTicksSinceIssue = 0;
			return true;
		}

		void TickDrive(FDrive& D, float DeltaSeconds)
		{
			D.Elapsed += DeltaSeconds;

			PIEWorldResolver::FPlayerTarget Target;
			FString Error;
			APawn* Pawn = nullptr;
			if (PIEWorldResolver::ResolvePlayer(D.PIEInstance, D.PlayerIndex, Target, Error))
			{
				Pawn = Target.PlayerController->GetPawn();
			}
			if (!Pawn)
			{
				// A level change or a respawn leaves the player without a pawn for some frames; the drive waits.
				D.PawnMissingSeconds += DeltaSeconds;
				if (D.Elapsed >= D.Duration) Finish(D, D.bHasTarget ? TEXT("timeout") : TEXT("completed"));
				return;
			}
			APlayerController* PC = Target.PlayerController;
			UWorld* World = Target.World;
			D.Controller = PC;

			const FDriveSample Now = SampleOf(D.Elapsed, Pawn, PC, World);
			if (!D.bHasStart)
			{
				D.Start = Now;
				D.bHasStart = true;
				D.CurrentMap = Now.Map;
			}
			if (D.bHasLast && Now.Map == D.CurrentMap) D.DistanceTravelled += FVector::Dist(D.LastLocation, Now.Location);
			D.LastLocation = Now.Location;
			D.bHasLast = true;
			D.Last = Now;

			if (Now.Map != D.CurrentMap)
			{
				auto Change = MakeShared<FJsonObject>();
				Change->SetNumberField(TEXT("t"), D.Elapsed);
				Change->SetStringField(TEXT("from"), D.CurrentMap);
				Change->SetStringField(TEXT("to"), Now.Map);
				Change->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Now.Location));
				Change->SetObjectField(TEXT("controlRotation"), MCPRotatorToJsonObject(Now.ControlRotation));
				D.MapChanges.Add(MakeShared<FJsonValueObject>(Change));
				D.CurrentMap = Now.Map;
				AddSample(D, Now);
				D.NextSample = D.Elapsed + D.SampleEvery;
				if (D.bStopOnMapChange)
				{
					Finish(D, TEXT("map_changed"));
					return;
				}
			}
			else if (D.Elapsed >= D.NextSample || D.Samples.Num() == 0)
			{
				AddSample(D, Now);
				D.NextSample = D.Elapsed + D.SampleEvery;
			}

			if (D.bHasTarget && DistanceToTarget(D, Now.Location) <= D.AcceptanceRadius)
			{
				Finish(D, TEXT("arrived"));
				return;
			}
			if (D.Elapsed >= D.Duration)
			{
				Finish(D, D.bHasTarget ? TEXT("timeout") : TEXT("completed"));
				return;
			}

			if (D.Mode == EDriveMode::Navigate)
			{
				if (D.NavIssuedMap != Now.Map)
				{
					IssueNavigate(D, World, Pawn, PC);
					return;
				}
				++D.NavTicksSinceIssue;
				UPathFollowingComponent* Follow = PC->FindComponentByClass<UPathFollowingComponent>();
				if (D.NavTicksSinceIssue > 2 && (!Follow || Follow->GetStatus() == EPathFollowingStatus::Idle))
				{
					Finish(D, TEXT("nav_ended"), FString::Printf(TEXT("path following stopped %.0f cm from the target, outside acceptanceRadius"),
						DistanceToTarget(D, Now.Location)));
				}
				return;
			}

			FVector Dir = D.Direction;
			if (D.bHasTarget)
			{
				Dir = D.Target - Now.Location;
				if (D.bPlanar) Dir.Z = 0;
			}
			Pawn->AddMovementInput(Dir.GetSafeNormal(), static_cast<float>(D.Scale), false);
		}

		bool TickAll(float DeltaSeconds)
		{
			for (auto& Pair : GDrives)
			{
				if (Pair.Value->State == TEXT("running")) TickDrive(*Pair.Value, DeltaSeconds);
			}
			if (IsActive()) return true;
			GTickHandle.Reset();
			return false;
		}

		void EnsureTicking()
		{
			if (!GEndPIEHandle.IsValid())
			{
				GEndPIEHandle = FEditorDelegates::EndPIE.AddLambda([](bool)
				{
					for (auto& Pair : GDrives) Finish(*Pair.Value, TEXT("pie_ended"));
				});
			}
			if (!GTickHandle.IsValid())
			{
				GTickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickAll));
			}
		}

		void Shutdown()
		{
			if (GTickHandle.IsValid())
			{
				FTSTicker::GetCoreTicker().RemoveTicker(GTickHandle);
				GTickHandle.Reset();
			}
			if (GEndPIEHandle.IsValid())
			{
				FEditorDelegates::EndPIE.Remove(GEndPIEHandle);
				GEndPIEHandle.Reset();
			}
			GDrives.Reset();
			GOrder.Reset();
		}

		void Remember(const TSharedPtr<FDrive>& D)
		{
			GDrives.Add(D->Id, D);
			GOrder.Remove(D->Id);
			GOrder.Add(D->Id);
			for (int32 i = 0; GOrder.Num() > MaxKeptDrives && i < GOrder.Num();)
			{
				const TSharedPtr<FDrive>* Old = GDrives.Find(GOrder[i]);
				if (Old && (*Old)->State == TEXT("running")) { ++i; continue; }
				GDrives.Remove(GOrder[i]);
				GOrder.RemoveAt(i);
			}
		}

		TSharedPtr<FDrive> Lookup(const FString& Id, FString& OutError)
		{
			if (!Id.IsEmpty())
			{
				if (const TSharedPtr<FDrive>* Found = GDrives.Find(Id)) return *Found;
				OutError = FString::Printf(TEXT("No drive '%s'. Known: %s"), *Id,
					GOrder.Num() ? *FString::Join(GOrder, TEXT(", ")) : TEXT("none"));
				return nullptr;
			}
			if (GOrder.Num() == 0)
			{
				OutError = TEXT("No drive has been started; call pie(drive_pawn) first");
				return nullptr;
			}
			return GDrives.FindRef(GOrder.Last());
		}

		TSharedPtr<FJsonObject> Report(const FDrive& D, bool bIncludeSamples)
		{
			auto R = MCPSuccess();
			R->SetStringField(TEXT("driveId"), D.Id);
			R->SetStringField(TEXT("state"), D.State);
			R->SetBoolField(TEXT("running"), D.State == TEXT("running"));
			if (!D.Detail.IsEmpty()) R->SetStringField(TEXT("detail"), D.Detail);
			R->SetStringField(TEXT("mode"), D.Mode == EDriveMode::Navigate ? TEXT("navigate") : TEXT("input"));
			if (D.bHasTarget) R->SetObjectField(TEXT("targetLocation"), MCPVec3ToJsonObject(D.Target));
			else R->SetObjectField(TEXT("direction"), MCPVec3ToJsonObject(D.Direction));
			R->SetNumberField(TEXT("elapsedSeconds"), D.Elapsed);
			R->SetNumberField(TEXT("durationSeconds"), D.Duration);
			R->SetNumberField(TEXT("pawnMissingSeconds"), D.PawnMissingSeconds);
			R->SetNumberField(TEXT("pieInstance"), D.PIEInstance);
			R->SetNumberField(TEXT("playerIndex"), D.PlayerIndex);
			if (D.bHasStart)
			{
				R->SetObjectField(TEXT("start"), SampleJson(D.Start));
				R->SetObjectField(TEXT("final"), SampleJson(D.Last));
				R->SetStringField(TEXT("startMap"), D.Start.Map);
				R->SetStringField(TEXT("finalMap"), D.Last.Map);
				R->SetNumberField(TEXT("distanceTravelled"), D.DistanceTravelled);
				if (D.Start.Map == D.Last.Map)
					R->SetObjectField(TEXT("displacement"), MCPVec3ToJsonObject(D.Last.Location - D.Start.Location));
				if (D.bHasTarget) R->SetNumberField(TEXT("distanceToTarget"), DistanceToTarget(D, D.Last.Location));
			}
			R->SetArrayField(TEXT("mapChanges"), D.MapChanges);
			if (D.NavPath.IsValid()) R->SetObjectField(TEXT("navPath"), D.NavPath);
			R->SetNumberField(TEXT("sampleCount"), D.Samples.Num());
			R->SetBoolField(TEXT("samplesTruncated"), D.bSamplesTruncated);
			if (bIncludeSamples)
			{
				TArray<TSharedPtr<FJsonValue>> Out;
				Out.Reserve(D.Samples.Num());
				for (const FDriveSample& S : D.Samples) Out.Add(MakeShared<FJsonValueObject>(SampleJson(S)));
				R->SetArrayField(TEXT("samples"), Out);
			}
			return R;
		}

		bool ReadIndex(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, int32 Default, int32& Out, FString& OutError)
		{
			const double Raw = OptionalNumber(Params, Key, Default);
			if (!FMath::IsFinite(Raw) || Raw < 0 || Raw > MAX_int32 || Raw != FMath::FloorToDouble(Raw))
			{
				OutError = FString::Printf(TEXT("%s must be a non-negative integer"), Key);
				return false;
			}
			Out = static_cast<int32>(Raw);
			return true;
		}
	}
}

TSharedPtr<FJsonValue> FPIEHandlers::PieDrivePawn(const TSharedPtr<FJsonObject>& Params)
{
	namespace Drive = MCPPIE::PIEPawnDrive;
	// wait and timeoutSeconds belong to the server, which polls drive_status; read so the read set is the declared set.
	MCPReadParamsAhead(Params, { TEXT("direction"), TEXT("targetLocation"), TEXT("mode"), TEXT("durationSeconds"),
		TEXT("scale"), TEXT("sampleEvery"), TEXT("acceptanceRadius"), TEXT("planar"), TEXT("stopOnMapChange"),
		TEXT("playerIndex"), TEXT("pieInstance"), TEXT("driveId"), TEXT("wait"), TEXT("timeoutSeconds") });

	auto D = MakeShared<Drive::FDrive>();
	const bool bHasDirection = HasParam(Params, TEXT("direction"));
	D->bHasTarget = HasParam(Params, TEXT("targetLocation"));
	if (bHasDirection == D->bHasTarget) return MCPError(TEXT("Pass exactly one of direction or targetLocation"));
	const FString Mode = OptionalString(Params, TEXT("mode"), TEXT("input")).ToLower();
	if (Mode == TEXT("navigate")) D->Mode = Drive::EDriveMode::Navigate;
	else if (Mode != TEXT("input")) return MCPError(FString::Printf(TEXT("mode '%s' is not input or navigate"), *Mode));
	if (D->Mode == Drive::EDriveMode::Navigate && !D->bHasTarget) return MCPError(TEXT("mode=navigate needs targetLocation"));

	if (bHasDirection)
	{
		D->Direction = OptionalVec3(Params, TEXT("direction"));
		if (D->Direction.ContainsNaN() || D->Direction.IsNearlyZero())
			return MCPError(TEXT("direction must be a finite, nonzero vector"));
		D->Direction = D->Direction.GetSafeNormal();
	}
	else
	{
		D->Target = OptionalVec3(Params, TEXT("targetLocation"));
		if (D->Target.ContainsNaN()) return MCPError(TEXT("targetLocation must be finite"));
	}
	D->Duration = OptionalNumber(Params, TEXT("durationSeconds"), 5);
	D->Scale = OptionalNumber(Params, TEXT("scale"), 1);
	D->SampleEvery = OptionalNumber(Params, TEXT("sampleEvery"), 0.1);
	D->AcceptanceRadius = OptionalNumber(Params, TEXT("acceptanceRadius"), 50);
	D->bPlanar = OptionalBool(Params, TEXT("planar"), true);
	D->bStopOnMapChange = OptionalBool(Params, TEXT("stopOnMapChange"), false);
	if (!FMath::IsFinite(D->Duration) || D->Duration <= 0 || D->Duration > 600)
		return MCPError(TEXT("durationSeconds must be greater than 0 and at most 600"));
	if (!FMath::IsFinite(D->Scale) || D->Scale < -1 || D->Scale > 1) return MCPError(TEXT("scale must be between -1 and 1"));
	if (!FMath::IsFinite(D->SampleEvery) || D->SampleEvery < 0) return MCPError(TEXT("sampleEvery must be 0 (every frame) or more seconds"));
	if (!FMath::IsFinite(D->AcceptanceRadius) || D->AcceptanceRadius < 0) return MCPError(TEXT("acceptanceRadius must be 0 or more"));

	FString Error;
	int32 PlayerIndex = 0;
	if (!Drive::ReadIndex(Params, TEXT("playerIndex"), 0, PlayerIndex, Error)) return MCPError(Error);
	int32 RequestedInstance = INDEX_NONE;
	if (HasParam(Params, TEXT("pieInstance")) && !Drive::ReadIndex(Params, TEXT("pieInstance"), 0, RequestedInstance, Error)) return MCPError(Error);

	MCPPIE::PIEWorldResolver::FPlayerTarget Target;
	if (!MCPPIE::PIEWorldResolver::ResolvePlayer(RequestedInstance, PlayerIndex, Target, Error))
		return MCPError(FString::Printf(TEXT("No PIE player to drive: %s. Start PIE with editor(play_in_editor) first."), *Error));
	APawn* Pawn = Target.PlayerController->GetPawn();
	if (!Pawn) return MCPError(FString::Printf(TEXT("PIE player %d has no pawn to drive"), PlayerIndex));
	if (D->Mode == Drive::EDriveMode::Input && !Pawn->GetMovementComponent())
		return MCPError(FString::Printf(TEXT("Pawn %s has no movement component, so movement input would not move it; use mode=navigate or editor(teleport_runtime_actor)"),
			*Pawn->GetClass()->GetName()));

	D->PIEInstance = Target.PIEInstance;
	D->PlayerIndex = PlayerIndex;
	D->Id = OptionalString(Params, TEXT("driveId"));
	if (D->Id.IsEmpty()) D->Id = FString::Printf(TEXT("drive_%d"), Drive::GNextId++);
	if (const TSharedPtr<Drive::FDrive>* Existing = Drive::GDrives.Find(D->Id))
	{
		if ((*Existing)->State == TEXT("running")) return MCPError(FString::Printf(TEXT("Drive '%s' is still running; stop it with pie(drive_stop) first"), *D->Id));
	}
	// One drive per player at a time: two would sum their input on the same pawn.
	for (const auto& Pair : Drive::GDrives)
	{
		const Drive::FDrive& Other = *Pair.Value;
		if (Other.State == TEXT("running") && Other.PIEInstance == D->PIEInstance && Other.PlayerIndex == D->PlayerIndex)
			return MCPError(FString::Printf(TEXT("Drive '%s' is already driving this player; stop it with pie(drive_stop) first"), *Other.Id));
	}

	// The first frame is taken now, so the start pose is the one the call saw.
	D->Controller = Target.PlayerController;
	D->Start = Drive::SampleOf(0, Pawn, Target.PlayerController, Target.World);
	D->bHasStart = true;
	D->Last = D->Start;
	D->CurrentMap = D->Start.Map;
	D->LastLocation = D->Start.Location;
	D->bHasLast = true;
	Drive::AddSample(*D, D->Start);
	D->NextSample = D->SampleEvery;
	if (D->bHasTarget && Drive::DistanceToTarget(*D, D->Start.Location) <= D->AcceptanceRadius)
	{
		Drive::Finish(*D, TEXT("arrived"));
	}
	else if (D->Mode == Drive::EDriveMode::Navigate)
	{
		Drive::IssueNavigate(*D, Target.World, Pawn, Target.PlayerController);
	}

	Drive::Remember(D);
	if (D->State == TEXT("running")) Drive::EnsureTicking();
	TSharedPtr<FJsonObject> Result = Drive::Report(*D, false);
	MCPSetCreated(Result);
	if (D->State == TEXT("running"))
	{
		// A running drive is undone by stopping it; the distance already walked stays walked.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("driveId"), D->Id);
		MCPSetRollback(Result, TEXT("pie_drive_stop"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("The drive ended on its first frame, so there is nothing left to stop"));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieDriveStatus(const TSharedPtr<FJsonObject>& Params)
{
	namespace Drive = MCPPIE::PIEPawnDrive;
	const FString Id = OptionalString(Params, TEXT("driveId"));
	const bool bIncludeSamples = OptionalBool(Params, TEXT("includeSamples"), true);
	FString Error;
	const TSharedPtr<Drive::FDrive> D = Drive::Lookup(Id, Error);
	if (!D) return MCPError(Error);
	return MCPResult(Drive::Report(*D, bIncludeSamples));
}

TSharedPtr<FJsonValue> FPIEHandlers::PieDriveStop(const TSharedPtr<FJsonObject>& Params)
{
	namespace Drive = MCPPIE::PIEPawnDrive;
	const FString Id = OptionalString(Params, TEXT("driveId"));
	FString Error;
	const TSharedPtr<Drive::FDrive> D = Drive::Lookup(Id, Error);
	if (!D) return MCPError(Error);
	const bool bWasRunning = D->State == TEXT("running");
	Drive::Finish(*D, TEXT("stopped"));
	auto R = Drive::Report(*D, true);
	R->SetBoolField(TEXT("alreadyStopped"), !bWasRunning);
	MCPSetNoRollback(R, TEXT("A stopped drive is not resumed; drive_pawn starts a new one from where the pawn stands"));
	return MCPResult(R);
}
