#include "BridgeServer.h"
#include "UE_MCP_BridgeModule.h"
#include "MCPEngineStatus.h"
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
#include "Misc/Timespan.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "Async/Async.h"
#include "Handlers/EditorHandlers.h"
#include "Handlers/AssetHandlers.h"
#include "Handlers/BlueprintHandlers.h"
#include "Handlers/ProjectHandlers.h"
#include "Handlers/LevelHandlers.h"
#include "Handlers/ReflectionHandlers.h"
#include "Handlers/GasHandlers.h"
#include "Handlers/GameplayHandlers.h"
#include "Handlers/DialogHandlers.h"
#include "Handlers/MaterialHandlers.h"
#include "Handlers/AnimationHandlers.h"
#include "Handlers/AudioHandlers.h"
#include "Handlers/WidgetHandlers.h"
#include "Handlers/FoliageHandlers.h"
#include "Handlers/LandscapeHandlers.h"
#include "Handlers/NetworkingHandlers.h"
#include "Handlers/NiagaraHandlers.h"
#include "Handlers/PCGHandlers.h"
#include "Handlers/SequencerHandlers.h"
#include "Handlers/SplineHandlers.h"
#include "Handlers/PhysicsHandlers.h"
#include "Handlers/DemoHandlers.h"
#include "Handlers/StateTreeHandlers.h"
#include "Handlers/ChooserHandlers.h"
#include "Handlers/EpicHandlers.h"
#include "Handlers/FabHandlers.h"
#include "Handlers/LockHandlers.h"
#include "Handlers/DiffHandlers.h"

// Platform-specific socket includes
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include "Windows/HideWindowsPlatformTypes.h"
#pragma comment(lib, "ws2_32.lib")
#elif PLATFORM_LINUX || PLATFORM_MAC
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>
#endif

#include "Misc/Base64.h"
#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <wincrypt.h>
#include "Windows/HideWindowsPlatformTypes.h"
#pragma comment(lib, "advapi32.lib")
#endif

namespace
{
	// #821: a JSON-RPC message can span many TCP reads and many WebSocket
	// frames, so the reader accumulates. These are the bounds on how much it
	// will hold for one connection before it refuses and says why, instead of
	// growing without limit on a corrupt or hostile length field.
	constexpr int64 kMaxWebSocketMessageBytes = 64ll * 1024ll * 1024ll; // 64 MiB
	constexpr int32 kRecvChunkBytes = 65536;

	// The upgrade request is read to its terminator rather than in one recv, so
	// it needs its own bounds: how long the whole read may take, and how large
	// the headers may grow before the bridge stops waiting for a blank line.
	constexpr double kUpgradeReadTimeoutSeconds = 5.0;
	constexpr int32 kMaxUpgradeHeaderBytes = 16 * 1024;
}

