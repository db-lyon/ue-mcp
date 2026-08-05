#include "BridgeStateFiles.h"
#include "UE_MCP_BridgeModule.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#endif

FString FMCPBridgeStateFiles::NormalizeProjectRoot(const FString& Dir)
{
	FString Norm = Dir;
	Norm.ReplaceInline(TEXT("\\"), TEXT("/"));
	while (Norm.EndsWith(TEXT("/")))
	{
		Norm = Norm.LeftChop(1);
	}
	Norm.ToLowerInline();
	return Norm;
}

FString FMCPBridgeStateFiles::ThisProjectRoot()
{
	return NormalizeProjectRoot(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
}

FString FMCPBridgeStateFiles::StateDir()
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("UE_MCP_Bridge"));
}

FString FMCPBridgeStateFiles::InstancesDir()
{
	return FPaths::Combine(StateDir(), TEXT("instances"));
}

FString FMCPBridgeStateFiles::RequestedPortPath()
{
	return FPaths::Combine(StateDir(), TEXT("requested.json"));
}

bool FMCPBridgeStateFiles::PublishJson(const FString& FilePath, const TSharedPtr<FJsonObject>& Payload)
{
	if (!Payload.IsValid())
	{
		return false;
	}

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), /*Tree*/ true);

	FString Serialized;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Serialized);
	FJsonSerializer::Serialize(Payload.ToSharedRef(), Writer);

	const FString TempPath = FString::Printf(TEXT("%s.%u.tmp"), *FilePath, FPlatformProcess::GetCurrentProcessId());
	if (!FFileHelper::SaveStringToFile(Serialized, *TempPath))
	{
		return false;
	}
	if (!IFileManager::Get().Move(*FilePath, *TempPath, /*Replace*/ true))
	{
		IFileManager::Get().Delete(*TempPath);
		return false;
	}
	return true;
}

TSharedPtr<FJsonObject> FMCPBridgeStateFiles::LoadJson(const FString& FilePath)
{
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *FilePath))
	{
		return nullptr;
	}
	TSharedPtr<FJsonObject> Parsed;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(Reader, Parsed) || !Parsed.IsValid())
	{
		return nullptr;
	}
	return Parsed;
}

FString FMCPBridgeStateFiles::RecordPath(const FString& InInstancesDir, uint32 Pid)
{
	return FPaths::Combine(InInstancesDir, FString::Printf(TEXT("%u.json"), Pid));
}

bool FMCPBridgeStateFiles::WriteInstanceRecord(const FString& InInstancesDir, const FMCPInstanceRecord& Record)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("port"), Record.Port);
	Obj->SetNumberField(TEXT("pid"), (double)Record.Pid);
	Obj->SetStringField(TEXT("instanceId"), Record.InstanceId);
	Obj->SetStringField(TEXT("projectRoot"), Record.ProjectRoot);
	Obj->SetStringField(TEXT("startedAt"), Record.StartedAtUtc);
	Obj->SetStringField(TEXT("engineVersion"), Record.EngineVersion);
	Obj->SetNumberField(TEXT("protocolVersion"), Record.ProtocolVersion);
	Obj->SetNumberField(TEXT("handlerApiVersion"), Record.HandlerApiVersion);
	Obj->SetStringField(TEXT("state"), Record.State);

	const FString FilePath = RecordPath(InInstancesDir, Record.Pid);
	if (!PublishJson(FilePath, Obj))
	{
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Failed to write instance record: %s"), *FilePath);
		return false;
	}
	return true;
}

bool FMCPBridgeStateFiles::ReadInstanceRecord(const FString& FilePath, FMCPInstanceRecord& OutRecord)
{
	const TSharedPtr<FJsonObject> Parsed = LoadJson(FilePath);
	if (!Parsed.IsValid())
	{
		return false;
	}

	double NumberValue = 0.0;
	if (Parsed->TryGetNumberField(TEXT("port"), NumberValue))
	{
		OutRecord.Port = (int32)NumberValue;
	}
	if (Parsed->TryGetNumberField(TEXT("pid"), NumberValue))
	{
		OutRecord.Pid = (uint32)NumberValue;
	}
	if (Parsed->TryGetNumberField(TEXT("protocolVersion"), NumberValue))
	{
		OutRecord.ProtocolVersion = (int32)NumberValue;
	}
	if (Parsed->TryGetNumberField(TEXT("handlerApiVersion"), NumberValue))
	{
		OutRecord.HandlerApiVersion = (int32)NumberValue;
	}

	Parsed->TryGetStringField(TEXT("instanceId"), OutRecord.InstanceId);
	Parsed->TryGetStringField(TEXT("projectRoot"), OutRecord.ProjectRoot);
	Parsed->TryGetStringField(TEXT("startedAt"), OutRecord.StartedAtUtc);
	Parsed->TryGetStringField(TEXT("engineVersion"), OutRecord.EngineVersion);
	Parsed->TryGetStringField(TEXT("state"), OutRecord.State);

	// A record with no pid names no process, so nothing about it can be
	// verified and nothing about it can be safely reaped either.
	return OutRecord.Pid > 0;
}

