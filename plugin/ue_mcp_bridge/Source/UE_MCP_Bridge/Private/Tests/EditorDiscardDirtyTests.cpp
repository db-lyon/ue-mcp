// editor(discard_dirty): an editor whose only unsaved package was the
// /Engine/Maps/Entry startup map could not be closed. stop_editor sends the
// quit and the editor's Save Content prompt asks about a map nobody can save,
// and the Python workaround (Package.set_dirty_flag) does not exist in 5.8.
//
// The test dirties an engine map, a transient map and a content asset, shows
// the content asset refused without allowContent (and that the refusal clears
// nothing), clears the maps, and reads the editor's dirty lists, the ones
// request_editor_shutdown and a close prompt for, to show they are off them.
//
// The engine map is an in-memory world under a per-run /Engine/ name, so the
// real Entry map is never touched, and the content asset lives under its own
// mount, so nothing here reaches the attached project.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Editor/EditorHandlers.h"
#include "Tests/MCPScopedTestMount.h"

#include "Curves/CurveFloat.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

namespace
{
	const TCHAR* const MCPDiscardDirtyTestRoot = TEXT("/UEMCPDiscardDirtyTest/");

	TSharedPtr<FJsonObject> MCPDiscardCall(
		FMCPHandlerRegistry& Registry,
		const TArray<FString>& Packages,
		bool bAllowContent = false,
		bool bDryRun = false)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Names;
		for (const FString& Package : Packages)
		{
			Names.Add(MakeShared<FJsonValueString>(Package));
		}
		Params->SetArrayField(TEXT("packages"), Names);
		if (bAllowContent) Params->SetBoolField(TEXT("allowContent"), true);
		if (bDryRun) Params->SetBoolField(TEXT("dryRun"), true);
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("discard_dirty"), Params);
		return Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : nullptr;
	}

	/** The status discard_dirty reported for one package, or empty. */
	FString MCPDiscardStatus(const TSharedPtr<FJsonObject>& Result, const FString& Package)
	{
		const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
		if (!Result.IsValid() || !Result->TryGetArrayField(TEXT("packages"), Items)) return FString();
		for (const TSharedPtr<FJsonValue>& Item : *Items)
		{
			const TSharedPtr<FJsonObject> Obj = Item.IsValid() ? Item->AsObject() : nullptr;
			if (Obj.IsValid() && Obj->GetStringField(TEXT("package")) == Package)
			{
				return Obj->GetStringField(TEXT("status"));
			}
		}
		return FString();
	}

	/** Every package editor(list_dirty_packages) reports, content and maps. */
	TSet<FString> MCPListedDirty(FMCPHandlerRegistry& Registry)
	{
		TSet<FString> Names;
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("list_dirty_packages"), MakeShared<FJsonObject>());
		const TSharedPtr<FJsonObject> Result = Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : nullptr;
		if (!Result.IsValid()) return Names;
		for (const TCHAR* Key : { TEXT("content"), TEXT("maps") })
		{
			const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
			if (!Result->TryGetArrayField(Key, Items)) continue;
			for (const TSharedPtr<FJsonValue>& Item : *Items)
			{
				const TSharedPtr<FJsonObject> Obj = Item.IsValid() ? Item->AsObject() : nullptr;
				if (Obj.IsValid()) Names.Add(Obj->GetStringField(TEXT("package")));
			}
		}
		return Names;
	}

	UWorld* MCPMakeDirtyTestWorld(UPackage* Package, const TCHAR* WorldName)
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Inactive, /*bInformEngineOfWorld=*/false, FName(WorldName), Package, /*bAddToRoot=*/false, ERHIFeatureLevel::Num,
			/*InWorldInitializationValues=*/nullptr, /*bInSkipInitWorld=*/true);
		if (World)
		{
			World->SetFlags(RF_Public | RF_Standalone);
			Package->SetDirtyFlag(true);
		}
		return World;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FEditorDiscardDirtyTest,
	"UE.MCP.Editor.DiscardDirty.EngineMapClearsContentRefused",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEditorDiscardDirtyTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ FString(MCPDiscardDirtyTestRoot), TEXT("UEMCPDiscardDirtyTest") };
	FMCPHandlerRegistry Registry;
	FEditorHandlers::RegisterHandlers(Registry);

	// Fresh names per run: the suite runs twice in one editor process.
	const FString Run = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString EngineMapName = TEXT("/Engine/UEMCPDiscardDirtyTest_") + Run;
	const FString TransientMapName = TEXT("/Engine/UEMCPDiscardDirtyTransient_") + Run;
	const FString ContentName = FString(MCPDiscardDirtyTestRoot) + TEXT("Curve_") + Run;

	UPackage* EngineMapPackage = CreatePackage(*EngineMapName);
	UPackage* TransientMapPackage = CreatePackage(*TransientMapName);
	UPackage* ContentPackage = CreatePackage(*ContentName);
	if (!TestTrue(TEXT("test packages created"), EngineMapPackage && TransientMapPackage && ContentPackage))
	{
		return false;
	}
	TransientMapPackage->SetFlags(RF_Transient);

	UWorld* EngineMap = MCPMakeDirtyTestWorld(EngineMapPackage, TEXT("EngineMap"));
	UWorld* TransientMap = MCPMakeDirtyTestWorld(TransientMapPackage, TEXT("TransientMap"));
	TestTrue(TEXT("engine map package retains ContainsMap without world initialization"), EngineMap && EngineMapPackage->ContainsMap());
	TestTrue(TEXT("transient map package retains ContainsMap without world initialization"), TransientMap && TransientMapPackage->ContainsMap());
	UCurveFloat* Curve = NewObject<UCurveFloat>(ContentPackage, *(TEXT("Curve_") + Run), RF_Public | RF_Standalone);
	if (!TestTrue(TEXT("test worlds and asset created"), EngineMap && TransientMap && Curve))
	{
		return false;
	}
	const FGCRootScope KeepEngineMap(EngineMap);
	const FGCRootScope KeepTransientMap(TransientMap);
	const FGCRootScope KeepCurve(Curve);
	ContentPackage->SetDirtyFlag(true);

	ON_SCOPE_EXIT
	{
		EngineMapPackage->SetDirtyFlag(false);
		TransientMapPackage->SetDirtyFlag(false);
		ContentPackage->SetDirtyFlag(false);
		MCPDestroyDisposableWorld(EngineMap);
		MCPDestroyDisposableWorld(TransientMap);
	};

	const TSet<FString> Before = MCPListedDirty(Registry);
	TestTrue(TEXT("the engine map is on the editor's dirty list"), Before.Contains(EngineMapName));
	TestTrue(TEXT("the content asset is on the editor's dirty list"), Before.Contains(ContentName));

	// Content without allowContent is refused, and one refusal clears nothing.
	{
		const TSharedPtr<FJsonObject> Result = MCPDiscardCall(Registry, { EngineMapName, ContentName });
		TestTrue(TEXT("mixed call answers"), Result.IsValid());
		TestFalse(TEXT("mixed call fails"), Result.IsValid() && Result->GetBoolField(TEXT("success")));
		TestEqual(TEXT("content asset refused"), MCPDiscardStatus(Result, ContentName), FString(TEXT("refused")));
		TestEqual(TEXT("engine map reported as wouldClear"), MCPDiscardStatus(Result, EngineMapName), FString(TEXT("wouldClear")));
		TestTrue(TEXT("engine map still dirty after a refused call"), EngineMapPackage->IsDirty());
		TestTrue(TEXT("content asset still dirty after a refused call"), ContentPackage->IsDirty());
	}

	// The maps clear, and only the packages named do.
	{
		const TSharedPtr<FJsonObject> Result = MCPDiscardCall(Registry, { EngineMapName, TransientMapName });
		TestTrue(TEXT("map call succeeds"), Result.IsValid() && Result->GetBoolField(TEXT("success")));
		TestEqual(TEXT("engine map cleared"), MCPDiscardStatus(Result, EngineMapName), FString(TEXT("cleared")));
		TestEqual(TEXT("transient map cleared"), MCPDiscardStatus(Result, TransientMapName), FString(TEXT("cleared")));
		TestFalse(TEXT("engine map package is clean"), EngineMapPackage->IsDirty());
		TestFalse(TEXT("transient map package is clean"), TransientMapPackage->IsDirty());
		TestTrue(TEXT("the unnamed content asset is untouched"), ContentPackage->IsDirty());

		const TSet<FString> After = MCPListedDirty(Registry);
		TestFalse(TEXT("the engine map is off the list a close prompts for"), After.Contains(EngineMapName));
		TestTrue(TEXT("the content asset is still on it"), After.Contains(ContentName));
	}

	// A repeat reports notDirty rather than failing.
	{
		const TSharedPtr<FJsonObject> Result = MCPDiscardCall(Registry, { EngineMapName });
		TestTrue(TEXT("repeat succeeds"), Result.IsValid() && Result->GetBoolField(TEXT("success")));
		TestEqual(TEXT("repeat reports notDirty"), MCPDiscardStatus(Result, EngineMapName), FString(TEXT("notDirty")));
		TestFalse(TEXT("repeat changed nothing"), Result.IsValid() && Result->GetBoolField(TEXT("changed")));
	}

	// A name nothing loaded answers to is refused, not quietly skipped.
	{
		const FString Missing = FString(MCPDiscardDirtyTestRoot) + TEXT("Missing_") + Run;
		const TSharedPtr<FJsonObject> Result = MCPDiscardCall(Registry, { Missing });
		TestEqual(TEXT("unloaded package refused"), MCPDiscardStatus(Result, Missing), FString(TEXT("refused")));
	}

	// allowContent: a dry run clears nothing, the real call clears it.
	{
		const TSharedPtr<FJsonObject> Dry = MCPDiscardCall(Registry, { ContentName }, /*bAllowContent=*/true, /*bDryRun=*/true);
		TestEqual(TEXT("dry run reports wouldClear"), MCPDiscardStatus(Dry, ContentName), FString(TEXT("wouldClear")));
		TestTrue(TEXT("dry run leaves the content dirty"), ContentPackage->IsDirty());

		const TSharedPtr<FJsonObject> Result = MCPDiscardCall(Registry, { ContentName }, /*bAllowContent=*/true);
		TestTrue(TEXT("allowContent call succeeds"), Result.IsValid() && Result->GetBoolField(TEXT("success")));
		TestEqual(TEXT("content cleared with allowContent"), MCPDiscardStatus(Result, ContentName), FString(TEXT("cleared")));
		TestFalse(TEXT("content package is clean"), ContentPackage->IsDirty());
		TestFalse(TEXT("the content asset is off the list"), MCPListedDirty(Registry).Contains(ContentName));
	}

	return true;
}

#endif
