// Dialog policy and dialog answering.
//
// THE ONLY BUTTON THIS PLUGIN EVER PRESSES IS ONE A CALLER NAMED.
//
// A modal dialog is a question for a person. Nothing in this module arms a
// policy of its own, and nothing invents an answer so that a request can carry
// on: when a dialog goes up with no policy armed for it, it stays up, the
// status snapshot publishes it, and list_dialogs reports its full text and
// every button so somebody can read the question and press a button with
// respond_to_dialog. A caller who genuinely wants a prompt answered without
// seeing it arms set_dialog_policy for it in advance, which is their decision
// and is recorded as such.
//
// THERE ARE TWO KINDS OF MODAL AND ONLY ONE OF THEM IS A MESSAGE DIALOG.
//
// FMessageDialog::Open routes through FCoreDelegates::ModalMessageDialog when
// something is bound to it, so HandleModalDialog below can apply an armed
// policy to those without a window ever reaching the screen.
//
// It covers nothing else. The editor's own modals are ordinary Slate windows
// shown with FSlateApplication::AddModalWindow, and they never touch that
// delegate. The shutdown "Save Content" prompt is the important one: FileHelpers
// builds it through FPackagesDialogModule, its title is
// NSLOCTEXT("PackagesDialogModule", "PackagesDialogTitle", "Save Content")
// (Editor/UnrealEd/Public/FileHelpers.h), and its buttons are Save Selected /
// Don't Save / Cancel. None of those are EAppReturnType values, and the delegate
// is never called, so a policy armed against it could never fire no matter how
// well its pattern matched, and a caller was told their policy was armed while
// nothing could ever act on it.
//
// ApplyPolicyToActiveModal is the missing half: it presses a real button on a
// real modal window, from Slate's modal-loop tick, which keeps running while
// the game thread is parked inside the modal loop. It runs only for a policy a
// caller armed, because that is the only kind there is.

#include "DialogHandlers.h"
#include "UE_MCP_BridgeModule.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Misc/CoreDelegates.h"
#include "Misc/MessageDialog.h"     // #603 re-show real dialog
#include "GenericPlatform/GenericPlatformMisc.h" // EAppMsgCategory
#include "HAL/PlatformTime.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"

// Static member definitions
TArray<FDialogHandlers::FDialogPolicy> FDialogHandlers::Policies;
FDelegateHandle FDialogHandlers::OriginalDelegateHandle;
bool FDialogHandlers::bHookInstalled = false;

// A NAMED namespace, not an anonymous one: this module is a unity build, so an
// anonymous namespace here merges with whatever other .cpp shares the blob and
// a same-named helper elsewhere becomes a C2084 redefinition.
namespace MCPDialogPolicy
{
	/**
	 * Compare button labels the way a human reads them. Slate ships the
	 * typographic apostrophe in "Don't Save" in some builds while a caller
	 * types the ASCII one, and a label may carry padding.
	 */
	static FString Normalise(const FString& In)
	{
		static const FString CurlyApostrophe = FString::Chr(TCHAR(0x2019));
		FString Out = In.Replace(*CurlyApostrophe, TEXT("'"));
		Out.TrimStartAndEndInline();
		return Out.ToLower();
	}

	/**
	 * Button labels a response keyword is willing to press, most preferred
	 * first.
	 *
	 * This is where the vocabulary problem is solved. EAppReturnType has eight
	 * values and an editor dialog can offer any label it likes, so "no" on a
	 * Save Content prompt has to be allowed to mean "Don't Save" or the keyword
	 * names nothing on the dialog at all. Extending EAppReturnType was not an
	 * option: the FMessageDialog path has to RETURN one of its values, so a
	 * "dontsave" keyword would have nothing to return. A policy that needs to
	 * name a button precisely carries a literal buttonLabel instead.
	 */
	static const TArray<FString>& SynonymsFor(EAppReturnType::Type Response)
	{
		static const TArray<FString> Yes      = { TEXT("Yes"), TEXT("Save Selected"), TEXT("Save All"), TEXT("Save") };
		static const TArray<FString> No       = { TEXT("No"), TEXT("Don't Save"), TEXT("Do Not Save"), TEXT("Discard"), TEXT("Skip") };
		static const TArray<FString> Ok       = { TEXT("OK"), TEXT("Continue"), TEXT("Yes") };
		static const TArray<FString> Cancel   = { TEXT("Cancel"), TEXT("Abort"), TEXT("Close") };
		static const TArray<FString> Retry    = { TEXT("Retry"), TEXT("Try Again") };
		static const TArray<FString> Continue = { TEXT("Continue"), TEXT("OK") };
		static const TArray<FString> YesAll   = { TEXT("Yes to All"), TEXT("Yes All"), TEXT("Save All"), TEXT("Yes") };
		static const TArray<FString> NoAll    = { TEXT("No to All"), TEXT("No All"), TEXT("Don't Save"), TEXT("No") };
		static const TArray<FString> Empty;

		switch (Response)
		{
		case EAppReturnType::Yes:      return Yes;
		case EAppReturnType::No:       return No;
		case EAppReturnType::Ok:       return Ok;
		case EAppReturnType::Cancel:   return Cancel;
		case EAppReturnType::Retry:    return Retry;
		case EAppReturnType::Continue: return Continue;
		case EAppReturnType::YesAll:   return YesAll;
		case EAppReturnType::NoAll:    return NoAll;
		default:                       return Empty;
		}
	}

