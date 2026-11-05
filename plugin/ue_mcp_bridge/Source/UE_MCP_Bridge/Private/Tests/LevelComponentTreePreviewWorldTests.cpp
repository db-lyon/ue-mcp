// Coverage for get_component_tree in a preview world, relativeTo and socketNames.
//
// A Blueprint editor's viewport is a transient preview world that neither
// world="editor" nor world="pie" reaches, so reading where a component sits in
// it took a script. The probe here is an FPreviewScene, the same world type
// the Blueprint editor builds, holding one actor with a root, an offset static
// mesh, a rotated "camera" frame and a skeletal mesh. The assertions are about
// the contract: the world is addressed by its path or found from the actor's
// path, each row carries its attachment and mesh, relativeTo transforms match
// the engine's own composition, and named bones report component-space
// transforms while a missing name is reported rather than dropped.

#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Level/LevelHandlers.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "PreviewScene.h"
#include "ReferenceSkeleton.h"

namespace
{
	TSharedPtr<FJsonObject> MCPPreviewTreeRun(FMCPHandlerRegistry& Registry, const TSharedPtr<FJsonObject>& Request)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(TEXT("get_component_tree"), Request);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
	}

	bool MCPPreviewTreeSucceeded(const TSharedPtr<FJsonObject>& Response)
	{
		bool bSuccess = false;
		return Response.IsValid() && Response->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	TSharedPtr<FJsonObject> MCPPreviewTreeRow(const TSharedPtr<FJsonObject>& Response, const TCHAR* Name)
	{
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (!Response.IsValid() || !Response->TryGetArrayField(TEXT("components"), Rows) || !Rows) return nullptr;
		for (const TSharedPtr<FJsonValue>& Value : *Rows)
		{
			const TSharedPtr<FJsonObject> Row = Value.IsValid() ? Value->AsObject() : nullptr;
			if (Row.IsValid() && Row->GetStringField(TEXT("name")) == Name) return Row;
		}
		return nullptr;
	}

	FVector MCPPreviewTreeVector(const TSharedPtr<FJsonObject>& Obj)
	{
		if (!Obj.IsValid()) return FVector(TNumericLimits<double>::Max());
		return FVector(Obj->GetNumberField(TEXT("x")), Obj->GetNumberField(TEXT("y")), Obj->GetNumberField(TEXT("z")));
	}

	/** location of a { location, rotation, scale } field. */
	FVector MCPPreviewTreeLocation(const TSharedPtr<FJsonObject>& Row, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Transform = nullptr;
		if (!Row.IsValid() || !Row->TryGetObjectField(Field, Transform) || !Transform) return FVector(TNumericLimits<double>::Max());
		const TSharedPtr<FJsonObject>* Location = nullptr;
		if (!(*Transform)->TryGetObjectField(TEXT("location"), Location) || !Location) return FVector(TNumericLimits<double>::Max());
		return MCPPreviewTreeVector(*Location);
	}

	/** yaw of a { location, rotation, scale } field. */
	double MCPPreviewTreeYaw(const TSharedPtr<FJsonObject>& Row, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Transform = nullptr;
		if (!Row.IsValid() || !Row->TryGetObjectField(Field, Transform) || !Transform) return TNumericLimits<double>::Max();
		const TSharedPtr<FJsonObject>* Rotation = nullptr;
		if (!(*Transform)->TryGetObjectField(TEXT("rotation"), Rotation) || !Rotation) return TNumericLimits<double>::Max();
		return (*Rotation)->GetNumberField(TEXT("yaw"));
	}

	template <typename T>
	T* MCPPreviewTreeAdd(AActor* Owner, const TCHAR* Name, USceneComponent* Parent, const FTransform& Relative, FName Socket = NAME_None)
	{
		T* Component = NewObject<T>(Owner, Name);
		if (Parent)
		{
			Component->SetupAttachment(Parent, Socket);
		}
		else
		{
			Owner->SetRootComponent(Component);
		}
		Component->SetRelativeTransform(Relative);
		Component->RegisterComponent();
		return Component;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLevelComponentTreePreviewWorldTest,
	"UE.MCP.Level.ComponentTree.PreviewWorldRelativeAndSockets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLevelComponentTreePreviewWorldTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FLevelHandlers::RegisterHandlers(Registry);

	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	USkeletalMesh* SkeletalCube = LoadObject<USkeletalMesh>(nullptr, TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
	if (!TestNotNull(TEXT("the engine cube loads"), Cube)) return false;

	FPreviewScene Scene{FPreviewScene::ConstructionValues()};
	UWorld* PreviewWorld = Scene.GetWorld();
	if (!TestNotNull(TEXT("the preview scene has a world"), PreviewWorld)) return false;
	TestEqual(TEXT("which is a preview world"), MCPDescribeWorldType(PreviewWorld), TEXT("editorPreview"));

	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = TEXT("UEMCP_PreviewTreeActor");
	AActor* Actor = PreviewWorld->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, SpawnParams);
	if (!TestNotNull(TEXT("an actor spawns in the preview world"), Actor)) return false;

	USceneComponent* Root = MCPPreviewTreeAdd<USceneComponent>(Actor, TEXT("Root"), nullptr, FTransform::Identity);
	UStaticMeshComponent* Body = MCPPreviewTreeAdd<UStaticMeshComponent>(
		Actor, TEXT("Body"), Root, FTransform(FRotator::ZeroRotator, FVector(10.0, 20.0, 30.0)));
	Body->SetStaticMesh(Cube);
	Body->SetHiddenInGame(true);
	USceneComponent* Eye = MCPPreviewTreeAdd<USceneComponent>(
		Actor, TEXT("Eye"), Root, FTransform(FRotator(0.0, 90.0, 0.0), FVector(100.0, 0.0, 50.0)));
	USkeletalMeshComponent* Arms = nullptr;
	FName FirstBone = NAME_None;
	if (SkeletalCube)
	{
		Arms = MCPPreviewTreeAdd<USkeletalMeshComponent>(
			Actor, TEXT("Arms"), Eye, FTransform(FRotator::ZeroRotator, FVector(5.0, 0.0, 0.0)));
		Arms->SetSkeletalMeshAsset(SkeletalCube);
		if (SkeletalCube->GetRefSkeleton().GetNum() > 0)
		{
			FirstBone = SkeletalCube->GetRefSkeleton().GetBoneName(SkeletalCube->GetRefSkeleton().GetNum() - 1);
		}
	}
	Actor->UpdateComponentTransforms();

	const FString WorldPath = PreviewWorld->GetPathName();
	const FString ActorPath = Actor->GetPathName();

	// The preview world by its path. Labels are an editor-world idea, so the
	// actor is addressed by path, and the name pass also finds it.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("world"), WorldPath);
		Request->SetStringField(TEXT("actorLabel"), Actor->GetName());
		Request->SetStringField(TEXT("relativeTo"), TEXT("eye"));
		const TSharedPtr<FJsonObject> Response = MCPPreviewTreeRun(Registry, Request);
		TestTrue(TEXT("a world path reaches the preview world"), MCPPreviewTreeSucceeded(Response));
		if (Response.IsValid())
		{
			TestEqual(TEXT("the response names the world that answered"), Response->GetStringField(TEXT("world")), WorldPath);
			TestEqual(TEXT("and its kind"), Response->GetStringField(TEXT("worldType")), FString(TEXT("editorPreview")));
			TestEqual(TEXT("relativeTo is echoed by its real name"), Response->GetStringField(TEXT("relativeTo")), FString(TEXT("Eye")));
		}

		const TSharedPtr<FJsonObject> BodyRow = MCPPreviewTreeRow(Response, TEXT("Body"));
		if (TestNotNull(TEXT("the static mesh row is listed"), BodyRow.Get()))
		{
			TestEqual(TEXT("its attach parent"), BodyRow->GetStringField(TEXT("attachParent")), FString(TEXT("Root")));
			TestEqual(TEXT("its mesh"), BodyRow->GetStringField(TEXT("staticMesh")), Cube->GetPathName());
			TestTrue(TEXT("hiddenInGame is reported"), BodyRow->GetBoolField(TEXT("hiddenInGame")));
			TestTrue(TEXT("relativeLocation is the authored offset"),
				MCPPreviewTreeVector(BodyRow->GetObjectField(TEXT("relativeLocation"))).Equals(FVector(10.0, 20.0, 30.0), 1e-3));

			const FTransform Expected = Body->GetComponentTransform().GetRelativeTransform(Eye->GetComponentTransform());
			TestTrue(TEXT("relativeToComponent is the engine's own composition"),
				MCPPreviewTreeLocation(BodyRow, TEXT("relativeToComponent")).Equals(Expected.GetLocation(), 1e-3));
			TestTrue(TEXT("and carries the frame's rotation"),
				FMath::IsNearlyEqual(MCPPreviewTreeYaw(BodyRow, TEXT("relativeToComponent")), Expected.Rotator().Yaw, 1e-3));
			TestFalse(TEXT("a component without sockets grows no sockets field"), BodyRow->HasField(TEXT("sockets")));
		}

		const TSharedPtr<FJsonObject> EyeRow = MCPPreviewTreeRow(Response, TEXT("Eye"));
		if (TestNotNull(TEXT("the frame row is listed"), EyeRow.Get()))
		{
			TestTrue(TEXT("the frame is at the origin of its own space"),
				MCPPreviewTreeLocation(EyeRow, TEXT("relativeToComponent")).Equals(FVector::ZeroVector, 1e-3));
		}
	}

	// With world omitted, the actor's own path picks its world.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorPath"), ActorPath);
		const TSharedPtr<FJsonObject> Response = MCPPreviewTreeRun(Registry, Request);
		TestTrue(TEXT("an actorPath in a preview world resolves without world"), MCPPreviewTreeSucceeded(Response));
		if (Response.IsValid())
		{
			TestEqual(TEXT("and reads that world"), Response->GetStringField(TEXT("world")), WorldPath);
		}
	}

	// Bones and sockets on the skeletal mesh, in component space.
	if (Arms && FirstBone != NAME_None)
	{
		TArray<TSharedPtr<FJsonValue>> Names;
		Names.Add(MakeShared<FJsonValueString>(FirstBone.ToString()));
		Names.Add(MakeShared<FJsonValueString>(TEXT("UEMCP_NoSuchSocket")));
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorPath"), ActorPath);
		Request->SetArrayField(TEXT("socketNames"), Names);
		Request->SetStringField(TEXT("relativeTo"), TEXT("Eye"));
		const TSharedPtr<FJsonObject> Response = MCPPreviewTreeRun(Registry, Request);
		const TSharedPtr<FJsonObject> ArmsRow = MCPPreviewTreeRow(Response, TEXT("Arms"));
		if (TestNotNull(TEXT("the skeletal mesh row is listed"), ArmsRow.Get()))
		{
			TestEqual(TEXT("its attach parent"), ArmsRow->GetStringField(TEXT("attachParent")), FString(TEXT("Eye")));
			TestEqual(TEXT("its mesh"), ArmsRow->GetStringField(TEXT("skeletalMesh")), SkeletalCube->GetPathName());
			const TArray<TSharedPtr<FJsonValue>>* Sockets = nullptr;
			if (TestTrue(TEXT("it reports the named sockets"), ArmsRow->TryGetArrayField(TEXT("sockets"), Sockets) && Sockets && Sockets->Num() == 2))
			{
				const TSharedPtr<FJsonObject> Bone = (*Sockets)[0]->AsObject();
				TestTrue(TEXT("the bone is found"), Bone->GetBoolField(TEXT("found")));
				TestEqual(TEXT("as a bone"), Bone->GetStringField(TEXT("kind")), FString(TEXT("bone")));
				const FTransform Expected = Arms->GetSocketTransform(FirstBone, RTS_Component);
				TestTrue(TEXT("at its component-space transform"),
					MCPPreviewTreeLocation(Bone, TEXT("componentSpace")).Equals(Expected.GetLocation(), 1e-3));
				const FTransform ExpectedRelative = Arms->GetSocketTransform(FirstBone, RTS_World).GetRelativeTransform(Eye->GetComponentTransform());
				TestTrue(TEXT("and in the relativeTo frame"),
					MCPPreviewTreeLocation(Bone, TEXT("relativeToComponent")).Equals(ExpectedRelative.GetLocation(), 1e-3));

				const TSharedPtr<FJsonObject> Missing = (*Sockets)[1]->AsObject();
				TestFalse(TEXT("a missing name is reported as not found"), Missing->GetBoolField(TEXT("found")));
				TestFalse(TEXT("with no transform"), Missing->HasField(TEXT("componentSpace")));
			}
		}
	}
	else
	{
		AddWarning(TEXT("/Engine/EngineMeshes/SkeletalCube is not available, so the bone half was skipped"));
	}

	// Refusals: a world path that names nothing, and a relativeTo typo.
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("world"), TEXT("/Engine/Transient.UEMCP_NoSuchWorld"));
		Request->SetStringField(TEXT("actorPath"), ActorPath);
		TestFalse(TEXT("an unknown world path is refused"), MCPPreviewTreeSucceeded(MCPPreviewTreeRun(Registry, Request)));
	}
	{
		TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
		Request->SetStringField(TEXT("actorPath"), ActorPath);
		Request->SetStringField(TEXT("relativeTo"), TEXT("UEMCP_NoSuchComponent"));
		const TSharedPtr<FJsonObject> Response = MCPPreviewTreeRun(Registry, Request);
		TestFalse(TEXT("an unknown relativeTo is refused"), MCPPreviewTreeSucceeded(Response));
		if (Response.IsValid())
		{
			TestTrue(TEXT("naming the real components"), Response->GetStringField(TEXT("error")).Contains(TEXT("Eye")));
		}
	}

	Actor->Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
