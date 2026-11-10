// Localization targets, cultures, gather/compile and FText keys. See
// LocalizationHandlers.h.
//
// A gather and a compile run the GatherText commandlet in a separate process
// through FLocalizationCommandletProcess, with the config the Localization
// Dashboard generates for the target, exactly as the dashboard's Gather Text
// and Compile Text buttons do. The dashboard shows a modal window while it
// waits; these answer the bridge request when the process exits instead.

#include "LocalizationHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerDeferred.h"
#include "Handlers/Widget/WidgetGuidMap.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Containers/Ticker.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "LocalizationCommandletExecution.h"
#include "LocalizationConfigurationScript.h"
#include "LocalizationDelegates.h"
#include "LocalizationSettings.h"
#include "LocalizationTargetTypes.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "WidgetBlueprint.h"

// ── Targets ──────────────────────────────────────────────────────────────────

/** A target by name, game targets first. Engine targets only when asked. */
static ULocalizationTarget* MCPLocFindTarget(const FString& Name, bool bIncludeEngine)
{
	TArray<ULocalizationTargetSet*> Sets = { ULocalizationSettings::GetGameTargetSet() };
	if (bIncludeEngine) Sets.Add(ULocalizationSettings::GetEngineTargetSet());
	for (ULocalizationTargetSet* Set : Sets)
	{
		if (!Set) continue;
		for (ULocalizationTarget* Target : Set->TargetObjects)
		{
			if (Target && Target->Settings.Name.Equals(Name, ESearchCase::IgnoreCase)) return Target;
		}
	}
	return nullptr;
}

static TArray<TSharedPtr<FJsonValue>> MCPLocStrings(const TArray<FString>& Values)
{
	TArray<TSharedPtr<FJsonValue>> Out;
	for (const FString& Value : Values) Out.Add(MakeShared<FJsonValueString>(Value));
	return Out;
}

static FString MCPLocNativeCulture(const FLocalizationTargetSettings& Settings)
{
	return Settings.SupportedCulturesStatistics.IsValidIndex(Settings.NativeCultureIndex)
		? Settings.SupportedCulturesStatistics[Settings.NativeCultureIndex].CultureName
		: FString();
}

static TSharedPtr<FJsonObject> MCPLocFileEntry(const FString& Path)
{
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	const FString Full = FPaths::ConvertRelativePathToFull(Path);
	Out->SetStringField(TEXT("path"), Full);
	const int64 Size = IFileManager::Get().FileSize(*Full);
	Out->SetBoolField(TEXT("exists"), Size >= 0);
	if (Size >= 0)
	{
		Out->SetNumberField(TEXT("bytes"), static_cast<double>(Size));
		Out->SetStringField(TEXT("modified"), IFileManager::Get().GetTimeStamp(*Full).ToIso8601());
	}
	return Out;
}

/** The parameters set_localization_target takes to put a target back to Settings. */
static TSharedPtr<FJsonObject> MCPLocTargetToSetParams(const FLocalizationTargetSettings& Settings)
{
	TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("target"), Settings.Name);
	TArray<FString> Cultures;
	for (const FCultureStatistics& Culture : Settings.SupportedCulturesStatistics) Cultures.Add(Culture.CultureName);
	Out->SetArrayField(TEXT("cultures"), MCPLocStrings(Cultures));
	Out->SetStringField(TEXT("nativeCulture"), MCPLocNativeCulture(Settings));
	Out->SetBoolField(TEXT("gatherFromPackages"), Settings.GatherFromPackages.IsEnabled);
	TArray<FString> Include;
	for (const FGatherTextIncludePath& Path : Settings.GatherFromPackages.IncludePathWildcards) Include.Add(Path.Pattern);
	Out->SetArrayField(TEXT("includePaths"), MCPLocStrings(Include));
	TArray<FString> Exclude;
	for (const FGatherTextExcludePath& Path : Settings.GatherFromPackages.ExcludePathWildcards) Exclude.Add(Path.Pattern);
	Out->SetArrayField(TEXT("excludePaths"), MCPLocStrings(Exclude));
	Out->SetBoolField(TEXT("gatherFromTextFiles"), Settings.GatherFromTextFiles.IsEnabled);
	TArray<FString> Directories;
	for (const FGatherTextSearchDirectory& Directory : Settings.GatherFromTextFiles.SearchDirectories) Directories.Add(Directory.Path);
	Out->SetArrayField(TEXT("searchDirectories"), MCPLocStrings(Directories));
	return Out;
}

static TSharedPtr<FJsonObject> MCPLocDescribeTarget(const ULocalizationTarget* Target)
{
	const FLocalizationTargetSettings& Settings = Target->Settings;
	TSharedPtr<FJsonObject> Out = MCPLocTargetToSetParams(Settings);
	Out->SetStringField(TEXT("guid"), Settings.Guid.ToString(EGuidFormats::DigitsWithHyphens));
	Out->SetBoolField(TEXT("engineTarget"), Target->IsMemberOfEngineTargetSet());
	Out->SetBoolField(TEXT("gatherFromMetaData"), Settings.GatherFromMetaData.IsEnabled);
	Out->SetStringField(TEXT("conflictStatus"), StaticEnum<ELocalizationTargetConflictStatus>()->GetNameStringByValue(static_cast<int64>(Settings.ConflictStatus)));

	TArray<TSharedPtr<FJsonValue>> Cultures;
	TArray<TSharedPtr<FJsonValue>> Archives;
	TArray<TSharedPtr<FJsonValue>> LocRes;
	for (const FCultureStatistics& Culture : Settings.SupportedCulturesStatistics)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("culture"), Culture.CultureName);
		Row->SetNumberField(TEXT("wordCount"), Culture.WordCount);
		Cultures.Add(MakeShared<FJsonValueObject>(Row));

		TSharedPtr<FJsonObject> Archive = MCPLocFileEntry(LocalizationConfigurationScript::GetArchivePath(Target, Culture.CultureName));
		Archive->SetStringField(TEXT("culture"), Culture.CultureName);
		Archives.Add(MakeShared<FJsonValueObject>(Archive));
		TSharedPtr<FJsonObject> Resource = MCPLocFileEntry(LocalizationConfigurationScript::GetLocResPath(Target, Culture.CultureName));
		Resource->SetStringField(TEXT("culture"), Culture.CultureName);
		LocRes.Add(MakeShared<FJsonValueObject>(Resource));
	}
	Out->SetArrayField(TEXT("cultureStatistics"), Cultures);

	TSharedPtr<FJsonObject> Files = MakeShared<FJsonObject>();
	Files->SetStringField(TEXT("gatherConfig"), FPaths::ConvertRelativePathToFull(LocalizationConfigurationScript::GetGatherTextConfigPath(Target)));
	Files->SetStringField(TEXT("compileConfig"), FPaths::ConvertRelativePathToFull(LocalizationConfigurationScript::GetCompileTextConfigPath(Target)));
	Files->SetObjectField(TEXT("manifest"), MCPLocFileEntry(LocalizationConfigurationScript::GetManifestPath(Target)));
	Files->SetArrayField(TEXT("archives"), Archives);
	Files->SetArrayField(TEXT("locres"), LocRes);
	Out->SetObjectField(TEXT("files"), Files);
	return Out;
}

