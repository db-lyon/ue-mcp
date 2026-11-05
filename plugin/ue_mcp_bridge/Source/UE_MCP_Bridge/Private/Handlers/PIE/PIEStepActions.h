#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UWorld;

/**
 * The step actions pie(uat_run) drives and a test script step's act runs: input on any player, console,
 * place, capture and wait. One executor for both, so a verb means the same thing wherever it is written.
 *
 *   { client?, tape: {action, values} } | { client?, press: {action, frames?} } | { client?, hold: {action, value, ms} }
 *   | { client?, inject: {action, value} } | { client?, console: "cmd" }
 *   | { client?, place: {at, offset?, face?} | {location, yaw?} } | { client?, capture: "label" | true } | { wait_ms }
 *
 * Client 0 is the editor's own player; a client above 0 is a remote player, reached through PIERemotePlayers.
 */
namespace MCPPIE
{
	class FPIEViewportCapture;

	struct FStepAction
	{
		enum class EKind { Tape, Hold, Inject, Console, Wait, Place, Capture } Kind = EKind::Wait;
		// The verb as written: tape, press, hold, inject, console, place, capture or wait_ms.
		FString Verb;
		int32 Client = 0;
		FString ActionPath;
		TArray<FVector> Values;
		FVector Value = FVector(1, 0, 0);
		float Ms = 0.0f;
		FString Command;
		// Place: next to actor `At` (offset in its local frame, facing it), or at `Value` with `Yaw`.
		FString At;
		FVector Offset = FVector(300, 0, 0);
		bool bFace = true;
		TOptional<double> Yaw;

		bool UsesInputAction() const { return Kind == EKind::Tape || Kind == EKind::Hold || Kind == EKind::Inject; }
	};

	/** Parses one action. False with an error naming what is wrong. */
	bool ParseStepAction(const TSharedPtr<FJsonObject>& O, FStepAction& Out, FString& OutError);

	/** The world step actions act on: the server's when one is running, else the PIE world. */
	UWorld* StepActionWorld();

	/** Runs step actions against a live PIE session and tracks what they leave in flight. */
	class FStepActionRunner
	{
	public:
		struct FRelayed { FString Id; int32 Client = 0; FString What; };
		// Host captures report through Written: 0 pending, 1 written, 2 failed. Remote ones report through their relayed result.
		struct FCapture { int32 Client = 0; FString File; TSharedPtr<int32> Written; };

		FStepActionRunner();
		~FStepActionRunner();

		/** Captures go to <CaptureRoot>/captures/step<NN>_client<C>_<label>.jpg; File is relative to CaptureRoot. */
		FString CaptureRoot;
		int32 StepNumber = 1;

		/** Runs one action now. A wait does nothing here; FStepActionSequence times it. */
		bool Run(const FStepAction& A, FString& OutError);

		/** Ends holds whose time is up. Call every tick. */
		void TickStops(double Now);

		/** Forgets the previous step's relayed requests and captures. */
		void BeginStep();

		/** Every relayed request has been answered. */
		bool RelayedSettled() const;
		/** No host capture is still pending. */
		bool HostCapturesWritten() const;
		/** No hold is waiting for its stop and no tape started on the editor's player is still playing. */
		bool InputIdle() const;

		/** Stops every hold and tape this runner started and releases the host capture. */
		void Reset();

		TArray<FRelayed> Relayed;
		TArray<FCapture> Captures;

	private:
		bool Place(const FStepAction& A, FString& OutError);
		bool RequestCapture(const FStepAction& A, FString& OutError);

		struct FPendingStop { double At = 0.0; int32 Client = 0; FString Id; };
		TArray<FPendingStop> PendingStops;
		TArray<FString> LocalTapes;
		TSharedPtr<FPIEViewportCapture> HostCapture;
	};

	/** Walks one step's actions in order: a wait holds the sequence for its time, any other action runs at once. */
	class FStepActionSequence
	{
	public:
		enum class EOutcome { Running, Done, Failed };

		struct FRecord
		{
			int32 Index = 0;
			FString Verb;
			bool bOk = true;
			FString Error;
		};

		void Begin(const TArray<FStepAction>& InActions);

		/** Runs every action due at Now. Failed stops at the first action that could not run, with its error. */
		EOutcome Tick(FStepActionRunner& Runner, double Now, FString& OutError);

		/** 0-based index of the action that failed, INDEX_NONE before one has. */
		int32 FailedIndex = INDEX_NONE;
		/** One per action reached, in order. */
		TArray<FRecord> Records;

	private:
		TArray<FStepAction> Actions;
		int32 Next = 0;
		double WaitUntil = 0.0;
	};
}
