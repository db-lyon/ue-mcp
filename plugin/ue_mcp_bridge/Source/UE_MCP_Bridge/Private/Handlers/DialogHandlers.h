#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"

class SButton;
class SWindow;

class FDialogHandlers
{
public:
	static void RegisterHandlers(class FMCPHandlerRegistry& Registry);

	// Call once during module startup to hook FCoreDelegates::ModalMessageDialog
	static void InstallDialogHook();

	// Call during module shutdown to restore the original delegate
	static void RemoveDialogHook();

	/**
	 * Answer the modal WINDOW currently blocking the editor from the armed
	 * policies, by pressing one of its buttons.
	 *
	 * EVERY policy this can act on was armed by a caller through
	 * set_dialog_policy. There is deliberately no way to add one from inside
	 * the module: a dialog is a question for a person, and the only button this
	 * plugin ever presses is one somebody named.
	 *
	 * This is the second half of the policy mechanism and the one that was
	 * missing. FCoreDelegates::ModalMessageDialog only carries dialogs raised
	 * through FMessageDialog::Open; the editor's own Slate modals never touch
	 * that delegate, so no policy could ever answer one. The shutdown "Save
	 * Content" prompt is exactly that shape: FileHelpers builds it through
	 * FPackagesDialogModule (Editor/UnrealEd/Public/FileHelpers.h names the
	 * title NSLOCTEXT("PackagesDialogModule", "PackagesDialogTitle", "Save
	 * Content")) and shows it with FSlateApplication::AddModalWindow, and its
	 * buttons are Save Selected / Don't Save / Cancel, which are not
	 * EAppReturnType values at all.
	 *
	 * Called from Slate's modal-loop tick, which keeps firing while the game
	 * thread sits inside the modal loop. Game thread only. Returns true when a
	 * button was actually pressed.
	 */
	static bool ApplyPolicyToActiveModal();

	/**
	 * Describe the modal window currently blocking the editor, if any. Game
	 * thread only (it walks the Slate widget tree). Shared with
	 * FMCPEngineStatus, which captures the same description into its snapshot
	 * so an out-of-band caller can see the dialog that is blocking the very
	 * request it is waiting on.
	 */
	static bool DescribeActiveModal(FString& OutTitle, FString& OutMessage, TArray<FString>& OutButtons);

	/**
	 * One modal's buttons, in the order Slate laid them out. Public only so the
	 * file-local label matchers in DialogHandlers.cpp can name it; nothing
	 * outside that file uses it.
	 */
	struct FModalButton
	{
		TSharedPtr<SButton> Button;
		FString Label;
	};

private:
	// Dialog policy: pattern -> response mapping
	struct FDialogPolicy
	{
		FString Pattern;
		EAppReturnType::Type Response = EAppReturnType::Ok;
		/**
		 * A literal button label to press on a Slate modal. The response
		 * keyword cannot name every button an editor dialog offers - there is
		 * no EAppReturnType for "Don't Save" - so a policy may carry the label
		 * instead. Empty means "resolve the button from Response".
		 */
		FString ButtonLabel;
	};

	// Handler implementations
	static TSharedPtr<FJsonValue> SetDialogPolicy(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ClearDialogPolicy(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> GetDialogPolicy(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> ListDialogs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonValue> RespondToDialog(const TSharedPtr<FJsonObject>& Params);

	// The hooked dialog handler
	static EAppReturnType::Type HandleModalDialog(EAppMsgType::Type MsgType, const FText& Text, const FText& Title);
	// FCoreDelegates::ModalMessageDialog (UE 5.7) signature includes an EAppMsgCategory.
	static EAppReturnType::Type HandleModalDialogV2(enum EAppMsgCategory Category, EAppMsgType::Type MsgType, const FText& Text, const FText& Title);

	// Convert string to EAppReturnType. bOutValid reports whether the keyword
	// was recognised, so a typo can be refused instead of silently meaning Ok.
	static EAppReturnType::Type ParseResponseType(const FString& ResponseStr, bool& bOutValid);
	static FString ValidResponseList();
	static FString ResponseTypeToString(EAppReturnType::Type Response);
	static FString MsgTypeToString(EAppMsgType::Type MsgType);

	// Shared modal walk. One traversal answers DescribeActiveModal,
	// list_dialogs, respond_to_dialog and the policy applier, so the four
	// cannot disagree about which buttons a dialog has.
	static TSharedPtr<SWindow> CollectActiveModal(FString& OutTitle, FString& OutMessage, TArray<FModalButton>& OutButtons);

	/** First policy whose pattern appears in the title or the message. */
	static const FDialogPolicy* FindMatchingPolicy(const FString& Title, const FString& Message);

	/**
	 * Index of the button a policy resolves to, or INDEX_NONE with a reason.
	 * Never guesses: a policy that names nothing on the dialog presses nothing.
	 */
	static int32 ResolveButtonForPolicy(const FDialogPolicy& Policy, const TArray<FModalButton>& Buttons, FString& OutReason);

	// Active policies
	static TArray<FDialogPolicy> Policies;

	// Original delegate handle (so we can unbind)
	static FDelegateHandle OriginalDelegateHandle;

	// Whether we've installed our hook
	static bool bHookInstalled;
};
