#include "BridgePortConfig.h"
#include "BridgeServer.h"
#include "BridgeStateFiles.h"
#include "UE_MCP_BridgeModule.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Char.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

// Deterministic per-worktree base port. MUST match src/port.ts byte-for-byte:
// normalize the project root (forward slashes, no trailing slash, lowercased),
// SHA-1 the UTF-8 bytes, fold the first 4 bytes into the 49152-65535 ephemeral
// range. SHA-1 (not SHA-256) because FSHA1 is available on every platform with
// no extra dependency; the hash only spreads ports, it is not security.
int32 FMCPBridgeServer::DeriveProjectPort(const FString& ProjectRootDir)
{
	// One implementation of the normalization, shared with the instance records
	// and requested.json, all of which compare these strings against the ones
	// the client writes. Two copies is how the two sides drift apart.
	const FString Norm = FMCPBridgeStateFiles::NormalizeProjectRoot(ProjectRootDir);

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

// ── ue-mcp.yml: one key, read by hand ────────────────────────────────────────
//
// #819: the client honours `ue-mcp.bridge.port` to pin the bridge port. The
// bridge has to agree with it, or a project that pins a port gets a client
// aimed at one number and an editor listening on another.
//
// Unreal ships no YAML parser and one integer does not justify a new
// dependency, so what follows reads that single scalar and nothing else. It is
// NOT a YAML parser and must never be used as one. It models the shape that
// path actually has - block mappings, indentation, comments, plain or quoted
// scalars - and refuses everything else: sequences where a mapping belongs,
// flow mappings, anchors, aliases, tags, block scalars, tab indentation and
// multi-document streams. Anything it does not model makes it report that the
// file has no answer, which drops through to the next layer and ultimately to
// the derived port. Reading nothing is always safe; reading the wrong number
// is not.
namespace
{
	/** What a lookup found, kept separate from "no value" so the unusable case can be logged. */
	enum class EMCPScalarLookup : uint8
	{
		/** The key was there and its raw text is in the out parameter. */
		Found,
		/** The document parsed within the modelled subset and has no such key. */
		Absent,
		/** Something outside the subset. Treated as Absent by callers, but said out loud. */
		Unsupported,
	};

	/**
	 * Split `key: value` into its two halves. Handles a quoted key, since
	 * `"ue-mcp":` is legal and someone will eventually write it. False for
	 * anything that is not a plain mapping entry.
	 */
	bool SplitYamlMappingEntry(const FString& Trimmed, FString& OutKey, FString& OutRest)
	{
		int32 ColonIdx = INDEX_NONE;

		if (Trimmed.StartsWith(TEXT("\"")) || Trimmed.StartsWith(TEXT("'")))
		{
			const TCHAR Quote = Trimmed[0];
			int32 Close = INDEX_NONE;
			for (int32 Index = 1; Index < Trimmed.Len(); ++Index)
			{
				if (Trimmed[Index] == Quote)
				{
					Close = Index;
					break;
				}
			}
			if (Close == INDEX_NONE)
			{
				return false;
			}
			OutKey = Trimmed.Mid(1, Close - 1);
			// Escape sequences are not modelled, so a key holding one is refused
			// rather than compared byte-for-byte against something it is not.
			if (OutKey.Contains(TEXT("\\")))
			{
				return false;
			}
			if (Close + 1 >= Trimmed.Len() || Trimmed[Close + 1] != TEXT(':'))
			{
				return false;
			}
			ColonIdx = Close + 1;
		}
		else
		{
			if (!Trimmed.FindChar(TEXT(':'), ColonIdx))
			{
				return false;
			}
			OutKey = Trimmed.Left(ColonIdx).TrimEnd();
			if (OutKey.IsEmpty())
			{
				return false;
			}
			// None of these belong in a plain key. One appearing means the line
			// is a construct this reader does not model.
			const FString Forbidden = TEXT("{}[]#&*!|>,");
			for (int32 Index = 0; Index < OutKey.Len(); ++Index)
			{
				int32 Unused = INDEX_NONE;
				if (Forbidden.FindChar(OutKey[Index], Unused))
				{
					return false;
				}
			}
		}

		OutRest = Trimmed.Mid(ColonIdx + 1).TrimStartAndEnd();
		return true;
	}

	/**
	 * The value text after `key:`, minus any trailing comment and any pair of
	 * surrounding quotes. False when the text is a construct outside the
	 * modelled subset. An empty result means the key opens a nested block or
	 * holds a null.
	 */
	bool ExtractYamlScalar(const FString& RawValue, FString& OutScalar)
	{
		OutScalar.Reset();
		if (RawValue.IsEmpty())
		{
			return true;
		}

		if (RawValue.StartsWith(TEXT("\"")) || RawValue.StartsWith(TEXT("'")))
		{
			const TCHAR Quote = RawValue[0];
			int32 Close = INDEX_NONE;
			for (int32 Index = 1; Index < RawValue.Len(); ++Index)
			{
				if (RawValue[Index] == Quote)
				{
					Close = Index;
					break;
				}
			}
			if (Close == INDEX_NONE)
			{
				return false;
			}
			const FString Inner = RawValue.Mid(1, Close - 1);
			if (Inner.Contains(TEXT("\\")))
			{
				return false;
			}
			// Only a comment may follow the closing quote.
			const FString After = RawValue.Mid(Close + 1).TrimStartAndEnd();
			if (!After.IsEmpty() && !After.StartsWith(TEXT("#")))
			{
				return false;
			}
			OutScalar = Inner;
			return true;
		}

		// Plain scalar. A '#' at the start, or one following whitespace, opens
		// a comment; anywhere else it is part of the value.
		FString Value = RawValue;
		for (int32 Index = 0; Index < Value.Len(); ++Index)
		{
			if (Value[Index] == TEXT('#') && (Index == 0 || FChar::IsWhitespace(Value[Index - 1])))
			{
				Value = Value.Left(Index);
				break;
			}
		}
		Value.TrimStartAndEndInline();
		if (Value.IsEmpty())
		{
			return true;
		}

		// Flow collections, anchors, aliases, tags and block scalars all begin
		// with one of these, and none of them is a plain scalar.
		const TCHAR First = Value[0];
		if (First == TEXT('{') || First == TEXT('[') || First == TEXT('&') || First == TEXT('*')
			|| First == TEXT('!') || First == TEXT('|') || First == TEXT('>'))
		{
			return false;
		}

		OutScalar = Value;
		return true;
	}

	/**
	 * Walk a block-mapping document looking for one dotted key path, e.g.
	 * ue-mcp -> bridge -> port. Levels are matched by indentation: the first
	 * key seen inside a level fixes the column its siblings share, anything
	 * deeper belongs to a sibling this walk is not following, and anything
	 * shallower closes levels back off.
	 */
	EMCPScalarLookup FindYamlScalar(const FString& FileContents, const TArray<FString>& KeyPath, FString& OutValue)
	{
		constexpr int32 kMaxDepth = 8;
		if (KeyPath.Num() <= 0 || KeyPath.Num() > kMaxDepth)
		{
			return EMCPScalarLookup::Unsupported;
		}

		TArray<FString> Lines;
		FileContents.ParseIntoArrayLines(Lines, /*CullEmpty*/ false);

		// A stream holding more than one document has more than one possible
		// answer, and the client's parser rejects such a file outright. Refuse
		// it here too rather than return whichever document came first.
		// Markers only count at column zero, which is where YAML requires them,
		// so a "---" inside an indented block scalar is not one.
		{
			int32 MarkerCount = 0;
			bool bContentSeen = false;
			for (const FString& Raw : Lines)
			{
				const FString Flat = Raw.TrimStartAndEnd();
				if (Flat.IsEmpty() || Flat.StartsWith(TEXT("#")))
				{
					continue;
				}
				const bool bMarker = !Raw.StartsWith(TEXT(" ")) && !Raw.StartsWith(TEXT("\t"))
					&& (Flat == TEXT("---") || Flat == TEXT("..."));
				if (!bMarker)
				{
					bContentSeen = true;
					continue;
				}
				++MarkerCount;
				if (MarkerCount > 1 || bContentSeen)
				{
					return EMCPScalarLookup::Unsupported;
				}
			}
		}

		int32 KeyIndent[kMaxDepth];
		int32 ChildIndent[kMaxDepth + 1];
		for (int32 Index = 0; Index <= kMaxDepth; ++Index)
		{
			ChildIndent[Index] = INDEX_NONE;
		}
		FMemory::Memset(KeyIndent, 0, sizeof(KeyIndent));

		int32 Depth = 0;

		for (const FString& Raw : Lines)
		{
			int32 Indent = 0;
			while (Indent < Raw.Len() && Raw[Indent] == TEXT(' '))
			{
				++Indent;
			}
			if (Indent < Raw.Len() && Raw[Indent] == TEXT('\t'))
			{
				// Tabs are not legal YAML indentation, so the document this
				// reader thinks it is looking at is not the one on disk.
				return EMCPScalarLookup::Unsupported;
			}

			const FString Trimmed = Raw.Mid(Indent).TrimEnd();
			if (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT("#")))
			{
				continue;
			}
			if (Indent == 0 && (Trimmed == TEXT("---") || Trimmed == TEXT("...")))
			{
				// The single leading document marker the pre-pass allowed.
				continue;
			}

			// Close every level this line has stepped back out of.
			while (Depth > 0 && Indent <= KeyIndent[Depth - 1])
			{
				--Depth;
			}

			if (ChildIndent[Depth] == INDEX_NONE)
			{
				ChildIndent[Depth] = Indent;
			}
			if (Indent > ChildIndent[Depth])
			{
				// Inside a sibling's block. Not on the path.
				continue;
			}
			if (Indent < ChildIndent[Depth])
			{
				// Aligned with nothing this walk knows about.
				return EMCPScalarLookup::Unsupported;
			}

			if (Trimmed.StartsWith(TEXT("-")))
			{
				// A sequence where the path expects a mapping.
				return EMCPScalarLookup::Unsupported;
			}

			FString Key;
			FString Rest;
			if (!SplitYamlMappingEntry(Trimmed, Key, Rest))
			{
				return EMCPScalarLookup::Unsupported;
			}

			if (Key != KeyPath[Depth])
			{
				continue;
			}

			FString Scalar;
			if (!ExtractYamlScalar(Rest, Scalar))
			{
				return EMCPScalarLookup::Unsupported;
			}

			if (Depth == KeyPath.Num() - 1)
			{
				if (Scalar.IsEmpty())
				{
					return EMCPScalarLookup::Absent;
				}
				OutValue = Scalar;
				return EMCPScalarLookup::Found;
			}

			// An interior key has to open a nested block, so nothing may follow
			// the colon. `bridge: { port: 1 }` is a flow mapping, not modelled.
			if (!Scalar.IsEmpty())
			{
				return EMCPScalarLookup::Unsupported;
			}

			KeyIndent[Depth] = Indent;
			ChildIndent[Depth + 1] = INDEX_NONE;
			++Depth;
		}

		return EMCPScalarLookup::Absent;
	}

	/**
	 * `ue-mcp.bridge.port` from one config file, or INDEX_NONE when the file is
	 * absent, has no such key, or holds something that is not a port number.
	 * Every rejection is logged, because a pin that is silently ignored is the
	 * exact failure this whole path exists to remove.
	 */
	int32 ReadBridgePortFromFile(const FString& FilePath)
	{
		if (FilePath.IsEmpty() || !FPaths::FileExists(FilePath))
		{
			return INDEX_NONE;
		}

		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *FilePath))
		{
			UE_LOG(LogMCPBridge, Warning,
				TEXT("[UE-MCP] Could not read %s, so any bridge.port in it was ignored"), *FilePath);
			return INDEX_NONE;
		}

		// A file that never says "bridge" cannot hold the key under any spelling
		// of it, so there is nothing to find and nothing worth warning about.
		// Without this, every config using YAML outside the modelled subset for
		// something unrelated would produce a warning about a key it never set.
		if (!Contents.Contains(TEXT("bridge")))
		{
			return INDEX_NONE;
		}

		const TArray<FString> KeyPath = { TEXT("ue-mcp"), TEXT("bridge"), TEXT("port") };
		FString RawValue;
		const EMCPScalarLookup Outcome = FindYamlScalar(Contents, KeyPath, RawValue);

		if (Outcome == EMCPScalarLookup::Unsupported)
		{
			UE_LOG(LogMCPBridge, Warning,
				TEXT("[UE-MCP] %s uses YAML beyond what the bridge's single-key reader models, so bridge.port was not read from it. Write it as a plain nested key (ue-mcp: / bridge: / port: NNNN) for the editor to honour it."),
				*FilePath);
			return INDEX_NONE;
		}
		if (Outcome == EMCPScalarLookup::Absent)
		{
			return INDEX_NONE;
		}

		// Digits only, and few enough of them that Atoi cannot overflow. A value
		// too large for a port still reaches the range check below, so the
		// warning names the real problem instead of calling 999999 a non-number.
		bool bAllDigits = RawValue.Len() > 0 && RawValue.Len() <= 9;
		for (int32 Index = 0; bAllDigits && Index < RawValue.Len(); ++Index)
		{
			bAllDigits = FChar::IsDigit(RawValue[Index]);
		}
		if (!bAllDigits)
		{
			UE_LOG(LogMCPBridge, Warning,
				TEXT("[UE-MCP] bridge.port in %s is '%s', which is not a port number. Falling back to the derived port."),
				*FilePath, *RawValue);
			return INDEX_NONE;
		}

		const int32 Parsed = FCString::Atoi(*RawValue);
		if (Parsed < 1 || Parsed > 65535)
		{
			UE_LOG(LogMCPBridge, Warning,
				TEXT("[UE-MCP] bridge.port in %s is %d, outside the range 1-65535. Falling back to the derived port."),
				*FilePath, Parsed);
			return INDEX_NONE;
		}

		return Parsed;
	}

	/** `~/.ue-mcp/config.yml`, or whatever UE_MCP_GLOBAL_CONFIG points at. */
	FString UserGlobalConfigPath()
	{
		const FString Override = FPlatformMisc::GetEnvironmentVariable(TEXT("UE_MCP_GLOBAL_CONFIG"));
		if (!Override.IsEmpty())
		{
			return Override;
		}

		const FString Home = FPlatformProcess::UserHomeDir();
		if (Home.IsEmpty())
		{
			return FString();
		}
		return FPaths::Combine(Home, TEXT(".ue-mcp"), TEXT("config.yml"));
	}

	/**
	 * `ue-mcp.bridge.port` across the layered config, or INDEX_NONE when no
	 * layer pins one. The layers and their order are the client's, from
	 * loadLayeredUeMcpBlock in src/project.ts: user-global, then the tracked
	 * project file, then the optional UE_MCP_ENV overlay, then the untracked
	 * per-machine file, each winning over the one before. They are visited
	 * highest-first here so the first hit is the winner.
	 */
	int32 ReadConfiguredBridgePort(FString& OutSourceFile)
	{
		const FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());

		TArray<FString> Layers;
		Layers.Add(FPaths::Combine(ProjectRoot, TEXT("ue-mcp.local.yml")));

		const FString EnvName = FPlatformMisc::GetEnvironmentVariable(TEXT("UE_MCP_ENV"));
		if (!EnvName.IsEmpty())
		{
			// The name lands in a filename, so keep it to something that cannot
			// walk out of the project directory.
			bool bSafe = true;
			for (int32 Index = 0; bSafe && Index < EnvName.Len(); ++Index)
			{
				const TCHAR C = EnvName[Index];
				bSafe = FChar::IsAlnum(C) || C == TEXT('-') || C == TEXT('_');
			}
			if (bSafe)
			{
				Layers.Add(FPaths::Combine(ProjectRoot, FString::Printf(TEXT("ue-mcp.%s.yml"), *EnvName)));
			}
			else
			{
				UE_LOG(LogMCPBridge, Warning,
					TEXT("[UE-MCP] UE_MCP_ENV is '%s', which is not a usable config overlay name. Skipping the overlay layer."),
					*EnvName);
			}
		}

		Layers.Add(FPaths::Combine(ProjectRoot, TEXT("ue-mcp.yml")));
		Layers.Add(UserGlobalConfigPath());

		for (const FString& Layer : Layers)
		{
			const int32 Port = ReadBridgePortFromFile(Layer);
			if (Port != INDEX_NONE)
			{
				OutSourceFile = Layer;
				return Port;
			}
		}

		return INDEX_NONE;
	}
}