void FMCPBridgeStateFiles::DeleteOwnInstanceRecord(const FString& InInstancesDir, uint32 Pid, const FString& InstanceId)
{
	const FString FilePath = RecordPath(InInstancesDir, Pid);
	if (!FPaths::FileExists(FilePath))
	{
		return;
	}

	FMCPInstanceRecord Existing;
	if (!ReadInstanceRecord(FilePath, Existing))
	{
		// Unreadable and sitting on this process's own filename. Nobody else can
		// own it, and leaving it would make the next boot with this pid look
		// like a stale live editor.
		IFileManager::Get().Delete(*FilePath, /*RequireExists*/ false, /*EvenReadOnly*/ false, /*Quiet*/ true);
		return;
	}

	if (!Existing.InstanceId.IsEmpty() && Existing.InstanceId != InstanceId)
	{
		UE_LOG(LogMCPBridge, Log,
			TEXT("[UE-MCP] Leaving instance record %s alone: it belongs to instance %s, not %s."),
			*FilePath, *Existing.InstanceId, *InstanceId);
		return;
	}

	if (Existing.State == TEXT("bind-failed"))
	{
		// The whole point of that record is to outlive the process that wrote
		// it. Removing it here would delete the answer to the question the user
		// is about to ask.
		UE_LOG(LogMCPBridge, Log,
			TEXT("[UE-MCP] Keeping the bind-failed instance record at %s so the failure stays diagnosable."),
			*FilePath);
		return;
	}

	if (IFileManager::Get().Delete(*FilePath))
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Instance record removed: %s"), *FilePath);
	}
}

bool FMCPBridgeStateFiles::IsPortAccepting(int32 Port, int32 TimeoutMilliseconds)
{
	if (Port <= 0 || Port > 65535)
	{
		return false;
	}

#if PLATFORM_WINDOWS
	// Refcounted per process, so asking again from here is safe even though the
	// server thread has already asked.
	WSADATA WsaData;
	if (WSAStartup(MAKEWORD(2, 2), &WsaData) != 0)
	{
		return false;
	}
	SOCKET Sock = socket(AF_INET, SOCK_STREAM, 0);
	if (Sock == INVALID_SOCKET)
	{
		WSACleanup();
		return false;
	}
	u_long NonBlocking = 1;
	ioctlsocket(Sock, FIONBIO, &NonBlocking);
#else
	int32 Sock = socket(AF_INET, SOCK_STREAM, 0);
	if (Sock < 0)
	{
		return false;
	}
	const int32 Flags = fcntl(Sock, F_GETFL, 0);
	fcntl(Sock, F_SETFL, Flags | O_NONBLOCK);
#endif

	sockaddr_in Addr;
	FMemory::Memset(&Addr, 0, sizeof(Addr));
	Addr.sin_family = AF_INET;
	Addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	Addr.sin_port = htons((uint16)Port);

	bool bAccepting = false;
	const int32 ConnectResult = connect(Sock, (sockaddr*)&Addr, sizeof(Addr));
	if (ConnectResult == 0)
	{
		bAccepting = true;
	}
	else
	{
#if PLATFORM_WINDOWS
		const bool bInProgress = WSAGetLastError() == WSAEWOULDBLOCK;
#else
		const bool bInProgress = errno == EINPROGRESS;
#endif
		if (bInProgress)
		{
			fd_set WriteSet;
			FD_ZERO(&WriteSet);
			FD_SET(Sock, &WriteSet);

			timeval Timeout;
			Timeout.tv_sec = TimeoutMilliseconds / 1000;
			Timeout.tv_usec = (TimeoutMilliseconds % 1000) * 1000;

			const int32 SelectResult = select((int32)(Sock + 1), nullptr, &WriteSet, nullptr, &Timeout);
			if (SelectResult > 0)
			{
				// Writable covers both "connected" and "refused"; the pending
				// socket error is the only thing that tells them apart.
				int32 SocketError = 0;
				socklen_t ErrorLen = sizeof(SocketError);
				if (getsockopt(Sock, SOL_SOCKET, SO_ERROR, (char*)&SocketError, &ErrorLen) == 0)
				{
					bAccepting = SocketError == 0;
				}
			}
		}
	}

#if PLATFORM_WINDOWS
	closesocket(Sock);
	WSACleanup();
#else
	close(Sock);
#endif
	return bAccepting;
}

