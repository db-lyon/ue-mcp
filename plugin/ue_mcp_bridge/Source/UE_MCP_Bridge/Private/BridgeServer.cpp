#include "BridgeServer.h"
#include "BridgeParamEcho.h"
#include "BridgeStateFiles.h"
#include "UE_MCP_BridgeModule.h"
#include "MCPEngineStatus.h"
#include "MCPHandlerRegistration.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeLock.h"
#include "Misc/Char.h"
#include "Async/Async.h"
#include "HandlerCatalog.h"

#include "MCPSocketPlatform.h"

namespace
{
	// How long shutdown lets connection threads notice the stop flag and close
	// politely (their select is one second), and how long it then waits after
	// half-closing their sockets before giving up and saying so.
	constexpr double kConnectionCloseGraceSeconds = 2.0;
	constexpr double kConnectionDrainTimeoutSeconds = 10.0;
}

FMCPConnectionUnlist::FMCPConnectionUnlist(FMCPBridgeServer& InServer, FMCPSocketHandle InHandle)
	: Server(InServer)
	, Handle(InHandle)
{
}

FMCPConnectionUnlist::~FMCPConnectionUnlist()
{
	Server.UnlistConnection(Handle);
}

FMCPConnectionRelease::FMCPConnectionRelease(FMCPBridgeServer& InServer)
	: Server(InServer)
{
}

FMCPConnectionRelease::~FMCPConnectionRelease()
{
	Server.ReleaseConnectionSlot();
}

FMCPBridgeServer::FMCPBridgeServer(int32 Port, const FString& InPortSource, bool bInPortPinned)
	: ServerPort(Port)
	, PortSource(InPortSource)
	, bPortPinned(bInPortPinned)
	, ServerThread(nullptr)
	, bShouldStop(false)
	, bIsRunning(false)
	, InstanceId(FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens))
	, StartedAtUtc(FDateTime::UtcNow())
{
	// #817: construction-gated, from the command line or the environment only.
	// There is deliberately no way to turn this on over the socket: it is a
	// test facility, and a facility a caller can enable remotely is a facility
	// an attacker can enable remotely.
	FMCPParamEcho::Get().SetEnabled(FMCPParamEcho::ResolveEnabledFromEnvironment());

	MCPHandlerCatalog::RegisterAllHandlers(HandlerRegistry);
}

FMCPBridgeServer::~FMCPBridgeServer()
{
	Shutdown();
}

bool FMCPBridgeServer::Start()
{
	if (bIsRunning)
	{
		return false;
	}

	bShouldStop = false;
	ServerThread = FRunnableThread::Create(this, TEXT("MCPBridgeServer"), 0, TPri_Normal);
	return ServerThread != nullptr;
}

void FMCPBridgeServer::Shutdown()
{
	// No early return on bIsRunning. Exit() clears that flag, and on the
	// bind-failure path Exit() runs before Shutdown() does, so guarding on it
	// meant the server thread was never joined in exactly the case where the
	// thread had already failed and nobody was watching.
	bShouldStop = true;

	// Let anything waiting on the game thread give up now. Module teardown is
	// running on the game thread, so a queued handler will never execute and
	// its caller would otherwise sit here for the full handler timeout.
	GameThreadExecutor.BeginShutdown();

	if (ServerThread)
	{
		ServerThread->WaitForCompletion();
		delete ServerThread;
		ServerThread = nullptr;
	}

	// The accept loop is gone, but each connection thread captured `this` and
	// the module destroys this object as soon as Shutdown returns. A thread
	// still inside ProcessMessage at that point is running on freed memory:
	// that is the stop_editor-with-a-client-attached crash.
	//
	// Connection loops see bShouldStop at the end of their current one-second
	// select and close cleanly. Give them that long before being blunt about it.
	if (!WaitForConnectionsToFinish(kConnectionCloseGraceSeconds))
	{
		WakeAllConnections();
		if (!WaitForConnectionsToFinish(kConnectionDrainTimeoutSeconds))
		{
			UE_LOG(LogMCPBridge, Error,
				TEXT("[UE-MCP] %d bridge connection(s) still running after %.0fs. Continuing shutdown; a handler is not returning."),
				ActiveConnectionCount.GetValue(), kConnectionCloseGraceSeconds + kConnectionDrainTimeoutSeconds);
		}
	}

	bIsRunning = false;
}

void FMCPBridgeServer::RegisterConnection(FMCPSocketHandle Handle)
{
	ActiveConnectionCount.Increment();
	FScopeLock Lock(&ConnectionsMutex);
	LiveConnections.Add(Handle);
}

void FMCPBridgeServer::UnlistConnection(FMCPSocketHandle Handle)
{
	// Out of the set before the socket is closed, under the same lock
	// WakeAllConnections holds. Otherwise shutdown could half-close a
	// handle number the operating system had already handed to someone else.
	FScopeLock Lock(&ConnectionsMutex);
	LiveConnections.Remove(Handle);
}

void FMCPBridgeServer::ReleaseConnectionSlot()
{
	// The last thing a connection thread touches on this object, and the last
	// blocking call it makes at all: closesocket and the unlist have both
	// already happened. WaitForConnectionsToFinish only reads this counter and
	// holds no lock while it spins, so dropping it here cannot deadlock against
	// a thread that is still inside ConnectionsMutex.
	ActiveConnectionCount.Decrement();
}

