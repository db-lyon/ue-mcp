#include "PIEFrameSampler.h"
#include "PIE_StudioModule.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ActorComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "InputAction.h"
#include "UObject/UnrealType.h"

namespace UEMCPPIE
{
	namespace
	{
		EActionValueType ConvertValueType(EInputActionValueType T)
		{
			switch (T)
			{
			case EInputActionValueType::Boolean: return EActionValueType::Boolean;
			case EInputActionValueType::Axis1D:  return EActionValueType::Axis1D;
			case EInputActionValueType::Axis2D:  return EActionValueType::Axis2D;
			case EInputActionValueType::Axis3D:  return EActionValueType::Axis3D;
			}
			return EActionValueType::Boolean;
		}

		// Walk a dotted path from a root UObject down to a leaf FProperty and
		// return its numeric value as a double. Returns true on success.
		// Mirrors the dotted-path resolver in EditorHandlers_PIE.cpp but
		// returns a single double instead of typed JSON.
		bool ResolvePathToDouble(UObject* Root, const FString& Path, double& OutValue)
		{
			if (!Root || Path.IsEmpty()) return false;
			TArray<FString> Parts;
			Path.ParseIntoArray(Parts, TEXT("."));
			if (Parts.Num() == 0) return false;

			UStruct* CurrentStruct = Root->GetClass();
			const void* CurrentContainer = Root;
			FProperty* Property = nullptr;

			for (int32 i = 0; i < Parts.Num(); ++i)
			{
				FProperty* Seg = CurrentStruct->FindPropertyByName(FName(*Parts[i]));

				// Head-position fallback: bare component name.
				if (!Seg && i == 0)
				{
					if (AActor* AsActor = Cast<AActor>(Root))
					{
						UActorComponent* Match = nullptr;
						for (UActorComponent* C : AsActor->GetComponents())
						{
							if (C && C->GetName() == Parts[i]) { Match = C; break; }
						}
						if (Match)
						{
							CurrentContainer = Match;
							CurrentStruct = Match->GetClass();
							continue;
						}
					}
				}
				if (!Seg) return false;

				if (i < Parts.Num() - 1)
				{
					if (FStructProperty* SP = CastField<FStructProperty>(Seg))
					{
						CurrentContainer = SP->ContainerPtrToValuePtr<void>(const_cast<void*>(CurrentContainer));
						CurrentStruct = SP->Struct;
					}
					else if (FObjectProperty* OP = CastField<FObjectProperty>(Seg))
					{
						UObject* Sub = OP->GetObjectPropertyValue(
							OP->ContainerPtrToValuePtr<void>(const_cast<void*>(CurrentContainer)));
						if (!Sub) return false;
						CurrentContainer = Sub;
						CurrentStruct = Sub->GetClass();
					}
					else
					{
						return false;
					}
				}
				else
				{
					Property = Seg;
				}
			}

			if (!Property) return false;
			const void* Value = Property->ContainerPtrToValuePtr<void>(const_cast<void*>(CurrentContainer));

			if (auto* P = CastField<FFloatProperty>(Property))   { OutValue = P->GetPropertyValue(Value); return true; }
			if (auto* P = CastField<FDoubleProperty>(Property))  { OutValue = P->GetPropertyValue(Value); return true; }
			if (auto* P = CastField<FIntProperty>(Property))     { OutValue = P->GetPropertyValue(Value); return true; }
			if (auto* P = CastField<FInt64Property>(Property))   { OutValue = static_cast<double>(P->GetPropertyValue(Value)); return true; }
			if (auto* P = CastField<FInt16Property>(Property))   { OutValue = P->GetPropertyValue(Value); return true; }
			if (auto* P = CastField<FInt8Property>(Property))    { OutValue = P->GetPropertyValue(Value); return true; }
			if (auto* P = CastField<FUInt32Property>(Property))  { OutValue = P->GetPropertyValue(Value); return true; }
			if (auto* P = CastField<FUInt16Property>(Property))  { OutValue = P->GetPropertyValue(Value); return true; }
			if (auto* P = CastField<FByteProperty>(Property))    { OutValue = P->GetPropertyValue(Value); return true; }
			if (auto* P = CastField<FBoolProperty>(Property))    { OutValue = P->GetPropertyValue(Value) ? 1.0 : 0.0; return true; }
			if (auto* P = CastField<FEnumProperty>(Property))
			{
				if (auto* Under = P->GetUnderlyingProperty())
				{
					if (auto* B = CastField<FByteProperty>(Under))   { OutValue = B->GetPropertyValue(Value); return true; }
					if (auto* I = CastField<FIntProperty>(Under))    { OutValue = I->GetPropertyValue(Value); return true; }
				}
			}
			return false;
		}
	}

	FPIEFrameSampler::FPIEFrameSampler() = default;

