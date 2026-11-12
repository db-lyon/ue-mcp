#pragma once

// The seam between the widget(*viewmodel*) handlers and the UMG Viewmodel
// (ModelViewViewModel) plugin.
//
// That plugin is off by default and a project can leave it off, so this module
// never links it: a module that imported its DLLs would fail to load in every
// project without it. The MVVM authoring code lives in UE_MCP_BridgeMVVM, which
// the descriptor declares with LoadingPhase "None" so nothing loads it at
// startup. The widget handlers load it on first use, only after the plugin
// manager says ModelViewViewModel is enabled, and it registers itself here.
//
// The handlers stay in this module: they declare the specs, read every
// parameter (the read tracking is per module, so a read in the other DLL would
// not be seen), resolve the Widget Blueprint, and turn an FMCPMVVMOutcome into
// the response with its idempotency and rollback fields. The backend only does
// the MVVM work.

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UWidgetBlueprint;

/** What one MVVM operation did. The handler maps it onto the response envelope. */
struct FMCPMVVMOutcome
{
	enum class EChange : uint8
	{
		/** A read. */
		None,
		/** Something was added. */
		Created,
		/** The requested state already held; nothing was written. */
		Existed,
		/** Something was changed in place. */
		Updated,
		/** Something was removed. */
		Deleted,
		/** The thing to remove was not there. */
		AlreadyDeleted,
	};

	/** False when the operation could not do what it was asked. Error says why. */
	bool bOk = true;
	FString Error;

	EChange Change = EChange::None;

	/** Fields copied onto the response. */
	TSharedPtr<FJsonObject> Data;

	/** The inverse call, when the operation changed something that has one. */
	FString RollbackMethod;
	TSharedPtr<FJsonObject> RollbackPayload;

	/** Why there is no inverse, when the operation changed something that has none. */
	FString NoRollbackReason;

	/** The Widget Blueprint was changed and has to be saved. */
	bool bModified = false;

	static FMCPMVVMOutcome Failure(const FString& InError)
	{
		FMCPMVVMOutcome Out;
		Out.bOk = false;
		Out.Error = InError;
		return Out;
	}
};

/** The MVVM authoring operations, implemented by the UE_MCP_BridgeMVVM module. */
class IMCPMVVMBackend
{
public:
	virtual ~IMCPMVVMBackend() = default;

	/**
	 * Run one operation on a resolved Widget Blueprint. Operation is the bridge
	 * method name (list_widget_viewmodels, add_widget_viewmodel, ...). Params is
	 * the dispatch's parameter object; the handler has already read every key.
	 */
	virtual FMCPMVVMOutcome Run(const FString& Operation, UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonObject>& Params) = 0;
};

namespace UEMCP
{
	/** Called by UE_MCP_BridgeMVVM on startup with its backend, and with null on shutdown. */
	UE_MCP_BRIDGE_API void SetMVVMBackend(IMCPMVVMBackend* Backend);

	/** The registered backend, or null when the module has not been loaded. */
	UE_MCP_BRIDGE_API IMCPMVVMBackend* GetMVVMBackend();
}