void FMCPBridgeServer::WakeAllConnections()
{
	FScopeLock Lock(&ConnectionsMutex);
	for (const FMCPSocketHandle Handle : LiveConnections)
	{
		MCPSocket::ShutdownBoth(Handle);
	}
}

bool FMCPBridgeServer::WaitForConnectionsToFinish(double TimeoutSeconds)
{
	const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
	while (ActiveConnectionCount.GetValue() > 0)
	{
		if (FPlatformTime::Seconds() >= Deadline)
		{
			return false;
		}
		FPlatformProcess::Sleep(0.01f);
	}
	return true;
}

bool FMCPBridgeServer::Init()
{
	bIsRunning = true;
	return true;
}

uint32 FMCPBridgeServer::Run()
{
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge server thread started on port %d"), ServerPort);
	
	// Initialize platform sockets
	if (!MCPSocket::Startup())
	{
		UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Failed to initialize Winsock"));
		return 1;
	}

	// Create server socket
	const FMCPSocketHandle ServerSocketFD = socket(AF_INET, SOCK_STREAM, 0);
	if (ServerSocketFD == MCP_INVALID_SOCKET)
	{
		UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Failed to create socket"));
		MCPSocket::Cleanup();
		return 1;
	}

	// Claim the port exclusively.
	//
	// #821: Winsock's SO_REUSEADDR is not the POSIX one. It allows a bind to
	// succeed on a port another socket is actively listening on unless that
	// socket asked for exclusive use. With it set, the collision walk below
	// could never fire on Windows: a second editor of the same project bound at
	// offset 0, both processes believed they owned the port, and which listener
	// received a given connection was up to the stack. SO_EXCLUSIVEADDRUSE is
	// what makes the second bind fail, which is what lets the walk walk.
	//
	// On POSIX, SO_REUSEADDR only relaxes TIME_WAIT and cannot take a live
	// listener's port, so it stays there.
#if PLATFORM_WINDOWS
	int32 ExclusiveAddrUse = 1;
	setsockopt(ServerSocketFD, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (char*)&ExclusiveAddrUse, sizeof(ExclusiveAddrUse));
#else
	int32 ReuseAddr = 1;
	setsockopt(ServerSocketFD, SOL_SOCKET, SO_REUSEADDR, (char*)&ReuseAddr, sizeof(ReuseAddr));
#endif

	// Set TCP_NODELAY for immediate send (disable Nagle's algorithm)
	int32 NoDelay = 1;
	setsockopt(ServerSocketFD, IPPROTO_TCP, TCP_NODELAY, (char*)&NoDelay, sizeof(NoDelay));

	// Bind socket to loopback only. The bridge has no authentication on the
	// WebSocket upgrade, so binding to 0.0.0.0 (INADDR_ANY) would expose every
	// editor-side handler (including execute_python) to any client on the LAN.
	//
	// #492: when more than one editor is open locally, the default port is
	// already taken. Walk up to ServerPort+kMaxPortProbe so a second editor
	// can boot side-by-side; the actual bound port is published via a per-
	// project lockfile (see WritePortLockfile below).
	const int32 RequestedPort = ServerPort;
	constexpr int32 kMaxPortProbe = 50;
	int32 BoundPort = 0;
	bool bBound = false;
	for (int32 Offset = 0; Offset <= kMaxPortProbe; ++Offset)
	{
		sockaddr_in ServerAddr;
		FMemory::Memset(&ServerAddr, 0, sizeof(ServerAddr));
		ServerAddr.sin_family = AF_INET;
		ServerAddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		ServerAddr.sin_port = htons((uint16)(RequestedPort + Offset));

		if (bind(ServerSocketFD, (sockaddr*)&ServerAddr, sizeof(ServerAddr)) == 0)
		{
			BoundPort = RequestedPort + Offset;
			ServerPort = BoundPort;
			bBound = true;
			if (Offset > 0)
			{
				// Name the port that was asked for and where the request came
				// from. A user who pinned bridge.port has to be able to see
				// that the pin did not take, and that the lockfile (not their
				// config) is what says where the bridge actually is (#819).
				const FString Moved = FString::Printf(
					TEXT("[UE-MCP] Port %d (%s) was unavailable; bound to %d instead. The port lockfile names %d, which is what clients read (#492)."),
					RequestedPort, *PortSource, BoundPort, BoundPort);
				if (bPortPinned)
				{
					UE_LOG(LogMCPBridge, Warning, TEXT("%s"), *Moved);
				}
				else
				{
					UE_LOG(LogMCPBridge, Log, TEXT("%s"), *Moved);
				}
			}
			break;
		}
	}
	if (!bBound)
	{
		int32 ErrorCode = 0;
#if PLATFORM_WINDOWS
		ErrorCode = WSAGetLastError();
		UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Failed to bind to any port in [%d, %d], last error: %d"), RequestedPort, RequestedPort + kMaxPortProbe, ErrorCode);
#else
		UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Failed to bind to any port in [%d, %d]"), RequestedPort, RequestedPort + kMaxPortProbe);
#endif
		MCPSocket::Close(ServerSocketFD);
		MCPSocket::Cleanup();
		// #821: "editor alive, bridge dead" used to leave nothing on disk, so
		// the client could only report that it found no editor. Say what
		// actually happened, in a file that is not the live editor's record.
		WriteBindFailureRecord(RequestedPort, RequestedPort + kMaxPortProbe, ErrorCode);
		return 1;
	}

	// Listen
	if (listen(ServerSocketFD, 5) < 0)
	{
		int32 ErrorCode = 0;
#if PLATFORM_WINDOWS
		ErrorCode = WSAGetLastError();
		UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Failed to listen on socket, error: %d"), ErrorCode);
#else
		UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Failed to listen on socket"));
#endif
		MCPSocket::Close(ServerSocketFD);
		MCPSocket::Cleanup();
		WriteBindFailureRecord(BoundPort, BoundPort, ErrorCode);
		return 1;
	}

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge listening on ws://127.0.0.1:%d (loopback only)"), ServerPort);
	bIsRunning = true;

	// #817: this instance's own record first. It is written before port.json
	// because port.json may legitimately decline to name this bridge (another
	// live editor of the same project owns it), and in that case the instance
	// record is the only published address this editor has.
	WriteInstanceRecord(TEXT("listening"), ServerPort);

	// Records left by processes that are gone. Swept here, once, by the next
	// process that can prove them stale rather than by a timer, so a machine
	// that crashes repeatedly does not accumulate a directory of dead editors.
	ReapStaleInstanceRecords();

	// #492: publish the bound port to <Project>/Saved/UE_MCP_Bridge/port.json
	// so the npm client (which was started against this project's .uproject)
	// can find us even when the default port was already taken by another editor.
	WritePortLockfile(ServerPort);

	// Accept connections
	while (!bShouldStop)
	{
		fd_set ReadSet;
		FD_ZERO(&ReadSet);
		FD_SET(ServerSocketFD, &ReadSet);

		timeval Timeout;
		Timeout.tv_sec = 1;
		Timeout.tv_usec = 0;

		int32 SelectResult = select(ServerSocketFD + 1, &ReadSet, nullptr, nullptr, &Timeout);
		if (SelectResult > 0 && FD_ISSET(ServerSocketFD, &ReadSet))
		{
			sockaddr_in ClientAddr;
			socklen_t ClientAddrLen = sizeof(ClientAddr);
			const FMCPSocketHandle ClientSocketFD = accept(ServerSocketFD, (sockaddr*)&ClientAddr, &ClientAddrLen);
			if (ClientSocketFD != MCP_INVALID_SOCKET)
			{
				char AddrStr[INET_ADDRSTRLEN];
				inet_ntop(AF_INET, &ClientAddr.sin_addr, AddrStr, INET_ADDRSTRLEN);
				UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Client connected from %s:%d"),
					ANSI_TO_TCHAR(AddrStr), ntohs(ClientAddr.sin_port));

				// Handle each WebSocket connection in its own thread. Count it
				// here, before the thread exists, so a shutdown racing this
				// accept cannot decide that nothing is running and let the
				// module free the server out from under the new thread.
				RegisterConnection(ClientSocketFD);
				Async(EAsyncExecution::Thread, [this, ClientSocketFD]() {
					HandleWebSocketConnection(ClientSocketFD);
				});
			}
		}
	}

	// Cleanup
	MCPSocket::Close(ServerSocketFD);
	MCPSocket::Cleanup();

	bIsRunning = false;
	return 0;
}