	void FPIEFrameSampler::SetConfig(const FConfig& InConfig)
	{
		Config = InConfig;
		for (const FString& P : Config.TrackedValuePaths)
		{
			FTrackedValueSpec S;
			S.Path = P;
			S.Type = TEXT("double");
			TrackedValues.Add(S);
		}
	}

	void FPIEFrameSampler::Reset()
	{
		bAttached = false;
		PawnClassPath.Reset();
		PIEWorldPath.Reset();
		Actions.Reset();
		TrackedValues.Reset();
		Tracked.Reset();
		PendingMarkerLabels.Reset();
		PrevPawnLocation = FVector::ZeroVector;
		// Re-seed TrackedValues from config in case the caller calls AttachToPIE again.
		for (const FString& P : Config.TrackedValuePaths)
		{
			FTrackedValueSpec S;
			S.Path = P;
			S.Type = TEXT("double");
			TrackedValues.Add(S);
		}
	}

	void FPIEFrameSampler::DiscoverActions(APlayerController* PC, APawn* Pawn)
	{
		TSet<const UInputAction*> Seen;
		for (const FTrackedAction& T : Tracked)
		{
			if (T.Action.IsValid()) Seen.Add(T.Action.Get());
		}

		TSet<FString> Whitelist;
		for (const FString& P : Config.ActionPaths) Whitelist.Add(P);

		auto TryAdd = [&](const UInputAction* Action)
		{
			if (!Action || Seen.Contains(Action)) return;
			if (!Whitelist.IsEmpty() && !Whitelist.Contains(Action->GetPathName())) return;
			Seen.Add(Action);

			FTrackedAction T;
			T.Action = Action;
			T.Name = Action->GetName();
			T.Path = Action->GetPathName();
			T.ValueType = ConvertValueType(Action->ValueType);
			Tracked.Add(T);

			FActionSpec Spec;
			Spec.Name = T.Name;
			Spec.Path = T.Path;
			Spec.ValueType = T.ValueType;
			Actions.Add(Spec);
		};

		// Event bindings (BindAction calls on the pawn's EIC).
		if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(Pawn->InputComponent))
		{
			for (const TUniquePtr<FEnhancedInputActionEventBinding>& Bind : EIC->GetActionEventBindings())
			{
				TryAdd(Bind->GetAction());
			}
		}

