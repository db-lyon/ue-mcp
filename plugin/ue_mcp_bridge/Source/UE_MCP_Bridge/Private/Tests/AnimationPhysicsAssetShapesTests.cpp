// Coverage for animation(get_physics_asset_bodies) and
// animation(set_physics_asset_body_shapes).
//
// The probe physics asset lives under a private mount and every write passes
// save=false, so nothing touches the attached project's content. The assertions
// read the bodies' AggGeom directly: the read reports every shape in bone space,
// a dry run changes nothing, replace and append land as asked, kinds a call does
// not name (and tapered capsules) are kept, invalid shapes and unknown bones are
// refused before any write, and the rollback restores the previous shapes.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Animation/AnimationHandlers.h"
#include "Tests/MCPScopedTestMount.h"

#include "Misc/AutomationTest.h"
#include "PhysicsEngine/PhysicsAsset.h"
// PhysicsEngine/SkeletalBodySetup.h is unavailable as a public include on
// UE 5.4. USkeletalBodySetup is still defined transitively via PhysicsAsset.h.
#if __has_include("PhysicsEngine/SkeletalBodySetup.h")
#include "PhysicsEngine/SkeletalBodySetup.h"
#endif
#include "UObject/Package.h"

namespace
{
	const TCHAR* const MCPPhysShapesTestRoot = TEXT("/UEMCPPhysShapesTest/");