void FMCPBridgeServer::Stop()
{
	bShouldStop = true;
}

void FMCPBridgeServer::Exit()
{
	bIsRunning = false;
	// #492: remove the lockfile on graceful shutdown so the next editor boot
	// doesn't see a stale entry. A hard-crash leaves the file, but the next
	// startup overwrites it with the live PID.
	//
	// #821: only if this instance wrote it. Exit() runs on every return from
	// Run(), the bind-failure path included, so an unconditional delete here
	// let an editor that never listened remove a running editor's record.
	DeletePortLockfileIfOwned();

	// #817: and this instance's own record. Exit() runs on the bind-failure
	// path too, where the record says "bind-failed" and has to survive: it is
	// the only thing that will still be on disk to explain why an editor that
	// is plainly running has no bridge. DeleteOwnInstanceRecord knows that.
	DeleteOwnInstanceRecord();
}

TSharedPtr<FJsonObject> FMCPBridgeServer::ParseJsonRpcRequest(const FString& Message)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Message);
	
	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{
		return JsonObject;
	}

	return nullptr;
}

FString FMCPBridgeServer::CreateJsonRpcResponse(const TSharedPtr<FJsonObject>& Request, const TSharedPtr<FJsonValue>& Result)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	
	if (Request.IsValid() && Request->HasField(TEXT("id")))
	{
		Response->SetField(TEXT("id"), Request->TryGetField(TEXT("id")));
	}
	
	Response->SetField(TEXT("result"), Result);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
	return OutputString;
}

FString FMCPBridgeServer::CreateJsonRpcError(const TSharedPtr<FJsonObject>& Request, int32 ErrorCode, const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	
	if (Request.IsValid() && Request->HasField(TEXT("id")))
	{
		Response->SetField(TEXT("id"), Request->TryGetField(TEXT("id")));
	}
	else
	{
		Response->SetField(TEXT("id"), MakeShared<FJsonValueNull>());
	}

	TSharedPtr<FJsonObject> ErrorObject = MakeShared<FJsonObject>();
	ErrorObject->SetNumberField(TEXT("code"), ErrorCode);
	ErrorObject->SetStringField(TEXT("message"), ErrorMessage);
	Response->SetObjectField(TEXT("error"), ErrorObject);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);
	return OutputString;
}