static bool MCPLocValidTargetName(const FString& Name)
{
	if (Name.IsEmpty() || Name.Len() > 64) return false;
	for (const TCHAR Ch : Name)
	{
		if (!FChar::IsAlnum(Ch) && Ch != TEXT('_') && Ch != TEXT('-')) return false;
	}
	return true;
}

static bool MCPLocReadStringArray(const TSharedPtr<FJsonObject>& Params, const TCHAR* Key, bool& bOutGiven, TArray<FString>& Out, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	bOutGiven = TryGetArrayParam(Params, Key, Values) && Values;
	if (!bOutGiven) return true;
	for (const TSharedPtr<FJsonValue>& Value : *Values)
	{
		FString Text;
		if (!Value.IsValid() || !Value->TryGetString(Text))
		{
			OutError = FString::Printf(TEXT("%s has to be an array of strings."), Key);
			return false;
		}
		Out.Add(Text);
	}
	return true;
}

static void MCPLocPersistTargets()
{
	// The target set copies its objects back into the config-backed settings
	// and writes DefaultEditor.ini, the way an edit in Project Settings does.
	if (ULocalizationTargetSet* GameSet = ULocalizationSettings::GetGameTargetSet())
	{
		GameSet->PostEditChange();
	}
}

// ── Commandlet runs ──────────────────────────────────────────────────────────

struct FMCPLocRunState
{
	TSharedPtr<FLocalizationCommandletProcess> Process;
	TWeakObjectPtr<ULocalizationTarget> Target;
	FString TargetName;
	FString Step;
	FString Culture;
	FString ConfigPath;
	FString Log;
	double StartSeconds = 0.0;
};

static bool& MCPLocRunActive()
{
	static bool bActive = false;
	return bActive;
}

/** Drain the pipe. True while the process is running or still has output. */
static bool MCPLocPump(FMCPLocRunState& State)
{
	const FString Chunk = FPlatformProcess::ReadPipe(State.Process->GetReadPipe());
	if (!Chunk.IsEmpty())
	{
		State.Log += Chunk;
		// The tail is what is reported; keep memory bounded on a long gather.
		constexpr int32 MaxLog = 512 * 1024;
		if (State.Log.Len() > MaxLog) State.Log.RightChopInline(State.Log.Len() - MaxLog);
	}
	return FPlatformProcess::IsProcRunning(State.Process->GetHandle()) || !Chunk.IsEmpty();
}

/**
 * Write the target's config for Step, start the commandlet, and answer when it
 * exits. Step is "gather" or "compile"; Culture narrows a compile to one culture.
 */
