// #1097/#1109: exercise real package writes, then evict and reload the IMC.
// Everything lives under a private mount in the system temp directory.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/GameplayHandlers.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "InputTriggers.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "Tests/MCPScopedTestMount.h"

namespace ImcPersistenceTests
{
	struct FFixture
	{
		FAutomationTestBase& Test;
		// Declared before everything it owns, so it is destroyed after them.
		FMCPScopedTestMount Mount;
		FString RootPath;
		FString ContextPath;
		FString ContextFile;
		UInputAction* ActionA = nullptr;
		UInputAction* ActionB = nullptr;
		UInputMappingContext* Context = nullptr;
		FMCPHandlerRegistry Registry;
		bool bReady = false;

		explicit FFixture(FAutomationTestBase& InTest)
			: Test(InTest)
			, Mount(TEXT("/UEMCPImcPersistence_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPImcPersistence"))
			, RootPath(Mount.RootPath)
		{
			ActionA = NewObject<UInputAction>(CreatePackage(*(RootPath + TEXT("IA_A"))), TEXT("IA_A"), RF_Public | RF_Standalone);
			ActionB = NewObject<UInputAction>(CreatePackage(*(RootPath + TEXT("IA_B"))), TEXT("IA_B"), RF_Public | RF_Standalone);
			ActionA->AddToRoot();
			ActionB->AddToRoot();
			Context = NewObject<UInputMappingContext>(CreatePackage(*(RootPath + TEXT("IMC_Test"))), TEXT("IMC_Test"), RF_Public | RF_Standalone);
			ContextPath = Context->GetPathName();
			ResolvePackageFileName(Context->GetOutermost(), ContextFile);
			FGameplayHandlers::RegisterHandlers(Registry);

			FString Reason;
			bReady = Test.TestTrue(TEXT("first input action saves"), SaveAssetPackageChecked(ActionA, Reason));
			bReady &= Test.TestTrue(TEXT("second input action saves"), SaveAssetPackageChecked(ActionB, Reason));
			bReady &= Test.TestTrue(TEXT("empty context saves as a disk baseline"), SaveAssetPackageChecked(Context, Reason));
		}

		~FFixture()
		{
			FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*ContextFile, false);
			if (!Context) Context = FindObject<UInputMappingContext>(nullptr, *ContextPath);
			for (UObject* Asset : { static_cast<UObject*>(ActionA), static_cast<UObject*>(ActionB), static_cast<UObject*>(Context) })
			{
				if (!Asset) continue;
				if (Asset->IsRooted()) Asset->RemoveFromRoot();
				Asset->ClearFlags(RF_Standalone);
				Asset->GetOutermost()->SetDirtyFlag(false);
				ResetLoaders(Asset->GetOutermost());
			}
		}

