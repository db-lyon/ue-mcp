#include "MCPPIEInputInjector.h"
#include "UE_MCP_BridgeRuntimeModule.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameInstance.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "Misc/Guid.h"
#include "Misc/CoreDelegates.h"

namespace MCPPIE
{
	namespace
	{
		struct FHoldEntry
		{
			TWeakObjectPtr<UInputAction> Action;
			TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Subsystem;
			FInputActionValue Value;
			FString ActionPath;
			FString ActionName;
		};

		struct FTapeEntry
		{
			TWeakObjectPtr<UInputAction> Action;
			TWeakObjectPtr<UEnhancedInputLocalPlayerSubsystem> Subsystem;
			TArray<FVector> Values;
			int32 Index = 0;
			FString ActionPath;
			FString ActionName;
		};

		static TMap<FString, FHoldEntry> GHolds;
		static TMap<FString, FTapeEntry> GTapes;
		static FDelegateHandle GOnEndFrameHandle;
		static bool GTickerBound = false;
		static int32 GIdCounter = 0;
		static FPlayerResolver GPlayerResolver;
		static FOnInjectionFinished GOnTapeFinished;

		void StopContinuousInjection(
			UEnhancedInputLocalPlayerSubsystem* Subsystem, UInputAction* Action)
		{
			if (Subsystem && Action)
			{
				Subsystem->StopContinuousInputInjectionForAction(Action);
			}
		}

		void StopHoldEntry(const FHoldEntry& Entry)
		{
			StopContinuousInjection(Entry.Subsystem.Get(), Entry.Action.Get());
		}

		void StopTapeEntry(const FTapeEntry& Entry)
		{
			StopContinuousInjection(Entry.Subsystem.Get(), Entry.Action.Get());
		}

		void UnbindTicker()
		{
			if (GOnEndFrameHandle.IsValid())
			{
				FCoreDelegates::OnEndFrame.Remove(GOnEndFrameHandle);
				GOnEndFrameHandle.Reset();
			}
			GTickerBound = false;
		}

		void StopAll()
		{
			for (const TPair<FString, FHoldEntry>& Pair : GHolds)
			{
				StopHoldEntry(Pair.Value);
			}
			for (const TPair<FString, FTapeEntry>& Pair : GTapes)
			{
				StopTapeEntry(Pair.Value);
			}
			GHolds.Reset();
			GTapes.Reset();
			UnbindTicker();
		}

		bool ResolveEnhancedInputSubsystem(int32 RequestedPIEInstance, int32 ClientIndex,
			ULocalPlayer* ExactLocalPlayer,
			UEnhancedInputLocalPlayerSubsystem*& OutSubsystem, FString& OutError)
		{
			OutSubsystem = nullptr;

			if (ExactLocalPlayer)
			{
				UWorld* ExactWorld = ExactLocalPlayer->GetWorld();
				APlayerController* PlayerController = ExactLocalPlayer->PlayerController;
				if (!ExactWorld
					|| (ExactWorld->WorldType != EWorldType::PIE && ExactWorld->WorldType != EWorldType::Game)
					|| !PlayerController
					|| PlayerController->GetWorld() != ExactWorld)
				{
					OutError = FString::Printf(TEXT("Exact local-player target is no longer valid for PIE instance %d local player %d"),
						RequestedPIEInstance, ClientIndex);
					return false;
				}

				OutSubsystem = ExactLocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
				if (!OutSubsystem)
				{
					OutError = FString::Printf(TEXT("EnhancedInputLocalPlayerSubsystem not available for exact PIE instance %d local player %d"),
						RequestedPIEInstance, ClientIndex);
					return false;
				}
				return true;
			}

			if (!GPlayerResolver)
			{
				OutError = TEXT("No player resolver is installed in this process; pass the exact local player");
				return false;
			}
			ULocalPlayer* Resolved = GPlayerResolver(RequestedPIEInstance, ClientIndex, OutError);
			if (!Resolved)
			{
				return false;
			}

			OutSubsystem = Resolved->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
			if (!OutSubsystem)
			{
				OutError = FString::Printf(TEXT("EnhancedInputLocalPlayerSubsystem not available for PIE instance %d local player %d"),
					RequestedPIEInstance, ClientIndex);
				return false;
			}

			return true;
		}

