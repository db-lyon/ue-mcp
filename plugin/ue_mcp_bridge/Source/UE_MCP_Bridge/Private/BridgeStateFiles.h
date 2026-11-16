#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Templates/Function.h"

/**
 * Everything the bridge keeps under <Project>/Saved/UE_MCP_Bridge/, and the one
 * owner of every record it writes there. File names and field names are the
 * generated wire contract's (MCPWireContract.h), which the client reads from
 * the same source.
 *
 * `port.json` names one port for the whole project directory and
 * `bridge-error.json` one failed start, so both are single-writer by
 * assumption: a second editor of one project has no way to describe itself in
 * them. `instances/<pid>.json` is single-writer by construction: every process
 * writes exactly one file, named after itself, and removes only that file, so a
 * record left behind by a crash is provably stale (#817).
 *
 * `requested.json` runs the other way: the client writes it and the bridge
 * reads it. It exists because the port pin is resolved on the client (four
 * config layers plus environment) and an editor launched from Explorer cannot
 * see that resolution. It carries an integer, not a policy.
 */

/** Who this bridge is, as every record it writes names it. */
struct FMCPBridgeIdentity
{
	/**
	 * The server object that wrote a record. Pids are recycled and a project can
	 * be relaunched inside a second, so the pid alone cannot answer "is this the
	 * record I wrote".
	 */
	FString InstanceId;
	FString StartedAtUtc;
	uint32 Pid = 0;
};

/** What port.json says, read with its own reader rather than the instance record's. */
struct FMCPPortLockfile
{
	int32 Port = 0;
	/** 0 from a bridge built before port.json named its process. */
	uint32 Pid = 0;
	FString InstanceId;
};

/** One editor's record of the bridge it is running, or failed to run. */
struct FMCPInstanceRecord
{
	/** The bound port, or 0 when the bind never succeeded. */
	int32 Port = 0;

	/** The editor process. Named in the filename too, so the two can be compared. */
	uint32 Pid = 0;

	/**
	 * The server object that wrote this. Pids are recycled and a project can be
	 * relaunched inside a second, so the pid alone cannot answer "is this the
	 * record I wrote".
	 */
	FString InstanceId;

	/** Normalized project root, so a copied Saved directory is detectable. */
	FString ProjectRoot;

	FString StartedAtUtc;
	FString EngineVersion;
	int32 ProtocolVersion = 0;
	int32 HandlerApiVersion = 0;

	/** UEMCPWire::BridgeState: listening or bind-failed. */
	FString State;

	/** Absolute directory of the loaded bridge plugin, forward slashes. */
	FString PluginDir;

	/** "source" or "binary", from the install marker when there is one. */
	FString InstallKind;

	/** The loaded plugin descriptor's VersionName. */
	FString PluginVersion;
};

class FMCPBridgeStateFiles
{
public:
	/**
	 * Canonical form of a project root: forward slashes, no trailing slash,
	 * lowercased. The client normalizes the same way (the contract's port
	 * section), because the two sides compare these strings to each other.
	 */
	static FString NormalizeProjectRoot(const FString& Dir);

	/** This project's root, absolute, as the port derivation and config layers read it. */
	static FString ProjectRootDir();

	/** This project's normalized root, as the records record it. */
	static FString ThisProjectRoot();

	/** <Project>/Saved/UE_MCP_Bridge. */
	static FString StateDir();

	/** <Project>/Saved/UE_MCP_Bridge/instances. */
	static FString InstancesDir();

	/** <Project>/Saved/UE_MCP_Bridge/requested.json. */
	static FString RequestedPortPath();

	/** <Project>/Saved/UE_MCP_Bridge/port.json. */
	static FString PortLockfilePath();

	/** <Project>/Saved/UE_MCP_Bridge/bridge-error.json. */
	static FString BridgeErrorPath();

	/**
	 * Publish this bridge as listening on Port. Its own instance record goes
	 * first, because port.json may decline to name it (another live editor of
	 * the project owns it) and then the instance record is the only address
	 * this editor has. Then the stale sweep, then port.json, then the removal of
	 * a bind-failure record an earlier start left.
	 */
	static void PublishListening(const FMCPBridgeIdentity& Self, int32 Port);

	/**
	 * Leave an on-disk trace for "editor alive, bridge dead": bridge-error.json,
	 * never port.json (a failed start must not erase a live editor's record),
	 * and a bind-failed instance record, which no second editor can overwrite.
	 */
	static void PublishBindFailure(const FMCPBridgeIdentity& Self, int32 FirstPort, int32 LastPort, int32 ErrorCode);

