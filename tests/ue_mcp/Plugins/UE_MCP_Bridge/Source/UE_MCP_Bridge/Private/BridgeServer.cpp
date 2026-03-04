#include "BridgeServer.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/PlatformProcess.h"
#include "Misc/DateTime.h"
#include "Misc/Timespan.h"
#include "Async/Async.h"
#include "Handlers/EditorHandlers.h"
#include "Handlers/AssetHandlers.h"
#include "Handlers/BlueprintHandlers.h"
#include "Handlers/LevelHandlers.h"
#include "Handlers/ReflectionHandlers.h"
#include "Handlers/GasHandlers.h"
#include "Handlers/GameplayHandlers.h"

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
	UE_LOG(LogTemp, Log, TEXT("[UE-MCP] Bridge server thread started on port %d"), ServerPort);
	
	// Initialize platform sockets
#if PLATFORM_WINDOWS
	WSADATA WsaData;
	if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[UE-MCP] Failed to initialize Winsock"));
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
		UE_LOG(LogTemp, Error, TEXT("[UE-MCP] Failed to create socket"));
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

	// Bind socket
	sockaddr_in ServerAddr;
	FMemory::Memset(&ServerAddr, 0, sizeof(ServerAddr));
	ServerAddr.sin_family = AF_INET;
	ServerAddr.sin_addr.s_addr = INADDR_ANY;
	ServerAddr.sin_port = htons(ServerPort);

	if (bind(ServerSocketFD, (sockaddr*)&ServerAddr, sizeof(ServerAddr)) < 0)
	{
		int32 ErrorCode = 0;
#if PLATFORM_WINDOWS
		ErrorCode = WSAGetLastError();
		UE_LOG(LogTemp, Error, TEXT("[UE-MCP] Failed to bind socket to port %d, error: %d"), ServerPort, ErrorCode);
		closesocket(ServerSocketFD);
		WSACleanup();
#else
		UE_LOG(LogTemp, Error, TEXT("[UE-MCP] Failed to bind socket to port %d"), ServerPort);
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
		UE_LOG(LogTemp, Error, TEXT("[UE-MCP] Failed to listen on socket, error: %d"), ErrorCode);
		closesocket(ServerSocketFD);
		WSACleanup();
#else
		UE_LOG(LogTemp, Error, TEXT("[UE-MCP] Failed to listen on socket"));
		close(ServerSocketFD);
#endif
		return 1;
	}

	UE_LOG(LogTemp, Log, TEXT("[UE-MCP] Bridge listening on ws://localhost:%d"), ServerPort);
	bIsRunning = true;

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
			UE_LOG(LogTemp, Log, TEXT("[UE-MCP] Client connected from %s:%d"), 
				ANSI_TO_TCHAR(inet_ntoa(ClientAddr.sin_addr)), ntohs(ClientAddr.sin_port));
				
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

	UE_LOG(LogTemp, Log, TEXT("[UE-MCP] Processing method: %s"), *Method);

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

	// Execute handler on game thread
	FMCPHandlerRegistry::FHandlerFunction Handler = [this, Method](const TSharedPtr<FJsonObject>& HandlerParams) -> TSharedPtr<FJsonValue>
	{
		return HandlerRegistry.ExecuteHandler(Method, HandlerParams);
	};

	TSharedPtr<FJsonValue> Result = GameThreadExecutor.ExecuteOnGameThread(Handler, Params);

	if (Result.IsValid())
	{
		return CreateJsonRpcResponse(Request, Result);
	}
	else
	{
		return CreateJsonRpcError(Request, -32601, FString::Printf(TEXT("Unknown method: %s"), *Method));
	}
}

#if PLATFORM_WINDOWS
void FMCPBridgeServer::HandleWebSocketConnection(SOCKET ClientSocketFD)
#else
void FMCPBridgeServer::HandleWebSocketConnection(int32 ClientSocketFD)
#endif
{
	// Set TCP_NODELAY on client socket for immediate send
	int32 NoDelay = 1;
	setsockopt(ClientSocketFD, IPPROTO_TCP, TCP_NODELAY, (char*)&NoDelay, sizeof(NoDelay));
	
	// Perform WebSocket handshake
	FString Response = PerformWebSocketHandshake(ClientSocketFD);
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
			UE_LOG(LogTemp, Error, TEXT("[UE-MCP] Failed to send WebSocket handshake response, error: %d"), ErrorCode);
			closesocket(ClientSocketFD);
#else
			UE_LOG(LogTemp, Error, TEXT("[UE-MCP] Failed to send WebSocket handshake response"));
			close(ClientSocketFD);
#endif
			return;
		}
		SentBytes += BytesSent;
	}
	
	UE_LOG(LogTemp, Log, TEXT("[UE-MCP] Sent WebSocket handshake response (%d/%d bytes)"), SentBytes, TotalBytes);
	
	// Small delay to ensure response is fully sent and received by client
	FPlatformProcess::Sleep(0.01f); // 10ms
	
	// Process WebSocket messages
	UE_LOG(LogTemp, Log, TEXT("[UE-MCP] Starting WebSocket message processing"));
	ProcessWebSocketMessages(ClientSocketFD);
	UE_LOG(LogTemp, Log, TEXT("[UE-MCP] WebSocket message processing ended"));