	/** Index of the first button whose label equals Needle once normalised. */
	static int32 FindExact(const TArray<FDialogHandlers::FModalButton>& Buttons, const FString& Needle)
	{
		const FString Want = Normalise(Needle);
		for (int32 i = 0; i < Buttons.Num(); ++i)
		{
			if (Normalise(Buttons[i].Label) == Want)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	/** Index of the first button whose label contains Needle once normalised. */
	static int32 FindContains(const TArray<FDialogHandlers::FModalButton>& Buttons, const FString& Needle)
	{
		const FString Want = Normalise(Needle);
		if (Want.IsEmpty())
		{
			return INDEX_NONE;
		}
		for (int32 i = 0; i < Buttons.Num(); ++i)
		{
			if (Normalise(Buttons[i].Label).Contains(Want))
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	static FString JoinLabels(const TArray<FDialogHandlers::FModalButton>& Buttons)
	{
		TArray<FString> Labels;
		for (const FDialogHandlers::FModalButton& B : Buttons)
		{
			Labels.Add(B.Label);
		}
		return FString::Join(Labels, TEXT(", "));
	}

	// The modal this process last pressed a button on, and when. The modal-loop
	// tick fires many times a second and a window does not close on the frame
	// its button is pressed, so without this the policy would press again and
	// again. The pointer is an identity token only and is NEVER dereferenced;
	// the timestamp is what lets a genuinely new dialog through if the
	// allocator happens to hand back the same address.
	static const SWindow* LastAnsweredWindow = nullptr;
	static double LastAnsweredSeconds = 0.0;
	static const double AnswerCooldownSeconds = 2.0;

	// A modal we deliberately left alone, so the reason is logged once rather
	// than every frame of the modal loop.
	static const SWindow* LastDeclinedWindow = nullptr;
}

void FDialogHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	Registry.RegisterHandler(TEXT("set_dialog_policy"), &SetDialogPolicy);
	Registry.RegisterHandler(TEXT("clear_dialog_policy"), &ClearDialogPolicy);
	Registry.RegisterHandler(TEXT("get_dialog_policy"), &GetDialogPolicy);
	Registry.RegisterHandler(TEXT("list_dialogs"), &ListDialogs);
	Registry.RegisterHandler(TEXT("respond_to_dialog"), &RespondToDialog);
}

void FDialogHandlers::InstallDialogHook()
{
	if (bHookInstalled)
	{
		return;
	}

	// UE 5.7 routes FMessageDialog::Open through FCoreDelegates::ModalMessageDialog
	// when bound. Bind our handler so a policy a caller armed with
	// set_dialog_policy can answer the prompt they armed it for. A prompt nobody
	// armed anything for is handed straight back to the real dialog.
	//
	// This delegate is only half the coverage. Slate modal WINDOWS never reach
	// it; ApplyPolicyToActiveModal answers those, driven from the modal-loop
	// tick hook installed by FMCPEngineStatusHooks.
	FCoreDelegates::ModalMessageDialog.BindStatic(&FDialogHandlers::HandleModalDialogV2);
	bHookInstalled = true;

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Dialog hook installed (ModalMessageDialog delegate bound)"));
}

void FDialogHandlers::RemoveDialogHook()
{
	if (!bHookInstalled)
	{
		return;
	}

	FCoreDelegates::ModalMessageDialog.Unbind();
	bHookInstalled = false;
	Policies.Empty();

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Dialog hook removed"));
}

EAppReturnType::Type FDialogHandlers::HandleModalDialogV2(EAppMsgCategory /*Category*/, EAppMsgType::Type MsgType, const FText& Text, const FText& Title)
{
	return HandleModalDialog(MsgType, Text, Title);
}

auto FDialogHandlers::FindMatchingPolicy(const FString& Title, const FString& Message) -> const FDialogPolicy*
{
	// FString::Contains is case-insensitive by default, which is what the
	// FMessageDialog path has always done; both halves match the same way.
	for (const FDialogPolicy& Policy : Policies)
	{
		if (Policy.Pattern.IsEmpty())
		{
			continue;
		}
		if (Title.Contains(Policy.Pattern) || Message.Contains(Policy.Pattern))
		{
			return &Policy;
		}
	}
	return nullptr;
}

int32 FDialogHandlers::ResolveButtonForPolicy(const FDialogPolicy& Policy, const TArray<FModalButton>& Buttons, FString& OutReason)
{
	using namespace MCPDialogPolicy;

	OutReason.Empty();
	if (Buttons.Num() == 0)
	{
		OutReason = TEXT("the dialog exposes no button this walk can press");
		return INDEX_NONE;
	}

	// A literal label is the precise instrument and wins outright. Exact before
	// substring, because "Save" is a substring of "Don't Save" and a substring
	// hit on the wrong button is the one mistake that cannot be undone.
	if (!Policy.ButtonLabel.IsEmpty())
	{
		int32 Index = FindExact(Buttons, Policy.ButtonLabel);
		if (Index == INDEX_NONE)
		{
			Index = FindContains(Buttons, Policy.ButtonLabel);
		}
		if (Index == INDEX_NONE)
		{
			OutReason = FString::Printf(
				TEXT("buttonLabel '%s' matches none of [%s]"),
				*Policy.ButtonLabel, *JoinLabels(Buttons));
		}
		return Index;
	}

	const TArray<FString>& Synonyms = SynonymsFor(Policy.Response);
	for (const FString& Synonym : Synonyms)
	{
		const int32 Index = FindExact(Buttons, Synonym);
		if (Index != INDEX_NONE)
		{
			return Index;
		}
	}
	for (const FString& Synonym : Synonyms)
	{
		const int32 Index = FindContains(Buttons, Synonym);
		if (Index != INDEX_NONE)
		{
			return Index;
		}
	}

	OutReason = FString::Printf(
		TEXT("response '%s' names none of [%s]; set the policy with a literal buttonLabel to press one of them"),
		*ResponseTypeToString(Policy.Response), *JoinLabels(Buttons));
	return INDEX_NONE;
}

bool FDialogHandlers::ApplyPolicyToActiveModal()
{
	using namespace MCPDialogPolicy;

	if (!IsInGameThread() || !FSlateApplication::IsInitialized())
	{
		return false;
	}

	FString Title;
	FString Message;
	TArray<FModalButton> Buttons;
	TSharedPtr<SWindow> Modal = CollectActiveModal(Title, Message, Buttons);
	if (!Modal.IsValid())
	{
		LastAnsweredWindow = nullptr;
		LastDeclinedWindow = nullptr;
		return false;
	}

	const SWindow* WindowId = Modal.Get();
	const double Now = FPlatformTime::Seconds();
	if (WindowId == LastAnsweredWindow && (Now - LastAnsweredSeconds) < AnswerCooldownSeconds)
	{
		return false;
	}

	const FDialogPolicy* Policy = FindMatchingPolicy(Title, Message);
	if (!Policy)
	{
		return false;
	}

	// Every policy in the list was armed by a caller through set_dialog_policy,
	// so a match here is somebody's standing instruction being carried out.
	// Nothing else can put a policy in the list, which is what keeps this from
	// pressing a button on a dialog nobody asked about.
	FString Reason;
	const int32 Index = ResolveButtonForPolicy(*Policy, Buttons, Reason);
	if (Index == INDEX_NONE)
	{
		if (WindowId != LastDeclinedWindow)
		{
			LastDeclinedWindow = WindowId;
			UE_LOG(LogMCPBridge, Warning,
				TEXT("[UE-MCP] Modal '%s' matched policy '%s' but no button was pressed: %s"),
				*Title, *Policy->Pattern, *Reason);
		}
		return false;
	}

	LastAnsweredWindow = WindowId;
	LastAnsweredSeconds = Now;
	LastDeclinedWindow = nullptr;

	UE_LOG(LogMCPBridge, Log,
		TEXT("[UE-MCP] Modal '%s' answered by policy '%s': pressed '%s' of [%s]"),
		*Title, *Policy->Pattern, *Buttons[Index].Label, *JoinLabels(Buttons));

	// SimulateClick, for the same reason respond_to_dialog uses it: synthetic
	// mouse events bypass the capture bookkeeping SButton uses to decide a
	// click happened, and the dialog stays on screen while the caller is told
	// it was answered.
	FSlateApplication::Get().SetKeyboardFocus(Buttons[Index].Button);
	Buttons[Index].Button->SimulateClick();
	return true;
}

EAppReturnType::Type FDialogHandlers::HandleModalDialog(EAppMsgType::Type MsgType, const FText& Text, const FText& Title)
{
	FString MessageStr = Text.ToString();
	FString TitleStr = Title.ToString();

	// A policy a caller armed is a standing instruction, so it answers.
	for (const FDialogPolicy& Policy : Policies)
	{
		if (Policy.Pattern.IsEmpty())
		{
			continue;
		}
		if (MessageStr.Contains(Policy.Pattern) || TitleStr.Contains(Policy.Pattern))
		{
			UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Dialog answered by the policy a caller armed: pattern='%s' title='%s' response=%s"),
				*Policy.Pattern, *TitleStr, *ResponseTypeToString(Policy.Response));
			return Policy.Response;
		}
	}

	// Nothing a caller armed matches, so nobody has said how to answer this and
	// this module does not get to decide. Detach the hook and re-show the real
	// dialog.
	//
	// #603 did this only for a dialog the person at the keyboard raised, and
	// SYNTHESIZED an answer for one a bridge request raised, so the request would
	// not block. That synthesis was the bug in its purest form: on a save prompt
	// the invented answer was "No", which discards, and the caller was told the
	// request succeeded while a question they never saw had been answered for
	// them. Blocking is the honest outcome. The dialog is now on screen, the
	// status snapshot publishes its title and buttons, and editor(list_dialogs)
	// and editor(respond_to_dialog) both run while the game thread is parked
	// inside the modal loop, so the caller can read the exact question and press
	// the button they choose.
	UE_LOG(LogMCPBridge, Warning,
		TEXT("[UE-MCP] Modal dialog raised with no policy armed for it, so it is left for a person to answer: title='%s' message='%s'. Read it with editor(list_dialogs) and answer it with editor(respond_to_dialog)."),
		*TitleStr, *MessageStr);
	FCoreDelegates::ModalMessageDialog.Unbind();
	const EAppReturnType::Type UserAnswer = FMessageDialog::Open(MsgType, Text, Title);
	// Reattach for subsequent dialogs.
	FCoreDelegates::ModalMessageDialog.BindStatic(&FDialogHandlers::HandleModalDialogV2);
	return UserAnswer;
}

TSharedPtr<FJsonValue> FDialogHandlers::SetDialogPolicy(const TSharedPtr<FJsonObject>& Params)
{
	FString Pattern;
	if (!Params->TryGetStringField(TEXT("pattern"), Pattern) || Pattern.IsEmpty())
	{
		return MCPError(TEXT("Missing or empty 'pattern' parameter. It is matched case-insensitively against the dialog title and message."));
	}

	const FString ButtonLabel = OptionalString(Params, TEXT("buttonLabel"));
	FString ResponseStr = OptionalString(Params, TEXT("response"));

	if (ResponseStr.IsEmpty() && ButtonLabel.IsEmpty())
	{
		return MCPError(FString::Printf(
			TEXT("A policy needs a 'response' or a 'buttonLabel'. Valid responses are %s. Use 'buttonLabel' for a dialog whose buttons are not those, such as the shutdown 'Save Content' prompt whose buttons are Save Selected, Don't Save and Cancel."),
			*ValidResponseList()));
	}

	// A response keyword is still required for the FMessageDialog path, which
	// has to RETURN an EAppReturnType and cannot press a label. A policy given
	// only a buttonLabel gets Cancel there, which is the non-destructive answer.
	EAppReturnType::Type Response = EAppReturnType::Cancel;
	if (!ResponseStr.IsEmpty())
	{
		bool bValidResponse = false;
		Response = ParseResponseType(ResponseStr, bValidResponse);
		if (!bValidResponse)
		{
			return MCPError(FString::Printf(
				TEXT("Unknown response '%s'. Valid responses are %s."),
				*ResponseStr, *ValidResponseList()));
		}
	}

	// Idempotency: an identical policy is already armed.
	for (const FDialogPolicy& P : Policies)
	{
		if (P.Pattern == Pattern && P.Response == Response && P.ButtonLabel == ButtonLabel)
		{
			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			Existed->SetStringField(TEXT("pattern"), Pattern);
			Existed->SetStringField(TEXT("response"), ResponseTypeToString(Response));
			Existed->SetStringField(TEXT("buttonLabel"), ButtonLabel);
			Existed->SetNumberField(TEXT("policyCount"), Policies.Num());
			// Still worth trying: the policy may have been armed before the
			// modal that it matches came up.
			Existed->SetBoolField(TEXT("answeredActiveModal"), ApplyPolicyToActiveModal());
			return MCPResult(Existed);
		}
	}

	// Remove existing policy with same pattern
	Policies.RemoveAll([&Pattern](const FDialogPolicy& P) { return P.Pattern == Pattern; });

	// Add new policy
	FDialogPolicy NewPolicy;
	NewPolicy.Pattern = Pattern;
	NewPolicy.Response = Response;
	NewPolicy.ButtonLabel = ButtonLabel;
	// Appended, because matching is first-wins and every policy in the list was
	// armed by a caller: two calls in a row must keep the order they were made
	// in rather than reversing it.
	Policies.Add(NewPolicy);

	// Said out loud in the editor's own log, because from here on a prompt
	// matching this pattern is answered and dismissed and the person at the
	// keyboard never sees it. That is what the caller asked for, and it should
	// leave a trace they can find afterwards.
	const FString ButtonNote = ButtonLabel.IsEmpty()
		? FString()
		: FString::Printf(TEXT(" (button '%s')"), *ButtonLabel);
	UE_LOG(LogMCPBridge, Warning,
		TEXT("[UE-MCP] A caller armed a dialog policy: any dialog matching '%s' will be answered with '%s'%s and dismissed WITHOUT being shown. Clear it with editor(clear_dialog_policy)."),
		*Pattern, *ResponseTypeToString(Response), *ButtonNote);

	// Ensure hook is installed
	if (!bHookInstalled)
	{
		InstallDialogHook();
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("pattern"), Pattern);
	Result->SetStringField(TEXT("response"), ResponseTypeToString(Response));
	Result->SetStringField(TEXT("buttonLabel"), ButtonLabel);
	Result->SetNumberField(TEXT("policyCount"), Policies.Num());

	// A policy armed while a modal is ALREADY up has to answer it now. This
	// handler is modal-safe, so it runs from inside the modal loop, which is
	// precisely the case where nothing else will get a chance to.
	Result->SetBoolField(TEXT("answeredActiveModal"), ApplyPolicyToActiveModal());

	// Rollback: clear_dialog_policy with same pattern
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("pattern"), Pattern);
	MCPSetRollback(Result, TEXT("clear_dialog_policy"), Payload);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FDialogHandlers::ClearDialogPolicy(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();

	FString Pattern = OptionalString(Params, TEXT("pattern"));

	// Copied BEFORE the removal. Re-arming a policy needs the response and the
	// button label it was armed with, and both are gone the moment it leaves
	// the list, so a rollback captured afterwards would re-arm the pattern with
	// a different answer than the one that was cleared.
	TArray<FDialogPolicy> Cleared;
	if (!Pattern.IsEmpty())
	{
		for (const FDialogPolicy& P : Policies)
		{
			if (P.Pattern == Pattern) Cleared.Add(P);
		}
		Policies.RemoveAll([&Pattern](const FDialogPolicy& P) { return P.Pattern == Pattern; });
		Result->SetStringField(TEXT("pattern"), Pattern);
	}
	else
	{
		Cleared = Policies;
		Policies.Empty();
	}

	Result->SetNumberField(TEXT("removed"), Cleared.Num());
	Result->SetNumberField(TEXT("policyCount"), Policies.Num());

	// Clearing a pattern nothing was armed for is a no-op, and saying so is the
	// difference between "your policy is gone" and "there was never one".
	Result->SetBoolField(TEXT("unchanged"), Cleared.Num() == 0);

	// Every policy that was actually removed, in the form set_dialog_policy
	// takes back. A caller that cleared several can re-arm them from this
	// without having read get_dialog_policy first.
	TArray<TSharedPtr<FJsonValue>> ClearedJson;
	for (const FDialogPolicy& P : Cleared)
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("pattern"), P.Pattern);
		Entry->SetStringField(TEXT("response"), ResponseTypeToString(P.Response));
		Entry->SetStringField(TEXT("buttonLabel"), P.ButtonLabel);
		ClearedJson.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("cleared"), ClearedJson);

