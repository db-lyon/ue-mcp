// Multiplayer PIE players: listing them, running console commands in a player's process, and reading what a
// relayed request did. Remote players are reached through PIERemotePlayers (see that header).

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerUtils.h"
#include "Handlers/PIE/PIERemotePlayers.h"
#include "MCPPIERemoteControl.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

TSharedPtr<FJsonValue> FPIEHandlers::PieClients(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	UWorld* Server = MCPPIE::PIERemotePlayers::FindServerWorld();

	TArray<TSharedPtr<FJsonValue>> Players;
	for (const MCPPIE::PIERemotePlayers::FRemotePlayer& P : MCPPIE::PIERemotePlayers::List())
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("client"), P.Client);
		Entry->SetStringField(TEXT("player"), P.PlayerName);
		Entry->SetStringField(TEXT("controller"), GetNameSafe(P.PlayerController));
		Entry->SetBoolField(TEXT("ready"), P.bReady);
		if (!P.LogFile.IsEmpty()) Entry->SetStringField(TEXT("log_file"), P.LogFile);
		Players.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("networked"), Server != nullptr);
	Result->SetStringField(TEXT("server_net_mode"), !Server ? TEXT("none")
		: Server->GetNetMode() == NM_ListenServer ? TEXT("listen_server") : TEXT("dedicated_server"));
	Result->SetBoolField(TEXT("host_player"), Server && Server->GetNetMode() == NM_ListenServer);
	Result->SetArrayField(TEXT("clients"), Players);
	Result->SetStringField(TEXT("addressing"),
		TEXT("client 0 = the editor's own player (listen server); clients 1..N = remote players in join order"));
	return MCPResult(Result);
}

// A command in a remote player's own process. The editor's own PIE player is editor(execute_command, world=pie).
TSharedPtr<FJsonValue> FPIEHandlers::PieConsole(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Command;
	if (auto E = RequireString(Params, TEXT("command"), Command)) return E;
	const int32 Client = OptionalInt(Params, TEXT("client"), 0);
	if (Client <= 0)
	{
		return MCPError(TEXT("console takes client 1..N, a remote player in join order. For the editor's own PIE player use editor(execute_command, world=\"pie\")."));
	}

	FString Err;
	UMCPPIERemoteControl* Remote = MCPPIE::PIERemotePlayers::Find(Client, Err);
	if (!Remote) return MCPError(Err);
	const FString Id = MCPPIE::PIERemotePlayers::NewId(TEXT("console"));
	Remote->SendConsole(Id, Command);
	MCPPIE::PIERemotePlayers::RememberId(Id, Remote);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("request_id"), Id);
	Result->SetNumberField(TEXT("client"), Client);
	Result->SetStringField(TEXT("command"), Command);
	Result->SetBoolField(TEXT("relayed"), true);
	MCPSetIdempotencyUnobservable(Result, TEXT("A console command runs again each time it is sent"));
	MCPSetNoRollback(Result, TEXT("A console command already run in another process has no inverse"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieRemoteResult(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Id;
	if (auto E = RequireString(Params, TEXT("request_id"), Id)) return E;

	UMCPPIERemoteControl* Remote = MCPPIE::PIERemotePlayers::FindById(Id);
	if (!Remote)
	{
		return MCPError(FString::Printf(TEXT("remote_result: '%s' is not a request relayed in this PIE session"), *Id));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("request_id"), Id);
	if (const FMCPPIERemoteResult* R = Remote->FindResult(Id))
	{
		Result->SetBoolField(TEXT("reported"), true);
		Result->SetBoolField(TEXT("ok"), R->bOk);
		Result->SetBoolField(TEXT("finished"), R->bFinished);
		if (!R->Error.IsEmpty())
		{
			Result->SetStringField(TEXT("error"), R->Error);
		}
	}
	else
	{
		Result->SetBoolField(TEXT("reported"), false);
	}
	return MCPResult(Result);
}
