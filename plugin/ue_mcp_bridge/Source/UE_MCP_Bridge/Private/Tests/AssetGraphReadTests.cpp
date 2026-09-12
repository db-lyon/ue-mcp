// asset(read_graph): what it reports, and what it refuses to guess (#1059).
//
// Built on a Blueprint because that is a graph type the engine will create
// without an editor window. The reader itself is type-agnostic.

#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/AssetHandlers.h"
#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "UObject/Package.h"

namespace
{
	const TCHAR* const MCPGraphTestRoot = TEXT("/UEMCPGraphTest/");

	/** A private content root for the duration of the test, so nothing here
	 *  can touch the project the bridge happens to be attached to. */
	struct FScopedGraphTestMount
	{
		FString RootPath;
		FString ContentPath;

		FScopedGraphTestMount()
			: RootPath(MCPGraphTestRoot)
			, ContentPath(FPaths::Combine(
				FPaths::ConvertRelativePathToFull(FString(FPlatformProcess::UserTempDir())),
				FString(TEXT("UEMCPGraphTest")),
				FGuid::NewGuid().ToString(EGuidFormats::Digits)))
		{
			IFileManager::Get().MakeDirectory(*ContentPath, /*Tree=*/true);
			FPackageName::RegisterMountPoint(RootPath, ContentPath);
		}

		~FScopedGraphTestMount()
		{
			FPackageName::UnRegisterMountPoint(RootPath, ContentPath);
			IFileManager::Get().DeleteDirectory(*ContentPath, /*RequireExists=*/false, /*Tree=*/true);
		}

		FScopedGraphTestMount(const FScopedGraphTestMount&) = delete;
		FScopedGraphTestMount& operator=(const FScopedGraphTestMount&) = delete;
	};

	/** Through the registry, which also proves the action is registered. */
	TSharedPtr<FJsonObject> ReadGraph(const FString& AssetPath, int32 MaxNodes = 0)
	{
		FMCPHandlerRegistry Registry;
		FAssetHandlers::RegisterHandlers(Registry);

		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), AssetPath);
		if (MaxNodes > 0) Params->SetNumberField(TEXT("maxNodes"), MaxNodes);

		const TSharedPtr<FJsonValue> Value = Registry.ExecuteHandler(TEXT("read_asset_graph"), Params);
		return Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
	}

	/** The named graph out of a read_graph response, or null. */
	TSharedPtr<FJsonObject> GraphNamed(const TSharedPtr<FJsonObject>& Result, const TCHAR* Name)
	{
		if (!Result.IsValid()) return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Graphs = nullptr;
		if (!Result->TryGetArrayField(TEXT("graphs"), Graphs) || !Graphs) return nullptr;
		for (const TSharedPtr<FJsonValue>& Entry : *Graphs)
		{
			const TSharedPtr<FJsonObject> Graph = Entry.IsValid() ? Entry->AsObject() : nullptr;
			FString GraphName;
			if (Graph.IsValid() && Graph->TryGetStringField(TEXT("name"), GraphName) && GraphName == Name)
			{
				return Graph;
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPAssetReadGraphDefersTest,
	"UE.MCP.Asset.ReadGraph.DefersToTheActionThatOwnsTheType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetReadGraphDefersTest::RunTest(const FString& Parameters)
{
	// A Blueprint has blueprint(read_graph) and blueprint(get_connections).
	// Answering it here too would be a second shape for the same question.
	const FScopedGraphTestMount Mount;

	const FString ProbeName = TEXT("BP_GraphProbe_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString PackageName = FString(MCPGraphTestRoot) + ProbeName;

	UPackage* Package = CreatePackage(*PackageName);
	TestNotNull(TEXT("probe package created"), Package);
	if (!Package) return false;

	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(), Package, FName(*ProbeName), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	TestNotNull(TEXT("probe blueprint created"), Blueprint);
	if (!Blueprint)
	{
		Package->SetDirtyFlag(false);
		return false;
	}

	const TSharedPtr<FJsonObject> Result = ReadGraph(PackageName);
	TestNotNull(TEXT("the call answered"), Result.Get());
	if (Result.IsValid())
	{
		bool bSuccess = true;
		Result->TryGetBoolField(TEXT("success"), bSuccess);
		TestFalse(TEXT("a Blueprint is refused here"), bSuccess);

		FString Error;
		Result->TryGetStringField(TEXT("error"), Error);
		TestTrue(TEXT("and is pointed at the action that owns it"),
			Error.Contains(TEXT("blueprint(read_graph)")));
	}

	Blueprint->ClearFlags(RF_Public | RF_Standalone);
	Blueprint->MarkAsGarbage();
	Package->SetDirtyFlag(false);
	Package->ClearFlags(RF_Public | RF_Standalone);
	Blueprint = nullptr;
	Package = nullptr;
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPAssetReadGraphNoGraphTest,
	"UE.MCP.Asset.ReadGraph.AnAssetWithNoGraphSaysSoWithoutFailing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetReadGraphNoGraphTest::RunTest(const FString& Parameters)
{
	// A graph-less asset is not an error, and the answer must not assert more
	// than was actually searched.
	const TSharedPtr<FJsonObject> Result = ReadGraph(TEXT("/Engine/EngineMaterials/DefaultMaterial"));
	TestNotNull(TEXT("the call answered"), Result.Get());
	if (!Result.IsValid()) return false;

	bool bSuccess = false;
	Result->TryGetBoolField(TEXT("success"), bSuccess);
	TestTrue(TEXT("no graph is not a failure"), bSuccess);
	TestEqual(TEXT("no graphs are reported"), (int32)Result->GetNumberField(TEXT("graphCount")), 0);

	FString Note;
	Result->TryGetStringField(TEXT("note"), Note);
	TestTrue(TEXT("the note says what was searched"), Note.Contains(TEXT("Searched its subobjects")));
	TestTrue(TEXT("and does not assert the asset has no graph"), !Note.Contains(TEXT("holds no UEdGraph")));

	return true;
}

#endif