/**
 * The one answer every non-modal-safe method gets while a dialog is up.
 *
 * Shaped so a caller does not have to parse prose to act: `dialogBlocking` is
 * the flag to branch on, `buttons` is the dialog's own order, and `choices`
 * pairs each label with the literal call that presses it. Nothing here ranks
 * the buttons or hints at one, because the gate does not know which is right
 * and guessing is what this whole mechanism exists to stop.
 */
TSharedPtr<FJsonObject> FMCPBridgeServer::BuildDialogGateRefusal(
	const FString& Method, const FString& Title, const FString& Message, const TArray<FString>& Buttons)
{
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetBoolField(TEXT("success"), false);
	Out->SetBoolField(TEXT("dialogBlocking"), true);
	Out->SetStringField(TEXT("refusedMethod"), Method);
	Out->SetStringField(TEXT("dialogTitle"), Title);
	Out->SetStringField(TEXT("dialogMessage"), Message);

	TArray<TSharedPtr<FJsonValue>> ButtonValues;
	TArray<TSharedPtr<FJsonValue>> Choices;
	for (const FString& Label : Buttons)
	{
		ButtonValues.Add(MakeShared<FJsonValueString>(Label));

		TSharedPtr<FJsonObject> Choice = MakeShared<FJsonObject>();
		Choice->SetStringField(TEXT("buttonLabel"), Label);
		// Single quotes around the label would break on "Don't Save", which is
		// a real button on the prompt this fires for most often.
		Choice->SetStringField(
			TEXT("respondWith"),
			FString::Printf(TEXT("editor(action='respond_to_dialog', buttonLabel=\"%s\")"), *Label));
		Choices.Add(MakeShared<FJsonValueObject>(Choice));
	}
	Out->SetArrayField(TEXT("buttons"), ButtonValues);
	Out->SetArrayField(TEXT("choices"), Choices);

	Out->SetStringField(
		TEXT("error"),
		FString::Printf(
			TEXT("A modal dialog is blocking the editor, so '%s' was refused without running. ")
			TEXT("Unreal cannot execute anything else until the dialog is answered. ")
			TEXT("Read it in dialogMessage, choose a button, and press it with the call beside it in choices. ")
			TEXT("Every other action returns this same refusal until then."),
			*Method));
	return Out;
}

TSharedPtr<FJsonObject> FMCPBridgeServer::BuildCapabilitiesPayload()
{
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetBoolField(TEXT("success"), true);
	Payload->SetBoolField(TEXT("servedWithoutGameThread"), true);
	Payload->SetNumberField(TEXT("protocolVersion"), (double)UEMCP_BRIDGE_PROTOCOL_VERSION);
	Payload->SetNumberField(TEXT("handlerApiVersion"), (double)UEMCP_BRIDGE_API_VERSION);

	// The question behind "which version is this" is nearly always "is the
	// binary I am talking to the one built from the source on disk". A compile
	// timestamp answers that; a constant read out of a header file cannot,
	// because the header is the source and the source is what got ahead.
	Payload->SetStringField(TEXT("builtAt"), ANSI_TO_TCHAR(__DATE__ " " __TIME__));
	Payload->SetStringField(TEXT("engineVersion"), FEngineVersion::Current().ToString());
	Payload->SetStringField(TEXT("projectName"), FApp::GetProjectName());
	Payload->SetStringField(TEXT("instanceId"), InstanceId);
	Payload->SetNumberField(TEXT("pid"), (double)FPlatformProcess::GetCurrentProcessId());
	Payload->SetNumberField(TEXT("port"), ServerPort);
	Payload->SetStringField(TEXT("startedAt"), StartedAtUtc.ToIso8601());

	// Named capabilities rather than "anything at or above version N", so a
	// client can ask about the one thing it needs.
	static const TCHAR* const Features[] = {
		TEXT("frame-reassembly"),
		TEXT("control-frames"),
		TEXT("capability-handshake"),
		TEXT("exclusive-port-claim"),
		TEXT("owned-port-record"),
		// #817. Both are always compiled in and always advertised: a caller has
		// to be able to tell "this bridge cannot do that" from "this bridge can
		// and the facility is switched off", and only the first of those two is
		// grounds for skipping a test.
		TEXT("instance-records"),
		TEXT("requested-port-file"),
		TEXT("param-echo"),
		// #1057: handlerSpecs below.
		TEXT("handler-specs"),
	};
	TArray<TSharedPtr<FJsonValue>> FeatureValues;
	for (const TCHAR* Feature : Features)
	{
		FeatureValues.Add(MakeShared<FJsonValueString>(Feature));
	}
	Payload->SetArrayField(TEXT("features"), FeatureValues);

	// Whether the echo is currently recording, which is a runtime fact and not
	// a capability. A test asserting on forwarded parameters needs both: the
	// feature name says the method exists, this says the answer will be real.
	Payload->SetBoolField(TEXT("paramEcho"), FMCPParamEcho::Get().IsEnabled());

	// The registered action list, from the running binary. This is the only
	// answer to "does the plugin I reached have this method" that a stale DLL
	// cannot fake.
	TArray<FString> Names = HandlerRegistry.GetHandlerNames();
	Names.Sort();
	TArray<TSharedPtr<FJsonValue>> ActionValues;
	ActionValues.Reserve(Names.Num());
	for (const FString& Name : Names)
	{
		ActionValues.Add(MakeShared<FJsonValueString>(Name));
	}
	Payload->SetNumberField(TEXT("actionCount"), Names.Num());
	Payload->SetArrayField(TEXT("actions"), ActionValues);

	// #1057: the declared parameter contract of every handler registered with
	// one. The server's surface for those actions is generated from a recording
	// of this, so a live answer that differs from the recording is drift.
	Payload->SetObjectField(TEXT("handlerSpecs"), HandlerRegistry.BuildHandlerSpecsJson());

	return Payload;
}