static TSharedPtr<FJsonValue> MCPLocRunCommandlet(ULocalizationTarget* Target, const FString& Step, const FString& Culture)
{
	if (MCPLocRunActive())
	{
		return MCPError(TEXT("A localization gather or compile is already running. Wait for it to answer, then run this one."));
	}

	FString ConfigPath;
	bool bWritten = false;
	if (Step == TEXT("gather"))
	{
		ConfigPath = LocalizationConfigurationScript::GetGatherTextConfigPath(Target);
		bWritten = LocalizationConfigurationScript::GenerateGatherTextConfigFile(Target).WriteWithSCC(ConfigPath);
	}
	else
	{
		const TOptional<FString> CultureName = Culture.IsEmpty() ? TOptional<FString>() : TOptional<FString>(Culture);
		ConfigPath = LocalizationConfigurationScript::GetCompileTextConfigPath(Target, CultureName);
		bWritten = LocalizationConfigurationScript::GenerateCompileTextConfigFile(Target, CultureName).WriteWithSCC(ConfigPath);
	}
	if (!bWritten)
	{
		return MCPError(FString::Printf(TEXT("Could not write the %s config for target '%s' to %s. Check the file is writable (or checked out)."),
			*Step, *Target->Settings.Name, *FPaths::ConvertRelativePathToFull(ConfigPath)));
	}

	TSharedPtr<FMCPLocRunState> State = MakeShared<FMCPLocRunState>();
	State->Process = FLocalizationCommandletProcess::Execute(ConfigPath, !Target->IsMemberOfEngineTargetSet());
	if (!State->Process.IsValid())
	{
		return MCPError(FString::Printf(TEXT("Could not start the GatherText commandlet for target '%s'."), *Target->Settings.Name));
	}
	State->Target = Target;
	State->TargetName = Target->Settings.Name;
	State->Step = Step;
	State->Culture = Culture;
	State->ConfigPath = ConfigPath;
	State->StartSeconds = FPlatformTime::Seconds();
	MCPLocRunActive() = true;

	// Runs on the game thread once the process has exited and the pipe is empty.
	auto Finish = [](FMCPLocRunState& Run) -> TSharedPtr<FJsonValue>
	{
		MCPLocRunActive() = false;
		int32 ExitCode = -1;
		FPlatformProcess::GetProcReturnCode(Run.Process->GetHandle(), &ExitCode);

		TArray<FString> Lines;
		Run.Log.ParseIntoArrayLines(Lines);
		TArray<FString> Errors;
		int32 Warnings = 0;
		for (const FString& Line : Lines)
		{
			if (Line.Contains(TEXT("Error:"))) Errors.Add(Line.TrimStartAndEnd());
			else if (Line.Contains(TEXT("Warning:"))) ++Warnings;
		}
		TArray<FString> Tail;
		for (int32 Index = FMath::Max(0, Lines.Num() - 40); Index < Lines.Num(); ++Index) Tail.Add(Lines[Index]);

		const bool bSucceeded = ExitCode == 0;
		TSharedPtr<FJsonObject> Result = bSucceeded
			? MCPSuccess()
			: MCPErrorObject(FString::Printf(TEXT("GatherText (%s) for target '%s' exited with code %d%s"),
				*Run.Step, *Run.TargetName, ExitCode, Errors.Num() ? *FString::Printf(TEXT(": %s"), *Errors[0]) : TEXT("; see logTail")));
		Result->SetStringField(TEXT("target"), Run.TargetName);
		Result->SetStringField(TEXT("step"), Run.Step);
		if (!Run.Culture.IsEmpty()) Result->SetStringField(TEXT("culture"), Run.Culture);
		Result->SetNumberField(TEXT("exitCode"), ExitCode);
		Result->SetNumberField(TEXT("durationSeconds"), FPlatformTime::Seconds() - Run.StartSeconds);
		Result->SetStringField(TEXT("configPath"), FPaths::ConvertRelativePathToFull(Run.ConfigPath));
		Result->SetStringField(TEXT("commandletArguments"), Run.Process->GetProcessArguments());
		Result->SetArrayField(TEXT("errors"), MCPLocStrings(Errors));
		Result->SetNumberField(TEXT("warningCount"), Warnings);
		Result->SetArrayField(TEXT("logTail"), MCPLocStrings(Tail));

		if (ULocalizationTarget* Target = Run.Target.Get())
		{
			if (Run.Step == TEXT("gather"))
			{
				Target->UpdateWordCountsFromCSV();
				Target->UpdateStatusFromConflictReport();
			}
			else
			{
				// Pick the new LocRes up in this editor session.
				FTextLocalizationManager::Get().RefreshResources();
			}
			const FString ContentPath = FPaths::Combine(Target->IsMemberOfEngineTargetSet() ? FPaths::EngineContentDir() : FPaths::ProjectContentDir(), TEXT("Localization"), Target->Settings.Name);
			LocalizationDelegates::OnLocalizationTargetDataUpdated.Broadcast(ContentPath);
			Result->SetObjectField(TEXT("targetState"), MCPLocDescribeTarget(Target));
		}

		Result->SetBoolField(TEXT("changed"), bSucceeded);
		MCPSetNoRollback(Result, Run.Step == TEXT("gather")
			? TEXT("A gather rewrites the target's manifest, archives and word-count report in place. The previous files are not kept, so no call restores them; recover them from source control.")
			: TEXT("A compile rewrites the target's LocRes files from its archives. The previous files are not kept; compiling again from unchanged archives reproduces them."));
		return MCPResult(Result);
	};

	const FMCPDeferredCallPtr Deferred = MCPDeferRequest();
	if (!Deferred)
	{
		// Called in-process: wait here.
		while (MCPLocPump(*State))
		{
			FPlatformProcess::Sleep(0.05f);
		}
		return Finish(*State);
	}

	Deferred->SetCancelNote(TEXT("The commandlet process was terminated. Files it had already written stay as they are."));
	Deferred->OnCancel([State](const FString&)
	{
		if (State->Process.IsValid() && FPlatformProcess::IsProcRunning(State->Process->GetHandle()))
		{
			FPlatformProcess::TerminateProc(State->Process->GetHandle(), true);
		}
	});
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State, Deferred, Finish](float) -> bool
	{
		if (MCPLocPump(*State)) return true;
		TSharedPtr<FJsonValue> Answer = Finish(*State);
		if (!Deferred->IsCancelled()) Deferred->Complete(Answer);
		return false;
	}), 0.1f);
	return MCPDeferredMarker();
}

// ── FText properties ─────────────────────────────────────────────────────────

/** The object whose property is edited: a widget in a Widget Blueprint's tree, a Blueprint's CDO, or the asset. */
static UObject* MCPLocResolveTextOwner(UObject* Asset, const FString& WidgetName, FString& OutError)
{
	if (!WidgetName.IsEmpty())
	{
		UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset);
		if (!WidgetBP)
		{
			OutError = FString::Printf(TEXT("widgetName needs a Widget Blueprint; %s is a %s."), *Asset->GetPathName(), *Asset->GetClass()->GetName());
			return nullptr;
		}
		UWidget* Widget = WidgetBP->WidgetTree ? WidgetBP->WidgetTree->FindWidget(FName(*WidgetName)) : nullptr;
		if (!Widget)
		{
			OutError = FString::Printf(TEXT("No widget named '%s' in %s."), *WidgetName, *Asset->GetPathName());
		}
		return Widget;
	}
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		UObject* Defaults = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
		if (!Defaults)
		{
			OutError = FString::Printf(TEXT("Blueprint %s has no generated class yet; compile it first."), *Asset->GetPathName());
		}
		return Defaults;
	}
	return Asset;
}

