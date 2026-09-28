// The response of the Animation asset creators the F4 port covers, recorded
// before it and held to the contract after it (plans/domain-design.md 0 and
// 8). Every asset is built on the engine's SkeletalCube. The fixture is
// tests/golden/cpp/animation-create-responses.json; a run without it records it.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerUtils.h"
#include "Handlers/Animation/AnimationHandlers.h"
#include "Tests/MCPCharacterisationRun.h"
#include "Tests/MCPScopedTestMount.h"

class FMCPAnimationCreateCharTestBase : public FAutomationTestBase
{
public:
	FMCPAnimationCreateCharTestBase(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{
	}

	virtual TArray<FString> GetSuppressedLogCategories() override
	{
		TArray<FString> Categories = FAutomationTestBase::GetSuppressedLogCategories();
		Categories.Add(TEXT("LogAnimation"));
		Categories.Add(TEXT("LogBlueprint"));
		Categories.Add(TEXT("LogK2Compiler"));
		Categories.Add(TEXT("LogPoseSearch"));
		Categories.Add(TEXT("LogIKRig"));
		Categories.Add(TEXT("LogUObjectGlobals"));
		Categories.Add(TEXT("LogLinker"));
		Categories.Add(TEXT("LogUE_MCP"));
		return Categories;
	}
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FMCPAnimationCreateCharacterisationTest, FMCPAnimationCreateCharTestBase,
	"UE.MCP.Animation.Characterisation.Create",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAnimationCreateCharacterisationTest::RunTest(const FString& Parameters)
{
	using namespace UEMCPCharacterisation;

	FMCPScopedTestMount Mount(TEXT("/UEMCPAnimCreateChar/"), TEXT("UEMCPAnimCreateChar"));
	const FString Folder = Mount.RootPath.LeftChop(1);
	const FString Skeleton = TEXT("/Engine/EngineMeshes/SkeletalCube_Skeleton");
	const FString Mesh = TEXT("/Engine/EngineMeshes/SkeletalCube");
	const FString Missing = Mount.RootPath + TEXT("Nope");

	FScriptedRun Run;
	FAnimationHandlers::RegisterHandlers(Run.Registry);
	Run.Recording.Normaliser.Replace(Mount.RootPath, TEXT("/<root>/"));

	auto At = [&](const TCHAR* Name) { return FArgs().S(TEXT("name"), Name).S(TEXT("packagePath"), Folder); };
	auto OnSkeleton = [&](const TCHAR* Name) { return At(Name).S(TEXT("skeletonPath"), Skeleton); };

	// Each creator: created, rerun, and its missing-input refusal.
	auto Triple = [&](const TCHAR* Method, const FArgs& Args, const FArgs& Bad)
	{
		Run.Step(*(FString(Method) + TEXT("_01_created")), Method, Args);
		Run.Step(*(FString(Method) + TEXT("_02_existed")), Method, Args);
		Run.Step(*(FString(Method) + TEXT("_03_refused")), Method, Bad);
	};

	Triple(TEXT("create_sequence"), OnSkeleton(TEXT("AS_Char")).N(TEXT("numFrames"), 30).N(TEXT("frameRate"), 30),
		At(TEXT("AS_Bad")).S(TEXT("skeletonPath"), Missing));
	Triple(TEXT("create_anim_montage"), At(TEXT("AM_Char")).S(TEXT("animSequencePath"), Mount.RootPath + TEXT("AS_Char")),
		At(TEXT("AM_Bad")).S(TEXT("animSequencePath"), Missing));
	Triple(TEXT("create_anim_blueprint"), OnSkeleton(TEXT("ABP_Char")),
		At(TEXT("ABP_Bad")).S(TEXT("skeletonPath"), Missing));
	Run.Step(TEXT("create_anim_blueprint_04_bad_parent"), TEXT("create_anim_blueprint"), OnSkeleton(TEXT("ABP_BadParent")).S(TEXT("parentClass"), TEXT("Actor")));
	Triple(TEXT("create_blendspace"), OnSkeleton(TEXT("BS_Char")).S(TEXT("axisHorizontal"), TEXT("Speed")).N(TEXT("horizontalMax"), 600),
		At(TEXT("BS_Bad")).S(TEXT("skeletonPath"), Missing));
	Triple(TEXT("create_blendspace_1d"), OnSkeleton(TEXT("BS1_Char")).S(TEXT("axisName"), TEXT("Speed")).N(TEXT("axisMax"), 600),
		At(TEXT("BS1_Bad")).S(TEXT("skeletonPath"), Missing));
	Triple(TEXT("create_anim_composite"), OnSkeleton(TEXT("AC_Char")),
		At(TEXT("AC_Bad")).S(TEXT("skeletonPath"), Missing));
	Triple(TEXT("create_ik_rig"), At(TEXT("IK_Char")).S(TEXT("skeletalMeshPath"), Mesh),
		At(TEXT("IK_Bad")).S(TEXT("skeletalMeshPath"), Missing));
	Triple(TEXT("create_mirror_data_table"), OnSkeleton(TEXT("MDT_Char")),
		OnSkeleton(TEXT("MDT_Bad")).S(TEXT("mirrorAxis"), TEXT("W")));
	Triple(TEXT("create_pose_search_schema"), OnSkeleton(TEXT("PSS_Char")),
		At(TEXT("PSS_Bad")).S(TEXT("skeletonPath"), Missing));
	Triple(TEXT("create_pose_search_database"), At(TEXT("PSD_Char")).S(TEXT("schemaPath"), Mount.RootPath + TEXT("PSS_Char")),
		At(TEXT("PSD_Bad")).S(TEXT("schemaPath"), Missing));
	Triple(TEXT("create_pose_search_normalization_set"), At(TEXT("PSN_Char")).A(TEXT("databases"), Strings({ *(Mount.RootPath + TEXT("PSD_Char")) })),
		At(TEXT("PSN_Bad")).A(TEXT("databases"), Strings({ *Missing })));
	Run.Step(TEXT("create_sequence_04_conflict_error"), TEXT("create_sequence"), OnSkeleton(TEXT("AS_Char")).S(TEXT("onConflict"), TEXT("error")));
	Run.Step(TEXT("create_sequence_05_missing_name"), TEXT("create_sequence"), FArgs().S(TEXT("skeletonPath"), Skeleton));

	return Run.Finish(*this, TEXT("animation-create-responses"));
}

#endif // WITH_DEV_AUTOMATION_TESTS