FString FMCPBridgeServer::ProcessMessage(const FString& Message)
{
	TSharedPtr<FJsonObject> Request = ParseJsonRpcRequest(Message);
	if (!Request.IsValid())
	{
		return CreateJsonRpcError(nullptr, -32700, TEXT("Parse error"));
	}

	FString Method;
	if (!Request->TryGetStringField(TEXT("method"), Method))
	{
		return CreateJsonRpcError(Request, -32600, TEXT("Invalid Request"));
	}

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Processing method: %s"), *Method);

	TSharedPtr<FJsonObject> Params;
	if (Request->HasField(TEXT("params")))
	{
		TSharedPtr<FJsonValue> ParamsValue = Request->TryGetField(TEXT("params"));
		if (ParamsValue.IsValid() && ParamsValue->Type == EJson::Object)
		{
			Params = ParamsValue->AsObject();
		}
		else
		{
			Params = MakeShared<FJsonObject>();
		}
	}
	else
	{
		Params = MakeShared<FJsonObject>();
	}

	// #817: note what this dispatch was handed, before anything decides what to
	// do with it. Recorded for unknown methods too: a leaked parameter on a
	// method the bridge does not have is still a leaked parameter, and it is
	// the case a stale-plugin test is most likely to hit.
	//
	// The two echo methods are excluded so reading the log does not append to
	// it, which would make a second read return a different answer from the
	// first for reasons that have nothing to do with the call under test.
	if (Method != TEXT("get_param_echo") && Method != TEXT("clear_param_echo"))
	{
		FMCPParamEcho::Get().Record(Method, Params);
	}

	// Served here, on the socket thread, deliberately. Every other method waits
	// on the game thread, so when the game thread is inside a modal dialog, a
	// slow task, or a hang, this is the only question the bridge can still
	// answer - and it is the question worth asking at that moment.
	if (Method == TEXT("get_engine_state"))
	{
		TSharedPtr<FJsonObject> Snapshot = FMCPEngineStatus::Get().Snapshot();
		Snapshot->SetBoolField(TEXT("success"), true);
		Snapshot->SetBoolField(TEXT("servedWithoutGameThread"), true);
		return CreateJsonRpcResponse(Request, MakeShared<FJsonValueObject>(Snapshot));
	}

	// #821: also served here, for the same reason and one more. This is the
	// question a client asks to find out whether the plugin it reached
	// understands the protocol it speaks, and it asks it on connect, which is
	// exactly when the game thread is least likely to answer anything.
	if (Method == TEXT("get_bridge_capabilities"))
	{
		return CreateJsonRpcResponse(Request, MakeShared<FJsonValueObject>(BuildCapabilitiesPayload()));
	}

	// #817: the parameter-name log, and its reset. Served off the game thread
	// for the same reason the handshake is: the assertion that reads it runs
	// straight after the call it is about, and making it queue behind the game
	// thread would let an unrelated slow handler decide whether a leak test
	// passes.
	if (Method == TEXT("get_param_echo"))
	{
		return CreateJsonRpcResponse(Request, MakeShared<FJsonValueObject>(FMCPParamEcho::Get().BuildPayload()));
	}
	if (Method == TEXT("clear_param_echo"))
	{
		FMCPParamEcho::Get().Clear();
		TSharedPtr<FJsonObject> Cleared = MakeShared<FJsonObject>();
		Cleared->SetBoolField(TEXT("success"), true);
		Cleared->SetBoolField(TEXT("servedWithoutGameThread"), true);
		Cleared->SetBoolField(TEXT("enabled"), FMCPParamEcho::Get().IsEnabled());
		return CreateJsonRpcResponse(Request, MakeShared<FJsonValueObject>(Cleared));
	}

	// A method nothing registered is answered as unknown before either gate, so
	// it never reads as a dialog refusal or as the editor still starting up.
	if (!HandlerRegistry.HasHandler(Method))
	{
		return CreateJsonRpcError(Request, -32601, DescribeUnknownMethod(Method));
	}

	// Execute handler on game thread
	FMCPHandlerRegistry::FHandlerFunction Handler = [this, Method](const TSharedPtr<FJsonObject>& HandlerParams) -> TSharedPtr<FJsonValue>
	{
		return HandlerRegistry.ExecuteHandler(Method, HandlerParams);
	};

	// Some handlers (create_cpp_class regenerates IDE project files;
	// long-running compiles) legitimately need minutes. Honor per-handler
	// timeouts registered via FMCPHandlerRegistry::RegisterHandlerWithTimeout.
	const float PerHandlerTimeout = HandlerRegistry.GetHandlerTimeout(Method);

	// These read or answer the dialog that is blocking the engine loop, so they
	// are the handlers that must keep working while one is up. Everything else
	// waits for the core ticker, which a modal loop suspends.
	static const TSet<FString> ModalSafeMethods = {
		TEXT("list_dialogs"),
		TEXT("respond_to_dialog"),
		TEXT("get_dialog_policy"),
		TEXT("set_dialog_policy"),
		TEXT("clear_dialog_policy"),
	};
	const bool bModalSafe = ModalSafeMethods.Contains(Method);

	// THE DIALOG GATE.
	//
	// Every bridge method arrives here, so this is the one place that can make
	// the guarantee: while a modal is up, nothing else runs. Any tool, any
	// flow, any action, whatever raised the dialog.
	//
	// Before this, a call landing during a modal was queued behind a game
	// thread parked in the modal loop and died on the 30 second timeout, and
	// the caller got a timeout that reads like the editor is slow. Nothing
	// obliged the caller to notice the dialog at all, so an agent could
	// timeout, retry, and time out again forever while the answer sat one
	// respond_to_dialog away.
	//
	// Refusing immediately is what makes it deterministic. The refusal names
	// the dialog, quotes it whole, lists its buttons in the dialog's own order
	// and hands back the exact call for each, and it is the identical answer
	// on every method, so the only way forward is through the dialog.
	//
	// It refuses for a window the game thread is PARKED behind, and only that.
	// A prompt the editor raised without AddModalWindow is on screen without
	// holding anything: the ticker runs, the handler would have completed, and
	// refusing it turned a Find results window or an undocked panel into a
	// session-long outage with no button that could clear it (#1118). Such a
	// window is still reported by list_dialogs and still stops a quit; it just
	// stops standing in for a blocked engine.
	if (!bModalSafe)
	{
		FString ModalTitle, ModalMessage;
		TArray<FString> ModalButtons;
		bool bModalBlocksGameThread = false;
		if (FMCPEngineStatus::Get().GetActiveModal(ModalTitle, ModalMessage, ModalButtons, &bModalBlocksGameThread)
			&& bModalBlocksGameThread)
		{
			return CreateJsonRpcResponse(
				Request,
				MakeShared<FJsonValueObject>(
					BuildDialogGateRefusal(Method, ModalTitle, ModalMessage, ModalButtons)));
		}
	}

	FMCPEngineStatus::Get().NoteHandlerBegin(Method);
	TSharedPtr<FJsonValue> Result = GameThreadExecutor.ExecuteOnGameThread(
		Handler,
		Params,
		PerHandlerTimeout > 0.0f ? PerHandlerTimeout : FMCPGameThreadExecutor::DefaultTimeoutSeconds,
		bModalSafe);
	FMCPEngineStatus::Get().NoteHandlerEnd(Method);

	// A bare "Handler execution timed out" tells the caller nothing they can
	// act on. Attach what the engine was doing while the request waited: the
	// dialog blocking the game thread, the slow task and its percentage, or how
	// long the game thread has gone without ticking at all.
	if (Result.IsValid() && Result->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject>& ResultObject = Result->AsObject();
		FString ErrorText;
		if (ResultObject->TryGetStringField(TEXT("error"), ErrorText)
			&& (ErrorText.Contains(TEXT("timed out")) || ErrorText.Contains(TEXT("still initializing"))))
		{
			ResultObject->SetObjectField(TEXT("engineState"), FMCPEngineStatus::Get().Snapshot());
		}
	}

	if (!Result.IsValid())
	{
		// The method was registered, so this is the handler failing to answer,
		// not a method the plugin lacks.
		return CreateJsonRpcError(Request, -32603,
			FString::Printf(TEXT("Internal error: handler '%s' returned no result"), *Method));
	}
	return CreateJsonRpcResponse(Request, Result);
}

