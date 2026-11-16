#include "BridgeWebSocket.h"

// The RFC 6455 frame codec: pure functions over byte buffers, with no socket
// and no server state behind them.

int64 MCPFrameCodec::MaxMessageBytes()
{
	return MCPWebSocketLimits::kMaxWebSocketMessageBytes;
}

TArray<uint8> MCPFrameCodec::CreateTextFrame(const FString& Message)
{
	TArray<uint8> Frame;

	FTCHARToUTF8 UTF8String(*Message);
	int32 MessageLen = UTF8String.Length();

	Frame.Add(0x81); // FIN + text frame

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
		// #731: shifting an int32 by 32-56 bits is undefined and corrupted the
		// extended length of every frame of 64 KiB or more.
		const uint64 Length = static_cast<uint64>(MessageLen);
		for (int32 i = 7; i >= 0; --i)
		{
			Frame.Add(static_cast<uint8>((Length >> (i * 8)) & 0xFF));
		}
	}

	Frame.Append((uint8*)UTF8String.Get(), MessageLen);
	return Frame;
}

EMCPFrameDecode MCPFrameCodec::Decode(TArray<uint8>& Buffer, FMCPWebSocketFrame& OutFrame, FString& OutError, uint16& OutCloseCode)
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

	// RFC 6455 section 5.1: a client must mask every frame it sends, and a
	// server that receives an unmasked one must fail the connection.
	const bool bMasked = (SecondByte & 0x80) != 0;
	if (!bMasked)
	{
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
		// Accumulate in 64 bits: a 32-bit accumulator turns a large or hostile
		// length into a negative count and a read off the end of the buffer.
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

	HeaderLen += 4; // masking key

	const int64 TotalLen = HeaderLen + (int64)PayloadLen;
	if (Available < TotalLen)
	{
		return EMCPFrameDecode::NeedMoreData;
	}

	OutFrame.Payload.Reset();
	OutFrame.Payload.Append(Buffer.GetData() + HeaderLen, (int32)PayloadLen);

	const uint8* MaskKey = Buffer.GetData() + HeaderLen - 4;
	for (int32 i = 0; i < OutFrame.Payload.Num(); ++i)
	{
		OutFrame.Payload[i] ^= MaskKey[i % 4];
	}

	Buffer.RemoveAt(0, (int32)TotalLen);
	return EMCPFrameDecode::Decoded;
}

TArray<uint8> MCPFrameCodec::CreateControlFrame(EMCPWebSocketOpcode Opcode, const TArray<uint8>& Payload)
{
	TArray<uint8> Frame;
	Frame.Add((uint8)(0x80 | (uint8)Opcode)); // FIN + opcode
	const int32 Len = FMath::Min(Payload.Num(), 125);
	Frame.Add((uint8)Len);
	Frame.Append(Payload.GetData(), Len);
	return Frame;
}
