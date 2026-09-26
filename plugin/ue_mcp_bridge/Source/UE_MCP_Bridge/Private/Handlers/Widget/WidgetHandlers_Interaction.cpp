#include "WidgetHandlers.h"
#include "HandlerUtils.h"

#include "Components/Widget.h"
#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableText.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableText.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/Slider.h"
#include "Components/SpinBox.h"
#include "Types/SlateEnums.h"

// ─────────────────────────────────────────────────────────────
// #812  Runtime child-widget interaction.
//
// invoke_runtime_function understood exactly one interaction: UButton::OnClicked.
// A live UI pass could click buttons, but a checkbox row, a slider, a text field
// or a combo box could not be driven at all, so those rows stayed unverified.
//
// Each interaction below does two things: it writes the new state through the
// widget's own setter (so the UPROPERTY and the Slate widget agree), and it
// makes sure the delegate a real interaction fires is broadcast exactly once,
// because that broadcast is what runs the Blueprint graph bound to the event.
// Setting the property alone would leave the graph unrun.
//
// A few UMG setters broadcast on their own (USlider::SetValue when the value
// actually changes, UComboBoxString::SetSelectedIndex on a real change), so
// those cases only broadcast by hand when the engine path was a no-op. Every
// other setter here is silent, so the broadcast is always ours.
// ─────────────────────────────────────────────────────────────

namespace WidgetInteraction_Internal
{
	/** Widget classes invoke_runtime_function can drive, for the unsupported-class error. */
	static const TCHAR* SupportedClasses =
		TEXT("Button, CheckBox, Slider, SpinBox, EditableText, EditableTextBox, "
		     "MultiLineEditableText, MultiLineEditableTextBox, ComboBoxString");

	/** The `value` param as a raw JSON value, or an unset pointer when absent/null. */
	static TSharedPtr<FJsonValue> ReadValueField(const TSharedPtr<FJsonObject>& Params)
	{
		if (!Params.IsValid()) return TSharedPtr<FJsonValue>();
		TSharedPtr<FJsonValue> Field = TryGetParam(Params, TEXT("value"));
		if (Field.IsValid() && Field->Type == EJson::Null) return TSharedPtr<FJsonValue>();
		return Field;
	}

	/** Accept a number as JSON number, numeric string, or bool. */
	static bool ValueAsNumber(const TSharedPtr<FJsonValue>& Value, double& Out)
	{
		if (!Value.IsValid()) return false;
		if (Value->Type == EJson::Number)  { Out = Value->AsNumber(); return true; }
		if (Value->Type == EJson::Boolean) { Out = Value->AsBool() ? 1.0 : 0.0; return true; }
		if (Value->Type == EJson::String)
		{
			const FString Text = Value->AsString().TrimStartAndEnd();
			if (Text.IsNumeric()) { Out = FCString::Atod(*Text); return true; }
		}
		return false;
	}

	/** Accept text as a JSON string, number or bool so numeric fields can be typed into. */
	static bool ValueAsString(const TSharedPtr<FJsonValue>& Value, FString& Out)
	{
		if (!Value.IsValid()) return false;
		if (Value->Type == EJson::String)  { Out = Value->AsString(); return true; }
		if (Value->Type == EJson::Number)  { Out = FString::SanitizeFloat(Value->AsNumber()); return true; }
		if (Value->Type == EJson::Boolean) { Out = Value->AsBool() ? TEXT("true") : TEXT("false"); return true; }
		return false;
	}

	enum class ECheckRequest : uint8 { Checked, Unchecked, Undetermined, Toggle };

