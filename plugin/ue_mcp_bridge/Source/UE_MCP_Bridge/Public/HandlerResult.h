#pragma once

// The response envelope every handler returns: success and error objects, and
// the created/existed/updated, rollback and idempotency fields a result carries.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ── Quick result builders ────────────────────────────────────────────────────

/** A fresh { success: false, error: "..." } object, for an error that carries
 *  more fields than the message. */
inline TSharedPtr<FJsonObject> MCPErrorObject(const FString& Message)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("error"), Message);
	return Obj;
}

/** Return an error response: { success: false, error: "..." } */
inline TSharedPtr<FJsonValue> MCPError(const FString& Message)
{
	return MakeShared<FJsonValueObject>(MCPErrorObject(Message));
}

/** Return an error response with a machine-readable code:
 *  { success: false, errorCode: "...", error: "..." } */
inline TSharedPtr<FJsonValue> MCPErrorWithCode(const FString& Code, const FString& Message)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), false);
	Obj->SetStringField(TEXT("errorCode"), Code);
	Obj->SetStringField(TEXT("error"), Message);
	return MakeShared<FJsonValueObject>(Obj);
}

/** An action compiled out on this engine: errorCode unsupported_engine_version,
 *  error "<Subject> requires Unreal Engine <MinVersion> or newer", then Detail
 *  as a second sentence when given. */
inline TSharedPtr<FJsonValue> MCPUnsupportedEngineError(
	const FString& Subject,
	const TCHAR* MinVersion,
	const FString& Detail = FString())
{
	FString Message = FString::Printf(TEXT("%s requires Unreal Engine %s or newer"), *Subject, MinVersion);
	if (!Detail.IsEmpty()) Message += TEXT(". ") + Detail;
	return MCPErrorWithCode(TEXT("unsupported_engine_version"), Message);
}

/** Return a formatted error. Usage: MCPError(FString::Printf(TEXT("Not found: %s"), *Path)) */
// NOTE: Do not use a variadic template wrapper - UE 5.7's consteval format
// string validation requires TEXT() literals passed directly to FString::Printf.

/** Wrap a populated FJsonObject as a FJsonValue (the common return). */
inline TSharedPtr<FJsonValue> MCPResult(TSharedPtr<FJsonObject> Obj)
{
	return MakeShared<FJsonValueObject>(Obj);
}

/** Create a fresh result object with success=true pre-set. */
inline TSharedPtr<FJsonObject> MCPSuccess()
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("success"), true);
	return Obj;
}

/** Attach a rollback record to a result. The TS bridge lifts this onto
 *  TaskResult.rollback so FlowRunner can invoke it on failure. */
inline void MCPSetRollback(
	TSharedPtr<FJsonObject> Result,
	const FString& InverseMethod,
	TSharedPtr<FJsonObject> Payload)
{
	TSharedPtr<FJsonObject> Rollback = MakeShared<FJsonObject>();
	Rollback->SetStringField(TEXT("method"), InverseMethod);
	Rollback->SetObjectField(TEXT("payload"), Payload);
	Result->SetObjectField(TEXT("rollback"), Rollback);
}

/** State that this mutation has no inverse, and say why.
 *
 *  The counterpart to MCPSetRollback, and the difference between a mutation
 *  that FORGOT its inverse and one that DECIDED it has none. Both emit no
 *  rollback record, so from the outside they are indistinguishable unless the
 *  second one says so in the result body.
 *
 *  The reason is a required argument rather than an optional one. A bare
 *  `rollbackPossible: false` tells a caller that recovery is off the table
 *  without telling it why, which is the half of the answer that decides what
 *  the caller does next; and a handler that sets the flag and forgets the note
 *  still reads as considered to anything auditing the pair. Taking both in one
 *  call is what makes them inseparable.
 *
 *  Keep the note concrete: what was changed, and what call would have to exist
 *  for it to be undone. See docs/handler-conventions.md. */
inline void MCPSetNoRollback(TSharedPtr<FJsonObject> Result, const FString& Reason)
{
	Result->SetBoolField(TEXT("rollbackPossible"), false);
	Result->SetStringField(TEXT("rollbackNote"), Reason);
}

/** Mark a result as "already existed, nothing created" - idempotent replay. */
inline void MCPSetExisted(TSharedPtr<FJsonObject> Result)
{
	Result->SetBoolField(TEXT("existed"), true);
	Result->SetBoolField(TEXT("created"), false);
}

/** Mark a result as "created this time". */
inline void MCPSetCreated(TSharedPtr<FJsonObject> Result)
{
	Result->SetBoolField(TEXT("existed"), false);
	Result->SetBoolField(TEXT("created"), true);
}

/** Mark a result as "updated the existing entity". */
inline void MCPSetUpdated(TSharedPtr<FJsonObject> Result)
{
	Result->SetBoolField(TEXT("updated"), true);
}

/** State that whether this call changed anything CANNOT BE READ, and say why.
 *
 *  The idempotency counterpart to MCPSetNoRollback, and it exists for the same
 *  reason: a handler that cannot answer and says so has finished the job, and
 *  one that says nothing has not, but from the outside the two look identical.
 *
 *  The cases are real and narrow. epic(call_tool) dispatches a wrapped engine
 *  tool whose writes it never sees, and the Fab module publishes no
 *  authentication or library state this build can read back. Both are asking
 *  about somebody else's code, so a fabricated `unchanged: false` would be a
 *  claim rather than a reading, which is worse than an honest absence.
 *
 *  This is NOT the escape hatch for a handler that could measure its effect and
 *  did not. If the state is readable, read it before and after and report what
 *  moved. The reason is a required argument for the same purpose it is on
 *  MCPSetNoRollback: the flag alone tells a caller that replay safety is
 *  unknown without telling it why, and the pair cannot come apart if one call
 *  sets both.
 *
 *  The spelling was already in use in FabHandlers before this helper existed,
 *  which is exactly how the earlier rollbackPossible drift started: an idiom
 *  spread by copy while the convention audit recognised none of it. */
inline void MCPSetIdempotencyUnobservable(TSharedPtr<FJsonObject> Result, const FString& Reason)
{
	Result->SetBoolField(TEXT("idempotencyObservable"), false);
	Result->SetStringField(TEXT("idempotencyNote"), Reason);
}

/** Emit the standard delete_asset rollback record on a create result. */
inline void MCPSetDeleteAssetRollback(TSharedPtr<FJsonObject> Result, const FString& AssetPath)
{
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	MCPSetRollback(Result, TEXT("delete_asset"), Payload);
}
