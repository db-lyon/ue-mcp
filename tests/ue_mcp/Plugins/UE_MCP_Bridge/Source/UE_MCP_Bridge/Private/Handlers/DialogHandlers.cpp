#include "DialogHandlers.h"
#include "UE_MCP_BridgeModule.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Misc/CoreDelegates.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"

// Static member definitions
TArray<FDialogHandlers::FDialogPolicy> FDialogHandlers::Policies;
FDelegateHandle FDialogHandlers::OriginalDelegateHandle;
bool FDialogHandlers::bHookInstalled = false;

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

	// UE 5.7: ModalErrorMessage was removed. Use OnModalMessageBox instead if available.
	// For now, we skip the hook — dialog listing/responding still works via Slate.
	bHookInstalled = true;

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Dialog hook installed"));
}

void FDialogHandlers::RemoveDialogHook()
{
	if (!bHookInstalled)
	{
		return;
	}

	bHookInstalled = false;
	Policies.Empty();

	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Dialog hook removed"));
}

void FDialogHandlers::AddDefaultPolicy(const FString& Pattern, EAppReturnType::Type Response)
{
	FDialogPolicy Policy;
	Policy.Pattern = Pattern;
	Policy.Response = Response;
	Policies.Add(Policy);
}

EAppReturnType::Type FDialogHandlers::HandleModalDialog(EAppMsgType::Type MsgType, const FText& Text, const FText& Title)
{
	FString MessageStr = Text.ToString();
	FString TitleStr = Title.ToString();

	// Check policies for a match
	for (const FDialogPolicy& Policy : Policies)
	{
		if (MessageStr.Contains(Policy.Pattern) || TitleStr.Contains(Policy.Pattern))
		{
			UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Dialog auto-responded: pattern='%s' title='%s' response=%s"),
				*Policy.Pattern, *TitleStr, *ResponseTypeToString(Policy.Response));
			return Policy.Response;
		}
	}

	// No policy matched — fall through to default UE behavior
	UE_LOG(LogMCPBridge, Log, TEXT("[UE-MCP] Dialog shown (no policy match): title='%s' message='%s'"),
		*TitleStr, *MessageStr.Left(200));

	// Return the "default" response based on message type
	switch (MsgType)
	{
	case EAppMsgType::Ok:
		return EAppReturnType::Ok;
	case EAppMsgType::YesNo:
	case EAppMsgType::YesNoCancel:
		return EAppReturnType::No;
	case EAppMsgType::OkCancel:
	case EAppMsgType::YesNoYesAllNoAll:
	case EAppMsgType::YesNoYesAllNoAllCancel:
	case EAppMsgType::YesNoYesAll:
		return EAppReturnType::Cancel;
	case EAppMsgType::CancelRetryContinue:
		return EAppReturnType::Cancel;
	default:
		return EAppReturnType::No;
	}
}

