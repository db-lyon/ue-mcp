// Input-injection handlers (inject_input / inject_input_start / _update / _stop /
// _tape). Members of FGameplayHandlers; registration is in GameplayHandlers.cpp.
//
// All handlers delegate to PIE/PIEInputInjector.cpp which routes through
// UEnhancedInputLocalPlayerSubsystem::InjectInputForAction.

#include "GameplayHandlers.h"
#include "HandlerUtils.h"
#include "PIE/PIEInputInjector.h"
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

	FString ValueTypeName(EInputActionValueType T)
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

TSharedPtr<FJsonValue> FGameplayHandlers::InjectInput(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString ActionPath;
	if (auto E = RequireStringAlt(Params, TEXT("action"), TEXT("action_path"), ActionPath)) return E;
	TSharedPtr<FJsonValue> LoadErr;
	UInputAction* Action = LoadInputAction(ActionPath, LoadErr);
	if (!Action) return LoadErr;

	const FInputActionValue Value = BuildValueForAction(Action, Params);
	const int32 ClientIndex = FMath::Max(0, OptionalInt(Params, TEXT("client_id"), 0));
	FString Err;
	if (!UEMCPPIE::FPIEInputInjector::InjectOnce(Action, Value, Err, ClientIndex))
	{
		return MCPError(Err);
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("action"), ActionPath);
	Result->SetStringField(TEXT("value_type"), ValueTypeName(Action->ValueType));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::InjectInputStart(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString ActionPath;
	if (auto E = RequireStringAlt(Params, TEXT("action"), TEXT("action_path"), ActionPath)) return E;
	TSharedPtr<FJsonValue> LoadErr;
	UInputAction* Action = LoadInputAction(ActionPath, LoadErr);
	if (!Action) return LoadErr;

	const FInputActionValue Value = BuildValueForAction(Action, Params);
	const FString DesiredId = OptionalString(Params, TEXT("injection_id"));
	const int32 ClientIndex = FMath::Max(0, OptionalInt(Params, TEXT("client_id"), 0));

	FString Err;
	const FString Id = UEMCPPIE::FPIEInputInjector::StartHold(Action, Value, DesiredId, Err, ClientIndex);
	if (Id.IsEmpty()) return MCPError(Err);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("injection_id"), Id);
	Result->SetStringField(TEXT("action"), ActionPath);
	Result->SetStringField(TEXT("value_type"), ValueTypeName(Action->ValueType));

	// Inverse: stop the hold by id we just minted.
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("injection_id"), Id);
	MCPSetRollback(Result, TEXT("inject_input_stop"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::InjectInputUpdate(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Id;
	if (auto E = RequireString(Params, TEXT("injection_id"), Id)) return E;

	// We need an action reference to interpret value_x/y/z by type. Look up
	// the active injection in the current snapshot and resolve the action.
	UInputAction* Action = nullptr;
	for (const UEMCPPIE::FInjectionStatus& S : UEMCPPIE::FPIEInputInjector::List())
	{
		if (S.Id == Id)
		{
			Action = LoadAssetByPath<UInputAction>(S.ActionPath);
			break;
		}
	}
	if (!Action) return MCPError(FString::Printf(TEXT("No active injection with id '%s'"), *Id));

	const FInputActionValue Value = BuildValueForAction(Action, Params);
	if (!UEMCPPIE::FPIEInputInjector::UpdateHold(Id, Value))
	{
		return MCPError(FString::Printf(TEXT("Injection '%s' is not a hold (try inject_input_stop)"), *Id));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("injection_id"), Id);
	MCPSetUpdated(Result);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::InjectInputStop(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Id;
	if (auto E = RequireString(Params, TEXT("injection_id"), Id)) return E;
	const bool Stopped = UEMCPPIE::FPIEInputInjector::StopAny(Id);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("injection_id"), Id);
	Result->SetBoolField(TEXT("stopped"), Stopped);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FGameplayHandlers::InjectInputTape(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString ActionPath;
	if (auto E = RequireStringAlt(Params, TEXT("action"), TEXT("action_path"), ActionPath)) return E;
	TSharedPtr<FJsonValue> LoadErr;
	UInputAction* Action = LoadInputAction(ActionPath, LoadErr);
	if (!Action) return LoadErr;

	const TArray<TSharedPtr<FJsonValue>>* ValuesArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("values"), ValuesArr) || !ValuesArr || ValuesArr->Num() == 0)
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

	FString Err;
	const FString Id = UEMCPPIE::FPIEInputInjector::StartTape(Action, Vals, Hz, DesiredId, Err, ClientIndex);
	if (Id.IsEmpty()) return MCPError(Err);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("injection_id"), Id);
	Result->SetStringField(TEXT("action"), ActionPath);
	Result->SetNumberField(TEXT("frame_count"), Vals.Num());
	Result->SetNumberField(TEXT("hz"), Hz);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("injection_id"), Id);
	MCPSetRollback(Result, TEXT("inject_input_stop"), Payload);

	return MCPResult(Result);
}