	/** Checkbox `value`: bool, 0/1, "checked"/"unchecked"/"undetermined"/"toggle". Absent means toggle. */
	static bool ParseCheckRequest(const TSharedPtr<FJsonValue>& Value, ECheckRequest& Out)
	{
		if (!Value.IsValid()) { Out = ECheckRequest::Toggle; return true; }
		if (Value->Type == EJson::Boolean)
		{
			Out = Value->AsBool() ? ECheckRequest::Checked : ECheckRequest::Unchecked;
			return true;
		}
		if (Value->Type == EJson::Number)
		{
			Out = FMath::IsNearlyZero(Value->AsNumber()) ? ECheckRequest::Unchecked : ECheckRequest::Checked;
			return true;
		}
		if (Value->Type == EJson::String)
		{
			const FString Text = Value->AsString().TrimStartAndEnd();
			if (Text.Equals(TEXT("toggle"), ESearchCase::IgnoreCase))
			{
				Out = ECheckRequest::Toggle;
				return true;
			}
			if (Text.Equals(TEXT("undetermined"), ESearchCase::IgnoreCase) ||
				Text.Equals(TEXT("indeterminate"), ESearchCase::IgnoreCase))
			{
				Out = ECheckRequest::Undetermined;
				return true;
			}
			if (Text.Equals(TEXT("checked"), ESearchCase::IgnoreCase) ||
				Text.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
				Text.Equals(TEXT("on"), ESearchCase::IgnoreCase) ||
				Text == TEXT("1"))
			{
				Out = ECheckRequest::Checked;
				return true;
			}
			if (Text.Equals(TEXT("unchecked"), ESearchCase::IgnoreCase) ||
				Text.Equals(TEXT("false"), ESearchCase::IgnoreCase) ||
				Text.Equals(TEXT("off"), ESearchCase::IgnoreCase) ||
				Text == TEXT("0"))
			{
				Out = ECheckRequest::Unchecked;
				return true;
			}
		}
		return false;
	}

	static FString CheckStateToString(ECheckBoxState State)
	{
		switch (State)
		{
			case ECheckBoxState::Checked:   return TEXT("checked");
			case ECheckBoxState::Unchecked: return TEXT("unchecked");
			default:                        return TEXT("undetermined");
		}
	}

	/** `commitMethod` for text and spin box commits. Defaults to the Enter key. */
	static bool ParseCommitMethod(const FString& Name, ETextCommit::Type& Out)
	{
		if (Name.IsEmpty() || Name.Equals(TEXT("OnEnter"), ESearchCase::IgnoreCase))
		{
			Out = ETextCommit::OnEnter;
			return true;
		}
		if (Name.Equals(TEXT("Default"), ESearchCase::IgnoreCase))
		{
			Out = ETextCommit::Default;
			return true;
		}
		if (Name.Equals(TEXT("OnUserMovedFocus"), ESearchCase::IgnoreCase))
		{
			Out = ETextCommit::OnUserMovedFocus;
			return true;
		}
		if (Name.Equals(TEXT("OnCleared"), ESearchCase::IgnoreCase))
		{
			Out = ETextCommit::OnCleared;
			return true;
		}
		return false;
	}

	static FString CommitMethodToString(ETextCommit::Type Method)
	{
		switch (Method)
		{
			case ETextCommit::OnEnter:          return TEXT("OnEnter");
			case ETextCommit::OnUserMovedFocus: return TEXT("OnUserMovedFocus");
			case ETextCommit::OnCleared:        return TEXT("OnCleared");
			default:                            return TEXT("Default");
		}
	}

	static bool NameMatches(const FString& Requested, const TCHAR* DelegateName)
	{
		return Requested.Equals(DelegateName, ESearchCase::IgnoreCase);
	}

	/** Error for a functionName that is not one of the widget's simulatable delegates. */
	static TSharedPtr<FJsonValue> UnsupportedDelegate(
		const UWidget* Target,
		const FString& Requested,
		const TCHAR* Supported)
	{
		return MCPError(FString::Printf(
			TEXT("'%s' is not a simulatable delegate on %s '%s'. Supported: %s"),
			*Requested,
			*Target->GetClass()->GetName(),
			*Target->GetName(),
			Supported));
	}

	static void RecordDelegates(const TSharedPtr<FJsonObject>& OutInfo, const TArray<FString>& Names)
	{
		TArray<TSharedPtr<FJsonValue>> Arr;
		for (const FString& Name : Names)
		{
			Arr.Add(MakeShared<FJsonValueString>(Name));
		}
		OutInfo->SetArrayField(TEXT("delegates"), Arr);
		if (Names.Num() > 0)
		{
			// `invoked` stays the single primary delegate name so existing
			// button callers keep reading the same field.
			OutInfo->SetStringField(TEXT("invoked"), Names.Last());
		}
	}
}