FString FMCPBridgeServer::DescribeUnknownMethod(const FString& Method) const
{
	// #233: a stale plugin build can dispatch a method that the TS schema
	// advertises but the C++ side hasn't registered yet. The bare
	// "Unknown method" error gave callers no way to tell that apart from
	// a typo. List a few near-matches so it's obvious when the deployed
	// plugin is behind the schema.
	FString Detail = FString::Printf(TEXT("Unknown method: %s"), *Method);
	const TArray<FString> All = HandlerRegistry.GetHandlerNames();
	TArray<FString> Hints;
	for (const FString& Name : All)
	{
		if (Name.Contains(Method, ESearchCase::IgnoreCase) || Method.Contains(Name, ESearchCase::IgnoreCase))
		{
			Hints.Add(Name);
			if (Hints.Num() >= 5) break;
		}
	}
	if (Hints.Num() == 0 && !All.IsEmpty())
	{
		Detail += FString::Printf(TEXT(" (no near-matches in %d registered handlers - the deployed plugin may be behind the TS schema; try a clean rebuild + redeploy)."), All.Num());
	}
	else if (Hints.Num() > 0)
	{
		Detail += FString::Printf(TEXT(" (did you mean: %s)"), *FString::Join(Hints, TEXT(", ")));
	}
	return Detail;
}

FMCPClientSocket::FMCPClientSocket(FMCPSocketHandle InHandle)
	: Handle(InHandle)
{
}

