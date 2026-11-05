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
		// return its numeric value as a double. Returns true on success; on
		// failure OutWhy (when given) names the segment that stopped the walk.
		bool ResolvePathToDouble(UObject* Root, const FString& Path, double& OutValue, FString* OutWhy = nullptr)
		{
			auto Fail = [OutWhy](const FString& Why) { if (OutWhy) *OutWhy = Why; return false; };
			if (!Root) return Fail(TEXT("no object to read from"));
			if (Path.IsEmpty()) return Fail(TEXT("empty path"));
			TArray<FString> Parts;
			Path.ParseIntoArray(Parts, TEXT("."));
			if (Parts.Num() == 0) return Fail(TEXT("empty path"));

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
				if (!Seg)
				{
					return Fail(FString::Printf(TEXT("no property '%s' on %s"), *Parts[i], *CurrentStruct->GetName()));
				}

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
						if (!Sub) return Fail(FString::Printf(TEXT("'%s' is null"), *Parts[i]));
						CurrentContainer = Sub;
						CurrentStruct = Sub->GetClass();
					}
					else
					{
						return Fail(FString::Printf(TEXT("'%s' is a %s and has no fields"), *Parts[i], *Seg->GetCPPType()));
					}
				}
				else
				{
					Property = Seg;
				}
			}

			if (!Property) return Fail(TEXT("path names a component, not a value"));
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
			if (FStructProperty* SP = CastField<FStructProperty>(Property))
			{
				return Fail(FString::Printf(TEXT("ends at struct %s; name a numeric field, e.g. %s.X"),
					*SP->Struct->GetName(), *Path));
			}
			return Fail(FString::Printf(TEXT("ends at %s, which is not numeric"), *Property->GetCPPType()));
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

	FString FPIEFrameSampler::NormalizeActionPath(const FString& InPath)
	{
		FString Path = InPath.TrimStartAndEnd();
		// Export text: InputAction'/Game/Input/IA_Move.IA_Move'
		int32 Quote = INDEX_NONE;
		if (Path.FindChar(TEXT('\''), Quote) && Path.Len() > Quote + 1 && Path.EndsWith(TEXT("'")))
		{
			Path = Path.Mid(Quote + 1, Path.Len() - Quote - 2);
		}
		FString Name;
		if (!Path.Split(TEXT("/"), nullptr, &Name, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			return Path;
		}
		if (Name.IsEmpty() || Name.Contains(TEXT(".")))
		{
			return Path;
		}
		return Path + TEXT(".") + Name;
	}

	void FPIEFrameSampler::SetConfig(const FConfig& InConfig)
	{
		Config = InConfig;
		for (FString& P : Config.ActionPaths)
		{
			P = NormalizeActionPath(P);
		}
		// Replace, never append: a session armed without track_values must
		// not inherit the previous session's paths (#1690).
		SeedTrackedValues();
	}

	void FPIEFrameSampler::SeedTrackedValues()
	{
		TrackedValues.Reset();
		for (const FString& P : Config.TrackedValuePaths)
		{
			FTrackedValueSpec S;
			S.Path = P;
			S.Type = TEXT("double");
			TrackedValues.Add(S);
		}
	}

	TArray<FString> FPIEFrameSampler::GetUnmatchedActionPaths() const
	{
		TArray<FString> Out;
		if (!bAttached) return Out;
		for (const FString& P : Config.ActionPaths)
		{
			const bool bMatched = Tracked.ContainsByPredicate([&P](const FTrackedAction& T) { return T.Path == P; });
			if (!bMatched) Out.Add(P);
		}
		return Out;
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
		Warnings.Reset();
		TrackedActorRefs.Reset();
		PrevPawnLocation = FVector::ZeroVector;
		// Re-seed from the current config so AttachToPIE can run again.
		SeedTrackedValues();
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

		// Event bindings (BindAction calls) on the pawn's and the controller's
		// input components.
		UInputComponent* const InputComponents[] = { Pawn->InputComponent, PC->InputComponent };
		for (UInputComponent* IC : InputComponents)
		{
			if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(IC))
			{
				for (const TUniquePtr<FEnhancedInputActionEventBinding>& Bind : EIC->GetActionEventBindings())
				{
					TryAdd(Bind->GetAction());
				}
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
		Warnings.Reset();
		for (const FString& Missing : GetUnmatchedActionPaths())
		{
			Warnings.Add(FString::Printf(
				TEXT("actions: '%s' is not bound or mapped for this player, so it is not recorded"), *Missing));
		}
		ProbeTrackedValues(PIEWorld, Pawn);
		UE_LOG(LogMCPPIE, Log, TEXT("[PIE-SAMPLER] Attached: %d actions, %d tracked values, pawn=%s"),
			Actions.Num(), TrackedValues.Num(), *PawnClassPath);
		for (const FString& W : Warnings)
		{
			UE_LOG(LogMCPPIE, Warning, TEXT("[PIE-SAMPLER] %s"), *W);
		}
		return true;
	}

	AActor* FPIEFrameSampler::FindTrackedActor(UWorld* PIEWorld, const FString& Id)
	{
		FTrackedActorRef& Ref = TrackedActorRefs.FindOrAdd(Id);
		if (AActor* Cached = Ref.Actor.Get())
		{
			if (Cached->GetWorld() == PIEWorld) return Cached;
			Ref.Actor.Reset();
		}
		const double Now = FPlatformTime::Seconds();
		if (Now < Ref.NextRetrySeconds) return nullptr;
		AActor* Found = FindActorById(PIEWorld, Id);
		Ref.Actor = Found;
		Ref.NextRetrySeconds = Found ? 0.0 : Now + 1.0;
		return Found;
	}

	bool FPIEFrameSampler::SampleTrackedValue(UWorld* PIEWorld, APawn* Pawn, const FString& Path, double& OutValue, FString* OutWhy)
	{
		if (Path.StartsWith(TEXT("sub:")))
		{
			FString ClassName, PropPath;
			if (!Path.RightChop(4).Split(TEXT("."), &ClassName, &PropPath))
			{
				if (OutWhy) *OutWhy = TEXT("expected sub:<Class>.<path>");
				return false;
			}
			UObject* SubObj = ResolveSubsystemByName(PIEWorld, ClassName, Config.ClientIndex);
			if (!SubObj)
			{
				if (OutWhy) *OutWhy = FString::Printf(TEXT("no subsystem '%s' in the PIE world"), *ClassName);
				return false;
			}
			return ResolvePathToDouble(SubObj, PropPath, OutValue, OutWhy);
		}
		if (Path.StartsWith(TEXT("bone:")) || Path.StartsWith(TEXT("bonecs:")))
		{
			const bool bCS = Path.StartsWith(TEXT("bonecs:"));
			if (ResolveBoneChannel(Pawn, Path.RightChop(bCS ? 7 : 5), bCS, OutValue))
			{
				return true;
			}
			if (OutWhy) *OutWhy = TEXT("no such bone or socket on the pawn's skeletal mesh, or an unknown channel");
			return false;
		}
		FString PawnWhy;
		if (ResolvePathToDouble(Pawn, Path, OutValue, &PawnWhy))
		{
			return true;
		}
		// "<Actor>.<path>": the head names a level actor the same way
		// tracked_actors and expect {op: exists} do (#1655).
		FString Head, Rest;
		if (Path.Split(TEXT("."), &Head, &Rest) && !Rest.IsEmpty())
		{
			if (AActor* Actor = FindTrackedActor(PIEWorld, Head))
			{
				return ResolvePathToDouble(Actor, Rest, OutValue, OutWhy);
			}
		}
		if (OutWhy)
		{
			*OutWhy = Head.IsEmpty()
				? PawnWhy
				: FString::Printf(TEXT("%s on the pawn, and no actor in the PIE world is named '%s'"), *PawnWhy, *Head);
		}
		return false;
	}

	void FPIEFrameSampler::ProbeTrackedValues(UWorld* PIEWorld, APawn* Pawn)
	{
		for (FTrackedValueSpec& S : TrackedValues)
		{
			double Unused = 0.0;
			FString Why;
			if (SampleTrackedValue(PIEWorld, Pawn, S.Path, Unused, &Why))
			{
				S.Type = TEXT("double");
				continue;
			}
			// Kept as a column, since a subsystem can appear later; the type
			// says it never resolved so a column of zeros is not read as data.
			S.Type = TEXT("unresolved");
			Warnings.Add(FString::Printf(TEXT("track_values: '%s' does not resolve to a number (%s)"), *S.Path, *Why));
		}
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

		// Tracked reflection values: resolve against the pawn, a level actor
		// ("<Actor>.<path>"), or a named subsystem ("sub:<Class>.<path>").
		// A miss writes 0 to the CSV; the spec's type and the attach warning
		// say so. A path that resolves later is promoted back to double.
		for (FTrackedValueSpec& S : TrackedValues)
		{
			double Val = 0.0;
			if (SampleTrackedValue(PIEWorld, Pawn, S.Path, Val, nullptr))
			{
				Row.TrackedValues.Add(S.Path, Val);
				S.Type = TEXT("double");
			}
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
