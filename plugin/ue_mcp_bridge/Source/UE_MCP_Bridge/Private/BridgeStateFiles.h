#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Templates/Function.h"

/**
 * Everything the bridge keeps under <Project>/Saved/UE_MCP_Bridge/.
 *
 * There were two files here before #817 and both of them are single-writer by
 * assumption rather than by construction: `port.json` names one port for the
 * whole project directory, and `bridge-error.json` names one failed start. Two
 * editors of one project therefore had no way to describe themselves, and the
 * second one to boot simply overwrote the first one's address.
 *
 * `instances/<pid>.json` is the fix. Every process writes exactly one file,
 * named after itself, and removes only that file. Nothing is shared, so nothing
 * can be clobbered, and a record left behind by a crash is provably stale
 * rather than indistinguishable from a live one.
 *
 * `requested.json` runs the other way: the client writes it and the bridge
 * reads it. It exists because the port pin is resolved on the client (four
 * config layers plus environment) and an editor launched from Explorer cannot
 * see that resolution. It carries an integer, not a policy.
 */

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

	/** "listening" or "bind-failed". */
	FString State;
};

class FMCPBridgeStateFiles
{
public:
	/**
	 * Canonical form of a project root: forward slashes, no trailing slash,
	 * lowercased. Identical to normalizeProjectRoot in src/port.ts, because the
	 * two sides compare these strings to each other.
	 */
	static FString NormalizeProjectRoot(const FString& Dir);

	/** This project's normalized root, as the records record it. */
	static FString ThisProjectRoot();

	/** <Project>/Saved/UE_MCP_Bridge. */
	static FString StateDir();

	/** <Project>/Saved/UE_MCP_Bridge/instances. */
	static FString InstancesDir();

	/** <Project>/Saved/UE_MCP_Bridge/requested.json. */
	static FString RequestedPortPath();

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

	/** True when the process and, for a listening record, the port are still there. */
	static bool IsInstanceLive(const FMCPInstanceRecord& Record);

	/**
	 * Delete every record in the directory that can be proven dead, skipping the
	 * one carrying OwnInstanceId. Returns how many were removed.
	 *
	 * Proof runs one way only: a record is removed when liveness says no, and
	 * kept in every ambiguous case. Deleting a live editor's record is a
	 * connectivity outage; keeping a dead one costs a few hundred bytes until
	 * the next boot looks again.
	 */
	static int32 ReapStaleInstanceRecords(const FString& InInstancesDir, const FString& OwnInstanceId);

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