		// IMC-mapped actions: walk all loaded UInputAction assets and check
		// which ones have active instance data in the player input. This
		// catches actions polled via GetActionValue() without BindAction.
		ULocalPlayer* LP = PC->GetLocalPlayer();
		UEnhancedInputLocalPlayerSubsystem* Sub = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		UEnhancedPlayerInput* PlayerInput = Sub ? Sub->GetPlayerInput() : nullptr;
		if (PlayerInput)
		{
			for (TObjectIterator<UInputAction> It; It; ++It)
			{
				const UInputAction* Action = *It;
				if (Action && PlayerInput->FindActionInstanceData(Action))
				{
					TryAdd(Action);
				}
			}
		}
	}

	bool FPIEFrameSampler::AttachToPIE(UWorld* PIEWorld)
	{
		if (bAttached) return true;
		if (!PIEWorld) return false;
		APlayerController* PC = (Config.ClientIndex > 0)
			? UGameplayStatics::GetPlayerController(PIEWorld, Config.ClientIndex)
			: PIEWorld->GetFirstPlayerController();
		if (!PC) return false;
		APawn* Pawn = PC->GetPawn();
		if (!Pawn || !Pawn->InputComponent) return false;

		PawnClassPath = Pawn->GetClass()->GetPathName();
		PIEWorldPath = PIEWorld->GetPathName();

		DiscoverActions(PC, Pawn);

		bAttached = true;
		UE_LOG(LogMCPReplay, Log, TEXT("[PIE-SAMPLER] Attached: %d actions, %d tracked values, pawn=%s"),
			Actions.Num(), TrackedValues.Num(), *PawnClassPath);
		return true;
	}

	FCSVRow FPIEFrameSampler::SampleFrame(UWorld* PIEWorld, uint64 FrameNumber, double GameTime, double DeltaTime)
	{
		FCSVRow Row;
		Row.Frame = FrameNumber;
		Row.Time = GameTime;
		Row.Dt = DeltaTime;

		if (!PIEWorld) return Row;
		APlayerController* PC = (Config.ClientIndex > 0)
			? UGameplayStatics::GetPlayerController(PIEWorld, Config.ClientIndex)
			: PIEWorld->GetFirstPlayerController();
		if (!PC) return Row;
		APawn* Pawn = PC->GetPawn();
		if (!Pawn) return Row;

		// Rescan for late-bound actions (IMCs added after initial attach).
		{
			const int32 Before = Tracked.Num();
			DiscoverActions(PC, Pawn);
			for (int32 i = Before; i < Tracked.Num(); ++i)
			{
				UE_LOG(LogMCPReplay, Log, TEXT("[PIE-SAMPLER] Late-discovered action: %s (%s)"),
					*Tracked[i].Name, *Tracked[i].Path);
			}
		}

		if (Config.bCapturePawnState)
		{
			Row.PawnLocation = Pawn->GetActorLocation();
			Row.PawnRotation = Pawn->GetActorRotation();
			if (ACharacter* Ch = Cast<ACharacter>(Pawn))
			{
				if (UCharacterMovementComponent* Mv = Ch->GetCharacterMovement())
				{
					Row.PawnVelocity = Mv->Velocity;
					Row.Speed2D = Mv->Velocity.Size2D();
				}
			}
			else
			{
				FVector Vel = Pawn->GetVelocity();
				if (Vel.IsNearlyZero())
				{
					if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Pawn->GetRootComponent()))
					{
						Vel = Root->GetPhysicsLinearVelocity();
					}
					if (Vel.IsNearlyZero() && FrameNumber > 0)
					{
						Vel = (Pawn->GetActorLocation() - PrevPawnLocation) / FMath::Max(DeltaTime, 0.001);
					}
				}
				PrevPawnLocation = Pawn->GetActorLocation();
				Row.PawnVelocity = Vel;
				Row.Speed2D = Vel.Size2D();
			}
		}

		if (Config.bCaptureMontage)
		{
			if (ACharacter* Ch = Cast<ACharacter>(Pawn))
			{
				if (USkeletalMeshComponent* Mesh = Ch->GetMesh())
				{
					if (UAnimInstance* Anim = Mesh->GetAnimInstance())
					{
						if (UAnimMontage* M = Anim->GetCurrentActiveMontage())
						{
							FName Section = Anim->Montage_GetCurrentSection(M);
							Row.MontageSection = FString::Printf(TEXT("%s:%s"), *M->GetName(), *Section.ToString());
						}
					}
				}
			}
		}

		// Enhanced Input values.
		ULocalPlayer* LP = PC->GetLocalPlayer();
		UEnhancedInputLocalPlayerSubsystem* Sub = LP ? LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
		UEnhancedPlayerInput* PlayerInput = Sub ? Sub->GetPlayerInput() : nullptr;
		if (PlayerInput)
		{
			for (FTrackedAction& T : Tracked)
			{
				if (!T.Action.IsValid()) continue;
				const FInputActionValue V = PlayerInput->GetActionValue(T.Action.Get());
				bool bNowActive = false;
				switch (T.ValueType)
				{
				case EActionValueType::Boolean:
				{
					const bool B = V.Get<bool>();
					Row.ActionValues.Add(T.Name, FVector(B ? 1.0 : 0.0, 0.0, 0.0));
					bNowActive = B;
					break;
				}
				case EActionValueType::Axis1D:
				{
					const float X = V.Get<float>();
					Row.ActionValues.Add(T.Name, FVector(X, 0.0, 0.0));
					bNowActive = FMath::Abs(X) > Config.AxisThreshold;
					break;
				}
				case EActionValueType::Axis2D:
				{
					const FVector2D X = V.Get<FVector2D>();
					Row.ActionValues.Add(T.Name, FVector(X.X, X.Y, 0.0));
					bNowActive = X.Size() > Config.AxisThreshold;
					break;
				}
				case EActionValueType::Axis3D:
				{
					const FVector X = V.Get<FVector>();
					Row.ActionValues.Add(T.Name, X);
					bNowActive = X.Size() > Config.AxisThreshold;
					break;
				}
				}
				if (bNowActive && !T.bWasActive)
				{
					Row.EdgeEvents.Add(T.Name + TEXT("_pressed"));
				}
				else if (!bNowActive && T.bWasActive)
				{
					Row.EdgeEvents.Add(T.Name + TEXT("_released"));
				}
				T.bWasActive = bNowActive;
			}
		}

		// Tracked reflection values: resolve against the pawn as the root.
		for (const FTrackedValueSpec& S : TrackedValues)
		{
			double Val = 0.0;
			if (ResolvePathToDouble(Pawn, S.Path, Val))
			{
				Row.TrackedValues.Add(S.Path, Val);
			}
			// Missing paths silently sample 0; the recorder doesn't fail on
			// per-frame resolution misses because a subsystem may not yet exist.
		}

		// Drain queued markers into this frame's edge events.
		for (const FString& L : PendingMarkerLabels)
		{
			Row.EdgeEvents.Add(FString::Printf(TEXT("mark:%s"), *L));
		}
		PendingMarkerLabels.Reset();

		return Row;
	}

	void FPIEFrameSampler::QueueMarker(const FString& Label)
	{
		if (!Label.IsEmpty()) PendingMarkerLabels.Add(Label);
	}
}
