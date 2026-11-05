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
#include "HAL/PlatformMisc.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Widgets/SWindow.h"
#include "Widgets/IToolTip.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"

// Static member definitions
TArray<FDialogHandlers::FDialogPolicy> FDialogHandlers::Policies;
decltype(FCoreDelegates::ModalMessageDialog) FDialogHandlers::PreviousModalMessageDialog;
FDelegateHandle FDialogHandlers::OurDialogHandle;
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

	/** Whether text names an IDE or a source code editor. */
	static bool NamesAnIde(const FString& Text)
	{
		const FString Lower = Normalise(Text);
		for (const TCHAR* Phrase : { TEXT("visual studio"), TEXT("vs code"), TEXT("source code editor"), TEXT("code editor") })
		{
			if (Lower.Contains(Phrase)) return true;
		}
		TArray<FString> Words;
		FString Word;
		for (const TCHAR C : Lower)
		{
			if (FChar::IsAlnum(C)) { Word.AppendChar(C); continue; }
			if (!Word.IsEmpty()) { Words.Add(Word); Word.Reset(); }
		}
		if (!Word.IsEmpty()) Words.Add(Word);
		for (const TCHAR* Name : { TEXT("ide"), TEXT("vscode"), TEXT("rider"), TEXT("xcode"), TEXT("devenv") })
		{
			if (Words.Contains(Name)) return true;
		}
		return false;
	}

	/**
	 * Whether pressing this button would open an IDE: its label names one, or
	 * the dialog asks about one and the button is the one that agrees.
	 */
	static bool OpensAnIde(const FString& Label, const FString& Message)
	{
		if (NamesAnIde(Label)) return true;
		if (!NamesAnIde(Message)) return false;
		const FString L = Normalise(Label);
		return L == TEXT("yes") || L == TEXT("ok") || L == TEXT("open") || L == TEXT("yes to all") || L == TEXT("continue");
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

	/** One button an FMessageDialog of some type shows, with the labels it goes by. */
	struct FMessageDialogButton
	{
		EAppReturnType::Type Answer;
		TArray<FString> Labels;
	};

	static FMessageDialogButton MessageButton(EAppReturnType::Type Answer)
	{
		switch (Answer)
		{
		case EAppReturnType::Yes:      return { Answer, { TEXT("Yes") } };
		case EAppReturnType::No:       return { Answer, { TEXT("No") } };
		case EAppReturnType::Ok:       return { Answer, { TEXT("OK") } };
		case EAppReturnType::Cancel:   return { Answer, { TEXT("Cancel") } };
		case EAppReturnType::Retry:    return { Answer, { TEXT("Retry"), TEXT("Try Again") } };
		case EAppReturnType::Continue: return { Answer, { TEXT("Continue") } };
		case EAppReturnType::YesAll:   return { Answer, { TEXT("Yes to All"), TEXT("Yes All"), TEXT("YesAll") } };
		case EAppReturnType::NoAll:    return { Answer, { TEXT("No to All"), TEXT("No All"), TEXT("NoAll") } };
		default:                       return { Answer, {} };
		}
	}

	/** The buttons an FMessageDialog of this type offers, in layout order. */
	static TArray<FMessageDialogButton> MessageDialogButtons(EAppMsgType::Type MsgType)
	{
		TArray<EAppReturnType::Type> Answers;
		switch (MsgType)
		{
		case EAppMsgType::Ok:                     Answers = { EAppReturnType::Ok }; break;
		case EAppMsgType::YesNo:                  Answers = { EAppReturnType::Yes, EAppReturnType::No }; break;
		case EAppMsgType::OkCancel:               Answers = { EAppReturnType::Ok, EAppReturnType::Cancel }; break;
		case EAppMsgType::YesNoCancel:            Answers = { EAppReturnType::Yes, EAppReturnType::No, EAppReturnType::Cancel }; break;
		case EAppMsgType::CancelRetryContinue:    Answers = { EAppReturnType::Cancel, EAppReturnType::Retry, EAppReturnType::Continue }; break;
		case EAppMsgType::YesNoYesAllNoAll:       Answers = { EAppReturnType::Yes, EAppReturnType::No, EAppReturnType::YesAll, EAppReturnType::NoAll }; break;
		case EAppMsgType::YesNoYesAllNoAllCancel: Answers = { EAppReturnType::Yes, EAppReturnType::No, EAppReturnType::YesAll, EAppReturnType::NoAll, EAppReturnType::Cancel }; break;
		case EAppMsgType::YesNoYesAll:            Answers = { EAppReturnType::Yes, EAppReturnType::No, EAppReturnType::YesAll }; break;
		default: break;
		}
		TArray<FMessageDialogButton> Out;
		for (const EAppReturnType::Type Answer : Answers)
		{
			Out.Add(MessageButton(Answer));
		}
		return Out;
	}

	/** The answer a label names among Buttons: exact first, then substring. */
	static bool FindMessageAnswer(const TArray<FMessageDialogButton>& Buttons, const FString& Label, EAppReturnType::Type& OutAnswer)
	{
		const FString Want = Normalise(Label);
		if (Want.IsEmpty())
		{
			return false;
		}
		for (const FMessageDialogButton& Button : Buttons)
		{
			for (const FString& Offered : Button.Labels)
			{
				if (Normalise(Offered) == Want)
				{
					OutAnswer = Button.Answer;
					return true;
				}
			}
		}
		for (const FMessageDialogButton& Button : Buttons)
		{
			for (const FString& Offered : Button.Labels)
			{
				if (Normalise(Offered).Contains(Want))
				{
					OutAnswer = Button.Answer;
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * The button an FMessageDialog of this type offers for a response, by
	 * meaning: the response itself, else its nearest equivalent there. An
	 * OK/Cancel dialog has no Yes or No, so yes answers OK and no answers
	 * Cancel; returning a value the dialog does not offer is read by the caller
	 * as whatever it tests for, which let "no" continue a rename (#1334).
	 */
	static bool OfferedAnswerFor(EAppMsgType::Type MsgType, EAppReturnType::Type Response, EAppReturnType::Type& OutAnswer)
	{
		TArray<EAppReturnType::Type> Preference;
		switch (Response)
		{
		case EAppReturnType::Yes:      Preference = { EAppReturnType::Yes, EAppReturnType::Ok, EAppReturnType::Continue }; break;
		case EAppReturnType::Ok:       Preference = { EAppReturnType::Ok, EAppReturnType::Yes, EAppReturnType::Continue }; break;
		case EAppReturnType::Continue: Preference = { EAppReturnType::Continue, EAppReturnType::Ok, EAppReturnType::Yes }; break;
		case EAppReturnType::YesAll:   Preference = { EAppReturnType::YesAll, EAppReturnType::Yes, EAppReturnType::Ok }; break;
		case EAppReturnType::No:       Preference = { EAppReturnType::No, EAppReturnType::Cancel }; break;
		case EAppReturnType::NoAll:    Preference = { EAppReturnType::NoAll, EAppReturnType::No, EAppReturnType::Cancel }; break;
		case EAppReturnType::Cancel:   Preference = { EAppReturnType::Cancel, EAppReturnType::No }; break;
		default:                       Preference = { Response }; break;
		}
		const TArray<FMessageDialogButton> Offered = MessageDialogButtons(MsgType);
		for (const EAppReturnType::Type Want : Preference)
		{
			for (const FMessageDialogButton& Button : Offered)
			{
				if (Button.Answer == Want)
				{
					OutAnswer = Want;
					return true;
				}
			}
		}
		return false;
	}

	/** The first response keyword whose synonyms carry Label, so a refusal can name it (#1421). */
	static bool ResponseForButtonLabel(const FString& Label, EAppReturnType::Type& OutAnswer)
	{
		const FString Want = Normalise(Label);
		for (EAppReturnType::Type Candidate : { EAppReturnType::Yes, EAppReturnType::No, EAppReturnType::Ok, EAppReturnType::Cancel,
			EAppReturnType::Retry, EAppReturnType::Continue, EAppReturnType::YesAll, EAppReturnType::NoAll })
		{
			for (const FString& Synonym : SynonymsFor(Candidate))
			{
				if (Normalise(Synonym) == Want)
				{
					OutAnswer = Candidate;
					return true;
				}
			}
		}
		return false;
	}

	/** The response keyword a label names on some message dialog, for reporting. */
	static bool ResponseNamedByLabel(const FString& Label, EAppReturnType::Type& OutAnswer)
	{
		return FindMessageAnswer(MessageDialogButtons(EAppMsgType::YesNoYesAllNoAllCancel), Label, OutAnswer)
			|| FindMessageAnswer(MessageDialogButtons(EAppMsgType::CancelRetryContinue), Label, OutAnswer)
			|| FindMessageAnswer(MessageDialogButtons(EAppMsgType::Ok), Label, OutAnswer);
	}

	// Recent answered dialogs, newest last, bounded. Game thread only.
	static TArray<FDialogHandlers::FAnsweredDialog> AnsweredDialogs;
	static int32 AnsweredDialogSerial = 0;
	static const int32 MaxAnsweredDialogs = 16;
}

void FDialogHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	// Reports parameters its handlers never read (#1057). Tagged with the editor
	// category because the editor tool is the surface these methods are exposed
	// through, so their parameter specs generate into its module.
	FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("editor"));

	// #1057: arming a policy and pressing a button would act on whatever the
	// contract test sends, so both are ContractExempt and their sources are held
	// to their specs instead.
	using EType = EMCPParamType;
	const TArray<FMCPParamSpec> NoParams;
	// buttonLabel and response combine: the label answers a Slate modal, the
	// response keyword a message dialog.
	Registry.RegisterHandler(TEXT("set_dialog_policy"), &SetDialogPolicy, {
		MCPParam::Required(TEXT("pattern"), EType::String, TEXT("Substring matched case-insensitively against the dialog title and message")),
		MCPParam::Optional(TEXT("response"), EType::String, TEXT("yes | no | ok | cancel | retry | continue | yesall | noall. On a Slate modal it presses whichever button carries that meaning; an unknown keyword is refused")),
		MCPParam::Optional(TEXT("buttonLabel"), EType::String, TEXT("Button a matched dialog gets pressed for it, matched exactly first, then as a substring. Reaches buttons no response names, such as Don't Save")),
	}, MCPSpec::AtLeastOne({ { TEXT("response") }, { TEXT("buttonLabel") } })
		.ContractExempt(TEXT("arms a policy that answers dialogs unattended")));
	Registry.RegisterHandler(TEXT("clear_dialog_policy"), &ClearDialogPolicy, {
		MCPParam::Optional(TEXT("pattern"), EMCPParamType::String, TEXT("Exact pattern of the policy to clear; omit to clear every policy")),
	});
	Registry.RegisterHandler(TEXT("get_dialog_policy"), &GetDialogPolicy, NoParams);
	Registry.RegisterHandler(TEXT("list_dialogs"), &ListDialogs, NoParams);
	// buttonLabel wins over buttonIndex, and dialogAction applies only when no
	// button was named.
	Registry.RegisterHandler(TEXT("respond_to_dialog"), &RespondToDialog, {
		MCPParam::Optional(TEXT("buttonLabel"), EType::String, TEXT("Label of the button to press, matched exactly first, then as a substring")),
		MCPParam::Optional(TEXT("buttonIndex"), EType::Number, TEXT("Index of the button to press, in the order list_dialogs reports")),
		MCPParam::Optional(TEXT("dialogAction"), EType::String, TEXT("escape | close: dismiss the dialog without pressing a button, for a modal offering none that fits")),
		MCPParam::Optional(TEXT("items"), EType::Array, TEXT("The dialog's own tickable rows to set before the button is pressed; indices come from list_dialogs, and rows left out keep their state")).Items(EType::Object).WithFields({
			MCPParam::RequiredField(TEXT("index"), EType::Number, TEXT("Row index from list_dialogs")),
			MCPParam::RequiredField(TEXT("checked"), EType::Boolean, TEXT("Whether the row ends up ticked")),
		}),
	}, MCPSpec::AtLeastOne({ { TEXT("buttonLabel") }, { TEXT("buttonIndex") }, { TEXT("dialogAction") } })
		.ContractExempt(TEXT("presses a button on the active modal dialog")));

	// #1231: every open modal and floating window, not only the one the gate
	// describes, addressed by the windowId list_windows reports.
	Registry.RegisterHandler(TEXT("list_windows"), &ListWindows, {
		MCPParam::Optional(TEXT("windowId"), EType::Integer, TEXT("Describe only this window, by the id list_windows or list_dialogs reported")),
		MCPParam::Optional(TEXT("maxTexts"), EType::Integer, TEXT("Texts reported per window (default 200)")),
		MCPParam::Optional(TEXT("maxTextChars"), EType::Integer, TEXT("Characters kept per text (default 2000)")),
		MCPParam::Optional(TEXT("maxButtons"), EType::Integer, TEXT("Buttons reported per window (default 200)")),
	});
	Registry.RegisterHandler(TEXT("press_window_button"), &PressWindowButton, {
		MCPParam::Required(TEXT("windowId"), EType::Integer, TEXT("Window to press a button on, by the id list_windows reported")),
		MCPParam::Optional(TEXT("buttonLabel"), EType::String, TEXT("Label of the button to press, matched exactly first, then as a substring; refused when ambiguous")),
		MCPParam::Optional(TEXT("buttonIndex"), EType::Number, TEXT("Index of the button to press, in the order list_windows reports")),
	}, MCPSpec::ExactlyOne({ { TEXT("buttonLabel") }, { TEXT("buttonIndex") } })
		.ContractExempt(TEXT("presses a button on an open editor window")));
	Registry.RegisterHandler(TEXT("close_window"), &CloseWindowById, {
		MCPParam::Required(TEXT("windowId"), EType::Integer, TEXT("Window to close, by the id list_windows reported")),
	}, MCPSpec::ContractExempt(TEXT("closes an open editor window")));
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
	PreviousModalMessageDialog = FCoreDelegates::ModalMessageDialog;
	FCoreDelegates::ModalMessageDialog.BindStatic(&FDialogHandlers::HandleModalDialogV2);
	OurDialogHandle = FCoreDelegates::ModalMessageDialog.GetHandle();
	bHookInstalled = true;

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Dialog hook installed (ModalMessageDialog delegate bound)"));
}

void FDialogHandlers::RemoveDialogHook()
{
	if (!bHookInstalled)
	{
		return;
	}

	// Put back what was there, unless something rebound the delegate since.
	if (FCoreDelegates::ModalMessageDialog.GetHandle() == OurDialogHandle)
	{
		FCoreDelegates::ModalMessageDialog = PreviousModalMessageDialog;
	}
	PreviousModalMessageDialog.Unbind();
	OurDialogHandle.Reset();
	bHookInstalled = false;
	Policies.Empty();

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Dialog hook removed"));
}

EAppReturnType::Type FDialogHandlers::HandleModalDialogV2(EAppMsgCategory Category, EAppMsgType::Type MsgType, const FText& Text, const FText& Title)
{
	return HandleModalDialog(Category, MsgType, Text, Title);
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
	int32 Index = ResolveButtonForPolicy(*Policy, Buttons, Reason);
	if (Buttons.IsValidIndex(Index) && OpensAnIde(Buttons[Index].Label, Message))
	{
		Reason = FString::Printf(TEXT("'%s' opens an IDE or source code editor, which no policy presses"), *Buttons[Index].Label);
		Index = INDEX_NONE;
	}
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
	NoteAnsweredDialog(Title, Message, Buttons[Index].Label, TEXT("policy"));

	// SimulateClick, for the same reason respond_to_dialog uses it: synthetic
	// mouse events bypass the capture bookkeeping SButton uses to decide a
	// click happened, and the dialog stays on screen while the caller is told
	// it was answered.
	FSlateApplication::Get().SetKeyboardFocus(Buttons[Index].Button);
	Buttons[Index].Button->SimulateClick();
	return true;
}

bool FDialogHandlers::ResolveMessageDialogAnswer(const FDialogPolicy& Policy, EAppMsgType::Type MsgType, EAppReturnType::Type& OutAnswer, FString& OutReason)
{
	using namespace MCPDialogPolicy;

	OutReason.Empty();
	const TArray<FMessageDialogButton> Offered = MessageDialogButtons(MsgType);
	if (!Policy.ButtonLabel.IsEmpty() && FindMessageAnswer(Offered, Policy.ButtonLabel, OutAnswer))
	{
		return true;
	}
	// A label this dialog does not carry still answers by the response it names
	// (Yes on an OK/Cancel dialog is OK), as an explicit response does.
	const bool bAnswersByResponse = Policy.ButtonLabel.IsEmpty() || Policy.bHasResponse;
	if (bAnswersByResponse && OfferedAnswerFor(MsgType, Policy.Response, OutAnswer))
	{
		return true;
	}
	TArray<FString> Labels;
	for (const FMessageDialogButton& Button : Offered)
	{
		Labels.Add(Button.Labels.Num() > 0 ? Button.Labels[0] : FString());
	}
	OutReason = bAnswersByResponse
		? FString::Printf(TEXT("response '%s' has no equivalent among [%s]"), *ResponseTypeToString(Policy.Response), *FString::Join(Labels, TEXT(", ")))
		: FString::Printf(TEXT("buttonLabel '%s' matches none of [%s] and the policy has no response keyword"), *Policy.ButtonLabel, *FString::Join(Labels, TEXT(", ")));
	return false;
}

void FDialogHandlers::NoteAnsweredDialog(const FString& Title, const FString& Message, const FString& Answer, const TCHAR* AnsweredBy)
{
	using namespace MCPDialogPolicy;

	FAnsweredDialog Entry;
	Entry.Serial = ++AnsweredDialogSerial;
	Entry.Title = Title;
	Entry.Message = Message;
	Entry.Answer = Answer;
	Entry.AnsweredBy = AnsweredBy;
	AnsweredDialogs.Add(MoveTemp(Entry));
	if (AnsweredDialogs.Num() > MaxAnsweredDialogs)
	{
		AnsweredDialogs.RemoveAt(0, AnsweredDialogs.Num() - MaxAnsweredDialogs);
	}
}

int32 FDialogHandlers::GetAnsweredDialogSerial()
{
	return MCPDialogPolicy::AnsweredDialogSerial;
}

TArray<FDialogHandlers::FAnsweredDialog> FDialogHandlers::AnsweredDialogsSince(int32 Serial)
{
	TArray<FAnsweredDialog> Out;
	for (const FAnsweredDialog& Entry : MCPDialogPolicy::AnsweredDialogs)
	{
		if (Entry.Serial > Serial)
		{
			Out.Add(Entry);
		}
	}
	return Out;
}

EAppReturnType::Type FDialogHandlers::HandleModalDialog(EAppMsgCategory Category, EAppMsgType::Type MsgType, const FText& Text, const FText& Title)
{
	FString MessageStr = Text.ToString();
	FString TitleStr = Title.ToString();

	// A policy a caller armed is a standing instruction, so it answers. Its
	// buttonLabel names one of the buttons this dialog type offers (#1334).
	if (const FDialogPolicy* Policy = FindMatchingPolicy(TitleStr, MessageStr))
	{
		EAppReturnType::Type Answer = EAppReturnType::Cancel;
		FString Reason;
		const bool bResolved = ResolveMessageDialogAnswer(*Policy, MsgType, Answer, Reason);
		// A message box that asks about an IDE is never agreed to: Yes or OK is what opens it.
		if (bResolved && MCPDialogPolicy::OpensAnIde(ResponseTypeToString(Answer), MessageStr))
		{
			UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Dialog '%s' matched policy '%s' but its answer would open an IDE, so it is left unanswered."),
				*TitleStr, *Policy->Pattern);
		}
		else if (bResolved)
		{
			UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Dialog answered by the policy a caller armed: pattern='%s' title='%s' answer=%s"),
				*Policy->Pattern, *TitleStr, *ResponseTypeToString(Answer));
			NoteAnsweredDialog(TitleStr, MessageStr, ResponseTypeToString(Answer), TEXT("policy"));
			return Answer;
		}
		UE_LOG(LogMCPBridge, Warning, TEXT("[UE-MCP] Dialog '%s' matched policy '%s' but was not answered: %s"),
			*TitleStr, *Policy->Pattern, *Reason);
	}

	// Nothing a caller armed answers this, so it goes to the binding the hook
	// replaced: the editor's Slate dialog, which the modal-loop tick, the
	// status snapshot, list_dialogs and respond_to_dialog all reach. Unbinding
	// and calling FMessageDialog::Open instead fell through to a native
	// platform box none of them could see (#1272).
	UE_LOG(LogMCPBridge, Warning,
		TEXT("[UE-MCP] Modal dialog raised with no policy armed for it, so it is left for a person to answer: title='%s' message='%s'. Read it with editor(list_dialogs) and answer it with editor(respond_to_dialog)."),
		*TitleStr, *MessageStr);
	const EAppReturnType::Type UserAnswer = PreviousModalMessageDialog.IsBound()
		? PreviousModalMessageDialog.Execute(Category, MsgType, Text, Title)
		: FPlatformMisc::MessageBoxExt(MsgType, *MessageStr, *TitleStr);
	NoteAnsweredDialog(TitleStr, MessageStr, ResponseTypeToString(UserAnswer), TEXT("person"));
	return UserAnswer;
}

TSharedPtr<FJsonValue> FDialogHandlers::SetDialogPolicy(const TSharedPtr<FJsonObject>& Params)
{
	FString Pattern;
	if (!TryGetStringParam(Params, TEXT("pattern"), Pattern) || Pattern.IsEmpty())
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

	// On the FMessageDialog path a buttonLabel answers with the button it names
	// there (#1334). A label-only policy naming no such button leaves the
	// dialog for a person rather than inventing an answer.
	EAppReturnType::Type Response = EAppReturnType::Cancel;
	const bool bResponseExplicit = !ResponseStr.IsEmpty();
	bool bHasResponse = bResponseExplicit;
	if (bResponseExplicit)
	{
		bool bValidResponse = false;
		Response = ParseResponseType(ResponseStr, bValidResponse);
		if (!bValidResponse)
		{
			// A button label passed as the response is refused with the keyword that presses it (#1421).
			EAppReturnType::Type Mapped = EAppReturnType::Cancel;
			if (MCPDialogPolicy::ResponseForButtonLabel(ResponseStr, Mapped))
			{
				return MCPError(FString::Printf(
					TEXT("Unknown response '%s'. That is a button label, not a response keyword: response '%s' presses it, or pass buttonLabel '%s' to press it by name. Valid responses are %s."),
					*ResponseStr, *ResponseTypeToString(Mapped), *ResponseStr, *ValidResponseList()));
			}
			return MCPError(FString::Printf(
				TEXT("Unknown response '%s'. Valid responses are %s. To press a button by its label, pass buttonLabel instead."),
				*ResponseStr, *ValidResponseList()));
		}
	}
	else
	{
		bHasResponse = MCPDialogPolicy::ResponseNamedByLabel(ButtonLabel, Response);
	}

	FDialogPolicy NewPolicy;
	NewPolicy.Pattern = Pattern;
	NewPolicy.Response = Response;
	NewPolicy.ButtonLabel = ButtonLabel;
	NewPolicy.bResponseExplicit = bResponseExplicit;
	NewPolicy.bHasResponse = bHasResponse;
	const FString ResponseOut = PolicyResponseString(NewPolicy);

	// Idempotency: an identical policy is already armed.
	for (const FDialogPolicy& P : Policies)
	{
		if (P.Pattern == Pattern && P.Response == Response && P.ButtonLabel == ButtonLabel
			&& P.bResponseExplicit == bResponseExplicit)
		{
			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			Existed->SetStringField(TEXT("pattern"), Pattern);
			Existed->SetStringField(TEXT("response"), ResponseOut);
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
		*Pattern, ResponseOut.IsEmpty() ? *ButtonLabel : *ResponseOut, *ButtonNote);

	// Ensure hook is installed
	if (!bHookInstalled)
	{
		InstallDialogHook();
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("pattern"), Pattern);
	Result->SetStringField(TEXT("response"), ResponseOut);
	Result->SetStringField(TEXT("buttonLabel"), ButtonLabel);
	Result->SetNumberField(TEXT("policyCount"), Policies.Num());
	if (!bResponseExplicit)
	{
		Result->SetStringField(TEXT("messageDialogNote"), bHasResponse
			? FString::Printf(TEXT("On a message dialog this label answers '%s', or the nearest button that dialog offers (OK for yes on an OK/Cancel dialog, Cancel for no)."), *ResponseOut)
			: FString(TEXT("No message dialog button carries this label, so a message dialog this pattern matches is left for a person.")));
	}

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
		Entry->SetStringField(TEXT("response"), PolicyResponseString(P));
		Entry->SetStringField(TEXT("buttonLabel"), P.ButtonLabel);
		ClearedJson.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("cleared"), ClearedJson);

	if (Cleared.Num() == 1)
	{
		// A cleared policy has a real inverse: arm it again exactly as it was.
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("pattern"), Cleared[0].Pattern);
		Payload->SetStringField(TEXT("response"), Cleared[0].bResponseExplicit ? ResponseTypeToString(Cleared[0].Response) : FString());
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
		P->SetStringField(TEXT("response"), PolicyResponseString(Policy));
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

// A NAMED namespace: this module is a unity build, so an anonymous one here
// would merge with whatever other .cpp shares the blob.
namespace MCPDialogWindows
{
	/** Does this window host docked tabs? Such a window is a workspace, never a
	 *  prompt (#1078). Matched by type name because Slate's docking types are
	 *  private to their module. */
	static bool HostsDockedTabs(const TSharedRef<SWidget>& Widget)
	{
		const FName Type = Widget->GetType();
		if (Type == TEXT("SDockingArea")
			|| Type == TEXT("SDockingTabStack")
			|| Type == TEXT("SDockingSplitter")
			|| Type == TEXT("SDockTab"))
		{
			return true;
		}
		FChildren* Children = Widget->GetChildren();
		if (Children)
		{
			for (int32 i = 0; i < Children->Num(); ++i)
			{
				if (HostsDockedTabs(Children->GetChildAt(i))) return true;
			}
		}
		return false;
	}

	/** Why a window was not counted as blocking, for the diagnostics below. */
	struct FSkippedWindow
	{
		FString Title;
		FString Reason;
	};

	/** A window the walk did count, and what it counted it as. */
	struct FFoundWindow
	{
		TSharedPtr<SWindow> Window;
		EMCPPromptKind Kind = EMCPPromptKind::NotAPrompt;
	};

	/**
	 * Every window the editor is holding the user with, gathered recursively.
	 *
	 * Menus, tooltips and notifications are not regular windows, so they are
	 * never mistaken for questions.
	 *
	 * Visibility is deliberately NOT consulted: a window that is undrawn or
	 * behind the editor still holds whatever raised it.
	 *
	 * OutSkipped records what was walked past, because a wrong answer either
	 * way is expensive and an empty list is otherwise undiagnosable.
	 */
	static void Gather(
		const TArray<TSharedRef<SWindow>>& Roots,
		TArray<FFoundWindow>& Out,
		TArray<FSkippedWindow>* OutSkipped = nullptr)
	{
		for (const TSharedRef<SWindow>& Window : Roots)
		{
			FString SkipReason;
			const EMCPPromptKind Kind = FDialogHandlers::ClassifyWindow(Window, &SkipReason);
			if (Kind != EMCPPromptKind::NotAPrompt)
			{
				const bool bAlreadyFound = Out.ContainsByPredicate(
					[&Window](const FFoundWindow& Found) { return Found.Window == Window; });
				if (!bAlreadyFound)
				{
					Out.Add({ TSharedPtr<SWindow>(Window), Kind });
				}
			}
			else if (OutSkipped && !SkipReason.IsEmpty())
			{
				OutSkipped->Add({ Window->GetTitle().ToString(), SkipReason });
			}
			Gather(Window->GetChildWindows(), Out, OutSkipped);
		}
	}

	/**
	 * The window blocking the editor, by every route it can be blocked.
	 *
	 * GetActiveModalWindow leads, because a window on the modal stack is one
	 * Slate itself calls blocking. It is NOT the whole answer and never was: it
	 * reports only what was pushed with AddModalWindow, so a dialog raised any
	 * other way came back as no dialog at all. Every layer above then read that
	 * as "the screen is clear" and sent a quit at an editor sitting on a save
	 * prompt, while the same editor had the window on screen. One narrow
	 * detector was the whole system's eyes, and absence of evidence from it was
	 * being reported as evidence of absence.
	 */
	static TSharedPtr<SWindow> FindBlocking(
		TArray<FSkippedWindow>* OutSkipped = nullptr,
		EMCPPromptKind* OutKind = nullptr)
	{
		if (OutKind)
		{
			*OutKind = EMCPPromptKind::NotAPrompt;
		}
		if (!FSlateApplication::IsInitialized())
		{
			return nullptr;
		}
		FSlateApplication& Slate = FSlateApplication::Get();
		TArray<FFoundWindow> Found;
		if (TSharedPtr<SWindow> Modal = Slate.GetActiveModalWindow())
		{
			Found.Add({ Modal, EMCPPromptKind::Modal });
		}
		Gather(Slate.GetInteractiveTopLevelWindows(), Found, OutSkipped);
		if (Found.Num() == 0)
		{
			return nullptr;
		}
		// A modal leads whatever order the walk found things in. It is the one
		// window the game thread is parked behind, so reporting a Find results
		// window in its place would name the wrong thing as the reason the
		// editor is stuck, on the one call that has to be right (#1118).
		int32 Best = 0;
		for (int32 Index = 0; Index < Found.Num(); ++Index)
		{
			if (Found[Index].Kind == EMCPPromptKind::Modal)
			{
				Best = Index;
				break;
			}
		}
		if (OutKind)
		{
			*OutKind = Found[Best].Kind;
		}
		return Found[Best].Window;
	}
}

// Window control (#1231): every open modal and floating window, addressed by
// an id, acted on only where Slate would route a person's input. A NAMED
// namespace, for the unity-build reason MCPDialogPolicy gives.
namespace MCPWindowControl
{
	/** Weak, so a record never keeps a closed window alive; ids are never reused. */
	struct FTrackedWindow
	{
		int32 Id = 0;
		TWeakPtr<SWindow> Window;
	};

	static TArray<FTrackedWindow> TrackedWindows;
	static int32 NextWindowId = 1;

	static int32 IdForWindow(const TSharedRef<SWindow>& Window)
	{
		TrackedWindows.RemoveAll([](const FTrackedWindow& Entry) { return !Entry.Window.IsValid(); });
		for (const FTrackedWindow& Entry : TrackedWindows)
		{
			if (Entry.Window.HasSameObject(&Window.Get()))
			{
				return Entry.Id;
			}
		}
		FTrackedWindow NewEntry;
		NewEntry.Id = NextWindowId++;
		NewEntry.Window = Window;
		TrackedWindows.Add(NewEntry);
		return NewEntry.Id;
	}

	static void GatherOpenWindows(const TArray<TSharedRef<SWindow>>& Windows, const SWindow* MainWindow, TArray<TSharedRef<SWindow>>& Out)
	{
		for (const TSharedRef<SWindow>& Window : Windows)
		{
			if (Window->IsRegularWindow() && &Window.Get() != MainWindow)
			{
				Out.AddUnique(Window);
			}
			GatherOpenWindows(Window->GetChildWindows(), MainWindow, Out);
		}
	}

	/**
	 * Every regular window except the main editor window. GetTopLevelWindows,
	 * because GetInteractiveTopLevelWindows returns only the modal while one is
	 * up and would hide the windows behind it.
	 */
	static TArray<TSharedRef<SWindow>> OpenWindows()
	{
		TArray<TSharedRef<SWindow>> Out;
		if (!FSlateApplication::IsInitialized())
		{
			return Out;
		}
		const TSharedPtr<SWindow> MainWindow = FGlobalTabmanager::Get()->GetRootWindow();
		GatherOpenWindows(FSlateApplication::Get().GetTopLevelWindows(), MainWindow.Get(), Out);
		return Out;
	}

	static TSharedPtr<SWindow> FindOpenWindow(int32 WindowId)
	{
		const FTrackedWindow* Entry = TrackedWindows.FindByPredicate(
			[WindowId](const FTrackedWindow& Candidate) { return Candidate.Id == WindowId; });
		const TSharedPtr<SWindow> Tracked = Entry ? Entry->Window.Pin() : nullptr;
		if (!Tracked.IsValid())
		{
			return nullptr;
		}
		// Destruction is queued for the next Slate tick, so alive is not open.
		for (const TSharedRef<SWindow>& Window : OpenWindows())
		{
			if (&Window.Get() == Tracked.Get())
			{
				return Tracked;
			}
		}
		return nullptr;
	}

	/** FSlateApplication::ShouldProcessUserInputMessages, asked of one window. */
	static bool ReceivesInput(const TSharedRef<SWindow>& Window)
	{
		const TSharedPtr<SWindow> ActiveModal = FSlateApplication::Get().GetActiveModalWindow();
		return !ActiveModal.IsValid() || ActiveModal.Get() == &Window.Get() || Window->IsDescendantOf(ActiveModal);
	}

	/**
	 * SButton and the public classes derived from it. Slate has no is-a test
	 * without RTTI, so the cast to SButton is valid only for a type named here.
	 */
	static bool IsButton(const TSharedRef<SWidget>& Widget)
	{
		static const FName ButtonType(TEXT("SButton"));
		static const FName HyperlinkType(TEXT("SHyperlink"));
		static const FName PrimaryButtonType(TEXT("SPrimaryButton"));
		static const FName SimpleButtonType(TEXT("SSimpleButton"));
		const FName Type = Widget->GetType();
		return Type == ButtonType || Type == HyperlinkType || Type == PrimaryButtonType || Type == SimpleButtonType;
	}

	/** The text a label widget shows. Editable fields are never read. */
	static bool ReadDisplayedText(const TSharedRef<SWidget>& Widget, FString& OutText)
	{
		static const FName TextBlockType(TEXT("STextBlock"));
		const FName Type = Widget->GetType();
		if (Type == TextBlockType)
		{
			OutText = StaticCastSharedRef<STextBlock>(Widget)->GetText().ToString();
			return true;
		}
#if WITH_FANCY_TEXT
		static const FName RichTextBlockType(TEXT("SRichTextBlock"));
		if (Type == RichTextBlockType)
		{
			OutText = StaticCastSharedRef<SRichTextBlock>(Widget)->GetText().ToString();
			return true;
		}
#endif
		return false;
	}

	/** A plain tooltip's text, which is how an icon-only button says what it does. */
	static FString ReadToolTipText(const TSharedRef<SWidget>& Widget)
	{
		const TSharedPtr<IToolTip> ToolTip = Widget->GetToolTip();
		if (!ToolTip.IsValid())
		{
			return FString();
		}
		const TSharedRef<SWidget> ToolTipWidget = ToolTip->AsWidget();
		static const FName ToolTipType(TEXT("SToolTip"));
		if (ToolTipWidget->GetType() != ToolTipType)
		{
			return FString();
		}
		return StaticCastSharedRef<SToolTip>(ToolTipWidget)->GetTextTooltip().ToString();
	}

	/** SButton::SimulateClick runs even on a disabled button, so enablement is checked up the tree. */
	static bool IsEffectivelyEnabled(const TSharedRef<SWidget>& Widget)
	{
		for (TSharedPtr<SWidget> Current = Widget; Current.IsValid(); Current = Current->GetParentWidget())
		{
			if (!Current->IsEnabled())
			{
				return false;
			}
		}
		return true;
	}

	struct FWindowButton
	{
		TSharedPtr<SButton> Button;
		FString Label;
		FString ToolTip;
		bool bEnabled = true;
	};

	struct FWindowContent
	{
		TArray<FString> Texts;
		TArray<FWindowButton> Buttons;
	};

	/**
	 * Depth first over visible widgets. A button's label is the first text
	 * inside it, and text inside a button belongs to that button.
	 */
	static void WalkWindowContent(const TSharedRef<SWidget>& Widget, int32 EnclosingButton, FWindowContent& Out)
	{
		if (!Widget->GetVisibility().IsVisible())
		{
			return;
		}

		int32 ButtonIndex = EnclosingButton;
		if (IsButton(Widget))
		{
			FWindowButton Entry;
			Entry.Button = StaticCastSharedRef<SButton>(Widget);
			Entry.bEnabled = IsEffectivelyEnabled(Widget);
			ButtonIndex = Out.Buttons.Add(Entry);
		}

		FString Text;
		if (ReadDisplayedText(Widget, Text) && !Text.IsEmpty())
		{
			if (ButtonIndex == INDEX_NONE)
			{
				Out.Texts.Add(Text);
			}
			else if (Out.Buttons[ButtonIndex].Label.IsEmpty())
			{
				Out.Buttons[ButtonIndex].Label = Text;
			}
		}

		if (FChildren* Children = Widget->GetChildren())
		{
			for (int32 Index = 0; Index < Children->Num(); ++Index)
			{
				WalkWindowContent(Children->GetChildAt(Index), ButtonIndex, Out);
			}
		}
	}

	static FWindowContent CollectWindowContent(const TSharedRef<SWindow>& Window)
	{
		FWindowContent Content;
		WalkWindowContent(Window, INDEX_NONE, Content);

		const FString Title = Window->GetTitle().ToString();
		Content.Texts.RemoveAll([&Title](const FString& Text) { return Text == Title; });

		for (FWindowButton& Button : Content.Buttons)
		{
			if (Button.Label.IsEmpty() && Button.Button.IsValid())
			{
				Button.ToolTip = ReadToolTipText(Button.Button.ToSharedRef());
			}
		}
		return Content;
	}

	static TSharedPtr<FJsonObject> DescribeWindow(const TSharedRef<SWindow>& Window, const TSharedPtr<SWindow>& GatingWindow)
	{
		const TSharedPtr<SWindow> ActiveModal = FSlateApplication::Get().GetActiveModalWindow();
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("windowId"), IdForWindow(Window));
		Obj->SetStringField(TEXT("title"), Window->GetTitle().ToString());
		Obj->SetBoolField(TEXT("isModal"), Window->IsModalWindow());
		Obj->SetBoolField(TEXT("isActiveModal"), ActiveModal.Get() == &Window.Get());
		Obj->SetBoolField(TEXT("receivesInput"), ReceivesInput(Window));
		Obj->SetBoolField(TEXT("hasParentWindow"), Window->GetParentWindow().IsValid());
		Obj->SetBoolField(TEXT("hostsDockTabs"), MCPDialogWindows::HostsDockedTabs(Window));
		Obj->SetBoolField(TEXT("isVisible"), Window->IsVisible());
		Obj->SetBoolField(TEXT("gatesBridge"), GatingWindow.Get() == &Window.Get());
		return Obj;
	}

	static TArray<TSharedPtr<FJsonValue>> DescribeOpenWindows(const TSharedPtr<SWindow>& GatingWindow)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TSharedRef<SWindow>& Window : OpenWindows())
		{
			Out.Add(MakeShared<FJsonValueObject>(DescribeWindow(Window, GatingWindow)));
		}
		return Out;
	}

	static TSharedPtr<FJsonObject> DescribeButton(const FWindowContent& Content, int32 Index)
	{
		const FWindowButton& Button = Content.Buttons[Index];
		TSharedPtr<FJsonObject> ButtonObj = MakeShared<FJsonObject>();
		ButtonObj->SetNumberField(TEXT("index"), Index);
		ButtonObj->SetStringField(TEXT("label"), Button.Label);
		ButtonObj->SetStringField(TEXT("toolTip"), Button.ToolTip);
		ButtonObj->SetBoolField(TEXT("enabled"), Button.bEnabled);
		return ButtonObj;
	}

	static void AddWindowContent(const TSharedPtr<FJsonObject>& Obj, const FWindowContent& Content, int32 MaxTexts, int32 MaxTextChars, int32 MaxButtons)
	{
		bool bTextsTruncated = false;
		TArray<TSharedPtr<FJsonValue>> TextsJson;
		for (const FString& Text : Content.Texts)
		{
			if (TextsJson.Num() >= MaxTexts)
			{
				bTextsTruncated = true;
				break;
			}
			if (Text.Len() > MaxTextChars)
			{
				bTextsTruncated = true;
			}
			TextsJson.Add(MakeShared<FJsonValueString>(Text.Left(MaxTextChars)));
		}
		Obj->SetArrayField(TEXT("texts"), TextsJson);
		Obj->SetNumberField(TEXT("textCount"), Content.Texts.Num());
		Obj->SetBoolField(TEXT("textsTruncated"), bTextsTruncated);

		// Every button keeps its own index, labelled or not, because that index
		// is what press_window_button takes.
		TArray<TSharedPtr<FJsonValue>> ButtonsJson;
		for (int32 Index = 0; Index < Content.Buttons.Num() && Index < MaxButtons; ++Index)
		{
			ButtonsJson.Add(MakeShared<FJsonValueObject>(DescribeButton(Content, Index)));
		}
		Obj->SetArrayField(TEXT("buttons"), ButtonsJson);
		Obj->SetNumberField(TEXT("buttonCount"), Content.Buttons.Num());
		Obj->SetBoolField(TEXT("buttonsTruncated"), Content.Buttons.Num() > MaxButtons);
	}

	static TArray<TSharedPtr<FJsonValue>> BriefWindowList()
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TSharedRef<SWindow>& Window : OpenWindows())
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetNumberField(TEXT("windowId"), IdForWindow(Window));
			Entry->SetStringField(TEXT("title"), Window->GetTitle().ToString());
			Out.Add(MakeShared<FJsonValueObject>(Entry));
		}
		return Out;
	}

	/** A window id that names nothing open, which is what a closed window's id becomes. */
	static TSharedPtr<FJsonValue> WindowGoneRefusal(int32 WindowId)
	{
		TSharedPtr<FJsonObject> Gone = MakeShared<FJsonObject>();
		Gone->SetBoolField(TEXT("success"), false);
		Gone->SetBoolField(TEXT("windowGone"), true);
		Gone->SetBoolField(TEXT("alreadyClosed"), true);
		Gone->SetNumberField(TEXT("windowId"), WindowId);
		Gone->SetStringField(TEXT("error"), FString::Printf(
			TEXT("Window %d is not open: it has closed, or this editor session never issued that id. Nothing was done. openWindows lists what is open now."),
			WindowId));
		Gone->SetArrayField(TEXT("openWindows"), BriefWindowList());
		return MCPResult(Gone);
	}

	/**
	 * The open window a call names, or null with the refusal to return. A
	 * window Slate would not route input to is refused, so nothing is pressed
	 * or closed behind an active modal.
	 */
	static TSharedPtr<SWindow> ResolveTargetWindow(bool bHasWindowId, int32 WindowId, const TCHAR* Method, TSharedPtr<FJsonValue>& OutRefusal)
	{
		if (!bHasWindowId)
		{
			OutRefusal = MCPError(FString::Printf(
				TEXT("%s needs 'windowId'. editor(list_windows) and editor(list_dialogs) report one for every open window."),
				Method));
			return nullptr;
		}

		const TSharedPtr<SWindow> Window = FindOpenWindow(WindowId);
		if (!Window.IsValid())
		{
			OutRefusal = WindowGoneRefusal(WindowId);
			return nullptr;
		}

		if (!ReceivesInput(Window.ToSharedRef()))
		{
			const TSharedPtr<SWindow> ActiveModal = FSlateApplication::Get().GetActiveModalWindow();
			const FString ModalTitle = ActiveModal.IsValid() ? ActiveModal->GetTitle().ToString() : FString();
			TSharedPtr<FJsonObject> Blocked = MakeShared<FJsonObject>();
			Blocked->SetBoolField(TEXT("success"), false);
			Blocked->SetBoolField(TEXT("receivesInput"), false);
			Blocked->SetNumberField(TEXT("windowId"), WindowId);
			if (ActiveModal.IsValid())
			{
				Blocked->SetNumberField(TEXT("activeModalWindowId"), IdForWindow(ActiveModal.ToSharedRef()));
				Blocked->SetStringField(TEXT("activeModalTitle"), ModalTitle);
			}
			Blocked->SetStringField(TEXT("error"), FString::Printf(
				TEXT("Window %d does not receive input: the modal '%s' is up, and until it closes Slate routes input only to it and its child windows. Nothing was done. Answer or close that modal first."),
				WindowId, *ModalTitle));
			OutRefusal = MCPResult(Blocked);
			return nullptr;
		}

		return Window;
	}

	/** A press that did not happen, with the buttons the caller can choose from. */
	static TSharedPtr<FJsonValue> ButtonRefusal(int32 WindowId, const FString& WindowTitle, const FString& Error, const FWindowContent& Content, const TArray<int32>& Listed)
	{
		TArray<TSharedPtr<FJsonValue>> ButtonsJson;
		for (const int32 Index : Listed)
		{
			ButtonsJson.Add(MakeShared<FJsonValueObject>(DescribeButton(Content, Index)));
		}
		TSharedPtr<FJsonObject> Refusal = MakeShared<FJsonObject>();
		Refusal->SetBoolField(TEXT("success"), false);
		Refusal->SetNumberField(TEXT("windowId"), WindowId);
		Refusal->SetStringField(TEXT("windowTitle"), WindowTitle);
		Refusal->SetStringField(TEXT("error"), Error);
		Refusal->SetArrayField(TEXT("buttons"), ButtonsJson);
		return MCPResult(Refusal);
	}
}

