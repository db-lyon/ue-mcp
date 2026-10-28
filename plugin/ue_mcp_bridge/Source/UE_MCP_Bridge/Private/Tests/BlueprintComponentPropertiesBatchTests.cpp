// Batch writes use disposable assets in a private mount. Nothing in the
// attached project's Content directory is modified by these tests.
#if WITH_DEV_AUTOMATION_TESTS

#include "HandlerRegistry.h"
#include "HandlerUtils.h"
#include "Handlers/Blueprint/BlueprintHandlers.h"
#include "Handlers/Blueprint/BlueprintHandlers_Internal.h"
#include "Handlers/Blueprint/BlueprintHandlers_ComponentProperty.h"
#include "Tests/MCPScopedTestMount.h"
#include "Animation/AnimMontage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"

namespace MCPBlueprintComponentBatchTests
{
struct FFixture
{
	FMCPScopedTestMount Mount;
	FMCPHandlerRegistry Registry;
	TArray<TStrongObjectPtr<UObject>> Keep;
	FFixture() : Mount(TEXT("/UEMCPComponentBatch_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/"), TEXT("UEMCPComponentBatch"))
	{
		FBlueprintHandlers::RegisterHandlers(Registry);
	}
	UBlueprint* Create(const TCHAR* Name, UClass* Parent = ACharacter::StaticClass())
	{
		UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(Parent,
			CreatePackage(*(Mount.RootPath + Name)), FName(Name), BPTYPE_Normal,
			UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
		Keep.Emplace(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		return Blueprint;
	}
};

inline TSharedPtr<FJsonValue> Item(UBlueprint* Blueprint, const TCHAR* Component, const TSharedPtr<FJsonObject>& Properties)
{
	auto Object = MakeShared<FJsonObject>();
	Object->SetStringField(TEXT("assetPath"), Blueprint->GetPathName());
	Object->SetStringField(TEXT("componentName"), Component);
	Object->SetObjectField(TEXT("properties"), Properties);
	return MakeShared<FJsonValueObject>(Object);
}

inline TSharedPtr<FJsonObject> Request(const TArray<TSharedPtr<FJsonValue>>& Items, bool bDryRun = false)
{
	auto Params = MakeShared<FJsonObject>();
	Params->SetArrayField(TEXT("items"), Items);
	Params->SetBoolField(TEXT("dryRun"), bDryRun);
	return Params;
}

inline TSharedPtr<FJsonObject> Run(FFixture& Fixture, const TSharedPtr<FJsonObject>& Params)
{
	return Fixture.Registry.ExecuteHandler(TEXT("set_blueprint_component_properties_batch"), Params)->AsObject();
}

inline TSharedPtr<FJsonObject> Row(const TSharedPtr<FJsonObject>& Result, int32 Index)
{
	return Result->GetArrayField(TEXT("results"))[Index]->AsObject();
}

inline float Speed(UBlueprint* Blueprint)
{
	return CastChecked<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject())->GetCharacterMovement()->MaxWalkSpeed;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPBlueprintComponentBatchNativeTest,
	"UE.MCP.Blueprint.ComponentPropertiesBatch.NativePersistenceAndRollback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBlueprintComponentBatchNativeTest::RunTest(const FString& Parameters)
{
	using namespace MCPBlueprintComponentBatchTests;
	FFixture Fixture;
	UBlueprint* First = Fixture.Create(TEXT("BP_First"));
	UBlueprint* Second = Fixture.Create(TEXT("BP_Second"));
	UBlueprint* Unrelated = Fixture.Create(TEXT("BP_Unrelated"));
	Unrelated->MarkPackageDirty();
	FString SaveError;
	if (!TestTrue(TEXT("first baseline saves"), SaveAssetPackageChecked(First, SaveError)) ||
		!TestTrue(TEXT("second baseline saves"), SaveAssetPackageChecked(Second, SaveError))) return false;
	const float Previous = Speed(First);
	auto Properties = MakeShared<FJsonObject>();
	Properties->SetNumberField(TEXT("MaxWalkSpeed"), Previous + 123.0);
	Properties->SetBoolField(TEXT("bOrientRotationToMovement"), true);
	Properties->SetNumberField(TEXT("RotationRate.Yaw"), 321.0);
	auto CapsuleProperties = MakeShared<FJsonObject>();
	CapsuleProperties->SetBoolField(TEXT("bHiddenInGame"), false);
	auto Params = Request({Item(First, TEXT("CharacterMovement"), Properties),
		Item(First, TEXT("CapsuleComponent"), CapsuleProperties),
		Item(Second, TEXT("/Script/Engine.CharacterMovementComponent"), Properties)});

	const auto Dry = Run(Fixture, Request(Params->GetArrayField(TEXT("items")), true));
	TestTrue(TEXT("dry run validates every native component"), Dry->GetBoolField(TEXT("success")));
	TestEqual(TEXT("dry run does not compile"), Dry->GetIntegerField(TEXT("blueprintsCompiled")), 0);
	TestEqual(TEXT("dry run keeps original speed"), Speed(First), Previous);
	TestFalse(TEXT("dry run leaves clean Blueprint clean"), First->GetOutermost()->IsDirty());
	TestFalse(TEXT("dry run has no rollback because nothing changed"), Dry->HasField(TEXT("rollback")));

	const auto Result = Run(Fixture, Params);
	if (!TestTrue(TEXT("batch succeeds"), Result->GetBoolField(TEXT("success")))) return false;
	TestEqual(TEXT("three items compile two Blueprints"), Result->GetIntegerField(TEXT("blueprintsCompiled")), 2);
	TestEqual(TEXT("three items save two Blueprints"), Result->GetIntegerField(TEXT("blueprintsSaved")), 2);
	TestEqual(TEXT("first native default changed"), Speed(First), Previous + 123.0f);
	TestEqual(TEXT("class selector changed second native default"), Speed(Second), Previous + 123.0f);
	TestEqual(TEXT("post-compile readback reports speed"), Row(Result, 0)->GetObjectField(TEXT("values"))->GetNumberField(TEXT("MaxWalkSpeed")), double(Previous + 123.0f));
	TestFalse(TEXT("target package is clean after saving"), First->GetOutermost()->IsDirty());
	TestTrue(TEXT("unrelated dirty package remains unsaved"), Unrelated->GetOutermost()->IsDirty());
	FString UnrelatedFile;
	ResolvePackageFileName(Unrelated->GetOutermost(), UnrelatedFile);
	TestFalse(TEXT("there was no broad save"), IFileManager::Get().FileExists(*UnrelatedFile));

	const auto Rollback = Result->GetObjectField(TEXT("rollback"));
	TestEqual(TEXT("rollback uses public action"), Rollback->GetStringField(TEXT("method")), FString(TEXT("set_component_properties_batch")));
	const auto Restored = Run(Fixture, Rollback->GetObjectField(TEXT("payload")));
	if (!TestTrue(TEXT("rollback succeeds"), Restored->GetBoolField(TEXT("success")))) return false;
	TestEqual(TEXT("rollback restores first native default"), Speed(First), Previous);
	TestEqual(TEXT("rollback restores second native default"), Speed(Second), Previous);

	const FString FirstPath = First->GetPathName();
	Fixture.Keep.Empty();
	UEMCPTests::EvictPackagesUnderRoot(Fixture.Mount.RootPath);
	UBlueprint* Reloaded = LoadObject<UBlueprint>(nullptr, *FirstPath);
	if (!TestNotNull(TEXT("saved Blueprint reloads from disk"), Reloaded)) return false;
	Fixture.Keep.Emplace(Reloaded);
	TestEqual(TEXT("rollback survives reload"), Speed(Reloaded), Previous);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPBlueprintComponentBatchInheritedTest,
	"UE.MCP.Blueprint.ComponentPropertiesBatch.InheritedAndReferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBlueprintComponentBatchInheritedTest::RunTest(const FString& Parameters)
{
	using namespace MCPBlueprintComponentBatchTests;
	FFixture Fixture;
	UBlueprint* Parent = Fixture.Create(TEXT("BP_Parent"));
	USCS_Node* Node = Parent->SimpleConstructionScript->CreateNode(UStaticMeshComponent::StaticClass(), TEXT("Combat"));
	Parent->SimpleConstructionScript->AddNode(Node);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Parent);
	FKismetEditorUtilities::CompileBlueprint(Parent);
	UBlueprint* Child = Fixture.Create(TEXT("BP_Child"), Parent->GeneratedClass);
	UBlueprint* Sibling = Fixture.Create(TEXT("BP_Sibling"), Parent->GeneratedClass);
	bool bInherited = false;
	TArray<FString> Available;
	const float ParentScale = CastChecked<UStaticMeshComponent>(ResolveComponentTemplate(Parent, TEXT("Combat"), false, bInherited, Available))->BoundsScale;
	auto Properties = MakeShared<FJsonObject>();
	Properties->SetNumberField(TEXT("BoundsScale"), ParentScale + 2.0);
	Properties->SetArrayField(TEXT("OverrideMaterials"), {});
	auto Params = Request({Item(Child, TEXT("StaticMeshComponent"), Properties)});
	const bool bHadICH = Child->GetInheritableComponentHandler(false) != nullptr;
	const auto Dry = Run(Fixture, Request(Params->GetArrayField(TEXT("items")), true));
	TestTrue(TEXT("inherited dry run succeeds"), Dry->GetBoolField(TEXT("success")));
	TestEqual(TEXT("dry run does not create ICH"), Child->GetInheritableComponentHandler(false) != nullptr, bHadICH);
	const auto Result = Run(Fixture, Params);
	if (!TestTrue(TEXT("child override batch succeeds"), Result->GetBoolField(TEXT("success")))) return false;
	TestTrue(TEXT("child gets its own ICH"), Child->GetInheritableComponentHandler(false) != nullptr);
	TestEqual(TEXT("child override changed"), CastChecked<UStaticMeshComponent>(ResolveComponentTemplate(Child, TEXT("Combat"), false, bInherited, Available))->BoundsScale, ParentScale + 2.0f);
	TestEqual(TEXT("parent stays unchanged"), CastChecked<UStaticMeshComponent>(ResolveComponentTemplate(Parent, TEXT("Combat"), false, bInherited, Available))->BoundsScale, ParentScale);
	TestEqual(TEXT("sibling stays unchanged"), CastChecked<UStaticMeshComponent>(ResolveComponentTemplate(Sibling, TEXT("Combat"), false, bInherited, Available))->BoundsScale, ParentScale);
	const auto Restored = Run(Fixture, Result->GetObjectField(TEXT("rollback"))->GetObjectField(TEXT("payload")));
	if (!TestTrue(TEXT("inherited rollback succeeds"), Restored->GetBoolField(TEXT("success")))) return false;
	TestEqual(TEXT("inherited rollback restores value"), CastChecked<UStaticMeshComponent>(ResolveComponentTemplate(Child, TEXT("Combat"), false, bInherited, Available))->BoundsScale, ParentScale);
	TestTrue(TEXT("rollback retains the new ICH override"), Child->GetInheritableComponentHandler(false) != nullptr);

	// Exercise montage reference parsing on a native skeletal component, then
	// clearing that reference through the same property map path.
	UPackage* MontagePackage = CreatePackage(*(Fixture.Mount.RootPath + TEXT("AM_Probe")));
	UAnimMontage* Montage = NewObject<UAnimMontage>(MontagePackage, TEXT("AM_Probe"), RF_Public | RF_Standalone);
	Fixture.Keep.Emplace(Montage);
	FString SaveError;
	if (!TestTrue(TEXT("reference asset saves"), SaveAssetPackageChecked(Montage, SaveError))) return false;
	auto Animation = MakeShared<FJsonObject>();
	Animation->SetStringField(TEXT("AnimationData.AnimToPlay"), Montage->GetPathName());
	Animation->SetBoolField(TEXT("AnimationData.bSavedLooping"), false);
	const auto Reference = Run(Fixture, Request({Item(Child, TEXT("Mesh"), Animation)}));
	if (!TestTrue(TEXT("montage reference writes and saves"), Reference->GetBoolField(TEXT("success")))) return false;
	FString ReadError, Text;
	TSharedPtr<FJsonValue> Replay;
	UActorComponent* Mesh = ResolveComponentTemplate(Child, TEXT("Mesh"), false, bInherited, Available);
	TestTrue(TEXT("montage reference reads back"), MCPBlueprintComponentProperty::Read(Mesh, TEXT("AnimationData.AnimToPlay"), Text, Replay, ReadError));
	TestTrue(TEXT("reference points at montage"), Text.Contains(Montage->GetPathName()));
	Animation->SetField(TEXT("AnimationData.AnimToPlay"), MakeShared<FJsonValueNull>());
	const auto Cleared = Run(Fixture, Request({Item(Child, TEXT("Mesh"), Animation)}));
	if (!TestTrue(TEXT("null reference writes and saves"), Cleared->GetBoolField(TEXT("success")))) return false;
	const auto UndoClear = Run(Fixture, Cleared->GetObjectField(TEXT("rollback"))->GetObjectField(TEXT("payload")));
	TestTrue(TEXT("reference rollback restores montage"), UndoClear->GetBoolField(TEXT("success")));
	Mesh = ResolveComponentTemplate(Child, TEXT("Mesh"), false, bInherited, Available);
	TestTrue(TEXT("restored montage reads back"), MCPBlueprintComponentProperty::Read(Mesh, TEXT("AnimationData.AnimToPlay"), Text, Replay, ReadError));
	TestTrue(TEXT("restored reference points at montage"), Text.Contains(Montage->GetPathName()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPBlueprintComponentBatchRefusalTest,
	"UE.MCP.Blueprint.ComponentPropertiesBatch.ValidationAndReadOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBlueprintComponentBatchRefusalTest::RunTest(const FString& Parameters)
{
	using namespace MCPBlueprintComponentBatchTests;
	FFixture Fixture;
	UBlueprint* Bad = Fixture.Create(TEXT("BP_Bad"));
	UBlueprint* Good = Fixture.Create(TEXT("BP_Good"));
	const float Previous = Speed(Bad);
	auto Valid = MakeShared<FJsonObject>();
	Valid->SetNumberField(TEXT("MaxWalkSpeed"), Previous + 55.0);
	auto Invalid = MakeShared<FJsonObject>();
	Invalid->SetNumberField(TEXT("NoSuchProperty"), 42.0);
	const auto Result = Run(Fixture, Request({Item(Bad, TEXT("CharacterMovement"), Valid),
		Item(Bad, TEXT("CharacterMovement"), Invalid), Item(Good, TEXT("CharacterMovement"), Valid)}));
	TestFalse(TEXT("partial failure is visible"), Result->GetBoolField(TEXT("success")));
	TestEqual(TEXT("first item's Blueprint was refused before changes"), Speed(Bad), Previous);
	TestEqual(TEXT("valid Blueprint still changed"), Speed(Good), Previous + 55.0f);
	TestEqual(TEXT("bad property reports validation"), Row(Result, 1)->GetStringField(TEXT("stage")), FString(TEXT("validate")));
	TestEqual(TEXT("valid item on same Blueprint reports preflight"), Row(Result, 0)->GetStringField(TEXT("stage")), FString(TEXT("preflight")));
	TestEqual(TEXT("only successful Blueprint compiled"), Result->GetIntegerField(TEXT("blueprintsCompiled")), 1);

	FString SaveError, Filename;
	if (!TestTrue(TEXT("read-only fixture saves first"), SaveAssetPackageChecked(Bad, SaveError))) return false;
	if (!TestTrue(TEXT("read-only filename resolves"), ResolvePackageFileName(Bad->GetOutermost(), Filename))) return false;
	if (!TestTrue(TEXT("file can be marked read-only"), IFileManager::Get().SetReadOnly(*Filename, true))) return false;
	ON_SCOPE_EXIT { IFileManager::Get().SetReadOnly(*Filename, false); };
	const auto ReadOnly = Run(Fixture, Request({Item(Bad, TEXT("CharacterMovement"), Valid)}));
	TestFalse(TEXT("read-only package is refused"), ReadOnly->GetBoolField(TEXT("success")));
	TestEqual(TEXT("refusal stage names write guard"), Row(ReadOnly, 0)->GetStringField(TEXT("stage")), FString(TEXT("write_guard")));
	TestEqual(TEXT("read-only package keeps original speed"), Speed(Bad), Previous);
	TestFalse(TEXT("read-only refusal leaves clean package clean"), Bad->GetOutermost()->IsDirty());
	TestFalse(TEXT("read-only refusal emits no rollback"), ReadOnly->HasField(TEXT("rollback")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMCPBlueprintComponentBatchDirectoryTest,
	"UE.MCP.Blueprint.ComponentPropertiesBatch.DirectorySelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPBlueprintComponentBatchDirectoryTest::RunTest(const FString& Parameters)
{
	using namespace MCPBlueprintComponentBatchTests;
	FFixture Fixture;
	UBlueprint* Character = Fixture.Create(TEXT("BP_Character"));
	UBlueprint* Actor = Fixture.Create(TEXT("BP_Actor"), AActor::StaticClass());
	FString Error;
	if (!TestTrue(TEXT("character saves"), SaveAssetPackageChecked(Character, Error)) ||
		!TestTrue(TEXT("actor saves"), SaveAssetPackageChecked(Actor, Error))) return false;
	FAssetRegistryModule& RegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	RegistryModule.Get().ScanPathsSynchronous({Fixture.Mount.RootPath}, true);
	ON_SCOPE_EXIT
	{
		FAssetRegistryModule::AssetDeleted(Character);
		FAssetRegistryModule::AssetDeleted(Actor);
	};
	auto Properties = MakeShared<FJsonObject>();
	Properties->SetNumberField(TEXT("MaxWalkSpeed"), 432.0);
	auto Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("directory"), Fixture.Mount.RootPath);
	Params->SetStringField(TEXT("classFilter"), TEXT("/Script/Engine.Character"));
	Params->SetStringField(TEXT("componentName"), TEXT("CharacterMovement"));
	Params->SetObjectField(TEXT("properties"), Properties);
	const auto Result = Run(Fixture, Params);
	TestTrue(TEXT("directory batch succeeds"), Result->GetBoolField(TEXT("success")));
	TestEqual(TEXT("class filter skips actor Blueprint"), Result->GetIntegerField(TEXT("skipped")), 1);
	TestEqual(TEXT("directory writes matching Blueprint"), Speed(Character), 432.0f);
	TestEqual(TEXT("directory saves only matching Blueprint"), Result->GetIntegerField(TEXT("blueprintsSaved")), 1);
	Params->RemoveField(TEXT("classFilter"));
	Params->SetBoolField(TEXT("dryRun"), true);
	const auto Missing = Run(Fixture, Params);
	TestEqual(TEXT("missing component is observable selection skip"), Missing->GetIntegerField(TEXT("skipped")), 1);
	Params->SetNumberField(TEXT("maxBlueprints"), 1);
	const auto Capped = Run(Fixture, Params);
	TestFalse(TEXT("candidate overflow refuses instead of truncating"), Capped->GetBoolField(TEXT("success")));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
