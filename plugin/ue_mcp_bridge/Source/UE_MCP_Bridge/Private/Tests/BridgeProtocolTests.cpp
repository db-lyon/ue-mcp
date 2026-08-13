#if WITH_DEV_AUTOMATION_TESTS

#include "BridgeParamEcho.h"
#include "BridgeServer.h"
#include "BridgeStateFiles.h"
#include "MCPHandlerRegistration.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"

/**
 * Phase 0 of the multi-editor plan (#817), scripted.
 *
 * The TypeScript tier drives the bridge through a socket and can therefore see
 * responses and nothing else. It cannot see a frame boundary, a file on disk
 * that no handler reads, or a parameter that arrived and was ignored, which is
 * exactly the set of things Phase 0 is about. This module is where those are
 * asserted.
 *
 * Everything here is a pure function over a buffer, a directory, or a
 * standalone object. Nothing binds a port and nothing writes into the project's
 * live Saved/UE_MCP_Bridge directory: a test that clobbered port.json would
 * disconnect the editor it is running inside.
 *
 * Two Phase 0 items are deliberately not here. 0.4 (identity answered while a
 * handler blocks the game thread) and 0.6 (shutdown with a client attached)
 * are both statements about two threads and a live socket, and a test that
 * stood one up inside the editor would be asserting on the editor it is hosted
 * by. Those belong to the live tier in Phase 7.
 */

namespace
{
	/**
	 * One client frame, framed the way a client must: masked, with the mask
	 * applied to the payload.
	 *
	 * Written out longhand rather than reusing the server's framer, because the
	 * server's framer is one of the things under test and a test that encodes
	 * with the same code it decodes with proves only that the code agrees with
	 * itself.
	 */
	TArray<uint8> MakeClientFrame(uint8 Opcode, const TArray<uint8>& Payload, bool bFinal = true)
	{
		TArray<uint8> Frame;
		Frame.Add((uint8)((bFinal ? 0x80 : 0x00) | (Opcode & 0x0F)));

		const int64 Length = Payload.Num();
		if (Length < 126)
		{
			Frame.Add((uint8)(0x80 | (uint8)Length)); // mask bit set
		}
		else if (Length < 65536)
		{
			Frame.Add((uint8)(0x80 | 126));
			Frame.Add((uint8)((Length >> 8) & 0xFF));
			Frame.Add((uint8)(Length & 0xFF));
		}
		else
		{
			Frame.Add((uint8)(0x80 | 127));
			for (int32 Index = 7; Index >= 0; --Index)
			{
				Frame.Add((uint8)((Length >> (Index * 8)) & 0xFF));
			}
		}

		const uint8 MaskKey[4] = { 0x37, 0xFA, 0x21, 0x3D };
		Frame.Append(MaskKey, 4);
		for (int32 Index = 0; Index < Payload.Num(); ++Index)
		{
			Frame.Add(Payload[Index] ^ MaskKey[Index % 4]);
		}
		return Frame;
	}

	TArray<uint8> TextPayload(const FString& Text)
	{
		const FTCHARToUTF8 Utf8(*Text);
		TArray<uint8> Bytes;
		Bytes.Append((const uint8*)Utf8.Get(), Utf8.Length());
		return Bytes;
	}

	FString PayloadAsText(const TArray<uint8>& Payload)
	{
		if (Payload.Num() == 0)
		{
			return FString();
		}
		const FUTF8ToTCHAR Converted((const char*)Payload.GetData(), Payload.Num());
		return FString(Converted.Length(), Converted.Get());
	}

