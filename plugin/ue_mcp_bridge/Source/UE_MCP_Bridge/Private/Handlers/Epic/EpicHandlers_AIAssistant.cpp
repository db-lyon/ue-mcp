#include "EpicHandlers.h"
#include "MCPAIAssistantListener.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerStaticInvoke.h"

#include "Misc/EngineVersion.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "UObject/WeakObjectPtrTemplates.h"

// ── Reflection bridge to UAIAssistantInstance (UE 5.9+) ───
//
// The class lives in the AIAssistantEditor module (/Script/AIAssistantEditor.AIAssistantInstance).
// Early 5.9 builds had it in AIAssistant; the plugin's ClassRedirects map the old path, but
// FindObject does not follow redirects, so both paths are tried.
//
// Static UFUNCTIONs, each returning an async string result (UAsyncFunctionResultString,
// UToolCallAsyncResultString on early builds; both expose bIsComplete, Error and Value):
//   SendTextMessage(InConversationId, Message, HiddenContext)            -> conversation id
//   SendTextMessageAndGetReply(InConversationId, Message, HiddenContext) -> reply text
//   GetConversationJson(InConversationId)                                -> conversation JSON
// Results complete on later game-thread ticks, so each call is tracked under a
// requestId and the server polls epic_ai_assistant_get_request for it.
namespace UEMCPAIAssistant
{
	static const TCHAR* const InstanceClassPath = TEXT("/Script/AIAssistantEditor.AIAssistantInstance");
	static const TCHAR* const LegacyInstanceClassPath = TEXT("/Script/AIAssistant.AIAssistantInstance");
	static const TCHAR* const Subject = TEXT("Epic's AI Assistant API (UAIAssistantInstance)");
	static const TCHAR* const FnSend = TEXT("SendTextMessage");
	static const TCHAR* const FnSendAndReply = TEXT("SendTextMessageAndGetReply");
	static const TCHAR* const FnGetConversation = TEXT("GetConversationJson");
	static const TCHAR* const FnGet = TEXT("Get");
	static const int32 MaxEvents = 1000;
	static const double RequestLifetimeSeconds = 3600.0;

	struct FAssistantRequest
	{
		FString Function;
		FString ConversationId;
		TWeakObjectPtr<UObject> Result;
		FDateTime StartedUtc;
		int64 FirstEventSeq = 0;
	};

	struct FUpdateEvent
	{
		int64 Seq = 0;
		FString ConversationId;
		FString UpdateType;
		FDateTime Utc;
	};

	static TMap<int64, FAssistantRequest> Requests;
	static int64 NextRequestId = 1;
	static TArray<FUpdateEvent> Events;
	static int64 NextEventSeq = 1;
	static TWeakObjectPtr<UObject> BoundInstance;
	static UMCPAIAssistantListener* Listener = nullptr;
	static UEnum* UpdateTypeEnum = nullptr;
	static FString ListenError;

	static UClass* InstanceClass()
	{
		if (UClass* Cls = FindObject<UClass>(nullptr, InstanceClassPath)) return Cls;
		return FindObject<UClass>(nullptr, LegacyInstanceClassPath);
	}

	static FString EngineVersionString()
	{
		const FEngineVersion& V = FEngineVersion::Current();
		return FString::Printf(TEXT("%d.%d"), V.GetMajor(), V.GetMinor());
	}

	/** unsupported_engine_version before 5.9, PLUGIN_NOT_AVAILABLE when the plugin is off. */
	static TSharedPtr<FJsonObject> UnavailableObject()
	{
		const FEngineVersion& V = FEngineVersion::Current();
		TSharedPtr<FJsonObject> Obj;
		if (V.GetMajor() == 5 && V.GetMinor() < 9)
		{
			Obj = MCPUnsupportedEngineError(Subject, TEXT("5.9"),
				FString::Printf(TEXT("This editor is %s"), *EngineVersionString()))->AsObject();
		}
		else
		{
			Obj = MCPErrorObject(FString::Printf(
				TEXT("AI Assistant plugin not available: %s is not loaded. Enable it with project(enable_plugin, pluginName=\"AIAssistant\") and restart the editor."),
				Subject));
			Obj->SetStringField(TEXT("errorCode"), TEXT("PLUGIN_NOT_AVAILABLE"));
		}
		Obj->SetStringField(TEXT("requiredPlugin"), TEXT("AIAssistant"));
		Obj->SetStringField(TEXT("requiredClass"), InstanceClassPath);
		Obj->SetStringField(TEXT("engineVersion"), EngineVersionString());
		return Obj;
	}