	if (Cleared.Num() == 1)
	{
		// A cleared policy has a real inverse: arm it again exactly as it was.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("pattern"), Cleared[0].Pattern);
		Payload->SetStringField(TEXT("response"), ResponseTypeToString(Cleared[0].Response));
		Payload->SetStringField(TEXT("buttonLabel"), Cleared[0].ButtonLabel);
		MCPSetRollback(Result, TEXT("set_dialog_policy"), Payload);
	}
	else if (Cleared.Num() == 0)
	{
		MCPSetNoRollback(Result,
			TEXT("Nothing was armed for that pattern, so no policy was removed and there is nothing to put back. "
			     "set_dialog_policy is what arms one."));
	}
	else
	{
		MCPSetNoRollback(Result, FString::Printf(
			TEXT("%d policies were cleared, and a rollback record carries one call. set_dialog_policy arms a single "
			     "pattern, so putting these back takes one call per entry in 'cleared', which lists each pattern with "
			     "the response and buttonLabel it was armed with."),
			Cleared.Num()));
	}

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FDialogHandlers::GetDialogPolicy(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();

	TArray<TSharedPtr<FJsonValue>> PoliciesArray;
	for (const FDialogPolicy& Policy : Policies)
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("pattern"), Policy.Pattern);
		P->SetStringField(TEXT("response"), ResponseTypeToString(Policy.Response));
		P->SetStringField(TEXT("buttonLabel"), Policy.ButtonLabel);
		// Which policies answer a Slate modal that no bridge request raised.
		// Constant on purpose. A policy can only get into this list through
		// set_dialog_policy, so there is no other source to report; the module
		// arms none of its own.
		P->SetStringField(TEXT("source"), TEXT("caller"));
		PoliciesArray.Add(MakeShared<FJsonValueObject>(P));
	}

	Result->SetArrayField(TEXT("policies"), PoliciesArray);
	Result->SetNumberField(TEXT("count"), Policies.Num());
	Result->SetBoolField(TEXT("hookInstalled"), bHookInstalled);

	return MCPResult(Result);
}

