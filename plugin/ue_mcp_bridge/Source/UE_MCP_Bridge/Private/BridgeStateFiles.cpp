#include "BridgeStateFiles.h"
#include "BridgeServer.h"
#include "MCPHandlerRegistration.h"
#include "Misc/EngineVersion.h"
#include "MCPBridgeStateDir.h"
#include "UE_MCP_BridgeModule.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "MCPSocketPlatform.h"

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
	return UEMCP::BridgeStateDir();
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

	// Refcounted per process, so asking again from here is safe even though the
	// server thread has already asked.
	if (!MCPSocket::Startup())
	{
		return false;
	}
	const FMCPSocketHandle Sock = socket(AF_INET, SOCK_STREAM, 0);
	if (Sock == MCP_INVALID_SOCKET)
	{
		MCPSocket::Cleanup();
		return false;
	}
	MCPSocket::SetNonBlocking(Sock);

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
		const bool bInProgress = MCPSocket::ConnectInProgress();
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

	MCPSocket::Close(Sock);
	MCPSocket::Cleanup();
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

// The port.json lockfile and bind-failure record FMCPBridgeServer publishes.

// #492: per-project port lockfile. Multiple editors can run side-by-side as
// long as each one's npm client can find the right bridge. Publishing the
// bound port in <Project>/Saved/UE_MCP_Bridge/port.json (resolved from the
// .uproject path the client was given) is the cheapest way to do that.
FString FMCPBridgeServer::GetPortLockfilePath()
{
	return FPaths::Combine(FMCPBridgeStateFiles::StateDir(), TEXT("port.json"));
}

FString FMCPBridgeServer::GetBridgeErrorFilePath()
{
	return FPaths::Combine(FMCPBridgeStateFiles::StateDir(), TEXT("bridge-error.json"));
}

namespace
{
	/** Is the instance that published this port.json still there? Asked only of a
	 *  record another instance wrote, before publishing over it. */
	bool PortLockfileOwnerIsLive(const FString& FilePath)
	{
		FMCPInstanceRecord Owner;
		return FMCPBridgeStateFiles::ReadInstanceRecord(FilePath, Owner)
			&& FMCPBridgeStateFiles::IsInstanceLive(Owner);
	}

	/** Read the instanceId out of a record, or empty when there is not one. */
	FString ReadRecordInstanceId(const FString& FilePath)
	{
		const TSharedPtr<FJsonObject> Parsed = FMCPBridgeStateFiles::LoadJson(FilePath);
		if (!Parsed.IsValid())
		{
			return FString();
		}
		FString Value;
		Parsed->TryGetStringField(TEXT("instanceId"), Value);
		return Value;
	}
}

void FMCPBridgeServer::WritePortLockfile(int32 PortValue)
{
	const FString FilePath = GetPortLockfilePath();
	const FString OurId = InstanceId;

	// #817: the write is owner-checked, the same way the delete already was.
	// One project directory has one port.json and two editors of that project
	// have two ports, so the second editor to boot used to publish its own
	// address over a perfectly healthy first editor's, and every client reading
	// the file was silently re-aimed at the newcomer. The newcomer's address is
	// in its own instance record, where it cannot displace anyone.
	{
		const TSharedPtr<FJsonObject> Existing = FMCPBridgeStateFiles::LoadJson(FilePath);
		FString ExistingOwner;
		if (Existing.IsValid() && Existing->TryGetStringField(TEXT("instanceId"), ExistingOwner)
			&& !ExistingOwner.IsEmpty() && ExistingOwner != OurId && PortLockfileOwnerIsLive(FilePath))
		{
			UE_LOG(LogMCPBridge, Warning,
				TEXT("[UE-MCP] Another editor of this project (instance %s) is still listening and owns %s, so this bridge did not publish over it. This bridge is on port %d and its address is in %s. Clients reading port.json will reach the other editor."),
				*ExistingOwner, *FilePath, PortValue, *FMCPBridgeStateFiles::RecordPath(FMCPBridgeStateFiles::InstancesDir(), FPlatformProcess::GetCurrentProcessId()));
			return;
		}
	}

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("port"), PortValue);
	Obj->SetNumberField(TEXT("pid"), (double)FPlatformProcess::GetCurrentProcessId());
	Obj->SetStringField(TEXT("startedAt"), StartedAtUtc.ToIso8601());
	// Who wrote this. A pid is not identity: pids are recycled, and two
	// instances of one project would otherwise be indistinguishable on disk.
	Obj->SetStringField(TEXT("instanceId"), InstanceId);
	Obj->SetStringField(TEXT("status"), TEXT("listening"));
	Obj->SetNumberField(TEXT("protocolVersion"), (double)UEMCP_BRIDGE_PROTOCOL_VERSION);
	Obj->SetNumberField(TEXT("handlerApiVersion"), (double)UEMCP_BRIDGE_API_VERSION);

	if (!FMCPBridgeStateFiles::PublishJson(FilePath, Obj))
	{
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Failed to write port lockfile: %s"), *FilePath);
		return;
	}

	// A previous failed start may have left a bind-failure record. This
	// instance is listening, so that record no longer describes reality.
	IFileManager::Get().Delete(*GetBridgeErrorFilePath(), /*RequireExists*/ false, /*EvenReadOnly*/ false, /*Quiet*/ true);

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Port lockfile published: %s (port=%d, instance=%s)"),
		*FilePath, PortValue, *InstanceId);
}