bool FMCPBridgeStateFiles::IsInstanceLive(const FMCPInstanceRecord& Record)
{
	if (Record.Pid == 0)
	{
		return false;
	}
	if (!FPlatformProcess::IsApplicationRunning(Record.Pid))
	{
		return false;
	}
	if (Record.State == TEXT("bind-failed") || Record.Port <= 0)
	{
		// Nothing bound, so there is no address to test. The process being alive
		// is the whole of what this record claims.
		return true;
	}
	// The pid could have been recycled onto an unrelated process, in which case
	// nothing is listening where the record says.
	return IsPortAccepting(Record.Port, /*TimeoutMilliseconds*/ 250);
}

int32 FMCPBridgeStateFiles::ReapStaleInstanceRecords(const FString& InInstancesDir, const FString& OwnInstanceId)
{
	return ReapStaleInstanceRecords(InInstancesDir, OwnInstanceId,
		[](const FMCPInstanceRecord& Record) { return FMCPBridgeStateFiles::IsInstanceLive(Record); });
}

int32 FMCPBridgeStateFiles::ReapStaleInstanceRecords(
	const FString& InInstancesDir,
	const FString& OwnInstanceId,
	TFunctionRef<bool(const FMCPInstanceRecord&)> IsLive)
{
	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(FileNames, *FPaths::Combine(InInstancesDir, TEXT("*.json")), /*Files*/ true, /*Directories*/ false);

	int32 Removed = 0;
	for (const FString& FileName : FileNames)
	{
		const FString FilePath = FPaths::Combine(InInstancesDir, FileName);

		FMCPInstanceRecord Record;
		if (!ReadInstanceRecord(FilePath, Record))
		{
			// Not a record this bridge wrote, or corrupt. Either way there is no
			// owner to check against, so leave it where it is and say so once.
			UE_LOG(LogMCPBridge, Verbose,
				TEXT("[UE-MCP] Ignoring unreadable instance record %s during the staleness sweep."), *FilePath);
			continue;
		}

		if (!Record.InstanceId.IsEmpty() && Record.InstanceId == OwnInstanceId)
		{
			continue;
		}

		if (IsLive(Record))
		{
			continue;
		}

		if (IFileManager::Get().Delete(*FilePath, /*RequireExists*/ false, /*EvenReadOnly*/ false, /*Quiet*/ true))
		{
			++Removed;
			UE_LOG(LogMCPBridge, Log,
				TEXT("[UE-MCP] Removed the stale instance record for pid %u (port %d): that process is gone."),
				Record.Pid, Record.Port);
		}
	}

	return Removed;
}

int32 FMCPBridgeStateFiles::ReadRequestedPort(const FString& FilePath, const FString& NormalizedProjectRoot, FString& OutDetail)
{
	OutDetail.Reset();

	if (FilePath.IsEmpty() || !FPaths::FileExists(FilePath))
	{
		// No pin was ever published for this project. Say nothing: an install
		// that never used this channel has to resolve its port exactly as it did
		// before the channel existed, log lines included.
		return INDEX_NONE;
	}

	const TSharedPtr<FJsonObject> Parsed = LoadJson(FilePath);
	if (!Parsed.IsValid())
	{
		OutDetail = FString::Printf(TEXT("%s is not readable JSON, so the port it asks for was ignored."), *FilePath);
		return INDEX_NONE;
	}

	double PortValue = 0.0;
	if (!Parsed->TryGetNumberField(TEXT("port"), PortValue))
	{
		OutDetail = FString::Printf(TEXT("%s has no numeric 'port', so it was ignored."), *FilePath);
		return INDEX_NONE;
	}

	const int32 Port = (int32)PortValue;
	if (Port < 1 || Port > 65535)
	{
		OutDetail = FString::Printf(TEXT("%s asks for port %d, outside the range 1-65535, so it was ignored."), *FilePath, Port);
		return INDEX_NONE;
	}

	FString RecordedRoot;
	if (!Parsed->TryGetStringField(TEXT("projectRoot"), RecordedRoot) || RecordedRoot.IsEmpty())
	{
		OutDetail = FString::Printf(
			TEXT("%s names no project root, so there is no way to tell whether it was written for this project. Ignoring it."),
			*FilePath);
		return INDEX_NONE;
	}

	if (NormalizeProjectRoot(RecordedRoot) != NormalizedProjectRoot)
	{
		// A Saved directory that was copied between checkouts carries the
		// original's pin. Honouring it would aim two projects at one port.
		OutDetail = FString::Printf(
			TEXT("%s was written for project root '%s', not '%s', so the port it asks for was ignored."),
			*FilePath, *RecordedRoot, *NormalizedProjectRoot);
		return INDEX_NONE;
	}

	return Port;
}