TSharedPtr<SWindow> FDialogHandlers::CollectActiveModal(FString& OutTitle, FString& OutMessage, TArray<FModalButton>& OutButtons)
{
	OutTitle.Empty();
	OutMessage.Empty();
	OutButtons.Empty();

	if (!FSlateApplication::IsInitialized())
	{
		return nullptr;
	}

	TSharedPtr<SWindow> ActiveModal = FSlateApplication::Get().GetActiveModalWindow();
	if (!ActiveModal.IsValid())
	{
		return nullptr;
	}

	OutTitle = ActiveModal->GetTitle().ToString();

	TArray<FString> TextContents;

	TFunction<void(const TSharedRef<SWidget>&)> TraverseWidgets = [&](const TSharedRef<SWidget>& Widget)
	{
		if (Widget->GetType() == TEXT("STextBlock"))
		{
			TSharedRef<STextBlock> TextBlock = StaticCastSharedRef<STextBlock>(Widget);
			FString Text = TextBlock->GetText().ToString();
			if (!Text.IsEmpty())
			{
				TextContents.Add(Text);
			}
		}

		if (Widget->GetType() == TEXT("SButton"))
		{
			FModalButton Entry;
			Entry.Button = StaticCastSharedRef<SButton>(Widget);

			// The label is the first text block inside the button.
			FChildren* ButtonChildren = Widget->GetChildren();
			if (ButtonChildren)
			{
				for (int32 i = 0; i < ButtonChildren->Num(); ++i)
				{
					TSharedRef<SWidget> Child = ButtonChildren->GetChildAt(i);
					if (Child->GetType() == TEXT("STextBlock"))
					{
						Entry.Label = StaticCastSharedRef<STextBlock>(Child)->GetText().ToString();
						break;
					}
				}
			}
			OutButtons.Add(Entry);
		}

		FChildren* Children = Widget->GetChildren();
		if (Children)
		{
			for (int32 i = 0; i < Children->Num(); ++i)
			{
				TraverseWidgets(Children->GetChildAt(i));
			}
		}
	};

	TraverseWidgets(ActiveModal.ToSharedRef());

	for (const FString& T : TextContents)
	{
		if (T != OutTitle)
		{
			if (!OutMessage.IsEmpty()) OutMessage += TEXT("\n");
			OutMessage += T;
		}
	}

	return ActiveModal;
}

