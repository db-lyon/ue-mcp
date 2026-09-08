#include "EpicHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "UE_MCP_BridgeModule.h"

#include "UObject/Object.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

// ── Reflection bridge to /Script/ToolsetRegistry.ToolsetRegistry ──────────────
//
// UToolsetRegistry is a UBlueprintFunctionLibrary exposing static UFUNCTIONs:
//   bool                        IsAvailable()
//   FString                     GetAllToolsetJsonSchemas()
//   UToolCallAsyncResultString* ExecuteTool(FString, FString, FString)
// We invoke them via ProcessEvent on the CDO with a property-driven parameter
// buffer, so we never assume a hand-mirrored struct layout.
namespace UEMCPEpic
{
	static UClass* RegistryClass()
	{
		// Present only when the ToolsetRegistry plugin module is loaded (UE 5.8+
		// with the plugin enabled). Null otherwise -> callers report unavailable.
		return FindObject<UClass>(nullptr, TEXT("/Script/ToolsetRegistry.ToolsetRegistry"));
	}

	// Invoke a static UFUNCTION by name with string inputs. ReadOut runs after
	// ProcessEvent to copy return/out values out of the parm buffer before it is
	// destroyed. Returns false if the class or function is unavailable.
	static bool InvokeStatic(
		const TCHAR* FuncName,
		const TMap<FString, FString>& StringArgs,
		TFunctionRef<void(UFunction*, uint8*)> ReadOut)
	{
		UClass* Cls = RegistryClass();
		if (!Cls) return false;
		UFunction* Func = Cls->FindFunctionByName(FName(FuncName));
		if (!Func) return false;
		UObject* CDO = Cls->GetDefaultObject();
		if (!CDO) return false;

		// Zeroed memory is a valid empty state for FString/bool/object-ptr parms,
		// so Memzero is sufficient initialization for these signatures.
		uint8* Buffer = (uint8*)FMemory_Alloca(FMath::Max<int32>(Func->ParmsSize, 1));
		FMemory::Memzero(Buffer, Func->ParmsSize);

		for (const TPair<FString, FString>& Arg : StringArgs)
		{
			if (FStrProperty* SP = CastField<FStrProperty>(Func->FindPropertyByName(FName(*Arg.Key))))
			{
				SP->SetPropertyValue_InContainer(Buffer, Arg.Value);
			}
		}

		CDO->ProcessEvent(Func, Buffer);
		ReadOut(Func, Buffer);

		// Free heap allocations owned by the parms (notably the FString return).
		for (TFieldIterator<FProperty> It(Func); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Parm))
			{
				It->DestroyValue_InContainer(Buffer);
			}
		}
		return true;
	}

	static bool IsAvailable()
	{
		bool bResult = false;
		InvokeStatic(TEXT("IsAvailable"), {}, [&](UFunction* Func, uint8* Buffer)
		{
			for (TFieldIterator<FProperty> It(Func); It; ++It)
			{
				if (It->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					if (FBoolProperty* BP = CastField<FBoolProperty>(*It))
					{
						bResult = BP->GetPropertyValue_InContainer(Buffer);
					}
				}
			}
		});
		return bResult;
	}

	static bool GetAllSchemas(FString& Out)
	{
		FString Result;
		const bool bCalled = InvokeStatic(TEXT("GetAllToolsetJsonSchemas"), {}, [&](UFunction* Func, uint8* Buffer)
		{
			for (TFieldIterator<FProperty> It(Func); It; ++It)
			{
				if (It->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					if (FStrProperty* SP = CastField<FStrProperty>(*It))
					{
						Result = SP->GetPropertyValue_InContainer(Buffer);
					}
				}
			}
		});
		if (!bCalled) return false;
		Out = MoveTemp(Result);
		return true;
	}

	// Returns the UToolCallAsyncResult* (as UObject*) or nullptr if unavailable.
	static UObject* ExecuteTool(const FString& Toolset, const FString& Tool, const FString& Input)
	{
		UObject* Ret = nullptr;
		TMap<FString, FString> Args;
		Args.Add(TEXT("ToolsetName"), Toolset);
		Args.Add(TEXT("ToolName"), Tool);
		Args.Add(TEXT("JsonInput"), Input);
		InvokeStatic(TEXT("ExecuteTool"), Args, [&](UFunction* Func, uint8* Buffer)
		{
			for (TFieldIterator<FProperty> It(Func); It; ++It)
			{
				if (It->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					if (FObjectPropertyBase* OP = CastField<FObjectPropertyBase>(*It))
					{
						Ret = OP->GetObjectPropertyValue_InContainer(Buffer);
					}
				}
			}
		});
		return Ret;
	}

	static bool ParseJsonArray(const FString& Raw, TArray<TSharedPtr<FJsonValue>>& Out)
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		return FJsonSerializer::Deserialize(Reader, Out) && Out.Num() >= 0;
	}

	static const FString UnavailableMsg =
		TEXT("Epic ToolsetRegistry not available. Requires UE 5.8+ with the ToolsetRegistry plugin enabled in the project.");
}