		bool HasActiveTarget(UEnhancedInputLocalPlayerSubsystem* Subsystem, UInputAction* Action)
		{
			for (const TPair<FString, FHoldEntry>& Pair : GHolds)
			{
				if (Pair.Value.Subsystem.Get() == Subsystem && Pair.Value.Action.Get() == Action)
				{
					return true;
				}
			}
			for (const TPair<FString, FTapeEntry>& Pair : GTapes)
			{
				if (Pair.Value.Subsystem.Get() == Subsystem && Pair.Value.Action.Get() == Action)
				{
					return true;
				}
			}
			return false;
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
			for (auto It = GHolds.CreateIterator(); It; ++It)
			{
				FHoldEntry& Hold = It->Value;
				if (!Hold.Action.IsValid() || !Hold.Subsystem.IsValid())
				{
					StopHoldEntry(Hold);
					It.RemoveCurrent();
				}
			}

			for (auto It = GTapes.CreateIterator(); It; ++It)
			{
				FTapeEntry& Tape = It->Value;
				if (!Tape.Action.IsValid() || !Tape.Subsystem.IsValid())
				{
					StopTapeEntry(Tape);
					It.RemoveCurrent();
					continue;
				}

				if (Tape.Index >= Tape.Values.Num())
				{
					StopTapeEntry(Tape);
					const FString FinishedId = It->Key;
					It.RemoveCurrent();
					GOnTapeFinished.Broadcast(FinishedId);
					continue;
				}

				const FInputActionValue Value = AxisFromVector(
					Tape.Action->ValueType, Tape.Values[Tape.Index]);
				Tape.Subsystem->UpdateValueOfContinuousInputInjectionForAction(
					Tape.Action.Get(), Value);
				Tape.Index++;
			}

			if (GHolds.Num() == 0 && GTapes.Num() == 0)
			{
				UnbindTicker();
			}
		}

		void EnsureTickerBound()
		{
			if (GTickerBound)
			{
				return;
			}
			GOnEndFrameHandle = FCoreDelegates::OnEndFrame.AddStatic(&TickEndOfFrame);
			GTickerBound = true;
		}
	}

	void FPIEInputInjector::SetPlayerResolver(FPlayerResolver Resolver)
	{
		GPlayerResolver = MoveTemp(Resolver);
	}

	FOnInjectionFinished& FPIEInputInjector::OnTapeFinished()
	{
		return GOnTapeFinished;
	}

	bool FPIEInputInjector::InjectOnce(UInputAction* Action, const FInputActionValue& Value,
		FString& OutError, int32 ClientIndex, int32 PIEInstance, ULocalPlayer* ExactLocalPlayer)
	{
		if (!Action)
		{
			OutError = TEXT("InjectOnce: null action");
			return false;
		}

		UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
		if (!ResolveEnhancedInputSubsystem(PIEInstance, ClientIndex, ExactLocalPlayer, Subsystem, OutError))
		{
			return false;
		}

		Subsystem->InjectInputForAction(Action, Value, {}, {});
		return true;
	}

	FString FPIEInputInjector::StartHold(UInputAction* Action, const FInputActionValue& Value,
		const FString& DesiredId, FString& OutError, int32 ClientIndex, int32 PIEInstance,
		ULocalPlayer* ExactLocalPlayer)
	{
		if (!Action)
		{
			OutError = TEXT("StartHold: null action");
			return FString();
		}

		const FString Id = DesiredId.IsEmpty() ? GenerateId(TEXT("hold")) : DesiredId;
		if (GHolds.Contains(Id) || GTapes.Contains(Id))
		{
			OutError = FString::Printf(TEXT("Injection id '%s' is already in use"), *Id);
			return FString();
		}

		UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
		if (!ResolveEnhancedInputSubsystem(PIEInstance, ClientIndex, ExactLocalPlayer, Subsystem, OutError))
		{
			return FString();
		}
		if (HasActiveTarget(Subsystem, Action))
		{
			OutError = FString::Printf(TEXT("Action '%s' already has an active hold or tape for the selected player"),
				*Action->GetPathName());
			return FString();
		}

		FHoldEntry Entry;
		Entry.Action = Action;
		Entry.Subsystem = Subsystem;
		Entry.Value = Value;
		Entry.ActionPath = Action->GetPathName();
		Entry.ActionName = Action->GetName();
		Subsystem->StartContinuousInputInjectionForAction(Action, Value, {}, {});
		GHolds.Add(Id, Entry);
		EnsureTickerBound();
		return Id;
	}