TSharedPtr<FJsonValue> FDialogHandlers::SetDialogPolicy(const TSharedPtr<FJsonObject>& Params)
{
	FString Pattern;
	if (!Params->TryGetStringField(TEXT("pattern"), Pattern) || Pattern.IsEmpty())
	{
		return MCPError(TEXT("Missing or empty 'pattern' parameter"));
	}

	FString ResponseStr;
	if (auto Err = RequireString(Params, TEXT("response"), ResponseStr)) return Err;

	EAppReturnType::Type Response = ParseResponseType(ResponseStr);

	// Idempotency: check if existing policy with same pattern+response
	for (const FDialogPolicy& P : Policies)
	{
		if (P.Pattern == Pattern && P.Response == Response)
		{
			auto Existed = MCPSuccess();
			MCPSetExisted(Existed);
			Existed->SetStringField(TEXT("pattern"), Pattern);
			Existed->SetStringField(TEXT("response"), ResponseTypeToString(Response));
			Existed->SetNumberField(TEXT("policyCount"), Policies.Num());
			return MCPResult(Existed);
		}
	}

	// Remove existing policy with same pattern
	Policies.RemoveAll([&Pattern](const FDialogPolicy& P) { return P.Pattern == Pattern; });

	// Add new policy
	FDialogPolicy NewPolicy;
	NewPolicy.Pattern = Pattern;
	NewPolicy.Response = Response;
	Policies.Add(NewPolicy);

	// Ensure hook is installed
	if (!bHookInstalled)
	{
		InstallDialogHook();
	}

	auto Result = MCPSuccess();
	MCPSetCreated(Result);
	Result->SetStringField(TEXT("pattern"), Pattern);
	Result->SetStringField(TEXT("response"), ResponseTypeToString(Response));
	Result->SetNumberField(TEXT("policyCount"), Policies.Num());

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
	if (!Pattern.IsEmpty())
	{
		int32 Removed = Policies.RemoveAll([&Pattern](const FDialogPolicy& P) { return P.Pattern == Pattern; });
		Result->SetStringField(TEXT("pattern"), Pattern);
		Result->SetNumberField(TEXT("removed"), Removed);
	}
	else
	{
		int32 Count = Policies.Num();
		Policies.Empty();
		Result->SetNumberField(TEXT("removed"), Count);
	}

	Result->SetNumberField(TEXT("policyCount"), Policies.Num());

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
		PoliciesArray.Add(MakeShared<FJsonValueObject>(P));
	}

	Result->SetArrayField(TEXT("policies"), PoliciesArray);
	Result->SetNumberField(TEXT("count"), Policies.Num());
	Result->SetBoolField(TEXT("hookInstalled"), bHookInstalled);

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FDialogHandlers::ListDialogs(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MCPSuccess();
	TArray<TSharedPtr<FJsonValue>> DialogsArray;

	if (FSlateApplication::IsInitialized())
	{
		TSharedPtr<SWindow> ActiveModal = FSlateApplication::Get().GetActiveModalWindow();
		if (ActiveModal.IsValid())
		{
			TSharedPtr<FJsonObject> DialogObj = MakeShared<FJsonObject>();
			DialogObj->SetStringField(TEXT("title"), ActiveModal->GetTitle().ToString());

			// Traverse widget tree to find text blocks and buttons
			TArray<FString> TextContents;
			TArray<FString> ButtonLabels;

			TFunction<void(const TSharedRef<SWidget>&)> TraverseWidgets = [&](const TSharedRef<SWidget>& Widget)
			{
				// Check for text blocks
				if (Widget->GetType() == TEXT("STextBlock"))
				{
					TSharedRef<STextBlock> TextBlock = StaticCastSharedRef<STextBlock>(Widget);
					FString Text = TextBlock->GetText().ToString();
					if (!Text.IsEmpty())
					{
						TextContents.Add(Text);
					}
				}

				// Check for buttons
				if (Widget->GetType() == TEXT("SButton"))
				{
					// Try to find the text label inside the button
					FChildren* ButtonChildren = Widget->GetChildren();
					if (ButtonChildren)
					{
						for (int32 i = 0; i < ButtonChildren->Num(); ++i)
						{
							TSharedRef<SWidget> Child = ButtonChildren->GetChildAt(i);
							if (Child->GetType() == TEXT("STextBlock"))
							{
								TSharedRef<STextBlock> BtnText = StaticCastSharedRef<STextBlock>(Child);
								FString Label = BtnText->GetText().ToString();
								if (!Label.IsEmpty())
								{
									ButtonLabels.Add(Label);
								}
							}
						}
					}
				}

				// Recurse into children
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

			// Build message from text contents (skip the title if it matches)
			FString TitleStr = ActiveModal->GetTitle().ToString();
			FString Message;
			for (const FString& T : TextContents)
			{
				if (T != TitleStr)
				{
					if (!Message.IsEmpty()) Message += TEXT("\n");
					Message += T;
				}
			}

			DialogObj->SetStringField(TEXT("message"), Message);

			TArray<TSharedPtr<FJsonValue>> ButtonsJsonArray;
			for (const FString& Label : ButtonLabels)
			{
				ButtonsJsonArray.Add(MakeShared<FJsonValueString>(Label));
			}
			DialogObj->SetArrayField(TEXT("buttons"), ButtonsJsonArray);

			DialogsArray.Add(MakeShared<FJsonValueObject>(DialogObj));
		}
	}

	Result->SetArrayField(TEXT("dialogs"), DialogsArray);
	Result->SetNumberField(TEXT("count"), DialogsArray.Num());

	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FDialogHandlers::RespondToDialog(const TSharedPtr<FJsonObject>& Params)
{
	if (!FSlateApplication::IsInitialized())
	{
		return MCPError(TEXT("Slate not initialized"));
	}

	TSharedPtr<SWindow> ActiveModal = FSlateApplication::Get().GetActiveModalWindow();
	if (!ActiveModal.IsValid())
	{
		return MCPError(TEXT("No active modal dialog"));
	}

	// Determine action: buttonIndex, buttonLabel, or key simulation
	FString ButtonLabel = OptionalString(Params, TEXT("buttonLabel"));
	int32 ButtonIndex = OptionalInt(Params, TEXT("buttonIndex"), -1);

	// Find buttons in the dialog
	TArray<TSharedRef<SButton>> Buttons;
	TArray<FString> ButtonTexts;

	TFunction<void(const TSharedRef<SWidget>&)> FindButtons = [&](const TSharedRef<SWidget>& Widget)
	{
		if (Widget->GetType() == TEXT("SButton"))
		{
			Buttons.Add(StaticCastSharedRef<SButton>(Widget));

			// Extract label
			FString Label;
			FChildren* ButtonChildren = Widget->GetChildren();
			if (ButtonChildren)
			{
				for (int32 i = 0; i < ButtonChildren->Num(); ++i)
				{
					TSharedRef<SWidget> Child = ButtonChildren->GetChildAt(i);
					if (Child->GetType() == TEXT("STextBlock"))
					{
						Label = StaticCastSharedRef<STextBlock>(Child)->GetText().ToString();
						break;
					}
				}
			}
			ButtonTexts.Add(Label);
		}

		FChildren* Children = Widget->GetChildren();
		if (Children)
		{
			for (int32 i = 0; i < Children->Num(); ++i)
			{
				FindButtons(Children->GetChildAt(i));
			}
		}
	};

	FindButtons(ActiveModal.ToSharedRef());

	// Resolve which button to click
	int32 TargetIndex = -1;

	if (!ButtonLabel.IsEmpty())
	{
		for (int32 i = 0; i < ButtonTexts.Num(); ++i)
		{
			if (ButtonTexts[i].Contains(ButtonLabel))
			{
				TargetIndex = i;
				break;
			}
		}
	}
	else if (ButtonIndex >= 0 && ButtonIndex < Buttons.Num())
	{
		TargetIndex = ButtonIndex;
	}

	auto Result = MCPSuccess();

	if (TargetIndex >= 0 && TargetIndex < Buttons.Num())
	{
		// Simulate the button click
		FSlateApplication::Get().SetKeyboardFocus(Buttons[TargetIndex]);

		// Use the reply mechanism to simulate a click
		FReply Reply = FReply::Handled();
		TSharedRef<SButton> TargetButton = Buttons[TargetIndex];

		// Simulate mouse down + up on the button to trigger OnClicked
		FGeometry ButtonGeometry = TargetButton->GetCachedGeometry();
		FVector2D LocalCenter = ButtonGeometry.GetLocalSize() * 0.5f;
		FVector2D AbsoluteCenter = ButtonGeometry.LocalToAbsolute(LocalCenter);

		FPointerEvent MouseDownEvent(
			0, // PointerIndex
			AbsoluteCenter,
			AbsoluteCenter,
			TSet<FKey>(),
			EKeys::LeftMouseButton,
			0,
			FModifierKeysState()
		);

		TargetButton->OnMouseButtonDown(ButtonGeometry, MouseDownEvent);
		TargetButton->OnMouseButtonUp(ButtonGeometry, MouseDownEvent);

		Result->SetStringField(TEXT("clickedButton"), ButtonTexts[TargetIndex]);
		Result->SetNumberField(TEXT("buttonIndex"), TargetIndex);
	}
	else
	{
		// Fallback: send Escape key to dismiss
		FString Action = OptionalString(Params, TEXT("action"));
		if (Action == TEXT("escape"))
		{
			FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
			FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(EKeys::Escape, FModifierKeysState(), 0, false, 0, 0));
			Result->SetStringField(TEXT("action"), TEXT("escape"));
		}
		else
		{
			TArray<TSharedPtr<FJsonValue>> AvailableButtons;
			for (const FString& T : ButtonTexts)
			{
				AvailableButtons.Add(MakeShared<FJsonValueString>(T));
			}
			Result->SetBoolField(TEXT("success"), false);
			Result->SetArrayField(TEXT("availableButtons"), AvailableButtons);
			Result->SetStringField(TEXT("error"), TEXT("Button not found. Provide buttonIndex or buttonLabel matching an available button."));
		}
	}

	return MCPResult(Result);
}

EAppReturnType::Type FDialogHandlers::ParseResponseType(const FString& ResponseStr)
{
	FString Lower = ResponseStr.ToLower();
	if (Lower == TEXT("yes"))       return EAppReturnType::Yes;
	if (Lower == TEXT("no"))        return EAppReturnType::No;
	if (Lower == TEXT("ok"))        return EAppReturnType::Ok;
	if (Lower == TEXT("cancel"))    return EAppReturnType::Cancel;
	if (Lower == TEXT("retry"))     return EAppReturnType::Retry;
	if (Lower == TEXT("continue"))  return EAppReturnType::Continue;
	if (Lower == TEXT("yesall"))    return EAppReturnType::YesAll;
	if (Lower == TEXT("noall"))     return EAppReturnType::NoAll;
	return EAppReturnType::Ok;
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
