#if WITH_DEV_AUTOMATION_TESTS

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "HandlerUtils.h"
#include "HandlerRegistry.h"
#include "Handlers/DiffHandlers.h"
#include "Misc/AutomationTest.h"
#include "ReferenceSkeleton.h"
#include "UObject/ObjectSaveContext.h"
#include "UObject/Package.h"

#include <initializer_list>

namespace
{
struct FTestBone
{
	const TCHAR* Name;
	int32 ParentIndex;
};

class FTransientSkeletonPackage
{
public:
	explicit FTransientSkeletonPackage(const TCHAR* TestName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/UEMCP_%s_%s"),
			TestName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		Package = CreatePackage(*PackageName);
		Package->SetPackageFlags(PKG_Transient);
		Package->SetDirtyFlag(false);
	}

	~FTransientSkeletonPackage()
	{
		if (Package)
		{
			Package->SetDirtyFlag(false);
			Package->MarkAsGarbage();
		}
	}

	USkeleton* AddSkeleton(const TCHAR* Name, std::initializer_list<FTestBone> Bones)
	{
		USkeleton* Skeleton = NewObject<USkeleton>(Package, FName(Name), RF_Transient);
		{
			FReferenceSkeletonModifier Modifier(Skeleton);
			for (const FTestBone& Bone : Bones)
			{
				Modifier.Add(
					FMeshBoneInfo(FName(Bone.Name), Bone.Name, Bone.ParentIndex),
					FTransform::Identity);
			}
		}
		return Skeleton;
	}

	USkeletalMesh* AddSkeletalMesh(
		const TCHAR* Name,
		std::initializer_list<FTestBone> Bones,
		USkeleton* AssociatedSkeleton)
	{
		USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Package, FName(Name), RF_Transient);
		FReferenceSkeleton ReferenceSkeleton;
		{
			FReferenceSkeletonModifier Modifier(ReferenceSkeleton, AssociatedSkeleton);
			for (const FTestBone& Bone : Bones)
			{
				Modifier.Add(
					FMeshBoneInfo(FName(Bone.Name), Bone.Name, Bone.ParentIndex),
					FTransform::Identity);
			}
		}
		Mesh->SetRefSkeleton(ReferenceSkeleton);
		Mesh->SetSkeleton(AssociatedSkeleton);
		return Mesh;
	}

	UObject* AddWrongTypeObject(const TCHAR* Name)
	{
		return NewObject<UObject>(Package, FName(Name), RF_Transient);
	}

	void ResetDirty() const
	{
		Package->SetDirtyFlag(false);
	}

	UPackage* GetPackage() const
	{
		return Package;
	}

private:
	UPackage* Package = nullptr;
};

class FPackageSaveObserver
{
public:
	explicit FPackageSaveObserver(UPackage* InPackage)
		: ObservedPackage(InPackage)
	{
		Handle = UPackage::PreSavePackageWithContextEvent.AddLambda(
			[this](UPackage* Package, FObjectPreSaveContext)
			{
				if (Package == ObservedPackage)
				{
					++SaveAttempts;
				}
			});
	}

	~FPackageSaveObserver()
	{
		UPackage::PreSavePackageWithContextEvent.Remove(Handle);
	}

	int32 GetSaveAttempts() const
	{
		return SaveAttempts;
	}

private:
	UPackage* ObservedPackage = nullptr;
	FDelegateHandle Handle;
	int32 SaveAttempts = 0;
};

TSharedPtr<FJsonObject> MakeDiffRequest(const FString& FromPath, const FString& ToPath)
{
	TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("assetPath"), FromPath);
	if (!ToPath.IsEmpty())
	{
		Request->SetStringField(TEXT("otherPath"), ToPath);
	}
	return Request;
}

TSharedPtr<FJsonObject> ExecuteDiff(
	FMCPHandlerRegistry& Registry,
	const FString& FromPath,
	const FString& ToPath)
{
	const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(
		TEXT("diff_asset"),
		MakeDiffRequest(FromPath, ToPath));
	return Response.IsValid() && Response->Type == EJson::Object
		? Response->AsObject()
		: nullptr;
}

