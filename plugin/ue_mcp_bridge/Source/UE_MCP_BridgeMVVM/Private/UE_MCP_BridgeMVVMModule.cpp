// Viewmodel authoring over the UMG Viewmodel (ModelViewViewModel) editor API.
// See UE_MCP_BridgeMVVM.Build.cs for why this is a module of its own, and
// Public/HandlerMVVMBackend.h in UE_MCP_Bridge for the contract with the
// widget handlers.
//
// Paths are written the way the viewmodel editor shows them: the first segment
// names where the path starts (a viewmodel by name, a widget in the tree, or
// Self for the widget blueprint itself), and the rest walk properties, with a
// function allowed as the last segment. "PlayerVM.Health", "HealthBar.Percent",
// "Self.Title".

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "HandlerMVVMBackend.h"
#include "HandlerParams.h"
#include "HandlerEngineVersion.h"

#if WITH_UEMCP_MVVM
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "INotifyFieldValueChanged.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/Kismet2NameValidators.h"
#include "MVVMEditorSubsystem.h"
#include "MVVMBlueprintView.h"
#include "MVVMBlueprintViewBinding.h"
#include "MVVMBlueprintViewModelContext.h"
#include "MVVMBlueprintViewConversionFunction.h"
#include "MVVMBlueprintFunctionReference.h"
#include "MVVMBlueprintPin.h"
#include "MVVMPropertyPath.h"
#include "Types/MVVMBindingMode.h"
#include "Types/MVVMExecutionMode.h"
#include "Types/MVVMFieldVariant.h"
#include "UObject/UnrealType.h"
#endif

#if WITH_UEMCP_MVVM
namespace UEMCPMVVM
{
	using UE::MVVM::FMVVMConstFieldVariant;

	// ── Enum spellings ───────────────────────────────────────────────────────

