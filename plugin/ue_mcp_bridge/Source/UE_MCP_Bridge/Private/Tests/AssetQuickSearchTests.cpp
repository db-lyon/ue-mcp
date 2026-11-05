// Coverage for asset(quick_search) (#1486): query parsing, the 200 ms budget
// at 50,000 assets, and an index that follows add, rename and delete.
//
// The latency case fills an unbound index by hand, so it measures the same
// scoring and ordering the handler runs without needing a 50,000-asset
// project. The currency case runs the real handlers under a private mount.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Asset/AssetHandlers.h"
#include "Handlers/Asset/AssetQuickSearch.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "Tests/MCPScopedTestMount.h"

namespace
{
	const TCHAR* const MCPQsTestRoot = TEXT("/UEMCPQuickSearchTest/");

	TSharedPtr<FJsonObject> MCPQsTestCall(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Params);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : TSharedPtr<FJsonObject>();
	}

	TSharedPtr<FJsonObject> MCPQsTestSearch(FMCPHandlerRegistry& Registry, const FString& Query)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("query"), Query);
		Params->SetNumberField(TEXT("limit"), 200);
		return MCPQsTestCall(Registry, TEXT("quick_search"), Params);
	}

	/** True when the response lists a row of this kind at this path. */
	bool MCPQsTestHasRow(const TSharedPtr<FJsonObject>& Response, const FString& Kind, const FString& Path)
	{
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("results"), Rows)) return false;
		for (const TSharedPtr<FJsonValue>& Row : *Rows)
		{
			const TSharedPtr<FJsonObject> Obj = Row.IsValid() ? Row->AsObject() : nullptr;
			if (Obj.IsValid() && Obj->GetStringField(TEXT("kind")) == Kind && Obj->GetStringField(TEXT("path")) == Path) return true;
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPQuickSearchParseTest,
	"UE.MCP.Asset.QuickSearch.ParsesPrefixAndKindFilters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPQuickSearchParseTest::RunTest(const FString& Parameters)
{
	using namespace MCPQuickSearch;

	const FQuery Q = ParseQuery(TEXT("actor:BP_Door frame"));
	TestTrue(TEXT("actor: sets the kind filter"), Q.bKindFilter && Q.Kind == EKind::Actor);
	TestEqual(TEXT("BP_ becomes the name prefix"), Q.NamePrefix, FString(TEXT("bp_")));
	TestEqual(TEXT("two terms remain"), Q.Terms.Num(), 2);

	const FDoc Door = MakeDoc(EKind::Actor, TEXT("/Game/Maps/L.L:PersistentLevel.BP_DoorFrame_C_1"), TEXT("BP_DoorFrame"), TEXT("BP_DoorFrame_C"), TEXT("in level L"));
	const FDoc Mesh = MakeDoc(EKind::Asset, TEXT("/Game/Props/SM_DoorFrame"), TEXT("SM_DoorFrame"), TEXT("StaticMesh"), TEXT("StaticMesh in /Game/Props"), TEXT("/Game/Props/SM_DoorFrame.SM_DoorFrame"));
	TestTrue(TEXT("the actor matches"), Score(Q, Door) > 0);
	TestEqual(TEXT("the asset is filtered out by kind"), Score(Q, Mesh), 0);
	TestEqual(TEXT("a name without the prefix is filtered out"), Score(ParseQuery(TEXT("DT_ door")), Mesh), 0);
	TestTrue(TEXT("a subsequence still finds a name"), Score(ParseQuery(TEXT("smdrfr")), Mesh) > 0);
	TestEqual(TEXT("every term has to match"), Score(ParseQuery(TEXT("door window")), Mesh), 0);
	TestTrue(TEXT("an exact name outranks a partial one"),
		Score(ParseQuery(TEXT("SM_DoorFrame")), Mesh) > Score(ParseQuery(TEXT("SM_Door")), Mesh));
	TestTrue(TEXT("an empty query is empty"), ParseQuery(TEXT("   ")).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPQuickSearchLatencyTest,
	"UE.MCP.Asset.QuickSearch.Under200MsAt50kAssets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPQuickSearchLatencyTest::RunTest(const FString& Parameters)
{
	using namespace MCPQuickSearch;

	static const TCHAR* const Prefixes[] = { TEXT("BP_"), TEXT("M_"), TEXT("MI_"), TEXT("T_"), TEXT("SM_"), TEXT("SK_"), TEXT("DT_"), TEXT("WBP_"), TEXT("NS_"), TEXT("A_") };
	static const TCHAR* const Classes[] = { TEXT("Blueprint"), TEXT("Material"), TEXT("MaterialInstanceConstant"), TEXT("Texture2D"), TEXT("StaticMesh"), TEXT("SkeletalMesh"), TEXT("DataTable"), TEXT("WidgetBlueprint"), TEXT("NiagaraSystem"), TEXT("AnimSequence") };
	static const TCHAR* const Words[] = { TEXT("Player"), TEXT("Character"), TEXT("Rock"), TEXT("Door"), TEXT("Frame"), TEXT("Weapon"), TEXT("Rifle"), TEXT("Item"), TEXT("Grass"), TEXT("Tree"), TEXT("Wall"), TEXT("Floor"), TEXT("Enemy"), TEXT("Spawn"), TEXT("Light"), TEXT("Water"), TEXT("Cloud"), TEXT("Brick"), TEXT("Metal"), TEXT("Wood") };
	static const TCHAR* const Folders[] = { TEXT("Characters"), TEXT("Environment/Props"), TEXT("Environment/Foliage"), TEXT("UI/Widgets"), TEXT("Weapons"), TEXT("FX"), TEXT("Data"), TEXT("Animations") };

	constexpr int32 AssetCount = 50000;
	FAssetIndex Index;
	for (int32 i = 0; i < AssetCount; ++i)
	{
		const int32 Kind = i % UE_ARRAY_COUNT(Prefixes);
		const FString Name = FString::Printf(TEXT("%s%s%s_%02d"), Prefixes[Kind],
			Words[(i / 7) % UE_ARRAY_COUNT(Words)], Words[(i / 131) % UE_ARRAY_COUNT(Words)], i % 100);
		const FString Folder = FString::Printf(TEXT("/Game/%s/Set%03d"), Folders[(i / 3) % UE_ARRAY_COUNT(Folders)], i % 400);
		const FString Package = Folder / Name;
		Index.Upsert(MakeDoc(EKind::Asset, Package, Name, Classes[Kind],
			FString::Printf(TEXT("%s in %s"), Classes[Kind], *Folder), Package + TEXT(".") + Name));
	}
	TestEqual(TEXT("the index holds every synthetic asset"), Index.Num(), AssetCount);

	// Typed one keystroke at a time, plus the broadest and emptiest shapes.
	const TCHAR* const Queries[] = {
		TEXT("p"), TEXT("pl"), TEXT("pla"), TEXT("play"), TEXT("player"), TEXT("player ch"), TEXT("player character"),
		TEXT("BP_"), TEXT("BP_door"), TEXT("DT_ item"), TEXT("m_rock"), TEXT("bpdrfr"), TEXT("environment wall"), TEXT("zzzzzz"),
	};

	double Worst = 0.0;
	FString WorstQuery;
	for (const TCHAR* Text : Queries)
	{
		const double Start = FPlatformTime::Seconds();
		const FQuery Query = ParseQuery(Text);
		TArray<FHit> Hits;
		Index.Query(Query, Hits);
		SortHits(Hits);
		// The page anchors the handler builds for every hit.
		TArray<FString> Ids;
		Ids.Reserve(Hits.Num());
		for (const FHit& Hit : Hits) Ids.Add(FString(TEXT("asset:")) + Hit.Doc->ObjectPath);
		const double Ms = (FPlatformTime::Seconds() - Start) * 1000.0;
		AddInfo(FString::Printf(TEXT("quick_search '%s' over %d assets: %d matches in %.2f ms"), Text, AssetCount, Hits.Num(), Ms));
		if (Ms > Worst) { Worst = Ms; WorstQuery = Text; }
	}
	AddInfo(FString::Printf(TEXT("slowest query '%s': %.2f ms"), *WorstQuery, Worst));
	TestTrue(FString::Printf(TEXT("every query answers under 200 ms at 50k assets (slowest '%s' took %.2f ms)"), *WorstQuery, Worst), Worst < 200.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPQuickSearchFollowsRegistryTest,
	"UE.MCP.Asset.QuickSearch.FollowsAddRenameAndDelete",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPQuickSearchFollowsRegistryTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ FString(MCPQsTestRoot), TEXT("UEMCPQuickSearchTest") };

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);

	// Bind the index before the asset exists, so finding it proves the add event.
	const TSharedPtr<FJsonObject> Warm = MCPQsTestSearch(Registry, TEXT("E_QsProbeOriginal"));
	if (!TestTrue(TEXT("quick_search answers"), Warm.IsValid() && Warm->GetBoolField(TEXT("success")))) return false;
	TestTrue(TEXT("the index reports its asset count"), Warm->HasField(TEXT("indexedAssets")));
	TestTrue(TEXT("the response records its latency"), Warm->HasField(TEXT("elapsedMs")));

	const FString Folder = Mount.RootPath.LeftChop(1);
	const FString Original = Folder + TEXT("/E_QsProbeOriginal");
	const FString Renamed = Folder + TEXT("/E_QsProbeRenamed");

	TSharedPtr<FJsonObject> Create = MakeShared<FJsonObject>();
	Create->SetStringField(TEXT("name"), TEXT("E_QsProbeOriginal"));
	Create->SetStringField(TEXT("packagePath"), Folder);
	const TSharedPtr<FJsonObject> Created = MCPQsTestCall(Registry, TEXT("create_user_defined_enum"), Create);
	if (!TestTrue(TEXT("the probe enum was created"), Created.IsValid() && Created->GetBoolField(TEXT("success")))) return false;

	const TSharedPtr<FJsonObject> AfterAdd = MCPQsTestSearch(Registry, TEXT("asset:E_QsProbe"));
	TestTrue(TEXT("a new asset is found without a reindex"), MCPQsTestHasRow(AfterAdd, TEXT("asset"), Original));

	TSharedPtr<FJsonObject> Rename = MakeShared<FJsonObject>();
	Rename->SetStringField(TEXT("assetPath"), Original);
	Rename->SetStringField(TEXT("newName"), TEXT("E_QsProbeRenamed"));
	const TSharedPtr<FJsonObject> RenameResult = MCPQsTestCall(Registry, TEXT("rename_asset"), Rename);
	if (!TestTrue(TEXT("the probe was renamed"), RenameResult.IsValid() && RenameResult->GetBoolField(TEXT("success")))) return false;

	const TSharedPtr<FJsonObject> AfterRename = MCPQsTestSearch(Registry, TEXT("asset:E_QsProbe"));
	TestTrue(TEXT("the renamed asset is found"), MCPQsTestHasRow(AfterRename, TEXT("asset"), Renamed));
	TestFalse(TEXT("the old name is gone"), MCPQsTestHasRow(AfterRename, TEXT("asset"), Original));

	TSharedPtr<FJsonObject> Delete = MakeShared<FJsonObject>();
	Delete->SetStringField(TEXT("assetPath"), Renamed);
	Delete->SetBoolField(TEXT("force"), true);
	const TSharedPtr<FJsonObject> Deleted = MCPQsTestCall(Registry, TEXT("delete_asset"), Delete);
	if (!TestTrue(TEXT("the probe was deleted"), Deleted.IsValid() && Deleted->GetBoolField(TEXT("success")))) return false;

	const TSharedPtr<FJsonObject> AfterDelete = MCPQsTestSearch(Registry, TEXT("asset:E_QsProbe"));
	TestFalse(TEXT("a deleted asset is no longer found"), MCPQsTestHasRow(AfterDelete, TEXT("asset"), Renamed));

	// Files: every project has Config/DefaultEngine.ini.
	const TSharedPtr<FJsonObject> Files = MCPQsTestSearch(Registry, TEXT("file:DefaultEngine"));
	TestTrue(TEXT("Config files are searchable"), MCPQsTestHasRow(Files, TEXT("file"), TEXT("Config/DefaultEngine.ini")));

	// Actors: the kind filter keeps only actor rows.
	const TSharedPtr<FJsonObject> Actors = MCPQsTestSearch(Registry, TEXT("actor:"));
	if (TestTrue(TEXT("actor: answers"), Actors.IsValid() && Actors->GetBoolField(TEXT("success"))))
	{
		const TSharedPtr<FJsonObject> ByKind = Actors->GetObjectField(TEXT("matchedByKind"));
		TestEqual(TEXT("actor: returns no assets"), static_cast<int32>(ByKind->GetNumberField(TEXT("asset"))), 0);
		TestEqual(TEXT("actor: returns no files"), static_cast<int32>(ByKind->GetNumberField(TEXT("file"))), 0);
		TestEqual(TEXT("every scanned actor matches a bare actor:"),
			static_cast<int32>(ByKind->GetNumberField(TEXT("actor"))), static_cast<int32>(Actors->GetNumberField(TEXT("scannedActors"))));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