bool FDialogHandlers::IsBlockingWindow(const TSharedRef<SWindow>& Window, FString* OutSkipReason)
{
	return ClassifyWindow(Window, OutSkipReason) != EMCPPromptKind::NotAPrompt;
}

const TCHAR* FDialogHandlers::PromptKindName(EMCPPromptKind Kind)
{
	switch (Kind)
	{
	case EMCPPromptKind::Modal:    return TEXT("modalWindow");
	case EMCPPromptKind::Parented: return TEXT("parentedWindow");
	default:                       return TEXT("none");
	}
}

EMCPPromptKind FDialogHandlers::ClassifyWindow(const TSharedRef<SWindow>& Window, FString* OutSkipReason)
{
	if (OutSkipReason) OutSkipReason->Reset();

	if (!Window->IsRegularWindow()) return EMCPPromptKind::NotAPrompt;
	// bIsModalWindow is set by FSlateApplication::AddModalWindow and nothing
	// else, so this is the one case in which the game thread is parked.
	if (Window->IsModalWindow()) return EMCPPromptKind::Modal;
	// Unparented and not modal: a top-level window of its own.
	if (!Window->GetParentWindow().IsValid()) return EMCPPromptKind::NotAPrompt;

	// Parented and not modal is usually a dialog raised without
	// AddModalWindow, the case GetActiveModalWindow misses. It is also the
	// shape of every utility window the editor puts in its own frame.
	if (MCPDialogWindows::HostsDockedTabs(Window))
	{
		if (OutSkipReason)
		{
			*OutSkipReason = TEXT("hosts docked tabs, so it is an editor window rather than a prompt");
		}
		return EMCPPromptKind::NotAPrompt;
	}
	return EMCPPromptKind::Parented;
}

