#include "PIEInputInjector.h"
#include "UE_MCP_BridgeModule.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Guid.h"
#include "Misc/CoreDelegates.h"

namespace UEMCPPIE
{
	namespace
	{
		struct FHoldEntry
		{
			TWeakObjectPtr<UInputAction> Action;
			FInputActionValue Value;
			FString ActionPath;
			FString ActionName;
			int32 ClientIndex = 0;
		};

		struct FTapeEntry
		{
			TWeakObjectPtr<UInputAction> Action;
			TArray<FVector> Values;
			int32 Index = 0;
			FString ActionPath;
			FString ActionName;
			int32 ClientIndex = 0;
		};

		static TMap<FString, FHoldEntry> GHolds;
		static TMap<FString, FTapeEntry> GTapes;
		static FDelegateHandle GOnEndFrameHandle;
		static bool GTickerBound = false;
		static int32 GIdCounter = 0;

		UWorld* GetPIEWorldLocal()
		{
			if (!GEngine) return nullptr;
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if ((Ctx.WorldType == EWorldType::PIE || Ctx.WorldType == EWorldType::Game) && Ctx.World())
				{
					return Ctx.World();
				}
			}
			return nullptr;
		}

		UEnhancedInputLocalPlayerSubsystem* GetEnhancedInputSubsystemForClient(int32 ClientIndex)
		{
			UWorld* W = GetPIEWorldLocal();
			if (!W) return nullptr;
			APlayerController* PC = (ClientIndex > 0)
				? UGameplayStatics::GetPlayerController(W, ClientIndex)
				: W->GetFirstPlayerController();
			if (!PC) return nullptr;
			ULocalPlayer* LP = PC->GetLocalPlayer();
			if (!LP) return nullptr;
			return LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		}

		UEnhancedInputLocalPlayerSubsystem* GetEnhancedInputSubsystem()
		{
			return GetEnhancedInputSubsystemForClient(0);
		}

