// Coverage for the batch form of animation(read_sequence) (#1163).
//
// The probe sequence lives under a private mount, so nothing here touches the
// attached project's content. The assertions are about the contract: one row
// per path, a failed path reported on its own row, `fields` narrowing the row,
// and paging.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Animation/AnimationHandlers.h"
#include "Tests/MCPScopedTestMount.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"
#include "UObject/Package.h"

namespace
{
	const TCHAR* const MCPReadSeqBatchTestRoot = TEXT("/UEMCPReadSeqBatchTest/");

	TSharedPtr<FJsonObject> MCPReadSeqBatchRun(FMCPHandlerRegistry& Registry, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("read_anim_sequence"), Request);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
	}

	TArray<TSharedPtr<FJsonValue>> MCPReadSeqStrings(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& Value : Values)
		{
			Out.Add(MakeShared<FJsonValueString>(Value));
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimationSequenceBatchReadTest,
	"UE.MCP.Animation.ReadSequence.Batch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationSequenceBatchReadTest::RunTest(const FString& Parameters)
{
	// The missing path below is deliberate and the engine loader logs an Error
	// for it. Count 0 means it must occur.
	AddExpectedError(TEXT("LoadAsset failed"), EAutomationExpectedErrorFlags::Contains, 0);

	const FMCPScopedTestMount Mount(FString(MCPReadSeqBatchTestRoot), TEXT("UEMCPReadSeqBatchTest"));

	const FString ProbePath = FString(MCPReadSeqBatchTestRoot) + TEXT("AS_BatchProbe");
	const FString MissingPath = FString(MCPReadSeqBatchTestRoot) + TEXT("AS_NotThere");

	UPackage* Package = CreatePackage(*ProbePath);
	TestNotNull(TEXT("probe package created"), Package);
	if (!Package)
	{
		return false;
	}
	UAnimSequence* Probe = NewObject<UAnimSequence>(Package, FName(TEXT("AS_BatchProbe")), RF_Public | RF_Standalone);
	TestNotNull(TEXT("probe sequence created"), Probe);
	if (!Probe)
	{
		Package->SetDirtyFlag(false);
		return false;
	}
	const FGCRootScope KeepProbeAlive(Probe);

	// A one-bone skeleton and a 30 frame, 30 fps model: one second long.
	USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage());
	const FGCRootScope KeepSkeletonAlive(Skeleton);
	{
		FReferenceSkeletonModifier Modifier(Skeleton);
		Modifier.Add(FMeshBoneInfo(TEXT("root"), TEXT("root"), INDEX_NONE), FTransform::Identity);
	}
	Probe->SetSkeleton(Skeleton);
	IAnimationDataController& Controller = Probe->GetController();
	Controller.InitializeModel();
	Controller.OpenBracket(FText::FromString(TEXT("Build batch-read probe")), false);
	Controller.SetFrameRate(FFrameRate(30, 1), false);
	Controller.SetNumberOfFrames(FFrameNumber(30), false);
	Controller.CloseBracket(false);
	Probe->RateScale = 0.5f;

	FMCPHandlerRegistry Registry;
	FAnimationHandlers::RegisterHandlers(Registry);

	// One row per path, in request order, and a missing path is its own row.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetArrayField(TEXT("assetPaths"), MCPReadSeqStrings({ ProbePath, MissingPath }));
		Request->SetArrayField(TEXT("fields"), MCPReadSeqStrings({ TEXT("rateScale"), TEXT("notifyCount") }));
		const TSharedPtr<FJsonObject> Response = MCPReadSeqBatchRun(Registry, Request);
		TestTrue(TEXT("a batch returns an object"), Response.IsValid());
		if (Response.IsValid())
		{
			TestTrue(TEXT("a batch with one bad path still succeeds"), Response->GetBoolField(TEXT("success")));
			TestEqual(TEXT("failedCount counts the bad path"), static_cast<int32>(Response->GetNumberField(TEXT("failedCount"))), 1);
			TestEqual(TEXT("total counts both paths"), static_cast<int32>(Response->GetNumberField(TEXT("total"))), 2);
			const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
			TestTrue(TEXT("rows are under 'sequences'"), Response->TryGetArrayField(TEXT("sequences"), Rows));
			if (Rows && Rows->Num() == 2)
			{
				const TSharedPtr<FJsonObject> Good = (*Rows)[0]->AsObject();
				const TSharedPtr<FJsonObject> Bad = (*Rows)[1]->AsObject();
				TestTrue(TEXT("the probe row succeeded"), Good->GetBoolField(TEXT("success")));
				TestEqual(TEXT("the probe row carries its path"), Good->GetStringField(TEXT("assetPath")), ProbePath);
				TestEqual(TEXT("the probe row reports rateScale"), Good->GetNumberField(TEXT("rateScale")), 0.5);
				TestEqual(TEXT("the probe row reports notifyCount"), static_cast<int32>(Good->GetNumberField(TEXT("notifyCount"))), 0);
				TestFalse(TEXT("an unselected field is left out"), Good->HasField(TEXT("curveNames")));
				TestFalse(TEXT("an unselected scalar is left out"), Good->HasField(TEXT("sequenceLength")));
				TestFalse(TEXT("the missing row failed"), Bad->GetBoolField(TEXT("success")));
				TestEqual(TEXT("the missing row carries its path"), Bad->GetStringField(TEXT("assetPath")), MissingPath);
				TestTrue(TEXT("the missing row names the path in its error"),
					Bad->GetStringField(TEXT("error")).Contains(TEXT("AS_NotThere")));
			}
			else
			{
				AddError(TEXT("expected exactly two rows"));
			}
		}
	}

	// Omitting fields gives the compact default row, not the full read.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetArrayField(TEXT("assetPaths"), MCPReadSeqStrings({ ProbePath }));
		const TSharedPtr<FJsonObject> Response = MCPReadSeqBatchRun(Registry, Request);
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (Response.IsValid() && Response->TryGetArrayField(TEXT("sequences"), Rows) && Rows->Num() == 1)
		{
			const TSharedPtr<FJsonObject> Row = (*Rows)[0]->AsObject();
			TestTrue(TEXT("the default row has rateScale"), Row->HasField(TEXT("rateScale")));
			TestTrue(TEXT("the default row reports the one second length"),
				FMath::IsNearlyEqual(Row->GetNumberField(TEXT("sequenceLength")), 1.0, 0.001));
			TestFalse(TEXT("the default row leaves out notifies"), Row->HasField(TEXT("notifies")));
		}
		else
		{
			AddError(TEXT("default-field batch did not return one row"));
		}
	}

	// Paging: limit 1 over two paths leaves a cursor, and the cursor reaches row two.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetArrayField(TEXT("assetPaths"), MCPReadSeqStrings({ ProbePath, MissingPath }));
		Request->SetNumberField(TEXT("limit"), 1);
		const TSharedPtr<FJsonObject> First = MCPReadSeqBatchRun(Registry, Request);
		FString Cursor;
		if (First.IsValid())
		{
			TestTrue(TEXT("page one says there is more"), First->GetBoolField(TEXT("hasMore")));
			TestTrue(TEXT("page one carries a cursor"), First->TryGetStringField(TEXT("nextCursor"), Cursor));
		}
		if (!Cursor.IsEmpty())
		{
			Request->SetStringField(TEXT("cursor"), Cursor);
			const TSharedPtr<FJsonObject> Second = MCPReadSeqBatchRun(Registry, Request);
			const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
			if (Second.IsValid() && Second->TryGetArrayField(TEXT("sequences"), Rows) && Rows->Num() == 1)
			{
				TestEqual(TEXT("page two is the second path"),
					(*Rows)[0]->AsObject()->GetStringField(TEXT("assetPath")), MissingPath);
				TestFalse(TEXT("page two is the last"), Second->GetBoolField(TEXT("hasMore")));
			}
			else
			{
				AddError(TEXT("page two did not return one row"));
			}
		}
	}

	// A misspelt field is refused with the valid list rather than ignored.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetArrayField(TEXT("assetPaths"), MCPReadSeqStrings({ ProbePath }));
		Request->SetArrayField(TEXT("fields"), MCPReadSeqStrings({ TEXT("rate_scale") }));
		const TSharedPtr<FJsonObject> Response = MCPReadSeqBatchRun(Registry, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unknown field fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the error lists rateScale"), Response->GetStringField(TEXT("error")).Contains(TEXT("rateScale")));
		}
	}

	// assetPaths and directory together is ambiguous and refused.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetArrayField(TEXT("assetPaths"), MCPReadSeqStrings({ ProbePath }));
		Request->SetStringField(TEXT("directory"), FString(MCPReadSeqBatchTestRoot));
		const TSharedPtr<FJsonObject> Response = MCPReadSeqBatchRun(Registry, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("assetPaths with directory fails"), Response->GetBoolField(TEXT("success")));
		}
	}

	// A directory with no sequences is an empty page, not an error.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("directory"), TEXT("/UEMCPReadSeqBatchTest/Empty"));
		Request->SetStringField(TEXT("nameFilter"), TEXT("walk*"));
		const TSharedPtr<FJsonObject> Response = MCPReadSeqBatchRun(Registry, Request);
		if (Response.IsValid())
		{
			TestTrue(TEXT("an empty directory succeeds"), Response->GetBoolField(TEXT("success")));
			TestEqual(TEXT("an empty directory has no rows"), static_cast<int32>(Response->GetNumberField(TEXT("total"))), 0);
		}
	}

	// The single-asset form is unchanged and still returns the full read.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), ProbePath);
		const TSharedPtr<FJsonObject> Response = MCPReadSeqBatchRun(Registry, Request);
		if (Response.IsValid())
		{
			TestTrue(TEXT("a single read succeeds"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("a single read keeps notifies"), Response->HasField(TEXT("notifies")));
			TestEqual(TEXT("a single read reports rateScale"), Response->GetNumberField(TEXT("rateScale")), 0.5);
		}
	}

	Package->SetDirtyFlag(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