TSharedPtr<FJsonObject> ExecuteDiff(
	FMCPHandlerRegistry& Registry,
	const UObject* From,
	const UObject* To)
{
	return ExecuteDiff(
		Registry,
		From->GetPathName(),
		To ? To->GetPathName() : FString());
}

TArray<FString> GetStringArray(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
{
	TArray<FString> Result;
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	if (Object.IsValid() && Object->TryGetArrayField(Field, Values) && Values)
	{
		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString StringValue;
			if (Value.IsValid() && Value->TryGetString(StringValue))
			{
				Result.Add(MoveTemp(StringValue));
			}
		}
	}
	return Result;
}

const TArray<TSharedPtr<FJsonValue>>* GetArray(
	const TSharedPtr<FJsonObject>& Object,
	const TCHAR* Field)
{
	const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
	return Object.IsValid() && Object->TryGetArrayField(Field, Values) ? Values : nullptr;
}

bool AddNamedVirtualBoneForTest(
	USkeleton* Skeleton,
	const FName SourceBone,
	const FName TargetBone,
	const FName DesiredName,
	FName& OutActualName)
{
#if UE_MCP_HAS_5_5_API
	OutActualName = DesiredName;
	return Skeleton->AddNewNamedVirtualBone(SourceBone, TargetBone, DesiredName);
#else
	OutActualName = NAME_None;
	return Skeleton->AddNewVirtualBone(SourceBone, TargetBone, OutActualName);
#endif
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSkeletonDiffRegistrationAndValidationTest,
	"UE.MCP.Diff.Skeleton.RegistrationAndValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkeletonDiffRegistrationAndValidationTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FDiffHandlers::RegisterHandlers(Registry);
	TestTrue(TEXT("diff_asset is registered"), Registry.HasHandler(TEXT("diff_asset")));

	FTransientSkeletonPackage Fixture(TEXT("SkeletonDiffValidation"));
	USkeleton* Skeleton = Fixture.AddSkeleton(TEXT("Skeleton"), {
		{ TEXT("root"), INDEX_NONE },
	});
	UObject* WrongType = Fixture.AddWrongTypeObject(TEXT("WrongType"));
	Fixture.ResetDirty();
	FPackageSaveObserver SaveObserver(Fixture.GetPackage());

	const TSharedPtr<FJsonObject> MissingOther = ExecuteDiff(Registry, Skeleton, nullptr);
	TestTrue(TEXT("missing otherPath returns an object"), MissingOther.IsValid());
	if (MissingOther.IsValid())
	{
		TestFalse(TEXT("missing otherPath fails"), MissingOther->GetBoolField(TEXT("success")));
		TestTrue(
			TEXT("missing otherPath names the missing parameter"),
			MissingOther->GetStringField(TEXT("error")).Contains(TEXT("otherPath")));
	}

	const TSharedPtr<FJsonObject> WrongTypeResult = ExecuteDiff(Registry, Skeleton, WrongType);
	TestTrue(TEXT("wrong-type otherPath returns an object"), WrongTypeResult.IsValid());
	if (WrongTypeResult.IsValid())
	{
		TestFalse(TEXT("wrong-type otherPath fails"), WrongTypeResult->GetBoolField(TEXT("success")));
		const FString Error = WrongTypeResult->GetStringField(TEXT("error"));
		TestTrue(TEXT("wrong type requires an exact Skeleton"), Error.Contains(TEXT("must be exactly a Skeleton")));
		TestTrue(TEXT("wrong type identifies the resolved class"), Error.Contains(TEXT("Object")));
	}

	const FString MissingFromPath = Fixture.GetPackage()->GetName() + TEXT(".MissingFrom");
	const TSharedPtr<FJsonObject> MissingFrom = ExecuteDiff(
		Registry,
		MissingFromPath,
		Skeleton->GetPathName());
	TestTrue(TEXT("unloadable assetPath returns an object"), MissingFrom.IsValid());
	if (MissingFrom.IsValid())
	{
		TestFalse(TEXT("unloadable assetPath fails"), MissingFrom->GetBoolField(TEXT("success")));
		const FString Error = MissingFrom->GetStringField(TEXT("error"));
		TestTrue(TEXT("unloadable assetPath has a clear not-found error"), Error.Contains(TEXT("Asset not found")));
		TestTrue(TEXT("unloadable assetPath error identifies the path"), Error.Contains(MissingFromPath));
	}

	const FString MissingToPath = Fixture.GetPackage()->GetName() + TEXT(".MissingTo");
	const TSharedPtr<FJsonObject> MissingTo = ExecuteDiff(
		Registry,
		Skeleton->GetPathName(),
		MissingToPath);
	TestTrue(TEXT("unloadable otherPath returns an object"), MissingTo.IsValid());
	if (MissingTo.IsValid())
	{
		TestFalse(TEXT("unloadable otherPath fails"), MissingTo->GetBoolField(TEXT("success")));
		const FString Error = MissingTo->GetStringField(TEXT("error"));
		TestTrue(TEXT("unloadable otherPath has a clear not-found error"), Error.Contains(TEXT("Asset not found")));
		TestTrue(TEXT("unloadable otherPath error identifies the path"), Error.Contains(MissingToPath));
	}
	TestFalse(TEXT("validation does not dirty the fixture package"), Fixture.GetPackage()->IsDirty());
	TestEqual(TEXT("validation never attempts to save the fixture package"), SaveObserver.GetSaveAttempts(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSkeletonDiffIdenticalAndSideEffectsTest,
	"UE.MCP.Diff.Skeleton.IdenticalAndSideEffects",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkeletonDiffIdenticalAndSideEffectsTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FDiffHandlers::RegisterHandlers(Registry);
	FTransientSkeletonPackage Fixture(TEXT("SkeletonDiffIdentical"));
	USkeleton* Skeleton = Fixture.AddSkeleton(TEXT("Skeleton"), {
		{ TEXT("root"), INDEX_NONE },
		{ TEXT("pelvis"), 0 },
	});
	Fixture.ResetDirty();
	FPackageSaveObserver SaveObserver(Fixture.GetPackage());

	const TSharedPtr<FJsonObject> Result = ExecuteDiff(Registry, Skeleton, Skeleton);
	TestTrue(TEXT("same-object diff returns an object"), Result.IsValid());
	if (Result.IsValid())
	{
		TestTrue(TEXT("same-object diff succeeds"), Result->GetBoolField(TEXT("success")));
		TestEqual(TEXT("asset type is Skeleton"), Result->GetStringField(TEXT("assetType")), FString(TEXT("Skeleton")));
		TestTrue(TEXT("same-object diff is identical"), Result->GetBoolField(TEXT("identical")));
		TestTrue(TEXT("same-object diff is structurally identical"), Result->GetBoolField(TEXT("structurallyIdentical")));
		TestEqual(TEXT("identical diff has zero changes"), static_cast<int32>(Result->GetNumberField(TEXT("changeCount"))), 0);
		TestEqual(TEXT("raw count is reported"), static_cast<int32>(Result->GetNumberField(TEXT("rawBoneCountFrom"))), 2);
		TestEqual(TEXT("final count is reported"), static_cast<int32>(Result->GetNumberField(TEXT("boneCountFrom"))), 2);
		TestTrue(TEXT("same object is editor-compatible"), Result->GetBoolField(TEXT("editorCompatible")));
		TestTrue(TEXT("identical hierarchy is compatible"), Result->GetBoolField(TEXT("hierarchyCompatible")));
		TestEqual(TEXT("identical diff has no added raw bones"), GetStringArray(Result, TEXT("rawBonesAdded")).Num(), 0);
	}
	TestFalse(TEXT("read-only diff does not dirty its package"), Fixture.GetPackage()->IsDirty());
	TestEqual(TEXT("read-only diff never attempts to save its package"), SaveObserver.GetSaveAttempts(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSkeletonDiffFNameIdentityTest,
	"UE.MCP.Diff.Skeleton.FNameIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkeletonDiffFNameIdentityTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FDiffHandlers::RegisterHandlers(Registry);
	FTransientSkeletonPackage Fixture(TEXT("SkeletonDiffFNameIdentity"));
	USkeleton* From = Fixture.AddSkeleton(TEXT("From"), {
		{ TEXT("root"), INDEX_NONE },
		{ TEXT("pelvis"), 0 },
		{ TEXT("hand"), 1 },
	});
	USkeleton* To = Fixture.AddSkeleton(TEXT("To"), {
		{ TEXT("ROOT"), INDEX_NONE },
		{ TEXT("PELVIS"), 0 },
		{ TEXT("HAND"), 1 },
	});
	FName FromVirtualName;
	FName ToVirtualName;
	TestTrue(
		TEXT("from virtual bone is created"),
		AddNamedVirtualBoneForTest(From, TEXT("pelvis"), TEXT("hand"), TEXT("VB Aim"), FromVirtualName));
	TestTrue(
		TEXT("to virtual bone is created"),
		AddNamedVirtualBoneForTest(To, TEXT("PELVIS"), TEXT("HAND"), TEXT("vb aim"), ToVirtualName));
	TestEqual(TEXT("case-only virtual bone names retain FName identity"), FromVirtualName, ToVirtualName);
	Fixture.ResetDirty();

	const TSharedPtr<FJsonObject> Result = ExecuteDiff(Registry, From, To);
	TestTrue(TEXT("case-only diff succeeds"), Result.IsValid() && Result->GetBoolField(TEXT("success")));
	if (Result.IsValid())
	{
		TestTrue(TEXT("FName-equivalent skeletons are structurally identical"), Result->GetBoolField(TEXT("structurallyIdentical")));
		TestTrue(TEXT("FName-equivalent hierarchy is compatible"), Result->GetBoolField(TEXT("hierarchyCompatible")));
		TestEqual(TEXT("case-only names add no changes"), static_cast<int32>(Result->GetNumberField(TEXT("changeCount"))), 0);
		TestEqual(TEXT("case-only raw names are not added"), GetStringArray(Result, TEXT("rawBonesAdded")).Num(), 0);
		TestEqual(TEXT("case-only raw names are not removed"), GetStringArray(Result, TEXT("rawBonesRemoved")).Num(), 0);
		const TArray<TSharedPtr<FJsonValue>>* AddedVirtual = GetArray(Result, TEXT("virtualBonesAdded"));
		const TArray<TSharedPtr<FJsonValue>>* RemovedVirtual = GetArray(Result, TEXT("virtualBonesRemoved"));
		TestTrue(TEXT("case-only virtual names are not added"), AddedVirtual && AddedVirtual->Num() == 0);
		TestTrue(TEXT("case-only virtual names are not removed"), RemovedVirtual && RemovedVirtual->Num() == 0);
	}
	TestFalse(TEXT("case-only diff does not dirty its package"), Fixture.GetPackage()->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSkeletonDiffDirectionalRawBoneTest,
	"UE.MCP.Diff.Skeleton.DirectionalRawBonesAndOrdering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkeletonDiffDirectionalRawBoneTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FDiffHandlers::RegisterHandlers(Registry);
	FTransientSkeletonPackage Fixture(TEXT("SkeletonDiffRaw"));
	USkeleton* From = Fixture.AddSkeleton(TEXT("From"), {
		{ TEXT("root"), INDEX_NONE },
		{ TEXT("RemovedZulu"), 0 },
		{ TEXT("RemovedAlpha"), 0 },
	});
	USkeleton* To = Fixture.AddSkeleton(TEXT("To"), {
		{ TEXT("root"), INDEX_NONE },
		{ TEXT("AddedZulu"), 0 },
		{ TEXT("AddedAlpha"), 0 },
	});
	Fixture.ResetDirty();

	const TSharedPtr<FJsonObject> Forward = ExecuteDiff(Registry, From, To);
	TestTrue(TEXT("forward raw-bone diff succeeds"), Forward.IsValid() && Forward->GetBoolField(TEXT("success")));
	if (Forward.IsValid())
	{
		const TArray<FString> Added = GetStringArray(Forward, TEXT("rawBonesAdded"));
		const TArray<FString> Removed = GetStringArray(Forward, TEXT("rawBonesRemoved"));
		TestEqual(TEXT("two raw bones are added"), Added.Num(), 2);
		TestEqual(TEXT("two raw bones are removed"), Removed.Num(), 2);
		if (Added.Num() == 2)
		{
			TestEqual(TEXT("added bones are sorted first"), Added[0], FString(TEXT("AddedAlpha")));
			TestEqual(TEXT("added bones are sorted second"), Added[1], FString(TEXT("AddedZulu")));
		}
		if (Removed.Num() == 2)
		{
			TestEqual(TEXT("removed bones are sorted first"), Removed[0], FString(TEXT("RemovedAlpha")));
			TestEqual(TEXT("removed bones are sorted second"), Removed[1], FString(TEXT("RemovedZulu")));
		}
		TestTrue(TEXT("additive/removal-only hierarchy stays compatible"), Forward->GetBoolField(TEXT("hierarchyCompatible")));
	}

	const TSharedPtr<FJsonObject> Reverse = ExecuteDiff(Registry, To, From);
	TestTrue(TEXT("reverse raw-bone diff succeeds"), Reverse.IsValid() && Reverse->GetBoolField(TEXT("success")));
	if (Reverse.IsValid())
	{
		TestTrue(
			TEXT("reverse added set is the forward removed set"),
			GetStringArray(Reverse, TEXT("rawBonesAdded")) == GetStringArray(Forward, TEXT("rawBonesRemoved")));
		TestTrue(
			TEXT("reverse removed set is the forward added set"),
			GetStringArray(Reverse, TEXT("rawBonesRemoved")) == GetStringArray(Forward, TEXT("rawBonesAdded")));
	}
	TestFalse(TEXT("directional diff does not dirty its package"), Fixture.GetPackage()->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSkeletonDiffRawBoneIndexTest,
	"UE.MCP.Diff.Skeleton.RawBoneIndexChanges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkeletonDiffRawBoneIndexTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FDiffHandlers::RegisterHandlers(Registry);
	FTransientSkeletonPackage Fixture(TEXT("SkeletonDiffRawIndex"));
	USkeleton* From = Fixture.AddSkeleton(TEXT("From"), {
		{ TEXT("root"), INDEX_NONE },
		{ TEXT("SiblingZulu"), 0 },
		{ TEXT("SiblingAlpha"), 0 },
	});
	USkeleton* To = Fixture.AddSkeleton(TEXT("To"), {
		{ TEXT("root"), INDEX_NONE },
		{ TEXT("SiblingAlpha"), 0 },
		{ TEXT("SiblingZulu"), 0 },
	});
	Fixture.ResetDirty();

	const TSharedPtr<FJsonObject> Result = ExecuteDiff(Registry, From, To);
	TestTrue(TEXT("raw-index diff succeeds"), Result.IsValid() && Result->GetBoolField(TEXT("success")));
	if (Result.IsValid())
	{
		TestFalse(TEXT("sibling reordering is not structurally identical"), Result->GetBoolField(TEXT("structurallyIdentical")));
		TestTrue(TEXT("sibling reordering preserves hierarchy compatibility"), Result->GetBoolField(TEXT("hierarchyCompatible")));
		TestEqual(TEXT("sibling reordering counts two index changes"), static_cast<int32>(Result->GetNumberField(TEXT("changeCount"))), 2);
		TestEqual(TEXT("sibling reordering adds no raw bones"), GetStringArray(Result, TEXT("rawBonesAdded")).Num(), 0);
		TestEqual(TEXT("sibling reordering removes no raw bones"), GetStringArray(Result, TEXT("rawBonesRemoved")).Num(), 0);
		const TArray<TSharedPtr<FJsonValue>>* IndexChanges = GetArray(Result, TEXT("rawBoneIndexChanges"));
		TestTrue(TEXT("raw index changes are present"), IndexChanges && IndexChanges->Num() == 2);
		if (IndexChanges && IndexChanges->Num() == 2)
		{
			const TSharedPtr<FJsonObject> First = (*IndexChanges)[0]->AsObject();
			const TSharedPtr<FJsonObject> Second = (*IndexChanges)[1]->AsObject();
			TestEqual(TEXT("index changes are sorted by bone name"), First->GetStringField(TEXT("bone")), FString(TEXT("SiblingAlpha")));
			TestEqual(TEXT("first old index is exact"), static_cast<int32>(First->GetNumberField(TEXT("fromIndex"))), 2);
			TestEqual(TEXT("first new index is exact"), static_cast<int32>(First->GetNumberField(TEXT("toIndex"))), 1);
			TestEqual(TEXT("second changed bone is exact"), Second->GetStringField(TEXT("bone")), FString(TEXT("SiblingZulu")));
			TestEqual(TEXT("second old index is exact"), static_cast<int32>(Second->GetNumberField(TEXT("fromIndex"))), 1);
			TestEqual(TEXT("second new index is exact"), static_cast<int32>(Second->GetNumberField(TEXT("toIndex"))), 2);
		}
	}
	TestFalse(TEXT("raw-index diff does not dirty its package"), Fixture.GetPackage()->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSkeletalMeshDiffPublicRouteTest,
	"UE.MCP.Diff.Skeleton.SkeletalMeshPublicRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkeletalMeshDiffPublicRouteTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FDiffHandlers::RegisterHandlers(Registry);
	FTransientSkeletonPackage Fixture(TEXT("SkeletalMeshDiffRoute"));
	USkeleton* FromPolicy = Fixture.AddSkeleton(TEXT("FromPolicy"), {
		{ TEXT("root"), INDEX_NONE },
		{ TEXT("pelvis"), 0 },
	});
	USkeleton* ToPolicy = Fixture.AddSkeleton(TEXT("ToPolicy"), {
		{ TEXT("root"), INDEX_NONE },
		{ TEXT("pelvis"), 0 },
		{ TEXT("hand_attach"), 1 },
	});
	FName FromVirtualName;
	FName ToVirtualName;
	TestTrue(
		TEXT("from policy virtual bone is created"),
		AddNamedVirtualBoneForTest(FromPolicy, TEXT("root"), TEXT("pelvis"), TEXT("VB shared"), FromVirtualName));
	TestTrue(
		TEXT("to policy virtual bone is created"),
		AddNamedVirtualBoneForTest(ToPolicy, TEXT("root"), TEXT("pelvis"), TEXT("VB shared"), ToVirtualName));
	USkeletalMesh* From = Fixture.AddSkeletalMesh(TEXT("SK_From"), {
		{ TEXT("root"), INDEX_NONE },
		{ TEXT("pelvis"), 0 },
	}, FromPolicy);
	USkeletalMesh* To = Fixture.AddSkeletalMesh(TEXT("SK_To"), {
		{ TEXT("root"), INDEX_NONE },
		{ TEXT("pelvis"), 0 },
		{ TEXT("hand_attach"), 1 },
	}, ToPolicy);
	Fixture.ResetDirty();
	FPackageSaveObserver SaveObserver(Fixture.GetPackage());

	const TSharedPtr<FJsonObject> Result = ExecuteDiff(Registry, From, To);
	TestTrue(TEXT("public diff_asset routes SkeletalMesh assets"), Result.IsValid() && Result->GetBoolField(TEXT("success")));
	if (Result.IsValid())
	{
		TestEqual(TEXT("asset type is SkeletalMesh"), Result->GetStringField(TEXT("assetType")), FString(TEXT("SkeletalMesh")));
		TestEqual(TEXT("mesh raw count from is compact"), static_cast<int32>(Result->GetNumberField(TEXT("rawBoneCountFrom"))), 2);
		TestEqual(TEXT("mesh raw count to is compact"), static_cast<int32>(Result->GetNumberField(TEXT("rawBoneCountTo"))), 3);
		const TArray<FString> Added = GetStringArray(Result, TEXT("rawBonesAdded"));
		TestEqual(TEXT("mesh diff reports one added bone"), Added.Num(), 1);
		if (Added.Num() == 1)
		{
			TestEqual(TEXT("mesh added bone is exact"), Added[0], FString(TEXT("hand_attach")));
		}
		TestTrue(TEXT("additive mesh hierarchy remains compatible"), Result->GetBoolField(TEXT("hierarchyCompatible")));
		TestEqual(TEXT("mesh policy virtual bones are compared"), static_cast<int32>(Result->GetNumberField(TEXT("virtualBoneCountFrom"))), 1);
		TestEqual(TEXT("from virtual source is the associated Skeleton"), Result->GetStringField(TEXT("virtualBoneSourceFrom")), FromPolicy->GetPathName());
		TestFalse(TEXT("reference-pose omission is explicit"), Result->GetBoolField(TEXT("referencePoseCompared")));
		TestFalse(TEXT("export-name omission is explicit"), Result->GetBoolField(TEXT("exportNamesCompared")));
	}
	TestFalse(TEXT("SkeletalMesh diff stays read-only"), Fixture.GetPackage()->IsDirty());
	TestEqual(TEXT("SkeletalMesh diff never saves"), SaveObserver.GetSaveAttempts(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSkeletonDiffReparentTest,
	"UE.MCP.Diff.Skeleton.ReparentedHierarchy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkeletonDiffReparentTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FDiffHandlers::RegisterHandlers(Registry);
	FTransientSkeletonPackage Fixture(TEXT("SkeletonDiffReparent"));
	USkeleton* From = Fixture.AddSkeleton(TEXT("From"), {
		{ TEXT("root"), INDEX_NONE },
		{ TEXT("ParentA"), 0 },
		{ TEXT("ParentB"), 0 },
		{ TEXT("CommonBone"), 1 },
	});
	USkeleton* To = Fixture.AddSkeleton(TEXT("To"), {
		{ TEXT("root"), INDEX_NONE },
		{ TEXT("ParentB"), 0 },
		{ TEXT("ParentA"), 0 },
		{ TEXT("CommonBone"), 1 },
	});
	Fixture.ResetDirty();

	const TSharedPtr<FJsonObject> Result = ExecuteDiff(Registry, From, To);
	TestTrue(TEXT("reparent diff succeeds"), Result.IsValid() && Result->GetBoolField(TEXT("success")));
	if (Result.IsValid())
	{
		TestFalse(TEXT("reparented hierarchy is incompatible"), Result->GetBoolField(TEXT("hierarchyCompatible")));
		TestEqual(TEXT("numeric sibling reordering does not look like add/remove"), GetStringArray(Result, TEXT("rawBonesAdded")).Num(), 0);
		const TArray<TSharedPtr<FJsonValue>>* Reparented = GetArray(Result, TEXT("reparented"));
		TestTrue(TEXT("reparented array is present"), Reparented != nullptr);
		if (Reparented && Reparented->Num() == 1)
		{
			const TSharedPtr<FJsonObject> Delta = (*Reparented)[0]->AsObject();
			TestEqual(TEXT("reparented bone is named"), Delta->GetStringField(TEXT("bone")), FString(TEXT("CommonBone")));
			TestEqual(TEXT("old parent is name-derived"), Delta->GetStringField(TEXT("fromParent")), FString(TEXT("ParentA")));
			TestEqual(TEXT("new parent is name-derived"), Delta->GetStringField(TEXT("toParent")), FString(TEXT("ParentB")));
		}
		else
		{
			AddError(TEXT("Expected exactly one reparented bone"));
		}
	}
	TestFalse(TEXT("reparent diff does not dirty its package"), Fixture.GetPackage()->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSkeletonDiffVirtualBoneTest,
	"UE.MCP.Diff.Skeleton.VirtualBones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkeletonDiffVirtualBoneTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FDiffHandlers::RegisterHandlers(Registry);
	FTransientSkeletonPackage Fixture(TEXT("SkeletonDiffVirtual"));
	USkeleton* From = Fixture.AddSkeleton(TEXT("From"), {
		{ TEXT("root"), INDEX_NONE },
		{ TEXT("Source"), 0 },
		{ TEXT("TargetA"), 0 },
		{ TEXT("TargetB"), 0 },
	});
	USkeleton* To = Fixture.AddSkeleton(TEXT("To"), {
		{ TEXT("root"), INDEX_NONE },
		{ TEXT("Source"), 0 },
		{ TEXT("TargetA"), 0 },
		{ TEXT("TargetB"), 0 },
	});
	FName FromVirtualName;
	FName ToVirtualName;
	TestTrue(
		TEXT("from virtual bone is created"),
		AddNamedVirtualBoneForTest(From, TEXT("Source"), TEXT("TargetA"), TEXT("VB shared"), FromVirtualName));
	TestTrue(
		TEXT("to virtual bone is created"),
		AddNamedVirtualBoneForTest(To, TEXT("Source"), TEXT("TargetB"), TEXT("VB shared"), ToVirtualName));
	Fixture.ResetDirty();

	const TSharedPtr<FJsonObject> Result = ExecuteDiff(Registry, From, To);
	TestTrue(TEXT("virtual-bone diff succeeds"), Result.IsValid() && Result->GetBoolField(TEXT("success")));
	if (Result.IsValid())
	{
		TestTrue(TEXT("virtual endpoint change is non-identical"), !Result->GetBoolField(TEXT("identical")));
		TestTrue(TEXT("virtual-only delta preserves raw hierarchy compatibility"), Result->GetBoolField(TEXT("hierarchyCompatible")));
		TestEqual(TEXT("declared virtual count from is reported"), static_cast<int32>(Result->GetNumberField(TEXT("virtualBoneCountFrom"))), 1);
		TestEqual(TEXT("final reference count includes valid virtual from"), static_cast<int32>(Result->GetNumberField(TEXT("boneCountFrom"))), 5);

		const TArray<TSharedPtr<FJsonValue>>* Added = GetArray(Result, TEXT("virtualBonesAdded"));
		const TArray<TSharedPtr<FJsonValue>>* Removed = GetArray(Result, TEXT("virtualBonesRemoved"));
		TestTrue(TEXT("changed virtual definition adds one record"), Added && Added->Num() == 1);
		TestTrue(TEXT("changed virtual definition removes one record"), Removed && Removed->Num() == 1);
		if (Added && Added->Num() == 1)
		{
			const TSharedPtr<FJsonObject> Bone = (*Added)[0]->AsObject();
			TestEqual(TEXT("added virtual name"), Bone->GetStringField(TEXT("name")), ToVirtualName.ToString());
			TestEqual(TEXT("added virtual source"), Bone->GetStringField(TEXT("sourceBone")), FString(TEXT("Source")));
			TestEqual(TEXT("added virtual target"), Bone->GetStringField(TEXT("targetBone")), FString(TEXT("TargetB")));
		}
		if (Removed && Removed->Num() == 1)
		{
			const TSharedPtr<FJsonObject> Bone = (*Removed)[0]->AsObject();
			TestEqual(TEXT("removed virtual name"), Bone->GetStringField(TEXT("name")), FromVirtualName.ToString());
			TestEqual(TEXT("removed virtual target"), Bone->GetStringField(TEXT("targetBone")), FString(TEXT("TargetA")));
		}
	}
	TestFalse(TEXT("virtual diff does not dirty its package"), Fixture.GetPackage()->IsDirty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FSkeletonDiffEditorCompatibilityTest,
	"UE.MCP.Diff.Skeleton.EditorCompatibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSkeletonDiffEditorCompatibilityTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FDiffHandlers::RegisterHandlers(Registry);
	FTransientSkeletonPackage Fixture(TEXT("SkeletonDiffCompatibility"));
	USkeleton* From = Fixture.AddSkeleton(TEXT("From"), {
		{ TEXT("root"), INDEX_NONE },
	});
	USkeleton* To = Fixture.AddSkeleton(TEXT("To"), {
		{ TEXT("root"), INDEX_NONE },
	});
	To->AddCompatibleSkeleton(From);
	TestFalse(
		TEXT("forward policy query is false before the reverse-compatible diff"),
		From->IsCompatibleForEditor(To));
	Fixture.ResetDirty();

	const TSharedPtr<FJsonObject> Result = ExecuteDiff(Registry, From, To);
	TestTrue(TEXT("editor-compatibility diff succeeds"), Result.IsValid() && Result->GetBoolField(TEXT("success")));
	if (Result.IsValid())
	{
		TestTrue(TEXT("editorCompatible field is present"), Result->HasTypedField<EJson::Boolean>(TEXT("editorCompatible")));
		TestTrue(TEXT("explicitly registered skeleton is editor-compatible"), Result->GetBoolField(TEXT("editorCompatible")));
		TestTrue(TEXT("hierarchy compatibility remains separately present"), Result->HasTypedField<EJson::Boolean>(TEXT("hierarchyCompatible")));
	}
	TestFalse(TEXT("compatibility query does not dirty its package"), Fixture.GetPackage()->IsDirty());
	return true;
}

#endif