	bool FPIEInputInjector::UpdateHold(const FString& Id, const FInputActionValue& Value)
	{
		FHoldEntry* Entry = GHolds.Find(Id);
		if (!Entry)
		{
			return false;
		}
		if (!Entry->Action.IsValid() || !Entry->Subsystem.IsValid())
		{
			StopHold(Id);
			return false;
		}
		Entry->Value = Value;
		Entry->Subsystem->UpdateValueOfContinuousInputInjectionForAction(
			Entry->Action.Get(), Value);
		return true;
	}

	bool FPIEInputInjector::StopHold(const FString& Id)
	{
		FHoldEntry* Entry = GHolds.Find(Id);
		if (!Entry)
		{
			return false;
		}
		StopHoldEntry(*Entry);
		GHolds.Remove(Id);
		if (GHolds.Num() == 0 && GTapes.Num() == 0)
		{
			UnbindTicker();
		}
		return true;
	}

	FString FPIEInputInjector::StartTape(UInputAction* Action, const TArray<FVector>& Values,
		int32 /*Hz*/, const FString& DesiredId, FString& OutError, int32 ClientIndex,
		int32 PIEInstance, ULocalPlayer* ExactLocalPlayer)
	{
		if (!Action)
		{
			OutError = TEXT("StartTape: null action");
			return FString();
		}
		if (Values.Num() == 0)
		{
			OutError = TEXT("StartTape: empty values array");
			return FString();
		}

		const FString Id = DesiredId.IsEmpty() ? GenerateId(TEXT("tape")) : DesiredId;
		if (GHolds.Contains(Id) || GTapes.Contains(Id))
		{
			OutError = FString::Printf(TEXT("Injection id '%s' is already in use"), *Id);
			return FString();
		}

		UEnhancedInputLocalPlayerSubsystem* Subsystem = nullptr;
		if (!ResolveEnhancedInputSubsystem(PIEInstance, ClientIndex, ExactLocalPlayer, Subsystem, OutError))
		{
			return FString();
		}
		if (HasActiveTarget(Subsystem, Action))
		{
			OutError = FString::Printf(TEXT("Action '%s' already has an active hold or tape for the selected player"),
				*Action->GetPathName());
			return FString();
		}

		FTapeEntry Entry;
		Entry.Action = Action;
		Entry.Subsystem = Subsystem;
		Entry.Values = Values;
		Entry.Index = 1;
		Entry.ActionPath = Action->GetPathName();
		Entry.ActionName = Action->GetName();
		const FInputActionValue FirstValue = AxisFromVector(Action->ValueType, Values[0]);
		Subsystem->StartContinuousInputInjectionForAction(Action, FirstValue, {}, {});
		GTapes.Add(Id, Entry);
		EnsureTickerBound();
		return Id;
	}

	bool FPIEInputInjector::StopTape(const FString& Id)
	{
		FTapeEntry* Entry = GTapes.Find(Id);
		if (!Entry)
		{
			return false;
		}
		StopTapeEntry(*Entry);
		GTapes.Remove(Id);
		if (GHolds.Num() == 0 && GTapes.Num() == 0)
		{
			UnbindTicker();
		}
		return true;
	}

	bool FPIEInputInjector::StopAny(const FString& Id)
	{
		const bool A = StopHold(Id);
		const bool B = StopTape(Id);
		return A || B;
	}

	bool FPIEInputInjector::IsActive(const FString& Id)
	{
		return GHolds.Contains(Id) || GTapes.Contains(Id);
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
		StopAll();
	}

	void FPIEInputInjector::OnPIEEnded()
	{
		StopAll();
	}
}