// ── Handlers ──────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FEpicHandlers::Status(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Res = MCPSuccess();

	if (!UEMCPEpic::RegistryClass())
	{
		Res->SetBoolField(TEXT("available"), false);
		Res->SetStringField(TEXT("reason"),
			TEXT("ToolsetRegistry plugin not loaded (requires UE 5.8+ with the plugin enabled)"));
		Res->SetNumberField(TEXT("toolsetCount"), 0);
		return MCPResult(Res);
	}

	const bool bAvail = UEMCPEpic::IsAvailable();
	Res->SetBoolField(TEXT("available"), bAvail);

	int32 Count = 0;
	FString Raw;
	if (bAvail && UEMCPEpic::GetAllSchemas(Raw))
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		if (UEMCPEpic::ParseJsonArray(Raw, Arr))
		{
			Count = Arr.Num();
		}
	}
	Res->SetNumberField(TEXT("toolsetCount"), Count);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FEpicHandlers::ListToolsets(const TSharedPtr<FJsonObject>& Params)
{
	FString Raw;
	if (!UEMCPEpic::GetAllSchemas(Raw))
	{
		return MCPError(UEMCPEpic::UnavailableMsg);
	}

	TArray<TSharedPtr<FJsonValue>> Arr;
	if (!UEMCPEpic::ParseJsonArray(Raw, Arr))
	{
		return MCPError(TEXT("Failed to parse toolset schemas returned by the registry"));
	}

	const FString Filter = OptionalString(Params, TEXT("nameFilter"), TEXT(""));
	// When true, return the full tool objects (name/description/input/output
	// schema) instead of just tool names. Used by the server to build
	// per-category first-class actions from the live catalog in a single call.
	const bool bIncludeSchemas = OptionalBool(Params, TEXT("includeSchemas"), false);

	TArray<TSharedPtr<FJsonValue>> OutList;
	for (const TSharedPtr<FJsonValue>& V : Arr)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj) continue;

		FString Name;
		(*Obj)->TryGetStringField(TEXT("name"), Name);
		if (!Filter.IsEmpty() && !Name.Contains(Filter)) continue;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Name);

		FString Ver;
		if ((*Obj)->TryGetStringField(TEXT("version"), Ver)) Entry->SetStringField(TEXT("version"), Ver);
		FString Desc;
		if ((*Obj)->TryGetStringField(TEXT("description"), Desc)) Entry->SetStringField(TEXT("description"), Desc);

		const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
		if ((*Obj)->TryGetArrayField(TEXT("tools"), Tools) && Tools)
		{
			Entry->SetNumberField(TEXT("toolCount"), Tools->Num());
			if (bIncludeSchemas)
			{
				// Pass the full tool objects straight through.
				Entry->SetArrayField(TEXT("tools"), *Tools);
			}
			else
			{
				TArray<TSharedPtr<FJsonValue>> ToolNames;
				for (const TSharedPtr<FJsonValue>& TV : *Tools)
				{
					const TSharedPtr<FJsonObject>* TO = nullptr;
					if (TV.IsValid() && TV->TryGetObject(TO) && TO)
					{
						FString TN;
						(*TO)->TryGetStringField(TEXT("name"), TN);
						ToolNames.Add(MakeShared<FJsonValueString>(TN));
					}
				}
				Entry->SetArrayField(TEXT("tools"), ToolNames);
			}
		}
		OutList.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetNumberField(TEXT("toolsetCount"), OutList.Num());
	Res->SetArrayField(TEXT("toolsets"), OutList);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FEpicHandlers::DescribeToolset(const TSharedPtr<FJsonObject>& Params)
{
	FString Want;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("toolset"), Want)) return Err;

	FString Raw;
	if (!UEMCPEpic::GetAllSchemas(Raw))
	{
		return MCPError(UEMCPEpic::UnavailableMsg);
	}

	TArray<TSharedPtr<FJsonValue>> Arr;
	if (!UEMCPEpic::ParseJsonArray(Raw, Arr))
	{
		return MCPError(TEXT("Failed to parse toolset schemas returned by the registry"));
	}

	for (const TSharedPtr<FJsonValue>& V : Arr)
	{
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!V.IsValid() || !V->TryGetObject(Obj) || !Obj) continue;
		FString Name;
		(*Obj)->TryGetStringField(TEXT("name"), Name);
		if (Name == Want)
		{
			TSharedPtr<FJsonObject> Res = MCPSuccess();
			Res->SetObjectField(TEXT("toolset"), *Obj);
			return MCPResult(Res);
		}
	}

	return MCPError(FString::Printf(
		TEXT("Toolset '%s' not found. Use epic(list_toolsets) for available toolset names."), *Want));
}