bool FDialogHandlers::DescribeActiveModal(FString& OutTitle, FString& OutMessage, TArray<FString>& OutButtons)
{
	OutButtons.Empty();

	TArray<FModalButton> Buttons;
	TSharedPtr<SWindow> Modal = CollectActiveModal(OutTitle, OutMessage, Buttons);
	if (!Modal.IsValid())
	{
		return false;
	}

	for (const FModalButton& Button : Buttons)
	{
		if (!Button.Label.IsEmpty())
		{
			OutButtons.Add(Button.Label);
		}
	}
	return true;
}

TSharedPtr<FJsonValue> FDialogHandlers::ListDialogs(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();
	TArray<TSharedPtr<FJsonValue>> DialogsArray;

	FString Title;
	FString Message;
	TArray<FModalButton> Buttons;
	if (CollectActiveModal(Title, Message, Buttons).IsValid())
	{
		TSharedPtr<FJsonObject> DialogObj = MakeShared<FJsonObject>();
		DialogObj->SetStringField(TEXT("title"), Title);
		// THE FULL TEXT, never a prefix of it. This is how a person reads what
		// the editor actually asked, and a question truncated mid-sentence is a
		// question answered on incomplete information.
		DialogObj->SetStringField(TEXT("message"), Message);
		DialogObj->SetBoolField(TEXT("messageTruncated"), false);

		// Every button, in the order Slate laid them out, each paired with the
		// exact call that presses it. No button is marked recommended and none is
		// ordered ahead of another: choosing is the caller's job, and this is the
		// list they choose from.
		TArray<TSharedPtr<FJsonValue>> ButtonsJsonArray;
		TArray<TSharedPtr<FJsonValue>> ChoicesJsonArray;
		for (const FModalButton& Button : Buttons)
		{
			if (Button.Label.IsEmpty())
			{
				continue;
			}
			ButtonsJsonArray.Add(MakeShared<FJsonValueString>(Button.Label));

			TSharedPtr<FJsonObject> Choice = MakeShared<FJsonObject>();
			Choice->SetStringField(TEXT("buttonLabel"), Button.Label);
			// "Don't Save" carries an apostrophe, so a single-quoted literal
			// would hand back a call that does not parse - on the exact button
			// where getting it wrong costs the most.
			const bool bHasApostrophe = Button.Label.Contains(TEXT("'")) || Button.Label.Contains(FString::Chr(TCHAR(0x2019)));
			const FString RespondWith = bHasApostrophe
				? FString::Printf(TEXT("editor(action='respond_to_dialog', buttonLabel=\"%s\")"), *Button.Label)
				: FString::Printf(TEXT("editor(action='respond_to_dialog', buttonLabel='%s')"), *Button.Label);
			Choice->SetStringField(TEXT("respondWith"), RespondWith);
			ChoicesJsonArray.Add(MakeShared<FJsonValueObject>(Choice));
		}
		DialogObj->SetArrayField(TEXT("buttons"), ButtonsJsonArray);
		DialogObj->SetArrayField(TEXT("choices"), ChoicesJsonArray);
		if (ChoicesJsonArray.Num() == 0)
		{
			DialogObj->SetStringField(TEXT("choicesNote"),
				TEXT("This dialog exposes no button label this walk can read. editor(action='respond_to_dialog', action='close') destroys the window, which is not the same as answering it."));
		}

		// Why an armed policy is or is not clearing this dialog. Without it the
		// caller sees a dialog, sees a matching policy, and has nothing that
		// says which of the two failed to meet the other.
		const FDialogPolicy* Policy = FindMatchingPolicy(Title, Message);
		if (!Policy)
		{
			DialogObj->SetBoolField(TEXT("policyMatched"), false);
			DialogObj->SetStringField(TEXT("policyNote"),
				TEXT("No policy is armed for this dialog, and nothing will answer it on its own. Press one of the buttons above with editor(respond_to_dialog)."));
		}
		else
		{
			FString Reason;
			const int32 Index = ResolveButtonForPolicy(*Policy, Buttons, Reason);
			DialogObj->SetBoolField(TEXT("policyMatched"), true);
			DialogObj->SetStringField(TEXT("policyPattern"), Policy->Pattern);
			DialogObj->SetStringField(TEXT("policyResponse"), ResponseTypeToString(Policy->Response));
			DialogObj->SetStringField(TEXT("policySource"), TEXT("caller"));
			if (Index != INDEX_NONE)
			{
				DialogObj->SetStringField(TEXT("policyWouldPress"), Buttons[Index].Label);
			}
			else
			{
				DialogObj->SetStringField(TEXT("policyNote"),
					FString::Printf(TEXT("The policy matches but presses nothing: %s"), *Reason));
			}
		}

		DialogsArray.Add(MakeShared<FJsonValueObject>(DialogObj));
	}

	Result->SetArrayField(TEXT("dialogs"), DialogsArray);
	Result->SetNumberField(TEXT("count"), DialogsArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FDialogHandlers::RespondToDialog(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPDialogPolicy;

	if (!FSlateApplication::IsInitialized())
	{
		return MCPError(TEXT("Slate not initialized"));
	}

	FString Title;
	FString Message;
	TArray<FModalButton> Buttons;
	TSharedPtr<SWindow> ActiveModal = CollectActiveModal(Title, Message, Buttons);
	if (!ActiveModal.IsValid())
	{
		// Still a failure: nothing was pressed on this call, and a caller
		// waiting on a particular button must not read this as done. But a
		// retry after a timeout whose click actually landed arrives here too,
		// so the reason is named rather than left to be inferred from a
		// sentence. alreadyClosed separates "the dialog is gone" from "you
		// aimed at an editor that never had one".
		TSharedPtr<FJsonObject> Gone = MakeShared<FJsonObject>();
		Gone->SetBoolField(TEXT("success"), false);
		Gone->SetStringField(TEXT("error"), TEXT("No active modal dialog"));
		Gone->SetBoolField(TEXT("alreadyClosed"), true);
		return MCPResult(Gone);
	}

	// Determine action: buttonIndex, buttonLabel, or key simulation
	FString ButtonLabel = OptionalString(Params, TEXT("buttonLabel"));
	int32 ButtonIndex = OptionalInt(Params, TEXT("buttonIndex"), -1);

	// Resolve which button to click. Exact match before substring: "Save" is a
	// substring of "Don't Save", so a substring-first search can press the
	// opposite of what was asked for on the one dialog where it matters most.
	int32 TargetIndex = -1;

	if (!ButtonLabel.IsEmpty())
	{
		TargetIndex = FindExact(Buttons, ButtonLabel);
		if (TargetIndex == INDEX_NONE)
		{
			TargetIndex = FindContains(Buttons, ButtonLabel);
		}
	}
	else if (ButtonIndex >= 0 && ButtonIndex < Buttons.Num())
	{
		TargetIndex = ButtonIndex;
	}

	auto Result = MCPSuccess();

	if (TargetIndex >= 0 && TargetIndex < Buttons.Num())
	{
		TSharedPtr<SButton> TargetButton = Buttons[TargetIndex].Button;
		FSlateApplication::Get().SetKeyboardFocus(TargetButton);

		// SButton::SimulateClick, not synthetic mouse events. Feeding
		// OnMouseButtonDown/Up straight to the widget bypasses the capture
		// bookkeeping SButton uses to decide a click happened, so the handler
		// reported a successful click while the dialog stayed on screen -
		// the worst possible failure for a call whose entire job is unblocking
		// the editor.
		TargetButton->SimulateClick();

		Result->SetStringField(TEXT("clickedButton"), Buttons[TargetIndex].Label);
		Result->SetNumberField(TEXT("buttonIndex"), TargetIndex);
		Result->SetBoolField(TEXT("alreadyClosed"), false);

		// A pressed button cannot be un-pressed. The dialog's own handler ran
		// on the press and whatever it did - saving packages, discarding them,
		// aborting an import - belongs to the editor code behind the button,
		// not to this call, which never saw it.
		MCPSetNoRollback(Result, FString::Printf(
			TEXT("Pressed '%s' on the dialog '%s'. The editor code behind that button has already run and this call "
			     "captured none of what it did, so there is nothing to restore. No action re-raises a dismissed modal "
			     "or reverses the answer given to it."),
			*Buttons[TargetIndex].Label, *Title));
	}
	else
	{
		// Last resort for a dialog with no button we can name: close the window
		// itself, which ends the modal loop and releases the game thread. A
		// synthetic Escape keypress alone does not reach a modal window that
		// never took keyboard focus, so send both.
		FString Action = OptionalString(Params, TEXT("action"));
		if (Action == TEXT("escape") || Action == TEXT("close"))
		{
			FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
			FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
			ActiveModal->RequestDestroyWindow();
			Result->SetStringField(TEXT("action"), TEXT("closed window"));
			Result->SetBoolField(TEXT("alreadyClosed"), false);

			// Destroying the window ends the modal loop without answering the
			// question, so the editor carries on down whatever path it takes
			// for an unanswered prompt.
			MCPSetNoRollback(Result, FString::Printf(
				TEXT("Destroyed the dialog window '%s' without pressing any of its buttons. The editor has already "
				     "resumed on its unanswered-prompt path and no action re-raises a destroyed modal, so the "
				     "question cannot be put back to be answered differently."),
				*Title));
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> AvailableButtons;
			for (const FModalButton& Button : Buttons)
			{
				AvailableButtons.Add(MakeShared<FJsonValueString>(Button.Label));
			}
			Result->SetBoolField(TEXT("success"), false);
			Result->SetArrayField(TEXT("availableButtons"), AvailableButtons);
			Result->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Button not found on dialog '%s'. Pass buttonLabel as one of [%s], or buttonIndex between 0 and %d, or action='close'."),
				*Title, *JoinLabels(Buttons), FMath::Max(0, Buttons.Num() - 1)));
		}
	}

	return MCPResult(Result);
}

