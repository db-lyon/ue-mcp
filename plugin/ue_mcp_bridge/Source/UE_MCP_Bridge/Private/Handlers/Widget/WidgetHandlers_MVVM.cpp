// Viewmodel (UMG MVVM) authoring. See WidgetHandlers_MVVM.h and
// Public/HandlerMVVMBackend.h for how the work is split with UE_MCP_BridgeMVVM.

#include "WidgetHandlers_MVVM.h"
#include "WidgetHandlers.h"
#include "WidgetGuidMap.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerMVVMBackend.h"
#include "WidgetBlueprint.h"
#include "Interfaces/IPluginManager.h"
#include "Modules/ModuleManager.h"

namespace UEMCP
{
	static IMCPMVVMBackend*& MCPMVVMBackendSlot()
	{
		static IMCPMVVMBackend* Slot = nullptr;
		return Slot;
	}

	void SetMVVMBackend(IMCPMVVMBackend* Backend)
	{
		MCPMVVMBackendSlot() = Backend;
	}

	IMCPMVVMBackend* GetMVVMBackend()
	{
		return MCPMVVMBackendSlot();
	}
}

/** The backend, loading UE_MCP_BridgeMVVM on first use once the plugin is known to be enabled. */
static IMCPMVVMBackend* MCPMVVMRequireBackend(FString& OutError)
{
	if (IMCPMVVMBackend* Backend = UEMCP::GetMVVMBackend())
	{
		return Backend;
	}
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("ModelViewViewModel"));
	if (!Plugin.IsValid())
	{
		OutError = TEXT("The UMG Viewmodel plugin (ModelViewViewModel) is not installed in this engine, so viewmodels cannot be authored.");
		return nullptr;
	}
	if (!Plugin->IsEnabled())
	{
		OutError = TEXT("The UMG Viewmodel plugin (ModelViewViewModel) is not enabled in this project. Enable it with project(action=\"enable_plugin\", pluginName=\"ModelViewViewModel\") and restart the editor.");
		return nullptr;
	}
	FModuleManager::Get().LoadModule(TEXT("UE_MCP_BridgeMVVM"));
	if (IMCPMVVMBackend* Backend = UEMCP::GetMVVMBackend())
	{
		return Backend;
	}
	OutError = TEXT("The UMG Viewmodel plugin is enabled but the bridge's UE_MCP_BridgeMVVM module did not load. Rebuild the UE_MCP_Bridge plugin; the editor log names the load failure.");
	return nullptr;
}

/**
 * "KismetTextLibrary.Conv_IntToText" and "UKismetTextLibrary::Conv_IntToText"
 * become "/Script/Engine.KismetTextLibrary:Conv_IntToText", the form the
 * backend resolves without the class resolver. A "Class:Function" path and a
 * bare function name (one on the widget blueprint) pass through.
 */
