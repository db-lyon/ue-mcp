// Multiplayer PIE players: listing them, running console commands in a player's process, and reading what a
// relayed request did. Remote players are reached through PIERemotePlayers (see that header).

#include "Handlers/PIE/PIEHandlers.h"
#include "HandlerUtils.h"
#include "Handlers/PIE/PIEClientUI.h"
#include "Handlers/PIE/PIERemotePlayers.h"
#include "Handlers/PIE/PIEWorldResolver.h"
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
	// The pie_instance and client_id numbers inject_input and script_run take, per world.
	Result->SetArrayField(TEXT("pie_worlds"), MCPPIE::PIEWorldResolver::DescribePlayWorlds());
	Result->SetStringField(TEXT("addressing"),
		TEXT("client 0 = the editor's own player (the listen-server host, else the lowest PIE instance's player); clients 1..N = remote players in join order"));
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

// A key or typed text through FSlateApplication as one PIE player's Slate user, so focused widgets, Slate
// navigation, UMG key handlers and, for a key nothing in the UI handles, the viewport and the player's input
// receive it. Enhanced Input injection never reaches a menu in UI-only input mode (#1928), and the first world
// context is the server on a dedicated session (#2018), so the player is the one the call names.
TSharedPtr<FJsonValue> FPIEHandlers::PieSendKey(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	int32 PIEInstance = INDEX_NONE;
	FString Err;
	if (!ParsePIEInstance(Params, PIEInstance, Err)) return MCPError(Err);
	const int32 ClientId = FMath::Max(0, OptionalInt(Params, TEXT("client_id"), 0));
	const FString KeyName = OptionalString(Params, TEXT("key"));
	const FString Text = OptionalString(Params, TEXT("text"));
	const FString EventText = OptionalString(Params, TEXT("event"), TEXT("press"));
	if (KeyName.IsEmpty() == Text.IsEmpty())
	{
		return MCPError(TEXT("send_key takes exactly one of key (an FKey name such as Right or Enter) or text (characters to type)."));
	}
	MCPPIE::PIEClientUI::EKeyPhase Phase = MCPPIE::PIEClientUI::EKeyPhase::Press;
	if (!MCPPIE::PIEClientUI::ParseKeyPhase(EventText, Phase))
	{
		return MCPError(FString::Printf(TEXT("event '%s' is not press, down or up."), *EventText));
	}

	MCPPIE::PIEClientUI::FUIPlayer Player;
	if (!MCPPIE::PIEClientUI::Resolve(PIEInstance, ClientId, Player, Err)) return MCPError(Err);

	MCPPIE::PIEClientUI::FKeyResult Sent;
	const bool bOk = KeyName.IsEmpty()
		? MCPPIE::PIEClientUI::SendText(Player, Text, Sent, Err)
		: MCPPIE::PIEClientUI::SendKey(Player, KeyName, Phase, Sent, Err);
	if (!bOk) return MCPError(Err);

	auto Result = MCPSuccess();
	if (!KeyName.IsEmpty())
	{
		Result->SetStringField(TEXT("key"), KeyName);
		Result->SetStringField(TEXT("event"), EventText.ToLower());
	}
	else
	{
		Result->SetStringField(TEXT("text"), Text);
	}
	MCPPIE::PIEClientUI::WriteKeyResult(Player, Sent, Result);
	if (Sent.bFocusMovedToViewport)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("This player's Slate user had no focus inside its viewport (another client's window or the editor held it), so focus moved to the viewport first. ")
			TEXT("Put focus on a widget with widget(set_runtime_focus) with the same pieInstance before navigating."));
	}
	MCPSetIdempotencyUnobservable(Result, TEXT("A key is delivered again each time it is sent"));
	MCPSetNoRollback(Result, TEXT("A key event already delivered to the game has no inverse"));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FPIEHandlers::PieRemoteResult(const TSharedPtr<FJsonObject>& Params)
{
	MCP_CHECK_GAME_THREAD();
	FString Id;
	if (auto E = RequireString(Params, TEXT("request_id"), Id)) return E;

	// Only the player a request was sent to answers for it, and only while it still holds the request.
	UMCPPIERemoteControl* Remote = MCPPIE::PIERemotePlayers::FindById(Id);
	if (!Remote || !Remote->OwnsRequest(Id))
	{
		return MCPError(FString::Printf(TEXT("remote_result: '%s' is not a request relayed in this PIE session, or is no longer held. ")
			TEXT("Each remote player keeps its newest %d requests; send the request again to read a fresh result."),
			*Id, FMCPPIERemoteReports::MaxRequests));
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
