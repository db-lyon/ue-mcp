// Config-backed class defaults: the Project Settings pages, and the Maps & Modes
// page as typed actions (#1566).
//
// A settings object is the default object of a config class. Its package is
// compiled in, so saving a package never persists it: the value lives in an
// ini file. PersistConfigProperty writes the one property to the file the
// Project Settings editor would, then reads it back from that file, so a
// caller learns what is stored rather than what is in memory.

#include "EditorHandlers.h"

#include "HandlerUtils.h"
#include "HandlerCheckpoint.h"
#include "HandlerJsonProperty.h"

#include "GameFramework/GameModeBase.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"
#include "UObject/SoftObjectPath.h"

// A NAMED namespace: this module is a unity build, so an anonymous one here
// would merge with whatever other .cpp shares the blob.
namespace MCPProjectSettings
{
	const TCHAR* const MapsSettingsClassPath = TEXT("/Script/EngineSettings.GameMapsSettings");

	enum class EMapsField : uint8 { GameModeClass, GameInstanceClass, World };

	struct FMapsField
	{
		const TCHAR* Param;
		const TCHAR* Property;
		EMapsField Kind;
	};

	const FMapsField MapsFields[] = {
		{ TEXT("globalDefaultGameMode"), TEXT("GlobalDefaultGameMode"), EMapsField::GameModeClass },
		{ TEXT("globalDefaultServerGameMode"), TEXT("GlobalDefaultServerGameMode"), EMapsField::GameModeClass },
		{ TEXT("gameInstanceClass"), TEXT("GameInstanceClass"), EMapsField::GameInstanceClass },
		{ TEXT("gameDefaultMap"), TEXT("GameDefaultMap"), EMapsField::World },
		{ TEXT("serverDefaultMap"), TEXT("ServerDefaultMap"), EMapsField::World },
		{ TEXT("editorStartupMap"), TEXT("EditorStartupMap"), EMapsField::World },
		{ TEXT("transitionMap"), TEXT("TransitionMap"), EMapsField::World },
	};

	UObject* MapsSettingsDefault()
	{
		UClass* Class = FindObject<UClass>(nullptr, MapsSettingsClassPath);
		return Class ? Class->GetDefaultObject() : nullptr;
	}

	FString PathText(UObject* Owner, const TCHAR* PropertyName)
	{
		FProperty* Prop = Owner->GetClass()->FindPropertyByName(PropertyName);
		if (!Prop) return FString();
		FString Text;
		Prop->ExportText_InContainer(0, Text, Owner, Owner, Owner, PPF_None);
		return Text;
	}

	/** The soft path a parameter names, validated. Empty input clears the setting. */
	bool ResolveMapsValue(const FMapsField& Field, const FString& Input, FString& OutPath, FString& OutError)
	{
		const FString Trimmed = Input.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			OutPath.Reset();
			return true;
		}
		if (Field.Kind == EMapsField::World)
		{
			FString PackageName = Trimmed;
			int32 Dot = INDEX_NONE;
			if (PackageName.FindChar(TEXT('.'), Dot)) PackageName.LeftInline(Dot);
			if (!FPackageName::IsValidLongPackageName(PackageName) || !FPackageName::DoesPackageExist(PackageName))
			{
				OutError = FString::Printf(TEXT("%s: no map package at '%s'"), Field.Param, *Trimmed);
				return false;
			}
			OutPath = FString::Printf(TEXT("%s.%s"), *PackageName, *FPackageName::GetShortName(PackageName));
			return true;
		}
		UClass* Base = Field.Kind == EMapsField::GameModeClass
			? AGameModeBase::StaticClass()
			: UGameInstance::StaticClass();
		UClass* Class = MCPResolveClassOfType(Trimmed, Base);
		if (!Class)
		{
			OutError = FString::Printf(TEXT("%s: no class found for '%s'"), Field.Param, *Trimmed);
			return false;
		}
		if (!Class->IsChildOf(Base))
		{
			OutError = FString::Printf(TEXT("%s: %s does not derive from %s"), Field.Param, *Class->GetPathName(), *Base->GetName());
			return false;
		}
		OutPath = Class->GetPathName();
		return true;
	}

	/** Every typed field, from the default object and from the file. */
	TSharedPtr<FJsonObject> DescribeMaps(UObject* Settings, TSharedPtr<FJsonObject>& OutStored, FString& OutConfigFile)
	{
		TSharedPtr<FJsonObject> Values = MakeShared<FJsonObject>();
		OutStored = MakeShared<FJsonObject>();
		OutConfigFile = Settings->GetDefaultConfigFilename();
		FConfigFile File;
		const bool bHaveFile = FPaths::FileExists(OutConfigFile);
		if (bHaveFile) File.Read(OutConfigFile);
		const FString Section = Settings->GetClass()->GetPathName();
		for (const FMapsField& Field : MapsFields)
		{
			Values->SetStringField(Field.Param, PathText(Settings, Field.Property));
			FString Stored;
			if (bHaveFile && File.GetString(*Section, Field.Property, Stored))
			{
				OutStored->SetStringField(Field.Param, Stored);
			}
			else
			{
				OutStored->SetField(Field.Param, MakeShared<FJsonValueNull>());
			}
		}
		return Values;
	}
}

