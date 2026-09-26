// Coverage for the World Partition streaming surface (#985).
//
// These actions write the streaming configuration of whatever map is open, so
// the assertions stay on the paths specified to change nothing: the refusals
// that keep a bad request from half-applying, and the shape of the read. A test
// that retuned CellSize to prove CellSize can be retuned would leave somebody's
// map streaming differently.
//
// The read is asserted on whichever map the run happens to have open. On a
// non-partitioned map it must refuse by name rather than answering with an
// empty success, because "this map has no grids" and "this map is not World
// Partition" are different facts and only one of them is actionable.

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "Engine/World.h"
#include "HandlerRegistry.h"
#include "Handlers/Level/LevelHandlers.h"
#include "Misc/AutomationTest.h"
#include "WorldPartition/WorldPartition.h"

namespace
{
	TSharedPtr<FJsonObject> MCPWorldPartitionTestRun(
		FMCPHandlerRegistry& Registry,
		const TCHAR* Method,
		const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Request);
		if (!Response.IsValid() || Response->Type != EJson::Object)
		{
			return nullptr;
		}
		return Response->AsObject();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelWorldPartitionSettingsSurfaceTest,
	"UE.MCP.Level.WorldPartition.SettingsSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelWorldPartitionSettingsSurfaceTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	for (const TCHAR* Method : {
		TEXT("get_world_partition_settings"),
		TEXT("set_world_partition_settings"),
		TEXT("add_runtime_cell_transformer"),
		TEXT("set_actor_hlod_layer") })
	{
		TestTrue(FString::Printf(TEXT("%s is registered"), Method), Registry.HasHandler(Method));
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World) return true;
	const bool bPartitioned = World->GetWorldPartition() != nullptr;

	// The read.
	{
		const TSharedPtr<FJsonObject> Response =
			MCPWorldPartitionTestRun(Registry, TEXT("get_world_partition_settings"), MakeShared<FJsonObject>());
		if (Response.IsValid())
		{
			if (bPartitioned)
			{
				TestTrue(TEXT("a World Partition map answers"), Response->GetBoolField(TEXT("success")));
				const TArray<TSharedPtr<FJsonValue>>* Grids = nullptr;
				TestTrue(TEXT("and reports its streaming grids"),
					Response->TryGetArrayField(TEXT("grids"), Grids));
				const TArray<TSharedPtr<FJsonValue>>* Transformers = nullptr;
				TestTrue(TEXT("and its runtime cell transformer stack"),
					Response->TryGetArrayField(TEXT("cellTransformers"), Transformers));
				FString WorldPartitionPath;
				TestTrue(TEXT("and names the world partition object"),
					Response->TryGetStringField(TEXT("worldPartition"), WorldPartitionPath));
			}
			else
			{
				// The distinction that matters: not partitioned is a refusal
				// with a reason, not an empty success a caller reads as "no
				// grids configured".
				TestFalse(TEXT("a non-partitioned map is refused"), Response->GetBoolField(TEXT("success")));
				TestTrue(TEXT("and the refusal says why"),
					Response->GetStringField(TEXT("error")).Contains(TEXT("World Partition")));
			}
		}
	}

	// A write with nothing in it is a request that means nothing.
	{
		const TSharedPtr<FJsonObject> Response =
			MCPWorldPartitionTestRun(Registry, TEXT("set_world_partition_settings"), MakeShared<FJsonObject>());
		if (Response.IsValid())
		{
			TestFalse(TEXT("an empty write is refused"), Response->GetBoolField(TEXT("success")));
		}
	}

	// A path that does not resolve fails the whole call. Streaming settings are
	// changed in batches and a half-applied batch is a configuration nobody
	// asked for.
	if (bPartitioned)
	{
		TSharedPtr<FJsonObject> Settings = MakeShared<FJsonObject>();
		Settings->SetNumberField(TEXT("NotARealWorldPartitionProperty"), 1.0);
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetObjectField(TEXT("settings"), Settings);
		const TSharedPtr<FJsonObject> Response =
			MCPWorldPartitionTestRun(Registry, TEXT("set_world_partition_settings"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unresolvable path fails the call"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and says nothing was written"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("Nothing was written")));
		}
	}

	// A class that is not a cell transformer is refused by name rather than
	// added and left to fail at streaming-generation time.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("transformerClass"), TEXT("StaticMeshActor"));
		const TSharedPtr<FJsonObject> Response =
			MCPWorldPartitionTestRun(Registry, TEXT("add_runtime_cell_transformer"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("a non-transformer class is refused"), Response->GetBoolField(TEXT("success")));
		}
	}

	// The HLOD batch refuses an unbounded run, like every other selector-driven
	// write in this category.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("hlodLayer"), FString());
		const TSharedPtr<FJsonObject> Response =
			MCPWorldPartitionTestRun(Registry, TEXT("set_actor_hlod_layer"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unbounded HLOD assignment is refused"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and names the selectors it wants"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("actorLabels")));
		}
	}

	// And a missing hlodLayer is refused rather than treated as "clear it",
	// because clearing 295 actors' overrides by omission would be a disaster.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("labelPrefix"), TEXT("UEMCP_NoSuchActorPrefix"));
		const TSharedPtr<FJsonObject> Response =
			MCPWorldPartitionTestRun(Registry, TEXT("set_actor_hlod_layer"), Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("omitting hlodLayer is refused"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("and says the parameter is required"),
				Response->GetStringField(TEXT("error")).Contains(TEXT("hlodLayer")));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
