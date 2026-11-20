#pragma once

// The bridge's per-session token: generated from the OS random source when the
// port is bound, published to a file only the editor's user can read, and
// required as "Authorization: Bearer <token>" on every WebSocket upgrade.
//
// The decision and file logic are free functions so the automation tests in
// Private/Tests/BridgeSessionTokenTests.cpp can exercise them without a socket.
// The directory, file name, scheme and header are the wire contract's
// (MCPWireContract.h), which the client reads from the same source.

#include "CoreMinimal.h"
#include "MCPWireContract.h"
#include "UE_MCP_BridgeModule.h"

/** Outcome of checking an upgrade request's Authorization header. */
enum class EMCPBridgeAuthDecision : uint8
{
	Accepted,
	/** No Authorization header: a client that predates the token. */
	Missing,
	/** A header that is not "Bearer <token>". */
	Malformed,
	/** A bearer token that is not this session's. */
	Mismatch,
	/** This bridge has no token (generation or publication failed), so it accepts nothing. */
	NoTokenConfigured,
};

namespace UEMCPBridgeSessionToken
{
	/** Random bytes in a token. The wire and file form is twice this many lowercase hex digits. */
	constexpr int32 TokenBytes = UEMCPWire::Token::Bytes;

	/** Response header naming the scheme, sent on the 101 and on a 401. */
	inline const TCHAR* AuthSchemeHeaderName() { return UEMCPWire::Token::SchemeHeader; }
	inline const TCHAR* AuthSchemeHeaderValue() { return UEMCPWire::Token::SchemeHeaderValue; }

	/** Overrides the token directory on both sides; hermetic tests set it. */
	inline const TCHAR* DirectoryEnvVar() { return UEMCPWire::EnvVar::BridgeTokenDir; }

	/**
	 * <home>/.ue-mcp, with home resolved the way Node's os.homedir() resolves it
	 * (USERPROFILE on Windows, HOME elsewhere, then the platform's own answer),
	 * so the client and the plugin agree. Empty when no home can be found.
	 */
	FString UserMcpDir();

	/**
	 * Fill Out with Num bytes from the operating system's cryptographic random
	 * source: BCryptGenRandom on Windows, /dev/urandom elsewhere. False when the
	 * source could not be read, in which case Out must not be used.
	 */
	bool FillRandomBytes(uint8* Out, int32 Num);

	/** Lowercase hex, two digits per byte. */
	FString BytesToLowerHex(const uint8* Bytes, int32 Num);

	/** A fresh token in its hex form, or false when no random bytes were available. */
	bool GenerateToken(FString& OutToken);

	/**
	 * Equality whose running time depends only on the lengths, never on where
	 * the inputs first differ. Different lengths compare unequal; the length of
	 * a token is not a secret.
	 */
	bool ConstantTimeEquals(const FString& A, const FString& B);

	/**
	 * Decide an upgrade request from its Authorization header. Pure: no socket,
	 * no file. An empty ExpectedToken accepts nothing.
	 */
	EMCPBridgeAuthDecision EvaluateAuthorization(bool bHeaderPresent, const FString& HeaderValue, const FString& ExpectedToken);

	/** The plain-text body of the 401 for a refusal. Never contains a token. */
	FString DescribeRefusal(EMCPBridgeAuthDecision Decision, int32 Port);

	/**
	 * Text with every credential replaced by "[REDACTED]": the values of the
	 * Authorization, Proxy-Authorization, X-UE-MCP-Token, Cookie and Set-Cookie
	 * headers (keeping the scheme word, so "Bearer [REDACTED]"), any bearer or
	 * basic credential, and a value assigned to a key named token, secret,
	 * password or api_key. Mirrors redactCredentials in
	 * src/core/redact-credentials.ts. Every handshake log line goes through it
	 * by way of UE_MCP_LOG_WIRE (#1866).
	 */
	FString RedactCredentials(const FString& Text);
}

/**
 * UE_LOG on LogMCPBridge with the formatted line passed through
 * RedactCredentials. Every log line in the upgrade handshake uses it, because
 * those lines quote what a client sent, and tests/unit/redact-credentials.test.ts
 * fails on a plain UE_LOG in BridgeWebSocket.cpp.
 */
#define UE_MCP_LOG_WIRE(Verbosity, Format, ...) \
	UE_LOG(LogMCPBridge, Verbosity, TEXT("%s"), *UEMCPBridgeSessionToken::RedactCredentials(FString::Printf(Format, ##__VA_ARGS__)))

namespace UEMCPBridgeSessionToken
{

	/** Where token files live: UE_MCP_BRIDGE_TOKEN_DIR, else UserMcpDir()/bridge. Empty when no home can be found. */
	FString TokenDirectory();

	/** <Dir>/<Port>.token */
	FString TokenFilePath(const FString& Dir, int32 Port);

	/**
	 * Publish Token to <Dir>/<Port>.token so only the current user can read it.
	 *
	 * POSIX: the directory is created 0700 and refused when another user owns
	 * it; the file is written 0600 through a temporary and renamed into place.
	 * Windows: the directory and file are created with a protected DACL that
	 * grants the current user alone full control, so nothing is inherited from
	 * a parent that other accounts can read.
	 */
	bool WriteTokenFile(const FString& Dir, int32 Port, const FString& Token, FString& OutError);

	/** The token held in FilePath, trimmed. False when unreadable. */
	bool ReadTokenFile(const FString& FilePath, FString& OutToken);

	/** Remove <Dir>/<Port>.token if it still holds Token. True when removed. */
	bool DeleteTokenFileIfOwned(const FString& Dir, int32 Port, const FString& Token);
}