FMCPBridgeServer::FMCPBridgeServer(int32 Port)
	: ServerPort(Port)
	, ServerThread(nullptr)
	, bShouldStop(false)
	, bIsRunning(false)
	, ServerSocket(nullptr)
{
	// Register core handlers
	FEditorHandlers::RegisterHandlers(HandlerRegistry);
	FAssetHandlers::RegisterHandlers(HandlerRegistry);
	FBlueprintHandlers::RegisterHandlers(HandlerRegistry);
	FLevelHandlers::RegisterHandlers(HandlerRegistry);
	FReflectionHandlers::RegisterHandlers(HandlerRegistry);
	FGasHandlers::RegisterHandlers(HandlerRegistry);
	FGameplayHandlers::RegisterHandlers(HandlerRegistry);
	FDialogHandlers::RegisterHandlers(HandlerRegistry);
	FMaterialHandlers::RegisterHandlers(HandlerRegistry);
	FAnimationHandlers::RegisterHandlers(HandlerRegistry);
	FAudioHandlers::RegisterHandlers(HandlerRegistry);
	FWidgetHandlers::RegisterHandlers(HandlerRegistry);
	FFoliageHandlers::RegisterHandlers(HandlerRegistry);
	FLandscapeHandlers::RegisterHandlers(HandlerRegistry);
	FNetworkingHandlers::RegisterHandlers(HandlerRegistry);
	FNiagaraHandlers::RegisterHandlers(HandlerRegistry);
	FPCGHandlers::RegisterHandlers(HandlerRegistry);
	FSequencerHandlers::RegisterHandlers(HandlerRegistry);
	FSplineHandlers::RegisterHandlers(HandlerRegistry);
	FPhysicsHandlers::RegisterHandlers(HandlerRegistry);
	FDemoHandlers::RegisterHandlers(HandlerRegistry);
	FProjectHandlers::RegisterHandlers(HandlerRegistry);
	FStateTreeHandlers::RegisterHandlers(HandlerRegistry);
	FChooserHandlers::RegisterHandlers(HandlerRegistry);
	FEpicHandlers::RegisterHandlers(HandlerRegistry);
	FFabHandlers::RegisterHandlers(HandlerRegistry);
	FLockHandlers::RegisterHandlers(HandlerRegistry);
	FDiffHandlers::RegisterHandlers(HandlerRegistry);
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
	if (!bIsRunning)
	{
		return;
	}

	bShouldStop = true;

	if (ServerThread)
	{
		ServerThread->WaitForCompletion();
		delete ServerThread;
		ServerThread = nullptr;
	}

	bIsRunning = false;
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
#if PLATFORM_WINDOWS
	WSADATA WsaData;
	if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
	{
		UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Failed to initialize Winsock"));
		return 1;
	}
#endif

	// Create server socket
#if PLATFORM_WINDOWS
	SOCKET ServerSocketFD = socket(AF_INET, SOCK_STREAM, 0);
	if (ServerSocketFD == INVALID_SOCKET)
#else
	int32 ServerSocketFD = socket(AF_INET, SOCK_STREAM, 0);
	if (ServerSocketFD < 0)
#endif
	{
		UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Failed to create socket"));
#if PLATFORM_WINDOWS
		WSACleanup();
#endif
		return 1;
	}

	// Set socket options
	int32 ReuseAddr = 1;
	setsockopt(ServerSocketFD, SOL_SOCKET, SO_REUSEADDR, (char*)&ReuseAddr, sizeof(ReuseAddr));
	
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
				UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Default port %d in use; bound to %d instead (#492)"), RequestedPort, BoundPort);
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
		closesocket(ServerSocketFD);
		WSACleanup();
#else
		UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Failed to bind to any port in [%d, %d]"), RequestedPort, RequestedPort + kMaxPortProbe);
		close(ServerSocketFD);
#endif
		return 1;
	}

	// Listen
	if (listen(ServerSocketFD, 5) < 0)
	{
		int32 ErrorCode = 0;
#if PLATFORM_WINDOWS
		ErrorCode = WSAGetLastError();
		UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Failed to listen on socket, error: %d"), ErrorCode);
		closesocket(ServerSocketFD);
		WSACleanup();
#else
		UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Failed to listen on socket"));
		close(ServerSocketFD);
#endif
		return 1;
	}

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge listening on ws://127.0.0.1:%d (loopback only)"), ServerPort);
	bIsRunning = true;

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
#if PLATFORM_WINDOWS
		if (SelectResult > 0 && FD_ISSET(ServerSocketFD, &ReadSet))
#else
		if (SelectResult > 0 && FD_ISSET(ServerSocketFD, &ReadSet))
#endif
		{
			sockaddr_in ClientAddr;
			socklen_t ClientAddrLen = sizeof(ClientAddr);
#if PLATFORM_WINDOWS
			SOCKET ClientSocketFD = accept(ServerSocketFD, (sockaddr*)&ClientAddr, &ClientAddrLen);
			if (ClientSocketFD != INVALID_SOCKET)
			{
#else
			int32 ClientSocketFD = accept(ServerSocketFD, (sockaddr*)&ClientAddr, &ClientAddrLen);
			if (ClientSocketFD >= 0)
			{
#endif
			char AddrStr[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &ClientAddr.sin_addr, AddrStr, INET_ADDRSTRLEN);
			UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Client connected from %s:%d"),
				ANSI_TO_TCHAR(AddrStr), ntohs(ClientAddr.sin_port));
				
				// Handle each WebSocket connection in its own thread
				Async(EAsyncExecution::Thread, [this, ClientSocketFD]() {
					HandleWebSocketConnection(ClientSocketFD);
				});
			}
		}
	}

	// Cleanup
#if PLATFORM_WINDOWS
	closesocket(ServerSocketFD);
	WSACleanup();
#else
	close(ServerSocketFD);
#endif

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
	DeletePortLockfile();
}

// #492: per-project port lockfile. Multiple editors can run side-by-side as
// long as each one's npm client can find the right bridge. Publishing the
// bound port in <Project>/Saved/UE_MCP_Bridge/port.json (resolved from the
// .uproject path the client was given) is the cheapest way to do that.
FString FMCPBridgeServer::GetPortLockfilePath()
{
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UE_MCP_Bridge"));
	return FPaths::Combine(Dir, TEXT("port.json"));
}

void FMCPBridgeServer::WritePortLockfile(int32 PortValue)
{
	const FString FilePath = GetPortLockfilePath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), /*Tree*/ true);

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("port"), PortValue);
	Obj->SetNumberField(TEXT("pid"), (double)FPlatformProcess::GetCurrentProcessId());
	Obj->SetStringField(TEXT("startedAt"), FDateTime::UtcNow().ToIso8601());
	Obj->SetNumberField(TEXT("apiVersion"), 1.0);

	FString Serialized;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);

	if (!FFileHelper::SaveStringToFile(Serialized, *FilePath))
	{
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Failed to write port lockfile: %s"), *FilePath);
		return;
	}
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Port lockfile published: %s (port=%d)"), *FilePath, PortValue);
}