bool FEditorHandlers::PersistConfigProperty(UObject* Object, const FString& PropertyPath, const TSharedPtr<FJsonObject>& Result)
{
	if (!Object || !Object->HasAnyFlags(RF_ClassDefaultObject)) return false;
	UClass* Class = Object->GetClass();
	if (!Class->HasAnyClassFlags(CLASS_Config)) return false;

	// The ini holds whole top-level properties, so a dotted or indexed write
	// persists the property it starts at.
	FString Head = PropertyPath;
	int32 Cut = INDEX_NONE;
	if (Head.FindChar(TEXT('.'), Cut)) Head.LeftInline(Cut);
	if (Head.FindChar(TEXT('['), Cut)) Head.LeftInline(Cut);
	FProperty* Prop = MCPJsonProperty::FindMemberProperty(Class, Head);
	if (!Prop || !Prop->HasAnyPropertyFlags(CPF_Config)) return false;

	FString Key = Prop->GetName();
#if WITH_EDITOR
	const FString& CVarName = Prop->GetMetaData(TEXT("ConsoleVariable"));
	if (!CVarName.IsEmpty()) Key = CVarName;
#endif
	const FString Section = Class->GetPathName();
	const bool bDefaultConfig = Class->HasAnyClassFlags(CLASS_DefaultConfig);

	FString ConfigFile;
	if (bDefaultConfig)
	{
		ConfigFile = Object->GetDefaultConfigFilename();
		MCPCheckpointRecordBeforeWrite(ConfigFile);
		Object->UpdateSinglePropertyInConfigFile(Prop, ConfigFile);
	}
	else
	{
		// A per-user or per-machine settings class saves into the layered config
		// under Saved/, which is where the engine itself keeps it.
		ConfigFile = Class->GetConfigName();
		Object->SaveConfig();
		GConfig->Flush(false, ConfigFile);
	}

	// Read the file back, not GConfig: the question is what is on disk.
	FConfigFile File;
	const FString DiskFile = bDefaultConfig ? ConfigFile : FString();
	bool bStored = false;
	if (bDefaultConfig && FPaths::FileExists(DiskFile))
	{
		File.Read(DiskFile);
		if (CastField<FArrayProperty>(Prop))
		{
			TArray<FString> Lines;
			File.GetArray(*Section, *Key, Lines);
			TArray<TSharedPtr<FJsonValue>> Items;
			for (const FString& Line : Lines) Items.Add(MakeShared<FJsonValueString>(Line));
			Result->SetArrayField(TEXT("storedValue"), Items);
			bStored = true;
		}
		else
		{
			FString Stored;
			bStored = File.GetString(*Section, *Key, Stored);
			if (bStored) Result->SetStringField(TEXT("storedValue"), Stored);
		}
	}
	else if (!bDefaultConfig)
	{
		FString Stored;
		bStored = GConfig->GetString(*Section, *Key, Stored, ConfigFile);
		if (bStored) Result->SetStringField(TEXT("storedValue"), Stored);
	}

	Result->SetBoolField(TEXT("saved"), bStored);
	Result->SetBoolField(TEXT("persisted"), bStored);
	Result->SetStringField(TEXT("configFile"), FPaths::ConvertRelativePathToFull(ConfigFile));
	Result->SetStringField(TEXT("configSection"), Section);
	Result->SetStringField(TEXT("configKey"), Key);
	if (!bStored)
	{
		// The engine skips a value equal to the one an inherited layer already
		// holds, and a read-only file (source control) refuses the write.
		Result->SetStringField(TEXT("saveNote"), FString::Printf(
			TEXT("%s has no %s entry under [%s] after the write. Either the value matches an inherited config layer, so nothing needed writing, or the file is read-only. The in-memory value is set for this session."),
			*FPaths::GetCleanFilename(ConfigFile), *Key, *Section));
	}
	return true;
}

