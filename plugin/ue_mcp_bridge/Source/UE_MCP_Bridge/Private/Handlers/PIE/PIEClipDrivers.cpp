#include "Handlers/PIE/PIEClipDrivers.h"
#include "Handlers/PIE/PIERemotePlayers.h"
#include "Handlers/PIE/PIELog.h"
#include "MCPPIERemoteControl.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"

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

		/** The run's own player or a remote client: Input Actions. */
		class FClipPlayerDriver final : public FClipPerformerDriver
		{
		public:
			FClipPlayerDriver(const FClipPerformer& InPerformer, const FClipDriverContext& InContext)
				: FClipPerformerDriver(InPerformer), Context(InContext) {}

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
	}

	TUniquePtr<FClipPerformerDriver> MakeClipPerformerDriver(const FClipPerformer& Performer, const FClipDriverContext& Context)
	{
		switch (Performer.Kind)
		{
		case EClipPerformerKind::Player: return MakeUnique<FClipPlayerDriver>(Performer, Context);
		}
		return nullptr;
	}
}