void FMCPBridgeServer::DeletePortLockfile()
{
	const FString FilePath = GetPortLockfilePath();
	if (!FPaths::FileExists(FilePath)) return;
	if (IFileManager::Get().Delete(*FilePath))
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Port lockfile removed: %s"), *FilePath);
	}
}

// Deterministic per-worktree base port. MUST match src/port.ts byte-for-byte:
// normalize the project root (forward slashes, no trailing slash, lowercased),
// SHA-1 the UTF-8 bytes, fold the first 4 bytes into the 49152-65535 ephemeral
// range. SHA-1 (not SHA-256) because FSHA1 is available on every platform with
// no extra dependency; the hash only spreads ports, it is not security.
int32 FMCPBridgeServer::DeriveProjectPort(const FString& ProjectRootDir)
{
	FString Norm = ProjectRootDir;
	Norm.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (Norm.EndsWith(TEXT("/")))
	{
		Norm = Norm.LeftChop(1);
	}
	Norm.ToLowerInline();

	FTCHARToUTF8 Utf8(*Norm);
	uint8 Hash[20];
	FSHA1 Sha;
	Sha.Update(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
	Sha.Final();
	Sha.GetHash(Hash);

	const uint32 V = ((uint32)Hash[0] << 24) | ((uint32)Hash[1] << 16) | ((uint32)Hash[2] << 8) | (uint32)Hash[3];
	constexpr uint32 EphemeralBase = 49152;
	constexpr uint32 EphemeralSpan = 65535u - EphemeralBase + 1u; // 16384
	return (int32)(EphemeralBase + (V % EphemeralSpan));
}

int32 FMCPBridgeServer::ResolveConfiguredPort()
{
	// 1. Explicit command-line override: -MCPPort=NNNN
	int32 CmdPort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("MCPPort="), CmdPort) && CmdPort > 0 && CmdPort < 65536)
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Using port %d from -MCPPort command line"), CmdPort);
		return CmdPort;
	}

	// 2. Environment override: UE_MCP_PORT (matches the Node client's env var).
	const FString EnvPort = FPlatformMisc::GetEnvironmentVariable(TEXT("UE_MCP_PORT"));
	if (!EnvPort.IsEmpty())
	{
		const int32 P = FCString::Atoi(*EnvPort);
		if (P > 0 && P < 65536)
		{
			UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Using port %d from UE_MCP_PORT env"), P);
			return P;
		}
	}

	// 3. Deterministic per-worktree port derived from the project root path.
	const FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const int32 Derived = DeriveProjectPort(ProjectRoot);
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Derived per-project port %d from %s"), Derived, *ProjectRoot);
	return Derived;
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

	FMCPEngineStatus::Get().NoteHandlerBegin(Method);
	TSharedPtr<FJsonValue> Result = GameThreadExecutor.ExecuteOnGameThread(
		Handler,
		Params,
		PerHandlerTimeout > 0.0f ? PerHandlerTimeout : 30.0f,
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

	if (Result.IsValid())
	{
		return CreateJsonRpcResponse(Request, Result);
	}
	else
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
		return CreateJsonRpcError(Request, -32601, Detail);
	}
}

void FMCPBridgeServer::HandleWebSocketConnection(FMCPSocketHandle ClientSocketFD)
{
	// Set TCP_NODELAY on client socket for immediate send
	int32 NoDelay = 1;
	setsockopt(ClientSocketFD, IPPROTO_TCP, TCP_NODELAY, (char*)&NoDelay, sizeof(NoDelay));
	
	// Anything the client pipelined behind its upgrade request. Those bytes
	// arrived on the same read as the header and belong to the frame reader.
	TArray<uint8> PipelinedBytes;

	// Perform WebSocket handshake
	FString Response = PerformWebSocketHandshake(ClientSocketFD, PipelinedBytes);
	if (Response.IsEmpty())
	{
#if PLATFORM_WINDOWS
		closesocket(ClientSocketFD);
#else
		close(ClientSocketFD);
#endif
		return;
	}

	// Send handshake response
	// HTTP headers are ASCII, FString uses TCHAR (which is wchar_t on Windows)
	// Convert to UTF-8 bytes for network transmission
	FTCHARToUTF8 UTF8Response(*Response);
	const char* ResponseBytes = (const char*)UTF8Response.Get();
	int32 TotalBytes = UTF8Response.Length();
	
	// Send response - ensure all bytes are sent
	int32 SentBytes = 0;
	while (SentBytes < TotalBytes)
	{
		int32 BytesSent = send(ClientSocketFD, ResponseBytes + SentBytes, TotalBytes - SentBytes, 0);
		if (BytesSent < 0)
		{
			int32 ErrorCode = 0;
#if PLATFORM_WINDOWS
			ErrorCode = WSAGetLastError();
			UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Failed to send WebSocket handshake response, error: %d"), ErrorCode);
			closesocket(ClientSocketFD);
#else
			UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] Failed to send WebSocket handshake response"));
			close(ClientSocketFD);
#endif
			return;
		}
		SentBytes += BytesSent;
	}
	
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Sent WebSocket handshake response (%d/%d bytes)"), SentBytes, TotalBytes);
	
	// Small delay to ensure response is fully sent and received by client
	FPlatformProcess::Sleep(0.01f); // 10ms
	
	// Process WebSocket messages
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Starting WebSocket message processing"));
	ProcessWebSocketMessages(ClientSocketFD, PipelinedBytes);
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] WebSocket message processing ended"));