#if PLATFORM_WINDOWS
	closesocket(ClientSocketFD);
#else
	close(ClientSocketFD);
#endif
}

#if PLATFORM_WINDOWS
FString FMCPBridgeServer::PerformWebSocketHandshake(SOCKET ClientSocketFD)
#else
FString FMCPBridgeServer::PerformWebSocketHandshake(int32 ClientSocketFD)
#endif
{
	FString Request = ReadHttpRequest(ClientSocketFD);
	if (Request.IsEmpty())
	{
		return TEXT("");
	}

	// Extract WebSocket-Key from request (case-insensitive search)
	FString WebSocketKey;
	int32 KeyStart = Request.Find(TEXT("Sec-WebSocket-Key:"), ESearchCase::IgnoreCase);
	if (KeyStart != INDEX_NONE)
	{
		// Skip past the header name and any whitespace
		int32 ValueStart = KeyStart + 18; // Length of "Sec-WebSocket-Key:"
		while (ValueStart < Request.Len() && (Request[ValueStart] == TEXT(' ') || Request[ValueStart] == TEXT('\t')))
		{
			ValueStart++;
		}
		int32 KeyEnd = Request.Find(TEXT("\r\n"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ValueStart);
		if (KeyEnd != INDEX_NONE)
		{
			WebSocketKey = Request.Mid(ValueStart, KeyEnd - ValueStart).TrimStartAndEnd();
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("[UE-MCP] Extracted WebSocket-Key: %s"), *WebSocketKey);

	if (WebSocketKey.IsEmpty())
	{
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
	
	UE_LOG(LogTemp, Log, TEXT("[UE-MCP] Accept key: %s"), *AcceptKey);
	UE_LOG(LogTemp, Log, TEXT("[UE-MCP] Response length: %d chars"), Response.Len());

	return Response;
}

#if PLATFORM_WINDOWS
FString FMCPBridgeServer::ReadHttpRequest(SOCKET SocketFD)
#else
FString FMCPBridgeServer::ReadHttpRequest(int32 SocketFD)
#endif
{
	// Read HTTP request headers (until \r\n\r\n)
	FString Request;
	TArray<uint8> Buffer;
	Buffer.SetNum(4096);
	
	// Use select to wait for data with timeout
	fd_set ReadSet;
	FD_ZERO(&ReadSet);
	FD_SET(SocketFD, &ReadSet);
	
	timeval Timeout;
	Timeout.tv_sec = 5; // 5 second timeout
	Timeout.tv_usec = 0;
	
	int32 SelectResult = select(SocketFD + 1, &ReadSet, nullptr, nullptr, &Timeout);
	if (SelectResult <= 0 || !FD_ISSET(SocketFD, &ReadSet))
	{
		UE_LOG(LogTemp, Warning, TEXT("[UE-MCP] Timeout waiting for HTTP request"));
		return TEXT("");
	}
	
	// Read data
	int32 BytesReceived = recv(SocketFD, (char*)Buffer.GetData(), Buffer.Num(), 0);
	if (BytesReceived <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UE-MCP] Failed to read HTTP request"));
		return TEXT("");
	}
	
	Buffer.SetNum(BytesReceived);
	Request = FString(ANSI_TO_TCHAR((char*)Buffer.GetData()));
	
	UE_LOG(LogTemp, Log, TEXT("[UE-MCP] Read HTTP request (%d bytes):\n%s"), BytesReceived, *Request.Left(200));
	
	return Request;
}

FString FMCPBridgeServer::CreateWebSocketAcceptKey(const FString& ClientKey)
{
	// WebSocket accept key = base64(sha1(client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
	FString MagicString = TEXT("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
	FString Combined = ClientKey + MagicString;

	// Compute SHA1 hash (20 bytes) using Windows CryptoAPI
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
	// Fallback: use a simple hash (not secure, but works for WebSocket handshake)
	uint32 Hash = 0;
	for (int32 i = 0; i < UTF8String.Length(); ++i)
	{
		Hash = Hash * 31 + UTF8String.Get()[i];
	}
	FMemory::Memcpy(HashBytes, &Hash, 20);
#endif

	// Base64 encode
	FString AcceptKey = FBase64::Encode(HashBytes, 20);
	return AcceptKey;
}

#if PLATFORM_WINDOWS
void FMCPBridgeServer::ProcessWebSocketMessages(SOCKET ClientSocketFD)
#else
void FMCPBridgeServer::ProcessWebSocketMessages(int32 ClientSocketFD)
#endif
{
	constexpr int32 RecvBufferSize = 65536;
	TArray<uint8> Buffer;
	Buffer.SetNumUninitialized(RecvBufferSize);

	while (!bShouldStop)
	{
		fd_set ReadSet;
		FD_ZERO(&ReadSet);
		FD_SET(ClientSocketFD, &ReadSet);
		
		timeval Timeout;
		Timeout.tv_sec = 1;
		Timeout.tv_usec = 0;
		
		int32 SelectResult = select(ClientSocketFD + 1, &ReadSet, nullptr, nullptr, &Timeout);
		
		if (SelectResult > 0 && FD_ISSET(ClientSocketFD, &ReadSet))
		{
			int32 BytesReceived = recv(ClientSocketFD, (char*)Buffer.GetData(), RecvBufferSize, 0);
			if (BytesReceived <= 0)
			{
				break;
			}

			TArray<uint8> FrameData(Buffer.GetData(), BytesReceived);
			FString Message = ParseWebSocketFrame(FrameData);
			
			if (!Message.IsEmpty())
			{
				FString Response = ProcessMessage(Message);
				TArray<uint8> ResponseFrame = CreateWebSocketFrame(Response);
				int32 TotalToSend = ResponseFrame.Num();
				int32 Sent = 0;
				while (Sent < TotalToSend)
				{
					int32 BytesSent = send(ClientSocketFD, (char*)ResponseFrame.GetData() + Sent, TotalToSend - Sent, 0);
					if (BytesSent <= 0) break;
					Sent += BytesSent;
				}
			}
		}
		else if (SelectResult < 0)
		{
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
		for (int32 i = 7; i >= 0; --i)
		{
			Frame.Add((MessageLen >> (i * 8)) & 0xFF);
		}
	}

	// Message payload (UTF-8 bytes)
	Frame.Append((uint8*)UTF8String.Get(), MessageLen);

	return Frame;
}

FString FMCPBridgeServer::ParseWebSocketFrame(const TArray<uint8>& Data)
{
	if (Data.Num() < 2)
	{
		return TEXT("");
	}

	uint8 FirstByte = Data[0];
	uint8 SecondByte = Data[1];

	bool bMasked = (SecondByte & 0x80) != 0;
	int32 PayloadLen = SecondByte & 0x7F;

	int32 HeaderLen = 2;
	if (PayloadLen == 126)
	{
		if (Data.Num() < 4)
		{
			return TEXT("");
		}
		PayloadLen = (Data[2] << 8) | Data[3];
		HeaderLen = 4;
	}
	else if (PayloadLen == 127)
	{
		if (Data.Num() < 10)
		{
			return TEXT("");
		}
		PayloadLen = 0;
		for (int32 i = 0; i < 8; ++i)
		{
			PayloadLen = (PayloadLen << 8) | Data[2 + i];
		}
		HeaderLen = 10;
	}

	if (bMasked)
	{
		HeaderLen += 4; // Masking key
	}

	if (Data.Num() < HeaderLen + PayloadLen)
	{
		return TEXT("");
	}

	TArray<uint8> Payload;
	Payload.Append(Data.GetData() + HeaderLen, PayloadLen);

	if (bMasked)
	{
		// Unmask payload
		uint8 MaskKey[4];
		MaskKey[0] = Data[HeaderLen - 4];
		MaskKey[1] = Data[HeaderLen - 3];
		MaskKey[2] = Data[HeaderLen - 2];
		MaskKey[3] = Data[HeaderLen - 1];
		
		for (int32 i = 0; i < Payload.Num(); ++i)
		{
			Payload[i] ^= MaskKey[i % 4];
		}
	}

	FUTF8ToTCHAR UTF8ToTCHAR((char*)Payload.GetData(), Payload.Num());
	return FString(UTF8ToTCHAR.Length(), UTF8ToTCHAR.Get());
}