TSharedPtr<FJsonValue> FWidgetHandlers::SimulateRuntimeChildInteraction(
	UWidget* Target,
	const TSharedPtr<FJsonObject>& Params,
	const TSharedPtr<FJsonObject>& OutInfo)
{
	using namespace WidgetInteraction_Internal;

	if (!Target || !OutInfo.IsValid())
	{
		return MCPError(TEXT("No child widget to interact with"));
	}

	const FString Requested = OptionalString(Params, TEXT("functionName"));
	const TSharedPtr<FJsonValue> Value = ReadValueField(Params);
	OutInfo->SetStringField(TEXT("childClass"), Target->GetClass()->GetName());

	// ── UButton: a click, or one of the other pointer delegates ──────────────
	if (UButton* Button = Cast<UButton>(Target))
	{
		OutInfo->SetStringField(TEXT("interaction"), TEXT("click"));
		if (Requested.IsEmpty() || NameMatches(Requested, TEXT("OnClicked")))
		{
			Button->OnClicked.Broadcast();
			RecordDelegates(OutInfo, TArray<FString>{ TEXT("OnClicked") });
		}
		else if (NameMatches(Requested, TEXT("OnPressed")))
		{
			Button->OnPressed.Broadcast();
			RecordDelegates(OutInfo, TArray<FString>{ TEXT("OnPressed") });
		}
		else if (NameMatches(Requested, TEXT("OnReleased")))
		{
			Button->OnReleased.Broadcast();
			RecordDelegates(OutInfo, TArray<FString>{ TEXT("OnReleased") });
		}
		else if (NameMatches(Requested, TEXT("OnHovered")))
		{
			Button->OnHovered.Broadcast();
			RecordDelegates(OutInfo, TArray<FString>{ TEXT("OnHovered") });
		}
		else if (NameMatches(Requested, TEXT("OnUnhovered")))
		{
			Button->OnUnhovered.Broadcast();
			RecordDelegates(OutInfo, TArray<FString>{ TEXT("OnUnhovered") });
		}
		else
		{
			return UnsupportedDelegate(Target, Requested,
				TEXT("OnClicked, OnPressed, OnReleased, OnHovered, OnUnhovered"));
		}
		return TSharedPtr<FJsonValue>();
	}

	// ── UCheckBox: set the state, then broadcast OnCheckStateChanged ─────────
	if (UCheckBox* CheckBox = Cast<UCheckBox>(Target))
	{
		if (!Requested.IsEmpty() && !NameMatches(Requested, TEXT("OnCheckStateChanged")))
		{
			return UnsupportedDelegate(Target, Requested, TEXT("OnCheckStateChanged"));
		}

		ECheckRequest Request = ECheckRequest::Toggle;
		if (!ParseCheckRequest(Value, Request))
		{
			return MCPError(FString::Printf(
				TEXT("CheckBox '%s': value must be a bool, 0/1, or one of \"checked\", \"unchecked\", \"undetermined\", \"toggle\" (omit value to toggle)"),
				*Target->GetName()));
		}

		const ECheckBoxState Previous = CheckBox->GetCheckedState();
		ECheckBoxState NewState = ECheckBoxState::Checked;
		switch (Request)
		{
			case ECheckRequest::Checked:      NewState = ECheckBoxState::Checked; break;
			case ECheckRequest::Unchecked:    NewState = ECheckBoxState::Unchecked; break;
			case ECheckRequest::Undetermined: NewState = ECheckBoxState::Undetermined; break;
			case ECheckRequest::Toggle:
				NewState = (Previous == ECheckBoxState::Checked)
					? ECheckBoxState::Unchecked
					: ECheckBoxState::Checked;
				break;
		}

		// SetCheckedState writes the property and the Slate widget but never
		// broadcasts, so the graph only runs because of the broadcast below.
		CheckBox->SetCheckedState(NewState);
		CheckBox->OnCheckStateChanged.Broadcast(NewState == ECheckBoxState::Checked);

		OutInfo->SetStringField(TEXT("interaction"), TEXT("check"));
		OutInfo->SetStringField(TEXT("previousValue"), CheckStateToString(Previous));
		OutInfo->SetStringField(TEXT("value"), CheckStateToString(NewState));
		OutInfo->SetBoolField(TEXT("isChecked"), NewState == ECheckBoxState::Checked);
		RecordDelegates(OutInfo, TArray<FString>{ TEXT("OnCheckStateChanged") });
		return TSharedPtr<FJsonValue>();
	}

	// ── USlider: set the value, then make sure OnValueChanged fired once ─────
	if (USlider* Slider = Cast<USlider>(Target))
	{
		if (!Requested.IsEmpty() && !NameMatches(Requested, TEXT("OnValueChanged")))
		{
			return UnsupportedDelegate(Target, Requested, TEXT("OnValueChanged"));
		}

		double Number = 0.0;
		if (!ValueAsNumber(Value, Number))
		{
			return MCPError(FString::Printf(
				TEXT("Slider '%s': value is required and must be a number"),
				*Target->GetName()));
		}

		const float Previous = Slider->GetValue();
		const float MinValue = Slider->GetMinValue();
		const float MaxValue = Slider->GetMaxValue();
		const float NewValue = (MaxValue > MinValue)
			? FMath::Clamp(static_cast<float>(Number), MinValue, MaxValue)
			: static_cast<float>(Number);

		// USlider::SetValue broadcasts OnValueChanged itself, but only when the
		// value actually moves. Re-driving the slider to the value it already
		// holds is still a request to run the graph, so cover that case here.
		// The comparison is exact because the engine's own guard is exact: an
		// approximate one here would double-broadcast on a hair-width move.
		Slider->SetValue(NewValue);
		if (Previous == NewValue)
		{
			Slider->OnValueChanged.Broadcast(NewValue);
		}

		OutInfo->SetStringField(TEXT("interaction"), TEXT("slide"));
		OutInfo->SetNumberField(TEXT("previousValue"), Previous);
		OutInfo->SetNumberField(TEXT("value"), NewValue);
		OutInfo->SetNumberField(TEXT("requestedValue"), Number);
		RecordDelegates(OutInfo, TArray<FString>{ TEXT("OnValueChanged") });
		return TSharedPtr<FJsonValue>();
	}

	// ── USpinBox: set the value, then broadcast changed plus committed ───────
	if (USpinBox* SpinBox = Cast<USpinBox>(Target))
	{
		const bool bWantsChanged   = Requested.IsEmpty() || NameMatches(Requested, TEXT("OnValueChanged"));
		const bool bWantsCommitted = Requested.IsEmpty() || NameMatches(Requested, TEXT("OnValueCommitted"));
		if (!bWantsChanged && !bWantsCommitted)
		{
			return UnsupportedDelegate(Target, Requested, TEXT("OnValueChanged, OnValueCommitted"));
		}

		double Number = 0.0;
		if (!ValueAsNumber(Value, Number))
		{
			return MCPError(FString::Printf(
				TEXT("SpinBox '%s': value is required and must be a number"),
				*Target->GetName()));
		}

		ETextCommit::Type CommitMethod = ETextCommit::OnEnter;
		const FString CommitName = OptionalString(Params, TEXT("commitMethod"));
		if (!ParseCommitMethod(CommitName, CommitMethod))
		{
			return MCPError(FString::Printf(
				TEXT("Unknown commitMethod '%s'. Use OnEnter, OnUserMovedFocus, OnCleared or Default"),
				*CommitName));
		}

		const float Previous = SpinBox->GetValue();
		const float NewValue = static_cast<float>(Number);
		// USpinBox::SetValue is silent, so both delegates are ours to fire.
		SpinBox->SetValue(NewValue);

		TArray<FString> Fired;
		if (bWantsChanged)
		{
			SpinBox->OnValueChanged.Broadcast(NewValue);
			Fired.Add(TEXT("OnValueChanged"));
		}
		if (bWantsCommitted)
		{
			SpinBox->OnValueCommitted.Broadcast(NewValue, CommitMethod);
			Fired.Add(TEXT("OnValueCommitted"));
		}

		OutInfo->SetStringField(TEXT("interaction"), TEXT("spin"));
		OutInfo->SetNumberField(TEXT("previousValue"), Previous);
		OutInfo->SetNumberField(TEXT("value"), NewValue);
		OutInfo->SetStringField(TEXT("commitMethod"), CommitMethodToString(CommitMethod));
		RecordDelegates(OutInfo, Fired);
		return TSharedPtr<FJsonValue>();
	}

	// ── Text entry: set the text, then broadcast changed plus committed ──────
	{
		UEditableText*             SingleLine    = Cast<UEditableText>(Target);
		UEditableTextBox*          SingleLineBox = Cast<UEditableTextBox>(Target);
		UMultiLineEditableText*    MultiLine     = Cast<UMultiLineEditableText>(Target);
		UMultiLineEditableTextBox* MultiLineBox  = Cast<UMultiLineEditableTextBox>(Target);

		if (SingleLine || SingleLineBox || MultiLine || MultiLineBox)
		{
			const bool bWantsChanged   = Requested.IsEmpty() || NameMatches(Requested, TEXT("OnTextChanged"));
			const bool bWantsCommitted = Requested.IsEmpty() || NameMatches(Requested, TEXT("OnTextCommitted"));
			if (!bWantsChanged && !bWantsCommitted)
			{
				return UnsupportedDelegate(Target, Requested, TEXT("OnTextChanged, OnTextCommitted"));
			}

			FString NewText;
			if (!ValueAsString(Value, NewText))
			{
				return MCPError(FString::Printf(
					TEXT("%s '%s': value is required and must be a string"),
					*Target->GetClass()->GetName(),
					*Target->GetName()));
			}

			ETextCommit::Type CommitMethod = ETextCommit::OnEnter;
			const FString CommitName = OptionalString(Params, TEXT("commitMethod"));
			if (!ParseCommitMethod(CommitName, CommitMethod))
			{
				return MCPError(FString::Printf(
					TEXT("Unknown commitMethod '%s'. Use OnEnter, OnUserMovedFocus, OnCleared or Default"),
					*CommitName));
			}

			const FText Text = FText::FromString(NewText);
			FString Previous;
			TArray<FString> Fired;

			// None of the UMG SetText overloads broadcast the text delegates,
			// so typing into a field only reaches Blueprint through these.
			if (SingleLine)
			{
				Previous = SingleLine->GetText().ToString();
				SingleLine->SetText(Text);
				if (bWantsChanged)   { SingleLine->OnTextChanged.Broadcast(Text); Fired.Add(TEXT("OnTextChanged")); }
				if (bWantsCommitted) { SingleLine->OnTextCommitted.Broadcast(Text, CommitMethod); Fired.Add(TEXT("OnTextCommitted")); }
			}
			else if (SingleLineBox)
			{
				Previous = SingleLineBox->GetText().ToString();
				SingleLineBox->SetText(Text);
				if (bWantsChanged)   { SingleLineBox->OnTextChanged.Broadcast(Text); Fired.Add(TEXT("OnTextChanged")); }
				if (bWantsCommitted) { SingleLineBox->OnTextCommitted.Broadcast(Text, CommitMethod); Fired.Add(TEXT("OnTextCommitted")); }
			}
			else if (MultiLine)
			{
				Previous = MultiLine->GetText().ToString();
				MultiLine->SetText(Text);
				if (bWantsChanged)   { MultiLine->OnTextChanged.Broadcast(Text); Fired.Add(TEXT("OnTextChanged")); }
				if (bWantsCommitted) { MultiLine->OnTextCommitted.Broadcast(Text, CommitMethod); Fired.Add(TEXT("OnTextCommitted")); }
			}
			else
			{
				Previous = MultiLineBox->GetText().ToString();
				MultiLineBox->SetText(Text);
				if (bWantsChanged)   { MultiLineBox->OnTextChanged.Broadcast(Text); Fired.Add(TEXT("OnTextChanged")); }
				if (bWantsCommitted) { MultiLineBox->OnTextCommitted.Broadcast(Text, CommitMethod); Fired.Add(TEXT("OnTextCommitted")); }
			}

			OutInfo->SetStringField(TEXT("interaction"), TEXT("text"));
			OutInfo->SetStringField(TEXT("previousValue"), Previous);
			OutInfo->SetStringField(TEXT("value"), NewText);
			OutInfo->SetStringField(TEXT("commitMethod"), CommitMethodToString(CommitMethod));
			RecordDelegates(OutInfo, Fired);
			return TSharedPtr<FJsonValue>();
		}
	}

	// ── UComboBoxString: pick an option, then confirm OnSelectionChanged ─────
	if (UComboBoxString* ComboBox = Cast<UComboBoxString>(Target))
	{
		if (!Requested.IsEmpty() && !NameMatches(Requested, TEXT("OnSelectionChanged")))
		{
			return UnsupportedDelegate(Target, Requested, TEXT("OnSelectionChanged"));
		}

		// A string value names the option; a number picks it by index.
		FString Option;
		if (Value.IsValid() && Value->Type == EJson::String)
		{
			Option = Value->AsString();
		}
		else if (Value.IsValid() && Value->Type == EJson::Number)
		{
			const int32 Index = static_cast<int32>(FMath::RoundToInt(Value->AsNumber()));
			if (Index < 0 || Index >= ComboBox->GetOptionCount())
			{
				return MCPError(FString::Printf(
					TEXT("ComboBox '%s': index %d is out of range (%d options)"),
					*Target->GetName(), Index, ComboBox->GetOptionCount()));
			}
			Option = ComboBox->GetOptionAtIndex(Index);
		}
		else
		{
			return MCPError(FString::Printf(
				TEXT("ComboBox '%s': value is required and must be an option string or option index"),
				*Target->GetName()));
		}

		if (ComboBox->FindOptionIndex(Option) == INDEX_NONE)
		{
			FString Available;
			for (int32 Index = 0; Index < ComboBox->GetOptionCount(); ++Index)
			{
				if (!Available.IsEmpty()) Available += TEXT(", ");
				Available += ComboBox->GetOptionAtIndex(Index);
			}
			return MCPError(FString::Printf(
				TEXT("ComboBox '%s' has no option '%s'. Options: %s"),
				*Target->GetName(), *Option, Available.IsEmpty() ? TEXT("(none)") : *Available));
		}

		const FString Previous = ComboBox->GetSelectedOption();
		// SetSelectedOption broadcasts OnSelectionChanged itself on a real
		// change (through Slate when the widget is realised, directly when it
		// is not). Selecting the option that is already current is a no-op
		// there, so that is the one case this has to broadcast.
		ComboBox->SetSelectedOption(Option);
		if (Previous == Option)
		{
			ComboBox->OnSelectionChanged.Broadcast(Option, ESelectInfo::Direct);
		}

		const FString Applied = ComboBox->GetSelectedOption();
		if (Applied != Option)
		{
			return MCPError(FString::Printf(
				TEXT("ComboBox '%s': selection stayed on '%s' after selecting '%s'"),
				*Target->GetName(), *Applied, *Option));
		}

		OutInfo->SetStringField(TEXT("interaction"), TEXT("select"));
		OutInfo->SetStringField(TEXT("previousValue"), Previous);
		OutInfo->SetStringField(TEXT("value"), Applied);
		OutInfo->SetNumberField(TEXT("selectedIndex"), ComboBox->FindOptionIndex(Applied));
		OutInfo->SetStringField(TEXT("selectInfo"), TEXT("Direct"));
		RecordDelegates(OutInfo, TArray<FString>{ TEXT("OnSelectionChanged") });
		return TSharedPtr<FJsonValue>();
	}

	return MCPError(FString::Printf(
		TEXT("Child '%s' is a %s, which has no simulatable interaction. Supported classes: %s"),
		*Target->GetName(), *Target->GetClass()->GetName(), SupportedClasses));
}
