// Input-injection handlers (inject_input / inject_input_start / _update / _stop /
// _tape). Members of FPIEHandlers; registration is in GameplayHandlers.cpp.
//
// All handlers delegate to PIE/PIEInputInjector.cpp, which routes one-shot
// values through InjectInputForAction and stateful values through the
// subsystem's continuous injection lifecycle.

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerUtils.h"
#include "MCPPIEInputInjector.h"
#include "Handlers/PIE/PIERemotePlayers.h"
#include "MCPPIERemoteControl.h"
#include "InputAction.h"
#include "InputActionValue.h"
#include "Math/Vector2D.h"
#include "Math/Vector.h"

namespace
{
	UInputAction* LoadInputAction(const FString& Path, TSharedPtr<FJsonValue>& OutError)
	{
		UInputAction* Action = LoadAssetByPath<UInputAction>(Path);
		if (!Action)
		{
			OutError = MCPError(FString::Printf(TEXT("InputAction not found: %s"), *Path));
			return nullptr;
		}
		return Action;
	}

	FInputActionValue BuildValueForAction(UInputAction* Action, const TSharedPtr<FJsonObject>& Params)
	{
		const double X = OptionalNumber(Params, TEXT("value_x"), 1.0);
		const double Y = OptionalNumber(Params, TEXT("value_y"), 0.0);
		const double Z = OptionalNumber(Params, TEXT("value_z"), 0.0);
		switch (Action->ValueType)
		{
		case EInputActionValueType::Boolean: return FInputActionValue(X != 0.0);
		case EInputActionValueType::Axis1D:  return FInputActionValue(static_cast<float>(X));
		case EInputActionValueType::Axis2D:  return FInputActionValue(FVector2D(X, Y));
		case EInputActionValueType::Axis3D:  return FInputActionValue(FVector(X, Y, Z));
		}
		return FInputActionValue();
	}

	FVector RawValue(const TSharedPtr<FJsonObject>& Params)
	{
		return FVector(OptionalNumber(Params, TEXT("value_x"), 1.0), OptionalNumber(Params, TEXT("value_y"), 0.0),
			OptionalNumber(Params, TEXT("value_z"), 0.0));
	}

	// `client` 1..N relays to that remote player through the game connection; absent or 0 keeps the local path
	// (the editor's own player, or `pie_instance`/`client_id` in one-process PIE).
	UMCPPIERemoteControl* ResolveRemoteClient(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonValue>& OutError)
	{
		const int32 Client = OptionalInt(Params, TEXT("client"), 0);
		if (Client <= 0)
		{
			return nullptr;
		}
		FString Err;
		UMCPPIERemoteControl* Control = MCPPIE::PIERemotePlayers::Find(Client, Err);
		if (!Control)
		{
			OutError = MCPError(Err);
		}
		return Control;
	}

	TSharedPtr<FJsonObject> RelayedResult(const TSharedPtr<FJsonObject>& Params, const FString& Id, const FString& ActionPath)
	{
		auto Result = MCPSuccess();
		Result->SetStringField(TEXT("injection_id"), Id);
		Result->SetStringField(TEXT("action"), ActionPath);
		Result->SetNumberField(TEXT("client"), OptionalInt(Params, TEXT("client"), 0));
		Result->SetBoolField(TEXT("relayed"), true);
		Result->SetStringField(TEXT("note"), TEXT("Sent to the remote player; remote_result reports whether it ran."));
		MCPSetIdempotencyUnobservable(Result, TEXT("The remote player applies the request in its own process; remote_result reports what it did"));
		return Result;
	}

	// A hold or tape is undone by stopping it by id.
	void SetStopRollback(TSharedPtr<FJsonObject> Result, const FString& Id)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("injection_id"), Id);
		MCPSetRollback(Result, TEXT("pie_inject_input_stop"), Payload);
	}

	// duration_seconds and control_rotation of a hold (#1449). False with OutError on a bad duration.
	bool ReadHoldOptions(const TSharedPtr<FJsonObject>& Params, MCPPIE::FHoldOptions& Out, bool& bOutAny, FString& OutError)
	{
		bOutAny = false;
		if (HasParam(Params, TEXT("duration_seconds")))
		{
			bOutAny = true;
			Out.DurationSeconds = OptionalNumber(Params, TEXT("duration_seconds"), 0.0);
			if (!FMath::IsFinite(Out.DurationSeconds) || Out.DurationSeconds < 0.0)
			{
				OutError = TEXT("duration_seconds must be a finite number of game seconds, 0 or more (0 holds until inject_input_stop)");
				return false;
			}
		}
		if (HasParam(Params, TEXT("control_rotation")))
		{
			bOutAny = true;
			Out.bHasControlRotation = true;
			Out.ControlRotation = OptionalRotator(Params, TEXT("control_rotation"));
		}
		return true;
	}

	void WriteHoldOptions(const TSharedPtr<FJsonObject>& Result, const MCPPIE::FHoldOptions& Options)
	{
		if (Options.DurationSeconds > 0)
		{
			Result->SetNumberField(TEXT("duration_seconds"), Options.DurationSeconds);
		}
		if (Options.bHasControlRotation)
		{
			Result->SetObjectField(TEXT("control_rotation"), MCPRotatorToJsonObject(Options.ControlRotation));
		}
	}

	FString InjectValueTypeName(EInputActionValueType T)
	{
		switch (T)
		{
		case EInputActionValueType::Boolean: return TEXT("Boolean");
		case EInputActionValueType::Axis1D:  return TEXT("Axis1D");
		case EInputActionValueType::Axis2D:  return TEXT("Axis2D");
		case EInputActionValueType::Axis3D:  return TEXT("Axis3D");
		}
		return TEXT("Boolean");
	}
}