		FString GenerateId(const TCHAR* Prefix)
		{
			GIdCounter++;
			return FString::Printf(TEXT("%s-%d-%s"),
				Prefix, GIdCounter, *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8).ToLower());
		}

		FInputActionValue AxisFromVector(EInputActionValueType Type, const FVector& V)
		{
			switch (Type)
			{
			case EInputActionValueType::Boolean: return FInputActionValue(V.X != 0.0);
			case EInputActionValueType::Axis1D:  return FInputActionValue(static_cast<float>(V.X));
			case EInputActionValueType::Axis2D:  return FInputActionValue(FVector2D(V.X, V.Y));
			case EInputActionValueType::Axis3D:  return FInputActionValue(V);
			}
			return FInputActionValue();
		}

		void TickEndOfFrame()
		{
			// Per-client subsystem cache for this tick (avoids re-resolving
			// for every entry; matters when many injections share a client).
			TMap<int32, UEnhancedInputLocalPlayerSubsystem*> ClientSubs;
			auto SubForClient = [&ClientSubs](int32 ClientIndex) -> UEnhancedInputLocalPlayerSubsystem*
			{
				if (UEnhancedInputLocalPlayerSubsystem** Cached = ClientSubs.Find(ClientIndex))
				{
					return *Cached;
				}
				UEnhancedInputLocalPlayerSubsystem* Sub = GetEnhancedInputSubsystemForClient(ClientIndex);
				ClientSubs.Add(ClientIndex, Sub);
				return Sub;
			};

			for (auto It = GHolds.CreateIterator(); It; ++It)
			{
				if (!It->Value.Action.IsValid())
				{
					It.RemoveCurrent();
					continue;
				}
				if (UEnhancedInputLocalPlayerSubsystem* Sub = SubForClient(It->Value.ClientIndex))
				{
					Sub->InjectInputForAction(It->Value.Action.Get(), It->Value.Value, {}, {});
				}
			}

			for (auto It = GTapes.CreateIterator(); It; ++It)
			{
				FTapeEntry& T = It->Value;
				if (!T.Action.IsValid() || T.Index >= T.Values.Num())
				{
					It.RemoveCurrent();
					continue;
				}
				if (UEnhancedInputLocalPlayerSubsystem* Sub = SubForClient(T.ClientIndex))
				{
					const FInputActionValue Val = AxisFromVector(T.Action->ValueType, T.Values[T.Index]);
					Sub->InjectInputForAction(T.Action.Get(), Val, {}, {});
				}
				T.Index++;
			}

			// Self-unbind when nothing remains.
			if (GHolds.Num() == 0 && GTapes.Num() == 0)
			{
				if (GOnEndFrameHandle.IsValid())
				{
					FCoreDelegates::OnEndFrame.Remove(GOnEndFrameHandle);
					GOnEndFrameHandle.Reset();
					GTickerBound = false;
				}
			}
		}

		void EnsureTickerBound()
		{
			if (GTickerBound) return;
			GOnEndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&TickEndOfFrame);
			GTickerBound = true;
		}
	}

	bool FPIEInputInjector::InjectOnce(UInputAction* Action, const FInputActionValue& Value, FString& OutError, int32 ClientIndex)
	{
		if (!Action) { OutError = TEXT("InjectOnce: null action"); return false; }
		UEnhancedInputLocalPlayerSubsystem* Sub = GetEnhancedInputSubsystemForClient(ClientIndex);
		if (!Sub)
		{
			OutError = FString::Printf(TEXT("EnhancedInputLocalPlayerSubsystem not available for client %d (PIE not running or that local player not spawned)"), ClientIndex);
			return false;
		}
		Sub->InjectInputForAction(Action, Value, {}, {});
		return true;
	}

	FString FPIEInputInjector::StartHold(UInputAction* Action, const FInputActionValue& Value, const FString& DesiredId, FString& OutError, int32 ClientIndex)
	{
		if (!Action) { OutError = TEXT("StartHold: null action"); return FString(); }
		FString Id = DesiredId.IsEmpty() ? GenerateId(TEXT("hold")) : DesiredId;
		if (GHolds.Contains(Id) || GTapes.Contains(Id))
		{
			OutError = FString::Printf(TEXT("Injection id '%s' is already in use"), *Id);
			return FString();
		}
		FHoldEntry E;
		E.Action = Action;
		E.Value = Value;
		E.ActionPath = Action->GetPathName();
		E.ActionName = Action->GetName();
		E.ClientIndex = ClientIndex;
		GHolds.Add(Id, E);
		EnsureTickerBound();
		return Id;
	}

	bool FPIEInputInjector::UpdateHold(const FString& Id, const FInputActionValue& Value)
	{
		FHoldEntry* E = GHolds.Find(Id);
		if (!E) return false;
		E->Value = Value;
		return true;
	}

	bool FPIEInputInjector::StopHold(const FString& Id)
	{
		return GHolds.Remove(Id) > 0;
	}

	FString FPIEInputInjector::StartTape(UInputAction* Action, const TArray<FVector>& Values, int32 /*Hz*/, const FString& DesiredId, FString& OutError, int32 ClientIndex)
	{
		if (!Action) { OutError = TEXT("StartTape: null action"); return FString(); }
		if (Values.Num() == 0) { OutError = TEXT("StartTape: empty values array"); return FString(); }
		FString Id = DesiredId.IsEmpty() ? GenerateId(TEXT("tape")) : DesiredId;
		if (GHolds.Contains(Id) || GTapes.Contains(Id))
		{
			OutError = FString::Printf(TEXT("Injection id '%s' is already in use"), *Id);
			return FString();
		}
		FTapeEntry E;
		E.Action = Action;
		E.Values = Values;
		E.Index = 0;
		E.ActionPath = Action->GetPathName();
		E.ActionName = Action->GetName();
		E.ClientIndex = ClientIndex;
		GTapes.Add(Id, E);
		EnsureTickerBound();
		// Hz is consumed by the surrounding replay/recorder pipeline which
		// pins t.MaxFPS; the injector itself runs once per end-of-frame.
		return Id;
	}

	bool FPIEInputInjector::StopTape(const FString& Id)
	{
		return GTapes.Remove(Id) > 0;
	}

	bool FPIEInputInjector::StopAny(const FString& Id)
	{
		const bool A = StopHold(Id);
		const bool B = StopTape(Id);
		return A || B;
	}

	TArray<FInjectionStatus> FPIEInputInjector::List()
	{
		TArray<FInjectionStatus> Out;
		for (const TPair<FString, FHoldEntry>& KV : GHolds)
		{
			FInjectionStatus S;
			S.Id = KV.Key;
			S.ActionPath = KV.Value.ActionPath;
			S.ActionName = KV.Value.ActionName;
			S.bIsTape = false;
			S.CurrentValue = KV.Value.Value;
			Out.Add(S);
		}
		for (const TPair<FString, FTapeEntry>& KV : GTapes)
		{
			FInjectionStatus S;
			S.Id = KV.Key;
			S.ActionPath = KV.Value.ActionPath;
			S.ActionName = KV.Value.ActionName;
			S.bIsTape = true;
			S.TapeIndex = KV.Value.Index;
			S.TapeTotal = KV.Value.Values.Num();
			Out.Add(S);
		}
		return Out;
	}

	void FPIEInputInjector::Init()
	{
		// Nothing to do; the ticker self-binds on demand.
	}

	void FPIEInputInjector::Shutdown()
	{
		GHolds.Reset();
		GTapes.Reset();
		if (GOnEndFrameHandle.IsValid())
		{
			FCoreDelegates::OnEndFrame.Remove(GOnEndFrameHandle);
			GOnEndFrameHandle.Reset();
			GTickerBound = false;
		}
	}

	void FPIEInputInjector::OnPIEEnded()
	{
		GHolds.Reset();
		GTapes.Reset();
	}
}