#if PLATFORM_WINDOWS
	closesocket(ClientSocketFD);
#else
	close(ClientSocketFD);
#endif
}

FString FMCPBridgeServer::PerformWebSocketHandshake(FMCPSocketHandle ClientSocketFD, TArray<uint8>& OutPipelinedBytes)
{
	FString Request;
	if (!ReadHttpRequest(ClientSocketFD, Request, OutPipelinedBytes))
	{
		return TEXT("");
	}

	// Validate the request before honouring it. Answering every request that
	// merely carries a Sec-WebSocket-Key with a 101 means a mistyped path, a
	// POST, or a client speaking an older WebSocket draft all get told the
	// upgrade succeeded and then fail incomprehensibly on the first frame.
	{
		int32 RequestLineEnd = Request.Find(TEXT("\r\n"));
		const FString RequestLine = (RequestLineEnd == INDEX_NONE)
			? Request.TrimStartAndEnd()
			: Request.Left(RequestLineEnd).TrimStartAndEnd();

		if (!RequestLine.StartsWith(TEXT("GET "), ESearchCase::CaseSensitive))
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Rejected non-GET upgrade request: %s"), *RequestLine.Left(80));
			SendHttpError(ClientSocketFD, 405, TEXT("Method Not Allowed"), TEXT("The UE-MCP bridge only accepts GET WebSocket upgrades."));
			return TEXT("");
		}
		if (!RequestLine.EndsWith(TEXT("HTTP/1.1"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Rejected upgrade request with unsupported HTTP version: %s"), *RequestLine.Left(80));
			SendHttpError(ClientSocketFD, 505, TEXT("HTTP Version Not Supported"), TEXT("WebSocket upgrades require HTTP/1.1."));
			return TEXT("");
		}

		FString UpgradeHeader;
		if (!FindHeaderValue(Request, TEXT("Upgrade"), UpgradeHeader) || !UpgradeHeader.Contains(TEXT("websocket"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Rejected request with no WebSocket Upgrade header"));
			SendHttpError(ClientSocketFD, 426, TEXT("Upgrade Required"), TEXT("The UE-MCP bridge speaks WebSocket only."));
			return TEXT("");
		}

		FString ConnectionHeader;
		if (!FindHeaderValue(Request, TEXT("Connection"), ConnectionHeader) || !ConnectionHeader.Contains(TEXT("upgrade"), ESearchCase::IgnoreCase))
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Rejected upgrade request with no 'Connection: Upgrade'"));
			SendHttpError(ClientSocketFD, 400, TEXT("Bad Request"), TEXT("A WebSocket upgrade needs 'Connection: Upgrade'."));
			return TEXT("");
		}

		FString VersionHeader;
		if (!FindHeaderValue(Request, TEXT("Sec-WebSocket-Version"), VersionHeader) || FCString::Atoi(*VersionHeader) != 13)
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Rejected upgrade with Sec-WebSocket-Version '%s' (13 required)"), *VersionHeader);
			SendHttpError(ClientSocketFD, 426, TEXT("Upgrade Required"), TEXT("The UE-MCP bridge speaks WebSocket version 13."));
			return TEXT("");
		}
	}

	// Reject browser-originated upgrades from any origin other than loopback.
	// Browsers always send an Origin header on WebSocket upgrades, so a present
	// Origin that isn't loopback is a cross-site websocket hijacking attempt
	// (a malicious page on the developer's machine reaching the editor bridge).
	// Native clients (Node ws, curl) omit Origin and are allowed.
	{
		int32 OriginStart = Request.Find(TEXT("Origin:"), ESearchCase::IgnoreCase);
		if (OriginStart != INDEX_NONE)
		{
			int32 ValueStart = OriginStart + 7; // strlen("Origin:")
			while (ValueStart < Request.Len() && (Request[ValueStart] == TEXT(' ') || Request[ValueStart] == TEXT('\t')))
			{
				ValueStart++;
			}
			int32 ValueEnd = Request.Find(TEXT("\r\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
			FString Origin = (ValueEnd == INDEX_NONE)
				? Request.Mid(ValueStart).TrimStartAndEnd()
				: Request.Mid(ValueStart, ValueEnd - ValueStart).TrimStartAndEnd();

			const bool bIsLoopback =
				Origin.StartsWith(TEXT("http://localhost"), ESearchCase::IgnoreCase) ||
				Origin.StartsWith(TEXT("https://localhost"), ESearchCase::IgnoreCase) ||
				Origin.StartsWith(TEXT("http://127.0.0.1"), ESearchCase::IgnoreCase) ||
				Origin.StartsWith(TEXT("https://127.0.0.1"), ESearchCase::IgnoreCase) ||
				Origin.StartsWith(TEXT("http://[::1]"), ESearchCase::IgnoreCase) ||
				Origin.StartsWith(TEXT("https://[::1]"), ESearchCase::IgnoreCase);

			if (!bIsLoopback)
			{
				UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Rejected WebSocket upgrade from Origin: %s"), *Origin);
				return TEXT("");
			}
		}
	}

	// Extract WebSocket-Key from request
	FString WebSocketKey;
	FindHeaderValue(Request, TEXT("Sec-WebSocket-Key"), WebSocketKey);

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Extracted WebSocket-Key: %s"), *WebSocketKey);

	TArray<uint8> DecodedKey;
	if (WebSocketKey.IsEmpty() || !FBase64::Decode(WebSocketKey, DecodedKey) || DecodedKey.Num() != 16)
	{
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Rejected upgrade with a missing or malformed Sec-WebSocket-Key"));
		SendHttpError(ClientSocketFD, 400, TEXT("Bad Request"), TEXT("Sec-WebSocket-Key must be 16 base64-encoded bytes."));
		return TEXT("");
	}

	// Create accept key
	FString AcceptKey = CreateWebSocketAcceptKey(WebSocketKey);

	// Build response (WebSocket spec requires exact format)
	// Must be: HTTP/1.1 101 Switching Protocols\r\n
	//          Upgrade: websocket\r\n
	//          Connection: Upgrade\r\n
	//          Sec-WebSocket-Accept: <key>\r\n
	//          \r\n
	FString Response = TEXT("HTTP/1.1 101 Switching Protocols\r\n");
	Response += TEXT("Upgrade: websocket\r\n");
	Response += TEXT("Connection: Upgrade\r\n");
	Response += FString::Printf(TEXT("Sec-WebSocket-Accept: %s\r\n"), *AcceptKey);
	Response += TEXT("\r\n");
	
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Accept key: %s"), *AcceptKey);
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Response length: %d chars"), Response.Len());

	return Response;
}

bool FMCPBridgeServer::FindHeaderValue(const FString& Request, const FString& HeaderName, FString& OutValue)
{
	// Scan line by line rather than searching the whole request for the header
	// name: a value that happens to contain another header's name would
	// otherwise be read as that header.
	TArray<FString> Lines;
	Request.ParseIntoArray(Lines, TEXT("\r\n"), /*InCullEmpty*/ false);
	for (int32 Index = 1; Index < Lines.Num(); ++Index) // line 0 is the request line
	{
		const int32 Colon = Lines[Index].Find(TEXT(":"), ESearchCase::CaseSensitive);
		if (Colon == INDEX_NONE)
		{
			continue;
		}
		if (Lines[Index].Left(Colon).TrimStartAndEnd().Equals(HeaderName, ESearchCase::IgnoreCase))
		{
			OutValue = Lines[Index].Mid(Colon + 1).TrimStartAndEnd();
			return true;
		}
	}
	return false;
}

void FMCPBridgeServer::SendHttpError(FMCPSocketHandle SocketFD, int32 StatusCode, const FString& StatusText, const FString& Detail)
{
	// A rejected upgrade used to be a silent disconnect, which reads to the
	// caller exactly like "no editor is running". Say what was wrong.
	const FString Body = Detail + TEXT("\r\n");
	const FTCHARToUTF8 Utf8Body(*Body);
	const FString Response = FString::Printf(
		TEXT("HTTP/1.1 %d %s\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s"),
		StatusCode, *StatusText, Utf8Body.Length(), *Body);

	const FTCHARToUTF8 Utf8Response(*Response);
	SendAll(SocketFD, (const uint8*)Utf8Response.Get(), Utf8Response.Length());
}

bool FMCPBridgeServer::ReadHttpRequest(FMCPSocketHandle SocketFD, FString& OutRequest, TArray<uint8>& OutPipelinedBytes)
{
	OutRequest.Reset();
	OutPipelinedBytes.Reset();

	TArray<uint8> Raw;
	uint8 Chunk[4096];
	int32 HeaderEnd = INDEX_NONE;

	const double Deadline = FPlatformTime::Seconds() + kUpgradeReadTimeoutSeconds;

	// Read until the blank line that ends the headers. A single recv is not a
	// request: a header split across segments loses Sec-WebSocket-Key, and the
	// connection then drops with nothing said about why.
	while (HeaderEnd == INDEX_NONE)
	{
		const double Remaining = Deadline - FPlatformTime::Seconds();
		if (Remaining <= 0.0)
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Timed out reading the WebSocket upgrade request (%d bytes read)"), Raw.Num());
			return false;
		}

		fd_set ReadSet;
		FD_ZERO(&ReadSet);
		FD_SET(SocketFD, &ReadSet);

		timeval Timeout;
		Timeout.tv_sec = (long)Remaining;
		Timeout.tv_usec = (long)((Remaining - (double)Timeout.tv_sec) * 1000000.0);

		const int32 SelectResult = select(SocketFD + 1, &ReadSet, nullptr, nullptr, &Timeout);
		if (SelectResult <= 0 || !FD_ISSET(SocketFD, &ReadSet))
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Timeout waiting for the WebSocket upgrade request"));
			return false;
		}

		const int32 BytesReceived = recv(SocketFD, (char*)Chunk, (int32)sizeof(Chunk), 0);
		if (BytesReceived <= 0)
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Connection closed before the upgrade request completed (%d bytes read)"), Raw.Num());
			return false;
		}

		// The terminator can straddle two reads, so back up three bytes.
		const int32 SearchFrom = FMath::Max(0, Raw.Num() - 3);
		Raw.Append(Chunk, BytesReceived);

		for (int32 Index = SearchFrom; Index + 3 < Raw.Num(); ++Index)
		{
			if (Raw[Index] == '\r' && Raw[Index + 1] == '\n' && Raw[Index + 2] == '\r' && Raw[Index + 3] == '\n')
			{
				HeaderEnd = Index + 4;
				break;
			}
		}

		if (HeaderEnd == INDEX_NONE && Raw.Num() > kMaxUpgradeHeaderBytes)
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Upgrade request headers exceed %d bytes with no terminator; refusing"), kMaxUpgradeHeaderBytes);
			SendHttpError(SocketFD, 431, TEXT("Request Header Fields Too Large"), TEXT("The upgrade request headers are too large for the UE-MCP bridge."));
			return false;
		}
	}

	// Decode exactly the header bytes. ANSI_TO_TCHAR reads until a NUL, and a
	// socket buffer does not contain one; passing the length is what keeps the
	// conversion inside the buffer.
	const FUTF8ToTCHAR Header((const char*)Raw.GetData(), HeaderEnd);
	OutRequest = FString(Header.Length(), Header.Get());

	// Whatever followed the blank line is the client's first frames, arriving
	// in the same segment as the upgrade. They belong to the frame reader.
	if (Raw.Num() > HeaderEnd)
	{
		OutPipelinedBytes.Append(Raw.GetData() + HeaderEnd, Raw.Num() - HeaderEnd);
	}

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Read HTTP upgrade request (%d header bytes, %d pipelined):\n%s"),
		HeaderEnd, OutPipelinedBytes.Num(), *OutRequest.Left(200));

	return true;
}

