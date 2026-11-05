#include "Handlers/PIE/PIERemotePlayers.h"

#include "Handlers/PIE/PIELog.h"
#include "MCPPIERemoteControl.h"
#include "HandlerWorld.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/Guid.h"

namespace MCPPIE::PIERemotePlayers
{
	namespace
	{
		FDelegateHandle GPostLoginHandle;
		FDelegateHandle GEndPIEHandle;
		TMap<FString, TWeakObjectPtr<UMCPPIERemoteControl>> GIds;
		int32 GIdCounter = 0;

		bool IsServerWorld(const UWorld* World)
		{
			if (!World || World->WorldType != EWorldType::PIE)
			{
				return false;
			}
			const ENetMode Mode = World->GetNetMode();
			return Mode == NM_ListenServer || Mode == NM_DedicatedServer;
		}

		void OnPostLogin(AGameModeBase* GameMode, APlayerController* NewPlayer)
		{
			if (!GameMode || !NewPlayer || !IsServerWorld(GameMode->GetWorld()) || NewPlayer->IsLocalController())
			{
				return;
			}
			if (NewPlayer->FindComponentByClass<UMCPPIERemoteControl>())
			{
				return;
			}
			UMCPPIERemoteControl* Control = NewObject<UMCPPIERemoteControl>(NewPlayer, TEXT("PIEStudioRemoteControl"));
			Control->RegisterComponent();
			UE_LOG(LogMCPPIE, Log, TEXT("[PIEStudio] remote control attached to %s"), *NewPlayer->GetName());
		}

		int32 JoinOrderKey(const APlayerController* PC)
		{
			return PC && PC->PlayerState ? PC->PlayerState->GetPlayerId() : MAX_int32;
		}
	}

	void Init()
	{
		GPostLoginHandle = FGameModeEvents::GameModePostLoginEvent.AddStatic(&OnPostLogin);
		GEndPIEHandle = FEditorDelegates::EndPIE.AddLambda([](bool) { GIds.Reset(); });
	}

	void Shutdown()
	{
		FGameModeEvents::GameModePostLoginEvent.Remove(GPostLoginHandle);
		FEditorDelegates::EndPIE.Remove(GEndPIEHandle);
		GIds.Reset();
	}

	UWorld* FindServerWorld()
	{
		if (!GEngine)
		{
			return nullptr;
		}
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (IsServerWorld(Context.World()))
			{
				return Context.World();
			}
		}
		return nullptr;
	}

	UWorld* FindLocalPlayerWorld()
	{
		UWorld* World = FindServerWorld();
		if (!World && GEditor)
		{
			World = GEditor->PlayWorld;
		}
		return World;
	}

	FString RunLocalConsole(UWorld* World, const FString& Command)
	{
		if (!World || !GEngine)
		{
			return FString();
		}
		FMCPScopedPlayWorldContext PlayContext(World);
		// Through the controller where there is one, so player-scoped commands (Net PktLoss, cheats) reach the
		// right connection.
		if (APlayerController* PC = World->GetFirstPlayerController(); PC && PC->IsLocalController())
		{
			PC->ConsoleCommand(Command, /*bWriteToLog*/ true);
			return TEXT("playerController");
		}
		GEngine->Exec(World, *Command);
		return TEXT("engine");
	}

	TArray<FRemotePlayer> List()
	{
		TArray<FRemotePlayer> Out;
		UWorld* World = FindServerWorld();
		if (!World)
		{
			return Out;
		}

		TArray<APlayerController*> Remote;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* PC = It->Get();
			if (PC && !PC->IsLocalController())
			{
				Remote.Add(PC);
			}
		}
		Remote.Sort([](const APlayerController& A, const APlayerController& B) { return JoinOrderKey(&A) < JoinOrderKey(&B); });

		for (int32 i = 0; i < Remote.Num(); ++i)
		{
			FRemotePlayer& Player = Out.AddDefaulted_GetRef();
			Player.Client = i + 1;
			Player.PlayerController = Remote[i];
			Player.Control = Remote[i]->FindComponentByClass<UMCPPIERemoteControl>();
			Player.PlayerName = Remote[i]->PlayerState ? Remote[i]->PlayerState->GetPlayerName() : Remote[i]->GetName();
			Player.bReady = Player.Control && Player.Control->IsClientReady();
			Player.LogFile = Player.Control ? Player.Control->GetClientLogFile() : FString();
		}
		return Out;
	}

	UMCPPIERemoteControl* Find(int32 Client, FString& OutError)
	{
		const TArray<FRemotePlayer> Players = List();
		if (Players.Num() == 0)
		{
			OutError = TEXT("No networked PIE session with remote players is running (net mode must be Listen Server or Client with 2+ players)");
			return nullptr;
		}
		const FRemotePlayer* Player = Players.FindByPredicate([Client](const FRemotePlayer& P) { return P.Client == Client; });
		if (!Player)
		{
			OutError = FString::Printf(TEXT("No remote client %d; this session has %d (numbered 1..%d)"), Client, Players.Num(), Players.Num());
			return nullptr;
		}
		if (!Player->bReady)
		{
			OutError = FString::Printf(TEXT("Remote client %d (%s) has not finished joining"), Client, *Player->PlayerName);
			return nullptr;
		}
		return Player->Control;
	}

	void RememberId(const FString& Id, UMCPPIERemoteControl* Control)
	{
		GIds.Add(Id, Control);
	}

	UMCPPIERemoteControl* FindById(const FString& Id)
	{
		const TWeakObjectPtr<UMCPPIERemoteControl>* Found = GIds.Find(Id);
		return Found ? Found->Get() : nullptr;
	}

	FString NewId(const TCHAR* Prefix)
	{
		++GIdCounter;
		return FString::Printf(TEXT("%s-%d-%s"), Prefix, GIdCounter, *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8).ToLower());
	}
}