	template <typename TEnum>
	FString EnumName(TEnum Value)
	{
		return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value));
	}

	/** Parse a short enumerator name, case-insensitively. False when nothing matches. */
	template <typename TEnum>
	bool ParseEnum(const FString& Text, TEnum& Out)
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
			if (Enum->GetNameStringByIndex(Index).Equals(Text, ESearchCase::IgnoreCase))
			{
				Out = static_cast<TEnum>(Enum->GetValueByIndex(Index));
				return true;
			}
		}
		return false;
	}

	template <typename TEnum>
	FString EnumChoices()
	{
		const UEnum* Enum = StaticEnum<TEnum>();
		TArray<FString> Names;
		for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
		{
#if WITH_EDITORONLY_DATA
			if (Enum->HasMetaData(TEXT("Hidden"), Index)) continue;
#endif
			Names.Add(Enum->GetNameStringByIndex(Index));
		}
		return FString::Join(Names, TEXT(", "));
	}

	/** EMVVMExecutionMode, with the editor's display name "Auto" for DelayedWhenSharedElseImmediate. */
	bool ParseExecutionMode(const FString& Text, EMVVMExecutionMode& Out)
	{
		if (Text.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
		{
			Out = EMVVMExecutionMode::DelayedWhenSharedElseImmediate;
			return true;
		}
		return ParseEnum(Text, Out);
	}

	FString ExecutionModeName(EMVVMExecutionMode Mode)
	{
		return Mode == EMVVMExecutionMode::DelayedWhenSharedElseImmediate ? FString(TEXT("Auto")) : EnumName(Mode);
	}

	// ── Context ──────────────────────────────────────────────────────────────

	UMVVMEditorSubsystem* Subsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UMVVMEditorSubsystem>() : nullptr;
	}

	/** The class a Self path resolves against: the skeleton, which carries members added since the last compile. */
	const UClass* SelfClass(const UWidgetBlueprint* WidgetBlueprint)
	{
		if (WidgetBlueprint->SkeletonGeneratedClass) return WidgetBlueprint->SkeletonGeneratedClass;
		return WidgetBlueprint->GeneratedClass;
	}

	/** The SKEL_ class of a Blueprint generated class, when there is one. */
	const UClass* SkeletonOf(const UClass* Class)
	{
		if (const UBlueprint* Blueprint = Class ? Cast<UBlueprint>(Class->ClassGeneratedBy) : nullptr)
		{
			return Blueprint->SkeletonGeneratedClass;
		}
		return nullptr;
	}

	/** Walk Segments from Struct: properties first, then a function, which has to be the last segment. */
	bool FindFieldChain(const UStruct* Struct, const TArray<FString>& Segments, int32 First, TArray<FMVVMConstFieldVariant>& OutChain, FString& OutError)
	{
		OutChain.Reset();
		const UStruct* Current = Struct;
		for (int32 Index = First; Index < Segments.Num(); ++Index)
		{
			const FString& Segment = Segments[Index];
			if (!Current)
			{
				OutError = FString::Printf(TEXT("'%s' is reached through a value that is neither an object nor a struct, so it has no fields."), *Segment);
				return false;
			}
			if (const FProperty* Property = Current->FindPropertyByName(FName(*Segment)))
			{
				OutChain.Emplace(Property);
				if (const FStructProperty* AsStruct = CastField<FStructProperty>(Property))
				{
					Current = AsStruct->Struct;
				}
				else if (const FObjectPropertyBase* AsObject = CastField<FObjectPropertyBase>(Property))
				{
					Current = AsObject->PropertyClass;
				}
				else
				{
					Current = nullptr;
				}
				continue;
			}
			if (const UClass* AsClass = Cast<UClass>(Current))
			{
				if (const UFunction* Function = AsClass->FindFunctionByName(FName(*Segment)))
				{
					if (Index != Segments.Num() - 1)
					{
						OutError = FString::Printf(TEXT("'%s' is a function, so it has to be the last segment of the path."), *Segment);
						return false;
					}
					OutChain.Emplace(Function);
					Current = nullptr;
					continue;
				}
			}
			OutError = FString::Printf(TEXT("'%s' has no property or function named '%s'."), *Current->GetName(), *Segment);
			return false;
		}
		return true;
	}

	/** First segment, the rest of the path, and which of the two kinds of answer it gave. */
	bool ResolvePath(UWidgetBlueprint* WidgetBlueprint, const UMVVMBlueprintView* View, const FString& Text, FMVVMBlueprintPropertyPath& OutPath, FString& OutError)
	{
		TArray<FString> Segments;
		Text.TrimStartAndEnd().ParseIntoArray(Segments, TEXT("."), true);
		if (Segments.Num() < 2)
		{
			OutError = FString::Printf(TEXT("Path '%s' needs a source and at least one field, e.g. PlayerVM.Health, HealthBar.Percent or Self.Title."), *Text);
			return false;
		}
		const FString& Head = Segments[0];

		FMVVMBlueprintPropertyPath Path;
		TArray<const UStruct*> Candidates;
		if (Head.Equals(TEXT("Self"), ESearchCase::IgnoreCase))
		{
			Path.SetSelfContext();
			Candidates.Add(SelfClass(WidgetBlueprint));
		}
		else if (const FMVVMBlueprintViewModelContext* ViewModel = View ? View->FindViewModel(FName(*Head)) : nullptr)
		{
			Path.SetViewModelId(ViewModel->GetViewModelId());
			const UClass* ViewModelClass = ViewModel->GetViewModelClass();
			Candidates.Add(ViewModelClass);
			if (const UClass* Skeleton = SkeletonOf(ViewModelClass))
			{
				Candidates.Add(Skeleton);
			}
		}
		else if (UWidget* Widget = WidgetBlueprint->WidgetTree ? WidgetBlueprint->WidgetTree->FindWidget(FName(*Head)) : nullptr)
		{
			Path.SetWidgetName(Widget->GetFName());
			Candidates.Add(Widget->GetClass());
		}
		else
		{
			TArray<FString> ViewModelNames;
			if (View)
			{
				for (const FMVVMBlueprintViewModelContext& Context : View->GetViewModels())
				{
					ViewModelNames.Add(Context.GetViewModelName().ToString());
				}
			}
			OutError = FString::Printf(
				TEXT("Path '%s' starts with '%s', which is not a viewmodel on this widget blueprint (%s), a widget in its tree, or Self."),
				*Text, *Head, ViewModelNames.Num() ? *FString::Join(ViewModelNames, TEXT(", ")) : TEXT("it has none"));
			return false;
		}

		FString FirstError;
		for (const UStruct* Candidate : Candidates)
		{
			if (!Candidate) continue;
			TArray<FMVVMConstFieldVariant> Chain;
			FString Error;
			if (FindFieldChain(Candidate, Segments, 1, Chain, Error))
			{
				for (const FMVVMConstFieldVariant& Field : Chain)
				{
					Path.AppendPropertyPath(WidgetBlueprint, Field);
				}
				OutPath = Path;
				return true;
			}
			if (FirstError.IsEmpty()) FirstError = Error;
		}
		OutError = FString::Printf(TEXT("Path '%s': %s"), *Text, FirstError.IsEmpty() ? TEXT("its source has no class to resolve against.") : *FirstError);
		return false;
	}

	FString PathToString(const UWidgetBlueprint* WidgetBlueprint, const UMVVMBlueprintView* View, const FMVVMBlueprintPropertyPath& Path)
	{
		if (!Path.IsValid()) return FString();
		FString Head;
		switch (Path.GetSource(WidgetBlueprint))
		{
		case EMVVMBlueprintFieldPathSource::Widget:
			Head = Path.GetWidgetName().ToString();
			break;
		case EMVVMBlueprintFieldPathSource::ViewModel:
		{
			const FMVVMBlueprintViewModelContext* Context = View ? View->FindViewModel(Path.GetViewModelId()) : nullptr;
			Head = Context ? Context->GetViewModelName().ToString() : Path.GetViewModelId().ToString();
			break;
		}
		case EMVVMBlueprintFieldPathSource::SelfContext:
			Head = TEXT("Self");
			break;
		default:
			break;
		}
		TArray<FString> Parts;
		if (!Head.IsEmpty()) Parts.Add(Head);
		for (const FName& Name : Path.GetFieldNames(WidgetBlueprint->GeneratedClass ? WidgetBlueprint->GeneratedClass.Get() : WidgetBlueprint->SkeletonGeneratedClass.Get()))
		{
			Parts.Add(Name.ToString());
		}
		return FString::Join(Parts, TEXT("."));
	}

	/** "<ClassPath>:<Function>" for a function on another class, the bare name for one on this widget blueprint. */
	FString FunctionReferenceToString(const UWidgetBlueprint* WidgetBlueprint, const FMVVMBlueprintFunctionReference& Reference)
	{
		switch (Reference.GetType())
		{
		case EMVVMBlueprintFunctionReferenceType::Function:
			if (const UFunction* Function = Reference.GetFunction(WidgetBlueprint))
			{
				const UClass* Owner = Function->GetOwnerClass();
				const bool bSelf = Owner && (Owner == WidgetBlueprint->GeneratedClass || Owner == WidgetBlueprint->SkeletonGeneratedClass);
				return bSelf ? Function->GetName() : FString::Printf(TEXT("%s:%s"), *Owner->GetPathName(), *Function->GetName());
			}
			return Reference.GetName().ToString();
		case EMVVMBlueprintFunctionReferenceType::Node:
			if (const UClass* NodeClass = Reference.GetNode().Get())
			{
				return FString::Printf(TEXT("node:%s"), *NodeClass->GetPathName());
			}
			return FString();
		default:
			return FString();
		}
	}

	/** A conversion function named as "<ClassPath>:<Function>" or by a bare name on the widget blueprint. */
	const UFunction* ResolveFunction(const UWidgetBlueprint* WidgetBlueprint, const FString& Spec, FString& OutError)
	{
		FString ClassPath;
		FString FunctionName;
		if (Spec.Split(TEXT(":"), &ClassPath, &FunctionName, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			const UClass* Owner = FindObject<UClass>(nullptr, *ClassPath);
			if (!Owner) Owner = LoadObject<UClass>(nullptr, *ClassPath);
			if (!Owner)
			{
				OutError = FString::Printf(TEXT("Conversion function '%s': no class at '%s'."), *Spec, *ClassPath);
				return nullptr;
			}
			if (const UFunction* Function = Owner->FindFunctionByName(FName(*FunctionName)))
			{
				return Function;
			}
			OutError = FString::Printf(TEXT("Conversion function '%s': %s has no function named '%s'."), *Spec, *Owner->GetName(), *FunctionName);
			return nullptr;
		}
		for (const UClass* Class : { SelfClass(WidgetBlueprint), static_cast<const UClass*>(WidgetBlueprint->GeneratedClass.Get()) })
		{
			if (const UFunction* Function = Class ? Class->FindFunctionByName(FName(*Spec)) : nullptr)
			{
				return Function;
			}
		}
		OutError = FString::Printf(TEXT("Conversion function '%s' is not a function on this widget blueprint. Name a library function as Class.Function or /Script/Module.Class:Function."), *Spec);
		return nullptr;
	}

	// ── Description ──────────────────────────────────────────────────────────

	TSharedPtr<FJsonObject> DescribeViewModel(const UMVVMBlueprintView* View, const FMVVMBlueprintViewModelContext& Context)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("viewModelName"), Context.GetViewModelName().ToString());
		Out->SetStringField(TEXT("viewModelId"), Context.GetViewModelId().ToString(EGuidFormats::DigitsWithHyphens));
		Out->SetStringField(TEXT("viewModelClass"), Context.GetViewModelClass() ? Context.GetViewModelClass()->GetPathName() : FString());
		Out->SetStringField(TEXT("creationType"), EnumName(Context.CreationType));
		Out->SetStringField(TEXT("globalViewModelIdentifier"), Context.GlobalViewModelIdentifier.IsNone() ? FString() : Context.GlobalViewModelIdentifier.ToString());
		Out->SetStringField(TEXT("viewModelPropertyPath"), Context.ViewModelPropertyPath);
		Out->SetBoolField(TEXT("optional"), Context.bOptional);
		Out->SetBoolField(TEXT("createSetter"), Context.bCreateSetterFunction);
		Out->SetBoolField(TEXT("createGetter"), Context.bCreateGetterFunction);
		Out->SetBoolField(TEXT("canRemove"), Context.bCanRemove);
		int32 BindingCount = 0;
		if (View)
		{
			for (const FMVVMBlueprintViewBinding& Binding : View->GetBindings())
			{
				if (Binding.SourcePath.GetViewModelId() == Context.GetViewModelId() || Binding.DestinationPath.GetViewModelId() == Context.GetViewModelId())
				{
					++BindingCount;
				}
			}
		}
		Out->SetNumberField(TEXT("bindingCount"), BindingCount);
		return Out;
	}

	struct FConversionSide
	{
		UMVVMBlueprintViewConversionFunction* Function = nullptr;
		bool bSourceToDestination = true;
	};

	/** The conversion a binding has, source-to-destination first. */
	FConversionSide ConversionOf(const FMVVMBlueprintViewBinding& Binding)
	{
		FConversionSide Side;
		if (UMVVMBlueprintViewConversionFunction* Forward = Binding.Conversion.GetConversionFunction(true))
		{
			Side.Function = Forward;
			Side.bSourceToDestination = true;
		}
		else if (UMVVMBlueprintViewConversionFunction* Backward = Binding.Conversion.GetConversionFunction(false))
		{
			Side.Function = Backward;
			Side.bSourceToDestination = false;
		}
		return Side;
	}

	FString PinName(const FMVVMBlueprintPinId& Id)
	{
		TArray<FString> Names;
		for (const FName& Name : Id.GetNames())
		{
			Names.Add(Name.ToString());
		}
		return FString::Join(Names, TEXT("."));
	}

	/** {pin: {path} | {literal}} for every saved pin of a conversion function. */
	TSharedPtr<FJsonObject> DescribeArguments(const UWidgetBlueprint* WidgetBlueprint, const UMVVMBlueprintView* View, const UMVVMBlueprintViewConversionFunction* Function)
	{
		TSharedPtr<FJsonObject> Arguments = MakeShared<FJsonObject>();
		if (!Function) return Arguments;
		for (const FMVVMBlueprintPin& Pin : Function->GetPins())
		{
			if (!Pin.IsValid() || Pin.IsSplit()) continue;
			TSharedPtr<FJsonObject> Value = MakeShared<FJsonObject>();
			if (Pin.UsedPathAsValue())
			{
				Value->SetStringField(TEXT("path"), PathToString(WidgetBlueprint, View, Pin.GetPath()));
			}
			else
			{
				Value->SetStringField(TEXT("literal"), Pin.GetValueAsString(SelfClass(WidgetBlueprint)));
			}
			if (Pin.GetStatus() == EMVVMBlueprintPinStatus::Orphaned)
			{
				Value->SetBoolField(TEXT("orphaned"), true);
			}
			Arguments->SetObjectField(PinName(Pin.GetId()), Value);
		}
		return Arguments;
	}

	TArray<TSharedPtr<FJsonValue>> Messages(const UMVVMBlueprintView* View, const FGuid& Id, UE::MVVM::EBindingMessageType Type)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		if (!View) return Out;
		for (const FText& Message : View->GetBindingMessages(Id, Type))
		{
			Out.Add(MakeShared<FJsonValueString>(Message.ToString()));
		}
		return Out;
	}

	/** The parameters add_widget_viewmodel_binding takes to recreate Binding. */
	TSharedPtr<FJsonObject> BindingToAddParams(const UWidgetBlueprint* WidgetBlueprint, const UMVVMBlueprintView* View, const FMVVMBlueprintViewBinding& Binding)
	{
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		const FString Source = PathToString(WidgetBlueprint, View, Binding.SourcePath);
		const FString Destination = PathToString(WidgetBlueprint, View, Binding.DestinationPath);
		if (!Source.IsEmpty()) Out->SetStringField(TEXT("sourcePath"), Source);
		if (!Destination.IsEmpty()) Out->SetStringField(TEXT("destinationPath"), Destination);
		Out->SetStringField(TEXT("bindingMode"), EnumName(Binding.BindingType));
		Out->SetStringField(TEXT("executionMode"), Binding.bOverrideExecutionMode ? ExecutionModeName(Binding.OverrideExecutionMode) : FString(TEXT("Default")));
		Out->SetBoolField(TEXT("enabled"), Binding.bEnabled);
		Out->SetBoolField(TEXT("includeInCompile"), Binding.bCompile);
		const FConversionSide Side = ConversionOf(Binding);
		if (Side.Function)
		{
			Out->SetStringField(TEXT("conversionFunction"), FunctionReferenceToString(WidgetBlueprint, Side.Function->GetConversionFunction()));
			Out->SetStringField(TEXT("conversionDirection"), Side.bSourceToDestination ? TEXT("sourceToDestination") : TEXT("destinationToSource"));
			Out->SetObjectField(TEXT("conversionArguments"), DescribeArguments(WidgetBlueprint, View, Side.Function));
		}
		return Out;
	}

	TSharedPtr<FJsonObject> DescribeBinding(const UWidgetBlueprint* WidgetBlueprint, const UMVVMBlueprintView* View, const FMVVMBlueprintViewBinding& Binding)
	{
		TSharedPtr<FJsonObject> Out = BindingToAddParams(WidgetBlueprint, View, Binding);
		Out->SetStringField(TEXT("bindingId"), Binding.BindingId.ToString(EGuidFormats::DigitsWithHyphens));
		Out->SetStringField(TEXT("displayName"), Binding.GetDisplayNameString(WidgetBlueprint));
		if (!Out->HasField(TEXT("sourcePath"))) Out->SetStringField(TEXT("sourcePath"), FString());
		if (!Out->HasField(TEXT("destinationPath"))) Out->SetStringField(TEXT("destinationPath"), FString());
		Out->SetArrayField(TEXT("errors"), Messages(View, Binding.BindingId, UE::MVVM::EBindingMessageType::Error));
		Out->SetArrayField(TEXT("warnings"), Messages(View, Binding.BindingId, UE::MVVM::EBindingMessageType::Warning));
		return Out;
	}

	bool SameJson(const TSharedPtr<FJsonObject>& A, const TSharedPtr<FJsonObject>& B)
	{
		if (!A.IsValid() || !B.IsValid()) return A.IsValid() == B.IsValid();
		const TSharedRef<FJsonValueObject> AV = MakeShared<FJsonValueObject>(A);
		const TSharedRef<FJsonValueObject> BV = MakeShared<FJsonValueObject>(B);
		return FJsonValue::CompareEqual(*AV, *BV);
	}

	// ── Binding edits ────────────────────────────────────────────────────────

	struct FBindingRequest
	{
		bool bHasSource = false;
		FMVVMBlueprintPropertyPath Source;
		FString SourceText;
		bool bHasDestination = false;
		FMVVMBlueprintPropertyPath Destination;
		FString DestinationText;
		bool bHasMode = false;
		EMVVMBindingMode Mode = EMVVMBindingMode::OneWayToDestination;
		bool bHasExecutionMode = false;
		bool bResetExecutionMode = false;
		EMVVMExecutionMode ExecutionMode = EMVVMExecutionMode::Immediate;
		bool bHasConversion = false;
		/** Null with bHasConversion clears the conversion. */
		const UFunction* Conversion = nullptr;
		FString ConversionText;
		bool bSourceToDestination = true;
		bool bHasDirection = false;
		TSharedPtr<FJsonObject> Arguments;
		bool bHasEnabled = false;
		bool bEnabled = true;
		bool bHasCompile = false;
		bool bCompile = true;
	};

	/** Read and resolve every binding parameter, so nothing is written when one of them is wrong. */
	bool ReadBindingRequest(UWidgetBlueprint* WidgetBlueprint, const UMVVMBlueprintView* View, const TSharedPtr<FJsonObject>& Params, FBindingRequest& Out, FString& OutError)
	{
		if (TryGetStringParam(Params, TEXT("sourcePath"), Out.SourceText) && !Out.SourceText.IsEmpty())
		{
			Out.bHasSource = true;
			if (!ResolvePath(WidgetBlueprint, View, Out.SourceText, Out.Source, OutError)) return false;
		}
		if (TryGetStringParam(Params, TEXT("destinationPath"), Out.DestinationText) && !Out.DestinationText.IsEmpty())
		{
			Out.bHasDestination = true;
			if (!ResolvePath(WidgetBlueprint, View, Out.DestinationText, Out.Destination, OutError)) return false;
		}
		FString ModeText;
		if (TryGetStringParam(Params, TEXT("bindingMode"), ModeText) && !ModeText.IsEmpty())
		{
			Out.bHasMode = true;
			if (!ParseEnum(ModeText, Out.Mode))
			{
				OutError = FString::Printf(TEXT("bindingMode '%s' is not one of %s."), *ModeText, *EnumChoices<EMVVMBindingMode>());
				return false;
			}
		}
		FString ExecutionText;
		if (TryGetStringParam(Params, TEXT("executionMode"), ExecutionText) && !ExecutionText.IsEmpty())
		{
			Out.bHasExecutionMode = true;
			if (ExecutionText.Equals(TEXT("Default"), ESearchCase::IgnoreCase))
			{
				Out.bResetExecutionMode = true;
			}
			else if (!ParseExecutionMode(ExecutionText, Out.ExecutionMode))
			{
				OutError = FString::Printf(TEXT("executionMode '%s' is not one of Default, Immediate, Delayed, Tick, Auto."), *ExecutionText);
				return false;
			}
		}
		if (TryGetStringParam(Params, TEXT("conversionFunction"), Out.ConversionText))
		{
			Out.bHasConversion = true;
			if (!Out.ConversionText.IsEmpty())
			{
				Out.Conversion = ResolveFunction(WidgetBlueprint, Out.ConversionText, OutError);
				if (!Out.Conversion) return false;
				if (!UMVVMBlueprintViewConversionFunction::IsValidConversionFunction(WidgetBlueprint, Out.Conversion))
				{
					OutError = FString::Printf(TEXT("'%s' cannot be a viewmodel conversion function: it has to be a pure function that returns a value, and the project's viewmodel settings have to allow it."), *Out.ConversionText);
					return false;
				}
			}
		}
		FString Direction;
		if (TryGetStringParam(Params, TEXT("conversionDirection"), Direction) && !Direction.IsEmpty())
		{
			Out.bHasDirection = true;
			if (Direction.Equals(TEXT("sourceToDestination"), ESearchCase::IgnoreCase)) Out.bSourceToDestination = true;
			else if (Direction.Equals(TEXT("destinationToSource"), ESearchCase::IgnoreCase)) Out.bSourceToDestination = false;
			else
			{
				OutError = FString::Printf(TEXT("conversionDirection '%s' is not sourceToDestination or destinationToSource."), *Direction);
				return false;
			}
		}
		const TSharedPtr<FJsonObject>* Arguments = nullptr;
		if (TryGetObjectParam(Params, TEXT("conversionArguments"), Arguments) && Arguments && Arguments->IsValid())
		{
			Out.Arguments = *Arguments;
		}
		Out.bHasEnabled = TryGetBoolParam(Params, TEXT("enabled"), Out.bEnabled);
		Out.bHasCompile = TryGetBoolParam(Params, TEXT("includeInCompile"), Out.bCompile);
		return true;
	}

	/** Apply the conversion arguments. A pin value is {path} or a literal (a string, number or boolean, or {literal}). */
	bool ApplyArguments(UMVVMEditorSubsystem* Editor, UWidgetBlueprint* WidgetBlueprint, UMVVMBlueprintView* View, FMVVMBlueprintViewBinding& Binding,
		bool bSourceToDestination, const TSharedPtr<FJsonObject>& Arguments, FString& OutError)
	{
		UMVVMBlueprintViewConversionFunction* Function = Binding.Conversion.GetConversionFunction(bSourceToDestination);
		if (!Function)
		{
			OutError = TEXT("conversionArguments were given, but the binding has no conversion function in that direction.");
			return false;
		}
		const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
		for (const auto& Entry : Arguments->Values)
		{
			const TPair<FString, TSharedPtr<FJsonValue>> Pair(Entry.Key, Entry.Value);
			TArray<FString> NameParts;
			Pair.Key.ParseIntoArray(NameParts, TEXT("."), true);
			TArray<FName> Names;
			for (const FString& Part : NameParts) Names.Add(FName(*Part));
			const FMVVMBlueprintPinId PinId(MoveTemp(Names));

			FString PathText;
			FString Literal;
			bool bIsPath = false;
			const TSharedPtr<FJsonValue>& Value = Pair.Value;
			if (Value.IsValid() && Value->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				if (Object->TryGetStringField(TEXT("path"), PathText)) bIsPath = true;
				else if (!Object->TryGetStringField(TEXT("literal"), Literal))
				{
					OutError = FString::Printf(TEXT("conversionArguments.%s is an object without 'path' or 'literal'."), *Pair.Key);
					return false;
				}
			}
			else if (Value.IsValid() && Value->Type == EJson::Boolean)
			{
				Literal = Value->AsBool() ? TEXT("true") : TEXT("false");
			}
			else if (!Value.IsValid() || !Value->TryGetString(Literal))
			{
				OutError = FString::Printf(TEXT("conversionArguments.%s has to be a string, number, boolean, {path} or {literal}."), *Pair.Key);
				return false;
			}

			UEdGraphPin* GraphPin = Editor->GetConversionFunctionArgumentPin(WidgetBlueprint, Binding, PinId, bSourceToDestination);
			if (!GraphPin || GraphPin->bOrphanedPin || !FMVVMBlueprintPin::IsInputPin(GraphPin))
			{
				TArray<FString> Known;
				for (const FMVVMBlueprintPin& Pin : Function->GetPins()) Known.Add(PinName(Pin.GetId()));
				OutError = FString::Printf(TEXT("The conversion function has no input '%s'. Its inputs are: %s."), *Pair.Key, *FString::Join(Known, TEXT(", ")));
				return false;
			}

			if (bIsPath)
			{
				FMVVMBlueprintPropertyPath Path;
				if (!ResolvePath(WidgetBlueprint, View, PathText, Path, OutError)) return false;
				Editor->SetPathForConversionFunctionArgument(WidgetBlueprint, Binding, PinId, Path, bSourceToDestination);
				continue;
			}

			const FName Category = GraphPin->PinType.PinCategory;
			if (GraphPin->PinType.IsContainer() || Category == UEdGraphSchema_K2::PC_Object || Category == UEdGraphSchema_K2::PC_Struct
				|| Category == UEdGraphSchema_K2::PC_Class || Category == UEdGraphSchema_K2::PC_Interface)
			{
				OutError = FString::Printf(TEXT("Input '%s' is a %s; only bool, number, name, string, text and enum inputs take a literal. Bind it to a path instead."),
					*Pair.Key, *UEdGraphSchema_K2::TypeToText(GraphPin->PinType).ToString());
				return false;
			}
			GraphPin->Modify();
			if (Category == UEdGraphSchema_K2::PC_Text)
			{
				Schema->TrySetDefaultText(*GraphPin, FText::FromString(Literal), false);
			}
			else
			{
				const FString Problem = Schema->IsPinDefaultValid(GraphPin, Literal, nullptr, FText::GetEmpty());
				if (!Problem.IsEmpty())
				{
					OutError = FString::Printf(TEXT("'%s' is not a valid value for input '%s': %s"), *Literal, *Pair.Key, *Problem);
					return false;
				}
				Schema->TrySetDefaultValue(*GraphPin, Literal, false);
			}
			Function->SavePinValues(WidgetBlueprint);
		}
		FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);
		return true;
	}

	/** The first input of the conversion that no argument binds, to receive the binding's other end. */
	FString FirstFreeInput(const UMVVMBlueprintViewConversionFunction* Function, const TSharedPtr<FJsonObject>& Arguments)
	{
		if (!Function) return FString();
		for (const FMVVMBlueprintPin& Pin : Function->GetPins())
		{
			if (!Pin.IsValid() || Pin.GetId().GetNames().Num() != 1) continue;
			const FString Name = PinName(Pin.GetId());
			if (Arguments.IsValid() && Arguments->HasField(Name)) continue;
			return Name;
		}
		return FString();
	}

	bool ArgumentsBindAPath(const TSharedPtr<FJsonObject>& Arguments)
	{
		if (!Arguments.IsValid()) return false;
		for (const auto& Entry : Arguments->Values)
		{
			const TPair<FString, TSharedPtr<FJsonValue>> Pair(Entry.Key, Entry.Value);
			if (Pair.Value.IsValid() && Pair.Value->Type == EJson::Object && Pair.Value->AsObject()->HasField(TEXT("path"))) return true;
		}
		return false;
	}

	/**
	 * Apply a request to a binding in the editor's own order: the mode, then the
	 * conversion, then the paths, then the conversion's arguments. A conversion
	 * replaces one end of the binding (the source when it runs source to
	 * destination), so with one in place that end of the request is bound to the
	 * conversion's first free input instead.
	 */
	bool ApplyBindingRequest(UMVVMEditorSubsystem* Editor, UWidgetBlueprint* WidgetBlueprint, UMVVMBlueprintView* View, const FGuid& BindingId,
		const FBindingRequest& Request, FString& OutError)
	{
		auto Binding = [View, &BindingId]() { return View->GetBinding(BindingId); };
		if (!Binding())
		{
			OutError = TEXT("The binding disappeared while it was being edited.");
			return false;
		}

		if (Request.bHasMode)
		{
			Editor->SetBindingTypeForBinding(WidgetBlueprint, *Binding(), Request.Mode);
		}

		bool bForward = Request.bSourceToDestination;
		if (!Request.bHasDirection)
		{
			const FConversionSide Existing = ConversionOf(*Binding());
			if (Existing.Function) bForward = Existing.bSourceToDestination;
			else bForward = Binding()->BindingType != EMVVMBindingMode::OneWayToSource && Binding()->BindingType != EMVVMBindingMode::OneTimeToSource;
		}

		if (Request.bHasConversion)
		{
			// The end the conversion does not replace is set first, because the
			// subsystem checks the conversion's types against it.
			if (bForward && Request.bHasDestination)
			{
				Editor->SetDestinationPathForBinding(WidgetBlueprint, *Binding(), Request.Destination, false);
			}
			if (!bForward && Request.bHasSource)
			{
				Editor->SetSourcePathForBinding(WidgetBlueprint, *Binding(), Request.Source);
			}
			const FMVVMBlueprintFunctionReference Reference = Request.Conversion ? FMVVMBlueprintFunctionReference(WidgetBlueprint, Request.Conversion) : FMVVMBlueprintFunctionReference();
			// Clearing one direction clears both: a binding has at most one conversion here.
			if (!Request.Conversion || bForward)
			{
				Editor->SetDestinationToSourceConversionFunction(WidgetBlueprint, *Binding(), FMVVMBlueprintFunctionReference());
			}
			if (!Request.Conversion || !bForward)
			{
				Editor->SetSourceToDestinationConversionFunction(WidgetBlueprint, *Binding(), FMVVMBlueprintFunctionReference());
			}
			if (Request.Conversion)
			{
				if (bForward) Editor->SetSourceToDestinationConversionFunction(WidgetBlueprint, *Binding(), Reference);
				else Editor->SetDestinationToSourceConversionFunction(WidgetBlueprint, *Binding(), Reference);
				if (!Binding()->Conversion.GetConversionFunction(bForward))
				{
					OutError = FString::Printf(TEXT("'%s' does not convert to the type of the binding's %s, so the viewmodel editor refused it."),
						*Request.ConversionText, bForward ? TEXT("destination") : TEXT("source"));
					return false;
				}
			}
		}

		UMVVMBlueprintViewConversionFunction* Conversion = Binding()->Conversion.GetConversionFunction(bForward);
		TSharedPtr<FJsonObject> Arguments = Request.Arguments.IsValid() ? MakeShared<FJsonObject>(*Request.Arguments) : MakeShared<FJsonObject>();

		if (Request.bHasDestination && !(Request.bHasConversion && bForward))
		{
			if (Conversion && !bForward)
			{
				if (!ArgumentsBindAPath(Request.Arguments))
				{
					const FString Input = FirstFreeInput(Conversion, Request.Arguments);
					if (Input.IsEmpty())
					{
						OutError = TEXT("The conversion function has no free input to take destinationPath.");
						return false;
					}
					TSharedPtr<FJsonObject> PathValue = MakeShared<FJsonObject>();
					PathValue->SetStringField(TEXT("path"), Request.DestinationText);
					Arguments->SetObjectField(Input, PathValue);
				}
			}
			else
			{
				Editor->SetDestinationPathForBinding(WidgetBlueprint, *Binding(), Request.Destination, false);
			}
		}
		if (Request.bHasSource && !(Request.bHasConversion && !bForward))
		{
			if (Conversion && bForward)
			{
				if (!ArgumentsBindAPath(Request.Arguments))
				{
					const FString Input = FirstFreeInput(Conversion, Request.Arguments);
					if (Input.IsEmpty())
					{
						OutError = TEXT("The conversion function has no free input to take sourcePath.");
						return false;
					}
					TSharedPtr<FJsonObject> PathValue = MakeShared<FJsonObject>();
					PathValue->SetStringField(TEXT("path"), Request.SourceText);
					Arguments->SetObjectField(Input, PathValue);
				}
			}
			else
			{
				Editor->SetSourcePathForBinding(WidgetBlueprint, *Binding(), Request.Source);
			}
		}

		if (Arguments->Values.Num() > 0)
		{
			if (!ApplyArguments(Editor, WidgetBlueprint, View, *Binding(), bForward, Arguments, OutError)) return false;
		}

		if (Request.bHasExecutionMode)
		{
			if (Request.bResetExecutionMode) Editor->ResetExecutionModeForBinding(WidgetBlueprint, *Binding());
			else Editor->OverrideExecutionModeForBinding(WidgetBlueprint, *Binding(), Request.ExecutionMode);
		}
		if (Request.bHasEnabled) Editor->SetEnabledForBinding(WidgetBlueprint, *Binding(), Request.bEnabled);
		if (Request.bHasCompile) Editor->SetCompileForBinding(WidgetBlueprint, *Binding(), Request.bCompile);
		return true;
	}

	bool ParseBindingId(const TSharedPtr<FJsonObject>& Params, FGuid& OutId, FString& OutError)
	{
		const FString Text = OptionalString(Params, TEXT("bindingId"));
		if (!FGuid::Parse(Text, OutId))
		{
			OutError = FString::Printf(TEXT("bindingId '%s' is not a binding id. widget(list_viewmodel_bindings) reports them."), *Text);
			return false;
		}
		return true;
	}

	// ── Operations ───────────────────────────────────────────────────────────

	class FBackend final : public IMCPMVVMBackend
	{
	public:
		virtual FMCPMVVMOutcome Run(const FString& Operation, UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonObject>& Params) override
		{
			UMVVMEditorSubsystem* Editor = Subsystem();
			if (!Editor || !WidgetBlueprint)
			{
				return FMCPMVVMOutcome::Failure(TEXT("The viewmodel editor subsystem is not available in this editor session."));
			}
			if (Operation == TEXT("list_widget_viewmodels")) return ListViewModels(Editor, WidgetBlueprint);
			if (Operation == TEXT("add_widget_viewmodel")) return AddViewModel(Editor, WidgetBlueprint, Params);
			if (Operation == TEXT("remove_widget_viewmodel")) return RemoveViewModel(Editor, WidgetBlueprint, Params);
			if (Operation == TEXT("list_widget_viewmodel_bindings")) return ListBindings(Editor, WidgetBlueprint);
			if (Operation == TEXT("add_widget_viewmodel_binding")) return AddBinding(Editor, WidgetBlueprint, Params);
			if (Operation == TEXT("update_widget_viewmodel_binding")) return UpdateBinding(Editor, WidgetBlueprint, Params);
			if (Operation == TEXT("remove_widget_viewmodel_binding")) return RemoveBinding(Editor, WidgetBlueprint, Params);
			return FMCPMVVMOutcome::Failure(FString::Printf(TEXT("The viewmodel backend has no operation '%s'."), *Operation));
		}

	private:
		static FMCPMVVMOutcome ListViewModels(UMVVMEditorSubsystem* Editor, UWidgetBlueprint* WidgetBlueprint)
		{
			const UMVVMBlueprintView* View = Editor->GetView(WidgetBlueprint);
			TArray<TSharedPtr<FJsonValue>> Rows;
			if (View)
			{
				for (const FMVVMBlueprintViewModelContext& Context : View->GetViewModels())
				{
					Rows.Add(MakeShared<FJsonValueObject>(DescribeViewModel(View, Context)));
				}
			}
			FMCPMVVMOutcome Out;
			Out.Data = MakeShared<FJsonObject>();
			Out.Data->SetBoolField(TEXT("hasView"), View != nullptr);
			Out.Data->SetArrayField(TEXT("viewModels"), Rows);
			Out.Data->SetNumberField(TEXT("count"), Rows.Num());
			return Out;
		}

		static FMCPMVVMOutcome AddViewModel(UMVVMEditorSubsystem* Editor, UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonObject>& Params)
		{
			const FString ClassPath = OptionalString(Params, TEXT("viewModelClass"));
			UClass* Class = FindObject<UClass>(nullptr, *ClassPath);
			if (!Class) Class = LoadObject<UClass>(nullptr, *ClassPath);
			if (!Class)
			{
				return FMCPMVVMOutcome::Failure(FString::Printf(TEXT("No class at '%s'."), *ClassPath));
			}
			if (!Class->ImplementsInterface(UNotifyFieldValueChanged::StaticClass()))
			{
				return FMCPMVVMOutcome::Failure(FString::Printf(
					TEXT("%s cannot be a viewmodel: it does not implement INotifyFieldValueChanged. Derive the class from MVVMViewModelBase."), *Class->GetName()));
			}

			FString Name = OptionalString(Params, TEXT("viewModelName"));
			if (Name.IsEmpty()) Name = UMVVMEditorSubsystem::GetDefaultViewModelName(Class);

			UMVVMBlueprintView* View = Editor->RequestView(WidgetBlueprint);
			if (!View)
			{
				return FMCPMVVMOutcome::Failure(TEXT("The widget blueprint has no viewmodel view and one could not be created."));
			}
			if (const FMVVMBlueprintViewModelContext* Existing = View->FindViewModel(FName(*Name)))
			{
				if (Existing->GetViewModelClass() != Class)
				{
					return FMCPMVVMOutcome::Failure(FString::Printf(
						TEXT("A viewmodel named '%s' already exists with class %s. Remove it first or pick another viewModelName."),
						*Name, Existing->GetViewModelClass() ? *Existing->GetViewModelClass()->GetPathName() : TEXT("(none)")));
				}
				FMCPMVVMOutcome Out;
				Out.Change = FMCPMVVMOutcome::EChange::Existed;
				Out.Data = DescribeViewModel(View, *Existing);
				return Out;
			}

			FKismetNameValidator Validator(WidgetBlueprint);
			const EValidatorResult Valid = Validator.IsValid(FName(*Name));
			if (Valid != EValidatorResult::Ok)
			{
				return FMCPMVVMOutcome::Failure(FString::Printf(TEXT("viewModelName: %s"), *INameValidatorInterface::GetErrorText(Name, Valid).ToString()));
			}

			FMVVMBlueprintViewModelContext Context(Class, FName(*Name));
			if (!Context.IsValid())
			{
				return FMCPMVVMOutcome::Failure(FString::Printf(TEXT("%s allows no viewmodel creation type, so it cannot be added."), *Class->GetName()));
			}

			const FString CreationText = OptionalString(Params, TEXT("creationType"));
			if (!CreationText.IsEmpty())
			{
				EMVVMBlueprintViewModelContextCreationType CreationType;
				if (!ParseEnum(CreationText, CreationType))
				{
					return FMCPMVVMOutcome::Failure(FString::Printf(TEXT("creationType '%s' is not one of %s on this engine."),
						*CreationText, *EnumChoices<EMVVMBlueprintViewModelContextCreationType>()));
				}
				const TArray<EMVVMBlueprintViewModelContextCreationType> Allowed = UE::MVVM::GetAllowedContextCreationType(Class);
				if (!Allowed.Contains(CreationType))
				{
					TArray<FString> AllowedNames;
					for (const EMVVMBlueprintViewModelContextCreationType Type : Allowed) AllowedNames.Add(EnumName(Type));
					return FMCPMVVMOutcome::Failure(FString::Printf(TEXT("%s does not allow creationType %s; it allows %s."),
						*Class->GetName(), *EnumName(CreationType), *FString::Join(AllowedNames, TEXT(", "))));
				}
				Context.CreationType = CreationType;
			}

			const FString GlobalIdentifier = OptionalString(Params, TEXT("globalViewModelIdentifier"));
			const FString PropertyPath = OptionalString(Params, TEXT("viewModelPropertyPath"));
			if (Context.CreationType == EMVVMBlueprintViewModelContextCreationType::GlobalViewModelCollection && GlobalIdentifier.IsEmpty())
			{
				return FMCPMVVMOutcome::Failure(TEXT("creationType GlobalViewModelCollection needs globalViewModelIdentifier."));
			}
			if (Context.CreationType == EMVVMBlueprintViewModelContextCreationType::PropertyPath && PropertyPath.IsEmpty())
			{
				return FMCPMVVMOutcome::Failure(TEXT("creationType PropertyPath needs viewModelPropertyPath."));
			}
			if (!GlobalIdentifier.IsEmpty()) Context.GlobalViewModelIdentifier = FName(*GlobalIdentifier);
			Context.ViewModelPropertyPath = PropertyPath;

			bool bFlag = false;
			if (TryGetBoolParam(Params, TEXT("optional"), bFlag)) Context.bOptional = bFlag;
			if (TryGetBoolParam(Params, TEXT("createSetter"), bFlag)) Context.bCreateSetterFunction = bFlag;
			if (TryGetBoolParam(Params, TEXT("createGetter"), bFlag)) Context.bCreateGetterFunction = bFlag;

			// The id is private to the context and has no setter. The rollback of
			// remove_widget_viewmodel passes the id the removed viewmodel had, so
			// the bindings that still name it resolve again.
			const FString IdText = OptionalString(Params, TEXT("viewModelId"));
			if (!IdText.IsEmpty())
			{
				FGuid Id;
				if (!FGuid::Parse(IdText, Id))
				{
					return FMCPMVVMOutcome::Failure(FString::Printf(TEXT("viewModelId '%s' is not a GUID."), *IdText));
				}
				if (View->FindViewModel(Id))
				{
					return FMCPMVVMOutcome::Failure(FString::Printf(TEXT("viewModelId %s is already used by another viewmodel on this widget blueprint."), *IdText));
				}
				if (FStructProperty* IdProperty = FindFProperty<FStructProperty>(FMVVMBlueprintViewModelContext::StaticStruct(), TEXT("ViewModelContextId")))
				{
					*IdProperty->ContainerPtrToValuePtr<FGuid>(&Context) = Id;
				}
			}

			if (Context.CreationType == EMVVMBlueprintViewModelContextCreationType::Resolver)
			{
#if UE_MCP_HAS_5_8_API
				Context.Resolver = Context.CreateDefaultResolver(WidgetBlueprint);
#else
				Context.Resolver = Context.CreateDefaultResolver(WidgetBlueprint->GetPackage());
#endif
			}

			View->Modify();
			View->AddViewModel(Context);
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

			FMCPMVVMOutcome Out;
			Out.Change = FMCPMVVMOutcome::EChange::Created;
			Out.bModified = true;
			const FMVVMBlueprintViewModelContext* Added = View->FindViewModel(Context.GetViewModelId());
			Out.Data = DescribeViewModel(View, Added ? *Added : Context);
			Out.RollbackMethod = TEXT("remove_widget_viewmodel");
			Out.RollbackPayload = MakeShared<FJsonObject>();
			Out.RollbackPayload->SetStringField(TEXT("viewModelName"), Name);
			return Out;
		}

		static FMCPMVVMOutcome RemoveViewModel(UMVVMEditorSubsystem* Editor, UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonObject>& Params)
		{
			const FString Name = OptionalString(Params, TEXT("viewModelName"));
			UMVVMBlueprintView* View = Editor->GetView(WidgetBlueprint);
			const FMVVMBlueprintViewModelContext* Context = View ? View->FindViewModel(FName(*Name)) : nullptr;
			if (!Context)
			{
				FMCPMVVMOutcome Out;
				Out.Change = FMCPMVVMOutcome::EChange::AlreadyDeleted;
				Out.Data = MakeShared<FJsonObject>();
				Out.Data->SetStringField(TEXT("viewModelName"), Name);
				return Out;
			}
			if (!Context->bCanRemove)
			{
				return FMCPMVVMOutcome::Failure(FString::Printf(TEXT("Viewmodel '%s' is marked as not removable."), *Name));
			}

			const FMVVMBlueprintViewModelContext Removed = *Context;
			TArray<TSharedPtr<FJsonValue>> Dependent;
			for (const FMVVMBlueprintViewBinding& Binding : View->GetBindings())
			{
				if (Binding.SourcePath.GetViewModelId() == Removed.GetViewModelId() || Binding.DestinationPath.GetViewModelId() == Removed.GetViewModelId())
				{
					Dependent.Add(MakeShared<FJsonValueString>(Binding.BindingId.ToString(EGuidFormats::DigitsWithHyphens)));
				}
			}

			TSharedPtr<FJsonObject> Restore = MakeShared<FJsonObject>();
			Restore->SetStringField(TEXT("viewModelClass"), Removed.GetViewModelClass() ? Removed.GetViewModelClass()->GetPathName() : FString());
			Restore->SetStringField(TEXT("viewModelName"), Removed.GetViewModelName().ToString());
			Restore->SetStringField(TEXT("creationType"), EnumName(Removed.CreationType));
			if (!Removed.GlobalViewModelIdentifier.IsNone()) Restore->SetStringField(TEXT("globalViewModelIdentifier"), Removed.GlobalViewModelIdentifier.ToString());
			if (!Removed.ViewModelPropertyPath.IsEmpty()) Restore->SetStringField(TEXT("viewModelPropertyPath"), Removed.ViewModelPropertyPath);
			Restore->SetBoolField(TEXT("optional"), Removed.bOptional);
			Restore->SetBoolField(TEXT("createSetter"), Removed.bCreateSetterFunction);
			Restore->SetBoolField(TEXT("createGetter"), Removed.bCreateGetterFunction);
			Restore->SetStringField(TEXT("viewModelId"), Removed.GetViewModelId().ToString(EGuidFormats::DigitsWithHyphens));

			View->Modify();
			View->RemoveViewModel(Removed.GetViewModelId());
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

			FMCPMVVMOutcome Out;
			Out.Change = FMCPMVVMOutcome::EChange::Deleted;
			Out.bModified = true;
			Out.Data = MakeShared<FJsonObject>();
			Out.Data->SetStringField(TEXT("viewModelName"), Name);
			Out.Data->SetArrayField(TEXT("bindingsLeftDangling"), Dependent);
			Out.RollbackMethod = TEXT("add_widget_viewmodel");
			Out.RollbackPayload = Restore;
			return Out;
		}

		static FMCPMVVMOutcome ListBindings(UMVVMEditorSubsystem* Editor, UWidgetBlueprint* WidgetBlueprint)
		{
			const UMVVMBlueprintView* View = Editor->GetView(WidgetBlueprint);
			TArray<TSharedPtr<FJsonValue>> Rows;
			if (View)
			{
				for (const FMVVMBlueprintViewBinding& Binding : View->GetBindings())
				{
					Rows.Add(MakeShared<FJsonValueObject>(DescribeBinding(WidgetBlueprint, View, Binding)));
				}
			}
			FMCPMVVMOutcome Out;
			Out.Data = MakeShared<FJsonObject>();
			Out.Data->SetBoolField(TEXT("hasView"), View != nullptr);
			Out.Data->SetArrayField(TEXT("bindings"), Rows);
			Out.Data->SetNumberField(TEXT("count"), Rows.Num());
			return Out;
		}

		static FMCPMVVMOutcome AddBinding(UMVVMEditorSubsystem* Editor, UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonObject>& Params)
		{
			UMVVMBlueprintView* View = Editor->GetView(WidgetBlueprint);
			FBindingRequest Request;
			FString Error;
			if (!ReadBindingRequest(WidgetBlueprint, View, Params, Request, Error))
			{
				return FMCPMVVMOutcome::Failure(Error);
			}
			if (!Request.bHasDestination && !(Request.bHasConversion && Request.Conversion && Request.bHasDirection && !Request.bSourceToDestination))
			{
				return FMCPMVVMOutcome::Failure(TEXT("destinationPath is required, e.g. HealthBar.Percent."));
			}
			if (!Request.bHasSource && !(Request.Conversion && ArgumentsBindAPath(Request.Arguments)))
			{
				return FMCPMVVMOutcome::Failure(TEXT("sourcePath is required, unless conversionFunction is given and conversionArguments bind an input to a {path}."));
			}
			if (!Request.bHasMode)
			{
				Request.bHasMode = true;
				Request.Mode = EMVVMBindingMode::OneWayToDestination;
			}
			if (!Request.bHasDirection)
			{
				Request.bSourceToDestination = Request.Mode != EMVVMBindingMode::OneWayToSource && Request.Mode != EMVVMBindingMode::OneTimeToSource;
				Request.bHasDirection = Request.Conversion != nullptr;
			}

			// The same binding asked for twice is reported, not added twice: same
			// destination, mode, source (or conversion function) and direction.
			if (View)
			{
				for (const FMVVMBlueprintViewBinding& Existing : View->GetBindings())
				{
					if (Existing.BindingType != Request.Mode) continue;
					const FConversionSide Side = ConversionOf(Existing);
					bool bSame = false;
					if (Request.Conversion)
					{
						bSame = Side.Function && Side.bSourceToDestination == Request.bSourceToDestination
							&& Side.Function->GetConversionFunction().GetFunction(WidgetBlueprint) == Request.Conversion
							&& (Request.bSourceToDestination ? Existing.DestinationPath == Request.Destination : Existing.SourcePath == Request.Source);
					}
					else
					{
						bSame = !Side.Function && Existing.SourcePath == Request.Source && Existing.DestinationPath == Request.Destination;
					}
					if (bSame)
					{
						FMCPMVVMOutcome Out;
						Out.Change = FMCPMVVMOutcome::EChange::Existed;
						Out.Data = DescribeBinding(WidgetBlueprint, View, Existing);
						return Out;
					}
				}
			}

			const FGuid BindingId = Editor->AddBinding(WidgetBlueprint).BindingId;
			View = Editor->GetView(WidgetBlueprint);
			if (!View || !View->GetBinding(BindingId))
			{
				return FMCPMVVMOutcome::Failure(TEXT("The viewmodel editor did not add a binding."));
			}
			if (!ApplyBindingRequest(Editor, WidgetBlueprint, View, BindingId, Request, Error))
			{
				// Nothing half-made is left behind.
				if (FMVVMBlueprintViewBinding* Partial = View->GetBinding(BindingId))
				{
					Editor->RemoveBinding(WidgetBlueprint, *Partial);
				}
				return FMCPMVVMOutcome::Failure(Error);
			}

			FMCPMVVMOutcome Out;
			Out.Change = FMCPMVVMOutcome::EChange::Created;
			Out.bModified = true;
			Out.Data = DescribeBinding(WidgetBlueprint, View, *View->GetBinding(BindingId));
			Out.RollbackMethod = TEXT("remove_widget_viewmodel_binding");
			Out.RollbackPayload = MakeShared<FJsonObject>();
			Out.RollbackPayload->SetStringField(TEXT("bindingId"), BindingId.ToString(EGuidFormats::DigitsWithHyphens));
			return Out;
		}

		static FMCPMVVMOutcome UpdateBinding(UMVVMEditorSubsystem* Editor, UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonObject>& Params)
		{
			FGuid BindingId;
			FString Error;
			if (!ParseBindingId(Params, BindingId, Error)) return FMCPMVVMOutcome::Failure(Error);
			UMVVMBlueprintView* View = Editor->GetView(WidgetBlueprint);
			const FMVVMBlueprintViewBinding* Binding = View ? View->GetBinding(BindingId) : nullptr;
			if (!Binding)
			{
				return FMCPMVVMOutcome::Failure(FString::Printf(TEXT("No binding %s on this widget blueprint."), *BindingId.ToString(EGuidFormats::DigitsWithHyphens)));
			}

			FBindingRequest Request;
			if (!ReadBindingRequest(WidgetBlueprint, View, Params, Request, Error))
			{
				return FMCPMVVMOutcome::Failure(Error);
			}

			const TSharedPtr<FJsonObject> Before = BindingToAddParams(WidgetBlueprint, View, *Binding);
			if (!ApplyBindingRequest(Editor, WidgetBlueprint, View, BindingId, Request, Error))
			{
				// What landed before the failure is described, and the rollback puts the binding back.
				FMCPMVVMOutcome Out = FMCPMVVMOutcome::Failure(Error);
				Out.RollbackMethod = TEXT("update_widget_viewmodel_binding");
				Out.RollbackPayload = RestorePayload(BindingId, Before);
				Out.bModified = true;
				return Out;
			}

			const FMVVMBlueprintViewBinding* After = View->GetBinding(BindingId);
			FMCPMVVMOutcome Out;
			Out.Data = After ? DescribeBinding(WidgetBlueprint, View, *After) : MakeShared<FJsonObject>();
			if (After && SameJson(BindingToAddParams(WidgetBlueprint, View, *After), Before))
			{
				Out.Change = FMCPMVVMOutcome::EChange::Existed;
				return Out;
			}
			Out.Change = FMCPMVVMOutcome::EChange::Updated;
			Out.bModified = true;
			Out.Data->SetObjectField(TEXT("previous"), Before);
			Out.RollbackMethod = TEXT("update_widget_viewmodel_binding");
			Out.RollbackPayload = RestorePayload(BindingId, Before);
			return Out;
		}

		/** update_widget_viewmodel_binding parameters that put a binding back as Before described it. */
		static TSharedPtr<FJsonObject> RestorePayload(const FGuid& BindingId, const TSharedPtr<FJsonObject>& Before)
		{
			TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>(*Before);
			Payload->SetStringField(TEXT("bindingId"), BindingId.ToString(EGuidFormats::DigitsWithHyphens));
			if (!Payload->HasField(TEXT("conversionFunction")))
			{
				// No conversion before: clear any the update set.
				Payload->SetStringField(TEXT("conversionFunction"), FString());
			}
			return Payload;
		}

		static FMCPMVVMOutcome RemoveBinding(UMVVMEditorSubsystem* Editor, UWidgetBlueprint* WidgetBlueprint, const TSharedPtr<FJsonObject>& Params)
		{
			FGuid BindingId;
			FString Error;
			if (!ParseBindingId(Params, BindingId, Error)) return FMCPMVVMOutcome::Failure(Error);
			UMVVMBlueprintView* View = Editor->GetView(WidgetBlueprint);
			FMVVMBlueprintViewBinding* Binding = View ? View->GetBinding(BindingId) : nullptr;
			if (!Binding)
			{
				FMCPMVVMOutcome Out;
				Out.Change = FMCPMVVMOutcome::EChange::AlreadyDeleted;
				Out.Data = MakeShared<FJsonObject>();
				Out.Data->SetStringField(TEXT("bindingId"), BindingId.ToString(EGuidFormats::DigitsWithHyphens));
				return Out;
			}
			const TSharedPtr<FJsonObject> Restore = BindingToAddParams(WidgetBlueprint, View, *Binding);
			const TSharedPtr<FJsonObject> Described = DescribeBinding(WidgetBlueprint, View, *Binding);
			Editor->RemoveBinding(WidgetBlueprint, *Binding);
			FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);

			FMCPMVVMOutcome Out;
			Out.Change = FMCPMVVMOutcome::EChange::Deleted;
			Out.bModified = true;
			Out.Data = Described;
			Out.RollbackMethod = TEXT("add_widget_viewmodel_binding");
			Out.RollbackPayload = Restore;
			return Out;
		}
	};
}
#endif // WITH_UEMCP_MVVM

class FUE_MCP_BridgeMVVMModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_UEMCP_MVVM
		Backend = MakeUnique<UEMCPMVVM::FBackend>();
		UEMCP::SetMVVMBackend(Backend.Get());
#endif
	}

	virtual void ShutdownModule() override
	{
		if (Backend.IsValid() && UEMCP::GetMVVMBackend() == Backend.Get())
		{
			UEMCP::SetMVVMBackend(nullptr);
		}
		Backend.Reset();
	}

private:
	TUniquePtr<IMCPMVVMBackend> Backend;
};

IMPLEMENT_MODULE(FUE_MCP_BridgeMVVMModule, UE_MCP_BridgeMVVM)
