// Coverage for animation(get_physics_asset_constraints).
//
// The probe physics asset lives under a private mount, so nothing here touches
// the attached project's content. The assertions are about the contract: one
// row per constraint template carrying the motion types, limit degrees, soft
// limit, drive, flag and frame values read off its default instance; the bone
// filter; the row cap; and the missing-path and wrong-type forms of the address.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Animation/AnimationHandlers.h"
#include "Tests/MCPScopedTestMount.h"

#include "Misc/AutomationTest.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
// PhysicsEngine/SkeletalBodySetup.h is unavailable as a public include on
// UE 5.4. USkeletalBodySetup is still defined transitively via PhysicsAsset.h.
#if __has_include("PhysicsEngine/SkeletalBodySetup.h")
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif
#include "UObject/Package.h"

namespace
{
	const TCHAR* const MCPPhysConstraintsTestRoot = TEXT("/UEMCPPhysConstraintsTest/");

	TSharedPtr<FJsonObject> MCPPhysConstraintsRun(FMCPHandlerRegistry& Registry, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("get_physics_asset_constraints"), Request);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
	}

	UPhysicsConstraintTemplate* MCPPhysConstraintsAdd(UPhysicsAsset* Asset, const TCHAR* Child, const TCHAR* Parent)
	{
		UPhysicsConstraintTemplate* Constraint = NewObject<UPhysicsConstraintTemplate>(Asset);
		Constraint->DefaultInstance.JointName = FName(Child);
		Constraint->DefaultInstance.ConstraintBone1 = FName(Child);
		Constraint->DefaultInstance.ConstraintBone2 = FName(Parent);
		Asset->ConstraintSetup.Add(Constraint);
		return Constraint;
	}

	/** The one row whose constraintName is Name, or null. */
	TSharedPtr<FJsonObject> MCPPhysConstraintsRow(const TSharedPtr<FJsonObject>& Response, const TCHAR* Name)
	{
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("constraints"), Rows)) return nullptr;
		for (const TSharedPtr<FJsonValue>& Row : *Rows)
		{
			const TSharedPtr<FJsonObject> Obj = Row->AsObject();
			if (Obj.IsValid() && Obj->GetStringField(TEXT("constraintName")) == Name) return Obj;
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimationPhysicsAssetConstraintsTest,
	"UE.MCP.Animation.PhysicsAsset.Constraints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationPhysicsAssetConstraintsTest::RunTest(const FString& Parameters)
{
	// The missing path below is deliberate and the engine loader logs an Error
	// for it. Count 0 means it must occur.
	AddExpectedError(TEXT("LoadAsset failed"), EAutomationExpectedErrorFlags::Contains, 0);

	const FMCPScopedTestMount Mount(FString(MCPPhysConstraintsTestRoot), TEXT("UEMCPPhysConstraintsTest"));

	const FString ProbePath = FString(MCPPhysConstraintsTestRoot) + TEXT("PHYS_Probe");
	const FString MissingPath = FString(MCPPhysConstraintsTestRoot) + TEXT("PHYS_NotThere");

	UPackage* Package = CreatePackage(*ProbePath);
	TestNotNull(TEXT("probe package created"), Package);
	if (!Package)
	{
		return false;
	}
	UPhysicsAsset* Probe = NewObject<UPhysicsAsset>(Package, FName(TEXT("PHYS_Probe")), RF_Public | RF_Standalone);
	TestNotNull(TEXT("probe physics asset created"), Probe);
	if (!Probe)
	{
		Package->SetDirtyFlag(false);
		return false;
	}
	const FGCRootScope KeepProbeAlive(Probe);

	// One body, so bodyCount is not just the default zero.
	USkeletalBodySetup* Body = NewObject<USkeletalBodySetup>(Probe);
	Body->BoneName = FName(TEXT("pelvis"));
	Probe->SkeletalBodySetups.Add(Body);

	// spine_01 -> pelvis: a limited cone, locked twist, soft swing, parent
	// dominates, and a child frame pushed off the origin.
	{
		UPhysicsConstraintTemplate* Spine = MCPPhysConstraintsAdd(Probe, TEXT("spine_01"), TEXT("pelvis"));
		FConstraintProfileProperties& Profile = Spine->DefaultInstance.ProfileInstance;
		Profile.ConeLimit.Swing1Motion = EAngularConstraintMotion::ACM_Limited;
		Profile.ConeLimit.Swing2Motion = EAngularConstraintMotion::ACM_Free;
		Profile.ConeLimit.Swing1LimitDegrees = 30.f;
		Profile.ConeLimit.Swing2LimitDegrees = 15.f;
		Profile.ConeLimit.bSoftConstraint = true;
		Profile.ConeLimit.Stiffness = 50.f;
		Profile.ConeLimit.Damping = 5.f;
		Profile.TwistLimit.TwistMotion = EAngularConstraintMotion::ACM_Locked;
		Profile.TwistLimit.TwistLimitDegrees = 10.f;
		Profile.LinearLimit.XMotion = ELinearConstraintMotion::LCM_Limited;
		Profile.LinearLimit.Limit = 2.5f;
		Profile.bParentDominates = true;
		Profile.bDisableCollision = false;
		Profile.AngularDrive.SlerpDrive.bEnablePositionDrive = true;
		Profile.AngularDrive.AngularDriveMode = EAngularDriveMode::SLERP;
		Spine->DefaultInstance.Pos1 = FVector(1.0, 2.0, 3.0);
	}
	// thigh_l -> pelvis: engine defaults, collision disabled.
	{
		UPhysicsConstraintTemplate* Thigh = MCPPhysConstraintsAdd(Probe, TEXT("thigh_l"), TEXT("pelvis"));
		Thigh->DefaultInstance.ProfileInstance.bDisableCollision = true;
	}

	FMCPHandlerRegistry Registry;
	FAnimationHandlers::RegisterHandlers(Registry);

	// The full read: one row per template, values read off the default instance.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), ProbePath);
		const TSharedPtr<FJsonObject> Response = MCPPhysConstraintsRun(Registry, Request);
		TestTrue(TEXT("a read returns an object"), Response.IsValid());
		if (Response.IsValid())
		{
			TestTrue(TEXT("the read succeeds"), Response->GetBoolField(TEXT("success")));
			TestEqual(TEXT("bodyCount counts the one body"), static_cast<int32>(Response->GetNumberField(TEXT("bodyCount"))), 1);
			TestEqual(TEXT("constraintCount counts both templates"), static_cast<int32>(Response->GetNumberField(TEXT("constraintCount"))), 2);
			TestEqual(TEXT("count returns both rows"), static_cast<int32>(Response->GetNumberField(TEXT("count"))), 2);
			TestEqual(TEXT("total matches both rows"), static_cast<int32>(Response->GetNumberField(TEXT("total"))), 2);
			TestFalse(TEXT("an uncapped read is not truncated"), Response->GetBoolField(TEXT("truncated")));
			TestTrue(TEXT("the preview mesh path is reported even when unset"), Response->HasField(TEXT("previewSkeletalMeshPath")));

			const TSharedPtr<FJsonObject> Spine = MCPPhysConstraintsRow(Response, TEXT("spine_01"));
			TestTrue(TEXT("the spine row is present"), Spine.IsValid());
			if (Spine.IsValid())
			{
				TestEqual(TEXT("index is the ConstraintSetup position"), static_cast<int32>(Spine->GetNumberField(TEXT("index"))), 0);
				TestEqual(TEXT("bone1 is the child"), Spine->GetStringField(TEXT("bone1")), FString(TEXT("spine_01")));
				TestEqual(TEXT("bone2 is the parent"), Spine->GetStringField(TEXT("bone2")), FString(TEXT("pelvis")));

				const TSharedPtr<FJsonObject> Swing = Spine->GetObjectField(TEXT("swing"));
				TestEqual(TEXT("swing1 is Limited"), Swing->GetStringField(TEXT("swing1Motion")), FString(TEXT("Limited")));
				TestEqual(TEXT("swing2 is Free"), Swing->GetStringField(TEXT("swing2Motion")), FString(TEXT("Free")));
				TestEqual(TEXT("swing1 limit degrees"), Swing->GetNumberField(TEXT("swing1LimitDegrees")), 30.0);
				TestEqual(TEXT("swing2 limit degrees"), Swing->GetNumberField(TEXT("swing2LimitDegrees")), 15.0);
				TestTrue(TEXT("the swing limit is soft"), Swing->GetBoolField(TEXT("soft")));
				TestEqual(TEXT("swing stiffness"), Swing->GetNumberField(TEXT("stiffness")), 50.0);
				TestEqual(TEXT("swing damping"), Swing->GetNumberField(TEXT("damping")), 5.0);

				const TSharedPtr<FJsonObject> Twist = Spine->GetObjectField(TEXT("twist"));
				TestEqual(TEXT("twist is Locked"), Twist->GetStringField(TEXT("twistMotion")), FString(TEXT("Locked")));
				TestEqual(TEXT("twist limit degrees"), Twist->GetNumberField(TEXT("twistLimitDegrees")), 10.0);

				const TSharedPtr<FJsonObject> Linear = Spine->GetObjectField(TEXT("linear"));
				TestEqual(TEXT("linear x is Limited"), Linear->GetStringField(TEXT("xMotion")), FString(TEXT("Limited")));
				TestEqual(TEXT("linear y is Locked"), Linear->GetStringField(TEXT("yMotion")), FString(TEXT("Locked")));
				TestEqual(TEXT("linear limit"), Linear->GetNumberField(TEXT("limit")), 2.5);

				TestTrue(TEXT("parentDominates is read"), Spine->GetBoolField(TEXT("parentDominates")));
				TestFalse(TEXT("disableCollision is read"), Spine->GetBoolField(TEXT("disableCollision")));

				const TSharedPtr<FJsonObject> AngularDrive = Spine->GetObjectField(TEXT("angularDrive"));
				TestTrue(TEXT("the SLERP orientation drive is enabled"), AngularDrive->GetBoolField(TEXT("orientationDriveEnabled")));
				TestEqual(TEXT("the angular drive mode"), AngularDrive->GetStringField(TEXT("mode")), FString(TEXT("SLERP")));
				const TSharedPtr<FJsonObject> LinearDrive = Spine->GetObjectField(TEXT("linearDrive"));
				TestFalse(TEXT("the linear drive is off"), LinearDrive->GetBoolField(TEXT("positionDriveEnabled")));

				const TSharedPtr<FJsonObject> Frame1 = Spine->GetObjectField(TEXT("frame1"));
				TestEqual(TEXT("frame1 is in the child's space"), Frame1->GetStringField(TEXT("bone")), FString(TEXT("spine_01")));
				const TSharedPtr<FJsonObject> Position = Frame1->GetObjectField(TEXT("position"));
				TestEqual(TEXT("frame1 position x"), Position->GetNumberField(TEXT("x")), 1.0);
				TestEqual(TEXT("frame1 position y"), Position->GetNumberField(TEXT("y")), 2.0);
				TestEqual(TEXT("frame1 position z"), Position->GetNumberField(TEXT("z")), 3.0);
				TestTrue(TEXT("frame1 carries a rotation"), Frame1->HasField(TEXT("rotation")));
				const TSharedPtr<FJsonObject> Frame2 = Spine->GetObjectField(TEXT("frame2"));
				TestEqual(TEXT("frame2 is in the parent's space"), Frame2->GetStringField(TEXT("bone")), FString(TEXT("pelvis")));
			}

			const TSharedPtr<FJsonObject> Thigh = MCPPhysConstraintsRow(Response, TEXT("thigh_l"));
			TestTrue(TEXT("the thigh row is present"), Thigh.IsValid());
			if (Thigh.IsValid())
			{
				TestTrue(TEXT("disableCollision is read on the thigh"), Thigh->GetBoolField(TEXT("disableCollision")));
				TestFalse(TEXT("parentDominates defaults off"), Thigh->GetBoolField(TEXT("parentDominates")));
			}
		}
	}

	// boneName keeps the constraints touching that bone: the parent matches
	// both, the child matches one.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), ProbePath);
		Request->SetStringField(TEXT("boneName"), TEXT("pelvis"));
		const TSharedPtr<FJsonObject> Both = MCPPhysConstraintsRun(Registry, Request);
		if (Both.IsValid())
		{
			TestEqual(TEXT("the parent bone matches both"), static_cast<int32>(Both->GetNumberField(TEXT("count"))), 2);
		}
		Request->SetStringField(TEXT("boneName"), TEXT("thigh_l"));
		const TSharedPtr<FJsonObject> One = MCPPhysConstraintsRun(Registry, Request);
		if (One.IsValid())
		{
			TestEqual(TEXT("the child bone matches one"), static_cast<int32>(One->GetNumberField(TEXT("count"))), 1);
			TestTrue(TEXT("and it is the thigh"), MCPPhysConstraintsRow(One, TEXT("thigh_l")).IsValid());
		}
		Request->SetStringField(TEXT("boneName"), TEXT("hand_r"));
		const TSharedPtr<FJsonObject> None = MCPPhysConstraintsRun(Registry, Request);
		if (None.IsValid())
		{
			TestTrue(TEXT("a bone with no constraint still succeeds"), None->GetBoolField(TEXT("success")));
			TestEqual(TEXT("and returns no rows"), static_cast<int32>(None->GetNumberField(TEXT("count"))), 0);
		}
	}

	// limit caps the rows; total still counts every match.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), ProbePath);
		Request->SetNumberField(TEXT("limit"), 1);
		const TSharedPtr<FJsonObject> Response = MCPPhysConstraintsRun(Registry, Request);
		if (Response.IsValid())
		{
			TestEqual(TEXT("limit 1 returns one row"), static_cast<int32>(Response->GetNumberField(TEXT("count"))), 1);
			TestEqual(TEXT("total still counts both"), static_cast<int32>(Response->GetNumberField(TEXT("total"))), 2);
			TestTrue(TEXT("the capped read says so"), Response->GetBoolField(TEXT("truncated")));
		}
		Request->SetNumberField(TEXT("limit"), -1);
		const TSharedPtr<FJsonObject> Bad = MCPPhysConstraintsRun(Registry, Request);
		if (Bad.IsValid())
		{
			TestFalse(TEXT("a negative limit is refused"), Bad->GetBoolField(TEXT("success")));
		}
	}

	// A missing path fails and names the path.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), MissingPath);
		const TSharedPtr<FJsonObject> Response = MCPPhysConstraintsRun(Registry, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("a missing path fails"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the miss names the path"), Response->GetStringField(TEXT("error")).Contains(TEXT("PHYS_NotThere")));
		}
	}

	Package->SetDirtyFlag(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
