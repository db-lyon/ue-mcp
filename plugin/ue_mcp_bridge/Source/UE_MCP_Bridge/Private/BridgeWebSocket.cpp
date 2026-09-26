#include "BridgeWebSocket.h"
#include "BridgeServer.h"
#include "UE_MCP_BridgeModule.h"
#include "MCPSocketPlatform.h"
#include "HAL/PlatformTime.h"
#include "Misc/Base64.h"
#include "Misc/SecureHash.h"

// The HTTP upgrade handshake and the RFC 6455 frame codec. The connection loop
// that drives them lives in BridgeServer.cpp.

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

	// Refuse every browser-originated upgrade.
	//
	// The bridge exposes execute_python and every editor mutation, and it
	// authenticates nothing about the caller. Allowing loopback origins meant
	// any page served by any dev server on the machine could scan the port
	// range and drive the editor, because the browser supplies
	// Sec-WebSocket-Key itself and the page never has to see the response to
	// cause the damage.
	//
	// A browser cannot suppress or forge the Origin header on a WebSocket
	// upgrade, so its presence is a reliable "this came from a page". Native
	// clients (the npm client, curl, editor tooling) omit it and are unaffected.
	{
		FString Origin;
		if (FindHeaderValue(Request, TEXT("Origin"), Origin))
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Rejected browser-originated WebSocket upgrade from Origin: %s"), *Origin);
			SendHttpError(ClientSocketFD, 403, TEXT("Forbidden"),
				TEXT("The UE-MCP bridge does not accept upgrades from web pages. Connect from a local process instead."));
			return TEXT("");
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
	const FString AcceptKey = CreateWebSocketAcceptKey(WebSocketKey);

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

	const double Deadline = FPlatformTime::Seconds() + MCPWebSocketLimits::kUpgradeReadTimeoutSeconds;

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
			// A close with NOTHING read is a liveness probe, not a fault. The
			// server's own readiness check opens a TCP socket, sees it accept
			// and destroys it without speaking HTTP, once a second while an
			// editor is starting. Logging that at Warning wrote fifty warnings
			// into the editor log per launch and taught the reader to ignore
			// the category. A PARTIAL read is different: somebody began an
			// upgrade and vanished, which is worth seeing.
			if (Raw.Num() == 0)
			{
				UE_LOG(LogMCPBridge, Verbose, TEXT("[UE-MCP] Port probe connected and closed without an upgrade request"));
			}
			else
			{
				UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Connection closed part way through the upgrade request (%d bytes read)"), Raw.Num());
			}
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

		if (HeaderEnd == INDEX_NONE && Raw.Num() > MCPWebSocketLimits::kMaxUpgradeHeaderBytes)
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Upgrade request headers exceed %d bytes with no terminator; refusing"), MCPWebSocketLimits::kMaxUpgradeHeaderBytes);
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

	// FSHA1 always produces the full 20-byte digest.
	FTCHARToUTF8 UTF8String(*Combined);

	uint8 HashBytes[20] = {};
	FSHA1 Sha1;
	Sha1.Update((const uint8*)UTF8String.Get(), UTF8String.Length());
	Sha1.Final();
	Sha1.GetHash(HashBytes);

	// Base64 encode
	FString AcceptKey = FBase64::Encode(HashBytes, 20);
	return AcceptKey;
}

int64 FMCPBridgeServer::MaxMessageBytes()
{
	return MCPWebSocketLimits::kMaxWebSocketMessageBytes;
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

EMCPFrameDecode FMCPBridgeServer::DecodeWebSocketFrame(TArray<uint8>& Buffer, FMCPWebSocketFrame& OutFrame, FString& OutError, uint16& OutCloseCode)
{
	// Everything below is a framing violation (1002) unless it is specifically
	// a size refusal, which the caller has to report as 1009 for the client to
	// tell "you sent too much" apart from "your framing is wrong".
	OutCloseCode = 1002;

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
	if (!bMasked)
	{
		// RFC 6455 section 5.1: a client must mask every frame it sends, and a
		// server that receives an unmasked one must fail the connection. The bit
		// was read here and then only used to decide whether to skip four bytes,
		// so an unmasked frame was accepted and its payload taken from wherever
		// the mask key would have been. Nothing after that point is trustworthy:
		// the very next frame boundary is already in the wrong place.
		OutError = TEXT("client frame arrived unmasked, which RFC 6455 requires clients never to send");
		return EMCPFrameDecode::ProtocolError;
	}

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

	if (PayloadLen > (uint64)MCPWebSocketLimits::kMaxWebSocketMessageBytes)
	{
		OutError = FString::Printf(
			TEXT("frame payload of %llu bytes exceeds the %lld byte bridge limit"),
			PayloadLen, MCPWebSocketLimits::kMaxWebSocketMessageBytes);
		OutCloseCode = 1009; // message too big, same as the assembled-message bound
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