FMCPClientSocket::~FMCPClientSocket()
{
	Close();
}

void FMCPClientSocket::Close()
{
	if (Handle == MCP_INVALID_SOCKET)
	{
		return;
	}
	MCPSocket::Close(Handle);
	Handle = MCP_INVALID_SOCKET;
}

void FMCPBridgeServer::HandleWebSocketConnection(FMCPSocketHandle ClientSocketFD)
{
	// Destroyed last of the three, so the count the shutdown wait reads drops
	// only after the handle has left the live set and the socket is closed.
	// Everything this thread runs that lives in the module's code pages happens
	// before that point, so ShutdownModule cannot complete and unload the DLL
	// while a frame of it is still on this stack.
	FMCPConnectionRelease Release(*this);

	// The accept loop created this handle and hands it over here. From this
	// line on, Connection is its only owner: it closes exactly once, on the way
	// out of this function, whichever path leaves it.
	FMCPClientSocket Connection(ClientSocketFD);

	// Declared after the socket so it is destroyed before it: the handle must
	// leave the live set while it is still open.
	FMCPConnectionUnlist Unlist(*this, ClientSocketFD);

	// Set TCP_NODELAY on client socket for immediate send
	int32 NoDelay = 1;
	setsockopt(Connection.Get(), IPPROTO_TCP, TCP_NODELAY, (char*)&NoDelay, sizeof(NoDelay));

	// Anything the client pipelined behind its upgrade request. Those bytes
	// arrived on the same read as the header and belong to the frame reader.
	TArray<uint8> PipelinedBytes;

	// Perform WebSocket handshake
	const FString Response = PerformWebSocketHandshake(Connection.Get(), PipelinedBytes);
	if (Response.IsEmpty())
	{
		return;
	}

	// HTTP headers are ASCII and FString is TCHAR, so convert to UTF-8 bytes
	// for the wire. A partial send is a failed handshake, not a success.
	const FTCHARToUTF8 UTF8Response(*Response);
	if (!SendAll(Connection.Get(), (const uint8*)UTF8Response.Get(), UTF8Response.Length()))
	{
		UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Failed to send WebSocket handshake response"));
		return;
	}

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Sent WebSocket handshake response (%d bytes)"), UTF8Response.Length());

	// Process WebSocket messages
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Starting WebSocket message processing"));
	ProcessWebSocketMessages(Connection.Get(), PipelinedBytes);
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] WebSocket message processing ended"));
}

