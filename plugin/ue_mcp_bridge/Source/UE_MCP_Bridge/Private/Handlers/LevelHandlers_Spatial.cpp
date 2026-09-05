#include "LevelHandlers.h"

#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/Actor.h"
#include "HandlerUtils.h"
#include "Misc/AutomationTest.h"
#include "ScopedTransaction.h"

namespace
{
	USceneComponent* FindExactSceneComponent(AActor* Actor, const FString& ComponentName)
	{
		if (!Actor || ComponentName.IsEmpty()) return nullptr;
		for (UActorComponent* Component : Actor->GetComponents())
		{
			if (USceneComponent* Scene = Cast<USceneComponent>(Component);
				Scene && Scene->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				return Scene;
			}
		}
		return nullptr;
	}

	TSharedPtr<FJsonObject> QuatToJson(const FQuat& Quat)
	{
		auto Json = MakeShared<FJsonObject>();
		Json->SetNumberField(TEXT("x"), Quat.X);
		Json->SetNumberField(TEXT("y"), Quat.Y);
		Json->SetNumberField(TEXT("z"), Quat.Z);
		Json->SetNumberField(TEXT("w"), Quat.W);
		return Json;
	}

	TSharedPtr<FJsonObject> TransformToJson(const FTransform& Transform)
	{
		auto Json = MakeShared<FJsonObject>();
		Json->SetObjectField(TEXT("location"), MCPVec3ToJsonObject(Transform.GetLocation()));
		Json->SetObjectField(TEXT("rotation"), MCPRotatorToJsonObject(Transform.Rotator()));
		Json->SetObjectField(TEXT("quaternion"), QuatToJson(Transform.GetRotation()));
		Json->SetObjectField(TEXT("scale"), MCPVec3ToJsonObject(Transform.GetScale3D()));
		return Json;
	}

	TSharedPtr<FJsonObject> ComponentTransformsToJson(USceneComponent* Component)
	{
		auto Json = MakeShared<FJsonObject>();
		Json->SetObjectField(TEXT("relative"), TransformToJson(Component->GetRelativeTransform()));
		Json->SetObjectField(TEXT("world"), TransformToJson(Component->GetComponentTransform()));
		return Json;
	}

	FVector NamedAxis(const FString& Axis);

	bool ReadOptionalBoolStrict(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field, bool DefaultValue, bool& Out)
	{
		Out = DefaultValue;
		if (!Json || !Json->HasField(Field)) return true;
		return Json->TryGetBoolField(Field, Out);
	}

	TSharedPtr<FJsonObject> BoundsToJson(const USceneComponent& Component)
	{
		const FBoxSphereBounds& Bounds = MCPComponentWorldBounds(Component);
		auto Json = MakeShared<FJsonObject>();
		Json->SetObjectField(TEXT("origin"), MCPVec3ToJsonObject(Bounds.Origin));
		Json->SetObjectField(TEXT("boxExtent"), MCPVec3ToJsonObject(Bounds.BoxExtent));
		Json->SetNumberField(TEXT("sphereRadius"), Bounds.SphereRadius);
		return Json;
	}

	TSharedPtr<FJsonObject> FrameAxesToJson(const FQuat& FrameRotation)
	{
		auto Json = MakeShared<FJsonObject>();
		Json->SetObjectField(TEXT("forward"), MCPVec3ToJsonObject(FrameRotation.RotateVector(FVector::ForwardVector)));
		Json->SetObjectField(TEXT("right"), MCPVec3ToJsonObject(FrameRotation.RotateVector(FVector::RightVector)));
		Json->SetObjectField(TEXT("up"), MCPVec3ToJsonObject(FrameRotation.RotateVector(FVector::UpVector)));
		return Json;
	}

	bool ResolveViewRotation(
		const TSharedPtr<FJsonObject>& ViewJson,
		const FQuat& FrameRotation,
		FString& OutViewFrom,
		FString& OutDirection,
		FString& OutAxis,
		double& OutSignedDegrees,
		FVector& OutAxisWorld)
	{
		if (!ViewJson ||
			!ViewJson->TryGetStringField(TEXT("viewFrom"), OutViewFrom) ||
			!ViewJson->TryGetStringField(TEXT("direction"), OutDirection))
		{
			return false;
		}
		double Degrees = 0.0;
		if (!ViewJson->TryGetNumberField(TEXT("degrees"), Degrees) || !FMath::IsFinite(Degrees) || Degrees <= 0.0 ||
			(OutDirection != TEXT("clockwise") && OutDirection != TEXT("counterclockwise")))
		{
			return false;
		}

		// The axis is deliberately one of the selected-frame basis axes.  A camera
		// looking from its named side sees clockwise as a positive rotation around
		// the axis pointing from the target to that camera.
		double ViewSign = 1.0;
		if (OutViewFrom == TEXT("front")) OutAxis = TEXT("forward");
		else if (OutViewFrom == TEXT("back")) { OutAxis = TEXT("forward"); ViewSign = -1.0; }
		else if (OutViewFrom == TEXT("right")) OutAxis = TEXT("right");
		else if (OutViewFrom == TEXT("left")) { OutAxis = TEXT("right"); ViewSign = -1.0; }
		else if (OutViewFrom == TEXT("above")) OutAxis = TEXT("up");
		else if (OutViewFrom == TEXT("below")) { OutAxis = TEXT("up"); ViewSign = -1.0; }
		else return false;

		if (OutDirection == TEXT("counterclockwise")) ViewSign *= -1.0;
		OutSignedDegrees = ViewSign * Degrees;
		OutAxisWorld = FrameRotation.RotateVector(NamedAxis(OutAxis)).GetSafeNormal();
		return true;
	}

