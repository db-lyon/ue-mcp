// One field deep inside an array of structs on a data asset, written as an
// unsaved preview and read back: the shape of
// `WeaponProfiles[47].Presentation.FirstPersonArmsPrimaryUseAnimation`.
//
// asset(set_property) with save=false is the action for it. It must reach the
// named element and no other, take an object reference by asset path and a
// struct as JSON, leave the package dirty and unwritten, be undoable with the
// editor's undo, hand back a rollback that restores the value without saving,
// and refuse an index or field that does not exist rather than writing
// somewhere else.
//
// Like the #931 persistence test, everything happens under a private mount so
// no package of the attached project is touched.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/MCPNestedArrayPathTestTypes.h"

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Asset/AssetHandlers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "Engine/DataTable.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"
#include "Tests/MCPScopedTestMount.h"

namespace
{
	const TCHAR* const MCPNestedPathTestRoot = TEXT("/UEMCPNestedArrayPathTest/");

	TSharedPtr<FJsonObject> NestedPathCall(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Params);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : MakeShared<FJsonObject>();
	}

	bool NestedPathBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool bDefault = false)
	{
		bool bValue = bDefault;
		Object->TryGetBoolField(Field, bValue);
		return bValue;
	}

	FString NestedPathString(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		FString Value;
		Object->TryGetStringField(Field, Value);
		return Value;
	}

	TSharedPtr<FJsonObject> NestedPathVector(double X, double Y, double Z)
	{
		TSharedPtr<FJsonObject> Vector = MakeShared<FJsonObject>();
		Vector->SetNumberField(TEXT("X"), X);
		Vector->SetNumberField(TEXT("Y"), Y);
		Vector->SetNumberField(TEXT("Z"), Z);
		return Vector;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPAssetNestedArrayPathTest,
	"UE.MCP.Asset.SetProperty.NestedArrayStructFieldUnsaved",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPAssetNestedArrayPathTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ FString(MCPNestedPathTestRoot), TEXT("UEMCPNestedArrayPathTest") };

	const FString AssetPath = FString(MCPNestedPathTestRoot) + TEXT("DA_Profiles");
	UPackage* Package = CreatePackage(*AssetPath);
	if (!TestNotNull(TEXT("fixture package created"), Package)) return false;
	UUEMCPNestedArrayPathTestAsset* Asset = NewObject<UUEMCPNestedArrayPathTestAsset>(
		Package, FName(TEXT("DA_Profiles")), RF_Public | RF_Standalone | RF_Transactional);
	Asset->Profiles.SetNum(3);
	const FGCRootScope KeepAssetAlive(Asset);

	const FString ClipPath = FString(MCPNestedPathTestRoot) + TEXT("DT_Clip");
	UPackage* ClipPackage = CreatePackage(*ClipPath);
	UDataTable* Clip = NewObject<UDataTable>(ClipPackage, FName(TEXT("DT_Clip")), RF_Public | RF_Standalone);
	const FGCRootScope KeepClipAlive(Clip);

	FString PackageFileName;
	FPackageName::TryConvertLongPackageNameToFilename(AssetPath, PackageFileName, FPackageName::GetAssetPackageExtension());

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);

	auto SetUnsaved = [&](const TCHAR* PropertyName, const TSharedPtr<FJsonValue>& Value)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), AssetPath);
		Params->SetStringField(TEXT("propertyName"), PropertyName);
		Params->SetField(TEXT("value"), Value);
		Params->SetBoolField(TEXT("save"), false);
		return NestedPathCall(Registry, TEXT("set_asset_property"), Params);
	};

	// An object reference, by asset path, two levels inside element 2.
	const TSharedPtr<FJsonObject> RefWrite = SetUnsaved(
		TEXT("Profiles[2].Presentation.PrimaryUse"), MakeShared<FJsonValueString>(ClipPath));
	TestTrue(FString::Printf(TEXT("the reference write succeeded (%s)"), *NestedPathString(RefWrite, TEXT("error"))),
		NestedPathBool(RefWrite, TEXT("success")));
	TestTrue(TEXT("the reference landed on element 2"), Asset->Profiles[2].Presentation.PrimaryUse == Clip);
	TestTrue(TEXT("element 1 is untouched"), Asset->Profiles[1].Presentation.PrimaryUse == nullptr);
	TestEqual(TEXT("the previous value is reported"), NestedPathString(RefWrite, TEXT("previousValue")), FString(TEXT("None")));
	TestFalse(TEXT("an unsaved write reports persisted=false"), NestedPathBool(RefWrite, TEXT("persisted"), true));
	TestTrue(TEXT("the package is dirty"), Package->IsDirty());
	TestFalse(TEXT("nothing was written to disk"), IFileManager::Get().FileExists(*PackageFileName));

	const TSharedPtr<FJsonObject>* Rollback = nullptr;
	const TSharedPtr<FJsonObject>* RollbackPayload = nullptr;
	if (TestTrue(TEXT("the write carries a rollback"), RefWrite->TryGetObjectField(TEXT("rollback"), Rollback))
		&& TestTrue(TEXT("the rollback has a payload"), (*Rollback)->TryGetObjectField(TEXT("payload"), RollbackPayload)))
	{
		TestEqual(TEXT("the rollback calls set_asset_property"),
			NestedPathString(*Rollback, TEXT("method")), FString(TEXT("set_asset_property")));
		TestFalse(TEXT("the rollback of an unsaved preview does not save"), NestedPathBool(*RollbackPayload, TEXT("save"), true));
	}

	// A struct as JSON, beside it.
	TSharedPtr<FJsonObject> Rotation = MakeShared<FJsonObject>();
	Rotation->SetNumberField(TEXT("X"), 0.0);
	Rotation->SetNumberField(TEXT("Y"), 0.0);
	Rotation->SetNumberField(TEXT("Z"), UE_HALF_SQRT_2);
	Rotation->SetNumberField(TEXT("W"), UE_HALF_SQRT_2);
	TSharedPtr<FJsonObject> Transform = MakeShared<FJsonObject>();
	Transform->SetObjectField(TEXT("Translation"), NestedPathVector(1.0, 2.0, 3.0));
	Transform->SetObjectField(TEXT("Rotation"), Rotation);
	Transform->SetObjectField(TEXT("Scale3D"), NestedPathVector(1.0, 1.0, 1.0));
	const FTransform Expected(FQuat(0.0, 0.0, UE_HALF_SQRT_2, UE_HALF_SQRT_2), FVector(1.0, 2.0, 3.0));

	const TSharedPtr<FJsonObject> TransformWrite = SetUnsaved(
		TEXT("Profiles[2].Presentation.CameraRelativeTransform"), MakeShared<FJsonValueObject>(Transform));
	TestTrue(FString::Printf(TEXT("the transform write succeeded (%s)"), *NestedPathString(TransformWrite, TEXT("error"))),
		NestedPathBool(TransformWrite, TEXT("success")));
	TestTrue(TEXT("the transform landed on element 2"),
		Asset->Profiles[2].Presentation.CameraRelativeTransform.Equals(Expected, 1e-4));
	TestTrue(TEXT("element 0's transform is untouched"),
		Asset->Profiles[0].Presentation.CameraRelativeTransform.Equals(FTransform::Identity, 0.0));

	// Read back through the same path.
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), AssetPath);
		Params->SetStringField(TEXT("propertyName"), TEXT("Profiles[2].Presentation.PrimaryUse"));
		const TSharedPtr<FJsonObject> Read = NestedPathCall(Registry, TEXT("read_asset_properties"), Params);
		TestTrue(TEXT("read_properties resolves the nested path"), NestedPathBool(Read, TEXT("success")));
		TestTrue(TEXT("read_properties returns the reference"), NestedPathString(Read, TEXT("value")).Contains(TEXT("DT_Clip")));
	}

	// A rejected conversion must not insert an undo step above the good write.
	auto InvalidTransform = MakeShared<FJsonObject>();
	InvalidTransform->SetNumberField(TEXT("NoSuchField"), 1.0);
	const auto RejectedTransform = SetUnsaved(TEXT("Profiles[2].Presentation.CameraRelativeTransform"), MakeShared<FJsonValueObject>(InvalidTransform));
	TestFalse(TEXT("invalid transform is rejected"), NestedPathBool(RejectedTransform, TEXT("success"), true));
	TestTrue(TEXT("rejected conversion leaves the transform intact"), Asset->Profiles[2].Presentation.CameraRelativeTransform.Equals(Expected, 1e-4));

	// The write is one editor transaction.
	if (TestNotNull(TEXT("the editor is available for undo"), GEditor))
	{
		GEditor->UndoTransaction();
		TestTrue(TEXT("undo restores the previous transform"),
			Asset->Profiles[2].Presentation.CameraRelativeTransform.Equals(FTransform::Identity, 1e-4));
		TestTrue(TEXT("undo leaves the earlier write in place"), Asset->Profiles[2].Presentation.PrimaryUse == Clip);
	}

	// Replaying the rollback restores the reference and still writes nothing.
	if (RollbackPayload)
	{
		const TSharedPtr<FJsonObject> Restored = NestedPathCall(Registry, TEXT("set_asset_property"), *RollbackPayload);
		TestTrue(FString::Printf(TEXT("the rollback replays (%s)"), *NestedPathString(Restored, TEXT("error"))),
			NestedPathBool(Restored, TEXT("success")));
		TestTrue(TEXT("the rollback cleared the reference"), Asset->Profiles[2].Presentation.PrimaryUse == nullptr);
		TestFalse(TEXT("the rollback wrote nothing to disk"), IFileManager::Get().FileExists(*PackageFileName));
	}

	// Appending in memory carries the same unsaved inverse as a field write.
	{
		Asset->Profiles[2].Id = FName(TEXT("Original"));
		Asset->Profiles[2].Presentation.PrimaryUse = Clip;
		Asset->Profiles[2].Presentation.CameraRelativeTransform = FTransform(Expected.GetRotation(), Expected.GetTranslation(), FVector(2.0, 3.0, 4.0));
		FArrayProperty* ProfilesProperty = FindFProperty<FArrayProperty>(Asset->GetClass(), TEXT("Profiles"));
		if (!TestNotNull(TEXT("profile array reflected"), ProfilesProperty)) return false;
		FString BeforeAppend;
		ProfilesProperty->ExportText_Direct(BeforeAppend, &Asset->Profiles, &Asset->Profiles, Asset, PPF_None);
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), AssetPath);
		Params->SetStringField(TEXT("propertyName"), TEXT("Profiles"));
		auto Element = MakeShared<FJsonObject>();
		Element->SetStringField(TEXT("Id"), TEXT("Appended"));
		Params->SetArrayField(TEXT("elements"), { MakeShared<FJsonValueObject>(Element) });
		Params->SetBoolField(TEXT("save"), false);
		const auto Appended = NestedPathCall(Registry, TEXT("append_asset_array_elements"), Params);
		if (!TestTrue(TEXT("unsaved append succeeds"), NestedPathBool(Appended, TEXT("success")))) return false;
		TestEqual(TEXT("one element appended"), Asset->Profiles.Num(), 4);
		const TSharedPtr<FJsonObject>* AppendRollback = nullptr;
		const TSharedPtr<FJsonObject>* AppendPayload = nullptr;
		if (!TestTrue(TEXT("append has rollback"), Appended->TryGetObjectField(TEXT("rollback"), AppendRollback))
			|| !TestTrue(TEXT("append rollback has payload"), (*AppendRollback)->TryGetObjectField(TEXT("payload"), AppendPayload))) return false;
		TestFalse(TEXT("append inverse preserves save=false"), NestedPathBool(*AppendPayload, TEXT("save"), true));
		const auto Restored = NestedPathCall(Registry, TEXT("set_asset_property"), *AppendPayload);
		TestTrue(TEXT("append inverse replays"), NestedPathBool(Restored, TEXT("success")));
		TestEqual(TEXT("append inverse restores the previous array"), Asset->Profiles.Num(), 3);
		FString AfterRollback;
		ProfilesProperty->ExportText_Direct(AfterRollback, &Asset->Profiles, &Asset->Profiles, Asset, PPF_None);
		TestEqual(TEXT("append inverse preserves every reference and transform channel"), AfterRollback, BeforeAppend);
		TestFalse(TEXT("unsaved append and inverse write no package"), IFileManager::Get().FileExists(*PackageFileName));
	}

	// #820: export-text replay must keep struct-keyed maps and their transforms.
	{
		Asset->MapProfiles.SetNum(1);
		Asset->MapProfiles[0].Offsets.Add(FIntPoint(2, 5), Expected);
		Asset->MapProfiles[0].Offsets.Add(FIntPoint(7, 11), FTransform(FQuat::Identity, FVector(8.0, 9.0, 10.0), FVector(2.0, 3.0, 4.0)));
		auto Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), AssetPath);
		Params->SetStringField(TEXT("propertyName"), TEXT("MapProfiles"));
		Params->SetArrayField(TEXT("elements"), { MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()) });
		Params->SetBoolField(TEXT("save"), false);
		const auto Appended = NestedPathCall(Registry, TEXT("append_asset_array_elements"), Params);
		if (!TestTrue(TEXT("map-bearing append succeeds"), NestedPathBool(Appended, TEXT("success")))) return false;
		TestEqual(TEXT("map-bearing element appended"), Asset->MapProfiles.Num(), 2);
		const TSharedPtr<FJsonObject>* MapRollback = nullptr;
		const TSharedPtr<FJsonObject>* MapPayload = nullptr;
		if (!TestTrue(TEXT("map append has rollback"), Appended->TryGetObjectField(TEXT("rollback"), MapRollback))
			|| !TestTrue(TEXT("map rollback has payload"), (*MapRollback)->TryGetObjectField(TEXT("payload"), MapPayload))) return false;
		const auto Restored = NestedPathCall(Registry, TEXT("set_asset_property"), *MapPayload);
		if (!TestTrue(TEXT("map-bearing inverse replays"), NestedPathBool(Restored, TEXT("success")))) return false;
		if (!TestEqual(TEXT("map-bearing array count restored"), Asset->MapProfiles.Num(), 1)) return false;
		TestEqual(TEXT("struct-keyed map keeps every pair"), Asset->MapProfiles[0].Offsets.Num(), 2);
		const FTransform* Offset = Asset->MapProfiles[0].Offsets.Find(FIntPoint(2, 5));
		TestTrue(TEXT("struct key and quaternion survive replay"), Offset && Offset->Equals(Expected, 1e-4));
		Offset = Asset->MapProfiles[0].Offsets.Find(FIntPoint(7, 11));
		TestTrue(TEXT("other struct key and nonuniform scale survive replay"), Offset && Offset->GetTranslation().Equals(FVector(8.0, 9.0, 10.0)) && Offset->GetScale3D().Equals(FVector(2.0, 3.0, 4.0)));
		TestFalse(TEXT("map inverse preserves save=false"), NestedPathBool(*MapPayload, TEXT("save"), true));
		TestFalse(TEXT("map-bearing inverse writes no package"), IFileManager::Get().FileExists(*PackageFileName));
	}

	// Paths that name nothing are refused, and nothing moves.
	struct FRefusal
	{
		const TCHAR* Path;
		const TCHAR* Reason;
	};
	const FRefusal Refusals[] = {
		{ TEXT("Profiles[3].Presentation.PrimaryUse"), TEXT("out of range") },
		{ TEXT("Profiles[0].Presentation.NoSuchField"), TEXT("not found") },
	};
	for (const FRefusal& Refusal : Refusals)
	{
		const TSharedPtr<FJsonObject> Refused = SetUnsaved(Refusal.Path, MakeShared<FJsonValueString>(ClipPath));
		TestFalse(FString::Printf(TEXT("'%s' is refused"), Refusal.Path), NestedPathBool(Refused, TEXT("success"), true));
		TestTrue(FString::Printf(TEXT("'%s' says why (%s)"), Refusal.Path, *NestedPathString(Refused, TEXT("error"))),
			NestedPathString(Refused, TEXT("error")).Contains(Refusal.Reason));
	}
	TestTrue(TEXT("a refused path wrote nothing to element 0"), Asset->Profiles[0].Presentation.PrimaryUse == nullptr);

	// Leave nothing dirty for a later save_dirty to flush through an unmounted root.
	Package->SetDirtyFlag(false);
	ClipPackage->SetDirtyFlag(false);
	return true;
}

#endif