TSharedPtr<SWindow> FDialogHandlers::CollectActiveModal(FString& OutTitle, FString& OutMessage, TArray<FModalButton>& OutButtons, TArray<FModalItem>* OutItems, EMCPPromptKind* OutKind)
{
	OutTitle.Empty();
	OutMessage.Empty();
	OutButtons.Empty();
	if (OutKind)
	{
		*OutKind = EMCPPromptKind::NotAPrompt;
	}

	if (!FSlateApplication::IsInitialized())
	{
		return nullptr;
	}

	TSharedPtr<SWindow> ActiveModal = MCPDialogWindows::FindBlocking(nullptr, OutKind);
	if (!ActiveModal.IsValid())
	{
		return nullptr;
	}

	OutTitle = ActiveModal->GetTitle().ToString();

	TArray<FString> TextContents;
	// How many buttons the walk is inside. A button's label is not a cell of
	// the row before it, and without this the last row of Save Content carried
	// "Save Selected", "Don't Save" and "Cancel" as its own cells (#1076).
	int32 ButtonDepth = 0;

	TFunction<void(const TSharedRef<SWidget>&)> TraverseWidgets = [&](const TSharedRef<SWidget>& Widget)
	{
		if (Widget->GetType() == TEXT("STextBlock"))
		{
			TSharedRef<STextBlock> TextBlock = StaticCastSharedRef<STextBlock>(Widget);
			FString Text = TextBlock->GetText().ToString();
			if (!Text.IsEmpty())
			{
				TextContents.Add(Text);
				// The walk is depth first and Slate lays a row out as its
				// checkbox then its cells, so text after a checkbox belongs to
				// that checkbox until the next one starts a new row. That is
				// what pairs "L_MoverPawnTest" with the box that saves it.
				if (OutItems && OutItems->Num() > 0 && ButtonDepth == 0)
				{
					OutItems->Last().Cells.Add(Text);
				}
			}
		}

		if (OutItems && Widget->GetType() == TEXT("SCheckBox"))
		{
			FModalItem Item;
			Item.Box = StaticCastSharedRef<SCheckBox>(Widget);
			Item.bChecked = Item.Box->IsChecked();
			OutItems->Add(Item);
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

		const bool bIsButton = Widget->GetType() == TEXT("SButton");
		if (bIsButton) ++ButtonDepth;
		FChildren* Children = Widget->GetChildren();
		if (Children)
		{
			for (int32 i = 0; i < Children->Num(); ++i)
			{
				TraverseWidgets(Children->GetChildAt(i));
			}
		}
		if (bIsButton) --ButtonDepth;
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

bool FDialogHandlers::DescribeActiveModal(FString& OutTitle, FString& OutMessage, TArray<FString>& OutButtons, bool* OutBlocksGameThread)
{
	OutButtons.Empty();
	if (OutBlocksGameThread)
	{
		*OutBlocksGameThread = false;
	}

	TArray<FModalButton> Buttons;
	EMCPPromptKind Kind = EMCPPromptKind::NotAPrompt;
	TSharedPtr<SWindow> Modal = CollectActiveModal(OutTitle, OutMessage, Buttons, nullptr, &Kind);
	if (!Modal.IsValid())
	{
		return false;
	}
	if (OutBlocksGameThread)
	{
		*OutBlocksGameThread = Kind == EMCPPromptKind::Modal;
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

	// What the walk rejected, reported alongside the dialogs (#1078).
	TArray<MCPDialogWindows::FSkippedWindow> Skipped;
	MCPDialogWindows::FindBlocking(&Skipped);
	if (Skipped.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> SkippedJson;
		for (const MCPDialogWindows::FSkippedWindow& Window : Skipped)
		{
			TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetStringField(TEXT("title"), Window.Title);
			Entry->SetStringField(TEXT("reason"), Window.Reason);
			SkippedJson.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Result->SetArrayField(TEXT("notTreatedAsDialogs"), SkippedJson);
	}

	FString Title;
	FString Message;
	TArray<FModalButton> Buttons;
	TArray<FModalItem> Items;
	EMCPPromptKind Kind = EMCPPromptKind::NotAPrompt;
	const bool bHasDialog = CollectActiveModal(Title, Message, Buttons, &Items, &Kind).IsValid();

	// Every open modal and floating window, as the ids the window actions take,
	// read here too because this is the call a refused client can still make.
	Result->SetArrayField(TEXT("windows"), MCPWindowControl::DescribeOpenWindows(FindGatingWindow()));

	if (bHasDialog)
	{
		TSharedPtr<FJsonObject> DialogObj = MakeShared<FJsonObject>();
		DialogObj->SetStringField(TEXT("title"), Title);
		// Whether the editor is actually held, and how the walk decided (#1118).
		// A parented non-modal window is a question worth reporting and is NOT
		// a reason to refuse a call: the game thread keeps ticking behind it,
		// so every action that would have worked still works.
		const bool bBlocks = Kind == EMCPPromptKind::Modal;
		DialogObj->SetBoolField(TEXT("blocksGameThread"), bBlocks);
		DialogObj->SetStringField(TEXT("detectedBy"), PromptKindName(Kind));
		DialogObj->SetStringField(TEXT("blockingNote"), bBlocks
			? TEXT("This window is on Slate's modal stack. The game thread is inside its loop, so every bridge method except the dialog ones is refused until a button is pressed.")
			: TEXT("This window is parented to the editor and is not modal, so the game thread keeps ticking and bridge calls keep working. It is reported because an unanswered question must never be quit out from under, not because anything is blocked."));
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
		// The per-item choices, when the dialog offers any. "Save Selected"
		// means nothing without them: the button is the same either way and the
		// ticks decide what it does.
		TArray<TSharedPtr<FJsonValue>> ItemsJsonArray;
		for (int32 ItemIndex = 0; ItemIndex < Items.Num(); ++ItemIndex)
		{
			const FModalItem& Item = Items[ItemIndex];
			TSharedPtr<FJsonObject> ItemObj = MakeShared<FJsonObject>();
			ItemObj->SetNumberField(TEXT("index"), ItemIndex);
			ItemObj->SetBoolField(TEXT("checked"), Item.bChecked);
			// The first cell is the row's label. A header row's "select all" box
			// owns the column headings instead, which is still reported: a caller
			// that ticks it is asking for exactly what that box does.
			ItemObj->SetStringField(TEXT("label"), Item.Cells.Num() > 0 ? Item.Cells[0] : FString());
			TArray<TSharedPtr<FJsonValue>> CellsJson;
			for (const FString& Cell : Item.Cells)
			{
				CellsJson.Add(MakeShared<FJsonValueString>(Cell));
			}
			ItemObj->SetArrayField(TEXT("cells"), CellsJson);
			ItemsJsonArray.Add(MakeShared<FJsonValueObject>(ItemObj));
		}
		DialogObj->SetArrayField(TEXT("items"), ItemsJsonArray);
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
			DialogObj->SetStringField(TEXT("policyResponse"), PolicyResponseString(*Policy));
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

bool FDialogHandlers::OpensAnIde(const FString& ButtonLabel, const FString& Message)
{
	return MCPDialogPolicy::OpensAnIde(ButtonLabel, Message);
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
	TArray<FModalItem> Items;
	TSharedPtr<SWindow> ActiveModal = CollectActiveModal(Title, Message, Buttons, &Items);
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

	// Nothing the bridge does may start an IDE on the user's desktop.
	if (Buttons.IsValidIndex(TargetIndex) && OpensAnIde(Buttons[TargetIndex].Label, Message))
	{
		return MCPError(FString::Printf(
			TEXT("Will not press '%s' on dialog '%s': it opens an IDE or source code editor. Press another of [%s], or dialogAction='close'."),
			*Buttons[TargetIndex].Label, *Title, *JoinLabels(Buttons)));
	}

	auto Result = MCPSuccess();

	// The per-item ticks go on BEFORE the button, in this one call.
	//
	// Setting them through a separate call would leave a window where the
	// selection is in place and nothing has pressed anything, and a modal is
	// exactly the state where a caller cannot be relied on to come back. So a
	// caller says what to tick and which button to press together, and the two
	// either both happen or neither does.
	//
	// ToggleCheckedState, not SetIsChecked: the dialog tracks its selection
	// through the checkbox's own delegate, so writing the visual state without
	// firing it would tick the box on screen and save the wrong packages.
	const TArray<TSharedPtr<FJsonValue>>* RequestedItems = nullptr;
	if (TryGetArrayParam(Params, TEXT("items"), RequestedItems) && RequestedItems)
	{
		TArray<TSharedPtr<FJsonValue>> AppliedJson;
		for (const TSharedPtr<FJsonValue>& Entry : *RequestedItems)
		{
			const TSharedPtr<FJsonObject>* ItemObj = nullptr;
			if (!Entry.IsValid() || !Entry->TryGetObject(ItemObj) || !ItemObj)
			{
				continue;
			}
			int32 Index = INDEX_NONE;
			bool bWanted = false;
			if (!(*ItemObj)->TryGetNumberField(TEXT("index"), Index)
				|| !(*ItemObj)->TryGetBoolField(TEXT("checked"), bWanted))
			{
				continue;
			}
			if (!Items.IsValidIndex(Index) || !Items[Index].Box.IsValid())
			{
				return MCPError(FString::Printf(
					TEXT("This dialog has %d tickable item(s), so item %d cannot be set. Read them with list_dialogs."),
					Items.Num(), Index));
			}
			if (Items[Index].bChecked != bWanted)
			{
				Items[Index].Box->ToggleCheckedState();
			}
			TSharedPtr<FJsonObject> Applied = MakeShared<FJsonObject>();
			Applied->SetNumberField(TEXT("index"), Index);
			Applied->SetBoolField(TEXT("checked"), bWanted);
			AppliedJson.Add(MakeShared<FJsonValueObject>(Applied));
		}
		Result->SetArrayField(TEXT("itemsApplied"), AppliedJson);
	}

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
		// dialogAction, never "action": that is the category tool's dispatch
		// field (#1078) and a routing name no spec may declare (#1057).
		const FString Action = OptionalString(Params, TEXT("dialogAction"));
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
				TEXT("Button not found on dialog '%s'. Pass buttonLabel as one of [%s], or buttonIndex between 0 and %d, or dialogAction='close'."),
				*Title, *JoinLabels(Buttons), FMath::Max(0, Buttons.Num() - 1)));
		}
	}

	return MCPResult(Result);
}

TSharedPtr<SWindow> FDialogHandlers::FindGatingWindow()
{
	FString Title;
	FString Message;
	TArray<FModalButton> Buttons;
	EMCPPromptKind Kind = EMCPPromptKind::NotAPrompt;
	const TSharedPtr<SWindow> Blocking = CollectActiveModal(Title, Message, Buttons, nullptr, &Kind);
	// The rule the dialog gate applies: a modal, titled or with a labelled button.
	const bool bDescribesAPrompt = !Title.IsEmpty()
		|| Buttons.ContainsByPredicate([](const FModalButton& Button) { return !Button.Label.IsEmpty(); });
	if (!Blocking.IsValid() || Kind != EMCPPromptKind::Modal || !bDescribesAPrompt)
	{
		return nullptr;
	}
	return Blocking;
}

TSharedPtr<FJsonValue> FDialogHandlers::ListWindows(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPWindowControl;

	int32 RequestedId = 0;
	const bool bHasWindowId = TryGetNumberParam(Params, TEXT("windowId"), RequestedId);
	const int32 MaxTexts = FMath::Clamp(OptionalInt(Params, TEXT("maxTexts"), 200), 1, 5000);
	const int32 MaxTextChars = FMath::Clamp(OptionalInt(Params, TEXT("maxTextChars"), 2000), 16, 100000);
	const int32 MaxButtons = FMath::Clamp(OptionalInt(Params, TEXT("maxButtons"), 200), 1, 5000);

	if (!FSlateApplication::IsInitialized())
	{
		return MCPError(TEXT("Slate not initialized"));
	}

	TArray<TSharedRef<SWindow>> Windows = OpenWindows();
	if (bHasWindowId)
	{
		const TSharedPtr<SWindow> Requested = FindOpenWindow(RequestedId);
		if (!Requested.IsValid())
		{
			return WindowGoneRefusal(RequestedId);
		}
		Windows.Reset();
		Windows.Add(Requested.ToSharedRef());
	}

	const TSharedPtr<SWindow> GatingWindow = FindGatingWindow();
	TArray<TSharedPtr<FJsonValue>> WindowsJson;
	for (const TSharedRef<SWindow>& Window : Windows)
	{
		const TSharedPtr<FJsonObject> Entry = DescribeWindow(Window, GatingWindow);
		AddWindowContent(Entry, CollectWindowContent(Window), MaxTexts, MaxTextChars, MaxButtons);
		WindowsJson.Add(MakeShared<FJsonValueObject>(Entry));
	}

	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("windows"), WindowsJson);
	Result->SetNumberField(TEXT("count"), WindowsJson.Num());
	if (GatingWindow.IsValid())
	{
		Result->SetNumberField(TEXT("gatingWindowId"), IdForWindow(GatingWindow.ToSharedRef()));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FDialogHandlers::PressWindowButton(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPWindowControl;

	int32 WindowIdParam = 0;
	const bool bHasWindowId = TryGetNumberParam(Params, TEXT("windowId"), WindowIdParam);
	const FString ButtonLabel = OptionalString(Params, TEXT("buttonLabel")).TrimStartAndEnd();
	int32 RequestedIndex = INDEX_NONE;
	const bool bHasIndex = TryGetNumberParam(Params, TEXT("buttonIndex"), RequestedIndex);

	if (!FSlateApplication::IsInitialized())
	{
		return MCPError(TEXT("Slate not initialized"));
	}

	TSharedPtr<FJsonValue> Refusal;
	const TSharedPtr<SWindow> Window = ResolveTargetWindow(bHasWindowId, WindowIdParam, TEXT("press_window_button"), Refusal);
	if (!Window.IsValid())
	{
		return Refusal;
	}

	const int32 WindowId = IdForWindow(Window.ToSharedRef());
	const FString WindowTitle = Window->GetTitle().ToString();
	const FWindowContent Content = CollectWindowContent(Window.ToSharedRef());

	TArray<int32> AllButtons;
	for (int32 Index = 0; Index < Content.Buttons.Num(); ++Index)
	{
		AllButtons.Add(Index);
	}

	if (ButtonLabel.IsEmpty() == !bHasIndex)
	{
		return ButtonRefusal(WindowId, WindowTitle,
			TEXT("Name the button with buttonLabel or with buttonIndex, exactly one of the two. buttons lists this window's buttons."),
			Content, AllButtons);
	}

	int32 Target = INDEX_NONE;
	if (bHasIndex)
	{
		if (!Content.Buttons.IsValidIndex(RequestedIndex))
		{
			return ButtonRefusal(WindowId, WindowTitle, FString::Printf(
				TEXT("Window %d has %d button(s), so there is no button %d. Nothing was pressed."),
				WindowId, Content.Buttons.Num(), RequestedIndex),
				Content, AllButtons);
		}
		Target = RequestedIndex;
	}
	else
	{
		// Exact before substring, and never a pick between two: "Save" is a
		// substring of "Don't Save", and a press cannot be taken back.
		const FString Wanted = MCPDialogPolicy::Normalise(ButtonLabel);
		TArray<int32> Exact;
		TArray<int32> Partial;
		for (int32 Index = 0; Index < Content.Buttons.Num(); ++Index)
		{
			const FString Offered = MCPDialogPolicy::Normalise(Content.Buttons[Index].Label);
			if (Offered.IsEmpty())
			{
				continue;
			}
			if (Offered == Wanted)
			{
				Exact.Add(Index);
			}
			else if (Offered.Contains(Wanted))
			{
				Partial.Add(Index);
			}
		}
		const TArray<int32>& Candidates = Exact.Num() > 0 ? Exact : Partial;
		if (Candidates.Num() == 0)
		{
			return ButtonRefusal(WindowId, WindowTitle, FString::Printf(
				TEXT("No button of window %d '%s' is labelled '%s'. Nothing was pressed. An icon-only button has no label: press it by buttonIndex."),
				WindowId, *WindowTitle, *ButtonLabel),
				Content, AllButtons);
		}
		if (Candidates.Num() > 1)
		{
			return ButtonRefusal(WindowId, WindowTitle, FString::Printf(
				TEXT("'%s' matches %d buttons of window %d. Nothing was pressed. Pass the buttonIndex of the one to press."),
				*ButtonLabel, Candidates.Num(), WindowId),
				Content, Candidates);
		}
		Target = Candidates[0];
	}

	const FWindowButton& Chosen = Content.Buttons[Target];
	if (!Chosen.bEnabled)
	{
		return ButtonRefusal(WindowId, WindowTitle, FString::Printf(
			TEXT("Button %d ('%s') of window %d is disabled, so it was not pressed. A person could not press it either."),
			Target, *Chosen.Label, WindowId),
			Content, { Target });
	}

	const FString ClickedLabel = Chosen.Label;
	const TSharedPtr<SButton> ClickedButton = Chosen.Button;
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Window %d '%s': pressing button %d '%s' as requested."),
		WindowId, *WindowTitle, Target, *ClickedLabel);

	// SimulateClick, for the reason respond_to_dialog gives: synthetic mouse
	// events miss the capture bookkeeping SButton decides a click with.
	ClickedButton->SimulateClick();

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("windowId"), WindowId);
	Result->SetStringField(TEXT("windowTitle"), WindowTitle);
	Result->SetStringField(TEXT("clickedButton"), ClickedLabel);
	Result->SetNumberField(TEXT("buttonIndex"), Target);
	// A retry of a press that landed finds the window gone (windowGone) or presses again.
	Result->SetBoolField(TEXT("alreadyClosed"), false);
	MCPSetNoRollback(Result, FString::Printf(
		TEXT("Pressed button %d '%s' of window %d '%s'. The editor code behind that button has already run and this call captured none of what it did, so there is nothing to restore."),
		Target, *ClickedLabel, WindowId, *WindowTitle));
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FDialogHandlers::CloseWindowById(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPWindowControl;

	int32 WindowIdParam = 0;
	const bool bHasWindowId = TryGetNumberParam(Params, TEXT("windowId"), WindowIdParam);

	if (!FSlateApplication::IsInitialized())
	{
		return MCPError(TEXT("Slate not initialized"));
	}

	TSharedPtr<FJsonValue> Refusal;
	const TSharedPtr<SWindow> Window = ResolveTargetWindow(bHasWindowId, WindowIdParam, TEXT("close_window"), Refusal);
	if (!Window.IsValid())
	{
		return Refusal;
	}

	const int32 WindowId = IdForWindow(Window.ToSharedRef());
	const FString WindowTitle = Window->GetTitle().ToString();
	const bool bWasModal = Window->IsModalWindow();
	const bool bHostedDockTabs = MCPDialogWindows::HostsDockedTabs(Window.ToSharedRef());

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Window %d '%s': close requested."), WindowId, *WindowTitle);

	// What the window's own close box does: a floating dock area asks each of
	// its tabs CanCloseTab first, and a modal ends its loop without an answer.
	Window->RequestDestroyWindow();

	auto Result = MCPSuccess();
	Result->SetNumberField(TEXT("windowId"), WindowId);
	Result->SetStringField(TEXT("windowTitle"), WindowTitle);
	Result->SetBoolField(TEXT("closeRequested"), true);
	Result->SetBoolField(TEXT("alreadyClosed"), false);
	Result->SetBoolField(TEXT("wasModal"), bWasModal);
	Result->SetBoolField(TEXT("hostsDockTabs"), bHostedDockTabs);
	Result->SetStringField(TEXT("note"),
		TEXT("Destruction is queued for the next Slate tick, and a floating dock area stays open when one of its tabs refuses to close. Read editor(list_windows) again to confirm the window is gone."));
	MCPSetNoRollback(Result, bWasModal
		? FString::Printf(
			TEXT("Asked the modal window %d '%s' to close without pressing any of its buttons. The editor resumes on its unanswered-prompt path, and no action re-raises a destroyed modal."),
			WindowId, *WindowTitle)
		: FString::Printf(
			TEXT("Asked window %d '%s' to close. No action reopens a window; reopen it from the editor's own menus."),
			WindowId, *WindowTitle));
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

FString FDialogHandlers::PolicyResponseString(const FDialogPolicy& Policy)
{
	return Policy.bHasResponse ? ResponseTypeToString(Policy.Response) : FString();
}