	TSharedPtr<FJsonObject> MCPPhysShapesRun(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Request);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
	}

	USkeletalBodySetup* MCPPhysShapesAddBody(UPhysicsAsset* Asset, const TCHAR* Bone)
	{
		USkeletalBodySetup* Body = NewObject<USkeletalBodySetup>(Asset);
		Body->BoneName = FName(Bone);
		Asset->SkeletalBodySetups.Add(Body);
		return Body;
	}

	TSharedPtr<FJsonObject> MCPPhysShapesVec(double X, double Y, double Z)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), X);
		Obj->SetNumberField(TEXT("y"), Y);
		Obj->SetNumberField(TEXT("z"), Z);
		return Obj;
	}

	TSharedPtr<FJsonObject> MCPPhysShapesSphyl(double Radius, double Length, const TSharedPtr<FJsonObject>& Center = nullptr, double Pitch = 0.0)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("radius"), Radius);
		Obj->SetNumberField(TEXT("length"), Length);
		if (Center.IsValid()) Obj->SetObjectField(TEXT("center"), Center);
		TSharedPtr<FJsonObject> Rotation = MakeShared<FJsonObject>();
		Rotation->SetNumberField(TEXT("pitch"), Pitch);
		Rotation->SetNumberField(TEXT("yaw"), 0.0);
		Rotation->SetNumberField(TEXT("roll"), 0.0);
		Obj->SetObjectField(TEXT("rotation"), Rotation);
		return Obj;
	}

	TArray<TSharedPtr<FJsonValue>> MCPPhysShapesList(const TArray<TSharedPtr<FJsonObject>>& Items)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const TSharedPtr<FJsonObject>& Item : Items) Out.Add(MakeShared<FJsonValueObject>(Item));
		return Out;
	}

	TSharedPtr<FJsonObject> MCPPhysShapesRequest(const FString& AssetPath, const TArray<TSharedPtr<FJsonObject>>& Bodies)
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), AssetPath);
		Request->SetArrayField(TEXT("bodyShapes"), MCPPhysShapesList(Bodies));
		Request->SetBoolField(TEXT("save"), false);
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimationPhysicsAssetShapesTest,
	"UE.MCP.Animation.PhysicsAsset.Shapes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAnimationPhysicsAssetShapesTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount(FString(MCPPhysShapesTestRoot), TEXT("UEMCPPhysShapesTest"));

	const FString ProbePath = FString(MCPPhysShapesTestRoot) + TEXT("PHYS_ShapesProbe");
	UPackage* Package = CreatePackage(*ProbePath);
	TestNotNull(TEXT("probe package created"), Package);
	if (!Package)
	{
		return false;
	}
	UPhysicsAsset* Probe = NewObject<UPhysicsAsset>(Package, FName(TEXT("PHYS_ShapesProbe")), RF_Public | RF_Standalone);
	TestNotNull(TEXT("probe physics asset created"), Probe);
	if (!Probe)
	{
		Package->SetDirtyFlag(false);
		return false;
	}
	const FGCRootScope KeepProbeAlive(Probe);

	// head: one capsule and a tapered capsule; pelvis: one box; hand_r: no shapes.
	USkeletalBodySetup* Head = MCPPhysShapesAddBody(Probe, TEXT("head"));
	FKSphylElem HeadCapsule;
	HeadCapsule.Radius = 8.f;
	HeadCapsule.Length = 4.f;
	Head->AggGeom.SphylElems.Add(HeadCapsule);
	FKTaperedCapsuleElem Tapered;
	Tapered.Radius0 = 3.f;
	Tapered.Radius1 = 2.f;
	Tapered.Length = 5.f;
	Head->AggGeom.TaperedCapsuleElems.Add(Tapered);
	USkeletalBodySetup* Pelvis = MCPPhysShapesAddBody(Probe, TEXT("pelvis"));
	FKBoxElem PelvisBox(20.f, 30.f, 10.f);
	Pelvis->AggGeom.BoxElems.Add(PelvisBox);
	MCPPhysShapesAddBody(Probe, TEXT("hand_r"));

	FMCPHandlerRegistry Registry;
	FAnimationHandlers::RegisterHandlers(Registry);

	const TCHAR* const GetBodies = TEXT("get_physics_asset_bodies");
	const TCHAR* const SetShapes = TEXT("set_physics_asset_body_shapes");

	// The read reports every body and every shape in bone space.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("assetPath"), ProbePath);
		const TSharedPtr<FJsonObject> Response = MCPPhysShapesRun(Registry, GetBodies, Request);
		TestTrue(TEXT("the read returns an object"), Response.IsValid());
		if (Response.IsValid())
		{
			TestTrue(TEXT("the read succeeds"), Response->GetBoolField(TEXT("success")));
			TestEqual(TEXT("every body is listed"), static_cast<int32>(Response->GetNumberField(TEXT("count"))), 3);
			const TArray<TSharedPtr<FJsonValue>>& Bodies = Response->GetArrayField(TEXT("bodies"));
			if (Bodies.Num() == 3)
			{
				const TSharedPtr<FJsonObject> HeadRow = Bodies[0]->AsObject();
				TestEqual(TEXT("the first row is the head"), HeadRow->GetStringField(TEXT("boneName")), FString(TEXT("head")));
				const TArray<TSharedPtr<FJsonValue>>& Sphyls = HeadRow->GetArrayField(TEXT("sphyls"));
				TestEqual(TEXT("the head has one capsule"), Sphyls.Num(), 1);
				if (Sphyls.Num() == 1)
				{
					TestEqual(TEXT("the capsule radius is read"), Sphyls[0]->AsObject()->GetNumberField(TEXT("radius")), 8.0);
					TestEqual(TEXT("the capsule length is read"), Sphyls[0]->AsObject()->GetNumberField(TEXT("length")), 4.0);
				}
				TestEqual(TEXT("the tapered capsule is read"), HeadRow->GetArrayField(TEXT("taperedCapsules")).Num(), 1);
				TestTrue(TEXT("the head has bounds"), HeadRow->HasTypedField<EJson::Object>(TEXT("bounds")));
				const TSharedPtr<FJsonObject> PelvisRow = Bodies[1]->AsObject();
				const TArray<TSharedPtr<FJsonValue>>& Boxes = PelvisRow->GetArrayField(TEXT("boxes"));
				TestEqual(TEXT("the pelvis has one box"), Boxes.Num(), 1);
				if (Boxes.Num() == 1)
				{
					TestEqual(TEXT("the box y extent is read"), Boxes[0]->AsObject()->GetNumberField(TEXT("y")), 30.0);
				}
				TestTrue(TEXT("a body with no shapes has null bounds"), Bodies[2]->AsObject()->HasTypedField<EJson::Null>(TEXT("bounds")));
			}
		}

		TArray<TSharedPtr<FJsonValue>> Names;
		Names.Add(MakeShared<FJsonValueString>(TEXT("pelvis")));
		Request->SetArrayField(TEXT("boneNames"), Names);
		const TSharedPtr<FJsonObject> Filtered = MCPPhysShapesRun(Registry, GetBodies, Request);
		if (Filtered.IsValid())
		{
			TestEqual(TEXT("boneNames keeps one body"), static_cast<int32>(Filtered->GetNumberField(TEXT("count"))), 1);
		}
		Names.Add(MakeShared<FJsonValueString>(TEXT("tail_01")));
		Request->SetArrayField(TEXT("boneNames"), Names);
		const TSharedPtr<FJsonObject> Unknown = MCPPhysShapesRun(Registry, GetBodies, Request);
		if (Unknown.IsValid())
		{
			TestFalse(TEXT("an unknown bone is refused on read"), Unknown->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the refusal names it"), Unknown->GetStringField(TEXT("error")).Contains(TEXT("tail_01")));
		}
	}

	// The head edit from the gap that prompted these actions: a larger head capsule and a neck capsule.
	auto HeadEntry = []()
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("boneName"), TEXT("head"));
		Entry->SetArrayField(TEXT("sphyls"), MCPPhysShapesList({
			MCPPhysShapesSphyl(9.5, 4.0),
			MCPPhysShapesSphyl(5.5, 6.0, MCPPhysShapesVec(-4.0, 0.0, 0.0), 90.0),
		}));
		return Entry;
	};

	// A dry run reports the result and changes nothing.
	{
		TSharedPtr<FJsonObject> Request = MCPPhysShapesRequest(ProbePath, { HeadEntry() });
		Request->SetBoolField(TEXT("dryRun"), true);
		const TSharedPtr<FJsonObject> Response = MCPPhysShapesRun(Registry, SetShapes, Request);
		if (Response.IsValid())
		{
			TestTrue(TEXT("the dry run succeeds"), Response->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the dry run says so"), Response->GetBoolField(TEXT("dryRun")));
			TestFalse(TEXT("the dry run carries no rollback"), Response->HasField(TEXT("rollback")));
			const TArray<TSharedPtr<FJsonValue>>& Rows = Response->GetArrayField(TEXT("bodies"));
			if (Rows.Num() == 1)
			{
				TestTrue(TEXT("the dry run reports the change"), Rows[0]->AsObject()->GetBoolField(TEXT("wouldChange")));
				TestEqual(TEXT("the dry run shows both capsules"), Rows[0]->AsObject()->GetArrayField(TEXT("sphyls")).Num(), 2);
			}
		}
		TestEqual(TEXT("the dry run leaves the head with one capsule"), Head->AggGeom.SphylElems.Num(), 1);
	}

	// The write replaces the head capsules, keeps the tapered capsule and returns a rollback.
	TSharedPtr<FJsonObject> Rollback;
	{
		const TSharedPtr<FJsonObject> Response = MCPPhysShapesRun(Registry, SetShapes, MCPPhysShapesRequest(ProbePath, { HeadEntry() }));
		TestTrue(TEXT("the write returns an object"), Response.IsValid());
		if (Response.IsValid())
		{
			TestTrue(TEXT("the write succeeds"), Response->GetBoolField(TEXT("success")));
			TestEqual(TEXT("one body changed"), static_cast<int32>(Response->GetNumberField(TEXT("changedCount"))), 1);
			TestFalse(TEXT("save=false does not save"), Response->GetBoolField(TEXT("saved")));
			const TArray<TSharedPtr<FJsonValue>>& Rows = Response->GetArrayField(TEXT("bodies"));
			if (Rows.Num() == 1)
			{
				TestTrue(TEXT("the write reads back what it wrote"), Rows[0]->AsObject()->GetBoolField(TEXT("verified")));
			}
			const TSharedPtr<FJsonObject>* RollbackObj = nullptr;
			TestTrue(TEXT("the write carries a rollback"), Response->TryGetObjectField(TEXT("rollback"), RollbackObj) && RollbackObj);
			if (RollbackObj) Rollback = *RollbackObj;
		}
		TestEqual(TEXT("the head has two capsules"), Head->AggGeom.SphylElems.Num(), 2);
		if (Head->AggGeom.SphylElems.Num() == 2)
		{
			const FKSphylElem& HeadNow = Head->AggGeom.SphylElems[0];
			const FKSphylElem& Neck = Head->AggGeom.SphylElems[1];
			TestEqual(TEXT("the head capsule radius"), HeadNow.Radius, 9.5f);
			TestEqual(TEXT("the neck capsule radius"), Neck.Radius, 5.5f);
			TestEqual(TEXT("the neck capsule length"), Neck.Length, 6.f);
			TestTrue(TEXT("the neck capsule centre"), Neck.Center.Equals(FVector(-4.0, 0.0, 0.0)));
			TestTrue(TEXT("the neck capsule pitch"), FMath::IsNearlyEqual(Neck.Rotation.Pitch, 90.0, 0.001));
		}
		TestEqual(TEXT("the tapered capsule is kept"), Head->AggGeom.TaperedCapsuleElems.Num(), 1);

		// A repeat is unchanged and carries no rollback.
		const TSharedPtr<FJsonObject> Again = MCPPhysShapesRun(Registry, SetShapes, MCPPhysShapesRequest(ProbePath, { HeadEntry() }));
		if (Again.IsValid())
		{
			TestTrue(TEXT("a repeat reports unchanged"), Again->GetBoolField(TEXT("unchanged")));
			TestFalse(TEXT("a repeat carries no rollback"), Again->HasField(TEXT("rollback")));
		}
	}

	// append adds after the existing shapes; a kind the call does not name is kept.
	{
		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("boneName"), TEXT("pelvis"));
		Entry->SetStringField(TEXT("mode"), TEXT("append"));
		TSharedPtr<FJsonObject> Box = MakeShared<FJsonObject>();
		Box->SetObjectField(TEXT("center"), MCPPhysShapesVec(0.0, 0.0, 12.0));
		Box->SetNumberField(TEXT("x"), 5.0);
		Box->SetNumberField(TEXT("y"), 6.0);
		Box->SetNumberField(TEXT("z"), 7.0);
		Box->SetStringField(TEXT("name"), TEXT("belt"));
		Entry->SetArrayField(TEXT("boxes"), MCPPhysShapesList({ Box }));
		const TSharedPtr<FJsonObject> Response = MCPPhysShapesRun(Registry, SetShapes, MCPPhysShapesRequest(ProbePath, { Entry }));
		if (Response.IsValid())
		{
			TestTrue(TEXT("the append succeeds"), Response->GetBoolField(TEXT("success")));
		}
		TestEqual(TEXT("the pelvis has two boxes"), Pelvis->AggGeom.BoxElems.Num(), 2);
		if (Pelvis->AggGeom.BoxElems.Num() == 2)
		{
			TestEqual(TEXT("the first box is kept"), Pelvis->AggGeom.BoxElems[0].Y, 30.f);
			TestTrue(TEXT("the appended box is named"), Pelvis->AggGeom.BoxElems[1].GetName() == FName(TEXT("belt")));
		}
	}

	// Invalid shapes and unknown bones are refused before anything changes.
	{
		TSharedPtr<FJsonObject> BadEntry = MakeShared<FJsonObject>();
		BadEntry->SetStringField(TEXT("boneName"), TEXT("hand_r"));
		BadEntry->SetArrayField(TEXT("sphyls"), MCPPhysShapesList({ MCPPhysShapesSphyl(-1.0, 2.0) }));
		const TSharedPtr<FJsonObject> Negative = MCPPhysShapesRun(Registry, SetShapes, MCPPhysShapesRequest(ProbePath, { HeadEntry(), BadEntry }));
		if (Negative.IsValid())
		{
			TestFalse(TEXT("a negative radius is refused"), Negative->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the refusal names the field"), Negative->GetStringField(TEXT("error")).Contains(TEXT("radius")));
		}

		TSharedPtr<FJsonObject> UnknownEntry = MakeShared<FJsonObject>();
		UnknownEntry->SetStringField(TEXT("boneName"), TEXT("tail_01"));
		UnknownEntry->SetArrayField(TEXT("spheres"), TArray<TSharedPtr<FJsonValue>>());
		TSharedPtr<FJsonObject> ClearHead = MakeShared<FJsonObject>();
		ClearHead->SetStringField(TEXT("boneName"), TEXT("head"));
		ClearHead->SetArrayField(TEXT("sphyls"), TArray<TSharedPtr<FJsonValue>>());
		const TSharedPtr<FJsonObject> Refused = MCPPhysShapesRun(Registry, SetShapes, MCPPhysShapesRequest(ProbePath, { ClearHead, UnknownEntry }));
		if (Refused.IsValid())
		{
			TestFalse(TEXT("an unknown bone is refused"), Refused->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the refusal names it"), Refused->GetStringField(TEXT("error")).Contains(TEXT("tail_01")));
		}
		TestEqual(TEXT("the refused calls leave the head capsules alone"), Head->AggGeom.SphylElems.Num(), 2);

		TSharedPtr<FJsonObject> Empty = MakeShared<FJsonObject>();
		Empty->SetStringField(TEXT("boneName"), TEXT("head"));
		const TSharedPtr<FJsonObject> Nothing = MCPPhysShapesRun(Registry, SetShapes, MCPPhysShapesRequest(ProbePath, { Empty }));
		if (Nothing.IsValid())
		{
			TestFalse(TEXT("an entry with no shape list is refused"), Nothing->GetBoolField(TEXT("success")));
		}
	}

	// The rollback restores the head's previous capsule.
	if (Rollback.IsValid())
	{
		TestEqual(TEXT("the rollback replays the same action"), Rollback->GetStringField(TEXT("method")), FString(SetShapes));
		const TSharedPtr<FJsonObject> Restored = MCPPhysShapesRun(Registry, SetShapes, Rollback->GetObjectField(TEXT("payload")));
		if (Restored.IsValid())
		{
			TestTrue(TEXT("the rollback succeeds"), Restored->GetBoolField(TEXT("success")));
		}
		TestEqual(TEXT("the rollback leaves one head capsule"), Head->AggGeom.SphylElems.Num(), 1);
		if (Head->AggGeom.SphylElems.Num() == 1)
		{
			TestEqual(TEXT("the rollback restores the radius"), Head->AggGeom.SphylElems[0].Radius, 8.f);
			TestEqual(TEXT("the rollback restores the length"), Head->AggGeom.SphylElems[0].Length, 4.f);
		}
		TestEqual(TEXT("the rollback keeps the tapered capsule"), Head->AggGeom.TaperedCapsuleElems.Num(), 1);
	}

	Package->SetDirtyFlag(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
