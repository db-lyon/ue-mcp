#include "UE_MCP_BridgeModule.h"
#include "Modules/ModuleManager.h"
#include "BridgeServer.h"
#include "EngineStatusHooks.h"
#include "MCPEngineStatus.h"
#include "Handlers/DialogHandlers.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/Parse.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY(LogMCPBridge);
IMPLEMENT_MODULE(FUE_MCP_BridgeModule, UE_MCP_Bridge)

static TSharedPtr<FMCPBridgeServer> G_BridgeServer;

namespace
{
	/**
	 * #968: dialog policies supplied at launch, before anything can wedge.
	 *
	 * A modal raised during startup blocks the game thread, and while the
	 * dialog handlers can now clear one from outside, an agent that already
	 * knows the answer should not have to. `UE_MCP_DIALOG_POLICY` (or
	 * `-MCPDialogPolicy=`) is a semicolon separated list of `pattern=response`
	 * pairs, applied here at module startup so the very first prompt is already
	 * covered:
	 *
	 *     UE_MCP_DIALOG_POLICY="Restore=no;Would you like to rebuild=yes"
	 *
	 * The response words are exactly the ones set_dialog_policy accepts,
	 * because each entry is applied by dispatching set_dialog_policy itself
	 * rather than by a second copy of its parsing. An entry that is not a
	 * pattern=response pair is skipped and said out loud: a policy that
	 * silently does nothing is worse than no policy at all.
	 */
	FString ReadConfiguredDialogPolicy()
	{
		FString FromCommandLine;
		if (FParse::Value(FCommandLine::Get(), TEXT("MCPDialogPolicy="), FromCommandLine) && !FromCommandLine.IsEmpty())
		{
			return FromCommandLine;
		}
		return FPlatformMisc::GetEnvironmentVariable(TEXT("UE_MCP_DIALOG_POLICY"));
	}

	void ApplyConfiguredDialogPolicies(FMCPHandlerRegistry& Registry)
	{
		const FString Spec = ReadConfiguredDialogPolicy();
		if (Spec.IsEmpty())
		{
			return;
		}

		TArray<FString> Entries;
		Spec.ParseIntoArray(Entries, TEXT(";"), /*InCullEmpty*/ true);
		for (const FString& Entry : Entries)
		{
			// Split on the LAST '=' so a pattern may contain one.
			int32 Separator = INDEX_NONE;
			if (!Entry.FindLastChar(TEXT('='), Separator))
			{
				UE_LOG(LogMCPBridge, Warning,
					TEXT("[UE-MCP] Dialog policy entry '%s' has no 'pattern=response' separator and was ignored."), *Entry);
				continue;
			}
			const FString Pattern = Entry.Left(Separator).TrimStartAndEnd();
			const FString Response = Entry.Mid(Separator + 1).TrimStartAndEnd();
			if (Pattern.IsEmpty() || Response.IsEmpty())
			{
				UE_LOG(LogMCPBridge, Warning,
					TEXT("[UE-MCP] Dialog policy entry '%s' names an empty pattern or response and was ignored."), *Entry);
				continue;
			}

			TSharedPtr<FJsonObject> PolicyParams = MakeShared<FJsonObject>();
			PolicyParams->SetStringField(TEXT("pattern"), Pattern);
			PolicyParams->SetStringField(TEXT("response"), Response);
			const TSharedPtr<FJsonValue> PolicyResult = Registry.ExecuteHandler(TEXT("set_dialog_policy"), PolicyParams);

			// The handler refuses a response keyword it does not recognise, and
			// its error names the valid ones. Reporting "applied" regardless
			// would leave a launch believing a prompt is answered when nothing
			// is armed for it, which is the failure this whole path exists to
			// prevent.
			bool bApplied = false;
			FString PolicyError;
			if (PolicyResult.IsValid() && PolicyResult->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject>& PolicyObject = PolicyResult->AsObject();
				bool bSuccess = false;
				bApplied = PolicyObject->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
				PolicyObject->TryGetStringField(TEXT("error"), PolicyError);
			}

			if (bApplied)
			{
				UE_LOG(LogMCPBridge, Log,
					TEXT("[UE-MCP] Startup dialog policy applied: '%s' answers '%s'"), *Pattern, *Response);
			}
			else
			{
				UE_LOG(LogMCPBridge, Warning,
					TEXT("[UE-MCP] Startup dialog policy '%s' was NOT applied: %s"),
					*Entry, PolicyError.IsEmpty() ? TEXT("the set_dialog_policy handler returned no success flag") : *PolicyError);
			}
		}
	}
}

