// project(set_developer_setting): write a config property on a settings class
// default object and save it the way the Project Settings dialog does (#1651).
// A translation-unit partition of FProjectHandlers, registered in
// ProjectHandlers.cpp::RegisterHandlers.

#include "ProjectHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "HandlerJsonProperty.h"
#include "HandlerCheckpoint.h"
#include "HandlerSourceControl.h"
#include "JsonSerializer.h"

#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace
{
	/** Where a persist mode writes, decided as FSettingsSection::Save and
	 *  SSettingsEditor::NotifyPostChange decide it. */
	enum class ESettingsPersist : uint8 { Default, User };

	FProperty* FindSettingsTopLevelProperty(UClass* Class, const FString& PropertyPath)
	{
		int32 Cut = PropertyPath.Len();
		int32 Dot = INDEX_NONE;
		int32 Bracket = INDEX_NONE;
		if (PropertyPath.FindChar(TEXT('.'), Dot)) Cut = FMath::Min(Cut, Dot);
		if (PropertyPath.FindChar(TEXT('['), Bracket)) Cut = FMath::Min(Cut, Bracket);
		const FString Leading = PropertyPath.Left(Cut);

		FProperty* Loose = nullptr;
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			if (It->GetName().Equals(Leading, ESearchCase::CaseSensitive)) return *It;
			if (!Loose && It->GetName().Equals(Leading, ESearchCase::IgnoreCase)) Loose = *It;
		}
		return Loose;
	}

	TArray<FString> SettingsConfigPropertyNames(UClass* Class)
	{
		TArray<FString> Names;
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It && Names.Num() < 60; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Config)) Names.Add(It->GetName());
		}
		return Names;
	}

	/** The key SaveConfig writes the property under: its console variable when
	 *  it is bound to one, otherwise its name. */
	FString SettingsConfigKeyFor(const FProperty* Prop)
	{
		const FString& CVarName = Prop->GetMetaData(TEXT("ConsoleVariable"));
		return CVarName.IsEmpty() ? Prop->GetName() : CVarName;
	}

	/** The lines of Section in the file on disk whose key is Key, array
	 *  operators (+ - . ! @ *) included, as the file holds them. */
	TArray<FString> ReadSettingsIniLines(const FString& FullPath, const FString& Section, const FString& Key)
	{
		TArray<FString> Matches;
		FString Contents;
		if (!FFileHelper::LoadFileToString(Contents, *FullPath)) return Matches;

		TArray<FString> Lines;
		Contents.ParseIntoArrayLines(Lines, /*CullEmpty*/ false);
		const FString Header = FString::Printf(TEXT("[%s]"), *Section);
		bool bInSection = false;
		for (const FString& Raw : Lines)
		{
			const FString Line = Raw.TrimStartAndEnd();
			if (Line.StartsWith(TEXT("[")) && Line.EndsWith(TEXT("]")))
			{
				bInSection = Line.Equals(Header, ESearchCase::IgnoreCase);
				continue;
			}
			if (!bInSection || Line.IsEmpty() || Line.StartsWith(TEXT(";"))) continue;

			int32 Eq = INDEX_NONE;
			if (!Line.FindChar(TEXT('='), Eq)) continue;
			FString LineKey = Line.Left(Eq).TrimStartAndEnd();
			while (LineKey.Len() > 0 && FCString::Strchr(TEXT("+-.!@*"), LineKey[0]))
			{
				LineKey.RightChopInline(1);
			}
			if (LineKey.Equals(Key, ESearchCase::IgnoreCase)) Matches.Add(Line);
		}
		return Matches;
	}

	TArray<TSharedPtr<FJsonValue>> SettingsIniLinesToJson(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& Value : Values) Out.Add(MakeShared<FJsonValueString>(Value));
		return Out;
	}
}

