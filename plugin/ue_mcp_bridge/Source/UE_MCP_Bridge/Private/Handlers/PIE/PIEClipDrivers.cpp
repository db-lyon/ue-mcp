#include "Handlers/PIE/PIEClipDrivers.h"
#include "Handlers/PIE/PIEActorPuppet.h"
#include "Handlers/PIE/PIEFrameSampler.h"
#include "Handlers/PIE/PIERemotePlayers.h"
#include "Handlers/PIE/PIESequenceFormat.h"
#include "Handlers/PIE/PIELog.h"
#include "MCPPIERemoteControl.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Components/ActorComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Navigation/PathFollowingComponent.h"
#include "UObject/UnrealType.h"

namespace MCPPIE
{
	namespace
	{
		FInputActionValue ClipDriverActionValue(const UInputAction* Action, const FVector& V)
		{
			switch (Action->ValueType)
			{
			case EInputActionValueType::Boolean: return FInputActionValue(V.X != 0.0);
			case EInputActionValueType::Axis1D: return FInputActionValue(static_cast<float>(V.X));
			case EInputActionValueType::Axis2D: return FInputActionValue(FVector2D(V.X, V.Y));
			default: return FInputActionValue(V);
			}
		}

		// "Component.Function" names a component's function; anything else is the actor's own.
		UObject* ClipCallTarget(AActor* Actor, const FString& Name, FString& OutFunction, FString& OutError)
		{
			FString Comp;
			OutFunction = Name;
			if (!Name.Split(TEXT("."), &Comp, &OutFunction)) return Actor;
			for (UActorComponent* C : Actor->GetComponents())
			{
				if (C && C->GetName() == Comp) return C;
			}
			OutError = FString::Printf(TEXT("%s has no component %s"), *Actor->GetName(), *Comp);
			return nullptr;
		}

		UBlackboardComponent* ClipBlackboardOf(AActor* Actor, FString& OutError)
		{
			APawn* Pawn = Cast<APawn>(Actor);
			AAIController* AI = Pawn ? Cast<AAIController>(Pawn->GetController()) : Cast<AAIController>(Actor);
			UBlackboardComponent* BB = AI ? AI->GetBlackboardComponent() : nullptr;
			if (!BB) OutError = FString::Printf(TEXT("%s has no AI controller with a blackboard"), *Actor->GetName());
			return BB;
		}

		/** The run's own player or a remote client: Input Actions, and action and value lanes on its pawn. */
		class FClipPlayerDriver final : public FClipPerformerDriver
		{
		public:
			FClipPlayerDriver(const FClipPerformer& InPerformer, const FClipDriverContext& InContext)
				: FClipPerformerDriver(InPerformer), Context(InContext) {}

			virtual AActor* ResolveActor(UWorld* World, FString& OutError) override
			{
				if (Performer.Client == 0)
				{
					ULocalPlayer* LP = PIERemotePlayers::FindOwnLocalPlayer(Context.PIEInstance, Context.OwnClientId, OutError);
					APlayerController* PC = LP ? LP->PlayerController.Get() : nullptr;
					APawn* Pawn = PC ? PC->GetPawn() : nullptr;
					if (!Pawn && OutError.IsEmpty()) OutError = TEXT("the run's own player has no pawn");
					return Pawn;
				}
				for (const PIERemotePlayers::FRemotePlayer& R : PIERemotePlayers::List())
				{
					if (R.Client != Performer.Client) continue;
					APawn* Pawn = R.PlayerController ? R.PlayerController->GetPawn() : nullptr;
					if (!Pawn) OutError = FString::Printf(TEXT("client %d has no pawn"), Performer.Client);
					return Pawn;
				}
				OutError = FString::Printf(TEXT("no remote client %d is in the session"), Performer.Client);
				return nullptr;
			}

			virtual void FeedInput(const FClipTrack& Track, bool bActive, const FVector& Value) override
			{
				if (Performer.Client > 0)
				{
					FeedRemote(Track, bActive, Value);
					return;
				}
				if (!bActive) return;
				FString Err;
				ULocalPlayer* LP = PIERemotePlayers::FindOwnLocalPlayer(Context.PIEInstance, Context.OwnClientId, Err);
				UEnhancedInputLocalPlayerSubsystem* Sub = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
				if (!Sub) return;
				TWeakObjectPtr<UInputAction>& Weak = Actions.FindOrAdd(Track.Action);
				UInputAction* A = Weak.Get();
				if (!A)
				{
					A = LoadObject<UInputAction>(nullptr, *Track.Action);
					Weak = A;
				}
				if (A) Sub->InjectInputForAction(A, ClipDriverActionValue(A, Value), {}, {});
			}

