#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MCPFab
{
struct FSession
{
	FString BaseUrl;
	FString AccountId;
	FString Token;
};

// Credentials remain in the editor's native Fab session, never in MCP results.
bool ReadSession(FSession& Out, FString& Error);
FString AccountFromToken(const FString& Token);

// Whether a URL Fab handed back may be fetched: https, or a loopback http
// server while a test session is installed.
bool IsFetchableUrl(const FString& Url);

#if WITH_DEV_AUTOMATION_TESTS
// Test seam: ReadSession answers with this session instead of the Fab
// plugin's, so a test can run the account flow against a local server. A
// null pointer restores the real session.
void SetSessionForTesting(const FSession* Session);
#endif
FString LibraryUrl(const FSession& Session, int32 BatchSize, const FString& Cursor);

struct FLibraryState
{
	FString State = TEXT("idle");
	FString Error;
	FString AccountId;
	FString Next;
	int32 Pages = 0;
	int32 HttpStatus = 0;
	int32 PublishedRows = 0;
	TArray<TSharedPtr<FJsonValue>> Items;
	TArray<TSharedPtr<FJsonValue>> Pending;
	TSet<FString> Cursors;

	void Begin(const FString& Account);
	bool Consume(int32 Status, const FString& ContentType, const FString& Body);
	void Fail(const FString& Message);
	TSharedPtr<FJsonObject> Describe() const;
};

// False leaves the previous snapshot and Content Browser rows intact.
bool PublishLibrary(const TArray<TSharedPtr<FJsonValue>>& Items, FString& Error);
FLibraryState& Library();
bool StartLibrarySync(int32 BatchSize, FString& Error);
void ResetLibrary();
}