TSharedPtr<FJsonValue> FProjectHandlers::SetDeveloperSetting(const TSharedPtr<FJsonObject>& Params)
{
	// Every parameter is read before anything can fail (#1057).
	FString ClassSpec;
	TSharedPtr<FJsonValue> ClassErr = RequireString(Params, TEXT("settingsClass"), ClassSpec);
	FString PropertyPath;
	TSharedPtr<FJsonValue> PropertyErr = RequireString(Params, TEXT("propertyName"), PropertyPath);
	TSharedPtr<FJsonValue> NewValue = TryGetParam(Params, TEXT("settingValue"));
	const FString PersistParam = OptionalString(Params, TEXT("persist")).ToLower();

	if (ClassErr) return ClassErr;
	if (PropertyErr) return PropertyErr;
	if (!NewValue.IsValid()) return MCPError(TEXT("Missing 'settingValue' parameter"));
	if (!PersistParam.IsEmpty() && PersistParam != TEXT("default") && PersistParam != TEXT("user"))
	{
		return MCPError(FString::Printf(TEXT("persist must be default or user, got '%s'"), *PersistParam));
	}

	UClass* Class = MCPResolveClass(ClassSpec);
	if (!Class) return MCPClassNotFoundError(ClassSpec, TEXT("settingsClass"));
	if (!Class->HasAnyClassFlags(CLASS_Config))
	{
		return MCPError(FString::Printf(
			TEXT("%s is not a config class (no config= in its UCLASS), so it has no ini section to save to. Use editor(set_property) for an asset or editor(set_object_property) for a live object."),
			*Class->GetPathName()));
	}
	if (Class->HasAnyClassFlags(CLASS_PerObjectConfig))
	{
		return MCPError(FString::Printf(
			TEXT("%s is PerObjectConfig: each instance saves to its own section, so its class default object is not what any section holds."),
			*Class->GetPathName()));
	}
	if (Class->HasAnyClassFlags(CLASS_Abstract))
	{
		return MCPError(FString::Printf(
			TEXT("%s is abstract. Name the concrete settings class that derives from it."), *Class->GetPathName()));
	}

	UObject* Settings = Class->GetDefaultObject();
	if (!Settings) return MCPError(FString::Printf(TEXT("%s has no class default object"), *Class->GetPathName()));

	FProperty* TopProp = FindSettingsTopLevelProperty(Class, PropertyPath);
	if (!TopProp || !TopProp->HasAnyPropertyFlags(CPF_Config))
	{
		return MCPError(FString::Printf(
			TEXT("%s on %s. Only UPROPERTY(Config) properties are saved to ini. Config properties: [%s]"),
			TopProp ? *FString::Printf(TEXT("'%s' is not a Config property"), *TopProp->GetName())
					: *FString::Printf(TEXT("No property '%s'"), *PropertyPath),
			*Class->GetName(),
			*FString::Join(SettingsConfigPropertyNames(Class), TEXT(", "))));
	}

	// Canonical spelling of the leading token, so a case slip still resolves.
	FString ResolvedPath = PropertyPath;
	{
		int32 Cut = PropertyPath.Len();
		int32 Idx = INDEX_NONE;
		if (PropertyPath.FindChar(TEXT('.'), Idx)) Cut = FMath::Min(Cut, Idx);
		if (PropertyPath.FindChar(TEXT('['), Idx)) Cut = FMath::Min(Cut, Idx);
		ResolvedPath = TopProp->GetName() + PropertyPath.Mid(Cut);
	}

	FProperty* LeafProp = nullptr;
	void* ValueAddr = nullptr;
	UObject* LeafOwner = nullptr;
	FString ResolveError;
	if (!MCPJsonProperty::ResolveDottedPath(Settings, ResolvedPath, LeafProp, ValueAddr, LeafOwner, ResolveError))
	{
		return MCPError(FString::Printf(TEXT("%s on %s"), *ResolveError, *Class->GetName()));
	}

	const ESettingsPersist Persist = PersistParam.IsEmpty()
		? (Class->HasAnyClassFlags(CLASS_DefaultConfig) ? ESettingsPersist::Default : ESettingsPersist::User)
		: (PersistParam == TEXT("default") ? ESettingsPersist::Default : ESettingsPersist::User);

	// The file the write lands in, named before anything changes so a
	// read-only file refuses the call instead of leaving the default object
	// edited and the ini stale.
	FString TargetFile;
	FString TargetKind;
	if (Persist == ESettingsPersist::Default)
	{
		TargetFile = Settings->GetDefaultConfigFilename();
		TargetKind = TEXT("default");
	}
	else if (Class->HasAnyClassFlags(CLASS_GlobalUserConfig))
	{
		TargetFile = Settings->GetGlobalUserConfigFilename();
		TargetKind = TEXT("globalUser");
	}
	else if (Class->HasAnyClassFlags(CLASS_ProjectUserConfig))
	{
		TargetFile = Settings->GetProjectUserConfigFilename();
		TargetKind = TEXT("projectUser");
	}
	else
	{
		TargetFile = Class->GetConfigName();
		TargetKind = TEXT("saved");
	}
	const FString FullPath = FPaths::ConvertRelativePathToFull(TargetFile);

	if (FPaths::FileExists(FullPath) && IFileManager::Get().IsReadOnly(*FullPath))
	{
		FString CheckoutNote;
		MCPSourceControl::TryAutoCheckout(FullPath, CheckoutNote);
		if (IFileManager::Get().IsReadOnly(*FullPath))
		{
			return MCPError(FString::Printf(
				TEXT("%s is read-only, so the setting cannot be saved. %s"),
				*FullPath,
				CheckoutNote.IsEmpty()
					? TEXT("Check it out, or turn on source_control(set_checkout_mode, mode=auto).")
					: *CheckoutNote));
		}
	}

	const FString Section = Class->GetPathName();
	const FString IniKey = SettingsConfigKeyFor(TopProp);
	const TArray<FString> LinesBefore = ReadSettingsIniLines(FullPath, Section, IniKey);

	TSharedPtr<FJsonValue> PreviousStructured = FMCPJsonSerializer::SerializeValue(ValueAddr, LeafProp);
	const bool bComparable = MCPJsonProperty::PropertyIsComparableByIdentity(LeafProp);
	FDefaultConstructedPropertyElement PreviousRaw(LeafProp);
	LeafProp->CopyCompleteValue(PreviousRaw.GetObjAddress(), ValueAddr);

	FString SetError;
	if (!MCPJsonProperty::SetJsonOnPropertyWithOwner(LeafProp, ValueAddr, NewValue, LeafOwner ? LeafOwner : Settings, SetError))
	{
		LeafProp->CopyCompleteValue(ValueAddr, PreviousRaw.GetObjAddress());
		return MCPError(FString::Printf(TEXT("Failed to set '%s': %s"), *ResolvedPath, *SetError));
	}

	// The settings object's own change handling runs first, as it does when the
	// dialog edits it: a UDeveloperSettings broadcasts SettingsChanged here.
	FPropertyChangedEvent ChangeEvent(LeafProp, EPropertyChangeType::ValueSet);
	ChangeEvent.MemberProperty = TopProp;
	Settings->PostEditChangeProperty(ChangeEvent);

	MCPCheckpointRecordBeforeWrite(FullPath);

	FString SaveMethod;
	if (Persist == ESettingsPersist::Default)
	{
		// The dialog writes one property, except a container while the old
		// property saving is on, which UpdateSinglePropertyInConfigFile cannot
		// write, so it saves the whole section instead.
		static IConsoleVariable* NewPropertySaving = IConsoleManager::Get().FindConsoleVariable(TEXT("ini.UseNewPropertySaving"));
		const bool bNewSaving = NewPropertySaving && NewPropertySaving->GetInt() != 0;
		const bool bContainer = TopProp->IsA<FArrayProperty>() || TopProp->IsA<FSetProperty>()
			|| TopProp->IsA<FMapProperty>() || TopProp->ArrayDim > 1;
		if (bContainer && !bNewSaving)
		{
			if (!Settings->TryUpdateDefaultConfigFile(TargetFile))
			{
				return MCPError(FString::Printf(TEXT("%s could not be written; the value is set in memory only"), *FullPath));
			}
			SaveMethod = TEXT("TryUpdateDefaultConfigFile");
		}
		else
		{
			Settings->UpdateSinglePropertyInConfigFile(TopProp, TargetFile);
			SaveMethod = TEXT("UpdateSinglePropertyInConfigFile");
		}
	}
	else if (TargetKind == TEXT("globalUser"))
	{
		Settings->UpdateGlobalUserConfigFile();
		SaveMethod = TEXT("UpdateGlobalUserConfigFile");
	}
	else if (TargetKind == TEXT("projectUser"))
	{
		Settings->UpdateProjectUserConfigFile();
		SaveMethod = TEXT("UpdateProjectUserConfigFile");
	}
	else
	{
		Settings->SaveConfig();
		// SaveConfig updates the config branch; the file on disk is written when the branch is flushed.
		if (GConfig) GConfig->Flush(/*bRemoveFromCache*/ false, Settings->GetClass()->GetConfigName());
		SaveMethod = TEXT("SaveConfig");
	}

	const TArray<FString> LinesAfter = ReadSettingsIniLines(FullPath, Section, IniKey);
	const bool bValueChanged = !bComparable || !LeafProp->Identical(ValueAddr, PreviousRaw.GetObjAddress(), PPF_None);
	const bool bFileChanged = !(LinesBefore == LinesAfter);

	auto Result = MCPSuccess();
	if (bValueChanged || bFileChanged) MCPSetUpdated(Result);
	else MCPSetExisted(Result);
	Result->SetStringField(TEXT("settingsClass"), Class->GetPathName());
	Result->SetStringField(TEXT("propertyName"), ResolvedPath);
	Result->SetStringField(TEXT("type"), LeafProp->GetCPPType());
	Result->SetField(TEXT("previousValue"), PreviousStructured);
	Result->SetField(TEXT("value"), FMCPJsonSerializer::SerializeValue(ValueAddr, LeafProp));
	Result->SetBoolField(TEXT("changed"), bValueChanged);
	Result->SetStringField(TEXT("persist"), Persist == ESettingsPersist::Default ? TEXT("default") : TEXT("user"));
	Result->SetStringField(TEXT("configKind"), TargetKind);
	Result->SetStringField(TEXT("configFile"), FullPath);
	Result->SetStringField(TEXT("section"), Section);
	Result->SetStringField(TEXT("key"), IniKey);
	Result->SetStringField(TEXT("saveMethod"), SaveMethod);
	Result->SetArrayField(TEXT("iniLines"), SettingsIniLinesToJson(LinesAfter));
	Result->SetBoolField(TEXT("persisted"), true);
	if (LinesAfter.Num() == 0)
	{
		Result->SetStringField(TEXT("iniNote"), TEXT("The file holds no line for this key: the value equals what the config hierarchy below this file already gives, so the save writes only the difference and that is none."));
	}

	const bool bRestart = TopProp->GetBoolMetaData(TEXT("ConfigRestartRequired"))
		|| LeafProp->GetBoolMetaData(TEXT("ConfigRestartRequired"));
	Result->SetBoolField(TEXT("restartRequired"), bRestart);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("settingsClass"), Class->GetPathName());
	Payload->SetStringField(TEXT("propertyName"), ResolvedPath);
	Payload->SetField(TEXT("settingValue"), PreviousStructured);
	Payload->SetStringField(TEXT("persist"), Persist == ESettingsPersist::Default ? TEXT("default") : TEXT("user"));
	MCPSetRollback(Result, TEXT("set_developer_setting"), Payload);

	return MCPResult(Result);
}