	bool ReadRequiredNumber(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field, double& Out)
	{
		return Json && Json->TryGetNumberField(Field, Out) && FMath::IsFinite(Out);
	}

	bool ReadRollbackTransform(const TSharedPtr<FJsonObject>& Json, FTransform& Out)
	{
		if (!Json) return false;

		const TSharedPtr<FJsonObject>* Location = nullptr;
		const TSharedPtr<FJsonObject>* Quaternion = nullptr;
		const TSharedPtr<FJsonObject>* Scale = nullptr;
		if (!Json->TryGetObjectField(TEXT("location"), Location) || !*Location ||
			!Json->TryGetObjectField(TEXT("quaternion"), Quaternion) || !*Quaternion ||
			!Json->TryGetObjectField(TEXT("scale"), Scale) || !*Scale)
		{
			return false;
		}

		double LX, LY, LZ, QX, QY, QZ, QW, SX, SY, SZ;
		if (!ReadRequiredNumber(*Location, TEXT("x"), LX) ||
			!ReadRequiredNumber(*Location, TEXT("y"), LY) ||
			!ReadRequiredNumber(*Location, TEXT("z"), LZ) ||
			!ReadRequiredNumber(*Quaternion, TEXT("x"), QX) ||
			!ReadRequiredNumber(*Quaternion, TEXT("y"), QY) ||
			!ReadRequiredNumber(*Quaternion, TEXT("z"), QZ) ||
			!ReadRequiredNumber(*Quaternion, TEXT("w"), QW) ||
			!ReadRequiredNumber(*Scale, TEXT("x"), SX) ||
			!ReadRequiredNumber(*Scale, TEXT("y"), SY) ||
			!ReadRequiredNumber(*Scale, TEXT("z"), SZ))
		{
			return false;
		}

		FQuat Rotation(QX, QY, QZ, QW);
		if (Rotation.SizeSquared() <= UE_SMALL_NUMBER) return false;
		Rotation.Normalize();
		Out = FTransform(Rotation, FVector(LX, LY, LZ), FVector(SX, SY, SZ));
		return true;
	}

	FVector NamedAxis(const FString& Axis)
	{
		if (Axis == TEXT("forward")) return FVector::ForwardVector;
		if (Axis == TEXT("right")) return FVector::RightVector;
		return FVector::UpVector;
	}

	FTransform ApplyWorldNudge(
		const FTransform& Current,
		const FVector& TranslationWorld,
		bool bRotate,
		const FVector& RotationAxisWorld,
		double RotationDegrees)
	{
		FTransform Result = Current;
		Result.AddToTranslation(TranslationWorld);
		if (bRotate)
		{
			FQuat NewRotation = FQuat(
				RotationAxisWorld.GetSafeNormal(),
				FMath::DegreesToRadians(RotationDegrees)) * Current.GetRotation();
			NewRotation.Normalize();
			Result.SetRotation(NewRotation);
		}
		return Result;
	}
}

