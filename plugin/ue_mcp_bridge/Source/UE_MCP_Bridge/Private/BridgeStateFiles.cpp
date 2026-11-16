#include "BridgeStateFiles.h"
#include "MCPWireContract.h"
#include "Misc/EngineVersion.h"
#include "MCPBridgeStateDir.h"
#include "MCPEngineStatus.h"
#include "UE_MCP_BridgeModule.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Interfaces/IPluginManager.h"

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

FString FMCPBridgeStateFiles::ProjectRootDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
}

FString FMCPBridgeStateFiles::ThisProjectRoot()
{
	return NormalizeProjectRoot(ProjectRootDir());
}

FString FMCPBridgeStateFiles::StateDir()
{
	return UEMCP::BridgeStateDir();
}

FString FMCPBridgeStateFiles::InstancesDir()
{
	return FPaths::Combine(StateDir(), UEMCPWire::InstancesDirName);
}

FString FMCPBridgeStateFiles::RequestedPortPath()
{
	return FPaths::Combine(StateDir(), UEMCPWire::StateFile::RequestedPort);
}

FString FMCPBridgeStateFiles::PortLockfilePath()
{
	return FPaths::Combine(StateDir(), UEMCPWire::StateFile::PortLockfile);
}

FString FMCPBridgeStateFiles::BridgeErrorPath()
{
	return FPaths::Combine(StateDir(), UEMCPWire::StateFile::BridgeError);
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

	return FMCPEngineStatus::PublishAtomically(FilePath, Serialized);
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

FString FMCPBridgeStateFiles::InstallKindFor(const FString& PluginDir)
{
	const TSharedPtr<FJsonObject> Marker = LoadJson(FPaths::Combine(PluginDir, UEMCPWire::InstallMarkerFile));
	FString Kind;
	if (Marker.IsValid() && Marker->TryGetStringField(TEXT("kind"), Kind)
		&& (Kind == UEMCPWire::InstallKind::Source || Kind == UEMCPWire::InstallKind::Binary))
	{
		return Kind;
	}
	return FPaths::DirectoryExists(FPaths::Combine(PluginDir, TEXT("Source"))) ? UEMCPWire::InstallKind::Source : UEMCPWire::InstallKind::Binary;
}

FString FMCPBridgeStateFiles::RecordPath(const FString& InInstancesDir, uint32 Pid)
{
	return FPaths::Combine(InInstancesDir, FString::Printf(TEXT("%u%s"), Pid, UEMCPWire::StateFile::InstanceRecordSuffix));
}

bool FMCPBridgeStateFiles::WriteInstanceRecord(const FString& InInstancesDir, const FMCPInstanceRecord& Record)
{
	namespace F = UEMCPWire::InstanceField;
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(F::Port, Record.Port);
	Obj->SetNumberField(F::Pid, (double)Record.Pid);
	Obj->SetStringField(F::InstanceId, Record.InstanceId);
	Obj->SetStringField(F::ProjectRoot, Record.ProjectRoot);
	Obj->SetStringField(F::StartedAt, Record.StartedAtUtc);
	Obj->SetStringField(F::EngineVersion, Record.EngineVersion);
	Obj->SetNumberField(F::ProtocolVersion, Record.ProtocolVersion);
	Obj->SetNumberField(F::HandlerApiVersion, Record.HandlerApiVersion);
	Obj->SetStringField(F::State, Record.State);
	Obj->SetStringField(F::PluginDir, Record.PluginDir);
	Obj->SetStringField(F::InstallKind, Record.InstallKind);
	Obj->SetStringField(F::PluginVersion, Record.PluginVersion);

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
	namespace F = UEMCPWire::InstanceField;
	const TSharedPtr<FJsonObject> Parsed = LoadJson(FilePath);
	if (!Parsed.IsValid())
	{
		return false;
	}

	double NumberValue = 0.0;
	if (Parsed->TryGetNumberField(F::Port, NumberValue))
	{
		OutRecord.Port = (int32)NumberValue;
	}
	if (Parsed->TryGetNumberField(F::Pid, NumberValue))
	{
		OutRecord.Pid = (uint32)NumberValue;
	}
	if (Parsed->TryGetNumberField(F::ProtocolVersion, NumberValue))
	{
		OutRecord.ProtocolVersion = (int32)NumberValue;
	}
	if (Parsed->TryGetNumberField(F::HandlerApiVersion, NumberValue))
	{
		OutRecord.HandlerApiVersion = (int32)NumberValue;
	}

	Parsed->TryGetStringField(F::InstanceId, OutRecord.InstanceId);
	Parsed->TryGetStringField(F::ProjectRoot, OutRecord.ProjectRoot);
	Parsed->TryGetStringField(F::StartedAt, OutRecord.StartedAtUtc);
	Parsed->TryGetStringField(F::EngineVersion, OutRecord.EngineVersion);
	Parsed->TryGetStringField(F::State, OutRecord.State);
	Parsed->TryGetStringField(F::PluginDir, OutRecord.PluginDir);
	Parsed->TryGetStringField(F::InstallKind, OutRecord.InstallKind);
	Parsed->TryGetStringField(F::PluginVersion, OutRecord.PluginVersion);

	// A record with no pid names no process, so nothing about it can be
	// verified and nothing about it can be safely reaped either.
	return OutRecord.Pid > 0;
}

bool FMCPBridgeStateFiles::ReadPortLockfile(const FString& FilePath, FMCPPortLockfile& Out)
{
	namespace F = UEMCPWire::PortLockfileField;
	const TSharedPtr<FJsonObject> Parsed = LoadJson(FilePath);
	if (!Parsed.IsValid())
	{
		return false;
	}
	double NumberValue = 0.0;
	if (Parsed->TryGetNumberField(F::Pid, NumberValue))
	{
		Out.Pid = (uint32)NumberValue;
	}
	Parsed->TryGetStringField(F::InstanceId, Out.InstanceId);
	if (!Parsed->TryGetNumberField(F::Port, NumberValue))
	{
		return false;
	}
	Out.Port = (int32)NumberValue;
	return Out.Port > 0 && Out.Port <= 65535;
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

	if (Existing.State == UEMCPWire::BridgeState::BindFailed)
	{
		// The whole point of that record is to outlive the process that wrote
		// it: it answers the question the user is about to ask.
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

bool FMCPBridgeStateFiles::IsInstanceLive(const FMCPInstanceRecord& Record, int32 OwnBoundPort)
{
	if (Record.Pid == 0)
	{
		return false;
	}
	if (!FPlatformProcess::IsApplicationRunning(Record.Pid))
	{
		return false;
	}
	if (Record.State == UEMCPWire::BridgeState::BindFailed || Record.Port <= 0)
	{
		// Nothing bound, so there is no address to test. The process being alive
		// is the whole of what this record claims.
		return true;
	}
	if (OwnBoundPort > 0 && Record.Port == OwnBoundPort)
	{
		// The pid was recycled and this bridge took the same deterministic port.
		// Connecting would reach this bridge and prove nothing (#1633).
		return false;
	}
	// The pid could have been recycled onto an unrelated process, in which case
	// nothing is listening where the record says.
	return IsPortAccepting(Record.Port, /*TimeoutMilliseconds*/ 250);
}

int32 FMCPBridgeStateFiles::ReapStaleInstanceRecords(const FString& InInstancesDir, const FString& OwnInstanceId, int32 OwnBoundPort)
{
	return ReapStaleInstanceRecords(InInstancesDir, OwnInstanceId,
		[OwnBoundPort](const FMCPInstanceRecord& Record) { return FMCPBridgeStateFiles::IsInstanceLive(Record, OwnBoundPort); });
}

int32 FMCPBridgeStateFiles::ReapStaleInstanceRecords(
	const FString& InInstancesDir,
	const FString& OwnInstanceId,
	TFunctionRef<bool(const FMCPInstanceRecord&)> IsLive)
{
	TArray<FString> FileNames;
	IFileManager::Get().FindFiles(FileNames,
		*FPaths::Combine(InInstancesDir, FString(TEXT("*")) + UEMCPWire::StateFile::InstanceRecordSuffix), /*Files*/ true, /*Directories*/ false);

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
	namespace F = UEMCPWire::RequestedPortField;
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
	if (!Parsed->TryGetNumberField(F::Port, PortValue))
	{
		OutDetail = FString::Printf(TEXT("%s has no numeric '%s', so it was ignored."), *FilePath, F::Port);
		return INDEX_NONE;
	}

	const int32 Port = (int32)PortValue;
	if (Port < 1 || Port > 65535)
	{
		OutDetail = FString::Printf(TEXT("%s asks for port %d, outside the range 1-65535, so it was ignored."), *FilePath, Port);
		return INDEX_NONE;
	}

	FString RecordedRoot;
	if (!Parsed->TryGetStringField(F::ProjectRoot, RecordedRoot) || RecordedRoot.IsEmpty())
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

namespace
{
	/** This process's own instance record, describing the running plugin. */
	FMCPInstanceRecord MakeInstanceRecord(const FMCPBridgeIdentity& Self, const TCHAR* State, int32 Port)
	{
		FMCPInstanceRecord Record;
		Record.Port = Port;
		Record.Pid = Self.Pid;
		Record.InstanceId = Self.InstanceId;
		Record.ProjectRoot = FMCPBridgeStateFiles::ThisProjectRoot();
		Record.StartedAtUtc = Self.StartedAtUtc;
		Record.EngineVersion = FEngineVersion::Current().ToString();
		Record.ProtocolVersion = UEMCP_BRIDGE_PROTOCOL_VERSION;
		Record.HandlerApiVersion = UEMCP_BRIDGE_API_VERSION;
		Record.State = State;

		// Where this plugin loaded from, so a client never assumes Plugins/UE_MCP_Bridge.
		if (const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(UEMCPWire::BridgePluginName))
		{
			FString PluginDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
			FPaths::NormalizeDirectoryName(PluginDir);
			Record.PluginDir = PluginDir;
			Record.InstallKind = FMCPBridgeStateFiles::InstallKindFor(PluginDir);
			Record.PluginVersion = Plugin->GetDescriptor().VersionName;
		}
		return Record;
	}

	void PublishInstanceRecord(const FMCPBridgeIdentity& Self, const TCHAR* State, int32 Port)
	{
		const FString Dir = FMCPBridgeStateFiles::InstancesDir();
		if (FMCPBridgeStateFiles::WriteInstanceRecord(Dir, MakeInstanceRecord(Self, State, Port)))
		{
			UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Instance record published: %s (state=%s, port=%d)"),
				*FMCPBridgeStateFiles::RecordPath(Dir, Self.Pid), State, Port);
		}
	}

	/** Is the instance that published this port.json still there? Asked only of a
	 *  record another instance wrote, before publishing over it. */
	bool PortLockfileOwnerIsLive(const FMCPPortLockfile& Owner, int32 OwnBoundPort)
	{
		FMCPInstanceRecord AsRecord;
		AsRecord.Pid = Owner.Pid;
		AsRecord.Port = Owner.Port;
		AsRecord.InstanceId = Owner.InstanceId;
		AsRecord.State = UEMCPWire::BridgeState::Listening;
		return FMCPBridgeStateFiles::IsInstanceLive(AsRecord, OwnBoundPort);
	}

	void PublishPortLockfile(const FMCPBridgeIdentity& Self, int32 Port)
	{
		namespace F = UEMCPWire::PortLockfileField;
		const FString FilePath = FMCPBridgeStateFiles::PortLockfilePath();

		// #817: the write is owner-checked, the same way the delete is. One
		// project directory has one port.json and two editors of that project
		// have two ports, so publishing unconditionally re-aimed every client at
		// whichever editor booted second. The newcomer's address is in its own
		// instance record, where it cannot displace anyone.
		FMCPPortLockfile Existing;
		if (FMCPBridgeStateFiles::ReadPortLockfile(FilePath, Existing)
			&& !Existing.InstanceId.IsEmpty() && Existing.InstanceId != Self.InstanceId
			&& PortLockfileOwnerIsLive(Existing, Port))
		{
			UE_LOG(LogMCPBridge, Warning,
				TEXT("[UE-MCP] Another editor of this project (instance %s) is still listening and owns %s, so this bridge did not publish over it. This bridge is on port %d and its address is in %s. Clients reading port.json will reach the other editor."),
				*Existing.InstanceId, *FilePath, Port, *FMCPBridgeStateFiles::RecordPath(FMCPBridgeStateFiles::InstancesDir(), Self.Pid));
			return;
		}

		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(F::Port, Port);
		Obj->SetNumberField(F::Pid, (double)Self.Pid);
		Obj->SetStringField(F::StartedAt, Self.StartedAtUtc);
		Obj->SetStringField(F::InstanceId, Self.InstanceId);
		Obj->SetStringField(F::State, UEMCPWire::BridgeState::Listening);
		// Clients built before port.json said `state` read this.
		Obj->SetStringField(F::LegacyState, UEMCPWire::BridgeState::Listening);
		Obj->SetNumberField(F::ProtocolVersion, (double)UEMCP_BRIDGE_PROTOCOL_VERSION);
		Obj->SetNumberField(F::HandlerApiVersion, (double)UEMCP_BRIDGE_API_VERSION);

		if (!FMCPBridgeStateFiles::PublishJson(FilePath, Obj))
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Failed to write port lockfile: %s"), *FilePath);
			return;
		}

		// This instance is listening, so a bind-failure record an earlier start
		// left no longer describes reality.
		IFileManager::Get().Delete(*FMCPBridgeStateFiles::BridgeErrorPath(), /*RequireExists*/ false, /*EvenReadOnly*/ false, /*Quiet*/ true);

		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Port lockfile published: %s (port=%d, instance=%s)"),
			*FilePath, Port, *Self.InstanceId);
	}
}

void FMCPBridgeStateFiles::PublishListening(const FMCPBridgeIdentity& Self, int32 Port)
{
	PublishInstanceRecord(Self, UEMCPWire::BridgeState::Listening, Port);

	// Swept here, once, by the next process that can prove them stale rather
	// than by a timer, so a machine that crashes repeatedly does not accumulate
	// a directory of dead editors.
	const int32 Removed = ReapStaleInstanceRecords(InstancesDir(), Self.InstanceId, Port);
	if (Removed > 0)
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Removed %d stale bridge instance record(s)."), Removed);
	}

	PublishPortLockfile(Self, Port);
}