static bool MCPMVVMNormalizeConversionFunction(FString& InOut, FString& OutError)
{
	if (InOut.IsEmpty() || (InOut.Contains(TEXT(":")) && !InOut.Contains(TEXT("::"))))
	{
		return true;
	}
	FString ClassPart;
	FString FunctionPart;
	if (!InOut.Split(TEXT("::"), &ClassPart, &FunctionPart, ESearchCase::CaseSensitive, ESearchDir::FromEnd)
		&& !InOut.Split(TEXT("."), &ClassPart, &FunctionPart, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
	{
		return true;
	}
	UClass* Owner = MCPResolveClass(ClassPart);
	if (!Owner)
	{
		OutError = FString::Printf(TEXT("conversionFunction '%s': no class '%s'."), *InOut, *ClassPart);
		return false;
	}
	InOut = FString::Printf(TEXT("%s:%s"), *Owner->GetPathName(), *FunctionPart);
	return true;
}

/**
 * Resolve the Widget Blueprint, run one backend operation and turn its outcome
 * into the response: the created/existed/updated/deleted answer, the inverse
 * call or the reason there is none, and the save.
 */
enum class EMCPMVVMReads : uint8
{
	Plain,
	/** The operation takes viewModelClass, resolved here. */
	ViewModelClass,
	/** The operation takes conversionFunction, normalized here. */
	ConversionFunction,
};

static TSharedPtr<FJsonValue> MCPMVVMRun(const TCHAR* Operation, const TSharedPtr<FJsonObject>& Params, EMCPMVVMReads Reads = EMCPMVVMReads::Plain)
{
	FString AssetPath;
	if (TSharedPtr<FJsonValue> Missing = RequireString(Params, TEXT("assetPath"), AssetPath))
	{
		return Missing;
	}
	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP)
	{
		return ResolveError;
	}

	// The class resolver lives in this module, so classes are resolved here and
	// handed to the backend as paths.
	TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>(*Params);
	FString ViewModelClass;
	if (Reads == EMCPMVVMReads::ViewModelClass && TryGetStringParam(Params, TEXT("viewModelClass"), ViewModelClass) && !ViewModelClass.IsEmpty())
	{
		UClass* Resolved = MCPResolveClass(ViewModelClass);
		if (!Resolved)
		{
			return MCPClassNotFoundError(ViewModelClass, TEXT("viewModelClass"));
		}
		Args->SetStringField(TEXT("viewModelClass"), Resolved->GetPathName());
	}
	FString Conversion;
	if (Reads == EMCPMVVMReads::ConversionFunction && TryGetStringParam(Params, TEXT("conversionFunction"), Conversion))
	{
		FString NormalizeError;
		if (!MCPMVVMNormalizeConversionFunction(Conversion, NormalizeError))
		{
			return MCPError(NormalizeError);
		}
		Args->SetStringField(TEXT("conversionFunction"), Conversion);
	}

	FString BackendError;
	IMCPMVVMBackend* Backend = MCPMVVMRequireBackend(BackendError);
	if (!Backend)
	{
		return MCPError(BackendError);
	}

	FMCPMVVMOutcome Outcome = Backend->Run(Operation, WidgetBP, Args);

	TSharedPtr<FJsonObject> Result = Outcome.bOk ? MCPSuccess() : MCPErrorObject(Outcome.Error);
	if (Outcome.Data.IsValid())
	{
		for (const auto& Entry : Outcome.Data->Values)
		{
			const TPair<FString, TSharedPtr<FJsonValue>> Pair(Entry.Key, Entry.Value);
			Result->SetField(Pair.Key, Pair.Value);
		}
	}
	Result->SetStringField(TEXT("assetPath"), AssetPath);

	switch (Outcome.Change)
	{
	case FMCPMVVMOutcome::EChange::Created:
		MCPSetCreated(Result);
		break;
	case FMCPMVVMOutcome::EChange::Existed:
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("unchanged"), true);
		break;
	case FMCPMVVMOutcome::EChange::Updated:
		MCPSetUpdated(Result);
		break;
	case FMCPMVVMOutcome::EChange::Deleted:
		Result->SetBoolField(TEXT("deleted"), true);
		break;
	case FMCPMVVMOutcome::EChange::AlreadyDeleted:
		Result->SetBoolField(TEXT("alreadyDeleted"), true);
		break;
	default:
		break;
	}

	if (!Outcome.RollbackMethod.IsEmpty())
	{
		TSharedPtr<FJsonObject> Payload = Outcome.RollbackPayload.IsValid() ? Outcome.RollbackPayload : MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("assetPath"), AssetPath);
		MCPSetRollback(Result, Outcome.RollbackMethod, Payload);
	}
	else if (!Outcome.NoRollbackReason.IsEmpty())
	{
		MCPSetNoRollback(Result, Outcome.NoRollbackReason);
	}

	if (Outcome.bModified)
	{
		// Saved without compiling: a viewmodel edit leaves the blueprint dirty the
		// way the editor does, and compile_widget_viewmodels compiles it and
		// reports each binding's errors.
		FString SaveError;
		const bool bSaved = SaveAssetPackageChecked(WidgetBP, SaveError);
		if (Outcome.bOk)
		{
			MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveError);
		}
		Result->SetStringField(TEXT("compileStatus"), WidgetCompileStatusString(WidgetBP));
	}
	return MCPResult(Result);
}

void FWidgetMVVMHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	using EType = EMCPParamType;
	auto AssetPath = []()
	{
		return MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("Widget Blueprint asset path, e.g. /Game/UI/WBP_Example")).Alias(TEXT("path"));
	};
	// The binding fields add and update share. Paths start with a viewmodel
	// name, a widget name or Self, then walk fields: PlayerVM.Health.
	auto BindingFields = [](TArray<FMCPParamSpec>& Out, bool bForAdd)
	{
		Out.Add(MCPParam::Optional(TEXT("sourcePath"), EType::String, TEXT("Source of the value: a viewmodel name, a widget name or Self, then its fields, e.g. PlayerVM.Health")));
		Out.Add(bForAdd
			? MCPParam::Optional(TEXT("destinationPath"), EType::String, TEXT("Where the value goes, same form, e.g. HealthBar.Percent. Required unless a destinationToSource conversion binds the widget through conversionArguments"))
			: MCPParam::Optional(TEXT("destinationPath"), EType::String, TEXT("Where the value goes, same form, e.g. HealthBar.Percent")));
		Out.Add(MCPParam::Optional(TEXT("bindingMode"), EType::String, bForAdd ? TEXT("Direction and frequency (default OneWayToDestination)") : TEXT("Direction and frequency"))
			.Enum({ TEXT("OneTimeToDestination"), TEXT("OneWayToDestination"), TEXT("TwoWay"), TEXT("OneWayToSource") }));
		Out.Add(MCPParam::Optional(TEXT("executionMode"), EType::String, TEXT("When the binding runs; Default uses the project setting"))
			.Enum({ TEXT("Default"), TEXT("Immediate"), TEXT("Delayed"), TEXT("Tick"), TEXT("Auto") }));
		Out.Add(MCPParam::Optional(TEXT("conversionFunction"), EType::String, bForAdd
			? TEXT("Pure function converting the value: Class.Function (KismetTextLibrary.Conv_IntToText), /Script/Module.Class:Function, or a function on this widget blueprint")
			: TEXT("Pure function converting the value, as for add; an empty string removes the conversion")));
		Out.Add(MCPParam::Optional(TEXT("conversionArguments"), EType::Object, TEXT("Conversion inputs by pin name: a literal (string, number, boolean or {literal}) or {path} bound to a field. The input sourcePath is not given to goes to the first free input")));
		Out.Add(MCPParam::Optional(TEXT("conversionDirection"), EType::String, TEXT("Which way the conversion runs (default sourceToDestination, destinationToSource for OneWayToSource)"))
			.Enum({ TEXT("sourceToDestination"), TEXT("destinationToSource") }));
		Out.Add(MCPParam::Optional(TEXT("enabled"), EType::Boolean, TEXT("Whether the binding runs by default (default true)")));
		Out.Add(MCPParam::Optional(TEXT("includeInCompile"), EType::Boolean, TEXT("False keeps the binding in the editor but out of the compiled class (default true)")));
	};

	Registry.RegisterHandler(TEXT("list_widget_viewmodels"), &ListViewModels, {
		AssetPath(),
	});
	Registry.RegisterHandler(TEXT("add_widget_viewmodel"), &AddViewModel, {
		AssetPath(),
		MCPParam::Required(TEXT("viewModelClass"), EType::String, TEXT("Viewmodel class: a native MVVMViewModelBase subclass name or path, or a Blueprint viewmodel asset path")),
		MCPParam::Optional(TEXT("viewModelName"), EType::String, TEXT("Name of the generated viewmodel property (default the class name)")),
		MCPParam::Optional(TEXT("creationType"), EType::String, TEXT("How the widget gets its instance (default the first type the class allows, usually CreateInstance). Context is UE 5.8+"))
			.Enum({ TEXT("Manual"), TEXT("CreateInstance"), TEXT("GlobalViewModelCollection"), TEXT("PropertyPath"), TEXT("Resolver"), TEXT("Context") }),
		MCPParam::Optional(TEXT("globalViewModelIdentifier"), EType::String, TEXT("Identifier in the global viewmodel collection, for creationType GlobalViewModelCollection")),
		MCPParam::Optional(TEXT("viewModelPropertyPath"), EType::String, TEXT("Property or function path that returns the instance, for creationType PropertyPath")),
		MCPParam::Optional(TEXT("optional"), EType::Boolean, TEXT("Do not warn when the instance is not set or found")),
		MCPParam::Optional(TEXT("createSetter"), EType::Boolean, TEXT("Generate a public setter for the viewmodel")),
		MCPParam::Optional(TEXT("createGetter"), EType::Boolean, TEXT("Generate a public getter for the viewmodel (default true)")),
		MCPParam::Optional(TEXT("viewModelId"), EType::String, TEXT("Reuse this viewmodel id; the rollback of remove_widget_viewmodel passes the removed one so its bindings resolve again")),
	});
	Registry.RegisterHandler(TEXT("remove_widget_viewmodel"), &RemoveViewModel, {
		AssetPath(),
		MCPParam::Required(TEXT("viewModelName"), EType::String, TEXT("Name of the viewmodel to remove")),
	});
	Registry.RegisterHandler(TEXT("list_widget_viewmodel_bindings"), &ListViewModelBindings, {
		AssetPath(),
	});

	TArray<FMCPParamSpec> AddSpec = { AssetPath() };
	BindingFields(AddSpec, true);
	Registry.RegisterHandler(TEXT("add_widget_viewmodel_binding"), &AddViewModelBinding, AddSpec);

	TArray<FMCPParamSpec> UpdateSpec = {
		AssetPath(),
		MCPParam::Required(TEXT("bindingId"), EType::String, TEXT("Binding id, as list_widget_viewmodel_bindings reports it")),
	};
	BindingFields(UpdateSpec, false);
	Registry.RegisterHandler(TEXT("update_widget_viewmodel_binding"), &UpdateViewModelBinding, UpdateSpec);

	Registry.RegisterHandler(TEXT("remove_widget_viewmodel_binding"), &RemoveViewModelBinding, {
		AssetPath(),
		MCPParam::Required(TEXT("bindingId"), EType::String, TEXT("Binding id, as list_widget_viewmodel_bindings reports it")),
	});
	Registry.RegisterHandler(TEXT("compile_widget_viewmodels"), &CompileViewModels, {
		AssetPath(),
	});
}