	/**
	 * Remove what this bridge published, on every exit including a failed bind:
	 * port.json only when it carries this instance's id, and its own instance
	 * record unless that record explains a bind failure.
	 */
	static void RetractOwn(const FMCPBridgeIdentity& Self);

	/** Read port.json, or false when it is absent, malformed, or names no port. */
	static bool ReadPortLockfile(const FString& FilePath, FMCPPortLockfile& Out);

	/**
	 * Publish JSON by writing a temporary file and renaming it over the target.
	 *
	 * The client polls these files while it waits for an editor. Writing in
	 * place means a poll can land mid-write, read a torn document, fail to
	 * parse it and fall back to a guessed port. A rename is the one step a
	 * reader cannot catch halfway through.
	 */
	static bool PublishJson(const FString& FilePath, const TSharedPtr<FJsonObject>& Payload);

	/** Parse a JSON object off disk, or null when it is absent or malformed. */
	static TSharedPtr<FJsonObject> LoadJson(const FString& FilePath);

	/**
	 * "source" or "binary" for a plugin directory. The install marker's `kind`
	 * wins; without one, a Source tree means source.
	 */
	static FString InstallKindFor(const FString& PluginDir);

	/** <InstancesDir>/<Pid>.json. */
	static FString RecordPath(const FString& InInstancesDir, uint32 Pid);

	static bool WriteInstanceRecord(const FString& InInstancesDir, const FMCPInstanceRecord& Record);
	static bool ReadInstanceRecord(const FString& FilePath, FMCPInstanceRecord& OutRecord);

	/**
	 * Remove this process's own record on the way out.
	 *
	 * Two things it deliberately will not do. It will not remove a file whose
	 * instanceId is somebody else's, which is what stops a recycled pid from
	 * erasing a live editor. And it will not remove a `bind-failed` record,
	 * because that record is the entire explanation for "the editor is running
	 * and the bridge is not" and the process that wrote it is exactly the
	 * process now exiting.
	 */
	static void DeleteOwnInstanceRecord(const FString& InInstancesDir, uint32 Pid, const FString& InstanceId);

	/**
	 * True when the process and, for a listening record, the port are still there.
	 *
	 * OwnBoundPort is the port this bridge holds, or 0. The bind is exclusive, so
	 * another instance's record naming that port is stale however alive its pid
	 * looks: the only listener there is this bridge (#1633).
	 */
	static bool IsInstanceLive(const FMCPInstanceRecord& Record, int32 OwnBoundPort = 0);

	/**
	 * Delete every record in the directory that can be proven dead, skipping the
	 * one carrying OwnInstanceId. Returns how many were removed.
	 *
	 * Proof runs one way only: a record is removed when liveness says no, and
	 * kept in every ambiguous case. Deleting a live editor's record is a
	 * connectivity outage; keeping a dead one costs a few hundred bytes until
	 * the next boot looks again.
	 */
	static int32 ReapStaleInstanceRecords(const FString& InInstancesDir, const FString& OwnInstanceId, int32 OwnBoundPort = 0);

	/** Reap with an injected liveness test, so the sweep can be tested offline. */
	static int32 ReapStaleInstanceRecords(
		const FString& InInstancesDir,
		const FString& OwnInstanceId,
		TFunctionRef<bool(const FMCPInstanceRecord&)> IsLive);

	/**
	 * Will anything accept a loopback connection on this port right now?
	 *
	 * Deliberately weaker than an identity check: it answers "is that address
	 * still occupied", which is all a staleness sweep needs. Deciding that a
	 * given editor is the right editor is the client's job and it has the
	 * capability handshake to do it with.
	 *
	 * The probe connects and hangs up without upgrading, so the editor on the
	 * other end logs one line about a connection that closed before its upgrade
	 * request arrived. That line is this function, once per sweep per record.
	 */
	static bool IsPortAccepting(int32 Port, int32 TimeoutMilliseconds);

	/**
	 * The port the client asked this editor to bind, or INDEX_NONE.
	 *
	 * OutDetail is filled in only when the file exists and cannot be honoured,
	 * so an install that never pinned a port produces no file, no detail and no
	 * log line, and resolves exactly as it did before this channel existed.
	 */
	static int32 ReadRequestedPort(const FString& FilePath, const FString& NormalizedProjectRoot, FString& OutDetail);
};
