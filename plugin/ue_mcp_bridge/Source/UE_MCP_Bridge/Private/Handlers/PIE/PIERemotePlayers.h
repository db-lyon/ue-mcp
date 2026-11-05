#pragma once

#include "CoreMinimal.h"

class APlayerController;
class UMCPPIERemoteControl;
class UWorld;

/**
 * The players of a multiplayer PIE session that the editor reaches through the game connection.
 *
 * The editor is the server (listen or dedicated). Every player whose controller is not local to it gets a
 * UMCPPIERemoteControl at login, and is addressed by `client`: 1..N in join order. Client 0 is the editor's own
 * player on a listen server. This works the same whether clients run in the editor process or in their own.
 */
namespace MCPPIE::PIERemotePlayers
{
	struct FRemotePlayer
	{
		int32 Client = 0;
		APlayerController* PlayerController = nullptr;
		UMCPPIERemoteControl* Control = nullptr;
		FString PlayerName;
		FString LogFile;
		bool bReady = false;
	};

	void Init();
	void Shutdown();

	/** The PIE world acting as server, or null when no networked PIE session is running. */
	UWorld* FindServerWorld();

	/** Client 0's world: the server world, else the editor's play world. Null when PIE is not running. */
	UWorld* FindLocalPlayerWorld();

	/**
	 * Runs Command as client 0, the same way core's editor(execute_command, world=pie) does: inside the PIE
	 * instance that owns World, through its local player controller, or against the world on a dedicated server.
	 * Returns the route taken ("playerController" or "engine").
	 */
	FString RunLocalConsole(UWorld* World, const FString& Command);

	/** Every remote player, numbered from 1 in join order. */
	TArray<FRemotePlayer> List();

	/** The remote control for `Client`, which must be ready; null with an error otherwise. */
	UMCPPIERemoteControl* Find(int32 Client, FString& OutError);

	/** Records which remote player an injection id was sent to, so updates and stops reach the same player. */
	void RememberId(const FString& Id, UMCPPIERemoteControl* Control);
	UMCPPIERemoteControl* FindById(const FString& Id);

	/** A new request id, unique for the session. */
	FString NewId(const TCHAR* Prefix);
}