TSharedPtr<FJsonValue> FEditorHandlers::GetMapsSettings(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPProjectSettings;
	UObject* Settings = MapsSettingsDefault();
	if (!Settings) return MCPError(TEXT("GameMapsSettings is not loaded"));

	TSharedPtr<FJsonObject> Stored;
	FString ConfigFile;
	TSharedPtr<FJsonObject> Values = DescribeMaps(Settings, Stored, ConfigFile);

	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("settings"), Values);
	Result->SetObjectField(TEXT("stored"), Stored);
	Result->SetStringField(TEXT("configFile"), FPaths::ConvertRelativePathToFull(ConfigFile));
	Result->SetStringField(TEXT("configSection"), Settings->GetClass()->GetPathName());
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FEditorHandlers::SetMapsSettings(const TSharedPtr<FJsonObject>& Params)
{
	using namespace MCPProjectSettings;
	UObject* Settings = MapsSettingsDefault();
	if (!Settings) return MCPError(TEXT("GameMapsSettings is not loaded"));

	// Every value is validated before anything is written, so a bad map path
	// never leaves the game mode half applied.
	TMap<FString, FString> Given;
	{
		FString V;
		if (TryGetStringParam(Params, TEXT("globalDefaultGameMode"), V)) Given.Add(TEXT("globalDefaultGameMode"), V);
		if (TryGetStringParam(Params, TEXT("globalDefaultServerGameMode"), V)) Given.Add(TEXT("globalDefaultServerGameMode"), V);
		if (TryGetStringParam(Params, TEXT("gameInstanceClass"), V)) Given.Add(TEXT("gameInstanceClass"), V);
		if (TryGetStringParam(Params, TEXT("gameDefaultMap"), V)) Given.Add(TEXT("gameDefaultMap"), V);
		if (TryGetStringParam(Params, TEXT("serverDefaultMap"), V)) Given.Add(TEXT("serverDefaultMap"), V);
		if (TryGetStringParam(Params, TEXT("editorStartupMap"), V)) Given.Add(TEXT("editorStartupMap"), V);
		if (TryGetStringParam(Params, TEXT("transitionMap"), V)) Given.Add(TEXT("transitionMap"), V);
	}

	struct FPending { const FMapsField* Field; FProperty* Prop; FString Path; FString Previous; };
	TArray<FPending> Pending;
	TArray<FString> Errors;
	for (const FMapsField& Field : MapsFields)
	{
		const FString* Found = Given.Find(Field.Param);
		if (!Found) continue;
		const FString& Input = *Found;
		FProperty* Prop = Settings->GetClass()->FindPropertyByName(Field.Property);
		if (!Prop)
		{
			Errors.Add(FString::Printf(TEXT("%s: GameMapsSettings has no %s property in this engine"), Field.Param, Field.Property));
			continue;
		}
		FString Path, Err;
		if (!ResolveMapsValue(Field, Input, Path, Err))
		{
			Errors.Add(Err);
			continue;
		}
		Pending.Add({ &Field, Prop, Path, PathText(Settings, Field.Property) });
	}
	if (Errors.Num() > 0) return MCPError(FString::Join(Errors, TEXT("; ")));
	if (Pending.Num() == 0)
	{
		return MCPError(TEXT("Pass at least one of globalDefaultGameMode, globalDefaultServerGameMode, gameInstanceClass, gameDefaultMap, serverDefaultMap, editorStartupMap, transitionMap"));
	}

	TArray<TSharedPtr<FJsonValue>> Changed;
	TSharedPtr<FJsonObject> Writes = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> RollbackPayload = MakeShared<FJsonObject>();
	bool bAllPersisted = true;
	for (const FPending& P : Pending)
	{
		if (P.Previous == P.Path) continue;
		// Every typed field is an FSoftObjectPath or its FSoftClassPath subclass.
		FSoftObjectPath* Value = P.Prop->ContainerPtrToValuePtr<FSoftObjectPath>(Settings);

		Settings->Modify();
		Value->SetPath(P.Path);
		FPropertyChangedEvent ChangeEvent(P.Prop);
		Settings->PostEditChangeProperty(ChangeEvent);

		TSharedPtr<FJsonObject> Write = MakeShared<FJsonObject>();
		PersistConfigProperty(Settings, P.Field->Property, Write);
		bAllPersisted &= Write->GetBoolField(TEXT("persisted"));
		Write->SetStringField(TEXT("previousValue"), P.Previous);
		Write->SetStringField(TEXT("value"), PathText(Settings, P.Field->Property));
		Writes->SetObjectField(P.Field->Param, Write);
		Changed.Add(MakeShared<FJsonValueString>(P.Field->Param));
		RollbackPayload->SetStringField(P.Field->Param, P.Previous);
	}

	TSharedPtr<FJsonObject> Stored;
	FString ConfigFile;
	TSharedPtr<FJsonObject> Values = DescribeMaps(Settings, Stored, ConfigFile);

	auto Result = MCPSuccess();
	Result->SetObjectField(TEXT("settings"), Values);
	Result->SetObjectField(TEXT("stored"), Stored);
	Result->SetStringField(TEXT("configFile"), FPaths::ConvertRelativePathToFull(ConfigFile));
	Result->SetArrayField(TEXT("changed"), Changed);
	if (Changed.Num() == 0)
	{
		MCPSetExisted(Result);
		return MCPResult(Result);
	}
	Result->SetObjectField(TEXT("writes"), Writes);
	Result->SetBoolField(TEXT("persisted"), bAllPersisted);
	MCPSetUpdated(Result);
	MCPSetRollback(Result, TEXT("set_maps_settings"), RollbackPayload);
	return MCPResult(Result);
}
