#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "MCPPIEViewportCapture.h"
#include "MCPPIERemoteControl.generated.h"

/** The outcome of one request a remote player ran, as the server last heard it. */
USTRUCT()
struct UE_MCP_BRIDGERUNTIME_API FMCPPIERemoteResult
{
	GENERATED_BODY()

	UPROPERTY()
	FString Id;

	UPROPERTY()
	bool bOk = false;

	UPROPERTY()
	bool bFinished = false;

	UPROPERTY()
	FString Error;
};

/**
 * Carries PIE Studio requests to a player whose game runs in another process.
 *
 * A separate-process PIE client is a `-game` process: no editor, no ue-mcp bridge, nothing to address directly. The
 * editor (the server) adds this component to each remote player's PlayerController, which that player's machine
 * owns, and calls its client RPCs; the client runs them on its own local player and answers through server RPCs.
 * The requests ride the game connection that already exists.
 *
 * Server side: request methods (Inject, StartHold, ...) send; results and readiness are read off this component.
 * Client side: the RPC implementations run the request through FPIEInputInjector or the console.
 */
UCLASS(ClassGroup = (PIEStudio), NotBlueprintable)
class UE_MCP_BRIDGERUNTIME_API UMCPPIERemoteControl : public UActorComponent
{
	GENERATED_BODY()

public:
	UMCPPIERemoteControl();

	/** Server: true once the owning client has created its copy and can run requests. */
	bool IsClientReady() const { return bClientReady; }

	/** Server: the absolute path of the log file the owning client process writes. */
	const FString& GetClientLogFile() const { return ClientLogFile; }

	/** Server: the last result reported for a request id, if any. */
	const FMCPPIERemoteResult* FindResult(const FString& Id) const { return Results.Find(Id); }

	// Server-side senders. Each request carries a caller-chosen id the client echoes back.
	void SendInject(const FString& Id, const FSoftObjectPath& Action, const FVector& Value);
	void SendStartHold(const FString& Id, const FSoftObjectPath& Action, const FVector& Value);
	void SendUpdateHold(const FString& Id, const FVector& Value);
	void SendStop(const FString& Id);
	void SendTape(const FString& Id, const FSoftObjectPath& Action, const TArray<FVector>& Values);
	void SendConsole(const FString& Id, const FString& Command);
	/** The owning client saves its viewport to OutputPath (same machine; absolute) and reports when written. */
	void SendCapture(const FString& Id, const FString& OutputPath);
	/** The owning client turns its pawn and view to Rotation; a server teleport cannot, as the client owns its facing. */
	void SendFace(const FString& Id, const FRotator& Rotation);

	/** Most input values a single tape request may carry. */
	static constexpr int32 MaxTapeFrames = 3600;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION(Client, Reliable)
	void ClientInject(const FString& Id, const FSoftObjectPath& Action, FVector_NetQuantize100 Value);

	UFUNCTION(Client, Reliable)
	void ClientStartHold(const FString& Id, const FSoftObjectPath& Action, FVector_NetQuantize100 Value);

	UFUNCTION(Client, Reliable)
	void ClientUpdateHold(const FString& Id, FVector_NetQuantize100 Value);

	UFUNCTION(Client, Reliable)
	void ClientStop(const FString& Id);

	UFUNCTION(Client, Reliable)
	void ClientTape(const FString& Id, const FSoftObjectPath& Action, const TArray<FVector_NetQuantize100>& Values);

	UFUNCTION(Client, Reliable)
	void ClientConsole(const FString& Id, const FString& Command);

	UFUNCTION(Client, Reliable)
	void ClientCapture(const FString& Id, const FString& OutputPath);

	UFUNCTION(Client, Reliable)
	void ClientFace(const FString& Id, FRotator Rotation);

	UFUNCTION(Server, Reliable)
	void ServerReady(const FString& LogFile);

	UFUNCTION(Server, Reliable)
	void ServerReport(const FString& Id, bool bOk, bool bFinished, const FString& Error);

	// Client side: the local player this component's controller drives, or null with an error.
	class ULocalPlayer* ResolveOwnLocalPlayer(FString& OutError) const;
	void Report(const FString& Id, bool bOk, bool bFinished, const FString& Error);
	void HandleTapeFinished(const FString& Id);

	// Server state.
	bool bClientReady = false;
	FString ClientLogFile;
	TMap<FString, FMCPPIERemoteResult> Results;

	// Client state: tapes this component started, so their completion is reported to the server.
	TSet<FString> OwnedTapes;
	FDelegateHandle TapeFinishedHandle;

	// Client state: reads this process's game viewport, created on the first capture request.
	TSharedPtr<MCPPIE::FPIEViewportCapture> Capture;
};