	static TSharedPtr<FJsonValue> MissingFunction(const TCHAR* Function)
	{
		TSharedPtr<FJsonObject> Obj = MCPErrorObject(FString::Printf(
			TEXT("UAIAssistantInstance on this engine has no UFUNCTION %s; the engine API changed."), Function));
		Obj->SetStringField(TEXT("errorCode"), TEXT("ai_assistant_api_changed"));
		Obj->SetStringField(TEXT("function"), Function);
		return MCPResult(Obj);
	}

	static UObject* ExistingInstance(UClass* Cls)
	{
		TArray<UObject*> Found;
		GetObjectsOfClass(Cls, Found);
		for (UObject* Obj : Found)
		{
			if (IsValid(Obj)) return Obj;
		}
		return nullptr;
	}

	static UObject* CallStatic(UClass* Cls, const TCHAR* Function, const TMap<FString, FString>& Args)
	{
		UObject* Ret = nullptr;
		MCPStaticInvoke::CallWithStrings(Cls, Function, Args, [&](UFunction* Func, uint8* Buffer)
		{
			Ret = MCPStaticInvoke::ReturnedObject(Func, Buffer);
		});
		return Ret;
	}

	/** The delegate's parms must sit where the listener's do: an FString, then one byte. */
	static bool LayoutMatches(UFunction* Signature, UFunction* Mine)
	{
		if (!Signature || !Mine || Signature->ParmsSize != Mine->ParmsSize) return false;
		TArray<FProperty*> A, B;
		for (TFieldIterator<FProperty> It(Signature); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) A.Add(*It);
		for (TFieldIterator<FProperty> It(Mine); It && It->HasAnyPropertyFlags(CPF_Parm); ++It) B.Add(*It);
		if (A.Num() != 2 || B.Num() != 2) return false;
		if (!CastField<FStrProperty>(A[0]) || A[1]->GetSize() != 1) return false;
		return A[0]->GetOffset_ForUFunction() == B[0]->GetOffset_ForUFunction()
			&& A[1]->GetOffset_ForUFunction() == B[1]->GetOffset_ForUFunction();
	}

	/** Subscribe to Instance's OnConversationUpdated once per instance. */
	static void EnsureListening(UObject* Instance)
	{
		if (!Instance || BoundInstance.Get() == Instance) return;
		FMulticastDelegateProperty* Prop = CastField<FMulticastDelegateProperty>(
			Instance->GetClass()->FindPropertyByName(TEXT("OnConversationUpdated")));
		if (!Prop || !Prop->SignatureFunction)
		{
			ListenError = TEXT("UAIAssistantInstance has no OnConversationUpdated delegate on this engine.");
			return;
		}
		if (!Listener)
		{
			Listener = NewObject<UMCPAIAssistantListener>(GetTransientPackage());
			Listener->AddToRoot();
		}
		UFunction* Handler = Listener->FindFunction(TEXT("HandleConversationUpdated"));
		if (!LayoutMatches(Prop->SignatureFunction, Handler))
		{
			ListenError = TEXT("OnConversationUpdated changed signature on this engine; conversation updates are not recorded.");
			return;
		}
		for (TFieldIterator<FProperty> It(Prop->SignatureFunction); It; ++It)
		{
			if (FEnumProperty* EP = CastField<FEnumProperty>(*It)) UpdateTypeEnum = EP->GetEnum();
			else if (FByteProperty* BP = CastField<FByteProperty>(*It)) UpdateTypeEnum = BP->Enum;
		}
		FScriptDelegate Delegate;
		Delegate.BindUFunction(Listener, TEXT("HandleConversationUpdated"));
		Prop->AddDelegate(MoveTemp(Delegate), Instance);
		BoundInstance = Instance;
		ListenError.Reset();
	}

	static FString UpdateTypeName(uint8 Value)
	{
		if (UpdateTypeEnum) return UpdateTypeEnum->GetNameStringByValue(Value);
		static const TCHAR* const Names[] = { TEXT("Unknown"), TEXT("MessagesUpdated"), TEXT("Stopped"), TEXT("Complete"), TEXT("Started") };
		return Value < UE_ARRAY_COUNT(Names) ? FString(Names[Value]) : FString::FromInt(Value);
	}