FString FMCPBridgeServer::CreateWebSocketAcceptKey(const FString& ClientKey)
{
	// WebSocket accept key = base64(sha1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
	FString MagicString = TEXT("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
	FString Combined = ClientKey + MagicString;

	// Compute SHA1 hash (20 bytes)
	FTCHARToUTF8 UTF8String(*Combined);
	uint8 HashBytes[20];

#if PLATFORM_WINDOWS
	HCRYPTPROV hProv = 0;
	HCRYPTHASH hHash = 0;
	if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT))
	{
		if (CryptCreateHash(hProv, CALG_SHA1, 0, 0, &hHash))
		{
			CryptHashData(hHash, (BYTE*)UTF8String.Get(), UTF8String.Length(), 0);
			DWORD HashLen = 20;
			CryptGetHashParam(hHash, HP_HASHVAL, HashBytes, &HashLen, 0);
			CryptDestroyHash(hHash);
		}
		CryptReleaseContext(hProv, 0);
	}
#else
	// UE's cross-platform SHA1
	FSHA1 Sha1;
	Sha1.Update((const uint8*)UTF8String.Get(), UTF8String.Length());
	Sha1.Final();
	Sha1.GetHash(HashBytes);
#endif

	// Base64 encode
	FString AcceptKey = FBase64::Encode(HashBytes, 20);
	return AcceptKey;
}