	/** A scratch directory that is not the project's live bridge state. */
	FString MakeScratchDir(const TCHAR* Leaf)
	{
		const FString Dir = FPaths::Combine(
			FPaths::ProjectIntermediateDir(),
			TEXT("UE_MCP_BridgeTests"),
			FString::Printf(TEXT("%s-%s"), Leaf, *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		IFileManager::Get().MakeDirectory(*Dir, /*Tree*/ true);
		return Dir;
	}

	void RemoveScratchDir(const FString& Dir)
	{
		IFileManager::Get().DeleteDirectory(*Dir, /*RequireExists*/ false, /*Tree*/ true);
	}

	FMCPInstanceRecord MakeRecord(uint32 Pid, int32 Port, const TCHAR* State)
	{
		FMCPInstanceRecord Record;
		Record.Pid = Pid;
		Record.Port = Port;
		Record.InstanceId = FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
		Record.ProjectRoot = TEXT("c:/projects/example");
		Record.StartedAtUtc = FDateTime::UtcNow().ToIso8601();
		Record.EngineVersion = TEXT("5.8.0");
		Record.ProtocolVersion = UEMCP_BRIDGE_PROTOCOL_VERSION;
		Record.HandlerApiVersion = UEMCP_BRIDGE_API_VERSION;
		Record.State = State;
		return Record;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
// 0.1 Inbound frame reassembly
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBridgeFrameReassemblyTest,
	"UE.MCP.Bridge.Protocol.FrameReassembly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBridgeFrameReassemblyTest::RunTest(const FString& Parameters)
{
	FMCPWebSocketFrame Frame;
	FString Error;
	uint16 CloseCode = 0;

	// Two requests in one segment. A decoder that handled one frame per read
	// answered the first and silently dropped the second, which reads to the
	// caller as a request the editor never received.
	{
		TArray<uint8> Buffer;
		Buffer.Append(MakeClientFrame(0x1, TextPayload(TEXT("{\"id\":\"one\"}"))));
		Buffer.Append(MakeClientFrame(0x1, TextPayload(TEXT("{\"id\":\"two\"}"))));

		TestEqual(TEXT("first frame decodes"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Buffer, Frame, Error, CloseCode), (int32)EMCPFrameDecode::Decoded);
		TestEqual(TEXT("first payload"), PayloadAsText(Frame.Payload), FString(TEXT("{\"id\":\"one\"}")));

		TestEqual(TEXT("second frame decodes from the same segment"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Buffer, Frame, Error, CloseCode), (int32)EMCPFrameDecode::Decoded);
		TestEqual(TEXT("second payload"), PayloadAsText(Frame.Payload), FString(TEXT("{\"id\":\"two\"}")));

		TestEqual(TEXT("both frames were consumed"), Buffer.Num(), 0);
	}

	// A frame split across reads. Nothing may be consumed until it is whole,
	// or the tail of the message is decoded as the head of the next one.
	{
		const TArray<uint8> Whole = MakeClientFrame(0x1, TextPayload(TEXT("{\"id\":\"split\"}")));
		TArray<uint8> Partial;
		Partial.Append(Whole.GetData(), Whole.Num() - 3);
		const int32 PartialSize = Partial.Num();

		TestEqual(TEXT("a partial frame asks for more data"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Partial, Frame, Error, CloseCode), (int32)EMCPFrameDecode::NeedMoreData);
		TestEqual(TEXT("a partial frame consumes nothing"), Partial.Num(), PartialSize);

		Partial.Append(Whole.GetData() + Whole.Num() - 3, 3);
		TestEqual(TEXT("the completed frame decodes"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Partial, Frame, Error, CloseCode), (int32)EMCPFrameDecode::Decoded);
		TestEqual(TEXT("split payload"), PayloadAsText(Frame.Payload), FString(TEXT("{\"id\":\"split\"}")));
	}

	// 100 KB inbound, which is past 65535 and so needs the 64-bit extended
	// length. Round-tripped byte for byte, not merely accepted.
	{
		TArray<uint8> Large;
		Large.SetNumUninitialized(100 * 1024);
		for (int32 Index = 0; Index < Large.Num(); ++Index)
		{
			Large[Index] = (uint8)(Index % 251);
		}

		TArray<uint8> Buffer = MakeClientFrame(0x1, Large);
		TestEqual(TEXT("a 100 KB frame decodes"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Buffer, Frame, Error, CloseCode), (int32)EMCPFrameDecode::Decoded);
		TestEqual(TEXT("100 KB payload length survives"), Frame.Payload.Num(), Large.Num());
		TestTrue(TEXT("100 KB payload content survives"), Frame.Payload == Large);
	}

	// Fragmentation: a data frame with FIN clear, then a continuation. The
	// reassembly loop in ProcessWebSocketMessages joins these; the decoder's
	// job is to report the two facts that loop needs.
	{
		TArray<uint8> Buffer;
		Buffer.Append(MakeClientFrame(0x1, TextPayload(TEXT("{\"id\":")), /*bFinal*/ false));
		Buffer.Append(MakeClientFrame(0x0, TextPayload(TEXT("\"frag\"}")), /*bFinal*/ true));

		TestEqual(TEXT("first fragment decodes"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Buffer, Frame, Error, CloseCode), (int32)EMCPFrameDecode::Decoded);
		TestFalse(TEXT("first fragment is not final"), Frame.bFinal);
		TestEqual(TEXT("first fragment is a text frame"), (int32)Frame.Opcode, (int32)EMCPWebSocketOpcode::Text);

		TestEqual(TEXT("continuation decodes"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Buffer, Frame, Error, CloseCode), (int32)EMCPFrameDecode::Decoded);
		TestTrue(TEXT("continuation is final"), Frame.bFinal);
		TestEqual(TEXT("continuation carries the continuation opcode"), (int32)Frame.Opcode, (int32)EMCPWebSocketOpcode::Continuation);
	}

	// A close frame is a close frame, not a JSON-RPC parse error. Handing one
	// to the parser replied to "goodbye" with an error and left the peer
	// waiting for a close that never came, holding a thread open.
	{
		TArray<uint8> ClosePayload;
		ClosePayload.Add(0x03);
		ClosePayload.Add(0xE8); // 1000, normal closure
		TArray<uint8> Buffer = MakeClientFrame(0x8, ClosePayload);

		TestEqual(TEXT("a close frame decodes"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Buffer, Frame, Error, CloseCode), (int32)EMCPFrameDecode::Decoded);
		TestEqual(TEXT("a close frame is recognised as one"), (int32)Frame.Opcode, (int32)EMCPWebSocketOpcode::Close);
		TestEqual(TEXT("the peer's status code survives"), (int32)Frame.Payload.Num(), 2);
	}

	// Ping is answered with pong, so ping has to be recognised too.
	{
		TArray<uint8> Buffer = MakeClientFrame(0x9, TextPayload(TEXT("hb")));
		TestEqual(TEXT("a ping decodes"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Buffer, Frame, Error, CloseCode), (int32)EMCPFrameDecode::Decoded);
		TestEqual(TEXT("a ping is recognised as one"), (int32)Frame.Opcode, (int32)EMCPWebSocketOpcode::Ping);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBridgeFrameRefusalTest,
	"UE.MCP.Bridge.Protocol.FrameRefusals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBridgeFrameRefusalTest::RunTest(const FString& Parameters)
{
	FMCPWebSocketFrame Frame;
	FString Error;
	uint16 CloseCode = 0;

	// RFC 6455 section 5.1: a client masks every frame. An unmasked one used to
	// be accepted with its payload read from where the mask key would have been,
	// which puts the very next frame boundary in the wrong place.
	{
		TArray<uint8> Buffer;
		Buffer.Add(0x81);
		Buffer.Add(0x02); // mask bit clear
		Buffer.Add('h');
		Buffer.Add('i');
		TestEqual(TEXT("an unmasked client frame is refused"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Buffer, Frame, Error, CloseCode), (int32)EMCPFrameDecode::ProtocolError);
		TestEqual(TEXT("refused as a framing violation"), (int32)CloseCode, 1002);
	}

	// A reserved bit set means the peer is framing to rules that were never
	// negotiated, so no boundary in the stream can be trusted.
	{
		TArray<uint8> Buffer = MakeClientFrame(0x1, TextPayload(TEXT("x")));
		Buffer[0] |= 0x40; // RSV1
		TestEqual(TEXT("a reserved bit is refused"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Buffer, Frame, Error, CloseCode), (int32)EMCPFrameDecode::ProtocolError);
	}

	// A 64-bit length must accumulate in 64 bits. Folding it into an int32 is
	// what turns a hostile length into a negative count and a read off the end.
	{
		TArray<uint8> Buffer;
		Buffer.Add(0x81);
		Buffer.Add((uint8)(0x80 | 127));
		Buffer.Add(0x80); // high bit set
		for (int32 Index = 0; Index < 7; ++Index)
		{
			Buffer.Add(0x00);
		}
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Buffer.Add(0x00); // mask key
		}
		TestEqual(TEXT("a 64-bit length with the high bit set is refused"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Buffer, Frame, Error, CloseCode), (int32)EMCPFrameDecode::ProtocolError);
	}

	// Too large is reported as too large (1009), never as broken framing, so a
	// caller can tell "you sent more than I hold" from "your stream is corrupt".
	{
		const uint64 TooLarge = (uint64)FMCPBridgeServer::MaxMessageBytes() + 1;
		TArray<uint8> Buffer;
		Buffer.Add(0x81);
		Buffer.Add((uint8)(0x80 | 127));
		for (int32 Index = 7; Index >= 0; --Index)
		{
			Buffer.Add((uint8)((TooLarge >> (Index * 8)) & 0xFF));
		}
		for (int32 Index = 0; Index < 4; ++Index)
		{
			Buffer.Add(0x00); // mask key
		}
		TestEqual(TEXT("an oversized frame is refused"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Buffer, Frame, Error, CloseCode), (int32)EMCPFrameDecode::ProtocolError);
		TestEqual(TEXT("refused as too big, not as bad framing"), (int32)CloseCode, 1009);
	}

	// Control frames carry at most 125 bytes and are never fragmented.
	{
		TArray<uint8> Payload;
		Payload.SetNumZeroed(126);
		TArray<uint8> Buffer = MakeClientFrame(0x9, Payload);
		TestEqual(TEXT("an oversized control frame is refused"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Buffer, Frame, Error, CloseCode), (int32)EMCPFrameDecode::ProtocolError);
	}
	{
		TArray<uint8> Buffer = MakeClientFrame(0x9, TextPayload(TEXT("x")), /*bFinal*/ false);
		TestEqual(TEXT("a fragmented control frame is refused"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Buffer, Frame, Error, CloseCode), (int32)EMCPFrameDecode::ProtocolError);
	}

	// An opcode the bridge does not implement is a stream it cannot follow.
	{
		TArray<uint8> Buffer = MakeClientFrame(0x3, TextPayload(TEXT("x")));
		TestEqual(TEXT("an unsupported opcode is refused"),
			(int32)FMCPBridgeServer::DecodeWebSocketFrame(Buffer, Frame, Error, CloseCode), (int32)EMCPFrameDecode::ProtocolError);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBridgeOutboundFramingTest,
	"UE.MCP.Bridge.Protocol.OutboundFraming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBridgeOutboundFramingTest::RunTest(const FString& Parameters)
{
	// Short: a one-byte length, no mask bit (a server never masks).
	{
		const TArray<uint8> Frame = FMCPBridgeServer::CreateWebSocketFrame(TEXT("hello"));
		TestEqual(TEXT("FIN plus text opcode"), (int32)Frame[0], 0x81);
		TestEqual(TEXT("length in the second byte, unmasked"), (int32)Frame[1], 5);
		TestEqual(TEXT("header plus payload"), Frame.Num(), 7);
	}

	// 16-bit extended length.
	{
		const FString Message = FString::ChrN(1000, TEXT('a'));
		const TArray<uint8> Frame = FMCPBridgeServer::CreateWebSocketFrame(Message);
		TestEqual(TEXT("126 selects the 16-bit length"), (int32)Frame[1], 126);
		const int32 Declared = ((int32)Frame[2] << 8) | (int32)Frame[3];
		TestEqual(TEXT("declared length matches the payload"), Declared, 1000);
		TestEqual(TEXT("frame size"), Frame.Num(), 4 + 1000);
	}

	// 64-bit extended length. #731: shifting an int32 by 32 or more is
	// undefined, and the corrupt 8-byte length it produced made every response
	// at or above 64 KiB unreadable, so the client closed the socket.
	{
		const int32 Length = 70000;
		const FString Message = FString::ChrN(Length, TEXT('b'));
		const TArray<uint8> Frame = FMCPBridgeServer::CreateWebSocketFrame(Message);
		TestEqual(TEXT("127 selects the 64-bit length"), (int32)Frame[1], 127);

		uint64 Declared = 0;
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Declared = (Declared << 8) | (uint64)Frame[2 + Index];
		}
		TestEqual(TEXT("declared length matches the payload"), (int64)Declared, (int64)Length);
		TestEqual(TEXT("frame size"), Frame.Num(), 10 + Length);
	}

	// A close frame carries its status code in the first two payload bytes.
	{
		TArray<uint8> Payload;
		Payload.Add(0x03);
		Payload.Add(0xF1); // 1009
		const TArray<uint8> Frame = FMCPBridgeServer::CreateControlFrame(EMCPWebSocketOpcode::Close, Payload);
		TestEqual(TEXT("FIN plus close opcode"), (int32)Frame[0], 0x88);
		TestEqual(TEXT("payload length"), (int32)Frame[1], 2);
		const int32 Code = ((int32)Frame[2] << 8) | (int32)Frame[3];
		TestEqual(TEXT("status code survives framing"), Code, 1009);
	}

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 0.2 Port derivation, the half of socket exclusivity that is not a syscall
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBridgeDerivedPortTest,
	"UE.MCP.Bridge.Protocol.DerivedPort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBridgeDerivedPortTest::RunTest(const FString& Parameters)
{
	// The client derives the same number from the same path (src/port.ts). If
	// these two drift, a project with no lockfile has its halves on different
	// ports and the failure looks like "no editor is running".
	const FString Path = TEXT("C:/Users/example/Projects/Demo/");
	const int32 First = FMCPBridgeServer::DeriveProjectPort(Path);

	TestTrue(TEXT("derived port is inside the ephemeral range"), First >= 49152 && First <= 65535);
	TestEqual(TEXT("derivation is deterministic"), FMCPBridgeServer::DeriveProjectPort(Path), First);

	// Normalization is what makes the two sides agree despite separators,
	// trailing slashes and drive-letter casing.
	TestEqual(TEXT("backslashes normalize"),
		FMCPBridgeServer::DeriveProjectPort(TEXT("C:\\Users\\example\\Projects\\Demo\\")), First);
	TestEqual(TEXT("a missing trailing slash normalizes"),
		FMCPBridgeServer::DeriveProjectPort(TEXT("C:/Users/example/Projects/Demo")), First);
	TestEqual(TEXT("case normalizes"),
		FMCPBridgeServer::DeriveProjectPort(TEXT("c:/users/example/projects/demo")), First);

	// Different worktrees have to land on different ports, which is the whole
	// reason the port is derived rather than fixed.
	TestTrue(TEXT("a different project derives a different port"),
		FMCPBridgeServer::DeriveProjectPort(TEXT("C:/Users/example/Projects/Other")) != First);

	TestEqual(TEXT("the shared normalizer agrees"),
		FMCPBridgeStateFiles::NormalizeProjectRoot(TEXT("C:\\Users\\Example\\Demo\\")),
		FString(TEXT("c:/users/example/demo")));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 0.3 Instance records
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBridgeInstanceRecordTest,
	"UE.MCP.Bridge.Protocol.InstanceRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBridgeInstanceRecordTest::RunTest(const FString& Parameters)
{
	const FString Dir = MakeScratchDir(TEXT("instances"));

	// Round-trip. Every field a client needs to tell one editor from another
	// has to survive the write.
	const FMCPInstanceRecord Written = MakeRecord(4242, 49500, TEXT("listening"));
	TestTrue(TEXT("a record is written"), FMCPBridgeStateFiles::WriteInstanceRecord(Dir, Written));

	FMCPInstanceRecord ReadBack;
	TestTrue(TEXT("a record is read back"),
		FMCPBridgeStateFiles::ReadInstanceRecord(FMCPBridgeStateFiles::RecordPath(Dir, 4242), ReadBack));
	TestEqual(TEXT("port survives"), ReadBack.Port, Written.Port);
	TestEqual(TEXT("pid survives"), (int32)ReadBack.Pid, (int32)Written.Pid);
	TestEqual(TEXT("instance id survives"), ReadBack.InstanceId, Written.InstanceId);
	TestEqual(TEXT("project root survives"), ReadBack.ProjectRoot, Written.ProjectRoot);
	TestEqual(TEXT("engine version survives"), ReadBack.EngineVersion, Written.EngineVersion);
	TestEqual(TEXT("state survives"), ReadBack.State, Written.State);
	TestEqual(TEXT("protocol version survives"), ReadBack.ProtocolVersion, (int32)UEMCP_BRIDGE_PROTOCOL_VERSION);

	// Two editors of one project write two files, so neither can lose to the
	// other. This is the whole point of the per-pid name.
	const FMCPInstanceRecord Second = MakeRecord(4243, 49501, TEXT("listening"));
	FMCPBridgeStateFiles::WriteInstanceRecord(Dir, Second);
	TestTrue(TEXT("the first record still exists after the second is written"),
		FPaths::FileExists(FMCPBridgeStateFiles::RecordPath(Dir, 4242)));
	TestTrue(TEXT("the second record exists"),
		FPaths::FileExists(FMCPBridgeStateFiles::RecordPath(Dir, 4243)));

	// A process removes its own record and nobody else's. A recycled pid that
	// took the other file away would drop a live editor off the map.
	FMCPBridgeStateFiles::DeleteOwnInstanceRecord(Dir, 4243, TEXT("some-other-instance-id"));
	TestTrue(TEXT("a record with someone else's instance id is left alone"),
		FPaths::FileExists(FMCPBridgeStateFiles::RecordPath(Dir, 4243)));

	FMCPBridgeStateFiles::DeleteOwnInstanceRecord(Dir, 4243, Second.InstanceId);
	TestFalse(TEXT("a record with a matching instance id is removed"),
		FPaths::FileExists(FMCPBridgeStateFiles::RecordPath(Dir, 4243)));

	// A bind-failure record exists to outlive the process that wrote it. Exit
	// runs on the bind-failure path, so an unconditional delete there erased
	// the only explanation for an editor with no bridge.
	const FMCPInstanceRecord Failed = MakeRecord(4244, 0, TEXT("bind-failed"));
	FMCPBridgeStateFiles::WriteInstanceRecord(Dir, Failed);
	FMCPBridgeStateFiles::DeleteOwnInstanceRecord(Dir, 4244, Failed.InstanceId);
	TestTrue(TEXT("a bind-failed record survives its own writer's exit"),
		FPaths::FileExists(FMCPBridgeStateFiles::RecordPath(Dir, 4244)));

	RemoveScratchDir(Dir);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBridgeInstanceReapTest,
	"UE.MCP.Bridge.Protocol.InstanceRecordReaping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBridgeInstanceReapTest::RunTest(const FString& Parameters)
{
	const FString Dir = MakeScratchDir(TEXT("reap"));

	const FMCPInstanceRecord Mine = MakeRecord(5001, 49600, TEXT("listening"));
	const FMCPInstanceRecord Live = MakeRecord(5002, 49601, TEXT("listening"));
	const FMCPInstanceRecord Dead = MakeRecord(5003, 49602, TEXT("listening"));
	const FMCPInstanceRecord Written[] = { Mine, Live, Dead };
	for (const FMCPInstanceRecord& Record : Written)
	{
		FMCPBridgeStateFiles::WriteInstanceRecord(Dir, Record);
	}

	// Liveness is injected so the sweep is testable without standing up a
	// listener. The real predicate is asserted separately below.
	const FString DeadInstanceId = Dead.InstanceId;
	const int32 Removed = FMCPBridgeStateFiles::ReapStaleInstanceRecords(Dir, Mine.InstanceId,
		[&DeadInstanceId](const FMCPInstanceRecord& Record) { return Record.InstanceId != DeadInstanceId; });

	TestEqual(TEXT("exactly the dead record is removed"), Removed, 1);
	TestTrue(TEXT("this instance's own record is never swept"),
		FPaths::FileExists(FMCPBridgeStateFiles::RecordPath(Dir, 5001)));
	TestTrue(TEXT("a live instance's record is kept"),
		FPaths::FileExists(FMCPBridgeStateFiles::RecordPath(Dir, 5002)));
	TestFalse(TEXT("a dead instance's record is gone"),
		FPaths::FileExists(FMCPBridgeStateFiles::RecordPath(Dir, 5003)));

	// Something that is not a record this bridge wrote is left where it is.
	// Proof runs one way: an unreadable file has no owner to check against.
	const FString Foreign = FPaths::Combine(Dir, TEXT("notes.json"));
	FFileHelper::SaveStringToFile(TEXT("{\"unrelated\":true}"), *Foreign);
	FMCPBridgeStateFiles::ReapStaleInstanceRecords(Dir, Mine.InstanceId,
		[](const FMCPInstanceRecord&) { return false; });
	TestTrue(TEXT("a file that is not an instance record is left alone"), FPaths::FileExists(Foreign));

	// The real predicate: a pid that cannot be running is not live.
	FMCPInstanceRecord Impossible = MakeRecord(0, 49603, TEXT("listening"));
	TestFalse(TEXT("a record naming no process is not live"), FMCPBridgeStateFiles::IsInstanceLive(Impossible));

	// This process is running and nothing is listening on a port it never
	// bound, so pid liveness alone must not be enough to call a record live.
	FMCPInstanceRecord RecycledPid = MakeRecord(FPlatformProcess::GetCurrentProcessId(), 1, TEXT("listening"));
	TestFalse(TEXT("a live pid with nothing on its port is not live"),
		FMCPBridgeStateFiles::IsInstanceLive(RecycledPid));

	// A bind-failed record claims no port, so the process being alive is the
	// whole of what it claims and the whole of what can be checked.
	FMCPInstanceRecord FailedHere = MakeRecord(FPlatformProcess::GetCurrentProcessId(), 0, TEXT("bind-failed"));
	TestTrue(TEXT("a bind-failed record of a running process is live"),
		FMCPBridgeStateFiles::IsInstanceLive(FailedHere));

	RemoveScratchDir(Dir);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 0.4 / 0.5 Off-thread identity and the capability list
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBridgeCapabilitiesTest,
	"UE.MCP.Bridge.Protocol.Capabilities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBridgeCapabilitiesTest::RunTest(const FString& Parameters)
{
	// Constructed, never started: registering handlers touches no socket and
	// writes no file, and the payload is built from values snapshotted at
	// construction, which is exactly why it can be answered off the game thread.
	FMCPBridgeServer Server(49700, TEXT("automation test"), /*bPinned*/ false);

	const TSharedPtr<FJsonObject> Payload = Server.BuildCapabilitiesPayload();
	TestTrue(TEXT("a payload is produced"), Payload.IsValid());
	if (!Payload.IsValid())
	{
		return false;
	}

	TestTrue(TEXT("the answer says it needed no game thread"), Payload->GetBoolField(TEXT("servedWithoutGameThread")));

	double Version = 0.0;
	TestTrue(TEXT("the protocol version is reported"), Payload->TryGetNumberField(TEXT("protocolVersion"), Version));
	TestEqual(TEXT("and it is the compiled-in one, not a literal"), (int32)Version, (int32)UEMCP_BRIDGE_PROTOCOL_VERSION);
	TestTrue(TEXT("the handler ABI version is reported"), Payload->TryGetNumberField(TEXT("handlerApiVersion"), Version));
	TestEqual(TEXT("and it is the compiled-in one"), (int32)Version, (int32)UEMCP_BRIDGE_API_VERSION);

	// The stale-DLL tell: a header constant cannot say whether the binary was
	// built from the source on disk, and a compile timestamp can.
	FString BuiltAt;
	TestTrue(TEXT("a compile timestamp is reported"), Payload->TryGetStringField(TEXT("builtAt"), BuiltAt));
	TestTrue(TEXT("the compile timestamp is not empty"), !BuiltAt.IsEmpty());

	FString InstanceId;
	TestTrue(TEXT("the instance identifies itself"), Payload->TryGetStringField(TEXT("instanceId"), InstanceId));
	FGuid ParsedInstanceId;
	TestTrue(TEXT("the instance id is a guid, so a recycled pid cannot impersonate it"),
		FGuid::Parse(InstanceId, ParsedInstanceId));

	const TArray<TSharedPtr<FJsonValue>>* Features = nullptr;
	TestTrue(TEXT("named capabilities are reported"), Payload->TryGetArrayField(TEXT("features"), Features));
	if (Features)
	{
		TSet<FString> Named;
		for (const TSharedPtr<FJsonValue>& Value : *Features)
		{
			Named.Add(Value->AsString());
		}
		// Named rather than "version N or above", so a client can ask about the
		// one thing it needs.
		TestTrue(TEXT("frame reassembly is advertised"), Named.Contains(TEXT("frame-reassembly")));
		TestTrue(TEXT("control frames are advertised"), Named.Contains(TEXT("control-frames")));
		TestTrue(TEXT("the handshake advertises itself"), Named.Contains(TEXT("capability-handshake")));
		TestTrue(TEXT("instance records are advertised"), Named.Contains(TEXT("instance-records")));
		TestTrue(TEXT("the requested-port file is advertised"), Named.Contains(TEXT("requested-port-file")));
		TestTrue(TEXT("the parameter echo is advertised"), Named.Contains(TEXT("param-echo")));
	}

	// The action list from the running binary is the only answer to "does the
	// plugin I reached have this method" that a stale DLL cannot fake.
	double ActionCount = 0.0;
	TestTrue(TEXT("the registered action count is reported"), Payload->TryGetNumberField(TEXT("actionCount"), ActionCount));
	TestTrue(TEXT("the binary registered some actions"), ActionCount > 0);
	const TArray<TSharedPtr<FJsonValue>>* Actions = nullptr;
	TestTrue(TEXT("the registered actions are reported"), Payload->TryGetArrayField(TEXT("actions"), Actions));
	if (Actions)
	{
		TSet<FString> ActionNames;
		for (const TSharedPtr<FJsonValue>& Value : *Actions)
		{
			ActionNames.Add(Value->AsString());
		}
		TestTrue(TEXT("runtime visibility set is advertised"),
			ActionNames.Contains(TEXT("set_runtime_visibility")));
		TestTrue(TEXT("runtime visibility restore is advertised"),
			ActionNames.Contains(TEXT("restore_runtime_visibility")));
	}

	// Advertised whether or not it is recording, so a caller can tell a bridge
	// that cannot from a bridge that is switched off.
	bool bEcho = true;
	TestTrue(TEXT("the echo's runtime state is reported"), Payload->TryGetBoolField(TEXT("paramEcho"), bEcho));

	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 0.7 The requested-port channel
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBridgeRequestedPortTest,
	"UE.MCP.Bridge.Protocol.RequestedPort",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBridgeRequestedPortTest::RunTest(const FString& Parameters)
{
	const FString Dir = MakeScratchDir(TEXT("requested"));
	const FString File = FPaths::Combine(Dir, TEXT("requested.json"));
	const FString Root = TEXT("c:/projects/example");

	auto Write = [&File](const FString& Body)
	{
		FFileHelper::SaveStringToFile(Body, *File);
	};

	FString Detail;

	// Absent is silent. An install that never pinned a port has to resolve
	// byte-identically to how it did before this channel existed, log lines
	// included, so there is nothing to say here.
	TestEqual(TEXT("an absent file yields no port"),
		FMCPBridgeStateFiles::ReadRequestedPort(FPaths::Combine(Dir, TEXT("nothing.json")), Root, Detail), (int32)INDEX_NONE);
	TestTrue(TEXT("and says nothing about it"), Detail.IsEmpty());

	// Present and valid.
	Write(FString::Printf(TEXT("{\"port\":51234,\"projectRoot\":\"%s\",\"source\":\"config\"}"), *Root));
	TestEqual(TEXT("a matching record yields its port"),
		FMCPBridgeStateFiles::ReadRequestedPort(File, Root, Detail), 51234);
	TestTrue(TEXT("a usable record needs no explanation"), Detail.IsEmpty());

	// The root is compared normalized, because the client writes what it has
	// and the two sides spell paths differently.
	Write(TEXT("{\"port\":51234,\"projectRoot\":\"C:\\\\Projects\\\\Example\\\\\"}"));
	TestEqual(TEXT("separators and case do not defeat the root check"),
		FMCPBridgeStateFiles::ReadRequestedPort(File, Root, Detail), 51234);

	// Present and unusable is said out loud. A pin that silently does not take
	// is the failure this channel exists to remove, so every refusal is named.
	Write(FString::Printf(TEXT("{\"port\":70000,\"projectRoot\":\"%s\"}"), *Root));
	TestEqual(TEXT("an out-of-range port is refused"),
		FMCPBridgeStateFiles::ReadRequestedPort(File, Root, Detail), (int32)INDEX_NONE);
	TestTrue(TEXT("and the refusal is explained"), !Detail.IsEmpty());

	Write(TEXT("{\"port\":51234,\"projectRoot\":\"c:/projects/somewhere-else\"}"));
	TestEqual(TEXT("a record written for another project is refused"),
		FMCPBridgeStateFiles::ReadRequestedPort(File, Root, Detail), (int32)INDEX_NONE);
	TestTrue(TEXT("and the refusal is explained"), !Detail.IsEmpty());

	Write(TEXT("{\"port\":51234}"));
	TestEqual(TEXT("a record naming no project is refused"),
		FMCPBridgeStateFiles::ReadRequestedPort(File, Root, Detail), (int32)INDEX_NONE);
	TestTrue(TEXT("and the refusal is explained"), !Detail.IsEmpty());

	Write(FString::Printf(TEXT("{\"projectRoot\":\"%s\"}"), *Root));
	TestEqual(TEXT("a record with no port is refused"),
		FMCPBridgeStateFiles::ReadRequestedPort(File, Root, Detail), (int32)INDEX_NONE);
	TestTrue(TEXT("and the refusal is explained"), !Detail.IsEmpty());

	Write(TEXT("this is not json"));
	TestEqual(TEXT("an unparseable record is refused"),
		FMCPBridgeStateFiles::ReadRequestedPort(File, Root, Detail), (int32)INDEX_NONE);
	TestTrue(TEXT("and the refusal is explained"), !Detail.IsEmpty());

	RemoveScratchDir(Dir);
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 0.8 The parameter echo, which is what 3.3 asserts against
// ─────────────────────────────────────────────────────────────────────────────

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPBridgeParamEchoTest,
	"UE.MCP.Bridge.Protocol.ParamEcho",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBridgeParamEchoTest::RunTest(const FString& Parameters)
{
	// A local recorder, not FMCPParamEcho::Get(). The singleton is what the
	// dispatch path writes to, and a bridge serving a client while this test
	// runs would append its own dispatches to whatever the test had recorded.
	// The assertions here are about the recorder's behaviour, not about which
	// object ProcessMessage reaches for.
	FMCPParamEcho Echo;

	auto MakeParams = [](const TArray<FString>& Names)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		for (const FString& Name : Names)
		{
			Params->SetStringField(Name, TEXT("value"));
		}
		return Params;
	};

	// Off by default, and off means nothing is retained. A test facility that
	// recorded while switched off would be a facility that leaks.
	Echo.SetEnabled(false);
	Echo.Record(TEXT("create_blueprint"), MakeParams({ TEXT("path"), TEXT("editor") }));
	TestEqual(TEXT("nothing is recorded while the echo is off"), Echo.Snapshot().Num(), 0);

	Echo.SetEnabled(true);
	Echo.Clear();

	// The oracle: which keys arrived, sorted so the assertion does not depend
	// on JSON field order.
	Echo.Record(TEXT("create_blueprint"), MakeParams({ TEXT("parentClass"), TEXT("assetPath") }));
	TArray<FMCPParamEchoEntry> Entries = Echo.Snapshot();
	TestEqual(TEXT("one dispatch, one entry"), Entries.Num(), 1);
	if (Entries.Num() == 1)
	{
		TestEqual(TEXT("the method is recorded"), Entries[0].Method, FString(TEXT("create_blueprint")));
		TestEqual(TEXT("both parameter names are recorded"), Entries[0].ParamNames.Num(), 2);
		TestEqual(TEXT("names come back sorted"), Entries[0].ParamNames[0], FString(TEXT("assetPath")));
		TestEqual(TEXT("names come back sorted"), Entries[0].ParamNames[1], FString(TEXT("parentClass")));
	}

	// This is the assertion 3.3 makes: a routing key that reached the bridge is
	// visible, and one that was consumed on the client is not.
	Echo.Clear();
	Echo.Record(TEXT("create_blueprint"), MakeParams({ TEXT("assetPath"), TEXT("editor") }));
	Entries = Echo.Snapshot();
	TestTrue(TEXT("a leaked routing parameter is visible"),
		Entries.Num() == 1 && Entries[0].ParamNames.Contains(TEXT("editor")));

	Echo.Clear();
	Echo.Record(TEXT("create_blueprint"), MakeParams({ TEXT("assetPath") }));
	Entries = Echo.Snapshot();
	TestTrue(TEXT("a consumed routing parameter is absent"),
		Entries.Num() == 1 && !Entries[0].ParamNames.Contains(TEXT("editor")));

	// A call with no parameters is still a call. Recording it is what lets a
	// test tell "nothing was forwarded" from "nothing was dispatched".
	Echo.Clear();
	Echo.Record(TEXT("get_status"), MakeShared<FJsonObject>());
	Entries = Echo.Snapshot();
	TestEqual(TEXT("a parameterless dispatch is still recorded"), Entries.Num(), 1);
	TestEqual(TEXT("with no names"), Entries.Num() == 1 ? Entries[0].ParamNames.Num() : -1, 0);

	// Bounded. A long session must not turn this into an unbounded buffer.
	Echo.Clear();
	for (int32 Index = 0; Index < FMCPParamEcho::MaxEntries + 20; ++Index)
	{
		Echo.Record(FString::Printf(TEXT("method_%d"), Index), MakeShared<FJsonObject>());
	}
	Entries = Echo.Snapshot();
	TestEqual(TEXT("the log is bounded"), Entries.Num(), FMCPParamEcho::MaxEntries);
	TestEqual(TEXT("the oldest entries are the ones dropped"),
		Entries.Num() > 0 ? Entries[0].Method : FString(),
		FString::Printf(TEXT("method_%d"), 20));

	// The payload is what the wire carries.
	const TSharedPtr<FJsonObject> Payload = Echo.BuildPayload();
	TestTrue(TEXT("the payload reports the echo is on"), Payload->GetBoolField(TEXT("enabled")));
	TestTrue(TEXT("the payload needs no game thread"), Payload->GetBoolField(TEXT("servedWithoutGameThread")));
	const TArray<TSharedPtr<FJsonValue>>* PayloadEntries = nullptr;
	TestTrue(TEXT("the payload carries the entries"), Payload->TryGetArrayField(TEXT("entries"), PayloadEntries));
	TestEqual(TEXT("all of them"), PayloadEntries ? PayloadEntries->Num() : -1, FMCPParamEcho::MaxEntries);

	// Switching off empties it, so a facility left on by accident stops holding
	// anything the moment it is turned off.
	Echo.SetEnabled(false);
	TestEqual(TEXT("turning the echo off discards what it held"), Echo.Snapshot().Num(), 0);
	TestFalse(TEXT("and the payload says so"), Echo.BuildPayload()->GetBoolField(TEXT("enabled")));

	// The process-wide recorder is construction-gated and must not be on unless
	// this editor was started with it asked for.
	TestTrue(TEXT("the process recorder matches what was asked for at startup"),
		FMCPParamEcho::Get().IsEnabled() == FMCPParamEcho::ResolveEnabledFromEnvironment());

	return true;
}

#endif