	static void RecordEvent(const FString& ConversationId, uint8 UpdateType)
	{
		FUpdateEvent& E = Events.AddDefaulted_GetRef();
		E.Seq = NextEventSeq++;
		E.ConversationId = ConversationId;
		E.UpdateType = UpdateTypeName(UpdateType);
		E.Utc = FDateTime::UtcNow();
		if (Events.Num() > MaxEvents) Events.RemoveAt(0, Events.Num() - MaxEvents);
	}

	static void Release(int64 Id)
	{
		if (FAssistantRequest* R = Requests.Find(Id))
		{
			if (UObject* Obj = R->Result.Get()) Obj->RemoveFromRoot();
			Requests.Remove(Id);
		}
	}

	/** Drop requests nobody polled for an hour, so their results can be collected. */
	static void PruneStale()
	{
		const FDateTime Now = FDateTime::UtcNow();
		TArray<int64> Stale;
		for (const TPair<int64, FAssistantRequest>& Pair : Requests)
		{
			if ((Now - Pair.Value.StartedUtc).GetTotalSeconds() > RequestLifetimeSeconds) Stale.Add(Pair.Key);
		}
		for (int64 Id : Stale) Release(Id);
	}

	static int64 Track(const TCHAR* Function, const FString& ConversationId, UObject* Result, int64 FirstEventSeq)
	{
		PruneStale();
		Result->AddToRoot();
		const int64 Id = NextRequestId++;
		FAssistantRequest& R = Requests.Add(Id);
		R.Function = Function;
		R.ConversationId = ConversationId;
		R.Result = Result;
		R.StartedUtc = FDateTime::UtcNow();
		R.FirstEventSeq = FirstEventSeq;
		return Id;
	}

	/** The conversation a reply landed in: the one given, else the first update after the send. */
	static FString ConversationOf(const FAssistantRequest& R, FString& OutSource)
	{
		if (!R.ConversationId.IsEmpty())
		{
			OutSource = TEXT("request");
			return R.ConversationId;
		}
		for (const FUpdateEvent& E : Events)
		{
			if (E.Seq >= R.FirstEventSeq && !E.ConversationId.IsEmpty())
			{
				OutSource = TEXT("update_event");
				return E.ConversationId;
			}
		}
		return FString();
	}