TSharedPtr<FJsonValue> FWidgetMVVMHandlers::ListViewModels(const TSharedPtr<FJsonObject>& Params)
{
	return MCPMVVMRun(TEXT("list_widget_viewmodels"), Params);
}

TSharedPtr<FJsonValue> FWidgetMVVMHandlers::AddViewModel(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("assetPath"), TEXT("viewModelClass"), TEXT("viewModelName"), TEXT("creationType"),
		TEXT("globalViewModelIdentifier"), TEXT("viewModelPropertyPath"), TEXT("optional"),
		TEXT("createSetter"), TEXT("createGetter"), TEXT("viewModelId"),
	});
	return MCPMVVMRun(TEXT("add_widget_viewmodel"), Params, EMCPMVVMReads::ViewModelClass);
}

TSharedPtr<FJsonValue> FWidgetMVVMHandlers::RemoveViewModel(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("assetPath"), TEXT("viewModelName") });
	return MCPMVVMRun(TEXT("remove_widget_viewmodel"), Params);
}

TSharedPtr<FJsonValue> FWidgetMVVMHandlers::ListViewModelBindings(const TSharedPtr<FJsonObject>& Params)
{
	return MCPMVVMRun(TEXT("list_widget_viewmodel_bindings"), Params);
}

TSharedPtr<FJsonValue> FWidgetMVVMHandlers::AddViewModelBinding(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("assetPath"), TEXT("sourcePath"), TEXT("destinationPath"), TEXT("bindingMode"), TEXT("executionMode"),
		TEXT("conversionFunction"), TEXT("conversionArguments"), TEXT("conversionDirection"),
		TEXT("enabled"), TEXT("includeInCompile"),
	});
	return MCPMVVMRun(TEXT("add_widget_viewmodel_binding"), Params, EMCPMVVMReads::ConversionFunction);
}