void FMCPBridgeServer::ProcessWebSocketMessages(FMCPSocketHandle ClientSocketFD, TArray<uint8>& InitialBytes)
{
	TArray<uint8> Chunk;
	Chunk.SetNumUninitialized(kRecvChunkBytes);

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
			const EMCPFrameDecode Status = DecodeWebSocketFrame(PendingBytes, Frame, DecodeError);

			if (Status == EMCPFrameDecode::NeedMoreData)
			{
				break;
			}
			if (Status == EMCPFrameDecode::ProtocolError)
			{
				UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] WebSocket protocol error, closing connection: %s"), *DecodeError);
				SendCloseFrame(ClientSocketFD, 1002, DecodeError);
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

			if ((int64)MessagePayload.Num() > kMaxWebSocketMessageBytes)
			{
				// Say the number rather than dying quietly: a caller that sends
				// a genuinely enormous payload needs to know it hit a limit and
				// what the limit is, not watch the socket disappear.
				const FString Reason = FString::Printf(
					TEXT("message of %lld bytes exceeds the %lld byte bridge limit"),
					(int64)MessagePayload.Num(), kMaxWebSocketMessageBytes);
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
			break;
		}
		if (SelectResult == 0 || !FD_ISSET(ClientSocketFD, &ReadSet))
		{
			continue;
		}

		const int32 BytesReceived = recv(ClientSocketFD, (char*)Chunk.GetData(), kRecvChunkBytes, 0);
		if (BytesReceived <= 0)
		{
			break;
		}
		PendingBytes.Append(Chunk.GetData(), BytesReceived);

		// A peer that keeps sending without ever completing a frame would grow
		// this buffer without limit. Bound it by the same number a single
		// message is bounded by.
		if ((int64)PendingBytes.Num() > kMaxWebSocketMessageBytes)
		{
			const FString Reason = FString::Printf(
				TEXT("unparsed receive buffer of %lld bytes exceeds the %lld byte bridge limit"),
				(int64)PendingBytes.Num(), kMaxWebSocketMessageBytes);
			UE_LOG(LogMCPBridge, Error, TEXT("[UE-MCP] %s"), *Reason);
			SendCloseFrame(ClientSocketFD, 1009, Reason);
			break;
		}
	}
}