			virtual void Stop() override
			{
				for (const TPair<FString, FHold>& H : Holds)
				{
					if (UMCPPIERemoteControl* Remote = PIERemotePlayers::FindById(H.Value.Id)) Remote->SendStop(H.Value.Id);
				}
				Holds.Reset();
				Errors.Reset();
			}

		private:
			// A remote client's lane is a hold on that client, started when the lane takes a value, updated when the
			// value changes and stopped when it feeds nothing: the client injects the held value on each of its own
			// frames, so the lane keeps feeding through a hold check as the run's own player's lanes do.
			void FeedRemote(const FClipTrack& Track, bool bActive, const FVector& V)
			{
				FHold* Hold = Holds.Find(Track.Action);
				if (!bActive)
				{
					if (Hold)
					{
						if (UMCPPIERemoteControl* Remote = PIERemotePlayers::FindById(Hold->Id)) Remote->SendStop(Hold->Id);
						Holds.Remove(Track.Action);
					}
					return;
				}
				if (Hold)
				{
					if (Hold->Value.Equals(V, 1e-4)) return;
					if (UMCPPIERemoteControl* Remote = PIERemotePlayers::FindById(Hold->Id))
					{
						Remote->SendUpdateHold(Hold->Id, V);
						Hold->Value = V;
						return;
					}
					Holds.Remove(Track.Action);
				}
				FString Err;
				UMCPPIERemoteControl* Remote = PIERemotePlayers::Find(Performer.Client, Err);
				if (!Remote)
				{
					if (!Errors.Contains(Track.Action))
					{
						Errors.Add(Track.Action);
						UE_LOG(LogMCPPIE, Warning, TEXT("[PIE-CLIP] the lane for %s on client %d feeds nothing: %s"), *Track.Action, Performer.Client, *Err);
					}
					return;
				}
				const FString Id = PIERemotePlayers::NewId(TEXT("clip"));
				PIERemotePlayers::RememberId(Id, Remote);
				Remote->SendStartHold(Id, FSoftObjectPath(Track.Action), V);
				Holds.Add(Track.Action, { Id, V });
			}

			FClipDriverContext Context;
			TMap<FString, TWeakObjectPtr<UInputAction>> Actions;
			struct FHold { FString Id; FVector Value; };
			TMap<FString, FHold> Holds;
			TSet<FString> Errors;
		};

		/** An actor in the level: moves through its controller, and action and value lanes on it. */
		class FClipActorDriver final : public FClipPerformerDriver
		{
		public:
			explicit FClipActorDriver(const FClipPerformer& InPerformer) : FClipPerformerDriver(InPerformer) {}

			virtual AActor* ResolveActor(UWorld* World, FString& OutError) override
			{
				if (AActor* Cached = Actor.Get())
				{
					if (Cached->GetWorld() == World && !Cached->IsActorBeingDestroyed()) return Cached;
					Actor.Reset();
				}
				AActor* Found = FindActorById(World, Performer.Actor);
				if (!Found) OutError = FString::Printf(TEXT("no actor in the PIE world is named %s"), *Performer.Actor);
				Actor = Found;
				return Found;
			}

			virtual bool MoveTo(UWorld* World, const FClipMovePoint& Point, FString& OutError) override
			{
				APawn* Pawn = Cast<APawn>(ResolveActor(World, OutError));
				if (!Pawn)
				{
					if (OutError.IsEmpty()) OutError = FString::Printf(TEXT("%s is not a pawn, so it has no controller to walk it"), *Performer.Actor);
					return false;
				}
				AController* Controller = Pawn->GetController();
				if (!Controller)
				{
					Pawn->SpawnDefaultController();
					Controller = Pawn->GetController();
				}
				if (AAIController* AI = Cast<AAIController>(Controller))
				{
					const float Radius = Point.Radius >= 0.0 ? static_cast<float>(Point.Radius) : -1.f;
					EPathFollowingRequestResult::Type R = AI->MoveToLocation(Point.Location, Radius, /*bStopOnOverlap=*/true, /*bUsePathfinding=*/true, /*bProjectDestinationToNavigation=*/true);
					// A level with no navigation mesh still walks the pawn, straight at the point.
					if (R == EPathFollowingRequestResult::Failed) R = AI->MoveToLocation(Point.Location, Radius, true, /*bUsePathfinding=*/false);
					if (R == EPathFollowingRequestResult::Failed)
					{
						OutError = FString::Printf(TEXT("%s's controller refused the move to %s"), *Performer.Actor, *Point.Location.ToCompactString());
						return false;
					}
					return true;
				}
				if (Controller)
				{
					UAIBlueprintHelperLibrary::SimpleMoveToLocation(Controller, Point.Location);
					return true;
				}
				OutError = FString::Printf(TEXT("%s has no controller, and its class spawns none"), *Performer.Actor);
				return false;
			}

