#include "EpicHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerStaticInvoke.h"
#include "UE_MCP_BridgeModule.h"
#include "HandlerDeferred.h"

#include "Containers/Ticker.h"
#include "Misc/Guid.h"
#include "UObject/StrongObjectPtr.h"

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
		return MCPStaticInvoke::CallWithStrings(RegistryClass(), FuncName, StringArgs, ReadOut);
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

	bool GetAllSchemas(FString& Out)
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
			Ret = MCPStaticInvoke::ReturnedObject(Func, Buffer);
		});
		return Ret;
	}

	static bool ParseJsonArray(const FString& Raw, TArray<TSharedPtr<FJsonValue>>& Out)
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		return FJsonSerializer::Deserialize(Reader, Out) && Out.Num() >= 0;
	}

	// Tools whose whole effect lands on one object the input names, so a success that changed nothing is observable.
	static const TCHAR* VerifiedTargetField(const FString& ToolName)
	{
		return ToolName == TEXT("UpdateNode") ? TEXT("node") : nullptr;
	}

	/** The object Input[Field] names, as a path string or a {refPath} object; null when it does not resolve. */
	static UObject* ResolveInputTarget(const FString& Input, const TCHAR* Field)
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Input);
		if (!Field || !FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid()) return nullptr;
		FString Path;
		const TSharedPtr<FJsonObject>* Ref = nullptr;
		if (!Obj->TryGetStringField(Field, Path) && Obj->TryGetObjectField(Field, Ref) && Ref) (*Ref)->TryGetStringField(TEXT("refPath"), Path);
		if (Path.IsEmpty()) return nullptr;
		if (UObject* Found = StaticFindObject(UObject::StaticClass(), nullptr, *Path)) return Found;
		// "/Game/Dir/Graph:Node" names the asset by package only.
		int32 Colon = INDEX_NONE;
		if (Path.FindChar(TEXT(':'), Colon) && !Path.Left(Colon).Contains(TEXT(".")))
		{
			const FString Package = Path.Left(Colon);
			const FString Expanded = Package + TEXT(".") + FPackageName::GetShortName(Package) + Path.Mid(Colon);
			return StaticFindObject(UObject::StaticClass(), nullptr, *Expanded);
		}
		return nullptr;
	}

	/** Every property of Target, the objects it owns, and same-package objects it points at, as export text. */
	static FString Fingerprint(UObject* Target)
	{
		TArray<UObject*> Objects;
		Objects.Add(Target);
		for (TFieldIterator<FObjectPropertyBase> It(Target->GetClass()); It; ++It)
		{
			UObject* Ref = It->GetObjectPropertyValue_InContainer(Target);
			if (Ref && Ref->GetOutermost() == Target->GetOutermost() && !Ref->IsIn(Target)) Objects.AddUnique(Ref);
		}
		const int32 Roots = Objects.Num();
		for (int32 i = 0; i < Roots; ++i)
		{
			TArray<UObject*> Inner;
			MCPGetNestedSubobjects(Objects[i], Inner);
			for (UObject* Obj : Inner) Objects.AddUnique(Obj);
		}
		Objects.Sort([](const UObject& A, const UObject& B) { return A.GetPathName() < B.GetPathName(); });
		FString Out;
		for (UObject* Obj : Objects)
		{
			Out += Obj->GetPathName() + TEXT("|") + Obj->GetClass()->GetName() + TEXT("\n");
			for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
			{
				if (It->HasAnyPropertyFlags(CPF_Transient)) continue;
				FString Text;
				It->ExportTextItem_InContainer(Text, Obj, nullptr, Obj, PPF_None);
				Out += It->GetName() + TEXT("=") + Text + TEXT("\n");
			}
		}
		return Out;
	}

	static const FString UnavailableMsg =
		TEXT("Epic ToolsetRegistry not available. Requires UE 5.8+ with the ToolsetRegistry plugin enabled in the project.");

	// outputSchema per qualified tool name. Game thread only. Re-read when a tool
	// is missing, since a toolset can register after the first call.
	static TMap<FString, TSharedPtr<FJsonObject>>& OutputSchemas()
	{
		static TMap<FString, TSharedPtr<FJsonObject>> Cache;
		return Cache;
	}

	static void RefreshOutputSchemas()
	{
		TMap<FString, TSharedPtr<FJsonObject>>& Cache = OutputSchemas();
		Cache.Reset();
		FString Raw;
		TArray<TSharedPtr<FJsonValue>> Toolsets;
		if (!GetAllSchemas(Raw) || !ParseJsonArray(Raw, Toolsets)) return;
		for (const TSharedPtr<FJsonValue>& TV : Toolsets)
		{
			const TSharedPtr<FJsonObject>* Toolset = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
			if (!TV.IsValid() || !TV->TryGetObject(Toolset) || !Toolset || !(*Toolset)->TryGetArrayField(TEXT("tools"), Tools) || !Tools) continue;
			FString ToolsetName;
			(*Toolset)->TryGetStringField(TEXT("name"), ToolsetName);
			for (const TSharedPtr<FJsonValue>& V : *Tools)
			{
				const TSharedPtr<FJsonObject>* Tool = nullptr;
				FString Name;
				if (!V.IsValid() || !V->TryGetObject(Tool) || !Tool || !(*Tool)->TryGetStringField(TEXT("name"), Name)) continue;
				const TSharedPtr<FJsonObject>* Schema = nullptr;
				(*Tool)->TryGetObjectField(TEXT("outputSchema"), Schema);
				Cache.Add(Name.StartsWith(ToolsetName + TEXT(".")) ? Name : ToolsetName + TEXT(".") + Name, Schema ? *Schema : nullptr);
			}
		}
	}

	static TSharedPtr<FJsonObject> FindOutputSchema(const FString& Toolset, const FString& Tool)
	{
		const FString Qualified = Toolset + TEXT(".") + Tool;
		if (!OutputSchemas().Contains(Qualified)) RefreshOutputSchemas();
		const TSharedPtr<FJsonObject>* Found = OutputSchemas().Find(Qualified);
		return Found ? *Found : nullptr;
	}

	// FJsonSchemaGenerator stamps a struct's name as the title of its schema
	// node, so an FToolsetImage slot reads {type: object, title: ToolsetImage}.
	// The same signal Epic's MCP server keys its image content blocks on.
	static bool IsImageSlot(const TSharedPtr<FJsonObject>& Schema)
	{
		FString Type, Title;
		Schema->TryGetStringField(TEXT("type"), Type);
		Schema->TryGetStringField(TEXT("title"), Title);
		return Type == TEXT("object") && Title == TEXT("ToolsetImage");
	}

	static FString EscapePointerPart(const FString& Part)
	{
		return Part.Replace(TEXT("~"), TEXT("~0")).Replace(TEXT("/"), TEXT("~1"));
	}

	/**
	 * Walks Value beside its outputSchema and moves every image the schema marks
	 * into OutMedia, leaving a urn in its slot. Returns the value to keep at this
	 * slot. A null or malformed image is left as the tool wrote it.
	 */
	static TSharedPtr<FJsonValue> ExtractMedia(const TSharedPtr<FJsonObject>& Schema, const TSharedPtr<FJsonValue>& Value, const FString& Pointer, const FString& UriPrefix, TArray<TSharedPtr<FJsonValue>>& OutMedia, int32 Depth)
	{
		if (!Schema.IsValid() || !Value.IsValid() || Depth > 64) return Value;
		if (IsImageSlot(Schema))
		{
			const TSharedPtr<FJsonObject>* Image = nullptr;
			FString Mime, Data;
			if (!Value->TryGetObject(Image) || !Image || !(*Image)->TryGetStringField(TEXT("mimeType"), Mime)
				|| !(*Image)->TryGetStringField(TEXT("data"), Data) || Mime.IsEmpty() || Data.IsEmpty())
			{
				return Value;
			}
			const FString Uri = UriPrefix + Pointer;
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("type"), TEXT("image"));
			Entry->SetStringField(TEXT("mimeType"), Mime);
			Entry->SetStringField(TEXT("data"), Data);
			Entry->SetStringField(TEXT("uri"), Uri);
			Entry->SetStringField(TEXT("pointer"), Pointer);
			OutMedia.Add(MakeShared<FJsonValueObject>(Entry));
			return MakeShared<FJsonValueString>(Uri);
		}
		FString Type;
		Schema->TryGetStringField(TEXT("type"), Type);
		if (Type == TEXT("object"))
		{
			const TSharedPtr<FJsonObject>* Properties = nullptr;
			const TSharedPtr<FJsonObject>* Object = nullptr;
			if (!Schema->TryGetObjectField(TEXT("properties"), Properties) || !Properties || !Value->TryGetObject(Object) || !Object) return Value;
			for (const auto& Pair : (*Properties)->Values)
			{
				const FString Key(*Pair.Key);
				const TSharedPtr<FJsonObject>* Sub = nullptr;
				const TSharedPtr<FJsonValue> Child = (*Object)->TryGetField(Key);
				if (!Child.IsValid() || !Pair.Value.IsValid() || !Pair.Value->TryGetObject(Sub) || !Sub) continue;
				const TSharedPtr<FJsonValue> Kept = ExtractMedia(*Sub, Child, Pointer + TEXT("/") + EscapePointerPart(Key), UriPrefix, OutMedia, Depth + 1);
				if (Kept != Child) (*Object)->SetField(Key, Kept);
			}
			return Value;
		}
		if (Type == TEXT("array"))
		{
			const TSharedPtr<FJsonObject>* Items = nullptr;
			const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
			if (!Schema->TryGetObjectField(TEXT("items"), Items) || !Items || !Value->TryGetArray(Array) || !Array) return Value;
			TArray<TSharedPtr<FJsonValue>> Out = *Array;
			bool bChanged = false;
			for (int32 Index = 0; Index < Out.Num(); ++Index)
			{
				const TSharedPtr<FJsonValue> Kept = ExtractMedia(*Items, Out[Index], Pointer + TEXT("/") + FString::FromInt(Index), UriPrefix, OutMedia, Depth + 1);
				if (Kept != Out[Index])
				{
					Out[Index] = Kept;
					bChanged = true;
				}
			}
			return bChanged ? MakeShared<FJsonValueArray>(Out) : Value;
		}
		return Value;
	}

	/** The answer for a finished UToolCallAsyncResult. */
	static TSharedPtr<FJsonValue> BuildCallAnswer(
		const FString& Toolset, const FString& Tool, const FString& ToolName, UObject* Result,
		bool bHadTarget, const TWeakObjectPtr<UObject>& VerifiedTarget, const FString& Before);
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
	// Read before the registry check can fail (#1057).
	const FString Filter = OptionalString(Params, TEXT("nameFilter"), TEXT(""));
	// When true, return the full tool objects (name/description/input/output
	// schema) instead of just tool names. Used by the server to build
	// per-category first-class actions from the live catalog in a single call.
	const bool bIncludeSchemas = OptionalBool(Params, TEXT("includeSchemas"), false);

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
	if (Input.IsEmpty() && TryGetObjectParam(Params, TEXT("input"), InObj) && InObj && (*InObj).IsValid())
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

	// A tool that reports success while its target is unchanged applied nothing (#1318).
	UObject* VerifiedTarget = UEMCPEpic::ResolveInputTarget(Input, UEMCPEpic::VerifiedTargetField(ToolName));
	const FString Before = VerifiedTarget ? UEMCPEpic::Fingerprint(VerifiedTarget) : FString();
	const TWeakObjectPtr<UObject> WeakTarget(VerifiedTarget);
	const bool bHadTarget = VerifiedTarget != nullptr;

	UObject* Result = UEMCPEpic::ExecuteTool(Toolset, ToolName, Input);
	if (!Result)
	{
		return MCPError(FString::Printf(
			TEXT("ExecuteTool returned no result for %s / %s (tool not found or registry unavailable)"),
			*Toolset, *Tool));
	}

	FBoolProperty* CompleteProp = CastField<FBoolProperty>(Result->GetClass()->FindPropertyByName(TEXT("bIsComplete")));
	if (!CompleteProp || CompleteProp->GetPropertyValue_InContainer(Result))
	{
		return UEMCPEpic::BuildCallAnswer(Toolset, Tool, ToolName, Result, bHadTarget, WeakTarget, Before);
	}

	// The tool answers later (a capture waiting on a frame, an async Python tool).
	// The registry hands back a promise object with no cancel of its own, so a
	// cancel stops the wait here and the tool runs on to its end in the editor.
	const FMCPDeferredCallPtr Deferred = MCPDeferRequest();
	if (!Deferred)
	{
		return MCPError(FString::Printf(
			TEXT("Tool '%s' completes asynchronously, and only a bridge request can wait for it."), *Tool));
	}
	Deferred->SetCancelNote(TEXT("Epic's ToolsetRegistry gives a running tool no cancel hook, so the tool keeps running in the editor; only the wait was stopped."));
	TStrongObjectPtr<UObject> Pending(Result);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[Deferred, Pending, CompleteProp, Toolset, Tool, ToolName, bHadTarget, WeakTarget, Before](float) -> bool
		{
			if (Deferred->IsCancelled()) return false;
			if (!CompleteProp->GetPropertyValue_InContainer(Pending.Get())) return true;
			Deferred->Complete(UEMCPEpic::BuildCallAnswer(Toolset, Tool, ToolName, Pending.Get(), bHadTarget, WeakTarget, Before));
			return false;
		}));
	return MCPDeferredMarker();
}