TSharedPtr<FJsonValue> FWidgetMVVMHandlers::UpdateViewModelBinding(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, {
		TEXT("assetPath"), TEXT("bindingId"), TEXT("sourcePath"), TEXT("destinationPath"), TEXT("bindingMode"),
		TEXT("executionMode"), TEXT("conversionFunction"), TEXT("conversionArguments"), TEXT("conversionDirection"),
		TEXT("enabled"), TEXT("includeInCompile"),
	});
	return MCPMVVMRun(TEXT("update_widget_viewmodel_binding"), Params, EMCPMVVMReads::ConversionFunction);
}

TSharedPtr<FJsonValue> FWidgetMVVMHandlers::RemoveViewModelBinding(const TSharedPtr<FJsonObject>& Params)
{
	MCPReadParamsAhead(Params, { TEXT("assetPath"), TEXT("bindingId") });
	return MCPMVVMRun(TEXT("remove_widget_viewmodel_binding"), Params);
}

TSharedPtr<FJsonValue> FWidgetMVVMHandlers::CompileViewModels(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (TSharedPtr<FJsonValue> Missing = RequireString(Params, TEXT("assetPath"), AssetPath))
	{
		return Missing;
	}
	TSharedPtr<FJsonValue> ResolveError;
	UWidgetBlueprint* WidgetBP = MCPWidget::ResolveWidgetBlueprintOrError(AssetPath, ResolveError);
	if (!WidgetBP)
	{
		return ResolveError;
	}
	FString BackendError;
	IMCPMVVMBackend* Backend = MCPMVVMRequireBackend(BackendError);
	if (!Backend)
	{
		return MCPError(BackendError);
	}

	const bool bWasUpToDate = WidgetBP->Status == BS_UpToDate || WidgetBP->Status == BS_UpToDateWithWarnings;
	const MCPWidgetGuidMap::FSyncReport Report = MCPWidgetGuidMap::CompileChecked(WidgetBP);
	if (Report.WasRefused())
	{
		return MCPWidgetGuidMap::BlockedError(AssetPath, Report);
	}
	FString SaveError;
	const bool bSaved = MCPWidgetGuidMap::SaveIfCompiled(WidgetBP, Report, SaveError);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveError);

	// The viewmodel compiler records each binding's problems on the view, which
	// is what the editor's binding panel shows; read them back after the compile.
	const FMCPMVVMOutcome Bindings = Backend->Run(TEXT("list_widget_viewmodel_bindings"), WidgetBP, Params);
	int32 BindingErrors = 0;
	if (Bindings.bOk && Bindings.Data.IsValid())
	{
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (Bindings.Data->TryGetArrayField(TEXT("bindings"), Rows) && Rows)
		{
			for (const TSharedPtr<FJsonValue>& Row : *Rows)
			{
				const TSharedPtr<FJsonObject>* RowObject = nullptr;
				const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
				if (Row.IsValid() && Row->TryGetObject(RowObject) && RowObject && (*RowObject)->TryGetArrayField(TEXT("errors"), Errors) && Errors)
				{
					BindingErrors += Errors->Num();
				}
			}
			Result->SetArrayField(TEXT("bindings"), *Rows);
		}
	}
	Result->SetNumberField(TEXT("bindingErrorCount"), BindingErrors);

	Result->SetBoolField(TEXT("changed"), !bWasUpToDate);
	MCPSetNoRollback(Result, TEXT("A compile regenerates the widget blueprint's class from its current graphs and view. The previous class is not kept, so no call restores it; the authored data the compile read is unchanged."));
	MCPSetWidgetCompileOutcome(Result, WidgetBP, AssetPath, Report, TEXT("compile_widget_viewmodels"));
	return MCPResult(Result);
}
