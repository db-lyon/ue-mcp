// project(set_developer_setting) sets a config property on a settings class
// default object and saves it to the class's ini as the Project Settings
// dialog does (#1651). Each test reads the file back from disk.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/MCPDeveloperSettingTestTypes.h"

#include "HandlerRegistry.h"
#include "Handlers/Project/ProjectHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigContext.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const TCHAR* SettingTestClassPath()
	{
		return TEXT("/Script/UE_MCP_Bridge.UEMCPDeveloperSettingTestObject");
	}

	FString SettingTestDefaultIni()
	{
		return FPaths::ConvertRelativePathToFull(
			GetDefault<UUEMCPDeveloperSettingTestObject>()->GetDefaultConfigFilename());
	}

	/** Restores the default object and removes the test's own default ini. */
	struct FSettingTestScope
	{
		FSettingTestScope() { Reset(); }
		~FSettingTestScope() { Reset(); }

		static void Reset()
		{
			UUEMCPDeveloperSettingTestObject* Cdo = GetMutableDefault<UUEMCPDeveloperSettingTestObject>();
			Cdo->Count = 0;
			Cdo->RuntimeClass.Reset();
			Cdo->Entries.Reset();
			Cdo->NotConfig = 0;
			IFileManager::Get().Delete(*SettingTestDefaultIni(), false, true, true);
		}
	};

	TSharedPtr<FJsonObject> CallSetDeveloperSetting(
		FMCPHandlerRegistry& Registry,
		const FString& SettingsClass,
		const FString& PropertyName,
		const TSharedPtr<FJsonValue>& Value,
		const FString& Persist = FString())
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("settingsClass"), SettingsClass);
		Request->SetStringField(TEXT("propertyName"), PropertyName);
		Request->SetField(TEXT("settingValue"), Value);
		if (!Persist.IsEmpty()) Request->SetStringField(TEXT("persist"), Persist);
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("set_developer_setting"), Request);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
	}

	bool SettingCallSucceeded(const TSharedPtr<FJsonObject>& Response)
	{
		bool bSuccess = false;
		return Response.IsValid() && Response->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	FString ReadFileOrEmpty(const FString& Path)
	{
		FString Contents;
		FFileHelper::LoadFileToString(Contents, *Path);
		return Contents;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPSetDeveloperSettingDefaultIniTest,
	"UE.MCP.Project.SetDeveloperSetting.WritesDefaultIni",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSetDeveloperSettingDefaultIniTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FProjectHandlers::RegisterHandlers(Registry);
	FSettingTestScope Scope;

	const TSharedPtr<FJsonObject> Response = CallSetDeveloperSetting(
		Registry, TEXT("UEMCPDeveloperSettingTestObject"), TEXT("Count"), MakeShared<FJsonValueNumber>(42));
	if (!TestTrue(TEXT("set_developer_setting succeeds"), SettingCallSucceeded(Response))) return false;

	TestEqual(TEXT("the default object holds the value"), GetDefault<UUEMCPDeveloperSettingTestObject>()->Count, 42);
	TestEqual(TEXT("omitted persist follows defaultconfig"), Response->GetStringField(TEXT("persist")), FString(TEXT("default")));
	TestEqual(TEXT("the class's default ini is reported"),
		FPaths::ConvertRelativePathToFull(Response->GetStringField(TEXT("configFile"))), SettingTestDefaultIni());
	TestEqual(TEXT("the section is the class path"), Response->GetStringField(TEXT("section")), FString(SettingTestClassPath()));

	const FString Ini = ReadFileOrEmpty(SettingTestDefaultIni());
	TestTrue(TEXT("the ini holds the section"), Ini.Contains(FString::Printf(TEXT("[%s]"), SettingTestClassPath())));
	TestTrue(TEXT("the ini holds the key and value"), Ini.Contains(TEXT("Count=42")));

	const TSharedPtr<FJsonObject>* Rollback = nullptr;
	TestTrue(TEXT("a rollback is offered"), Response->TryGetObjectField(TEXT("rollback"), Rollback));
	if (Rollback)
	{
		TestEqual(TEXT("rollback is the same action"), (*Rollback)->GetStringField(TEXT("method")), FString(TEXT("set_developer_setting")));
		const TSharedPtr<FJsonObject> Payload = (*Rollback)->GetObjectField(TEXT("payload"));
		const TSharedPtr<FJsonObject> Undo = CallSetDeveloperSetting(
			Registry, Payload->GetStringField(TEXT("settingsClass")), Payload->GetStringField(TEXT("propertyName")),
			Payload->TryGetField(TEXT("settingValue")), Payload->GetStringField(TEXT("persist")));
		TestTrue(TEXT("rollback applies"), SettingCallSucceeded(Undo));
		TestEqual(TEXT("rollback restores the default object"), GetDefault<UUEMCPDeveloperSettingTestObject>()->Count, 0);
		TestFalse(TEXT("rollback restores the ini"), ReadFileOrEmpty(SettingTestDefaultIni()).Contains(TEXT("Count=42")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPSetDeveloperSettingClassRefTest,
	"UE.MCP.Project.SetDeveloperSetting.ClassReferenceAndArray",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSetDeveloperSettingClassRefTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FProjectHandlers::RegisterHandlers(Registry);
	FSettingTestScope Scope;

	const TSharedPtr<FJsonObject> ClassRef = CallSetDeveloperSetting(
		Registry, SettingTestClassPath(), TEXT("RuntimeClass"), MakeShared<FJsonValueString>(TEXT("/Script/Engine.Actor")));
	if (!TestTrue(TEXT("class reference write succeeds"), SettingCallSucceeded(ClassRef))) return false;
	TestEqual(TEXT("the default object holds the class"),
		GetDefault<UUEMCPDeveloperSettingTestObject>()->RuntimeClass.ToSoftObjectPath().ToString(), FString(TEXT("/Script/Engine.Actor")));
	TestTrue(TEXT("the ini holds the class path"),
		ReadFileOrEmpty(SettingTestDefaultIni()).Contains(TEXT("RuntimeClass=/Script/Engine.Actor")));

	TArray<TSharedPtr<FJsonValue>> Entries;
	Entries.Add(MakeShared<FJsonValueString>(TEXT("first")));
	Entries.Add(MakeShared<FJsonValueString>(TEXT("second")));
	const TSharedPtr<FJsonObject> ArrayWrite = CallSetDeveloperSetting(
		Registry, SettingTestClassPath(), TEXT("Entries"), MakeShared<FJsonValueArray>(Entries));
	if (!TestTrue(TEXT("array write succeeds"), SettingCallSucceeded(ArrayWrite))) return false;
	TestEqual(TEXT("the default object holds both entries"), GetDefault<UUEMCPDeveloperSettingTestObject>()->Entries.Num(), 2);
	const FString Ini = ReadFileOrEmpty(SettingTestDefaultIni());
	TestTrue(TEXT("the ini holds the first entry"), Ini.Contains(TEXT("first")));
	TestTrue(TEXT("the ini holds the second entry"), Ini.Contains(TEXT("second")));
	TestTrue(TEXT("the earlier class reference is still saved"), Ini.Contains(TEXT("RuntimeClass=/Script/Engine.Actor")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPSetDeveloperSettingRefusalTest,
	"UE.MCP.Project.SetDeveloperSetting.RefusesBeforeWriting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSetDeveloperSettingRefusalTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FProjectHandlers::RegisterHandlers(Registry);
	FSettingTestScope Scope;

	const TSharedPtr<FJsonObject> NotConfig = CallSetDeveloperSetting(
		Registry, SettingTestClassPath(), TEXT("NotConfig"), MakeShared<FJsonValueNumber>(7));
	TestFalse(TEXT("a non-Config property is refused"), SettingCallSucceeded(NotConfig));
	TestEqual(TEXT("the refused property is untouched"), GetDefault<UUEMCPDeveloperSettingTestObject>()->NotConfig, 0);
	TestFalse(TEXT("a refusal writes no ini"), FPaths::FileExists(SettingTestDefaultIni()));
	TestTrue(TEXT("the refusal names the Config properties"),
		NotConfig.IsValid() && NotConfig->GetStringField(TEXT("error")).Contains(TEXT("Count")));

	const TSharedPtr<FJsonObject> NotConfigClass = CallSetDeveloperSetting(
		Registry, TEXT("/Script/CoreUObject.Package"), TEXT("PackageFlags"), MakeShared<FJsonValueNumber>(0));
	TestFalse(TEXT("a class without config= is refused"), SettingCallSucceeded(NotConfigClass));
	TestTrue(TEXT("the refusal says the class has no config"),
		NotConfigClass.IsValid() && NotConfigClass->GetStringField(TEXT("error")).Contains(TEXT("not a config class")));

	const TSharedPtr<FJsonObject> BadPersist = CallSetDeveloperSetting(
		Registry, SettingTestClassPath(), TEXT("Count"), MakeShared<FJsonValueNumber>(3), TEXT("elsewhere"));
	TestFalse(TEXT("an unknown persist mode is refused"), SettingCallSucceeded(BadPersist));
	TestEqual(TEXT("the refused write leaves the value"), GetDefault<UUEMCPDeveloperSettingTestObject>()->Count, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPSetDeveloperSettingUserTest,
	"UE.MCP.Project.SetDeveloperSetting.PersistUser",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPSetDeveloperSettingUserTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FProjectHandlers::RegisterHandlers(Registry);
	FSettingTestScope Scope;

	const TSharedPtr<FJsonObject> Response = CallSetDeveloperSetting(
		Registry, SettingTestClassPath(), TEXT("Count"), MakeShared<FJsonValueNumber>(9), TEXT("user"));
	if (!TestTrue(TEXT("persist=user succeeds"), SettingCallSucceeded(Response))) return false;
	const FString UserIni = FPaths::ConvertRelativePathToFull(
		GetDefault<UUEMCPDeveloperSettingTestObject>()->GetProjectUserConfigFilename());
	TestEqual(TEXT("a defaultconfig class persists user values to the project user layer"),
		Response->GetStringField(TEXT("configKind")), FString(TEXT("projectUser")));
	TestEqual(TEXT("the project user ini is reported"),
		FPaths::ConvertRelativePathToFull(Response->GetStringField(TEXT("configFile"))), UserIni);
	TestFalse(TEXT("user does not write the default ini"), FPaths::FileExists(SettingTestDefaultIni()));
	TestTrue(TEXT("the user config file holds the value"), ReadFileOrEmpty(UserIni).Contains(TEXT("Count=9")));

	// Remove the user layer and reload the branch so later runs start clean.
	FSettingTestScope::Reset();
	IFileManager::Get().Delete(*UserIni, false, true, true);
	FConfigContext::ForceReloadIntoGConfig().Load(TEXT("UEMCPSettingTest"));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