FMCPBridgePortChoice FMCPBridgeServer::ResolveConfiguredPort()
{
	FMCPBridgePortChoice Choice;

	// 1. Explicit command-line override: -MCPPort=NNNN
	int32 CmdPort = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("MCPPort="), CmdPort) && CmdPort > 0 && CmdPort < 65536)
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Using port %d from -MCPPort command line"), CmdPort);
		Choice.Port = CmdPort;
		Choice.Source = TEXT("-MCPPort command line");
		Choice.bPinned = true;
		return Choice;
	}

	// 2. Environment override: UE_MCP_PORT (matches the Node client's env var).
	const FString EnvPort = FPlatformMisc::GetEnvironmentVariable(TEXT("UE_MCP_PORT"));
	if (!EnvPort.IsEmpty())
	{
		const int32 P = FCString::Atoi(*EnvPort);
		if (P > 0 && P < 65536)
		{
			UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Using port %d from UE_MCP_PORT env"), P);
			Choice.Port = P;
			Choice.Source = TEXT("UE_MCP_PORT environment variable");
			Choice.bPinned = true;
			return Choice;
		}
		UE_LOG(LogMCPBridge, Warning,
			TEXT("[UE-MCP] UE_MCP_PORT is '%s', which is not a port number. Ignoring it."), *EnvPort);
	}

	// 3. The port the client published for this project in
	//    Saved/UE_MCP_Bridge/requested.json (#817).
	//
	//    The pin the client uses is a four-layer config merge plus environment,
	//    resolved in TypeScript. An editor launched from Explorer sees none of
	//    that: it has no UE_MCP_PORT in its environment and no way to apply the
	//    same precedence, so the two halves ended up on different ports for
	//    exactly the users who had asked for a specific one. The client writes
	//    the integer it resolved; the bridge reads it and binds it.
	//
	//    Above the bridge's own config read because it is the same answer with
	//    more inputs. Below the two explicit overrides, because a human typing
	//    -MCPPort= or UE_MCP_PORT for this launch means this launch.
	//
	//    The file exists only while a pin exists (the client removes it when the
	//    pin goes away), so an unpinned install finds nothing here, logs
	//    nothing, and resolves byte-identically to how it did before.
	{
		FString RequestDetail;
		const int32 RequestedPort = FMCPBridgeStateFiles::ReadRequestedPort(
			FMCPBridgeStateFiles::RequestedPortPath(),
			FMCPBridgeStateFiles::ThisProjectRoot(),
			RequestDetail);

		if (RequestedPort != INDEX_NONE)
		{
			UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Using port %d requested by the ue-mcp client in %s"),
				RequestedPort, *FMCPBridgeStateFiles::RequestedPortPath());
			Choice.Port = RequestedPort;
			Choice.Source = TEXT("requested.json published by the ue-mcp client");
			Choice.bPinned = true;
			return Choice;
		}
		if (!RequestDetail.IsEmpty())
		{
			// A file that is present and unusable is a pin that silently did not
			// take, which is the failure this whole channel exists to remove.
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] %s"), *RequestDetail);
		}
	}

	// 4. `ue-mcp.bridge.port` from the project's layered config (#819). The
	//    client reads the same key, so skipping it here is how a pinned project
	//    ends up with the two halves on different ports. This stays as the
	//    answer for a project the client has never been run against, which is
	//    the case where requested.json does not exist yet.
	FString ConfigFile;
	const int32 ConfigPort = ReadConfiguredBridgePort(ConfigFile);
	if (ConfigPort != INDEX_NONE)
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Using port %d from bridge.port in %s"), ConfigPort, *ConfigFile);
		Choice.Port = ConfigPort;
		Choice.Source = FString::Printf(TEXT("bridge.port in %s"), *FPaths::GetCleanFilename(ConfigFile));
		Choice.bPinned = true;
		return Choice;
	}

	// 5. Deterministic per-worktree port derived from the project root path.
	const FString ProjectRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir());
	const int32 Derived = DeriveProjectPort(ProjectRoot);
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Derived per-project port %d from %s"), Derived, *ProjectRoot);
	Choice.Port = Derived;
	Choice.Source = TEXT("derived from the project path");
	Choice.bPinned = false;
	return Choice;
}