		TSharedPtr<FJsonObject> Params() const
		{
			auto Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("imcPath"), ContextPath);
			return Result;
		}

		TSharedPtr<FJsonObject> AddParams() const
		{
			auto Result = Params();
			Result->SetStringField(TEXT("inputActionPath"), ActionA->GetPathName());
			Result->SetStringField(TEXT("key"), TEXT("SpaceBar"));
			return Result;
		}

		TSharedPtr<FJsonObject> ModifierParams() const
		{
			auto Result = Params();
			Result->SetNumberField(TEXT("mappingIndex"), 0);
			auto Negate = MakeShared<FJsonObject>();
			Negate->SetStringField(TEXT("class"), UInputModifierNegate::StaticClass()->GetPathName());
			Negate->SetBoolField(TEXT("bX"), false);
			Result->SetArrayField(TEXT("modifiers"), { MakeShared<FJsonValueObject>(Negate) });
			auto Hold = MakeShared<FJsonObject>();
			Hold->SetStringField(TEXT("class"), UInputTriggerHold::StaticClass()->GetPathName());
			Hold->SetNumberField(TEXT("HoldTimeThreshold"), 0.75);
			Result->SetArrayField(TEXT("triggers"), { MakeShared<FJsonValueObject>(Hold) });
			return Result;
		}

		TSharedPtr<FJsonObject> Run(const TCHAR* Method, const TSharedPtr<FJsonObject>& Arguments)
		{
			const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Arguments);
			if (!Test.TestTrue(FString::Printf(TEXT("%s returned an object"), Method), Response.IsValid() && Response->Type == EJson::Object))
			{
				return MakeShared<FJsonObject>();
			}
			return Response->AsObject();
		}

		bool Field(const TSharedPtr<FJsonObject>& Result, const TCHAR* Name, bool Expected)
		{
			bool Value = !Expected;
			const bool bPresent = Result->TryGetBoolField(Name, Value);
			return Test.TestTrue(FString::Printf(TEXT("%s is present and %s"), Name, Expected ? TEXT("true") : TEXT("false")), bPresent && Value == Expected);
		}

		bool Saved(const TSharedPtr<FJsonObject>& Result)
		{
			bool bOK = Field(Result, TEXT("success"), true);
			bOK &= Field(Result, TEXT("saved"), true);
			bOK &= Field(Result, TEXT("persisted"), true);
			bOK &= Field(Result, TEXT("packageDirty"), false);
			bOK &= Test.TestFalse(TEXT("saved package is clean"), Context->GetOutermost()->IsDirty());
			return bOK;
		}

		bool Deferred(const TSharedPtr<FJsonObject>& Result)
		{
			bool bOK = Field(Result, TEXT("success"), true);
			bOK &= Field(Result, TEXT("saved"), false);
			bOK &= Field(Result, TEXT("persisted"), false);
			bOK &= Field(Result, TEXT("packageDirty"), true);
			bOK &= Test.TestTrue(TEXT("deferred package is discoverable as dirty"), Context->GetOutermost()->IsDirty());
			FString Reason;
			bOK &= Test.TestTrue(TEXT("deferred save explains the opt-out"), Result->TryGetStringField(TEXT("persistError"), Reason) && Reason.Contains(TEXT("save=false")));
			return bOK;
		}

		bool Reload(int32 ExpectedCount)
		{
			// No pointers to the context or its instanced objects may survive this.
			// Clearing dirty state is limited to this disposable test package.
			Context->ClearFlags(RF_Standalone);
			Context->GetOutermost()->SetDirtyFlag(false);
			ResetLoaders(Context->GetOutermost());
			Context = nullptr;
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
			if (!Test.TestNull(TEXT("context really left memory before disk read"), FindObject<UInputMappingContext>(nullptr, *ContextPath))) return false;
			Context = LoadObject<UInputMappingContext>(nullptr, *ContextPath);
			return Test.TestNotNull(TEXT("context reloads from its saved package"), Context)
				&& Test.TestEqual(TEXT("reloaded mapping count"), Context->GetMappings().Num(), ExpectedCount);
		}

		void CheckModifiers()
		{
			if (!Test.TestEqual(TEXT("one mapping after reload"), Context->GetMappings().Num(), 1)) return;
			const FEnhancedActionKeyMapping& Mapping = Context->GetMappings()[0];
			if (Test.TestEqual(TEXT("modifier survived reload"), Mapping.Modifiers.Num(), 1))
			{
				const UInputModifierNegate* Negate = Cast<UInputModifierNegate>(Mapping.Modifiers[0]);
				if (Test.TestNotNull(TEXT("modifier class survived"), Negate))
				{
					Test.TestFalse(TEXT("non-default modifier property survived"), Negate->bX);
					Test.TestTrue(TEXT("modifier remains owned by the context"), Negate->GetOuter() == Context);
				}
			}
			if (Test.TestEqual(TEXT("trigger survived reload"), Mapping.Triggers.Num(), 1))
			{
				const UInputTriggerHold* Hold = Cast<UInputTriggerHold>(Mapping.Triggers[0]);
				if (Test.TestNotNull(TEXT("trigger class survived"), Hold)) Test.TestEqual(TEXT("non-default trigger property survived"), Hold->HoldTimeThreshold, 0.75f);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPImcSaveReloadTest,
	"UE.MCP.Gameplay.ImcPersistence.DefaultSavesSurviveReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPImcSaveReloadTest::RunTest(const FString& Parameters)
{
	ImcPersistenceTests::FFixture F(*this);
	if (!F.bReady) return false;
	if (!F.Saved(F.Run(TEXT("add_imc_mapping"), F.AddParams())) || !F.Reload(1)) return false;
	if (!TestEqual(TEXT("added mapping survived reload"), F.Context->GetMappings().Num(), 1)) return false;
	TestTrue(TEXT("added action survived reload"), F.Context->GetMappings()[0].Action == F.ActionA);
	TestEqual(TEXT("added key survived reload"), F.Context->GetMappings()[0].Key, EKeys::SpaceBar);

	if (!F.Saved(F.Run(TEXT("set_mapping_modifiers"), F.ModifierParams())) || !F.Reload(1)) return false;
	F.CheckModifiers();

	auto Rebind = F.Params();
	Rebind->SetNumberField(TEXT("mappingIndex"), 0);
	Rebind->SetStringField(TEXT("newKey"), TEXT("J"));
	const auto Rebound = F.Run(TEXT("set_imc_mapping_key"), Rebind);
	if (!F.Saved(Rebound) || !F.Reload(1)) return false;
	TestEqual(TEXT("changed key survived reload"), F.Context->GetMappings()[0].Key, EKeys::J);

	// Execute the returned inverse and verify that its save reaches disk too.
	const TSharedPtr<FJsonObject>* Rollback = nullptr;
	const TSharedPtr<FJsonObject>* Payload = nullptr;
	FString Inverse;
	if (!TestTrue(TEXT("rebind retained its rollback"), Rebound->TryGetObjectField(TEXT("rollback"), Rollback)
		&& (*Rollback)->TryGetStringField(TEXT("method"), Inverse)
		&& (*Rollback)->TryGetObjectField(TEXT("payload"), Payload))) return false;
	F.Field(*Payload, TEXT("save"), true);
	if (!F.Saved(F.Run(*Inverse, *Payload)) || !F.Reload(1)) return false;
	TestEqual(TEXT("rollback restored the saved key"), F.Context->GetMappings()[0].Key, EKeys::SpaceBar);

	auto Retarget = F.Params();
	Retarget->SetNumberField(TEXT("mappingIndex"), 0);
	Retarget->SetStringField(TEXT("newInputActionPath"), F.ActionB->GetPathName());
	if (!F.Saved(F.Run(TEXT("set_imc_mapping_action"), Retarget)) || !F.Reload(1)) return false;
	TestTrue(TEXT("changed action survived reload"), F.Context->GetMappings()[0].Action == F.ActionB);
	F.CheckModifiers();

	auto Remove = F.Params();
	Remove->SetNumberField(TEXT("mappingIndex"), 0);
	if (!F.Saved(F.Run(TEXT("remove_imc_mapping"), Remove)) || !F.Reload(0)) return false;
	TestEqual(TEXT("removed mapping stayed removed after reload"), F.Context->GetMappings().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPImcDeferredResaveTest,
	"UE.MCP.Gameplay.ImcPersistence.DeferredEditsSaveOnIdempotentReplay",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPImcDeferredResaveTest::RunTest(const FString& Parameters)
{
	ImcPersistenceTests::FFixture F(*this);
	if (!F.bReady) return false;
	auto Unsaved = F.AddParams();
	Unsaved->SetBoolField(TEXT("save"), false);
	if (!F.Deferred(F.Run(TEXT("add_imc_mapping"), Unsaved)) || !F.Reload(0)) return false;
	TestEqual(TEXT("save=false left the disk baseline untouched"), F.Context->GetMappings().Num(), 0);

	const auto DeferAndReplay = [&](const TCHAR* Method, const TSharedPtr<FJsonObject>& Args, const TCHAR* NoopField, int32 ExpectedCount = 1)
	{
		Args->SetBoolField(TEXT("save"), false);
		const auto Deferred = F.Run(Method, Args);
		if (!F.Deferred(Deferred)) return false;
		const TSharedPtr<FJsonObject>* Rollback = nullptr;
		const TSharedPtr<FJsonObject>* Payload = nullptr;
		if (!TestTrue(TEXT("deferred edit retained its rollback"), Deferred->TryGetObjectField(TEXT("rollback"), Rollback)
			&& (*Rollback)->TryGetObjectField(TEXT("payload"), Payload))) return false;
		if (!F.Field(*Payload, TEXT("save"), false)) return false;
		Args->SetBoolField(TEXT("save"), true);
		const auto Result = F.Run(Method, Args);
		return F.Field(Result, NoopField, true) && F.Saved(Result) && F.Reload(ExpectedCount);
	};

	if (!DeferAndReplay(TEXT("add_imc_mapping"), F.AddParams(), TEXT("existed"))) return false;
	if (!TestEqual(TEXT("deferred add was eventually saved"), F.Context->GetMappings().Num(), 1)) return false;
	if (!DeferAndReplay(TEXT("set_mapping_modifiers"), F.ModifierParams(), TEXT("unchanged"))) return false;
	F.CheckModifiers();

	auto Rebind = F.Params();
	Rebind->SetNumberField(TEXT("mappingIndex"), 0);
	Rebind->SetStringField(TEXT("newKey"), TEXT("J"));
	if (!DeferAndReplay(TEXT("set_imc_mapping_key"), Rebind, TEXT("unchanged"))) return false;
	TestEqual(TEXT("deferred key was eventually saved"), F.Context->GetMappings()[0].Key, EKeys::J);

	auto Retarget = F.Params();
	Retarget->SetNumberField(TEXT("mappingIndex"), 0);
	Retarget->SetStringField(TEXT("newInputActionPath"), F.ActionB->GetPathName());
	if (!DeferAndReplay(TEXT("set_imc_mapping_action"), Retarget, TEXT("unchanged"))) return false;
	TestTrue(TEXT("deferred action was eventually saved"), F.Context->GetMappings()[0].Action == F.ActionB);

	auto Remove = F.Params();
	Remove->SetStringField(TEXT("inputActionPath"), F.ActionB->GetPathName());
	Remove->SetStringField(TEXT("key"), TEXT("J"));
	if (!DeferAndReplay(TEXT("remove_imc_mapping"), Remove, TEXT("alreadyDeleted"), 0)) return false;
	TestEqual(TEXT("deferred removal was eventually saved"), F.Context->GetMappings().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPImcDeferredRollbackTest,
	"UE.MCP.Gameplay.ImcPersistence.DeferredRollbackDoesNotSavePendingEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPImcDeferredRollbackTest::RunTest(const FString& Parameters)
{
	ImcPersistenceTests::FFixture F(*this);
	if (!F.bReady) return false;
	auto Earlier = F.AddParams();
	Earlier->SetBoolField(TEXT("save"), false);
	if (!F.Deferred(F.Run(TEXT("add_imc_mapping"), Earlier))) return false;

	auto Later = F.AddParams();
	Later->SetStringField(TEXT("inputActionPath"), F.ActionB->GetPathName());
	Later->SetStringField(TEXT("key"), TEXT("J"));
	Later->SetBoolField(TEXT("save"), false);
	const auto Edited = F.Run(TEXT("add_imc_mapping"), Later);
	if (!F.Deferred(Edited)) return false;
	const TSharedPtr<FJsonObject>* Rollback = nullptr;
	const TSharedPtr<FJsonObject>* Payload = nullptr;
	FString Inverse;
	if (!TestTrue(TEXT("later edit supplies its inverse"), Edited->TryGetObjectField(TEXT("rollback"), Rollback)
		&& (*Rollback)->TryGetStringField(TEXT("method"), Inverse)
		&& (*Rollback)->TryGetObjectField(TEXT("payload"), Payload))) return false;
	F.Field(*Payload, TEXT("save"), false);
	if (!F.Deferred(F.Run(*Inverse, *Payload))) return false;
	if (!TestEqual(TEXT("rollback leaves only the earlier pending mapping"), F.Context->GetMappings().Num(), 1)) return false;
	TestTrue(TEXT("earlier pending mapping was preserved in memory"), F.Context->GetMappings()[0].Action == F.ActionA);
	if (!F.Reload(0)) return false;
	TestEqual(TEXT("rollback did not save the earlier pending edit"), F.Context->GetMappings().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPImcPersistenceFailureTest,
	"UE.MCP.Gameplay.ImcPersistence.SaveFailureAndSuppressedDirtyAreReported",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPImcPersistenceFailureTest::RunTest(const FString& Parameters)
{
	ImcPersistenceTests::FFixture F(*this);
	if (!F.bReady) return false;
	IPlatformFile& Files = FPlatformFileManager::Get().GetPlatformFile();
	if (!TestTrue(TEXT("private context file becomes read-only"), Files.SetReadOnly(*F.ContextFile, true))) return false;
	const auto Failed = F.Run(TEXT("add_imc_mapping"), F.AddParams());
	F.Field(Failed, TEXT("success"), false);
	F.Field(Failed, TEXT("saved"), false);
	FString Reason;
	TestTrue(TEXT("refusal names read-only file"), Failed->TryGetStringField(TEXT("error"), Reason) && Reason.Contains(TEXT("read-only")));
	TestFalse(TEXT("refused call has nothing to roll back"), Failed->HasField(TEXT("rollback")));
	TestEqual(TEXT("refused add left the context unchanged in memory"), F.Context->GetMappings().Num(), 0);
	TestFalse(TEXT("refused add left the package clean"), F.Context->GetOutermost()->IsDirty());

	// A no-op on a clean package has nothing to write, so read-only is no obstacle.
	auto Absent = F.AddParams();
	Absent->SetBoolField(TEXT("save"), true);
	const auto Noop = F.Run(TEXT("remove_imc_mapping"), Absent);
	F.Field(Noop, TEXT("alreadyDeleted"), true);
	F.Field(Noop, TEXT("success"), true);
	F.Field(Noop, TEXT("saved"), false);
	F.Field(Noop, TEXT("persisted"), true);
	F.Field(Noop, TEXT("packageDirty"), false);
	Files.SetReadOnly(*F.ContextFile, false);
	if (!F.Reload(0)) return false;
	TestEqual(TEXT("failed save did not alter the disk baseline"), F.Context->GetMappings().Num(), 0);

	// Exercise the engine's actual dirty suppression policy without starting PIE.
	// The flag is restored before any reload or garbage collection.
	{
		TGuardValue<bool> DuringPIE(GIsPlayInEditorWorld, true);
		auto Args = F.AddParams();
		Args->SetBoolField(TEXT("save"), false);
		const auto Suppressed = F.Run(TEXT("add_imc_mapping"), Args);
		F.Field(Suppressed, TEXT("success"), false);
		F.Field(Suppressed, TEXT("persisted"), false);
		F.Field(Suppressed, TEXT("packageDirty"), false);
		TestTrue(TEXT("suppressed dirtying explains recovery"), Suppressed->TryGetStringField(TEXT("persistError"), Reason) && Reason.Contains(TEXT("did not mark")));

		// An explicit package write must not depend on that suppressed flag.
		const auto Retry = F.Run(TEXT("add_imc_mapping"), F.AddParams());
		F.Field(Retry, TEXT("existed"), true);
		if (!F.Saved(Retry)) return false;
	}
	if (!F.Reload(1)) return false;
	TestEqual(TEXT("explicit save recovered the unmarked in-memory mapping"), F.Context->GetMappings().Num(), 1);
	return true;
}

#endif