bool FPIEHandlers::ParsePIEInstance(const TSharedPtr<FJsonObject>& Params,
	int32& OutPIEInstance, FString& OutError)
{
	OutPIEInstance = INDEX_NONE;
	OutError.Reset();
	if (!HasParam(Params, TEXT("pie_instance")))
	{
		return true;
	}

	double Value = 0.0;
	const bool bValid = TryGetNumberParam(Params, TEXT("pie_instance"), Value)
		&& FMath::IsFinite(Value)
		&& Value >= 0.0
		&& Value <= static_cast<double>(MAX_int32)
		&& Value == static_cast<double>(static_cast<int64>(Value));
	if (!bValid)
	{
		OutError = TEXT("pie_instance must be a finite non-negative integer in int32 range when supplied");
		return false;
	}

	OutPIEInstance = static_cast<int32>(Value);
	return true;
}

TSharedPtr<FJsonValue> FPIEHandlers::InjectInput(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	int32 PIEInstance = INDEX_NONE;
	FString PIEInstanceError;
	if (!FPIEHandlers::ParsePIEInstance(Params, PIEInstance, PIEInstanceError))
	{
		return MCPError(PIEInstanceError);
	}
	FString ActionPath;
	if (auto E = RequireString(Params, TEXT("action_path"), ActionPath)) return E;
	TSharedPtr<FJsonValue> LoadErr;
	UInputAction* Action = LoadInputAction(ActionPath, LoadErr);
	if (!Action) return LoadErr;

	TSharedPtr<FJsonValue> RemoteErr;
	if (UMCPPIERemoteControl* Remote = ResolveRemoteClient(Params, RemoteErr))
	{
		const FString Id = MCPPIE::PIERemotePlayers::NewId(TEXT("inject"));
		Remote->SendInject(Id, FSoftObjectPath(Action), RawValue(Params));
		MCPPIE::PIERemotePlayers::RememberId(Id, Remote);
		auto Relayed = RelayedResult(Params, Id, ActionPath);
		MCPSetNoRollback(Relayed, TEXT("One frame of input already delivered to the game cannot be taken back"));
		return MCPResult(Relayed);
	}
	if (RemoteErr) return RemoteErr;

	const FInputActionValue Value = BuildValueForAction(Action, Params);
	const int32 ClientIndex = FMath::Max(0, OptionalInt(Params, TEXT("client_id"), 0));

	FString Err;
	if (!MCPPIE::FPIEInputInjector::InjectOnce(Action, Value, Err, ClientIndex, PIEInstance))
	{
		return MCPError(Err);
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("action"), ActionPath);
	Result->SetStringField(TEXT("value_type"), InjectValueTypeName(Action->ValueType));
	MCPSetIdempotencyUnobservable(Result, TEXT("Each call injects one more frame of input"));
	MCPSetNoRollback(Result, TEXT("One frame of input already delivered to the game cannot be taken back"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::InjectInputStart(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	int32 PIEInstance = INDEX_NONE;
	FString PIEInstanceError;
	if (!FPIEHandlers::ParsePIEInstance(Params, PIEInstance, PIEInstanceError))
	{
		return MCPError(PIEInstanceError);
	}
	FString ActionPath;
	if (auto E = RequireString(Params, TEXT("action_path"), ActionPath)) return E;
	TSharedPtr<FJsonValue> LoadErr;
	UInputAction* Action = LoadInputAction(ActionPath, LoadErr);
	if (!Action) return LoadErr;

	const FInputActionValue Value = BuildValueForAction(Action, Params);
	const FString DesiredId = OptionalString(Params, TEXT("injection_id"));
	const int32 ClientIndex = FMath::Max(0, OptionalInt(Params, TEXT("client_id"), 0));
	MCPPIE::FHoldOptions Options;
	bool bHasOptions = false;
	FString OptionsError;
	if (!ReadHoldOptions(Params, Options, bHasOptions, OptionsError)) return MCPError(OptionsError);

	TSharedPtr<FJsonValue> RemoteErr;
	if (UMCPPIERemoteControl* Remote = ResolveRemoteClient(Params, RemoteErr))
	{
		if (bHasOptions)
		{
			return MCPError(TEXT("duration_seconds and control_rotation apply to a local player; a hold relayed to a remote player (client 1..N) runs until inject_input_stop"));
		}
		const FString Id = DesiredId.IsEmpty() ? MCPPIE::PIERemotePlayers::NewId(TEXT("hold")) : DesiredId;
		Remote->SendStartHold(Id, FSoftObjectPath(Action), RawValue(Params));
		MCPPIE::PIERemotePlayers::RememberId(Id, Remote);
		auto Relayed = RelayedResult(Params, Id, ActionPath);
		SetStopRollback(Relayed, Id);
		return MCPResult(Relayed);
	}
	if (RemoteErr) return RemoteErr;


	FString Err;
	const FString Id = MCPPIE::FPIEInputInjector::StartHold(
		Action, Value, DesiredId, Err, ClientIndex, PIEInstance, nullptr, Options);
	if (Id.IsEmpty()) return MCPError(Err);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("injection_id"), Id);
	Result->SetStringField(TEXT("action"), ActionPath);
	Result->SetStringField(TEXT("value_type"), InjectValueTypeName(Action->ValueType));
	WriteHoldOptions(Result, Options);

	MCPSetCreated(Result);
	SetStopRollback(Result, Id);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::InjectInputUpdate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Id;
	if (auto E = RequireString(Params, TEXT("injection_id"), Id)) return E;
	const bool bHasValue = HasParam(Params, TEXT("value_x")) || HasParam(Params, TEXT("value_y")) || HasParam(Params, TEXT("value_z"));
	const bool bHasRotation = HasParam(Params, TEXT("control_rotation"));
	const FRotator ControlRotation = OptionalRotator(Params, TEXT("control_rotation"));

	if (UMCPPIERemoteControl* Remote = MCPPIE::PIERemotePlayers::FindById(Id))
	{
		if (bHasRotation)
		{
			return MCPError(TEXT("control_rotation applies to a local player's hold; this hold was relayed to a remote player"));
		}
		Remote->SendUpdateHold(Id, RawValue(Params));
		auto Relayed = MCPSuccess();
		Relayed->SetStringField(TEXT("injection_id"), Id);
		Relayed->SetBoolField(TEXT("relayed"), true);
		MCPSetUpdated(Relayed);
		MCPSetNoRollback(Relayed, TEXT("The previous hold value is not kept, so it cannot be restored"));
		return MCPResult(Relayed);
	}

	// We need an action reference to interpret value_x/y/z by type. Look up
	// the active injection in the current snapshot and resolve the action.
	UInputAction* Action = nullptr;
	for (const MCPPIE::FInjectionStatus& S : MCPPIE::FPIEInputInjector::List())
	{
		if (S.Id == Id)
		{
			Action = LoadAssetByPath<UInputAction>(S.ActionPath);
			break;
		}
	}
	if (!Action) return MCPError(FString::Printf(TEXT("No active injection with id '%s'"), *Id));

	// control_rotation alone re-aims the hold and keeps its value; any value_* (or neither) sets the value.
	const bool bUpdateValue = bHasValue || !bHasRotation;
	if (bUpdateValue)
	{
		const FInputActionValue Value = BuildValueForAction(Action, Params);
		if (!MCPPIE::FPIEInputInjector::UpdateHold(Id, Value))
		{
			return MCPError(FString::Printf(TEXT("Injection '%s' is not a hold (try inject_input_stop)"), *Id));
		}
	}
	if (bHasRotation && !MCPPIE::FPIEInputInjector::SetHoldControlRotation(Id, ControlRotation))
	{
		return MCPError(FString::Printf(TEXT("Injection '%s' is not a hold (try inject_input_stop)"), *Id));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("injection_id"), Id);
	Result->SetBoolField(TEXT("valueUpdated"), bUpdateValue);
	if (bHasRotation)
	{
		Result->SetObjectField(TEXT("control_rotation"), MCPRotatorToJsonObject(ControlRotation));
	}
	MCPSetUpdated(Result);
	MCPSetNoRollback(Result, TEXT("The previous hold value is not kept, so it cannot be restored"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::InjectInputStop(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Id;
	if (auto E = RequireString(Params, TEXT("injection_id"), Id)) return E;
	if (UMCPPIERemoteControl* Remote = MCPPIE::PIERemotePlayers::FindById(Id))
	{
		Remote->SendStop(Id);
		auto Relayed = MCPSuccess();
		Relayed->SetStringField(TEXT("injection_id"), Id);
		Relayed->SetBoolField(TEXT("relayed"), true);
		MCPSetIdempotencyUnobservable(Relayed, TEXT("The remote player stops the request in its own process"));
		MCPSetNoRollback(Relayed, TEXT("A stopped hold or tape is not resumed; start a new one"));
		return MCPResult(Relayed);
	}
	const bool Stopped = MCPPIE::FPIEInputInjector::StopAny(Id);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("injection_id"), Id);
	Result->SetBoolField(TEXT("stopped"), Stopped);
	Result->SetBoolField(TEXT("alreadyStopped"), !Stopped);
	MCPSetNoRollback(Result, TEXT("A stopped hold or tape is not resumed; start a new one"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::InjectInputTape(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	int32 PIEInstance = INDEX_NONE;
	FString PIEInstanceError;
	if (!FPIEHandlers::ParsePIEInstance(Params, PIEInstance, PIEInstanceError))
	{
		return MCPError(PIEInstanceError);
	}
	FString ActionPath;
	if (auto E = RequireString(Params, TEXT("action_path"), ActionPath)) return E;
	TSharedPtr<FJsonValue> LoadErr;
	UInputAction* Action = LoadInputAction(ActionPath, LoadErr);
	if (!Action) return LoadErr;

	const TArray<TSharedPtr<FJsonValue>>* ValuesArr = nullptr;
	if (!TryGetArrayParam(Params, TEXT("values"), ValuesArr) || !ValuesArr || ValuesArr->Num() == 0)
	{
		return MCPError(TEXT("inject_input_tape: 'values' array is required and non-empty"));
	}

	TArray<FVector> Vals;
	Vals.Reserve(ValuesArr->Num());
	for (const TSharedPtr<FJsonValue>& V : *ValuesArr)
	{
		FVector Vec(0, 0, 0);
		if (V->Type == EJson::Number)
		{
			Vec.X = V->AsNumber();
		}
		else if (V->Type == EJson::Array)
		{
			const TArray<TSharedPtr<FJsonValue>>& Inner = V->AsArray();
			if (Inner.Num() >= 1) Vec.X = Inner[0]->AsNumber();
			if (Inner.Num() >= 2) Vec.Y = Inner[1]->AsNumber();
			if (Inner.Num() >= 3) Vec.Z = Inner[2]->AsNumber();
		}
		Vals.Add(Vec);
	}

	const int32 Hz = OptionalInt(Params, TEXT("hz"), 60);
	const FString DesiredId = OptionalString(Params, TEXT("injection_id"));
	const int32 ClientIndex = FMath::Max(0, OptionalInt(Params, TEXT("client_id"), 0));

	TSharedPtr<FJsonValue> RemoteErr;
	if (UMCPPIERemoteControl* Remote = ResolveRemoteClient(Params, RemoteErr))
	{
		if (Vals.Num() > UMCPPIERemoteControl::MaxTapeFrames)
		{
			return MCPError(FString::Printf(TEXT("inject_input_tape: a relayed tape carries at most %d frames; this one has %d"),
				UMCPPIERemoteControl::MaxTapeFrames, Vals.Num()));
		}
		const FString Id = DesiredId.IsEmpty() ? MCPPIE::PIERemotePlayers::NewId(TEXT("tape")) : DesiredId;
		Remote->SendTape(Id, FSoftObjectPath(Action), Vals);
		MCPPIE::PIERemotePlayers::RememberId(Id, Remote);
		auto Relayed = RelayedResult(Params, Id, ActionPath);
		Relayed->SetNumberField(TEXT("frame_count"), Vals.Num());
		SetStopRollback(Relayed, Id);
		return MCPResult(Relayed);
	}
	if (RemoteErr) return RemoteErr;


	FString Err;
	const FString Id = MCPPIE::FPIEInputInjector::StartTape(
		Action, Vals, Hz, DesiredId, Err, ClientIndex, PIEInstance);
	if (Id.IsEmpty()) return MCPError(Err);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("injection_id"), Id);
	Result->SetStringField(TEXT("action"), ActionPath);
	Result->SetNumberField(TEXT("frame_count"), Vals.Num());
	Result->SetNumberField(TEXT("hz"), Hz);
	MCPSetCreated(Result);
	SetStopRollback(Result, Id);

	return MCPResult(Result);
}