TSharedPtr<FJsonValue> FEpicHandlers::CallTool(const TSharedPtr<FJsonObject>& Params)
{
	FString Toolset;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("toolset"), Toolset)) return Err;
	FString Tool;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("tool"), Tool)) return Err;

	if (!UEMCPEpic::RegistryClass())
	{
		return MCPError(UEMCPEpic::UnavailableMsg);
	}

	// Build the JSON input: prefer an `input` object, fall back to `inputJson`.
	FString Input = OptionalString(Params, TEXT("inputJson"), TEXT(""));
	const TSharedPtr<FJsonObject>* InObj = nullptr;
	if (Input.IsEmpty() && Params->TryGetObjectField(TEXT("input"), InObj) && InObj && (*InObj).IsValid())
	{
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Input);
		FJsonSerializer::Serialize((*InObj).ToSharedRef(), Writer);
	}
	if (Input.IsEmpty()) Input = TEXT("{}");

	// ExecuteTool expects the bare tool name, but list/describe report the
	// fully-qualified "Toolset.Tool" form. Accept either: strip the toolset
	// prefix so a caller can paste the qualified name straight from describe.
	FString ToolName = Tool;
	const FString Prefix = Toolset + TEXT(".");
	if (ToolName.StartsWith(Prefix))
	{
		ToolName = ToolName.RightChop(Prefix.Len());
	}

	UObject* Result = UEMCPEpic::ExecuteTool(Toolset, ToolName, Input);
	if (!Result)
	{
		return MCPError(FString::Printf(
			TEXT("ExecuteTool returned no result for %s / %s (tool not found or registry unavailable)"),
			*Toolset, *Tool));
	}

	UClass* RC = Result->GetClass();
	bool bComplete = false;
	if (FBoolProperty* P = CastField<FBoolProperty>(RC->FindPropertyByName(TEXT("bIsComplete"))))
	{
		bComplete = P->GetPropertyValue_InContainer(Result);
	}
	FString ToolError;
	if (FStrProperty* P = CastField<FStrProperty>(RC->FindPropertyByName(TEXT("Error"))))
	{
		ToolError = P->GetPropertyValue_InContainer(Result);
	}
	if (!ToolError.IsEmpty())
	{
		return MCPError(FString::Printf(TEXT("Tool '%s' failed: %s"), *Tool, *ToolError));
	}
	if (!bComplete)
	{
		return MCPError(FString::Printf(
			TEXT("Tool '%s' returned an asynchronous result that did not complete synchronously; "
				 "async tool results are not yet supported by the bridge wrapper."), *Tool));
	}

	FString Value;
	if (FStrProperty* P = CastField<FStrProperty>(RC->FindPropertyByName(TEXT("Value"))))
	{
		Value = P->GetPropertyValue_InContainer(Result);
	}

	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("toolset"), Toolset);
	Res->SetStringField(TEXT("tool"), Tool);

	if (!Value.IsEmpty())
	{
		TSharedPtr<FJsonValue> Parsed;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Value);
		if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
		{
			Res->SetField(TEXT("result"), Parsed);
		}
		else
		{
			Res->SetStringField(TEXT("resultText"), Value);
		}
	}
	else
	{
		Res->SetStringField(TEXT("resultText"), TEXT(""));
	}

	// No inverse, and the reason is whose code ran rather than a gap here. This
	// action is a dispatcher: the effect belongs to the wrapped engine tool the
	// caller named in 'toolset' and 'tool', and the toolset API hands back a
	// result payload rather than a description of what was written. Naming an
	// inverse would mean guessing at an effect this wrapper never saw.
	MCPSetNoRollback(Res, FString::Printf(
		TEXT("Dispatched the wrapped engine tool '%s.%s'. Whatever that tool changed is its own, and the toolset API "
		     "reports a result value rather than the writes behind it, so nothing here knows what to reverse. An "
		     "inverse would be a second epic(call_tool) naming whichever tool undoes it, which only the caller "
		     "choosing the tool can decide."),
		*Toolset, *ToolName));

	// Idempotency is unreadable for the same reason. Whether the tool found
	// work to do, or found its target already in the requested state, is
	// decided inside the tool and is not part of what it returns. A flag
	// invented here would be a claim about somebody else's code.
	Res->SetBoolField(TEXT("idempotencyObservable"), false);
	Res->SetStringField(TEXT("idempotencyNote"), FString::Printf(
		TEXT("'%s.%s' decides whether it changed anything, and the toolset API returns its result value without "
		     "saying so. Read 'result' to see what the tool reported; this wrapper adds no claim of its own."),
		*Toolset, *ToolName));
	return MCPResult(Res);
}

void FEpicHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("epic_status"), &Status);
	Registry.RegisterHandler(TEXT("epic_list_toolsets"), &ListToolsets);
	Registry.RegisterHandler(TEXT("epic_describe_toolset"), &DescribeToolset);
	Registry.RegisterHandler(TEXT("epic_call_tool"), &CallTool);
}