	/** The request's state; a terminal one releases it. */
	static TSharedPtr<FJsonObject> Report(int64 Id)
	{
		FAssistantRequest& R = Requests.FindChecked(Id);
		UObject* Result = R.Result.Get();
		TSharedPtr<FJsonObject> Res;
		bool bTerminal = true;
		if (!Result)
		{
			Res = MCPErrorObject(TEXT("The AI Assistant result object was destroyed before it completed."));
			Res->SetStringField(TEXT("errorCode"), TEXT("ai_assistant_result_lost"));
			Res->SetStringField(TEXT("state"), TEXT("failed"));
		}
		else
		{
			const MCPStaticInvoke::FAsyncResultState S = MCPStaticInvoke::ReadAsyncResult(Result);
			if (!S.bComplete)
			{
				Res = MCPSuccess();
				Res->SetStringField(TEXT("state"), TEXT("pending"));
				bTerminal = false;
			}
			else if (!S.Error.IsEmpty())
			{
				Res = MCPErrorObject(FString::Printf(TEXT("AI Assistant %s failed: %s"), *R.Function, *S.Error));
				Res->SetStringField(TEXT("errorCode"), TEXT("ai_assistant_failed"));
				Res->SetStringField(TEXT("assistantError"), S.Error);
				Res->SetStringField(TEXT("state"), TEXT("failed"));
			}
			else
			{
				Res = MCPSuccess();
				Res->SetStringField(TEXT("state"), TEXT("complete"));
				if (R.Function == FnSend)
				{
					Res->SetStringField(TEXT("conversationId"), S.Value);
				}
				else if (R.Function == FnSendAndReply)
				{
					// Current 5.9 builds resolve with FSendTextMessageReply serialized as
					// {"conversationId", "reply"}; earlier ones with the bare reply text. The
					// conversationId in it is the canonical one: the first update event can
					// carry a provisional local id that the conversation is renamed from.
					TSharedPtr<FJsonObject> Wrapped;
					FString WrappedReply;
					TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(S.Value);
					if (S.Value.StartsWith(TEXT("{")) && FJsonSerializer::Deserialize(Reader, Wrapped) && Wrapped.IsValid()
						&& Wrapped->TryGetStringField(TEXT("reply"), WrappedReply))
					{
						Res->SetStringField(TEXT("reply"), WrappedReply);
						FString WrappedConversation;
						if (Wrapped->TryGetStringField(TEXT("conversationId"), WrappedConversation) && !WrappedConversation.IsEmpty())
						{
							Res->SetStringField(TEXT("conversationId"), WrappedConversation);
							Res->SetStringField(TEXT("conversationIdSource"), TEXT("reply"));
						}
					}
					else
					{
						Res->SetStringField(TEXT("reply"), S.Value);
					}
					FString Source;
					const FString Conversation = Res->HasField(TEXT("conversationId")) ? FString() : ConversationOf(R, Source);
					if (!Conversation.IsEmpty())
					{
						Res->SetStringField(TEXT("conversationId"), Conversation);
						Res->SetStringField(TEXT("conversationIdSource"), Source);
					}
				}
				else
				{
					TSharedPtr<FJsonObject> Conversation;
					TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(S.Value);
					if (FJsonSerializer::Deserialize(Reader, Conversation) && Conversation.IsValid())
					{
						Res->SetObjectField(TEXT("conversation"), Conversation);
					}
					else
					{
						Res->SetStringField(TEXT("conversationJson"), S.Value);
					}
				}
			}
		}
		Res->SetNumberField(TEXT("requestId"), static_cast<double>(Id));
		Res->SetStringField(TEXT("function"), R.Function);
		if (!R.ConversationId.IsEmpty() && !Res->HasField(TEXT("conversationId")))
		{
			Res->SetStringField(TEXT("conversationId"), R.ConversationId);
		}
		Res->SetNumberField(TEXT("elapsedSeconds"), (FDateTime::UtcNow() - R.StartedUtc).GetTotalSeconds());
		if (bTerminal) Release(Id);
		return Res;
	}

	/** Bind the update listener, call Function, and track what it returned. */
	static TSharedPtr<FJsonObject> Start(UClass* Cls, const TCHAR* Function, const FString& ConversationId,
		const TMap<FString, FString>& Args, FString& OutError)
	{
		// Get() opens the assistant panel and creates the singleton the delegate lives on.
		UObject* Instance = ExistingInstance(Cls);
		if (!Instance && Cls->FindFunctionByName(FnGet)) Instance = CallStatic(Cls, FnGet, {});
		EnsureListening(Instance);

		const int64 FirstSeq = NextEventSeq;
		UObject* Result = CallStatic(Cls, Function, Args);
		if (!Result)
		{
			OutError = FString::Printf(TEXT("UAIAssistantInstance::%s returned no result object."), Function);
			return nullptr;
		}
		return Report(Track(Function, ConversationId, Result, FirstSeq));
	}
}

void UMCPAIAssistantListener::HandleConversationUpdated(const FString& ConversationId, uint8 UpdateType)
{
	UEMCPAIAssistant::RecordEvent(ConversationId, UpdateType);
}