TSharedPtr<FJsonValue> FLevelHandlers::NudgeComponent(const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (auto Error = RequireStringAlt(Params, TEXT("actorLabel"), TEXT("actorPath"), ActorLabel)) return Error;
	FString ComponentName;
	if (auto Error = RequireString(Params, TEXT("componentName"), ComponentName)) return Error;

	FString WorldScope = TEXT("editor");
	if (Params->HasField(TEXT("world")) && !Params->TryGetStringField(TEXT("world"), WorldScope))
	{
		return MCPError(TEXT("world must be editor or pie"));
	}
	WorldScope = WorldScope.ToLower();
	if (WorldScope != TEXT("editor") && WorldScope != TEXT("pie"))
	{
		return MCPError(TEXT("world must be editor or pie"));
	}
	bool bDryRun = false;
	if (!ReadOptionalBoolStrict(Params, TEXT("dryRun"), false, bDryRun))
	{
		return MCPError(TEXT("dryRun must be a boolean"));
	}
	UWorld* World = ResolveWorldFromParams(Params, *WorldScope);
	if (!World)
	{
		return MCPError(WorldScope == TEXT("pie")
			? TEXT("PIE not running (or no such pieInstance). See editor(list_pie_instances).")
			: TEXT("Editor world not available"));
	}

	FMCPActorSelector ActorSel;
	ActorSel.Match = EMCPActorMatch::LabelNameOrPath;
	ActorSel.WorldLabel = World->IsGameWorld() ? TEXT("PIE") : TEXT("editor");
	TSharedPtr<FJsonValue> ActorErr;
	AActor* Actor = MCPResolveActor(World, Params, ActorErr, ActorSel);
	if (!Actor) return ActorErr;
	ActorLabel = Actor->GetActorLabel();
	USceneComponent* Component = FindExactSceneComponent(Actor, ComponentName);
	if (!Component)
	{
		return MCPError(FString::Printf(
			TEXT("Exact SceneComponent '%s' not found on actor '%s'"), *ComponentName, *ActorLabel));
	}

	const FTransform PreviousRelative = Component->GetRelativeTransform();
	const FTransform PreviousWorld = Component->GetComponentTransform();
	const TSharedPtr<FJsonObject>* RestoreJson = nullptr;
	const bool bHasRestoreField = Params->HasField(TEXT("_restoreRelative"));
	const bool bRestore = bHasRestoreField && Params->TryGetObjectField(TEXT("_restoreRelative"), RestoreJson) && *RestoreJson;
	if (bHasRestoreField && !bRestore) return MCPError(TEXT("Invalid internal rollback transform"));
	if (bRestore && bDryRun) return MCPError(TEXT("dryRun cannot be combined with internal rollback"));

	FString Frame = TEXT("actor");
	if (Params->HasField(TEXT("frame")) && !Params->TryGetStringField(TEXT("frame"), Frame))
	{
		return MCPError(TEXT("frame must be world, actor, parent, or component"));
	}
	Frame = Frame.ToLower();
	FVector TranslationWorld = FVector::ZeroVector;
	FVector RotationAxisWorld = FVector::ZeroVector;
	FQuat FrameRotation = FQuat::Identity;
	FString RotationAxis;
	double RotationDegrees = 0.0;
	double ScaleMultiplier = 1.0;
	bool bHasTranslation = false;
	bool bHasRotation = false;
	bool bHasScale = false;
	FTransform RestoreRelative;

	if (bRestore)
	{
		if (!ReadRollbackTransform(*RestoreJson, RestoreRelative))
		{
			return MCPError(TEXT("Invalid internal rollback transform"));
		}
	}
	else
	{
		if (Frame != TEXT("world") && Frame != TEXT("actor") &&
			Frame != TEXT("parent") && Frame != TEXT("component"))
		{
			return MCPError(TEXT("frame must be world, actor, parent, or component"));
		}

		if (Frame == TEXT("actor"))
		{
			FrameRotation = Actor->GetActorQuat();
		}
		else if (Frame == TEXT("parent"))
		{
			USceneComponent* Parent = Component->GetAttachParent();
			if (!Parent) return MCPError(TEXT("frame=parent requires an attached component"));
			FrameRotation = Parent->GetComponentQuat();
		}
		else if (Frame == TEXT("component"))
		{
			FrameRotation = Component->GetComponentQuat();
		}
		FrameRotation.Normalize();

		const TSharedPtr<FJsonObject>* TranslationJson = nullptr;
		if (Params->HasField(TEXT("translationDelta")))
		{
			if (!Params->TryGetObjectField(TEXT("translationDelta"), TranslationJson) || !*TranslationJson)
			{
				return MCPError(TEXT("translationDelta must be an object with finite numeric values"));
			}
			double Forward = 0.0, Right = 0.0, Up = 0.0;
			if (((*TranslationJson)->HasField(TEXT("forwardCm")) &&
				 !ReadRequiredNumber(*TranslationJson, TEXT("forwardCm"), Forward)) ||
				((*TranslationJson)->HasField(TEXT("rightCm")) &&
				 !ReadRequiredNumber(*TranslationJson, TEXT("rightCm"), Right)) ||
				((*TranslationJson)->HasField(TEXT("upCm")) &&
				 !ReadRequiredNumber(*TranslationJson, TEXT("upCm"), Up)))
			{
				return MCPError(TEXT("translationDelta values must be finite numbers"));
			}
			TranslationWorld = FrameRotation.RotateVector(FVector(Forward, Right, Up));
			bHasTranslation = !TranslationWorld.IsNearlyZero();
		}

		const TSharedPtr<FJsonObject>* RotationJson = nullptr;
		const TSharedPtr<FJsonObject>* ViewRotationJson = nullptr;
		if (Params->HasField(TEXT("axisRotation")) && Params->HasField(TEXT("viewRotation")))
		{
			return MCPError(TEXT("axisRotation and viewRotation are mutually exclusive"));
		}
		if (Params->HasField(TEXT("axisRotation")))
		{
			if (!Params->TryGetObjectField(TEXT("axisRotation"), RotationJson) || !*RotationJson)
			{
				return MCPError(TEXT("axisRotation must be an object"));
			}
			if (!(*RotationJson)->TryGetStringField(TEXT("axis"), RotationAxis) ||
				(RotationAxis != TEXT("forward") && RotationAxis != TEXT("right") && RotationAxis != TEXT("up")))
			{
				return MCPError(TEXT("axisRotation.axis must be forward, right, or up"));
			}
			if (!ReadRequiredNumber(*RotationJson, TEXT("degrees"), RotationDegrees))
			{
				return MCPError(TEXT("axisRotation.degrees must be a finite number"));
			}
			RotationAxisWorld = FrameRotation.RotateVector(NamedAxis(RotationAxis)).GetSafeNormal();
			bHasRotation = !FMath::IsNearlyZero(RotationDegrees);
		}
		else if (Params->HasField(TEXT("viewRotation")))
		{
			FString ViewFrom, ViewDirection;
			if (!Params->TryGetObjectField(TEXT("viewRotation"), ViewRotationJson) || !*ViewRotationJson ||
				!ResolveViewRotation(*ViewRotationJson, FrameRotation, ViewFrom, ViewDirection,
					RotationAxis, RotationDegrees, RotationAxisWorld))
			{
				return MCPError(TEXT("viewRotation requires viewFrom front|back|right|left|above|below, direction clockwise|counterclockwise, and finite degrees greater than zero"));
			}
			bHasRotation = true;
		}

		if (Params->HasField(TEXT("scaleMultiplier")))
		{
			if (!Params->TryGetNumberField(TEXT("scaleMultiplier"), ScaleMultiplier) ||
				!FMath::IsFinite(ScaleMultiplier) || ScaleMultiplier <= 0.0)
			{
				return MCPError(TEXT("scaleMultiplier must be a finite number greater than zero"));
			}
			bHasScale = !FMath::IsNearlyEqual(ScaleMultiplier, 1.0);
		}

		if (!bDryRun && !bHasTranslation && !bHasRotation && !bHasScale)
		{
			return MCPError(TEXT("Provide a non-zero translationDelta, axisRotation, or scaleMultiplier"));
		}
	}

	const FTransform RequestedWorld = bRestore ? PreviousWorld : ApplyWorldNudge(
		PreviousWorld, TranslationWorld, bHasRotation, RotationAxisWorld, RotationDegrees);
	const FVector RequestedRelativeScale = bRestore ? RestoreRelative.GetScale3D() :
		PreviousRelative.GetScale3D() * ScaleMultiplier;
	if (RequestedWorld.ContainsNaN() || RequestedRelativeScale.ContainsNaN())
	{
		return MCPError(TEXT("The requested nudge produces a non-finite transform"));
	}
	const bool bRuntimeWorld = World->IsGameWorld();
	const bool bRuntimeMobilityRestricted = bRuntimeWorld && Component->IsRegistered() &&
		Component->Mobility != EComponentMobility::Movable && (bHasTranslation || bHasRotation || bRestore);
	if (bRuntimeMobilityRestricted && !bDryRun)
	{
		return MCPError(TEXT("Registered non-Movable components in PIE cannot receive translation or rotation nudges"));
	}
	USceneComponent* Parent = Component->GetAttachParent();
	auto Attachment = MakeShared<FJsonObject>();
	Attachment->SetBoolField(TEXT("attached"), Parent != nullptr);
	if (Parent)
	{
		Attachment->SetStringField(TEXT("parentComponent"), Parent->GetName());
		Attachment->SetStringField(TEXT("parentPath"), Parent->GetPathName());
	}
	else
	{
		Attachment->SetStringField(TEXT("parentComponent"), TEXT(""));
		Attachment->SetStringField(TEXT("parentPath"), TEXT(""));
	}
	Attachment->SetStringField(TEXT("socket"), Component->GetAttachSocketName().ToString());

	auto Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetBoolField(TEXT("operationApplied"), !bDryRun);
	Result->SetStringField(TEXT("actorLabel"), ActorLabel);
	Result->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	Result->SetStringField(TEXT("actorClass"), Actor->GetClass()->GetPathName());
	Result->SetStringField(TEXT("componentName"), Component->GetName());
	Result->SetStringField(TEXT("componentPath"), Component->GetPathName());
	Result->SetStringField(TEXT("componentClass"), Component->GetClass()->GetPathName());
	Result->SetStringField(TEXT("frame"), bRestore ? TEXT("rollback") : Frame);
	Result->SetStringField(TEXT("world"), WorldScope);
	Result->SetStringField(TEXT("worldName"), World->GetName());
	Result->SetStringField(TEXT("worldPath"), World->GetPathName());
	int32 ActualPieInstance = INDEX_NONE;
	if (GEngine)
	{
		if (const FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(World))
		{
			ActualPieInstance = WorldContext->PIEInstance;
		}
	}
	Result->SetNumberField(TEXT("pieInstance"), ActualPieInstance);
	Result->SetObjectField(TEXT("attachment"), Attachment);
	Result->SetBoolField(TEXT("absoluteLocation"), Component->IsUsingAbsoluteLocation());
	Result->SetBoolField(TEXT("absoluteRotation"), Component->IsUsingAbsoluteRotation());
	Result->SetBoolField(TEXT("absoluteScale"), Component->IsUsingAbsoluteScale());
	Result->SetObjectField(TEXT("bounds"), BoundsToJson(*Component));
	Result->SetObjectField(TEXT("before"), ComponentTransformsToJson(Component));
	if (!bRestore) Result->SetObjectField(TEXT("frameAxesWorld"), FrameAxesToJson(FrameRotation));
	if (bHasTranslation) Result->SetObjectField(TEXT("resolvedTranslationWorld"), MCPVec3ToJsonObject(TranslationWorld));
	if (bHasRotation)
	{
		Result->SetStringField(TEXT("rotationAxis"), RotationAxis);
		Result->SetNumberField(TEXT("resolvedSignedDegrees"), RotationDegrees);
		Result->SetNumberField(TEXT("rotationDegrees"), RotationDegrees);
		Result->SetObjectField(TEXT("resolvedRotationAxisWorld"), MCPVec3ToJsonObject(RotationAxisWorld));
		if (Params->HasField(TEXT("viewRotation")))
		{
			Result->SetObjectField(TEXT("viewRotation"), Params->GetObjectField(TEXT("viewRotation")));
		}
	}
	if (bHasScale) Result->SetNumberField(TEXT("scaleMultiplier"), ScaleMultiplier);
	if (bRuntimeMobilityRestricted)
	{
		Result->SetStringField(TEXT("mutationRestriction"),
			TEXT("A registered non-Movable PIE component will reject translation or rotation when applied."));
	}

	if (bDryRun)
	{
		Result->SetBoolField(TEXT("changed"), false);
		Result->SetBoolField(TEXT("rollbackPossible"), false);
		Result->SetStringField(TEXT("predictionNote"),
			TEXT("Requested setter inputs only; attachment, absolute flags, scale and runtime systems can affect the applied result. Verify after applying."));
		Result->SetObjectField(TEXT("requestedWorldLocation"), MCPVec3ToJsonObject(RequestedWorld.GetLocation()));
		Result->SetObjectField(TEXT("requestedWorldRotation"), MCPRotatorToJsonObject(RequestedWorld.Rotator()));
		Result->SetObjectField(TEXT("requestedWorldQuaternion"), QuatToJson(RequestedWorld.GetRotation()));
		Result->SetObjectField(TEXT("requestedRelativeScale"), MCPVec3ToJsonObject(RequestedRelativeScale));
		return MCPResult(Result);
	}

	const FScopedTransaction Transaction(FText::FromString(TEXT("Nudge component")), !bRuntimeWorld);
	if (!bRuntimeWorld)
	{
		Actor->Modify();
		Component->Modify();
	}

	if (bRestore)
	{
		Component->SetRelativeTransform(RestoreRelative);
	}
	else
	{
		const FTransform AdjustedWorld = ApplyWorldNudge(
			PreviousWorld, TranslationWorld, bHasRotation, RotationAxisWorld, RotationDegrees);
		Component->SetWorldLocationAndRotation(
			AdjustedWorld.GetLocation(), AdjustedWorld.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		if (bHasScale)
		{
			Component->SetRelativeScale3D(PreviousRelative.GetScale3D() * ScaleMultiplier);
		}
	}

	Component->UpdateComponentToWorld();
	Component->MarkRenderStateDirty();
	if (!bRuntimeWorld)
	{
		Component->PostEditComponentMove(true);
		Component->MarkPackageDirty();
	}

	MCPSetUpdated(Result);
	Result->SetObjectField(TEXT("after"), ComponentTransformsToJson(Component));
	Result->SetStringField(TEXT("postApplyWarning"),
		TEXT("Physics simulation, construction scripts, or other runtime systems may subsequently change this component transform."));

	auto RollbackPayload = MakeShared<FJsonObject>();
	RollbackPayload->SetStringField(TEXT("actorLabel"), ActorLabel);
	RollbackPayload->SetStringField(TEXT("actorPath"), Actor->GetPathName());
	RollbackPayload->SetStringField(TEXT("componentName"), Component->GetName());
	RollbackPayload->SetStringField(TEXT("world"), WorldScope);
	double PieInstance = 0.0;
	if (Params->TryGetNumberField(TEXT("pieInstance"), PieInstance))
	{
		RollbackPayload->SetNumberField(TEXT("pieInstance"), PieInstance);
	}
	RollbackPayload->SetObjectField(TEXT("_restoreRelative"), TransformToJson(PreviousRelative));
	MCPSetRollback(Result, TEXT("nudge_component"), RollbackPayload);
	return MCPResult(Result);
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Editor.h"
#include "Editor/TransBuffer.h"
#include "Engine/World.h"
#include "HandlerRegistry.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	class FScopedSpatialNudgeTestWorld
	{
	public:
		FScopedSpatialNudgeTestWorld()
		{
			if (!GEditor) return;
			OriginalWorld = GEditor->GetEditorWorldContext().World();
			const FString PackageName = FString(TEXT("/Temp/UEMCP_SpatialNudgeTest_")) +
				FGuid::NewGuid().ToString(EGuidFormats::Digits);
			TestPackage = CreatePackage(*PackageName);
			if (!TestPackage) return;
			const FName WorldName = MakeUniqueObjectName(
				TestPackage, UWorld::StaticClass(), FName(TEXT("UEMCP_SpatialNudgeTestWorld")));
			const UWorld::InitializationValues Init = UWorld::InitializationValues()
				.InitializeScenes(false)
				.AllowAudioPlayback(false)
				.RequiresHitProxies(false)
				.CreatePhysicsScene(false)
				.CreateNavigation(false)
				.CreateAISystem(false)
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(false)
				.SetTransactional(true)
				.CreateFXSystem(false)
				.CreateWorldPartition(false);
			TestWorld = UWorld::CreateWorld(
				EWorldType::Editor, false, WorldName, TestPackage, true,
				ERHIFeatureLevel::Num, &Init);
			if (TestWorld) GEditor->GetEditorWorldContext().SetCurrentWorld(TestWorld);
		}

		~FScopedSpatialNudgeTestWorld()
		{
			if (GEditor && TestWorld) GEditor->GetEditorWorldContext().SetCurrentWorld(OriginalWorld);
			if (TestWorld) TestWorld->DestroyWorld(false);
			if (TestPackage) TestPackage->SetDirtyFlag(false);
		}

		UWorld* Get() const { return TestWorld; }
		UPackage* GetPackage() const { return TestPackage; }
		void ClearPackageDirty() const { if (TestPackage) TestPackage->SetDirtyFlag(false); }

	private:
		UWorld* OriginalWorld = nullptr;
		UWorld* TestWorld = nullptr;
		UPackage* TestPackage = nullptr;
	};

	TSharedPtr<FJsonObject> SpatialNudgeResult(
		FMCPHandlerRegistry& Registry,
		const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("nudge_component"), Params);
		return Response.IsValid() && Response->Type == EJson::Object ? Response->AsObject() : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelSpatialComponentNudgeTest,
	"UE.MCP.Level.SpatialComponentNudge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelSpatialComponentNudgeTest::RunTest(const FString& Parameters)
{
	const FTransform ParentWorld(
		FRotator(18.0, -42.0, 27.0), FVector(400.0, -80.0, 120.0), FVector::OneVector);
	const FTransform Relative(
		FRotator(-147.657698, 83.868340, -89.500431), FVector(-9.0, 3.0, 0.0), FVector(1.35));
	const FTransform ChildWorld = Relative * ParentWorld;
	const FVector AxisWorld = FVector::UpVector;
	const FQuat ExpectedRotation =
		FQuat(AxisWorld, FMath::DegreesToRadians(12.0)) * ChildWorld.GetRotation();
	const FTransform Adjusted = ApplyWorldNudge(
		ChildWorld, FVector::ZeroVector, true, AxisWorld, 12.0);

	TestTrue(TEXT("Axis-angle nudge applies the requested world rotation"),
		Adjusted.GetRotation().AngularDistance(ExpectedRotation) < 1.e-6);
	TestTrue(TEXT("Rotation preserves world location"),
		Adjusted.GetLocation().Equals(ChildWorld.GetLocation(), 1.e-6));
	TestTrue(TEXT("Rotation preserves scale"),
		Adjusted.GetScale3D().Equals(ChildWorld.GetScale3D(), 1.e-6));

	const FTransform SolvedRelative = Adjusted.GetRelativeTransform(ParentWorld);
	const FTransform RoundTripWorld = SolvedRelative * ParentWorld;
	TestTrue(TEXT("Attached relative transform round-trips to the desired world rotation"),
		RoundTripWorld.GetRotation().AngularDistance(Adjusted.GetRotation()) < 1.e-6);

	FRotator NaiveRelativeRotation = Relative.Rotator();
	NaiveRelativeRotation.Yaw += 12.0;
	const FTransform NaiveRelative(
		NaiveRelativeRotation, Relative.GetLocation(), Relative.GetScale3D());
	const FTransform NaiveWorld = NaiveRelative * ParentWorld;
	TestTrue(TEXT("Raw relative Euler yaw is not the requested world-axis adjustment"),
		FMath::RadiansToDegrees(NaiveWorld.GetRotation().AngularDistance(Adjusted.GetRotation())) > 1.0);

	// UE's +Z, +90-degree quaternion sends +X to +Y.  The screen bases below
	// are therefore intentionally engine/projection based rather than a generic
	// right-handed diagram: clockwise moves screen-up toward screen-right.
	const FVector QuarterTurn = FQuat(FVector::UpVector, HALF_PI).RotateVector(FVector::ForwardVector);
	TestTrue(TEXT("UE positive Z quarter turn maps X to Y"), QuarterTurn.Equals(FVector::RightVector, 1.e-6));
	struct FViewCase
	{
		const TCHAR* Name;
		FVector ScreenUp;
		FVector ScreenRight;
	};
	const FViewCase Views[] = {
		{ TEXT("front"), FVector::UpVector, -FVector::RightVector },
		{ TEXT("back"), FVector::UpVector, FVector::RightVector },
		{ TEXT("right"), FVector::UpVector, FVector::ForwardVector },
		{ TEXT("left"), FVector::UpVector, -FVector::ForwardVector },
		{ TEXT("above"), FVector::ForwardVector, FVector::RightVector },
		{ TEXT("below"), FVector::ForwardVector, -FVector::RightVector },
	};
	for (const FViewCase& View : Views)
	{
		for (const TCHAR* Direction : { TEXT("clockwise"), TEXT("counterclockwise") })
		{
			auto Json = MakeShared<FJsonObject>();
			Json->SetStringField(TEXT("viewFrom"), View.Name);
			Json->SetStringField(TEXT("direction"), Direction);
			Json->SetNumberField(TEXT("degrees"), 5.0);
			FString ViewFrom, ParsedDirection, Axis;
			double SignedDegrees = 0.0;
			FVector ViewAxisWorld;
			const bool bResolved = ResolveViewRotation(
				Json, FQuat::Identity, ViewFrom, ParsedDirection, Axis, SignedDegrees, ViewAxisWorld);
			TestTrue(FString::Printf(TEXT("%s %s resolves"), View.Name, Direction), bResolved);
			const FVector RotatedUp = FQuat(ViewAxisWorld, FMath::DegreesToRadians(SignedDegrees)).RotateVector(View.ScreenUp);
			const double ScreenRightProjection = FVector::DotProduct(RotatedUp, View.ScreenRight);
			TestTrue(FString::Printf(TEXT("%s %s has expected screen rotation"), View.Name, Direction),
				FCString::Strcmp(Direction, TEXT("clockwise")) == 0 ? ScreenRightProjection > 0.0 : ScreenRightProjection < 0.0);
		}
	}

	const FTransform DryRunInspection = ApplyWorldNudge(
		ChildWorld, FVector::ZeroVector, false, FVector::ZeroVector, 0.0);
	TestTrue(TEXT("A no-delta dry-run preview preserves the fixed pose"),
		DryRunInspection.Equals(ChildWorld, 1.e-6));

	FScopedSpatialNudgeTestWorld TestWorldScope;
	UWorld* TestWorld = TestWorldScope.Get();
	if (!TestNotNull(TEXT("a transient editor world was created for handler coverage"), TestWorld)) return false;
	const FString Label = FString(TEXT("SpatialNudgeFixture_")) + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Name = MakeUniqueObjectName(TestWorld->PersistentLevel, AActor::StaticClass(), FName(*Label));
	SpawnParameters.ObjectFlags = RF_Transactional;
	AActor* Fixture = TestWorld->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParameters);
	if (!TestNotNull(TEXT("a transient actor was spawned for handler coverage"), Fixture)) return false;
	Fixture->SetActorLabel(Label, false);
	USceneComponent* Parent = NewObject<USceneComponent>(Fixture, TEXT("SpatialNudgeParent"), RF_Transactional);
	Fixture->SetRootComponent(Parent);
	Fixture->AddInstanceComponent(Parent);
	Parent->SetMobility(EComponentMobility::Movable);
	Parent->RegisterComponent();
	USceneComponent* Child = NewObject<USceneComponent>(Fixture, TEXT("SpatialNudgeChild"), RF_Transactional);
	Child->SetupAttachment(Parent, TEXT("SpatialNudgeSocket"));
	Child->SetMobility(EComponentMobility::Movable);
	const FTransform InitialRelative(FRotator(7.0, -13.0, 29.0), FVector(3.0, -4.0, 5.0), FVector(1.1, 0.9, 1.2));
	Child->SetRelativeTransform(InitialRelative);
	Fixture->AddInstanceComponent(Child);
	Child->RegisterComponent();
	TestWorldScope.ClearPackageDirty();
	TestTrue(TEXT("fixture package is a real clean package"),
		TestWorldScope.GetPackage() != GetTransientPackage() && !TestWorldScope.GetPackage()->IsDirty());
	TestTrue(TEXT("fixture actor and components are transactional but nontransient"),
		Fixture->HasAnyFlags(RF_Transactional) && Child->HasAnyFlags(RF_Transactional) &&
		!Fixture->HasAnyFlags(RF_Transient) && !Child->HasAnyFlags(RF_Transient));

	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("nudge_component is registered"), Registry.HasHandler(TEXT("nudge_component")));
	auto MakeParams = [&Label]()
	{
		auto Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("actorLabel"), Label);
		Json->SetStringField(TEXT("componentName"), TEXT("SpatialNudgeChild"));
		Json->SetStringField(TEXT("world"), TEXT("editor"));
		return Json;
	};

	const TSharedPtr<FJsonObject> NoDeltaApply = SpatialNudgeResult(Registry, MakeParams());
	TestTrue(TEXT("non-dry no-delta nudge returns an object"), NoDeltaApply.IsValid());
	if (NoDeltaApply.IsValid())
	{
		TestFalse(TEXT("non-dry no-delta nudge remains rejected"), NoDeltaApply->GetBoolField(TEXT("success")));
	}
	const auto AxisRotation = []()
	{
		auto Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("axis"), TEXT("up"));
		Json->SetNumberField(TEXT("degrees"), 10.0);
		return Json;
	};
	const auto ViewRotation = []()
	{
		auto Json = MakeShared<FJsonObject>();
		Json->SetStringField(TEXT("viewFrom"), TEXT("above"));
		Json->SetStringField(TEXT("direction"), TEXT("clockwise"));
		Json->SetNumberField(TEXT("degrees"), 10.0);
		return Json;
	};
	auto MutuallyExclusiveParams = MakeParams();
	MutuallyExclusiveParams->SetObjectField(TEXT("axisRotation"), AxisRotation());
	MutuallyExclusiveParams->SetObjectField(TEXT("viewRotation"), ViewRotation());
	const TSharedPtr<FJsonObject> MutuallyExclusive = SpatialNudgeResult(Registry, MutuallyExclusiveParams);
	TestTrue(TEXT("mutually exclusive rotation inputs return an object"), MutuallyExclusive.IsValid());
	if (MutuallyExclusive.IsValid())
	{
		TestFalse(TEXT("axisRotation and viewRotation are rejected together"), MutuallyExclusive->GetBoolField(TEXT("success")));
	}
	auto MalformedViewParams = MakeParams();
	MalformedViewParams->SetStringField(TEXT("viewRotation"), TEXT("not-an-object"));
	const TSharedPtr<FJsonObject> MalformedView = SpatialNudgeResult(Registry, MalformedViewParams);
	TestTrue(TEXT("malformed viewRotation returns an object"), MalformedView.IsValid());
	if (MalformedView.IsValid())
	{
		TestFalse(TEXT("malformed viewRotation is rejected"), MalformedView->GetBoolField(TEXT("success")));
	}

	const bool bPackageDirtyBeforeDryRun = TestWorldScope.GetPackage()->IsDirty();
	UTransBuffer* Trans = GEditor ? Cast<UTransBuffer>(GEditor->Trans) : nullptr;
	const int32 TransactionsBeforeDryRun = Trans ? Trans->GetQueueLength() : INDEX_NONE;
	const int32 UndoCountBeforeDryRun = Trans ? Trans->GetUndoCount() : INDEX_NONE;
	const FTransform BeforeDryRun = Child->GetRelativeTransform();
	auto InspectionParams = MakeParams();
	InspectionParams->SetBoolField(TEXT("dryRun"), true);
	const TSharedPtr<FJsonObject> Inspection = SpatialNudgeResult(Registry, InspectionParams);
	TestTrue(TEXT("no-delta dry-run returns an object"), Inspection.IsValid());
	if (!Inspection.IsValid()) return false;
	TestTrue(TEXT("no-delta dry-run succeeds"), Inspection->GetBoolField(TEXT("success")));
	TestFalse(TEXT("no-delta dry-run does not apply an operation"), Inspection->GetBoolField(TEXT("operationApplied")));
	TestFalse(TEXT("no-delta dry-run has no after transform"), Inspection->HasField(TEXT("after")));
	TestFalse(TEXT("no-delta dry-run has no rollback"), Inspection->HasField(TEXT("rollback")));
	TestTrue(TEXT("no-delta dry-run returns the current transform"), Inspection->HasField(TEXT("before")));
	TestTrue(TEXT("no-delta dry-run reports absolute transform flags"),
		Inspection->HasField(TEXT("absoluteLocation")) && Inspection->HasField(TEXT("absoluteRotation")) && Inspection->HasField(TEXT("absoluteScale")));
	TestTrue(TEXT("no-delta dry-run reports exact world identity and PIE instance"),
		Inspection->HasField(TEXT("worldPath")) && Inspection->HasField(TEXT("pieInstance")));
	TestTrue(TEXT("no-delta dry-run does not change the relative transform"), Child->GetRelativeTransform().Equals(BeforeDryRun, 1.e-6));
	TestEqual(TEXT("no-delta dry-run does not dirty its package"), TestWorldScope.GetPackage()->IsDirty(), bPackageDirtyBeforeDryRun);
	if (Trans)
	{
		TestEqual(TEXT("no-delta dry-run does not add an undo transaction"), Trans->GetQueueLength(), TransactionsBeforeDryRun);
		TestEqual(TEXT("no-delta dry-run preserves undo state"), Trans->GetUndoCount(), UndoCountBeforeDryRun);
	}

	auto PreviewParams = MakeParams();
	PreviewParams->SetBoolField(TEXT("dryRun"), true);
	auto Translation = MakeShared<FJsonObject>();
	Translation->SetNumberField(TEXT("forwardCm"), 17.0);
	PreviewParams->SetObjectField(TEXT("translationDelta"), Translation);
	const TSharedPtr<FJsonObject> Preview = SpatialNudgeResult(Registry, PreviewParams);
	TestTrue(TEXT("requested-delta dry-run returns an object"), Preview.IsValid());
	if (!Preview.IsValid()) return false;
	TestFalse(TEXT("requested-delta dry-run does not apply an operation"), Preview->GetBoolField(TEXT("operationApplied")));
	TestTrue(TEXT("requested-delta dry-run supplies requested world location"), Preview->HasField(TEXT("requestedWorldLocation")));
	TestTrue(TEXT("requested-delta dry-run supplies requested world rotation"), Preview->HasField(TEXT("requestedWorldRotation")));
	TestTrue(TEXT("requested-delta dry-run supplies requested relative scale"), Preview->HasField(TEXT("requestedRelativeScale")));
	TestFalse(TEXT("requested-delta dry-run has no after transform"), Preview->HasField(TEXT("after")));
	TestFalse(TEXT("requested-delta dry-run has no rollback"), Preview->HasField(TEXT("rollback")));
	TestTrue(TEXT("requested-delta dry-run does not change the relative transform"), Child->GetRelativeTransform().Equals(BeforeDryRun, 1.e-6));
	TestEqual(TEXT("requested-delta dry-run does not dirty its package"), TestWorldScope.GetPackage()->IsDirty(), bPackageDirtyBeforeDryRun);
	if (Trans)
	{
		TestEqual(TEXT("requested-delta dry-run does not add an undo transaction"), Trans->GetQueueLength(), TransactionsBeforeDryRun);
		TestEqual(TEXT("requested-delta dry-run preserves undo state"), Trans->GetUndoCount(), UndoCountBeforeDryRun);
	}

	auto ApplyParams = MakeParams();
	ApplyParams->SetObjectField(TEXT("translationDelta"), Translation);
	Child->SetMobility(EComponentMobility::Static);
	TestWorld->WorldType = EWorldType::Game;
	TestWorldScope.ClearPackageDirty();
	const TSharedPtr<FJsonObject> RestrictedPreview = SpatialNudgeResult(Registry, PreviewParams);
	TestTrue(TEXT("runtime static preview reports the mobility restriction"),
		RestrictedPreview.IsValid() && RestrictedPreview->HasField(TEXT("mutationRestriction")));
	const TSharedPtr<FJsonObject> RestrictedApply = SpatialNudgeResult(Registry, ApplyParams);
	TestTrue(TEXT("runtime static mutation fails instead of claiming it applied"),
		RestrictedApply.IsValid() && !RestrictedApply->GetBoolField(TEXT("success")));
	TestTrue(TEXT("runtime mobility refusal preserves the transform"), Child->GetRelativeTransform().Equals(BeforeDryRun, 1.e-6));
	TestFalse(TEXT("runtime mobility refusal does not dirty the package"), TestWorldScope.GetPackage()->IsDirty());
	TestWorld->WorldType = EWorldType::Editor;
	Child->SetMobility(EComponentMobility::Movable);
	const TSharedPtr<FJsonObject> Applied = SpatialNudgeResult(Registry, ApplyParams);
	TestTrue(TEXT("apply returns an object"), Applied.IsValid());
	if (!Applied.IsValid()) return false;
	TestTrue(TEXT("apply succeeds"), Applied->GetBoolField(TEXT("success")));
	TestTrue(TEXT("apply reports operationApplied"), Applied->GetBoolField(TEXT("operationApplied")));
	TestTrue(TEXT("apply supplies after transform"), Applied->HasField(TEXT("after")));
	TestTrue(TEXT("apply warns that external systems can change the result"), Applied->HasField(TEXT("postApplyWarning")));
	TestTrue(TEXT("apply dirties the real fixture package"), TestWorldScope.GetPackage()->IsDirty());
	const TSharedPtr<FJsonObject>* Rollback = nullptr;
	TestTrue(TEXT("apply supplies rollback"), Applied->TryGetObjectField(TEXT("rollback"), Rollback) && *Rollback);
	TestFalse(TEXT("apply changes the relative transform"), Child->GetRelativeTransform().Equals(BeforeDryRun, 1.e-6));
	if (Rollback && *Rollback)
	{
		const TSharedPtr<FJsonObject>* RollbackPayload = nullptr;
		if (TestTrue(TEXT("rollback carries a payload"), (*Rollback)->TryGetObjectField(TEXT("payload"), RollbackPayload) && *RollbackPayload))
		{
			const TSharedPtr<FJsonObject> Restored = SpatialNudgeResult(Registry, *RollbackPayload);
			TestTrue(TEXT("rollback returns an object"), Restored.IsValid());
			if (Restored.IsValid()) TestTrue(TEXT("rollback succeeds"), Restored->GetBoolField(TEXT("success")));
			TestTrue(TEXT("rollback restores the relative transform"), Child->GetRelativeTransform().Equals(BeforeDryRun, 1.e-6));
		}
	}
	Fixture->Destroy();
	return true;
}
#endif