/** Walk PropertyPath (dots through structs and object references) to an FText. */
static FText* MCPLocResolveText(UObject* Owner, const FString& PropertyPath, FString& OutError)
{
	TArray<FString> Parts;
	PropertyPath.ParseIntoArray(Parts, TEXT("."), true);
	if (Parts.Num() == 0)
	{
		OutError = TEXT("propertyName is empty.");
		return nullptr;
	}
	const UStruct* Struct = Owner->GetClass();
	void* Container = Owner;
	for (int32 Index = 0; Index < Parts.Num(); ++Index)
	{
		FProperty* Property = FindFProperty<FProperty>(Struct, FName(*Parts[Index]));
		if (!Property)
		{
			OutError = FString::Printf(TEXT("%s has no property '%s'."), *Struct->GetName(), *Parts[Index]);
			return nullptr;
		}
		void* Value = Property->ContainerPtrToValuePtr<void>(Container);
		if (Index == Parts.Num() - 1)
		{
			if (!CastField<FTextProperty>(Property))
			{
				OutError = FString::Printf(TEXT("'%s' is a %s, not a Text property."), *PropertyPath, *Property->GetCPPType());
				return nullptr;
			}
			return static_cast<FText*>(Value);
		}
		if (const FStructProperty* AsStruct = CastField<FStructProperty>(Property))
		{
			Struct = AsStruct->Struct;
			Container = Value;
		}
		else if (const FObjectPropertyBase* AsObject = CastField<FObjectPropertyBase>(Property))
		{
			UObject* Inner = AsObject->GetObjectPropertyValue(Value);
			if (!Inner)
			{
				OutError = FString::Printf(TEXT("'%s' is empty, so '%s' cannot be reached."), *Parts[Index], *PropertyPath);
				return nullptr;
			}
			Struct = Inner->GetClass();
			Container = Inner;
		}
		else
		{
			OutError = FString::Printf(TEXT("'%s' is neither a struct nor an object, so '%s' cannot be reached."), *Parts[Index], *PropertyPath);
			return nullptr;
		}
	}
	return nullptr;
}

static void MCPLocDescribeText(const TSharedPtr<FJsonObject>& Out, const FText& Text)
{
	Out->SetStringField(TEXT("text"), Text.ToString());
	const FString* Source = FTextInspector::GetSourceString(Text);
	Out->SetStringField(TEXT("sourceString"), Source ? *Source : Text.ToString());
	FName TableId;
	FString TableKey;
	const bool bFromTable = FTextInspector::GetTableIdAndKey(Text, TableId, TableKey);
	Out->SetBoolField(TEXT("isFromStringTable"), bFromTable);
	Out->SetStringField(TEXT("stringTableId"), bFromTable ? TableId.ToString() : FString());
	Out->SetStringField(TEXT("stringTableKey"), bFromTable ? TableKey : FString());
	const TOptional<FString> Namespace = FTextInspector::GetNamespace(Text);
	const TOptional<FString> Key = FTextInspector::GetKey(Text);
	const FString FullNamespace = Namespace.IsSet() ? Namespace.GetValue() : FString();
	Out->SetStringField(TEXT("namespace"), TextNamespaceUtil::StripPackageNamespace(FullNamespace));
	Out->SetStringField(TEXT("packageNamespace"), TextNamespaceUtil::ExtractPackageNamespace(FullNamespace));
	Out->SetStringField(TEXT("fullNamespace"), FullNamespace);
	Out->SetStringField(TEXT("key"), Key.IsSet() ? Key.GetValue() : FString());
	Out->SetBoolField(TEXT("isCultureInvariant"), Text.IsCultureInvariant());
	Out->SetBoolField(TEXT("isLocalizable"), FTextInspector::ShouldGatherForLocalization(Text));
}

// ── Registration ─────────────────────────────────────────────────────────────

void FLocalizationHandlers::RegisterHandlers(FMCPHandlerRegistry& Registry)
{
	using EType = EMCPParamType;
	{
		FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("project"));
		Registry.RegisterHandler(TEXT("list_localization_cultures"), &ListCultures, {});
		Registry.RegisterHandler(TEXT("list_localization_targets"), &ListTargets, {
			MCPParam::Optional(TEXT("includeEngineTargets"), EType::Boolean, TEXT("Also list the engine's own targets (default false)")),
		});
		Registry.RegisterHandler(TEXT("set_localization_target"), &SetTarget, {
			MCPParam::Required(TEXT("target"), EType::String, TEXT("Localization target name (letters, digits, _ and -); created when the project has none of that name")),
			MCPParam::Optional(TEXT("cultures"), EType::Array, TEXT("Culture codes the target supports, e.g. [\"en\", \"fr\"]; replaces the list")).Items(EType::String),
			MCPParam::Optional(TEXT("nativeCulture"), EType::String, TEXT("The culture source text is written in; one of cultures (default the first)")),
			MCPParam::Optional(TEXT("gatherFromPackages"), EType::Boolean, TEXT("Gather text from assets and maps")),
			MCPParam::Optional(TEXT("includePaths"), EType::Array, TEXT("Package path wildcards to gather from, relative to the project root, e.g. Content/UI/*; replaces the list")).Items(EType::String),
			MCPParam::Optional(TEXT("excludePaths"), EType::Array, TEXT("Package path wildcards to skip; replaces the list")).Items(EType::String),
			MCPParam::Optional(TEXT("gatherFromTextFiles"), EType::Boolean, TEXT("Gather LOCTEXT and NSLOCTEXT from source files")),
			MCPParam::Optional(TEXT("searchDirectories"), EType::Array, TEXT("Source directories to gather from, relative to the project root, e.g. Source; replaces the list")).Items(EType::String),
		});
		Registry.RegisterHandler(TEXT("remove_localization_target"), &RemoveTarget, {
			MCPParam::Required(TEXT("target"), EType::String, TEXT("Localization target name")),
			MCPParam::Optional(TEXT("deleteFiles"), EType::Boolean, TEXT("Also delete its config, manifest, archives and LocRes files (default false)")),
		});
		Registry.RegisterHandlerWithTimeout(TEXT("gather_localization_target"), &GatherTarget, 1800.0f, {
			MCPParam::Required(TEXT("target"), EType::String, TEXT("Localization target name")),
		});
		Registry.RegisterHandlerWithTimeout(TEXT("compile_localization_target"), &CompileTarget, 1800.0f, {
			MCPParam::Required(TEXT("target"), EType::String, TEXT("Localization target name")),
			MCPParam::Optional(TEXT("culture"), EType::String, TEXT("Compile only this culture (default every culture of the target)")),
		});
	}
	{
		FMCPHandlerRegistry::FCategoryScope CategoryScope(Registry, TEXT("asset"));
		Registry.RegisterHandler(TEXT("get_text_localization"), &GetTextLocalization, {
			MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("Asset holding the Text property; a Blueprint means its class defaults")).Alias(TEXT("path")),
			MCPParam::Required(TEXT("propertyName"), EType::String, TEXT("Text property, dotted through structs and object references")),
			MCPParam::Optional(TEXT("widgetName"), EType::String, TEXT("A widget in a Widget Blueprint's tree that owns the property")),
		});
		Registry.RegisterHandler(TEXT("set_text_localization"), &SetTextLocalization, {
			MCPParam::Required(TEXT("assetPath"), EType::String, TEXT("Asset holding the Text property; a Blueprint means its class defaults")).Alias(TEXT("path")),
			MCPParam::Required(TEXT("propertyName"), EType::String, TEXT("Text property, dotted through structs and object references")),
			MCPParam::Optional(TEXT("widgetName"), EType::String, TEXT("A widget in a Widget Blueprint's tree that owns the property")),
			MCPParam::Optional(TEXT("namespace"), EType::String, TEXT("Text namespace, without the package marker (default the current one)")),
			MCPParam::Optional(TEXT("key"), EType::String, TEXT("Text key (default the current one)")),
			MCPParam::Optional(TEXT("sourceString"), EType::String, TEXT("Source string (default the current one)")),
			MCPParam::Optional(TEXT("stringTableId"), EType::String, TEXT("Reference a string table entry instead: the table's asset path or id, with stringTableKey")),
			MCPParam::Optional(TEXT("stringTableKey"), EType::String, TEXT("Entry key in stringTableId")),
		});
	}
}