TArray<uint8> FMCPBridgeServer::CreateWebSocketFrame(const FString& Message)
{
	// Simple WebSocket frame creation (text frame, no masking)
	TArray<uint8> Frame;
	
	// Convert to UTF-8 first to get correct byte length
	FTCHARToUTF8 UTF8String(*Message);
	int32 MessageLen = UTF8String.Length();
	
	// Frame header
	uint8 FirstByte = 0x81; // FIN + text frame
	Frame.Add(FirstByte);

	if (MessageLen < 126)
	{
		Frame.Add(MessageLen);
	}
	else if (MessageLen < 65536)
	{
		Frame.Add(126);
		Frame.Add((MessageLen >> 8) & 0xFF);
		Frame.Add(MessageLen & 0xFF);
	}
	else
	{
		Frame.Add(127);
		// #731: MessageLen is int32; shifting it by 32-56 bits is undefined and
		// produced a corrupt 8-byte extended payload length, so the client saw a
		// bogus frame size and closed the socket for any response >= 64 KiB.
		// Widen to uint64 before writing the extended length.
		const uint64 Length = static_cast<uint64>(MessageLen);
		for (int32 i = 7; i >= 0; --i)
		{
			Frame.Add(static_cast<uint8>((Length >> (i * 8)) & 0xFF));
		}
	}

	// Message payload (UTF-8 bytes)
	Frame.Append((uint8*)UTF8String.Get(), MessageLen);

	return Frame;
}

