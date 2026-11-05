#include "Handlers/PIE/PIEFrameSampler.h"
#include "Handlers/PIE/PIELog.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "Handlers/PIE/PIEWorldResolver.h"
#include "UObject/UObjectIterator.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/ActorComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "UObject/UnrealType.h"
#include "RenderCore.h"       // GGameThreadTime / GRenderThreadTime
#include "RHI.h"              // RHIGetGPUFrameTime
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMemory.h"
#include "Engine/GameInstance.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "Subsystems/LocalPlayerSubsystem.h"

namespace MCPPIE
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

		// pie-studio#4 (ue-mcp#756): sample one channel of a named bone or socket
		// transform per frame. ue-mcp's read_bone_transforms is a point-in-time
		// read by design; a bounded frame range with timestamps is observation,
		// so it belongs on this side of the boundary, and the tracked-value CSV
		// already carries the timestamps.
		//
		// Spec: "bone:<BoneOrSocket>.<channel>"   world space
		//       "bonecs:<BoneOrSocket>.<channel>" component space
		// Channel: x|y|z, pitch|yaw|roll, scalex|scaley|scalez.
		bool ResolveBoneChannel(APawn* Pawn, const FString& Spec, bool bComponentSpace, double& OutValue)
		{
			FString BoneName, Channel;
			// Split on the LAST dot: socket names may legitimately contain dots,
			// and the channel never does.
			if (!Spec.Split(TEXT("."), &BoneName, &Channel, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
			{
				return false;
			}
			BoneName.TrimStartAndEndInline();
			Channel = Channel.TrimStartAndEnd().ToLower();
			if (!Pawn || BoneName.IsEmpty() || Channel.IsEmpty()) return false;

			USkeletalMeshComponent* Mesh = Pawn->FindComponentByClass<USkeletalMeshComponent>();
			if (!Mesh) return false;
			const FName Bone(*BoneName);
			// DoesSocketExist covers sockets; GetBoneIndex covers raw bones.
			if (!Mesh->DoesSocketExist(Bone) && Mesh->GetBoneIndex(Bone) == INDEX_NONE) return false;

			const FTransform T = Mesh->GetSocketTransform(
				Bone, bComponentSpace ? RTS_Component : RTS_World);
			const FVector Loc = T.GetLocation();
			const FRotator Rot = T.Rotator();
			const FVector Scale = T.GetScale3D();

			if (Channel == TEXT("x"))      { OutValue = Loc.X;    return true; }
			if (Channel == TEXT("y"))      { OutValue = Loc.Y;    return true; }
			if (Channel == TEXT("z"))      { OutValue = Loc.Z;    return true; }
			if (Channel == TEXT("pitch"))  { OutValue = Rot.Pitch; return true; }
			if (Channel == TEXT("yaw"))    { OutValue = Rot.Yaw;   return true; }
			if (Channel == TEXT("roll"))   { OutValue = Rot.Roll;  return true; }
			if (Channel == TEXT("scalex")) { OutValue = Scale.X;  return true; }
			if (Channel == TEXT("scaley")) { OutValue = Scale.Y;  return true; }
			if (Channel == TEXT("scalez")) { OutValue = Scale.Z;  return true; }
			return false;
		}

		// Resolve a subsystem in the PIE world by short (or "U"-prefixed) class
		// name, so tracked paths of the form "sub:MyGameSubsystem.Phase" can be
		// sampled per frame (item 2b).
		UObject* ResolveSubsystemByName(UWorld* World, const FString& ClassName, int32 ClientIndex)
		{
			if (!World || ClientIndex < 0)
			{
				return nullptr;
			}
			auto Match = [&ClassName](UObject* O) -> bool
			{
				if (!O) return false;
				const FString N = O->GetClass()->GetName();
				return N == ClassName
					|| N == (FString(TEXT("U")) + ClassName)
					|| O->GetClass()->GetPathName() == ClassName;
			};
			if (UGameInstance* GI = World->GetGameInstance())
			{
				for (UGameInstanceSubsystem* S : GI->GetSubsystemArrayCopy<UGameInstanceSubsystem>())
				{
					if (Match(S)) return S;
				}

				const TArray<ULocalPlayer*>& LocalPlayers = GI->GetLocalPlayers();
				if (LocalPlayers.IsValidIndex(ClientIndex) && LocalPlayers[ClientIndex])
				{
					for (ULocalPlayerSubsystem* S :
						LocalPlayers[ClientIndex]->GetSubsystemArrayCopy<ULocalPlayerSubsystem>())
					{
						if (Match(S)) return S;
					}
				}
			}
			for (UWorldSubsystem* S : World->GetSubsystemArrayCopy<UWorldSubsystem>())
			{
				if (Match(S)) return S;
			}
			return nullptr;
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
		if (bAttached)
		{
			return true;
		}
		if (!PIEWorld)
		{
			return false;
		}

		PIEWorldResolver::FPlayerTarget Target;
		FString Error;
		if (!PIEWorldResolver::ResolvePlayerInWorld(PIEWorld, Config.ClientIndex, Target, Error))
		{
			return false;
		}

		APlayerController* PC = Target.PlayerController;
		if (!PC)
		{
			return false;
		}
		APawn* Pawn = PC->GetPawn();
		if (!Pawn || !Pawn->InputComponent)
		{
			return false;
		}

		PawnClassPath = Pawn->GetClass()->GetPathName();
		PIEWorldPath = PIEWorld->GetPathName();
		DiscoverActions(PC, Pawn);

		bAttached = true;
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-SAMPLER] Attached: %d actions, %d tracked values, pawn=%s"),
			Actions.Num(), TrackedValues.Num(), *PawnClassPath);
		return true;
	}

	FCSVRow FPIEFrameSampler::SampleFrame(UWorld* PIEWorld, uint64 FrameNumber, double GameTime, double DeltaTime)
	{
		FCSVRow Row;
		Row.Frame = FrameNumber;
		Row.Time = GameTime;
		Row.Dt = DeltaTime;

		// Per-frame performance (item 4a). Cheap global reads; populated every
		// frame so the recorder can persist them and perf_summary can aggregate.
		Row.GameMs = static_cast<float>(FPlatformTime::ToMilliseconds(GGameThreadTime));
		Row.RenderMs = static_cast<float>(FPlatformTime::ToMilliseconds(GRenderThreadTime));
		Row.GpuMs = static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));
		Row.MemMB = static_cast<float>(FPlatformMemory::GetStats().UsedPhysical / (1024.0 * 1024.0));

		if (!PIEWorld)
		{
			return Row;
		}

		PIEWorldResolver::FPlayerTarget Target;
		FString Error;
		if (!PIEWorldResolver::ResolvePlayerInWorld(PIEWorld, Config.ClientIndex, Target, Error))
		{
			return Row;
		}

		APlayerController* PC = Target.PlayerController;
		if (!PC)
		{
			return Row;
		}
		APawn* Pawn = PC->GetPawn();
		if (!Pawn)
		{
			return Row;
		}

		// Rescan for late-bound actions (IMCs added after initial attach).
		{
			const int32 Before = Tracked.Num();
			DiscoverActions(PC, Pawn);
			for (int32 i = Before; i < Tracked.Num(); ++i)
			{
				UE_LOG(LogMCPPIE, Log, TEXT("[PIE-SAMPLER] Late-discovered action: %s (%s)"),
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

		// Tracked reflection values: resolve against the pawn, or against a named
		// subsystem for "sub:<Class>.<path>" entries (item 2b).
		for (const FTrackedValueSpec& S : TrackedValues)
		{
			double Val = 0.0;
			if (S.Path.StartsWith(TEXT("sub:")))
			{
				const FString Rest = S.Path.RightChop(4);
				FString ClassName, PropPath;
				if (Rest.Split(TEXT("."), &ClassName, &PropPath))
				{
					if (UObject* SubObj = ResolveSubsystemByName(PIEWorld, ClassName, Config.ClientIndex))
					{
						if (ResolvePathToDouble(SubObj, PropPath, Val))
						{
							Row.TrackedValues.Add(S.Path, Val);
						}
					}
				}
			}
			else if (S.Path.StartsWith(TEXT("bone:")))
			{
				if (ResolveBoneChannel(Pawn, S.Path.RightChop(5), /*bComponentSpace=*/false, Val))
				{
					Row.TrackedValues.Add(S.Path, Val);
				}
			}
			else if (S.Path.StartsWith(TEXT("bonecs:")))
			{
				if (ResolveBoneChannel(Pawn, S.Path.RightChop(7), /*bComponentSpace=*/true, Val))
				{
					Row.TrackedValues.Add(S.Path, Val);
				}
			}
			else if (ResolvePathToDouble(Pawn, S.Path, Val))
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