// ── Handlers ─────────────────────────────────────────────────────────────────

TSharedPtr<FJsonValue> FLocalizationHandlers::ListCultures(const TSharedPtr<FJsonObject>& Params)
{
	FInternationalization& I18N = FInternationalization::Get();
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("currentCulture"), I18N.GetCurrentCulture()->GetName());
	Result->SetStringField(TEXT("currentLanguage"), I18N.GetCurrentLanguage()->GetName());
	Result->SetStringField(TEXT("currentLocale"), I18N.GetCurrentLocale()->GetName());

	FTextLocalizationManager& Manager = FTextLocalizationManager::Get();
	Result->SetStringField(TEXT("gameNativeCulture"), Manager.GetNativeCultureName(ELocalizedTextSourceCategory::Game));
	Result->SetStringField(TEXT("editorNativeCulture"), Manager.GetNativeCultureName(ELocalizedTextSourceCategory::Editor));
	Result->SetArrayField(TEXT("gameLocalizedCultures"), MCPLocStrings(Manager.GetLocalizedCultureNames(ELocalizationLoadFlags::Game)));

	TArray<FString> CulturesToStage;
	GConfig->GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("CulturesToStage"), CulturesToStage, GGameIni);
	Result->SetArrayField(TEXT("culturesToStage"), MCPLocStrings(CulturesToStage));

	TArray<FString> TargetCultures;
	TArray<TSharedPtr<FJsonValue>> PerTarget;
	if (const ULocalizationTargetSet* GameSet = ULocalizationSettings::GetGameTargetSet())
	{
		for (const ULocalizationTarget* Target : GameSet->TargetObjects)
		{
			if (!Target) continue;
			TArray<FString> Cultures;
			for (const FCultureStatistics& Culture : Target->Settings.SupportedCulturesStatistics)
			{
				Cultures.Add(Culture.CultureName);
				TargetCultures.AddUnique(Culture.CultureName);
			}
			TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
			Row->SetStringField(TEXT("target"), Target->Settings.Name);
			Row->SetStringField(TEXT("nativeCulture"), MCPLocNativeCulture(Target->Settings));
			Row->SetArrayField(TEXT("cultures"), MCPLocStrings(Cultures));
			PerTarget.Add(MakeShared<FJsonValueObject>(Row));
		}
	}
	TargetCultures.Sort();
	Result->SetArrayField(TEXT("targetCultures"), MCPLocStrings(TargetCultures));
	Result->SetArrayField(TEXT("targets"), PerTarget);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLocalizationHandlers::ListTargets(const TSharedPtr<FJsonObject>& Params)
{
	const bool bIncludeEngine = OptionalBool(Params, TEXT("includeEngineTargets"), false);
	TArray<TSharedPtr<FJsonValue>> Rows;
	TArray<const ULocalizationTargetSet*> Sets = { ULocalizationSettings::GetGameTargetSet() };
	if (bIncludeEngine) Sets.Add(ULocalizationSettings::GetEngineTargetSet());
	for (const ULocalizationTargetSet* Set : Sets)
	{
		if (!Set) continue;
		for (const ULocalizationTarget* Target : Set->TargetObjects)
		{
			if (Target) Rows.Add(MakeShared<FJsonValueObject>(MCPLocDescribeTarget(Target)));
		}
	}
	auto Result = MCPSuccess();
	Result->SetArrayField(TEXT("targets"), Rows);
	Result->SetNumberField(TEXT("count"), Rows.Num());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLocalizationHandlers::SetTarget(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	const TSharedPtr<FJsonValue> MissingName = RequireString(Params, TEXT("target"), Name);
	FString ArrayError;
	bool bCulturesGiven = false, bIncludeGiven = false, bExcludeGiven = false, bDirectoriesGiven = false;
	TArray<FString> Cultures, Include, Exclude, Directories;
	const bool bArraysOk =
		MCPLocReadStringArray(Params, TEXT("cultures"), bCulturesGiven, Cultures, ArrayError)
		&& MCPLocReadStringArray(Params, TEXT("includePaths"), bIncludeGiven, Include, ArrayError)
		&& MCPLocReadStringArray(Params, TEXT("excludePaths"), bExcludeGiven, Exclude, ArrayError)
		&& MCPLocReadStringArray(Params, TEXT("searchDirectories"), bDirectoriesGiven, Directories, ArrayError);
	MCPReadParamsAhead(Params, { TEXT("cultures"), TEXT("includePaths"), TEXT("excludePaths"), TEXT("searchDirectories") });
	FString NativeCulture;
	const bool bNativeGiven = TryGetStringParam(Params, TEXT("nativeCulture"), NativeCulture) && !NativeCulture.IsEmpty();
	bool bPackages = false, bTextFiles = false;
	const bool bPackagesGiven = TryGetBoolParam(Params, TEXT("gatherFromPackages"), bPackages);
	const bool bTextFilesGiven = TryGetBoolParam(Params, TEXT("gatherFromTextFiles"), bTextFiles);

	if (MissingName) return MissingName;
	if (!bArraysOk) return MCPError(ArrayError);
	if (!MCPLocValidTargetName(Name))
	{
		return MCPError(FString::Printf(TEXT("target '%s' is not a valid localization target name: use letters, digits, _ and -, at most 64 characters."), *Name));
	}
	for (const FString& Culture : Cultures)
	{
		if (!FInternationalization::Get().GetCulture(Culture).IsValid())
		{
			return MCPError(FString::Printf(TEXT("'%s' is not a culture this engine knows (try en, fr, de, ja, zh-Hans)."), *Culture));
		}
	}

	ULocalizationTargetSet* GameSet = ULocalizationSettings::GetGameTargetSet();
	if (!GameSet)
	{
		return MCPError(TEXT("The project's localization target set is not available."));
	}
	if (ULocalizationTarget* EngineTarget = MCPLocFindTarget(Name, true))
	{
		if (EngineTarget->IsMemberOfEngineTargetSet())
		{
			return MCPError(FString::Printf(TEXT("'%s' is an engine localization target; only project targets can be changed."), *Name));
		}
	}

	ULocalizationTarget* Target = MCPLocFindTarget(Name, false);
	const bool bCreated = Target == nullptr;
	TSharedPtr<FJsonObject> Before;
	FLocalizationTargetSettings Settings;
	if (Target)
	{
		Before = MCPLocTargetToSetParams(Target->Settings);
		Settings = Target->Settings;
	}
	else
	{
		Settings.Name = Name;
		// A new target gathers nothing until told what to gather from.
		Settings.GatherFromTextFiles.IsEnabled = false;
		Settings.GatherFromPackages.IsEnabled = false;
		Settings.GatherFromMetaData.IsEnabled = false;
	}

	const FString PreviousNative = MCPLocNativeCulture(Settings);
	if (bCulturesGiven)
	{
		TArray<FCultureStatistics> Statistics;
		for (const FString& Culture : Cultures)
		{
			const FCultureStatistics* Existing = Settings.SupportedCulturesStatistics.FindByPredicate(
				[&Culture](const FCultureStatistics& Row) { return Row.CultureName == Culture; });
			Statistics.Add(Existing ? *Existing : FCultureStatistics(Culture));
		}
		Settings.SupportedCulturesStatistics = Statistics;
	}
	const FString WantNative = bNativeGiven ? NativeCulture : PreviousNative;
	Settings.NativeCultureIndex = Settings.SupportedCulturesStatistics.IndexOfByPredicate(
		[&WantNative](const FCultureStatistics& Row) { return Row.CultureName == WantNative; });
	if (Settings.NativeCultureIndex == INDEX_NONE)
	{
		if (bNativeGiven)
		{
			return MCPError(FString::Printf(TEXT("nativeCulture '%s' is not one of the target's cultures."), *NativeCulture));
		}
		Settings.NativeCultureIndex = Settings.SupportedCulturesStatistics.Num() > 0 ? 0 : INDEX_NONE;
	}
	if (bPackagesGiven) Settings.GatherFromPackages.IsEnabled = bPackages;
	if (bIncludeGiven)
	{
		Settings.GatherFromPackages.IncludePathWildcards.Reset();
		for (const FString& Pattern : Include)
		{
			FGatherTextIncludePath Path;
			Path.Pattern = Pattern;
			Settings.GatherFromPackages.IncludePathWildcards.Add(Path);
		}
	}
	if (bExcludeGiven)
	{
		Settings.GatherFromPackages.ExcludePathWildcards.Reset();
		for (const FString& Pattern : Exclude)
		{
			FGatherTextExcludePath Path;
			Path.Pattern = Pattern;
			Settings.GatherFromPackages.ExcludePathWildcards.Add(Path);
		}
	}
	if (bTextFilesGiven) Settings.GatherFromTextFiles.IsEnabled = bTextFiles;
	if (bDirectoriesGiven)
	{
		Settings.GatherFromTextFiles.SearchDirectories.Reset();
		for (const FString& Directory : Directories)
		{
			FGatherTextSearchDirectory Path;
			Path.Path = Directory;
			Settings.GatherFromTextFiles.SearchDirectories.Add(Path);
		}
	}

	if (!bCreated)
	{
		const TSharedPtr<FJsonObject> After = MCPLocTargetToSetParams(Settings);
		const TSharedRef<FJsonValueObject> AV = MakeShared<FJsonValueObject>(After);
		const TSharedRef<FJsonValueObject> BV = MakeShared<FJsonValueObject>(Before);
		if (FJsonValue::CompareEqual(*AV, *BV))
		{
			auto Result = MCPSuccess();
			MCPSetExisted(Result);
			Result->SetBoolField(TEXT("unchanged"), true);
			Result->SetObjectField(TEXT("targetState"), MCPLocDescribeTarget(Target));
			return MCPResult(Result);
		}
	}

	GameSet->Modify();
	if (bCreated)
	{
		Target = NewObject<ULocalizationTarget>(GameSet);
		GameSet->TargetObjects.Add(Target);
	}
	Target->Modify();
	Target->Settings = Settings;
	MCPLocPersistTargets();

	auto Result = MCPSuccess();
	if (bCreated) MCPSetCreated(Result); else MCPSetUpdated(Result);
	Result->SetObjectField(TEXT("targetState"), MCPLocDescribeTarget(Target));
	if (bCreated)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("target"), Name);
		MCPSetRollback(Result, TEXT("remove_localization_target"), Payload);
	}
	else
	{
		Result->SetObjectField(TEXT("previous"), Before);
		MCPSetRollback(Result, TEXT("set_localization_target"), Before);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLocalizationHandlers::RemoveTarget(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	const TSharedPtr<FJsonValue> MissingName = RequireString(Params, TEXT("target"), Name);
	const bool bDeleteFiles = OptionalBool(Params, TEXT("deleteFiles"), false);
	if (MissingName) return MissingName;

	ULocalizationTarget* Target = MCPLocFindTarget(Name, true);
	if (Target && Target->IsMemberOfEngineTargetSet())
	{
		return MCPError(FString::Printf(TEXT("'%s' is an engine localization target; only project targets can be removed."), *Name));
	}
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("target"), Name);
	if (!Target)
	{
		Result->SetBoolField(TEXT("alreadyRemoved"), true);
		return MCPResult(Result);
	}

	const TSharedPtr<FJsonObject> Restore = MCPLocTargetToSetParams(Target->Settings);
	bool bFilesDeleted = false;
	if (bDeleteFiles)
	{
		bFilesDeleted = Target->DeleteFiles();
		Result->SetBoolField(TEXT("filesDeleted"), bFilesDeleted);
	}
	ULocalizationTargetSet* GameSet = ULocalizationSettings::GetGameTargetSet();
	GameSet->Modify();
	GameSet->TargetObjects.Remove(Target);
	MCPLocPersistTargets();

	Result->SetBoolField(TEXT("deleted"), true);
	MCPSetRollback(Result, TEXT("set_localization_target"), Restore);
	if (bDeleteFiles)
	{
		Result->SetStringField(TEXT("rollbackNote"), TEXT("The rollback recreates the target's settings under a new GUID. The deleted manifest, archives and LocRes files are not restored; gather and compile again to regenerate them."));
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLocalizationHandlers::GatherTarget(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	if (auto Missing = RequireString(Params, TEXT("target"), Name)) return Missing;
	ULocalizationTarget* Target = MCPLocFindTarget(Name, true);
	if (!Target)
	{
		return MCPError(FString::Printf(TEXT("No localization target named '%s'. project(list_localization_targets) lists them."), *Name));
	}
	return MCPLocRunCommandlet(Target, TEXT("gather"), FString());
}

TSharedPtr<FJsonValue> FLocalizationHandlers::CompileTarget(const TSharedPtr<FJsonObject>& Params)
{
	FString Name;
	const TSharedPtr<FJsonValue> Missing = RequireString(Params, TEXT("target"), Name);
	const FString Culture = OptionalString(Params, TEXT("culture"));
	if (Missing) return Missing;
	ULocalizationTarget* Target = MCPLocFindTarget(Name, true);
	if (!Target)
	{
		return MCPError(FString::Printf(TEXT("No localization target named '%s'. project(list_localization_targets) lists them."), *Name));
	}
	if (!Culture.IsEmpty() && !Target->Settings.SupportedCulturesStatistics.ContainsByPredicate(
		[&Culture](const FCultureStatistics& Row) { return Row.CultureName == Culture; }))
	{
		return MCPError(FString::Printf(TEXT("Target '%s' does not support culture '%s'."), *Name, *Culture));
	}
	return MCPLocRunCommandlet(Target, TEXT("compile"), Culture);
}

TSharedPtr<FJsonValue> FLocalizationHandlers::GetTextLocalization(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	const TSharedPtr<FJsonValue> MissingAsset = RequireString(Params, TEXT("assetPath"), AssetPath);
	FString PropertyName;
	const TSharedPtr<FJsonValue> MissingProperty = RequireString(Params, TEXT("propertyName"), PropertyName);
	const FString WidgetName = OptionalString(Params, TEXT("widgetName"));
	if (MissingAsset) return MissingAsset;
	if (MissingProperty) return MissingProperty;

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;
	FString Error;
	UObject* Owner = MCPLocResolveTextOwner(Asset, WidgetName, Error);
	if (!Owner) return MCPError(Error);
	const FText* Text = MCPLocResolveText(Owner, PropertyName, Error);
	if (!Text) return MCPError(Error);

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	if (!WidgetName.IsEmpty()) Result->SetStringField(TEXT("widgetName"), WidgetName);
	MCPLocDescribeText(Result, *Text);
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLocalizationHandlers::SetTextLocalization(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	const TSharedPtr<FJsonValue> MissingAsset = RequireString(Params, TEXT("assetPath"), AssetPath);
	FString PropertyName;
	const TSharedPtr<FJsonValue> MissingProperty = RequireString(Params, TEXT("propertyName"), PropertyName);
	const FString WidgetName = OptionalString(Params, TEXT("widgetName"));
	FString Namespace, Key, SourceString, TableId, TableKey;
	const bool bNamespaceGiven = TryGetStringParam(Params, TEXT("namespace"), Namespace);
	const bool bKeyGiven = TryGetStringParam(Params, TEXT("key"), Key) && !Key.IsEmpty();
	const bool bSourceGiven = TryGetStringParam(Params, TEXT("sourceString"), SourceString);
	const bool bTableGiven = TryGetStringParam(Params, TEXT("stringTableId"), TableId) && !TableId.IsEmpty();
	const bool bTableKeyGiven = TryGetStringParam(Params, TEXT("stringTableKey"), TableKey) && !TableKey.IsEmpty();
	if (MissingAsset) return MissingAsset;
	if (MissingProperty) return MissingProperty;

	if (bTableGiven != bTableKeyGiven)
	{
		return MCPError(TEXT("stringTableId and stringTableKey go together."));
	}
	if (bTableGiven && (bNamespaceGiven || bKeyGiven || bSourceGiven))
	{
		return MCPError(TEXT("A string table reference takes its namespace, key and source from the table: pass stringTableId and stringTableKey, or namespace/key/sourceString, not both."));
	}
	if (!bTableGiven && !bNamespaceGiven && !bKeyGiven && !bSourceGiven)
	{
		return MCPError(TEXT("Nothing to set: pass namespace, key or sourceString, or stringTableId with stringTableKey."));
	}

	TSharedPtr<FJsonValue> LoadError;
	UObject* Asset = MCPRequireAssetObject(AssetPath, LoadError);
	if (!Asset) return LoadError;
	FString Error;
	UObject* Owner = MCPLocResolveTextOwner(Asset, WidgetName, Error);
	if (!Owner) return MCPError(Error);
	FText* Text = MCPLocResolveText(Owner, PropertyName, Error);
	if (!Text) return MCPError(Error);

	const FText Previous = *Text;
	TSharedPtr<FJsonObject> PreviousState = MakeShared<FJsonObject>();
	MCPLocDescribeText(PreviousState, Previous);

	FText Updated;
	if (bTableGiven)
	{
		FName ResolvedId(*TableId);
		if (UStringTable* Table = LoadAssetByPath<UStringTable>(TableId))
		{
			ResolvedId = Table->GetStringTableId();
			FString Unused;
			if (!Table->GetStringTable()->GetSourceString(FTextKey(TableKey), Unused))
			{
				return MCPError(FString::Printf(TEXT("String table %s has no entry '%s'."), *TableId, *TableKey));
			}
		}
		Updated = FText::FromStringTable(ResolvedId, FTextKey(TableKey));
	}
	else
	{
		const TOptional<FString> CurrentNamespace = FTextInspector::GetNamespace(Previous);
		const TOptional<FString> CurrentKey = FTextInspector::GetKey(Previous);
		const FString* CurrentSource = FTextInspector::GetSourceString(Previous);
		const FString UseNamespace = bNamespaceGiven ? Namespace : TextNamespaceUtil::StripPackageNamespace(CurrentNamespace.IsSet() ? CurrentNamespace.GetValue() : FString());
		const FString UseKey = bKeyGiven ? Key : (CurrentKey.IsSet() && !CurrentKey.GetValue().IsEmpty() ? CurrentKey.GetValue() : FGuid::NewGuid().ToString());
		const FString UseSource = bSourceGiven ? SourceString : (CurrentSource ? *CurrentSource : Previous.ToString());
#if USE_STABLE_LOCALIZATION_KEYS
		// Text in an asset carries its package's namespace, as an edit in the details panel would give it.
		const FString FullNamespace = TextNamespaceUtil::BuildFullNamespace(UseNamespace, TextNamespaceUtil::EnsurePackageNamespace(Owner), true);
#else
		const FString FullNamespace = UseNamespace;
#endif
		Updated = FText::ChangeKey(FTextKey(FullNamespace), FTextKey(UseKey), FText::FromString(UseSource));
	}

	TSharedPtr<FJsonObject> UpdatedState = MakeShared<FJsonObject>();
	MCPLocDescribeText(UpdatedState, Updated);
	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetStringField(TEXT("propertyName"), PropertyName);
	if (!WidgetName.IsEmpty()) Result->SetStringField(TEXT("widgetName"), WidgetName);

	const TSharedRef<FJsonValueObject> BeforeValue = MakeShared<FJsonValueObject>(PreviousState);
	const TSharedRef<FJsonValueObject> AfterValue = MakeShared<FJsonValueObject>(UpdatedState);
	if (FJsonValue::CompareEqual(*BeforeValue, *AfterValue))
	{
		MCPSetExisted(Result);
		Result->SetBoolField(TEXT("unchanged"), true);
		MCPLocDescribeText(Result, Previous);
		return MCPResult(Result);
	}

	Owner->Modify();
	*Text = Updated;
	Owner->PostEditChange();

	FString SaveError;
	bool bSaved = false;
	if (UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset))
	{
		// The designer tree is the template; a compile carries the text into the generated class.
		FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBP);
		const MCPWidgetGuidMap::FSyncReport Report = MCPWidgetGuidMap::CompileChecked(WidgetBP);
		bSaved = Report.WasRefused() ? SaveAssetPackageChecked(WidgetBP, SaveError) : MCPWidgetGuidMap::SaveIfCompiled(WidgetBP, Report, SaveError);
		Result->SetStringField(TEXT("compileStatus"), WidgetCompileStatusString(WidgetBP));
	}
	else
	{
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
		bSaved = SaveAssetPackageChecked(Asset, SaveError);
	}

	MCPSetUpdated(Result);
	MCPNoteSaveOutcome(Result, AssetPath, bSaved, SaveError);
	MCPLocDescribeText(Result, *Text);
	Result->SetObjectField(TEXT("previous"), PreviousState);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("assetPath"), AssetPath);
	Payload->SetStringField(TEXT("propertyName"), PropertyName);
	if (!WidgetName.IsEmpty()) Payload->SetStringField(TEXT("widgetName"), WidgetName);
	bool bPreviousFromTable = false;
	PreviousState->TryGetBoolField(TEXT("isFromStringTable"), bPreviousFromTable);
	FString PreviousKey;
	PreviousState->TryGetStringField(TEXT("key"), PreviousKey);
	if (bPreviousFromTable)
	{
		Payload->SetStringField(TEXT("stringTableId"), PreviousState->GetStringField(TEXT("stringTableId")));
		Payload->SetStringField(TEXT("stringTableKey"), PreviousState->GetStringField(TEXT("stringTableKey")));
		MCPSetRollback(Result, TEXT("set_text_localization"), Payload);
	}
	else if (!PreviousKey.IsEmpty())
	{
		Payload->SetStringField(TEXT("namespace"), PreviousState->GetStringField(TEXT("namespace")));
		Payload->SetStringField(TEXT("key"), PreviousKey);
		Payload->SetStringField(TEXT("sourceString"), PreviousState->GetStringField(TEXT("sourceString")));
		MCPSetRollback(Result, TEXT("set_text_localization"), Payload);
	}
	else
	{
		MCPSetNoRollback(Result, TEXT("The previous value was culture-invariant text with no namespace or key, which set_text_localization cannot recreate. Set the old string again with asset or widget property actions."));
	}
	return MCPResult(Result);
}
