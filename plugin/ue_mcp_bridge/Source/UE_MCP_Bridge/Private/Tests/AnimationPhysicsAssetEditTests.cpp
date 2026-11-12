// Coverage for animation(set_physics_asset_constraint_limits) and
// animation(set_physics_asset_body_material) (#1554).
//
// The probe physics asset and physical materials live under a private mount and
// every call passes save=false, so nothing touches the attached project's content.
// The assertions read the templates and bodies directly: the limit writes land on
// both the ProfileInstance and the profile the template serializes, locked axes
// keep their degrees, the per-entry form and the rollback restore, an unknown name
// changes nothing, and body materials are set, cleared and restored.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Animation/AnimationHandlers.h"
#include "Tests/MCPScopedTestMount.h"

#include "Misc/AutomationTest.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
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
	const TCHAR* const MCPPhysEditTestRoot = TEXT("/UEMCPPhysEditTest/");

	TSharedPtr<FJsonObject> MCPPhysEditRun(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Request);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
	}

	UPhysicsConstraintTemplate* MCPPhysEditAddConstraint(UPhysicsAsset* Asset, const TCHAR* Child, const TCHAR* Parent)
	{
		UPhysicsConstraintTemplate* Constraint = NewObject<UPhysicsConstraintTemplate>(Asset);
		Constraint->DefaultInstance.JointName = FName(Child);
		Constraint->DefaultInstance.ConstraintBone1 = FName(Child);
		Constraint->DefaultInstance.ConstraintBone2 = FName(Parent);
		FConstraintProfileProperties& Profile = Constraint->DefaultInstance.ProfileInstance;
		Profile.ConeLimit.Swing1Motion = EAngularConstraintMotion::ACM_Limited;
		Profile.ConeLimit.Swing2Motion = EAngularConstraintMotion::ACM_Limited;
		Profile.TwistLimit.TwistMotion = EAngularConstraintMotion::ACM_Limited;
		Profile.ConeLimit.Swing1LimitDegrees = 45.f;
		Profile.ConeLimit.Swing2LimitDegrees = 45.f;
		Profile.TwistLimit.TwistLimitDegrees = 45.f;
		Profile.ConeLimit.bSoftConstraint = true;
		Profile.TwistLimit.bSoftConstraint = true;
		Constraint->SetDefaultProfile(Constraint->DefaultInstance);
		Asset->ConstraintSetup.Add(Constraint);
		return Constraint;
	}

	USkeletalBodySetup* MCPPhysEditAddBody(UPhysicsAsset* Asset, const TCHAR* Bone)
	{
		USkeletalBodySetup* Body = NewObject<USkeletalBodySetup>(Asset);
		Body->BoneName = FName(Bone);
		Asset->SkeletalBodySetups.Add(Body);
		return Body;
	}

	TArray<TSharedPtr<FJsonValue>> MCPPhysEditNames(const TArray<FString>& Names)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& Name : Names) Out.Add(MakeShared<FJsonValueString>(Name));
		return Out;
	}

	TArray<TSharedPtr<FJsonValue>> MCPPhysEditOne(const TSharedPtr<FJsonObject>& Entry)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		Out.Add(MakeShared<FJsonValueObject>(Entry));
		return Out;
	}

	UPhysicalMaterial* MCPPhysEditMaterial(const FString& Path, const TCHAR* Name)
	{
		UPackage* Package = CreatePackage(*Path);
		return Package ? NewObject<UPhysicalMaterial>(Package, FName(Name), RF_Public | RF_Standalone) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimationPhysicsAssetEditTest,
	"UE.MCP.Animation.PhysicsAsset.Edit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationPhysicsAssetEditTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount(FString(MCPPhysEditTestRoot), TEXT("UEMCPPhysEditTest"));

	const FString ProbePath = FString(MCPPhysEditTestRoot) + TEXT("PHYS_EditProbe");
	const FString MaterialPath = FString(MCPPhysEditTestRoot) + TEXT("PM_EditProbe");

	UPackage* Package = CreatePackage(*ProbePath);
	TestNotNull(TEXT("probe package created"), Package);
	if (!Package)
	{
		return false;
	}
	UPhysicsAsset* Probe = NewObject<UPhysicsAsset>(Package, FName(TEXT("PHYS_EditProbe")), RF_Public | RF_Standalone);
	UPhysicalMaterial* Material = MCPPhysEditMaterial(MaterialPath, TEXT("PM_EditProbe"));
	TestNotNull(TEXT("probe physics asset created"), Probe);
	TestNotNull(TEXT("probe physical material created"), Material);
	if (!Probe || !Material)
	{
		Package->SetDirtyFlag(false);
		return false;
	}
	const FGCRootScope KeepProbeAlive(Probe);
	const FGCRootScope KeepMaterialAlive(Material);

	UPhysicsConstraintTemplate* Spine = MCPPhysEditAddConstraint(Probe, TEXT("spine_01"), TEXT("pelvis"));
	UPhysicsConstraintTemplate* Thigh = MCPPhysEditAddConstraint(Probe, TEXT("thigh_l"), TEXT("pelvis"));
	// A locked twist on the thigh: a degree write must leave it alone.
	Thigh->DefaultInstance.ProfileInstance.TwistLimit.TwistMotion = EAngularConstraintMotion::ACM_Locked;
	Thigh->SetDefaultProfile(Thigh->DefaultInstance);
	USkeletalBodySetup* Pelvis = MCPPhysEditAddBody(Probe, TEXT("pelvis"));
	USkeletalBodySetup* SpineBody = MCPPhysEditAddBody(Probe, TEXT("spine_01"));

	FMCPHandlerRegistry Registry;
	FAnimationHandlers::RegisterHandlers(Registry);

	const TCHAR* const SetLimits = TEXT("set_physics_asset_constraint_limits");
	const TCHAR* const SetMaterial = TEXT("set_physics_asset_body_material");

	// Shared values on every constraint: hard limits and new ranges.
	TSharedPtr<FJsonObject> Rollback;
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), ProbePath);
		Request->SetBoolField(TEXT("swingSoft"), false);
		Request->SetBoolField(TEXT("twistSoft"), false);
		Request->SetNumberField(TEXT("swing1LimitDegrees"), 20.0);
		Request->SetNumberField(TEXT("swing2LimitDegrees"), 15.0);
		Request->SetNumberField(TEXT("twistLimitDegrees"), 10.0);
		Request->SetBoolField(TEXT("save"), false);
		const TSharedPtr<FJsonObject> Response = MCPPhysEditRun(Registry, SetLimits, Request);
		TestTrue(TEXT("the limit write returns an object"), Response.IsValid());
		if (Response.IsValid())
		{
			TestTrue(TEXT("the limit write succeeds"), Response->GetBoolField(TEXT("success")));
			TestEqual(TEXT("both constraints are reported"), static_cast<int32>(Response->GetNumberField(TEXT("count"))), 2);
			TestEqual(TEXT("both constraints changed"), static_cast<int32>(Response->GetNumberField(TEXT("changedCount"))), 2);
			TestFalse(TEXT("save=false does not save"), Response->GetBoolField(TEXT("saved")));
			const TSharedPtr<FJsonObject>* RollbackObj = nullptr;
			TestTrue(TEXT("the write carries a rollback"), Response->TryGetObjectField(TEXT("rollback"), RollbackObj) && RollbackObj);
			if (RollbackObj) Rollback = *RollbackObj;
		}

		const FConstraintProfileProperties& SpineProfile = Spine->DefaultInstance.ProfileInstance;
		TestFalse(TEXT("the spine cone limit is hard"), SpineProfile.ConeLimit.bSoftConstraint);
		TestFalse(TEXT("the spine twist limit is hard"), SpineProfile.TwistLimit.bSoftConstraint);
		TestEqual(TEXT("spine swing1 degrees"), SpineProfile.ConeLimit.Swing1LimitDegrees, 20.f);
		TestEqual(TEXT("spine swing2 degrees"), SpineProfile.ConeLimit.Swing2LimitDegrees, 15.f);
		TestEqual(TEXT("spine twist degrees"), SpineProfile.TwistLimit.TwistLimitDegrees, 10.f);

		// The serialized profile follows, which is what makes the edit survive a save.
		const FConstraintProfileProperties& Saved = Spine->GetConstraintProfilePropertiesOrDefault(NAME_None);
		TestFalse(TEXT("the serialized cone limit is hard"), Saved.ConeLimit.bSoftConstraint);
		TestEqual(TEXT("the serialized swing1 degrees"), Saved.ConeLimit.Swing1LimitDegrees, 20.f);
		TestEqual(TEXT("the serialized twist degrees"), Saved.TwistLimit.TwistLimitDegrees, 10.f);

		const FConstraintProfileProperties& ThighProfile = Thigh->DefaultInstance.ProfileInstance;
		TestEqual(TEXT("the locked thigh twist keeps its degrees"), ThighProfile.TwistLimit.TwistLimitDegrees, 45.f);
		TestTrue(TEXT("the thigh twist stays locked"), ThighProfile.TwistLimit.TwistMotion == EAngularConstraintMotion::ACM_Locked);
		TestEqual(TEXT("the thigh swing1 is written"), ThighProfile.ConeLimit.Swing1LimitDegrees, 20.f);
	}

	// The rollback restores every touched constraint.
	if (Rollback.IsValid())
	{
		const TSharedPtr<FJsonObject> Payload = Rollback->GetObjectField(TEXT("payload"));
		TestEqual(TEXT("the rollback replays the same action"), Rollback->GetStringField(TEXT("method")), FString(SetLimits));
		const TSharedPtr<FJsonObject> Response = MCPPhysEditRun(Registry, SetLimits, Payload);
		if (Response.IsValid())
		{
			TestTrue(TEXT("the rollback succeeds"), Response->GetBoolField(TEXT("success")));
		}
		TestTrue(TEXT("the rollback restores the soft cone"), Spine->DefaultInstance.ProfileInstance.ConeLimit.bSoftConstraint);
		TestEqual(TEXT("the rollback restores swing1"), Spine->DefaultInstance.ProfileInstance.ConeLimit.Swing1LimitDegrees, 45.f);
		TestEqual(TEXT("the rollback restores the serialized swing1"),
			Spine->GetConstraintProfilePropertiesOrDefault(NAME_None).ConeLimit.Swing1LimitDegrees, 45.f);
	}

	// constraintNames scopes the shared values; a per-entry value applies after them.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), ProbePath);
		Request->SetArrayField(TEXT("constraintNames"), MCPPhysEditNames({ TEXT("spine_01") }));
		Request->SetNumberField(TEXT("swing1LimitDegrees"), 30.0);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("constraintName"), TEXT("thigh_l"));
		Entry->SetStringField(TEXT("swing2Motion"), TEXT("Locked"));
		Request->SetArrayField(TEXT("constraints"), MCPPhysEditOne(Entry));
		Request->SetBoolField(TEXT("save"), false);
		const TSharedPtr<FJsonObject> Response = MCPPhysEditRun(Registry, SetLimits, Request);
		if (Response.IsValid())
		{
			TestTrue(TEXT("the scoped write succeeds"), Response->GetBoolField(TEXT("success")));
		}
		TestEqual(TEXT("the named spine gets swing1"), Spine->DefaultInstance.ProfileInstance.ConeLimit.Swing1LimitDegrees, 30.f);
		TestEqual(TEXT("the unnamed thigh keeps swing1"), Thigh->DefaultInstance.ProfileInstance.ConeLimit.Swing1LimitDegrees, 45.f);
		TestTrue(TEXT("the per-entry motion lands on the thigh"),
			Thigh->DefaultInstance.ProfileInstance.ConeLimit.Swing2Motion == EAngularConstraintMotion::ACM_Locked);
	}

	// An unknown name and a bad value are refused, and change nothing.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), ProbePath);
		Request->SetArrayField(TEXT("constraintNames"), MCPPhysEditNames({ TEXT("spine_01"), TEXT("hand_r") }));
		Request->SetNumberField(TEXT("swing1LimitDegrees"), 5.0);
		Request->SetBoolField(TEXT("save"), false);
		const TSharedPtr<FJsonObject> Response = MCPPhysEditRun(Registry, SetLimits, Request);
		if (Response.IsValid())
		{
			TestFalse(TEXT("an unknown constraint is refused"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the refusal names it"), Response->GetStringField(TEXT("error")).Contains(TEXT("hand_r")));
		}
		TestEqual(TEXT("the refused call leaves the spine alone"), Spine->DefaultInstance.ProfileInstance.ConeLimit.Swing1LimitDegrees, 30.f);

		Request->RemoveField(TEXT("constraintNames"));
		Request->SetNumberField(TEXT("swing1LimitDegrees"), 200.0);
		const TSharedPtr<FJsonObject> Bad = MCPPhysEditRun(Registry, SetLimits, Request);
		if (Bad.IsValid())
		{
			TestFalse(TEXT("a range over 180 is refused"), Bad->GetBoolField(TEXT("success")));
		}
		Request->RemoveField(TEXT("swing1LimitDegrees"));
		Request->SetStringField(TEXT("twistMotion"), TEXT("Wobbly"));
		const TSharedPtr<FJsonObject> BadMotion = MCPPhysEditRun(Registry, SetLimits, Request);
		if (BadMotion.IsValid())
		{
			TestFalse(TEXT("an unknown motion is refused"), BadMotion->GetBoolField(TEXT("success")));
		}
	}

	// Body materials: every body, then a per-body clear, then the rollback.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), ProbePath);
		Request->SetStringField(TEXT("physicalMaterialPath"), MaterialPath);
		Request->SetBoolField(TEXT("save"), false);
		const TSharedPtr<FJsonObject> Response = MCPPhysEditRun(Registry, SetMaterial, Request);
		TSharedPtr<FJsonObject> MaterialRollback;
		if (Response.IsValid())
		{
			TestTrue(TEXT("the material write succeeds"), Response->GetBoolField(TEXT("success")));
			TestEqual(TEXT("both bodies changed"), static_cast<int32>(Response->GetNumberField(TEXT("changedCount"))), 2);
			const TSharedPtr<FJsonObject>* RollbackObj = nullptr;
			if (Response->TryGetObjectField(TEXT("rollback"), RollbackObj) && RollbackObj) MaterialRollback = *RollbackObj;
		}
		TestTrue(TEXT("the pelvis body has the material"), Pelvis->PhysMaterial.Get() == Material);
		TestTrue(TEXT("the spine body has the material"), SpineBody->PhysMaterial.Get() == Material);

		const TSharedPtr<FJsonObject> Again = MCPPhysEditRun(Registry, SetMaterial, Request);
		if (Again.IsValid())
		{
			TestTrue(TEXT("a repeat reports unchanged"), Again->GetBoolField(TEXT("unchanged")));
		}

		TSharedPtr<FJsonObject> Clear = MakeShared<FJsonObject>();
		Clear->SetStringField(TEXT("assetPath"), ProbePath);
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("boneName"), TEXT("spine_01"));
		Entry->SetStringField(TEXT("physicalMaterialPath"), TEXT(""));
		Clear->SetArrayField(TEXT("bodies"), MCPPhysEditOne(Entry));
		Clear->SetBoolField(TEXT("save"), false);
		const TSharedPtr<FJsonObject> Cleared = MCPPhysEditRun(Registry, SetMaterial, Clear);
		if (Cleared.IsValid())
		{
			TestTrue(TEXT("the per-body clear succeeds"), Cleared->GetBoolField(TEXT("success")));
		}
		TestNull(TEXT("the spine body is cleared"), SpineBody->PhysMaterial.Get());
		TestTrue(TEXT("the pelvis body keeps its material"), Pelvis->PhysMaterial.Get() == Material);

		TSharedPtr<FJsonObject> Unknown = MakeShared<FJsonObject>();
		Unknown->SetStringField(TEXT("assetPath"), ProbePath);
		Unknown->SetStringField(TEXT("physicalMaterialPath"), MaterialPath);
		Unknown->SetArrayField(TEXT("boneNames"), MCPPhysEditNames({ TEXT("hand_r") }));
		Unknown->SetBoolField(TEXT("save"), false);
		const TSharedPtr<FJsonObject> Refused = MCPPhysEditRun(Registry, SetMaterial, Unknown);
		if (Refused.IsValid())
		{
			TestFalse(TEXT("an unknown bone is refused"), Refused->GetBoolField(TEXT("success")));
		}
		TestNull(TEXT("the refused call leaves the spine body alone"), SpineBody->PhysMaterial.Get());

		if (MaterialRollback.IsValid())
		{
			const TSharedPtr<FJsonObject> Restored = MCPPhysEditRun(Registry, SetMaterial, MaterialRollback->GetObjectField(TEXT("payload")));
			if (Restored.IsValid())
			{
				TestTrue(TEXT("the material rollback succeeds"), Restored->GetBoolField(TEXT("success")));
			}
			TestNull(TEXT("the rollback clears the pelvis body"), Pelvis->PhysMaterial.Get());
		}
	}

	Package->SetDirtyFlag(false);
	if (UPackage* MaterialPackage = Material->GetOutermost())
	{
		MaterialPackage->SetDirtyFlag(false);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
