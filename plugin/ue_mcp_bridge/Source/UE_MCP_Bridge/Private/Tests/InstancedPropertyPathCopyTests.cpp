// Coverage for writing an object path into an Instanced UObject property (#1635).
//
// An Instanced property owns its value. Assigning another object's subobject
// into one by path left two owners pointing at one object, so editing either
// edited both. With the owner known the write now takes a copy outered to it;
// a subobject the owner already holds and a standalone asset are assigned as
// they are. UStereoLayerComponent::Shape is the fixture: an engine Instanced
// object property whose classes resolve on every supported engine.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/MCPScopedTestMount.h"

#include "HandlerJsonProperty.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	const TCHAR* const MCPInstancedCopyMountRoot = TEXT("/UEMCPInstancedCopyTest/");

	UObject* InstancedCopyNewInPackage(const FString& PackageName, UClass* Class, const TCHAR* Name, EObjectFlags Flags)
	{
		UPackage* Package = CreatePackage(*PackageName);
		return Package ? NewObject<UObject>(Package, Class, FName(Name), Flags) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPInstancedPropertyPathCopyTest,
	"UE.MCP.Property.InstancedPathWrite.CopiesForeignSubobjectIntoOwner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPInstancedPropertyPathCopyTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ FString(MCPInstancedCopyMountRoot), TEXT("UEMCPInstancedCopyTest") };

	UClass* OwnerClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.StereoLayerComponent"));
	UClass* ShapeClass = FindObject<UClass>(nullptr, TEXT("/Script/Engine.StereoLayerShapeQuad"));
	if (!TestNotNull(TEXT("StereoLayerComponent resolves"), OwnerClass)) return false;
	if (!TestNotNull(TEXT("StereoLayerShapeQuad resolves"), ShapeClass)) return false;

	FObjectProperty* ShapeProp = CastField<FObjectProperty>(OwnerClass->FindPropertyByName(TEXT("Shape")));
	if (!TestNotNull(TEXT("StereoLayerComponent has an object property Shape"), ShapeProp)) return false;
	if (!TestTrue(TEXT("Shape is an Instanced property"), ShapeProp->HasAnyPropertyFlags(CPF_InstancedReference))) return false;

	const FString Root(MCPInstancedCopyMountRoot);
	TStrongObjectPtr<UObject> OwnerA(InstancedCopyNewInPackage(Root + TEXT("OwnerA"), OwnerClass, TEXT("OwnerA"), RF_Public | RF_Standalone));
	TStrongObjectPtr<UObject> OwnerB(InstancedCopyNewInPackage(Root + TEXT("OwnerB"), OwnerClass, TEXT("OwnerB"), RF_Public | RF_Standalone));
	TStrongObjectPtr<UObject> ShapeAsset(InstancedCopyNewInPackage(Root + TEXT("ShapeAsset"), ShapeClass, TEXT("ShapeAsset"), RF_Public | RF_Standalone));
	if (!TestTrue(TEXT("fixtures created"), OwnerA.IsValid() && OwnerB.IsValid() && ShapeAsset.IsValid())) return false;

	UObject* Source = NewObject<UObject>(OwnerA.Get(), ShapeClass, TEXT("SourceShape"), RF_Public | RF_Transactional);
	ShapeProp->SetObjectPropertyValue_InContainer(OwnerA.Get(), Source);

	void* const AddrB = ShapeProp->ContainerPtrToValuePtr<void>(OwnerB.Get());
	auto ValueB = [&]() { return ShapeProp->GetObjectPropertyValue(AddrB); };
	auto PathValue = [](const UObject* Obj) { return MakeShared<FJsonValueString>(Obj->GetPathName()); };

	// No owner: the write cannot know where a copy would live, so it assigns.
	{
		FString Error;
		const bool bOk = MCPJsonProperty::SetJsonOnProperty(ShapeProp, AddrB, PathValue(Source), Error);
		TestTrue(FString::Printf(TEXT("an ownerless write succeeds (%s)"), *Error), bOk);
		TestTrue(TEXT("an ownerless write assigns the named object"), ValueB() == Source);
		ShapeProp->SetObjectPropertyValue(AddrB, nullptr);
	}

	// Owner known, another owner's subobject: a copy outered to the writer.
	UObject* Copy = nullptr;
	{
		FString Error;
		const bool bOk = MCPJsonProperty::SetJsonOnPropertyWithOwner(ShapeProp, AddrB, PathValue(Source), OwnerB.Get(), Error);
		TestTrue(FString::Printf(TEXT("the owned write succeeds (%s)"), *Error), bOk);
		Copy = ValueB();
		if (!TestNotNull(TEXT("the owned write assigned a value"), Copy)) return false;
		TestTrue(TEXT("the owned write did not share the other owner's subobject"), Copy != Source);
		TestTrue(TEXT("the copy is outered to the writing owner"), Copy->GetOuter() == OwnerB.Get());
		TestTrue(TEXT("the copy has the source's class"), Copy->GetClass() == ShapeClass);
		TestTrue(TEXT("the source owner keeps its own subobject"),
			ShapeProp->GetObjectPropertyValue_InContainer(OwnerA.Get()) == Source);
		TestTrue(TEXT("the source subobject was not moved"), Source->GetOuter() == OwnerA.Get());
	}

	// A subobject the owner already holds is assigned, not copied again.
	{
		FString Error;
		const bool bOk = MCPJsonProperty::SetJsonOnPropertyWithOwner(ShapeProp, AddrB, PathValue(Copy), OwnerB.Get(), Error);
		TestTrue(FString::Printf(TEXT("rewriting the owner's own subobject succeeds (%s)"), *Error), bOk);
		TestTrue(TEXT("the owner's own subobject keeps its identity"), ValueB() == Copy);
	}

	// A standalone asset is referenced, not cloned into the owner.
	{
		FString Error;
		TestTrue(TEXT("the fixture asset is an asset"), ShapeAsset->IsAsset());
		const bool bOk = MCPJsonProperty::SetJsonOnPropertyWithOwner(ShapeProp, AddrB, PathValue(ShapeAsset.Get()), OwnerB.Get(), Error);
		TestTrue(FString::Printf(TEXT("writing a standalone asset succeeds (%s)"), *Error), bOk);
		TestTrue(TEXT("a standalone asset is assigned as is"), ValueB() == ShapeAsset.Get());
	}

	for (const TCHAR* Name : { TEXT("OwnerA"), TEXT("OwnerB"), TEXT("ShapeAsset") })
	{
		if (UPackage* Package = FindPackage(nullptr, *(Root + Name))) Package->SetDirtyFlag(false);
	}
	return true;
}

#endif