void FUE_MCP_BridgeModule::StartupModule()
{
	// Create and start bridge server. The base port is derived per-worktree
	// from the project root path, unless something pins it: -MCPPort,
	// UE_MCP_PORT, or `bridge.port` in the project's ue-mcp.yml layers, in the
	// order the client uses (#819). Deriving lets multiple checkouts run
	// side-by-side without colliding; the probe loop in Run() resolves the rare
	// clash and publishes the actual bound port to the per-project lockfile.
	const FMCPBridgePortChoice PortChoice = FMCPBridgeServer::ResolveConfiguredPort();
	G_BridgeServer = MakeShared<FMCPBridgeServer>(PortChoice.Port, PortChoice.Source, PortChoice.bPinned);

	// The snapshot has been publishing since PostConfigInit, from the
	// UE_MCP_BridgeStatus module. Now that Slate, the shader compiler and the
	// asset compiler exist, hand it the sensors that need them.
	FMCPEngineStatusHooks::Install();
	FMCPEngineStatus::Get().SetPhase(TEXT("bridge starting"));

	FDialogHandlers::InstallDialogHook();

	// NO POLICIES ARE ARMED HERE, AND NONE MAY BE.
	//
	// This module used to arm its own at load time - "Save Content", "Save
	// Changes", "Unsaved", "Untitled", "save the level", "already exists",
	// "Overwrite" - each with a response that pressed a button. That is the
	// module deciding, before anything has happened, how to answer a question
	// nobody has read yet, and on a save prompt the answer it had chosen threw
	// somebody's unsaved work away. Gating them on "a bridge request is in
	// flight" narrowed when it happened without changing what it was.
	//
	// A dialog is a question for a person. The bridge's job is to SURFACE it -
	// title, full message, every button - through editor(list_dialogs) and the
	// status snapshot, so a caller can read it and answer it deliberately with
	// editor(respond_to_dialog). Pressing a button is only ever something a
	// caller asked for by name.
	//
	// #968: policies the LAUNCHER asked for are the one thing applied here, and
	// they are applied by dispatching set_dialog_policy, so they are recorded as
	// what they are: a caller arming an answer in advance, before the socket is
	// listening, for a prompt they already know is coming.
	ApplyConfiguredDialogPolicies(G_BridgeServer->GetHandlerRegistry());

	if (G_BridgeServer->Start())
	{
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge server starting on base port %d (%s)"), PortChoice.Port, *PortChoice.Source);
	}
	else
	{
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Failed to start bridge server"));
	}

	// Defer the editor-ready signal until GEditor is available and has at least one world.
	// GetEditorWorldContext(false) can fail if no editor world context exists yet,
	// so we iterate all world contexts instead (#162).
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([](float) -> bool
		{
			if (!GEditor)
			{
				return true; // keep ticking - not ready yet
			}

			// Accept any world context (editor or PIE) as proof the editor is usable.
			bool bHasWorld = false;
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.World())
				{
					bHasWorld = true;
					break;
				}
			}
			if (!bHasWorld)
			{
				return true; // keep ticking
			}

			if (G_BridgeServer.IsValid())
			{
				G_BridgeServer->GetGameThreadExecutor().SetEditorReady();
				UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Editor ready - accepting requests"));
			}
			FMCPEngineStatus::Get().SetPhase(TEXT("ready"));

			return false; // done
		})
	);
}

void FUE_MCP_BridgeModule::ShutdownModule()
{
	FDialogHandlers::RemoveDialogHook();
	// The snapshot itself outlives this module (its own module owns it and
	// keeps publishing until PostConfigInit teardown); only the Slate and
	// Engine sensors go away with us.
	FMCPEngineStatusHooks::Remove();

	if (G_BridgeServer.IsValid())
	{
		G_BridgeServer->Shutdown();
		G_BridgeServer.Reset();
		UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Bridge server stopped"));
	}
}