void FMCPBridgeServer::ProcessWebSocketMessages(FMCPSocketHandle ClientSocketFD, TArray<uint8>& InitialBytes)
{
	TArray<uint8> Chunk;
	Chunk.SetNumUninitialized(MCPWebSocketLimits::kRecvChunkBytes);

	// Everything received and not yet consumed by the decoder. A TCP read is a
	// byte-stream event, not a message event: one read can carry half a frame,
	// three frames, or two frames and half of a fourth. This buffer is what
	// makes those all mean the same thing. It starts with whatever the client
	// pipelined behind its upgrade request.
	TArray<uint8> PendingBytes = MoveTemp(InitialBytes);

	// Reassembly state for a fragmented message (a data frame with FIN clear
	// followed by continuation frames).
	TArray<uint8> MessagePayload;
	bool bAssembling = false;

	// True once the connection has ended for a reason of its own: the peer
	// closed, the stream stopped parsing, or the socket failed. False means the
	// loop exited only because the bridge is stopping, which the peer deserves
	// to be told about.
	bool bConnectionFinished = false;

	while (!bShouldStop)
	{
		// Decode before reading. Bytes left over from the previous read may
		// already hold a whole request, and waiting on select first would stall
		// it until the peer happened to send something else.
		bool bDone = false;
		for (;;)
		{
			FMCPWebSocketFrame Frame;
			FString DecodeError;
			uint16 DecodeCloseCode = 1002;
			const EMCPFrameDecode Status = DecodeWebSocketFrame(PendingBytes, Frame, DecodeError, DecodeCloseCode);

			if (Status == EMCPFrameDecode::NeedMoreData)
			{
				break;
			}
			if (Status == EMCPFrameDecode::ProtocolError)
			{
				UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] WebSocket protocol error, closing connection: %s"), *DecodeError);
				SendCloseFrame(ClientSocketFD, DecodeCloseCode, DecodeError);
				bDone = true;
				break;
			}

			// Control frames are answers the protocol owes the peer, not
			// requests. Handing a close frame to the JSON-RPC parser (which is
			// what happened before opcodes were read) replied to "goodbye" with
			// a parse error and left the client waiting for a close that never
			// came, holding a connection thread open for the rest of the
			// session.
			if (Frame.Opcode == EMCPWebSocketOpcode::Close)
			{
				uint16 PeerCode = 1000;
				FString PeerReason;
				if (Frame.Payload.Num() >= 2)
				{
					PeerCode = (uint16)(((uint16)Frame.Payload[0] << 8) | (uint16)Frame.Payload[1]);
					if (Frame.Payload.Num() > 2)
					{
						FUTF8ToTCHAR ReasonText((const char*)Frame.Payload.GetData() + 2, Frame.Payload.Num() - 2);
						PeerReason = FString(ReasonText.Length(), ReasonText.Get());
					}
				}
				UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Client closed the WebSocket (code %u%s%s)"),
					(uint32)PeerCode,
					PeerReason.IsEmpty() ? TEXT("") : TEXT(": "),
					*PeerReason);
				// Echo the code back to finish the handshake, then stop reading.
				SendCloseFrame(ClientSocketFD, PeerCode, TEXT(""));
				bDone = true;
				break;
			}
			if (Frame.Opcode == EMCPWebSocketOpcode::Ping)
			{
				const TArray<uint8> Pong = CreateControlFrame(EMCPWebSocketOpcode::Pong, Frame.Payload);
				if (!SendAll(ClientSocketFD, Pong.GetData(), Pong.Num()))
				{
					bDone = true;
					break;
				}
				continue;
			}
			if (Frame.Opcode == EMCPWebSocketOpcode::Pong)
			{
				continue; // keepalive answer, nothing owed
			}

			if (Frame.Opcode == EMCPWebSocketOpcode::Continuation)
			{
				if (!bAssembling)
				{
					UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Continuation frame with no message in progress"));
					SendCloseFrame(ClientSocketFD, 1002, TEXT("continuation frame with no message in progress"));
					bDone = true;
					break;
				}
				MessagePayload.Append(Frame.Payload);
			}
			else
			{
				if (bAssembling)
				{
					UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] New data frame while a fragmented message was still open"));
					SendCloseFrame(ClientSocketFD, 1002, TEXT("data frame interleaved with an open fragmented message"));
					bDone = true;
					break;
				}
				MessagePayload = MoveTemp(Frame.Payload);
				bAssembling = true;
			}

			if ((int64)MessagePayload.Num() > MCPWebSocketLimits::kMaxWebSocketMessageBytes)
			{
				// Say the number rather than dying quietly: a caller that sends
				// a genuinely enormous payload needs to know it hit a limit and
				// what the limit is, not watch the socket disappear.
				const FString Reason = FString::Printf(
					TEXT("message of %lld bytes exceeds the %lld byte bridge limit"),
					(int64)MessagePayload.Num(), MCPWebSocketLimits::kMaxWebSocketMessageBytes);
				UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] %s"), *Reason);
				SendCloseFrame(ClientSocketFD, 1009, Reason);
				bDone = true;
				break;
			}

			if (!Frame.bFinal)
			{
				continue; // more fragments still to come
			}

			bAssembling = false;
			FString Message;
			if (MessagePayload.Num() > 0)
			{
				FUTF8ToTCHAR Converted((const char*)MessagePayload.GetData(), MessagePayload.Num());
				Message = FString(Converted.Length(), Converted.Get());
			}
			MessagePayload.Reset();

			if (Message.IsEmpty())
			{
				continue;
			}

			const FString Response = ProcessMessage(Message);
			const TArray<uint8> ResponseFrame = CreateWebSocketFrame(Response);
			if (!SendAll(ClientSocketFD, ResponseFrame.GetData(), ResponseFrame.Num()))
			{
				UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Failed to send response frame; closing connection"));
				bDone = true;
				break;
			}
		}

		if (bDone)
		{
			bConnectionFinished = true;
			break;
		}

		fd_set ReadSet;
		FD_ZERO(&ReadSet);
		FD_SET(ClientSocketFD, &ReadSet);

		timeval Timeout;
		Timeout.tv_sec = 1;
		Timeout.tv_usec = 0;

		const int32 SelectResult = select(ClientSocketFD + 1, &ReadSet, nullptr, nullptr, &Timeout);
		if (SelectResult < 0)
		{
			bConnectionFinished = true;
			break;
		}
		if (SelectResult == 0 || !FD_ISSET(ClientSocketFD, &ReadSet))
		{
			continue;
		}

		const int32 BytesReceived = recv(ClientSocketFD, (char*)Chunk.GetData(), MCPWebSocketLimits::kRecvChunkBytes, 0);
		if (BytesReceived <= 0)
		{
			bConnectionFinished = true;
			break;
		}
		PendingBytes.Append(Chunk.GetData(), BytesReceived);

		// A peer that keeps sending without ever completing a frame would grow
		// this buffer without limit. Bound it by the largest single frame the
		// bridge accepts: a full-size payload plus its header. Anything past
		// that is bytes that are not going to become a frame, which is what the
		// reason says.
		if ((int64)PendingBytes.Num() > MCPWebSocketLimits::kMaxPendingReceiveBytes)
		{
			const FString Reason = FString::Printf(
				TEXT("unparsed receive buffer of %lld bytes exceeds the %lld byte bridge limit ")
				TEXT("(a %lld byte message plus its frame header); no complete frame arrived"),
				(int64)PendingBytes.Num(), MCPWebSocketLimits::kMaxPendingReceiveBytes, MCPWebSocketLimits::kMaxWebSocketMessageBytes);
			UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] %s"), *Reason);
			SendCloseFrame(ClientSocketFD, 1009, Reason);
			bConnectionFinished = true;
			break;
		}
	}

	if (!bConnectionFinished)
	{
		// The only way out of the loop that is not the connection's own doing:
		// the bridge is stopping. Tell the client so, instead of leaving it to
		// infer a healthy editor from a severed socket.
		SendCloseFrame(ClientSocketFD, 1001, TEXT("editor is shutting down"));
	}
}