EMCPFrameDecode FMCPBridgeServer::DecodeWebSocketFrame(TArray<uint8>& Buffer, FMCPWebSocketFrame& OutFrame, FString& OutError)
{
	const int64 Available = (int64)Buffer.Num();
	if (Available < 2)
	{
		return EMCPFrameDecode::NeedMoreData;
	}

	const uint8 FirstByte = Buffer[0];
	const uint8 SecondByte = Buffer[1];

	// RSV1-3 only carry meaning once an extension has been negotiated, and the
	// bridge negotiates none. A set bit means the peer is framing to rules we
	// never agreed to, so no boundary in the stream can be trusted.
	if ((FirstByte & 0x70) != 0)
	{
		OutError = TEXT("reserved frame bits set with no negotiated extension");
		return EMCPFrameDecode::ProtocolError;
	}

	OutFrame.bFinal = (FirstByte & 0x80) != 0;

	const uint8 RawOpcode = FirstByte & 0x0F;
	switch (RawOpcode)
	{
	case 0x0: OutFrame.Opcode = EMCPWebSocketOpcode::Continuation; break;
	case 0x1: OutFrame.Opcode = EMCPWebSocketOpcode::Text; break;
	case 0x2: OutFrame.Opcode = EMCPWebSocketOpcode::Binary; break;
	case 0x8: OutFrame.Opcode = EMCPWebSocketOpcode::Close; break;
	case 0x9: OutFrame.Opcode = EMCPWebSocketOpcode::Ping; break;
	case 0xA: OutFrame.Opcode = EMCPWebSocketOpcode::Pong; break;
	default:
		OutError = FString::Printf(TEXT("unsupported WebSocket opcode 0x%X"), (int32)RawOpcode);
		return EMCPFrameDecode::ProtocolError;
	}

	const bool bMasked = (SecondByte & 0x80) != 0;
	uint64 PayloadLen = (uint64)(SecondByte & 0x7F);
	int64 HeaderLen = 2;

	if (PayloadLen == 126)
	{
		if (Available < 4)
		{
			return EMCPFrameDecode::NeedMoreData;
		}
		PayloadLen = ((uint64)Buffer[2] << 8) | (uint64)Buffer[3];
		HeaderLen = 4;
	}
	else if (PayloadLen == 127)
	{
		if (Available < 10)
		{
			return EMCPFrameDecode::NeedMoreData;
		}
		// Accumulate in 64 bits. Folding an 8-byte length into a 32-bit
		// accumulator is what turns a large or hostile length into a negative
		// count and a read that walks off the end of the buffer.
		PayloadLen = 0;
		for (int32 i = 0; i < 8; ++i)
		{
			PayloadLen = (PayloadLen << 8) | (uint64)Buffer[2 + i];
		}
		if ((PayloadLen & 0x8000000000000000ull) != 0)
		{
			OutError = TEXT("64-bit payload length has its high bit set");
			return EMCPFrameDecode::ProtocolError;
		}
		HeaderLen = 10;
	}

	const bool bIsControl = (RawOpcode & 0x08) != 0;
	if (bIsControl)
	{
		// Control frames carry at most 125 bytes and are never fragmented.
		if (PayloadLen > 125)
		{
			OutError = FString::Printf(TEXT("control frame payload of %llu bytes exceeds 125"), PayloadLen);
			return EMCPFrameDecode::ProtocolError;
		}
		if (!OutFrame.bFinal)
		{
			OutError = TEXT("fragmented control frame");
			return EMCPFrameDecode::ProtocolError;
		}
	}

	if (PayloadLen > (uint64)kMaxWebSocketMessageBytes)
	{
		OutError = FString::Printf(
			TEXT("frame payload of %llu bytes exceeds the %lld byte bridge limit"),
			PayloadLen, kMaxWebSocketMessageBytes);
		return EMCPFrameDecode::ProtocolError;
	}

	if (bMasked)
	{
		HeaderLen += 4; // masking key
	}

	const int64 TotalLen = HeaderLen + (int64)PayloadLen;
	if (Available < TotalLen)
	{
		// The rest of this frame is still in flight. Leave every byte in place
		// and let the caller read again.
		return EMCPFrameDecode::NeedMoreData;
	}

	OutFrame.Payload.Reset();
	OutFrame.Payload.Append(Buffer.GetData() + HeaderLen, (int32)PayloadLen);

	if (bMasked)
	{
		const uint8* MaskKey = Buffer.GetData() + HeaderLen - 4;
		for (int32 i = 0; i < OutFrame.Payload.Num(); ++i)
		{
			OutFrame.Payload[i] ^= MaskKey[i % 4];
		}
	}

	Buffer.RemoveAt(0, (int32)TotalLen);
	return EMCPFrameDecode::Decoded;
}

TArray<uint8> FMCPBridgeServer::CreateControlFrame(EMCPWebSocketOpcode Opcode, const TArray<uint8>& Payload)
{
	TArray<uint8> Frame;
	Frame.Add((uint8)(0x80 | (uint8)Opcode)); // FIN + opcode
	const int32 Len = FMath::Min(Payload.Num(), 125);
	Frame.Add((uint8)Len);
	Frame.Append(Payload.GetData(), Len);
	return Frame;
}

void FMCPBridgeServer::SendCloseFrame(FMCPSocketHandle SocketFD, uint16 StatusCode, const FString& Reason)
{
	TArray<uint8> Payload;
	Payload.Add((uint8)((StatusCode >> 8) & 0xFF));
	Payload.Add((uint8)(StatusCode & 0xFF));

	FTCHARToUTF8 Utf8Reason(*Reason);
	const int32 ReasonLen = FMath::Min(Utf8Reason.Length(), 123);
	Payload.Append((const uint8*)Utf8Reason.Get(), ReasonLen);

	const TArray<uint8> Frame = CreateControlFrame(EMCPWebSocketOpcode::Close, Payload);
	SendAll(SocketFD, Frame.GetData(), Frame.Num());
}

bool FMCPBridgeServer::SendAll(FMCPSocketHandle SocketFD, const uint8* Data, int32 NumBytes)
{
	int32 Sent = 0;
	while (Sent < NumBytes)
	{
		const int32 BytesSent = send(SocketFD, (const char*)Data + Sent, NumBytes - Sent, 0);
		if (BytesSent <= 0)
		{
			return false;
		}
		Sent += BytesSent;
	}
	return true;
}