TSharedPtr<FJsonValue> UEMCPEpic::BuildCallAnswer(
	const FString& Toolset, const FString& Tool, const FString& ToolName, UObject* Result,
	bool bHadTarget, const TWeakObjectPtr<UObject>& WeakTarget, const FString& Before)
{
	UClass* RC = Result->GetClass();
	FString ToolError;
	if (FStrProperty* P = CastField<FStrProperty>(RC->FindPropertyByName(TEXT("Error"))))
	{
		ToolError = P->GetPropertyValue_InContainer(Result);
	}
	if (!ToolError.IsEmpty())
	{
		return MCPError(FString::Printf(TEXT("Tool '%s' failed: %s"), *Tool, *ToolError));
	}

	FString Value;
	if (FStrProperty* P = CastField<FStrProperty>(RC->FindPropertyByName(TEXT("Value"))))
	{
		Value = P->GetPropertyValue_InContainer(Result);
	}

	UObject* VerifiedTarget = WeakTarget.Get();
	const bool bTargetUnchanged = IsValid(VerifiedTarget) && UEMCPEpic::Fingerprint(VerifiedTarget) == Before;
	TSharedPtr<FJsonObject> Res = MCPSuccess();
	if (bTargetUnchanged)
	{
		const bool bPCGNode = VerifiedTarget->GetClass()->GetName() == TEXT("PCGNode");
		Res = MCPErrorObject(FString::Printf(
			TEXT("Tool '%s' reported success, but '%s' is unchanged: nothing was applied, or every value already matched.%s"),
			*Tool, *VerifiedTarget->GetPathName(),
			bPCGNode ? TEXT(" For PCG node settings use pcg(set_node_settings); for a graph's input or output pins use pcg(set_graph_pins).") : TEXT("")));
		Res->SetStringField(TEXT("reason"), TEXT("no_change"));
		Res->SetStringField(TEXT("target"), VerifiedTarget->GetPathName());
	}
	Res->SetStringField(TEXT("toolset"), Toolset);
	Res->SetStringField(TEXT("tool"), Tool);
	if (bHadTarget) Res->SetBoolField(TEXT("targetChanged"), !bTargetUnchanged);

	if (!Value.IsEmpty())
	{
		TSharedPtr<FJsonValue> Parsed;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Value);
		if (FJsonSerializer::Deserialize(Reader, Parsed) && Parsed.IsValid())
		{
			// Images the outputSchema marks travel as media, so the server can hand
			// them to the client as image content instead of base64 inside text.
			TArray<TSharedPtr<FJsonValue>> Media;
			const FString UriPrefix = FString::Printf(TEXT("urn:toolresult:%s.%s:%s:"), *Toolset, *ToolName,
				*FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
			Parsed = UEMCPEpic::ExtractMedia(UEMCPEpic::FindOutputSchema(Toolset, ToolName), Parsed, FString(), UriPrefix, Media, 0);
			Res->SetField(TEXT("result"), Parsed);
			if (Media.Num() > 0) Res->SetArrayField(TEXT("media"), Media);
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
	if (bHadTarget)
	{
		Res->SetBoolField(TEXT("idempotencyObservable"), true);
		Res->SetStringField(TEXT("idempotencyNote"), TEXT("The target the input names was compared before and after the call; targetChanged reports the outcome."));
		return MCPResult(Res);
	}
	Res->SetBoolField(TEXT("idempotencyObservable"), false);
	Res->SetStringField(TEXT("idempotencyNote"), FString::Printf(
		TEXT("'%s.%s' decides whether it changed anything, and the toolset API returns its result value without "
		     "saying so. Read 'result' to see what the tool reported; this wrapper adds no claim of its own."),
		*Toolset, *ToolName));
	return MCPResult(Res);
}

void FEpicHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Reports parameters its handlers never read (#1057).
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("epic"));

	// #1057: the discovery handlers declare their parameters here.
	using EType = EMCPParamType;
	Registry.RegisterHandler(TEXT("epic_status"), &Status, {});
	Registry.RegisterHandler(TEXT("epic_list_toolsets"), &ListToolsets, {
		MCPParam::Optional(TEXT("nameFilter"), EType::String, TEXT("Case-sensitive substring of the qualified toolset name")),
		MCPParam::Optional(TEXT("includeSchemas"), EType::Boolean, TEXT("Return each tool with its input and output schemas instead of its name (default false)")),
	});
	Registry.RegisterHandler(TEXT("epic_describe_toolset"), &DescribeToolset, {
		MCPParam::Required(TEXT("toolset"), EType::String, TEXT("Qualified toolset name from epic_list_toolsets, e.g. GASToolsets.AttributeSetToolset")),
	});
	// The dispatcher every wrapped engine tool goes through. Its own contract is
	// this bag; each generated epic_* action builds it from the wrapped tool's
	// recorded input schema, which is that action's contract (#1057).
	// 300s: an asynchronous tool's answer is awaited within the handler's budget.
	Registry.RegisterHandlerWithTimeout(TEXT("epic_call_tool"), &CallTool, 300.0f, {
		MCPParam::Required(TEXT("toolset"), EType::String, TEXT("Qualified toolset name from epic_list_toolsets, e.g. GASToolsets.AttributeSetToolset")),
		MCPParam::Required(TEXT("tool"), EType::String, TEXT("Tool name from describe_toolset, bare or qualified with its toolset")),
		MCPParam::Optional(TEXT("input"), EType::Object, TEXT("Tool arguments as a JSON object")),
		MCPParam::Optional(TEXT("inputJson"), EType::String, TEXT("Tool arguments as a raw JSON string; wins over input")),
	}, MCPSpec::ContractExempt(TEXT("runs a registered engine tool")));

	// Epic's Unreal MCP plugin: its own tool list and resource providers.
	Registry.RegisterHandler(TEXT("epic_list_mcp_tools"), &ListMCPTools, {
		MCPParam::Optional(TEXT("nameFilter"), EType::String, TEXT("Case-sensitive substring of the tool name")),
		MCPParam::Optional(TEXT("includeSchemas"), EType::Boolean, TEXT("Return each tool's input and output schemas (default false)")),
		MCPParam::Optional(TEXT("includeRegistryAdapters"), EType::Boolean, TEXT("Also list the tools that only adapt ToolsetRegistry tools, which epic(list_toolsets) already covers (default false)")),
	});
	Registry.RegisterHandlerWithTimeout(TEXT("epic_call_mcp_tool"), &CallMCPTool, 300.0f, {
		MCPParam::Required(TEXT("tool"), EType::String, TEXT("Tool name from epic_list_mcp_tools")),
		MCPParam::Optional(TEXT("input"), EType::Object, TEXT("Tool arguments as a JSON object")),
		MCPParam::Optional(TEXT("inputJson"), EType::String, TEXT("Tool arguments as a raw JSON string; wins over input")),
	}, MCPSpec::ContractExempt(TEXT("runs a registered engine tool")));
	Registry.RegisterHandler(TEXT("epic_list_mcp_resources"), &ListMCPResources, {});
	Registry.RegisterHandler(TEXT("epic_read_mcp_resource"), &ReadMCPResource, {
		MCPParam::Required(TEXT("uri"), EType::String, TEXT("Resource uri from epic_list_mcp_resources")),
	});
	RegisterAIAssistantHandlers(Registry);
}