EAppReturnType::Type FDialogHandlers::ParseResponseType(const FString& ResponseStr, bool& bOutValid)
{
	bOutValid = true;
	FString Lower = ResponseStr.ToLower();
	if (Lower == TEXT("yes"))       return EAppReturnType::Yes;
	if (Lower == TEXT("no"))        return EAppReturnType::No;
	if (Lower == TEXT("ok"))        return EAppReturnType::Ok;
	if (Lower == TEXT("cancel"))    return EAppReturnType::Cancel;
	if (Lower == TEXT("retry"))     return EAppReturnType::Retry;
	if (Lower == TEXT("continue"))  return EAppReturnType::Continue;
	if (Lower == TEXT("yesall"))    return EAppReturnType::YesAll;
	if (Lower == TEXT("noall"))     return EAppReturnType::NoAll;
	bOutValid = false;
	return EAppReturnType::Ok;
}

FString FDialogHandlers::ValidResponseList()
{
	return TEXT("yes, no, ok, cancel, retry, continue, yesall, noall");
}

FString FDialogHandlers::ResponseTypeToString(EAppReturnType::Type Response)
{
	switch (Response)
	{
	case EAppReturnType::Yes:      return TEXT("yes");
	case EAppReturnType::No:       return TEXT("no");
	case EAppReturnType::Ok:       return TEXT("ok");
	case EAppReturnType::Cancel:   return TEXT("cancel");
	case EAppReturnType::Retry:    return TEXT("retry");
	case EAppReturnType::Continue: return TEXT("continue");
	case EAppReturnType::YesAll:   return TEXT("yesall");
	case EAppReturnType::NoAll:    return TEXT("noall");
	default:                       return TEXT("unknown");
	}
}

FString FDialogHandlers::MsgTypeToString(EAppMsgType::Type MsgType)
{
	switch (MsgType)
	{
	case EAppMsgType::Ok:                         return TEXT("Ok");
	case EAppMsgType::YesNo:                      return TEXT("YesNo");
	case EAppMsgType::OkCancel:                   return TEXT("OkCancel");
	case EAppMsgType::YesNoCancel:                return TEXT("YesNoCancel");
	case EAppMsgType::CancelRetryContinue:        return TEXT("CancelRetryContinue");
	case EAppMsgType::YesNoYesAllNoAll:            return TEXT("YesNoYesAllNoAll");
	case EAppMsgType::YesNoYesAllNoAllCancel:      return TEXT("YesNoYesAllNoAllCancel");
	case EAppMsgType::YesNoYesAll:                return TEXT("YesNoYesAll");
	default:                                      return TEXT("Unknown");
	}
}