void FMCPBridgeStateFiles::PublishBindFailure(const FMCPBridgeIdentity& Self, int32 FirstPort, int32 LastPort, int32 ErrorCode)
{
	namespace F = UEMCPWire::BridgeErrorField;
	const FString FilePath = BridgeErrorPath();

	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(F::State, UEMCPWire::BridgeState::BindFailed);
	// Clients built before bridge-error.json said `state` read this.
	Obj->SetStringField(F::LegacyState, UEMCPWire::BridgeState::BindFailed);
	Obj->SetNumberField(F::Pid, (double)Self.Pid);
	Obj->SetStringField(F::StartedAt, Self.StartedAtUtc);
	Obj->SetStringField(F::FailedAt, FDateTime::UtcNow().ToIso8601());
	Obj->SetStringField(F::InstanceId, Self.InstanceId);
	Obj->SetNumberField(F::FirstPortTried, FirstPort);
	Obj->SetNumberField(F::LastPortTried, LastPort);
	Obj->SetNumberField(F::ErrorCode, ErrorCode);
	Obj->SetStringField(F::Detail, FString::Printf(
		TEXT("The editor is running but its MCP bridge could not bind a port in [%d, %d]. Free one of those ports or pin another with bridge.port in ue-mcp.yml (or UE_MCP_PORT), then restart the editor."),
		FirstPort, LastPort));

	if (!PublishJson(FilePath, Obj))
	{
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Failed to write bridge error record: %s"), *FilePath);
	}

	PublishInstanceRecord(Self, UEMCPWire::BridgeState::BindFailed, /*Port*/ 0);
}

void FMCPBridgeStateFiles::RetractOwn(const FMCPBridgeIdentity& Self)
{
	const FString FilePath = PortLockfilePath();
	if (FPaths::FileExists(FilePath))
	{
		// Only take away a record this instance wrote: an editor that never
		// listened used to delete a live editor's record on its way out.
		FMCPPortLockfile Owner;
		ReadPortLockfile(FilePath, Owner);
		if (Owner.InstanceId != Self.InstanceId)
		{
			UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Leaving port lockfile alone: it belongs to instance %s, not %s"),
				Owner.InstanceId.IsEmpty() ? TEXT("(unknown)") : *Owner.InstanceId, *Self.InstanceId);
		}
		else if (IFileManager::Get().Delete(*FilePath))
		{
			UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Port lockfile removed: %s"), *FilePath);
		}
	}

	DeleteOwnInstanceRecord(InstancesDir(), Self.Pid, Self.InstanceId);
}