void FMCPBridgeServer::WriteBindFailureRecord(int32 FirstPort, int32 LastPort, int32 ErrorCode)
{
	// Its own path, never port.json: a failed start must not be able to erase
	// or overwrite the record of an editor that is running perfectly well.
	const FString FilePath = GetBridgeErrorFilePath();

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("status"), TEXT("bind-failed"));
	Obj->SetNumberField(TEXT("pid"), (double)FPlatformProcess::GetCurrentProcessId());
	Obj->SetStringField(TEXT("startedAt"), StartedAtUtc.ToIso8601());
	Obj->SetStringField(TEXT("failedAt"), FDateTime::UtcNow().ToIso8601());
	Obj->SetStringField(TEXT("instanceId"), InstanceId);
	Obj->SetNumberField(TEXT("firstPortTried"), FirstPort);
	Obj->SetNumberField(TEXT("lastPortTried"), LastPort);
	Obj->SetNumberField(TEXT("errorCode"), ErrorCode);
	Obj->SetStringField(TEXT("detail"), FString::Printf(
		TEXT("The editor is running but its MCP bridge could not bind a port in [%d, %d]."), FirstPort, LastPort));

	if (!FMCPBridgeStateFiles::PublishJson(FilePath, Obj))
	{
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Failed to write bridge error record: %s"), *FilePath);
	}

	// #817: and again as this instance's own record, so a failed start is
	// visible in the same place a successful one is. bridge-error.json is one
	// file per project and a second editor's failure would overwrite the first
	// editor's; the per-pid record cannot be overwritten by anyone.
	WriteInstanceRecord(TEXT("bind-failed"), /*PortValue*/ 0);
}

void FMCPBridgeServer::WriteInstanceRecord(const FString& State, int32 PortValue)
{
	FMCPInstanceRecord Record;
	Record.Port = PortValue;
	Record.Pid = FPlatformProcess::GetCurrentProcessId();
	Record.InstanceId = InstanceId;
	Record.ProjectRoot = FMCPBridgeStateFiles::ThisProjectRoot();
	Record.StartedAtUtc = StartedAtUtc.ToIso8601();
	Record.EngineVersion = FEngineVersion::Current().ToString();
	Record.ProtocolVersion = UEMCP_BRIDGE_PROTOCOL_VERSION;
	Record.HandlerApiVersion = UEMCP_BRIDGE_API_VERSION;
	Record.State = State;

	if (FMCPBridgeStateFiles::WriteInstanceRecord(FMCPBridgeStateFiles::InstancesDir(), Record))
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Instance record published: %s (state=%s, port=%d)"),
			*FMCPBridgeStateFiles::RecordPath(FMCPBridgeStateFiles::InstancesDir(), Record.Pid), *State, PortValue);
	}
}

void FMCPBridgeServer::DeleteOwnInstanceRecord()
{
	FMCPBridgeStateFiles::DeleteOwnInstanceRecord(
		FMCPBridgeStateFiles::InstancesDir(),
		FPlatformProcess::GetCurrentProcessId(),
		InstanceId);
}

void FMCPBridgeServer::ReapStaleInstanceRecords()
{
	const int32 Removed = FMCPBridgeStateFiles::ReapStaleInstanceRecords(
		FMCPBridgeStateFiles::InstancesDir(),
		InstanceId);
	if (Removed > 0)
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Removed %d stale bridge instance record(s)."), Removed);
	}
}

void FMCPBridgeServer::DeletePortLockfileIfOwned()
{
	const FString FilePath = GetPortLockfilePath();
	if (!FPaths::FileExists(FilePath))
	{
		return;
	}

	// Only take away a record this instance wrote. Exit() runs on every return
	// from Run(), including the one where the bind failed, so an editor that
	// never listened used to delete a live editor's record on its way out.
	const FString OwnerId = ReadRecordInstanceId(FilePath);
	const FString OurId = InstanceId;
	if (OwnerId != OurId)
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Leaving port lockfile alone: it belongs to instance %s, not %s"),
			OwnerId.IsEmpty() ? TEXT("(unknown)") : *OwnerId, *OurId);
		return;
	}

	if (IFileManager::Get().Delete(*FilePath))
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Port lockfile removed: %s"), *FilePath);
	}
}
