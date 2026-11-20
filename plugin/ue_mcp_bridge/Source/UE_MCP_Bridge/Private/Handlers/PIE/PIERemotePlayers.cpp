#include "Handlers/PIE/PIERemotePlayers.h"

#include "Handlers/PIE/PIELog.h"
#include "Handlers/PIE/PIEWorldResolver.h"
#include "MCPPIERemoteControl.h"
#include "HandlerWorld.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
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

	ULocalPlayer* FindOwnLocalPlayer(int32 PIEInstance, int32 LocalPlayerIndex, FString& OutError)
	{
		PIEWorldResolver::FPlayerTarget Target;
		if (PIEInstance >= 0)
		{
			return PIEWorldResolver::ResolvePlayer(PIEInstance, LocalPlayerIndex, Target, OutError) ? Target.LocalPlayer : nullptr;
		}
		if (UWorld* Server = FindServerWorld(); Server && Server->GetNetMode() == NM_ListenServer)
		{
			FString HostError;
			if (PIEWorldResolver::ResolvePlayerInWorld(Server, LocalPlayerIndex, Target, HostError))
			{
				return Target.LocalPlayer;
			}
		}
		if (!GEngine)
		{
			OutError = TEXT("GEngine is unavailable");
			return nullptr;
		}
		ULocalPlayer* Best = nullptr;
		int32 BestInstance = MAX_int32;
		FString LastError;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || Context.WorldType != EWorldType::PIE || Context.PIEInstance >= BestInstance)
			{
				continue;
			}
			if (PIEWorldResolver::ResolvePlayerInWorld(World, LocalPlayerIndex, Target, LastError))
			{
				Best = Target.LocalPlayer;
				BestInstance = Context.PIEInstance;
			}
		}
		if (!Best)
		{
			OutError = LastError.IsEmpty()
				? FString::Printf(TEXT("no PIE world has local player %d; is PIE running?"), LocalPlayerIndex)
				: LastError;
		}
		return Best;
	}

	FString DescribeLocalPlayer(const ULocalPlayer* LocalPlayer)
	{
		UWorld* World = LocalPlayer ? LocalPlayer->GetWorld() : nullptr;
		if (!World || !GEngine)
		{
			return TEXT("no player");
		}
		const FWorldContext* Context = GEngine->GetWorldContextFromWorld(World);
		const int32 Index = World->GetGameInstance() ? World->GetGameInstance()->GetLocalPlayers().IndexOfByKey(LocalPlayer) : INDEX_NONE;
		return FString::Printf(TEXT("pie_instance %d local player %d in %s (%s)"),
			Context ? Context->PIEInstance : INDEX_NONE, Index, *World->GetName(), *DescribePIENetMode(World));
	}

	APlayerController* FindAuthorityController(APlayerController* PC)
	{
		UWorld* World = PC ? PC->GetWorld() : nullptr;
		if (!World)
		{
			return nullptr;
		}
		if (World->GetNetMode() != NM_Client)
		{
			return PC;
		}
		UWorld* Server = FindServerWorld();
		if (!Server || !PC->PlayerState)
		{
			return nullptr;
		}
		const int32 PlayerId = PC->PlayerState->GetPlayerId();
		for (FConstPlayerControllerIterator It = Server->GetPlayerControllerIterator(); It; ++It)
		{
			APlayerController* Candidate = It->Get();
			if (Candidate && Candidate->PlayerState && Candidate->PlayerState->GetPlayerId() == PlayerId)
			{
				return Candidate;
			}
		}
		return nullptr;
	}

	APawn* TeleportOnAuthority(APlayerController* PC, APawn* Pawn, const FVector& Location, const FRotator& Rotation, FString& OutError)
	{
		if (!Pawn)
		{
			OutError = TEXT("no pawn to move");
			return nullptr;
		}
		APawn* Authority = Pawn;
		UWorld* World = Pawn->GetWorld();
		if (World && World->GetNetMode() == NM_Client)
		{
			APlayerController* AuthorityPC = FindAuthorityController(PC ? PC : Cast<APlayerController>(Pawn->GetController()));
			Authority = AuthorityPC ? AuthorityPC->GetPawn() : nullptr;
			if (!Authority)
			{
				OutError = FindServerWorld()
					? FString::Printf(TEXT("the server holds no pawn for %s's player"), *Pawn->GetName())
					: TEXT("the server runs outside the editor process, so it cannot be moved from here; a client-only move is undone by the server");
				return nullptr;
			}
			Authority->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::ResetPhysics);
			AuthorityPC->SetControlRotation(Rotation);
		}
		Pawn->SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::ResetPhysics);
		if (AController* Controller = PC ? PC : Pawn->GetController())
		{
			Controller->SetControlRotation(Rotation);
		}
		return Authority;
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
		// Each player holds its newest FMCPPIERemoteReports::MaxRequests requests; an id its player dropped, or whose
		// player left, routes nowhere and is forgotten here too.
		if (GIds.Num() > FMCPPIERemoteReports::MaxRequests)
		{
			for (auto It = GIds.CreateIterator(); It; ++It)
			{
				const UMCPPIERemoteControl* Held = It->Value.Get();
				if (!Held || !Held->OwnsRequest(It->Key))
				{
					It.RemoveCurrent();
				}
			}
		}
	}

	bool CanClaimId(const FString& Id, const UMCPPIERemoteControl* Control, FString& OutError)
	{
		if (Id.Len() > FMCPPIERemoteReports::MaxIdLength)
		{
			OutError = FString::Printf(TEXT("injection_id is %d characters; a caller-chosen id is at most %d. Leave it out to have one minted."),
				Id.Len(), FMCPPIERemoteReports::MaxIdLength);
			return false;
		}
		const UMCPPIERemoteControl* Holder = FindById(Id);
		if (Holder && Holder != Control && Holder->OwnsRequest(Id))
		{
			OutError = FString::Printf(TEXT("injection_id '%s' is already in use by another remote player's request. Stop it first, choose another id, or leave it out to have one minted."), *Id);
			return false;
		}
		return true;
	}

	int32 NumRememberedIds()
	{
		return GIds.Num();
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