// ── Handlers ──────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FEpicHandlers::AIAssistantStatus(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCPAIAssistant;
	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetStringField(TEXT("engineVersion"), EngineVersionString());
	UClass* Cls = InstanceClass();
	Res->SetBoolField(TEXT("available"), Cls != nullptr);
	if (!Cls)
	{
		const TSharedPtr<FJsonObject> Why = UnavailableObject();
		Res->SetStringField(TEXT("errorCode"), Why->GetStringField(TEXT("errorCode")));
		Res->SetStringField(TEXT("reason"), Why->GetStringField(TEXT("error")));
		Res->SetStringField(TEXT("requiredPlugin"), TEXT("AIAssistant"));
		return MCPResult(Res);
	}

	TArray<TSharedPtr<FJsonValue>> Present, Missing;
	for (const TCHAR* Fn : { FnGet, FnSend, FnSendAndReply, FnGetConversation })
	{
		(Cls->FindFunctionByName(Fn) ? Present : Missing).Add(MakeShared<FJsonValueString>(Fn));
	}
	Res->SetArrayField(TEXT("functions"), Present);
	Res->SetArrayField(TEXT("missingFunctions"), Missing);
	Res->SetBoolField(TEXT("instanceExists"), ExistingInstance(Cls) != nullptr);
	Res->SetBoolField(TEXT("listening"), BoundInstance.IsValid());
	if (!ListenError.IsEmpty()) Res->SetStringField(TEXT("listenError"), ListenError);
	Res->SetNumberField(TEXT("pendingRequests"), Requests.Num());
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FEpicHandlers::AIAssistantSendMessage(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCPAIAssistant;
	const FString ConversationId = OptionalString(Params, TEXT("conversationId"), TEXT(""));
	const FString HiddenContext = OptionalString(Params, TEXT("hiddenContext"), TEXT(""));
	const bool bReply = OptionalBool(Params, TEXT("reply"), true);
	// The MCP server does the waiting; the handler echoes the request.
	const bool bWait = OptionalBool(Params, TEXT("wait"), true);
	const double WaitTimeout = OptionalNumber(Params, TEXT("timeoutSeconds"), 300.0);
	FString Message;
	if (TSharedPtr<FJsonValue> Err = RequireString(Params, TEXT("message"), Message)) return Err;

	UClass* Cls = InstanceClass();
	if (!Cls) return MCPResult(UnavailableObject());
	const TCHAR* Function = bReply ? FnSendAndReply : FnSend;
	if (!Cls->FindFunctionByName(Function)) return MissingFunction(Function);

	TMap<FString, FString> Args;
	Args.Add(TEXT("InConversationId"), ConversationId);
	Args.Add(TEXT("Message"), Message);
	Args.Add(TEXT("HiddenContext"), HiddenContext);
	FString StartError;
	TSharedPtr<FJsonObject> Res = Start(Cls, Function, ConversationId, Args, StartError);
	if (!Res) return MCPError(StartError);

	const bool bFailed = Res->GetStringField(TEXT("state")) == TEXT("failed");
	Res->SetBoolField(TEXT("changed"), !bFailed);
	if (bWait) Res->SetNumberField(TEXT("waitRequestedSeconds"), WaitTimeout);
	if (!bFailed)
	{
		MCPSetNoRollback(Res, TEXT("Sent a user message into an AI Assistant conversation. UAIAssistantInstance has no call that "
			"removes a message or deletes a conversation, so nothing can withdraw it. Each call sends another message."));
	}
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FEpicHandlers::AIAssistantGetConversation(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCPAIAssistant;
	const FString ConversationId = OptionalString(Params, TEXT("conversationId"), TEXT(""));
	const bool bWait = OptionalBool(Params, TEXT("wait"), true);
	const double WaitTimeout = OptionalNumber(Params, TEXT("timeoutSeconds"), 60.0);

	UClass* Cls = InstanceClass();
	if (!Cls) return MCPResult(UnavailableObject());
	if (!Cls->FindFunctionByName(FnGetConversation)) return MissingFunction(FnGetConversation);

	TMap<FString, FString> Args;
	Args.Add(TEXT("InConversationId"), ConversationId);
	FString StartError;
	TSharedPtr<FJsonObject> Res = Start(Cls, FnGetConversation, ConversationId, Args, StartError);
	if (!Res) return MCPError(StartError);
	if (bWait) Res->SetNumberField(TEXT("waitRequestedSeconds"), WaitTimeout);
	return MCPResult(Res);
}

TSharedPtr<FJsonValue> FEpicHandlers::AIAssistantGetRequest(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCPAIAssistant;
	int64 Id = 0;
	if (!TryGetNumberParam(Params, TEXT("requestId"), Id))
	{
		return MCPError(TEXT("Missing required parameter 'requestId'"));
	}
	if (!Requests.Contains(Id))
	{
		TSharedPtr<FJsonObject> Obj = MCPErrorObject(FString::Printf(
			TEXT("No AI Assistant request %lld is open. A request is released once its complete or failed state has been returned."), Id));
		Obj->SetStringField(TEXT("errorCode"), TEXT("ai_assistant_request_not_found"));
		Obj->SetNumberField(TEXT("requestId"), static_cast<double>(Id));
		return MCPResult(Obj);
	}
	return MCPResult(Report(Id));
}

TSharedPtr<FJsonValue> FEpicHandlers::AIAssistantGetEvents(const TSharedPtr<FJsonObject>& Params)
{
	using namespace UEMCPAIAssistant;
	int64 Since = 0;
	TryGetNumberParam(Params, TEXT("sinceSeq"), Since);
	const FString Filter = OptionalString(Params, TEXT("conversationId"), TEXT(""));

	UClass* Cls = InstanceClass();
	if (!Cls) return MCPResult(UnavailableObject());
	// Never opens the panel: binds only to an instance something else created.
	EnsureListening(ExistingInstance(Cls));

	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FUpdateEvent& E : Events)
	{
		if (E.Seq <= Since || (!Filter.IsEmpty() && E.ConversationId != Filter)) continue;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("seq"), static_cast<double>(E.Seq));
		Obj->SetStringField(TEXT("conversationId"), E.ConversationId);
		Obj->SetStringField(TEXT("updateType"), E.UpdateType);
		Obj->SetStringField(TEXT("utc"), E.Utc.ToIso8601());
		Out.Add(MakeShared<FJsonValueObject>(Obj));
	}
	TSharedPtr<FJsonObject> Res = MCPSuccess();
	Res->SetBoolField(TEXT("listening"), BoundInstance.IsValid());
	if (!BoundInstance.IsValid())
	{
		Res->SetStringField(TEXT("note"), ListenError.IsEmpty()
			? TEXT("No AI Assistant instance exists yet. Updates are recorded from the first send_message or get_conversation on.")
			: *ListenError);
	}
	Res->SetArrayField(TEXT("events"), Out);
	Res->SetNumberField(TEXT("nextSeq"), static_cast<double>(NextEventSeq - 1));
	return MCPResult(Res);
}