			virtual void Stop() override
			{
				// A move still in flight when the run ends stops with it.
				if (APawn* Pawn = Cast<APawn>(Actor.Get()))
				{
					if (AAIController* AI = Cast<AAIController>(Pawn->GetController())) AI->StopMovement();
				}
				Actor.Reset();
			}

		private:
			TWeakObjectPtr<AActor> Actor;
		};
	}

	void FClipPerformerDriver::FeedInput(const FClipTrack& /*Track*/, bool /*bActive*/, const FVector& /*Value*/) {}

	bool FClipPerformerDriver::MoveTo(UWorld* /*World*/, const FClipMovePoint& /*Point*/, FString& OutError)
	{
		OutError = FString::Printf(TEXT("%s takes no move lanes"), *Performer.Label());
		return false;
	}

	bool FClipPerformerDriver::Call(UWorld* World, const FClipActionCall& Call, FString& OutError)
	{
		AActor* Actor = ResolveActor(World, OutError);
		if (!Actor) return false;
		FString Function;
		UObject* Target = ClipCallTarget(Actor, Call.Function, Function, OutError);
		return Target && FPIEActorPuppet::CallFunction(Target, Function, Call.Args, OutError);
	}

	bool FClipPerformerDriver::SetValue(UWorld* World, const FClipLane& Lane, double Value, FString& OutError)
	{
		AActor* Actor = ResolveActor(World, OutError);
		if (!Actor) return false;
		if (Lane.Target.StartsWith(TEXT("bb:")))
		{
			UBlackboardComponent* BB = ClipBlackboardOf(Actor, OutError);
			if (!BB) return false;
			const FName Key(*Lane.Target.RightChop(3));
			if (BB->GetKeyID(Key) == FBlackboard::InvalidKey)
			{
				OutError = FString::Printf(TEXT("%s's blackboard has no key %s"), *Actor->GetName(), *Key.ToString());
				return false;
			}
			const TSubclassOf<UBlackboardKeyType> Type = BB->GetKeyType(BB->GetKeyID(Key));
			if (Type == UBlackboardKeyType_Bool::StaticClass()) BB->SetValueAsBool(Key, Value >= 0.5);
			else if (Type == UBlackboardKeyType_Int::StaticClass()) BB->SetValueAsInt(Key, FMath::RoundToInt(Value));
			else BB->SetValueAsFloat(Key, static_cast<float>(Value));
			return true;
		}
		const TSharedPtr<FJsonValue> J = Lane.Type == EClipValueType::Bool ? StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueBoolean>(Value >= 0.5)) : StaticCastSharedRef<FJsonValue>(MakeShared<FJsonValueNumber>(Value));
		return FPIEActorPuppet::SetPropertyByPath(Actor, Lane.Target, J, OutError);
	}

	bool FClipPerformerDriver::ReadValue(UWorld* World, const FClipLane& Lane, double& OutValue, FString& OutError)
	{
		AActor* Actor = ResolveActor(World, OutError);
		if (!Actor) return false;
		if (Lane.Target.StartsWith(TEXT("bb:")))
		{
			UBlackboardComponent* BB = ClipBlackboardOf(Actor, OutError);
			if (!BB) return false;
			const FName Key(*Lane.Target.RightChop(3));
			const TSubclassOf<UBlackboardKeyType> Type = BB->GetKeyType(BB->GetKeyID(Key));
			if (Type == UBlackboardKeyType_Bool::StaticClass()) OutValue = BB->GetValueAsBool(Key) ? 1.0 : 0.0;
			else if (Type == UBlackboardKeyType_Int::StaticClass()) OutValue = BB->GetValueAsInt(Key);
			else OutValue = BB->GetValueAsFloat(Key);
			return true;
		}
		return ResolvePathToDouble(Actor, Lane.Target, OutValue, &OutError);
	}

	bool FClipPerformerDriver::ReadLocation(UWorld* World, FVector& OutLocation, FString& OutError)
	{
		AActor* Actor = ResolveActor(World, OutError);
		if (!Actor) return false;
		OutLocation = Actor->GetActorLocation();
		return true;
	}

	TUniquePtr<FClipPerformerDriver> MakeClipPerformerDriver(const FClipPerformer& Performer, const FClipDriverContext& Context)
	{
		switch (Performer.Kind)
		{
		case EClipPerformerKind::Player: return MakeUnique<FClipPlayerDriver>(Performer, Context);
		case EClipPerformerKind::Actor: return MakeUnique<FClipActorDriver>(Performer);
		}
		return nullptr;
	}

	// ── Recording an action lane ───────────────────────────────────────

	FClipCallRecorder& FClipCallRecorder::Get()
	{
		static FClipCallRecorder Instance;
		return Instance;
	}

	TArray<FString> FClipCallRecorder::Watch(const FString& LaneId, AActor* Actor, const TArray<FString>& Functions)
	{
		TArray<FString> Missing;
		Seen.FindOrAdd(LaneId);
		for (const FString& Name : Functions)
		{
			FString Fn, Err;
			UObject* Target = ClipCallTarget(Actor, Name, Fn, Err);
			UFunction* F = Target ? Target->FindFunction(FName(*Fn)) : nullptr;
			if (!F)
			{
				Missing.Add(Name);
				continue;
			}
			FHook* Hook = Hooks.Find(F);
			if (!Hook)
			{
				FHook New;
				New.Function = F;
				New.Original = F->GetNativeFunc();
				Hook = &Hooks.Add(F, New);
				F->SetNativeFunc(&FClipCallRecorder::Thunk);
			}
			Hook->Watchers.Add(TPair<TWeakObjectPtr<UObject>, FString>(Target, LaneId));
		}
		return Missing;
	}

	TMap<FString, TArray<FClipActionCall>> FClipCallRecorder::Finish()
	{
		for (TPair<UFunction*, FHook>& H : Hooks)
		{
			if (UFunction* F = H.Value.Function.Get()) F->SetNativeFunc(H.Value.Original);
		}
		Hooks.Reset();
		TMap<FString, TArray<FClipActionCall>> Out = MoveTemp(Seen);
		Seen.Reset();
		return Out;
	}

	void FClipCallRecorder::Thunk(UObject* Context, FFrame& Stack, RESULT_DECL)
	{
		FClipCallRecorder& Self = Get();
		UFunction* Function = Stack.CurrentNativeFunction;
		FHook* Hook = Function ? Self.Hooks.Find(Function) : nullptr;
		if (!Hook || !Hook->Original) return;
		const FNativeFuncPtr Original = Hook->Original;
		Self.Note(Function, Context, Stack);
		(*Original)(Context, Stack, RESULT_PARAM);
	}

	void FClipCallRecorder::Note(UFunction* Function, UObject* Context, FFrame& Stack)
	{
		FHook* Hook = Hooks.Find(Function);
		if (!Hook) return;
		for (const TPair<TWeakObjectPtr<UObject>, FString>& W : Hook->Watchers)
		{
			if (W.Key.Get() != Context) continue;
			FClipActionCall Call;
			Call.Time = CurrentFrame;
			const bool bOnComponent = Context && !Context->IsA<AActor>();
			Call.Function = bOnComponent ? Context->GetName() + TEXT(".") + Function->GetName() : Function->GetName();
			// The arguments, when the frame holds them: a call through ProcessEvent, or a Blueprint function's own
			// frame. A Blueprint calling a native function has not evaluated them yet, and the call is kept without.
			if (Stack.Node == Function && Stack.Locals)
			{
				for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
				{
					if (It->HasAnyPropertyFlags(CPF_ReturnParm) || (It->HasAnyPropertyFlags(CPF_OutParm) && !It->HasAnyPropertyFlags(CPF_ReferenceParm))) continue;
					const void* Value = It->ContainerPtrToValuePtr<void>(Stack.Locals);
					if (const FBoolProperty* B = CastField<FBoolProperty>(*It)) Call.Args.Add(MakeShared<FJsonValueBoolean>(B->GetPropertyValue(Value)));
					else if (const FNumericProperty* N = CastField<FNumericProperty>(*It); N && !N->IsEnum())
					{
						Call.Args.Add(MakeShared<FJsonValueNumber>(N->IsFloatingPoint() ? N->GetFloatingPointPropertyValue(Value) : static_cast<double>(N->GetSignedIntPropertyValue(Value))));
					}
					else
					{
						FString Text;
						It->ExportTextItem_Direct(Text, Value, nullptr, nullptr, PPF_None);
						Call.Args.Add(MakeShared<FJsonValueString>(Text));
					}
				}
			}
			Seen.FindOrAdd(W.Value).Add(MoveTemp(Call));
		}
	}
}