void FEpicHandlers::RegisterAIAssistantHandlers(FMCPHandlerRegistry& Registry)
{
	using EType = EMCPParamType;
	const FMCPParamSpec SpecConversation = MCPParam::Optional(TEXT("conversationId"), EType::String,
		TEXT("Conversation to use; empty or omitted starts a new one (send_message) or reads the active one (get_conversation)"));
	const FMCPParamSpec SpecWait = MCPParam::Optional(TEXT("wait"), EType::Boolean,
		TEXT("Return only once the result is complete (default true); false returns the requestId to poll with get_request"));

	Registry.RegisterHandler(TEXT("epic_ai_assistant_status"), &FEpicHandlers::AIAssistantStatus, {});
	Registry.RegisterHandler(TEXT("epic_ai_assistant_send_message"), &FEpicHandlers::AIAssistantSendMessage, {
		MCPParam::Required(TEXT("message"), EType::String, TEXT("The user-visible prompt text")),
		SpecConversation,
		MCPParam::Optional(TEXT("hiddenContext"), EType::String, TEXT("Context appended to the message but not shown in the panel")),
		MCPParam::Optional(TEXT("reply"), EType::Boolean, TEXT("Resolve with the assistant's reply text (default true); false resolves as soon as the message is bound to a conversation, with its id")),
		SpecWait,
		MCPParam::Optional(TEXT("timeoutSeconds"), EType::Number, TEXT("Longest wait in seconds (send_message default 300, get_conversation default 60)")),
	}, MCPSpec::ContractExempt(TEXT("opens the AI Assistant panel and sends a message to its backend")));
	Registry.RegisterHandler(TEXT("epic_ai_assistant_get_conversation"), &FEpicHandlers::AIAssistantGetConversation, {
		SpecConversation,
		SpecWait,
		MCPParam::Optional(TEXT("timeoutSeconds"), EType::Number, TEXT("Longest wait in seconds (send_message default 300, get_conversation default 60)")),
	}, MCPSpec::ContractExempt(TEXT("opens the AI Assistant panel")));
	Registry.RegisterHandler(TEXT("epic_ai_assistant_get_request"), &FEpicHandlers::AIAssistantGetRequest, {
		MCPParam::Required(TEXT("requestId"), EType::Integer, TEXT("requestId from send_message or get_conversation")),
	});
	Registry.RegisterHandler(TEXT("epic_ai_assistant_get_events"), &FEpicHandlers::AIAssistantGetEvents, {
		MCPParam::Optional(TEXT("sinceSeq"), EType::Integer, TEXT("Return only updates after this seq (default 0, every recorded update)")),
		MCPParam::Optional(TEXT("conversationId"), EType::String, TEXT("Return only this conversation's updates")),
	});
}
