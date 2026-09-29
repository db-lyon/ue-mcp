// Coverage for key selectors in property paths:
// "Profiles[ProfileId=SGK_Stone_Axe].Presentation.MeshOffset.Scale3D".
//
// An agent editing one entry of a 48-element struct array had to know the
// entry's index, and a selector that was not a number went through Atoi and
// silently landed on element 0. The selector lives in the shared resolver,
// MCPJsonProperty::ResolveDottedPath, so these tests drive it directly and
// through the read and write actions that use it.
//
// Fixtures live in fresh packages: /Temp/ for the editor actions, and a
// private mount for the asset actions, which refuse protected mounts.

#if WITH_DEV_AUTOMATION_TESTS

#include "Tests/MCPKeyedPathTestTypes.h"
#include "Tests/MCPInstancedStructPathTestTypes.h"
#include "Tests/MCPScopedTestMount.h"

#include "HandlerJsonProperty.h"
#include "HandlerRegistry.h"
#include "Handlers/Asset/AssetHandlers.h"
#include "Handlers/Editor/EditorHandlers.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	const TCHAR* const MCPKeyedPathMountRoot = TEXT("/UEMCPKeyedPathTest/");

	void KeyedPathFill(UUEMCPKeyedPathTestObject& Target)
	{
		struct FSeed { const TCHAR* Id; const TCHAR* Label; int32 Rank; bool bEnabled; EUEMCPKeyedPathSlot Slot; };
		const FSeed Seeds[] = {
			{ TEXT("SGK_Stone_Axe"),   TEXT("Stone Axe"),  1, false, EUEMCPKeyedPathSlot::Back },
			{ TEXT("SGK_Stone_Knife"), TEXT("Knife"),      2, false, EUEMCPKeyedPathSlot::Hip },
			{ TEXT("SGK_Iron_Sword"),  TEXT("Sword.Long"), 2, true,  EUEMCPKeyedPathSlot::Back },
			{ TEXT("SGK_Stone_Spear"), TEXT("Spear"),      3, false, EUEMCPKeyedPathSlot::Chest },
		};
		Target.Profiles.Reset();
		for (const FSeed& Seed : Seeds)
		{
			const int32 Index = Target.Profiles.Num();
			FUEMCPKeyedPathProfile& Profile = Target.Profiles.AddDefaulted_GetRef();
			Profile.ProfileId = FName(Seed.Id);
			Profile.Label = Seed.Label;
			Profile.Rank = Seed.Rank;
			Profile.bEnabled = Seed.bEnabled;
			Profile.Slot = Seed.Slot;
			Profile.Weight = 0.5f * (Index + 1);
			Profile.Presentation.MeshOffset = FTransform(FRotator::ZeroRotator, FVector(Index * 10.0, 0.0, 0.0), FVector(Index + 1.0));
			Profile.Presentation.SlotOffsets.Add(EUEMCPKeyedPathSlot::Back, FTransform(FVector(Index * 1.0, 0.0, 0.0)));
		}
		Target.Lookup.Add(TEXT("A"), 1);
		Target.Plain = 7;
	}

	TStrongObjectPtr<UUEMCPKeyedPathTestObject> KeyedPathTempFixture()
	{
		UPackage* Package = CreatePackage(*FString::Printf(
			TEXT("/Temp/UEMCPKeyedPathTests/%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		Package->SetDirtyFlag(false);
		TStrongObjectPtr<UUEMCPKeyedPathTestObject> Fixture(
			NewObject<UUEMCPKeyedPathTestObject>(Package, TEXT("Fixture")));
		KeyedPathFill(*Fixture);
		return Fixture;
	}

	/** Fixture under the private mount, where the asset actions will write. */
	TStrongObjectPtr<UUEMCPKeyedPathTestObject> KeyedPathMountedFixture()
	{
		UPackage* Package = CreatePackage(*FString::Printf(
			TEXT("%sKP_%s"), MCPKeyedPathMountRoot, *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
		Package->SetDirtyFlag(false);
		TStrongObjectPtr<UUEMCPKeyedPathTestObject> Fixture(
			NewObject<UUEMCPKeyedPathTestObject>(Package, TEXT("KP_Fixture"), RF_Public | RF_Standalone));
		KeyedPathFill(*Fixture);
		return Fixture;
	}

	TSharedPtr<FJsonObject> KeyedPathCall(FMCPHandlerRegistry& Registry, const TCHAR* Method, const TSharedPtr<FJsonObject>& Params)
	{
		const TSharedPtr<FJsonValue> Response = Registry.ExecuteHandler(Method, Params);
		return (Response.IsValid() && Response->Type == EJson::Object) ? Response->AsObject() : nullptr;
	}

	TSharedPtr<FJsonObject> KeyedPathEditorGet(FMCPHandlerRegistry& Registry, const UObject& Target, const FString& Path)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("objectPath"), Target.GetPathName());
		Params->SetStringField(TEXT("propertyName"), Path);
		return KeyedPathCall(Registry, TEXT("get_property"), Params);
	}

	TSharedPtr<FJsonObject> KeyedPathEditorSet(FMCPHandlerRegistry& Registry, const UObject& Target, const FString& Path, const TSharedPtr<FJsonValue>& Value)
	{
		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("objectPath"), Target.GetPathName());
		Params->SetStringField(TEXT("propertyName"), Path);
		Params->SetField(TEXT("value"), Value);
		Params->SetBoolField(TEXT("save"), false);
		return KeyedPathCall(Registry, TEXT("set_property"), Params);
	}

	bool KeyedPathSucceeded(const TSharedPtr<FJsonObject>& Response)
	{
		bool bSuccess = false;
		return Response.IsValid() && Response->TryGetBoolField(TEXT("success"), bSuccess) && bSuccess;
	}

	FString KeyedPathString(const TSharedPtr<FJsonObject>& Response, const TCHAR* Field)
	{
		FString Value;
		if (Response.IsValid()) Response->TryGetStringField(Field, Value);
		return Value;
	}

	/** The rollback payload's propertyName, or empty when there is none. */
	FString KeyedPathRollbackName(const TSharedPtr<FJsonObject>& Response)
	{
		const TSharedPtr<FJsonObject>* Rollback = nullptr;
		const TSharedPtr<FJsonObject>* Payload = nullptr;
		if (!Response.IsValid() || !Response->TryGetObjectField(TEXT("rollback"), Rollback) || !Rollback) return FString();
		if (!(*Rollback)->TryGetObjectField(TEXT("payload"), Payload) || !Payload) return FString();
		return KeyedPathString(*Payload, TEXT("propertyName"));
	}

	/** Resolve straight through the shared resolver. */
	bool KeyedPathResolve(UObject* Root, const FString& Path, FProperty*& OutProp, void*& OutAddr,
		MCPJsonProperty::FResolvedPathInfo& OutInfo, FString& OutError)
	{
		UObject* Owner = nullptr;
		return MCPJsonProperty::ResolveDottedPath(Root, Path, OutProp, OutAddr, Owner, OutError, &OutInfo);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPKeyedPathParseTest,
	"UE.MCP.Property.KeyedPath.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPKeyedPathParseTest::RunTest(const FString& Parameters)
{
	using namespace MCPJsonProperty;

	TArray<FString> Parts;
	FString Error;
	TestTrue(TEXT("a dot inside a selector does not split"),
		SplitPropertyPath(TEXT("Profiles[Label=Sword.Long].Rank"), Parts, Error));
	TestEqual(TEXT("two segments"), Parts.Num(), 2);
	if (Parts.Num() == 2) TestEqual(TEXT("first segment keeps its selector"), Parts[0], FString(TEXT("Profiles[Label=Sword.Long]")));

	TestTrue(TEXT("a quoted value may hold ']' and '.'"),
		SplitPropertyPath(TEXT("Profiles[Label=\"a]b.c\"].Rank"), Parts, Error));
	TestEqual(TEXT("quoted value still gives two segments"), Parts.Num(), 2);
	FPropertyPathSegment Segment;
	if (Parts.Num() == 2 && TestTrue(TEXT("quoted segment parses"), ParsePropertyPathSegment(Parts[0], Segment, Error)))
	{
		TestTrue(TEXT("quoted segment is a key"), Segment.bHasKey);
		TestEqual(TEXT("quoted key field"), Segment.KeyField, FString(TEXT("Label")));
		TestEqual(TEXT("quotes are stripped from the value"), Segment.KeyValue, FString(TEXT("a]b.c")));
	}

	TestTrue(TEXT("empty segments are still dropped"), SplitPropertyPath(TEXT("A..B."), Parts, Error));
	TestEqual(TEXT("empty segments dropped"), Parts.Num(), 2);

	TestTrue(TEXT("index parses"), ParsePropertyPathSegment(TEXT("Rules[ 12 ]"), Segment, Error));
	TestTrue(TEXT("index kind"), Segment.bHasIndex && !Segment.bHasKey);
	TestEqual(TEXT("index value, whitespace tolerated as Atoi did"), Segment.Index, 12);

	TestTrue(TEXT("key parses with spaces around '='"), ParsePropertyPathSegment(TEXT("Rules[ Id = Axe ]"), Segment, Error));
	TestEqual(TEXT("key field trimmed"), Segment.KeyField, FString(TEXT("Id")));
	TestEqual(TEXT("key value trimmed"), Segment.KeyValue, FString(TEXT("Axe")));

	TestFalse(TEXT("a word is neither index nor key"), ParsePropertyPathSegment(TEXT("Rules[abc]"), Segment, Error));
	TestTrue(TEXT("the refusal says how to write both"), Error.Contains(TEXT("neither an index nor a key")));
	TestFalse(TEXT("an empty selector is refused"), ParsePropertyPathSegment(TEXT("Rules[]"), Segment, Error));
	TestFalse(TEXT("a key without a field is refused"), ParsePropertyPathSegment(TEXT("Rules[=Axe]"), Segment, Error));
	TestFalse(TEXT("text after ']' is refused"), ParsePropertyPathSegment(TEXT("Rules[1]x"), Segment, Error));
	TestFalse(TEXT("an unterminated '[' is refused"), SplitPropertyPath(TEXT("Rules[1.Field"), Parts, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPKeyedPathReadTest,
	"UE.MCP.Property.KeyedPath.Read",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPKeyedPathReadTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UUEMCPKeyedPathTestObject> Fixture = KeyedPathTempFixture();

	// Every key kind resolves to the element it names.
	struct FCase { const TCHAR* Path; int32 ExpectedIndex; };
	const FCase Cases[] = {
		{ TEXT("Profiles[ProfileId=SGK_Stone_Knife].Rank"), 1 },
		{ TEXT("Profiles[Label=Sword.Long].Rank"), 2 },
		{ TEXT("Profiles[Label=\"Stone Axe\"].Rank"), 0 },
		{ TEXT("Profiles[Rank=3].Rank"), 3 },
		{ TEXT("Profiles[bEnabled=TRUE].Rank"), 2 },
		{ TEXT("Profiles[Slot=Chest].Rank"), 3 },
		{ TEXT("Profiles[Slot=EUEMCPKeyedPathSlot::Hip].Rank"), 1 },
	};
	for (const FCase& Case : Cases)
	{
		FProperty* Prop = nullptr;
		void* Addr = nullptr;
		MCPJsonProperty::FResolvedPathInfo Info;
		FString Error;
		if (!TestTrue(FString::Printf(TEXT("%s resolves (%s)"), Case.Path, *Error),
			KeyedPathResolve(Fixture.Get(), Case.Path, Prop, Addr, Info, Error)))
		{
			continue;
		}
		TestTrue(FString::Printf(TEXT("%s lands on element %d"), Case.Path, Case.ExpectedIndex),
			Addr == static_cast<void*>(&Fixture->Profiles[Case.ExpectedIndex].Rank));
		TestTrue(FString::Printf(TEXT("%s reports a key selector"), Case.Path), Info.bUsedKeySelector);
		TestEqual(*FString::Printf(TEXT("%s indexed form"), Case.Path),
			Info.IndexedPath, FString::Printf(TEXT("Profiles[%d].Rank"), Case.ExpectedIndex));
	}

	// Through editor(get_property): a small struct leaf under a nested struct
	// of a keyed element, as JSON, without reading the array.
	FMCPHandlerRegistry Registry;
	FEditorHandlers::RegisterHandlers(Registry);
	const TSharedPtr<FJsonObject> Scale = KeyedPathEditorGet(Registry, *Fixture,
		TEXT("Profiles[ProfileId=SGK_Stone_Knife].Presentation.MeshOffset.Scale3D"));
	TestTrue(FString::Printf(TEXT("get_property reads the keyed leaf (%s)"), *KeyedPathString(Scale, TEXT("error"))), KeyedPathSucceeded(Scale));
	TestEqual(TEXT("get_property reports the index the key matched"),
		KeyedPathString(Scale, TEXT("indexedPath")), FString(TEXT("Profiles[1].Presentation.MeshOffset.Scale3D")));
	TestEqual(TEXT("get_property names the leaf"), KeyedPathString(Scale, TEXT("leafPropertyName")), FString(TEXT("Scale3D")));
	const TSharedPtr<FJsonObject>* ScaleValue = nullptr;
	double ScaleX = 0.0;
	TestTrue(TEXT("get_property returns the vector as JSON"),
		Scale.IsValid() && Scale->TryGetObjectField(TEXT("value"), ScaleValue) && ScaleValue
		&& (*ScaleValue)->TryGetNumberField(TEXT("x"), ScaleX));
	TestEqual(TEXT("the value is element 1's scale"), ScaleX, 2.0);

	// A path without a key does not grow an indexedPath field.
	const TSharedPtr<FJsonObject> Plain = KeyedPathEditorGet(Registry, *Fixture, TEXT("Profiles[1].Rank"));
	TestTrue(TEXT("numeric read still works"), KeyedPathSucceeded(Plain));
	TestFalse(TEXT("numeric read reports no indexedPath"), Plain.IsValid() && Plain->HasField(TEXT("indexedPath")));

	Fixture->GetOutermost()->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPKeyedPathAssetReadTest,
	"UE.MCP.Property.KeyedPath.AssetRead",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPKeyedPathAssetReadTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ FString(MCPKeyedPathMountRoot), TEXT("UEMCPKeyedPathTest") };
	TStrongObjectPtr<UUEMCPKeyedPathTestObject> Fixture = KeyedPathMountedFixture();

	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);

	// asset(get_properties) with valueFormat=json: one FTransform, not 48 rows.
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("assetPath"), Fixture->GetPathName());
	Params->SetStringField(TEXT("propertyName"), TEXT("Profiles[ProfileId=SGK_Stone_Axe].Presentation.MeshOffset"));
	Params->SetStringField(TEXT("valueFormat"), TEXT("json"));
	const TSharedPtr<FJsonObject> Offset = KeyedPathCall(Registry, TEXT("get_asset_properties"), Params);
	TestTrue(FString::Printf(TEXT("get_properties reads the keyed struct (%s)"), *KeyedPathString(Offset, TEXT("error"))), KeyedPathSucceeded(Offset));
	TestEqual(TEXT("get_properties reports the index"),
		KeyedPathString(Offset, TEXT("indexedPath")), FString(TEXT("Profiles[0].Presentation.MeshOffset")));
	TestEqual(TEXT("get_properties reports the leaf type"), KeyedPathString(Offset, TEXT("type")), FString(TEXT("FTransform")));
	TestTrue(TEXT("the transform comes back as a JSON object"),
		Offset.IsValid() && Offset->HasTypedField<EJson::Object>(TEXT("value")));

	// The map under the keyed element, whole, as the read-back half of a write.
	Params->SetStringField(TEXT("propertyName"), TEXT("Profiles[ProfileId=SGK_Stone_Spear].Presentation.SlotOffsets"));
	const TSharedPtr<FJsonObject> Map = KeyedPathCall(Registry, TEXT("get_asset_properties"), Params);
	TestTrue(TEXT("get_properties reads the map under a keyed element"), KeyedPathSucceeded(Map));
	double PairCount = 0.0;
	TestTrue(TEXT("map read reports its pair count"), Map.IsValid() && Map->TryGetNumberField(TEXT("mapPairCount"), PairCount));
	TestEqual(TEXT("map read holds the one pair"), PairCount, 1.0);

	Fixture->GetOutermost()->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPKeyedPathWriteTest,
	"UE.MCP.Property.KeyedPath.Write",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPKeyedPathWriteTest::RunTest(const FString& Parameters)
{
	// editor(set_property): nested struct leaf under a keyed element.
	{
		TStrongObjectPtr<UUEMCPKeyedPathTestObject> Fixture = KeyedPathTempFixture();
		UPackage* Package = Fixture->GetOutermost();
		FMCPHandlerRegistry Registry;
		FEditorHandlers::RegisterHandlers(Registry);

		TSharedPtr<FJsonObject> Scale = MakeShared<FJsonObject>();
		Scale->SetNumberField(TEXT("X"), 3);
		Scale->SetNumberField(TEXT("Y"), 4);
		Scale->SetNumberField(TEXT("Z"), 5);
		const TSharedPtr<FJsonObject> Response = KeyedPathEditorSet(Registry, *Fixture,
			TEXT("Profiles[ProfileId=SGK_Stone_Axe].Presentation.MeshOffset.Scale3D"), MakeShared<FJsonValueObject>(Scale));
		TestTrue(FString::Printf(TEXT("set_property writes through the key (%s)"), *KeyedPathString(Response, TEXT("error"))), KeyedPathSucceeded(Response));
		TestTrue(TEXT("the keyed element changed"),
			Fixture->Profiles[0].Presentation.MeshOffset.GetScale3D().Equals(FVector(3.0, 4.0, 5.0)));
		TestTrue(TEXT("its sibling element did not"),
			Fixture->Profiles[1].Presentation.MeshOffset.GetScale3D().Equals(FVector(2.0)));
		TestTrue(TEXT("the translation beside the leaf did not"),
			Fixture->Profiles[0].Presentation.MeshOffset.GetTranslation().Equals(FVector::ZeroVector));
		TestTrue(TEXT("the write marked the package dirty"), Package->IsDirty());
		TestEqual(TEXT("the response keeps the caller's path"),
			KeyedPathString(Response, TEXT("propertyName")), FString(TEXT("Profiles[ProfileId=SGK_Stone_Axe].Presentation.MeshOffset.Scale3D")));
		TestEqual(TEXT("the response reports the index"),
			KeyedPathString(Response, TEXT("indexedPath")), FString(TEXT("Profiles[0].Presentation.MeshOffset.Scale3D")));
		TestEqual(TEXT("the rollback addresses the index"),
			KeyedPathRollbackName(Response), FString(TEXT("Profiles[0].Presentation.MeshOffset.Scale3D")));
		Package->SetDirtyFlag(false);
	}

	// asset(set_property): the save flag is honoured and a write that renames
	// the key rolls back by index.
	{
		const FMCPScopedTestMount Mount{ FString(MCPKeyedPathMountRoot), TEXT("UEMCPKeyedPathTest") };
		TStrongObjectPtr<UUEMCPKeyedPathTestObject> Fixture = KeyedPathMountedFixture();
		UPackage* Package = Fixture->GetOutermost();
		FMCPHandlerRegistry Registry;
		FAssetHandlers::RegisterHandlers(Registry);

		TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
		Params->SetStringField(TEXT("assetPath"), Fixture->GetPathName());
		Params->SetStringField(TEXT("propertyName"), TEXT("Profiles[ProfileId=SGK_Stone_Knife].ProfileId"));
		Params->SetStringField(TEXT("value"), TEXT("SGK_Stone_Dagger"));
		Params->SetBoolField(TEXT("save"), false);
		const TSharedPtr<FJsonObject> Response = KeyedPathCall(Registry, TEXT("set_asset_property"), Params);
		TestTrue(FString::Printf(TEXT("set_asset_property writes through the key (%s)"), *KeyedPathString(Response, TEXT("error"))), KeyedPathSucceeded(Response));
		TestEqual(TEXT("the key itself was renamed"), Fixture->Profiles[1].ProfileId.ToString(), FString(TEXT("SGK_Stone_Dagger")));
		bool bPersisted = true;
		TestTrue(TEXT("save=false reports persisted"), Response.IsValid() && Response->TryGetBoolField(TEXT("persisted"), bPersisted));
		TestFalse(TEXT("save=false is not persisted"), bPersisted);
		TestTrue(TEXT("save=false leaves the package dirty"), Package->IsDirty());
		TestEqual(TEXT("the rollback addresses the index, since the old key no longer matches"),
			KeyedPathRollbackName(Response), FString(TEXT("Profiles[1].ProfileId")));

		// The old key is gone and the new one answers.
		FProperty* Prop = nullptr;
		void* Addr = nullptr;
		MCPJsonProperty::FResolvedPathInfo Info;
		FString Error;
		TestFalse(TEXT("the old key no longer matches"),
			KeyedPathResolve(Fixture.Get(), TEXT("Profiles[ProfileId=SGK_Stone_Knife].Rank"), Prop, Addr, Info, Error));
		TestTrue(TEXT("the new key does"),
			KeyedPathResolve(Fixture.Get(), TEXT("Profiles[ProfileId=SGK_Stone_Dagger].Rank"), Prop, Addr, Info, Error));
		Package->SetDirtyFlag(false);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPKeyedPathBulkWriteTest,
	"UE.MCP.Property.KeyedPath.BulkWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPKeyedPathBulkWriteTest::RunTest(const FString& Parameters)
{
	const FMCPScopedTestMount Mount{ FString(MCPKeyedPathMountRoot), TEXT("UEMCPKeyedPathTest") };
	TStrongObjectPtr<UUEMCPKeyedPathTestObject> Fixture = KeyedPathMountedFixture();
	FMCPHandlerRegistry Registry;
	FAssetHandlers::RegisterHandlers(Registry);

	// Both properties select by the same key and the first renames it. Keys are
	// matched once in preflight, so the second still lands on the same element.
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	Properties->SetStringField(TEXT("Profiles[ProfileId=SGK_Stone_Axe].ProfileId"), TEXT("SGK_Stone_Hatchet"));
	Properties->SetNumberField(TEXT("Profiles[ProfileId=SGK_Stone_Axe].Rank"), 9);
	TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
	Item->SetStringField(TEXT("assetPath"), Fixture->GetPathName());
	Item->SetObjectField(TEXT("properties"), Properties);
	TArray<TSharedPtr<FJsonValue>> Items;
	Items.Add(MakeShared<FJsonValueObject>(Item));
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetArrayField(TEXT("items"), Items);
	Params->SetBoolField(TEXT("save"), false);

	const TSharedPtr<FJsonObject> Response = KeyedPathCall(Registry, TEXT("bulk_set_asset_properties"), Params);
	TestTrue(FString::Printf(TEXT("bulk write succeeds (%s)"), *KeyedPathString(Response, TEXT("error"))), KeyedPathSucceeded(Response));
	TestEqual(TEXT("the key was renamed"), Fixture->Profiles[0].ProfileId.ToString(), FString(TEXT("SGK_Stone_Hatchet")));
	TestEqual(TEXT("the second write reached the same element"), Fixture->Profiles[0].Rank, 9);
	TestEqual(TEXT("no other element changed"), Fixture->Profiles[1].Rank, 2);
	TestTrue(TEXT("save=false leaves the package dirty"), Fixture->GetOutermost()->IsDirty());

	// The rollback names the indices.
	const TSharedPtr<FJsonObject>* Rollback = nullptr;
	const TSharedPtr<FJsonObject>* Payload = nullptr;
	const TArray<TSharedPtr<FJsonValue>>* RollbackItems = nullptr;
	if (TestTrue(TEXT("bulk write emits a rollback"),
			Response.IsValid() && Response->TryGetObjectField(TEXT("rollback"), Rollback) && Rollback
			&& (*Rollback)->TryGetObjectField(TEXT("payload"), Payload) && Payload
			&& (*Payload)->TryGetArrayField(TEXT("items"), RollbackItems) && RollbackItems && RollbackItems->Num() == 1))
	{
		const TSharedPtr<FJsonObject>* RollbackProperties = nullptr;
		(*RollbackItems)[0]->AsObject()->TryGetObjectField(TEXT("properties"), RollbackProperties);
		TestTrue(TEXT("rollback restores the key by index"),
			RollbackProperties && (*RollbackProperties)->HasField(TEXT("Profiles[0].ProfileId")));
		TestTrue(TEXT("rollback restores the rank by index"),
			RollbackProperties && (*RollbackProperties)->HasField(TEXT("Profiles[0].Rank")));
	}

	Fixture->GetOutermost()->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPKeyedPathIndexRegressionTest,
	"UE.MCP.Property.KeyedPath.NumericIndexUnchanged",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPKeyedPathIndexRegressionTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UUEMCPKeyedPathTestObject> Fixture = KeyedPathTempFixture();
	FProperty* Prop = nullptr;
	void* Addr = nullptr;
	MCPJsonProperty::FResolvedPathInfo Info;
	FString Error;

	TestTrue(TEXT("numeric index resolves"), KeyedPathResolve(Fixture.Get(), TEXT("Profiles[2].Rank"), Prop, Addr, Info, Error));
	TestTrue(TEXT("numeric index lands on element 2"), Addr == static_cast<void*>(&Fixture->Profiles[2].Rank));
	TestFalse(TEXT("no key selector reported"), Info.bUsedKeySelector);

	TestTrue(TEXT("numeric index into a nested struct leaf resolves"),
		KeyedPathResolve(Fixture.Get(), TEXT("Profiles[2].Presentation.MeshOffset.Scale3D"), Prop, Addr, Info, Error));
	TestEqual(TEXT("the leaf is Scale3D"), Prop ? Prop->GetName() : FString(), FString(TEXT("Scale3D")));
	TestEqual(TEXT("the leaf holds element 2's scale"),
		Addr ? *static_cast<const FVector*>(Addr) : FVector::ZeroVector, FVector(3.0));
	TestEqual(TEXT("indexed form of an indexed path is itself"),
		Info.IndexedPath, FString(TEXT("Profiles[2].Presentation.MeshOffset.Scale3D")));

	TestFalse(TEXT("out of range is still refused"), KeyedPathResolve(Fixture.Get(), TEXT("Profiles[9].Rank"), Prop, Addr, Info, Error));
	TestTrue(TEXT("with the same message"), Error.Contains(TEXT("index 9 out of range on 'Profiles' (num=4)")));

	TestFalse(TEXT("indexing a non-array is still refused"), KeyedPathResolve(Fixture.Get(), TEXT("Plain[0]"), Prop, Addr, Info, Error));
	TestTrue(TEXT("with the same message"), Error.Contains(TEXT("'Plain' is not an array but was indexed [0]")));

	// Before selectors, "[abc]" went through Atoi and silently meant [0].
	TestFalse(TEXT("a non-numeric, non-key selector no longer means element 0"),
		KeyedPathResolve(Fixture.Get(), TEXT("Profiles[abc].Rank"), Prop, Addr, Info, Error));

	// The existing instanced-struct paths, by index and by key, nested.
	UPackage* Package = CreatePackage(*FString::Printf(
		TEXT("/Temp/UEMCPKeyedPathTests/%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	TStrongObjectPtr<UUEMCPInstancedStructPathTestObject> Ops(NewObject<UUEMCPInstancedStructPathTestObject>(Package, TEXT("Ops")));
	Ops->OpStack.SetNum(2);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Ops->OpStack[Index].InitializeAs<FUEMCPInstancedStructPathPayload>();
		FUEMCPInstancedStructPathPayload& Op = Ops->OpStack[Index].GetMutable<FUEMCPInstancedStructPathPayload>();
		Op.Scalar = 101 + Index * 100;
		Op.Elements.SetNum(2);
		Op.Elements[0].Sibling = FString::Printf(TEXT("op-%d-first"), Index);
		Op.Elements[1].Sibling = FString::Printf(TEXT("op-%d-second"), Index);
		Op.Elements[1].Scalar = 121 + Index * 100;
	}
	TestTrue(TEXT("numeric instanced-struct path resolves"),
		KeyedPathResolve(Ops.Get(), TEXT("OpStack[1].Elements[1].Scalar"), Prop, Addr, Info, Error));
	void* const Expected = &Ops->OpStack[1].GetMutable<FUEMCPInstancedStructPathPayload>().Elements[1].Scalar;
	TestTrue(TEXT("numeric instanced-struct path lands"), Addr == Expected);
	TestTrue(FString::Printf(TEXT("nested keys through instanced-struct payloads resolve (%s)"), *Error),
		KeyedPathResolve(Ops.Get(), TEXT("OpStack[Scalar=201].Elements[Sibling=op-1-second].Scalar"), Prop, Addr, Info, Error));
	TestTrue(TEXT("nested keys land on the same leaf"), Addr == Expected);
	TestEqual(TEXT("nested keys report both indices"), Info.IndexedPath, FString(TEXT("OpStack[1].Elements[1].Scalar")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMCPKeyedPathRefusalTest,
	"UE.MCP.Property.KeyedPath.Refusals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMCPKeyedPathRefusalTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UUEMCPKeyedPathTestObject> Fixture = KeyedPathTempFixture();
	FProperty* Prop = nullptr;
	void* Addr = nullptr;
	MCPJsonProperty::FResolvedPathInfo Info;
	FString Error;

	// No match: the error lists the values that exist.
	TestFalse(TEXT("an unknown key is refused"),
		KeyedPathResolve(Fixture.Get(), TEXT("Profiles[ProfileId=SGK_Stone_Hammer].Rank"), Prop, Addr, Info, Error));
	TestTrue(TEXT("no-match names the array"), Error.Contains(TEXT("no element of 'Profiles' (4 elements) has ProfileId == 'SGK_Stone_Hammer'")));
	TestTrue(TEXT("no-match lists the candidates"), Error.Contains(TEXT("SGK_Stone_Axe, SGK_Stone_Knife, SGK_Iron_Sword, SGK_Stone_Spear")));

	TestFalse(TEXT("keys match case-sensitively"),
		KeyedPathResolve(Fixture.Get(), TEXT("Profiles[ProfileId=sgk_stone_axe].Rank"), Prop, Addr, Info, Error));
	TestTrue(TEXT("a case-only miss is pointed out"), Error.Contains(TEXT("'SGK_Stone_Axe' differs only in case")));

	// Ambiguous: refused, with the indices to pick from.
	TestFalse(TEXT("a key matching two elements is refused"),
		KeyedPathResolve(Fixture.Get(), TEXT("Profiles[Rank=2].Label"), Prop, Addr, Info, Error));
	TestTrue(TEXT("ambiguity counts the matches"), Error.Contains(TEXT("matches 2 elements of 'Profiles' (indices 1, 2)")));
	TestTrue(TEXT("ambiguity suggests an index"), Error.Contains(TEXT("'Profiles[1]'")));

	// And through a write: an ambiguous key changes nothing.
	{
		FMCPHandlerRegistry Registry;
		FEditorHandlers::RegisterHandlers(Registry);
		const TSharedPtr<FJsonObject> Response = KeyedPathEditorSet(Registry, *Fixture,
			TEXT("Profiles[Slot=Back].Label"), MakeShared<FJsonValueString>(TEXT("Changed")));
		TestFalse(TEXT("an ambiguous keyed write fails"), KeyedPathSucceeded(Response));
		TestTrue(TEXT("the write error is the ambiguity"), KeyedPathString(Response, TEXT("error")).Contains(TEXT("indices 0, 2")));
		TestEqual(TEXT("first candidate untouched"), Fixture->Profiles[0].Label, FString(TEXT("Stone Axe")));
		TestEqual(TEXT("second candidate untouched"), Fixture->Profiles[2].Label, FString(TEXT("Sword.Long")));
		TestFalse(TEXT("a refused write does not dirty the package"), Fixture->GetOutermost()->IsDirty());
	}

	// Field problems.
	TestFalse(TEXT("an unknown key field is refused"),
		KeyedPathResolve(Fixture.Get(), TEXT("Profiles[WeaponId=SGK_Stone_Axe].Rank"), Prop, Addr, Info, Error));
	TestTrue(TEXT("unknown field names the usable keys"),
		Error.Contains(TEXT("has a field named 'WeaponId'")) && Error.Contains(TEXT("ProfileId")) && !Error.Contains(TEXT("Weight")));
	TestFalse(TEXT("a float field is not a key"),
		KeyedPathResolve(Fixture.Get(), TEXT("Profiles[Weight=0.5].Rank"), Prop, Addr, Info, Error));
	TestTrue(TEXT("float refusal says why"), Error.Contains(TEXT("cannot be a key")));
	TestFalse(TEXT("a struct field is not a key"),
		KeyedPathResolve(Fixture.Get(), TEXT("Profiles[Presentation=x].Rank"), Prop, Addr, Info, Error));

	// Containers the selector does not address.
	TestFalse(TEXT("a map is not selected by key"), KeyedPathResolve(Fixture.Get(), TEXT("Lookup[Key=A]"), Prop, Addr, Info, Error));
	TestTrue(TEXT("map refusal says to use the whole map"), Error.Contains(TEXT("TMap")));
	TestFalse(TEXT("a scalar is not selected by key"), KeyedPathResolve(Fixture.Get(), TEXT("Plain[K=1]"), Prop, Addr, Info, Error));
	TestTrue(TEXT("scalar refusal names it"), Error.Contains(TEXT("'Plain' is not an array")));

	// The candidate list is bounded.
	for (int32 Index = 0; Index < 30; ++Index)
	{
		FUEMCPKeyedPathProfile& Extra = Fixture->Profiles.AddDefaulted_GetRef();
		Extra.ProfileId = FName(*FString::Printf(TEXT("Extra_%02d"), Index));
	}
	TestFalse(TEXT("an unknown key on a long array is refused"),
		KeyedPathResolve(Fixture.Get(), TEXT("Profiles[ProfileId=Nope].Rank"), Prop, Addr, Info, Error));
	TestTrue(TEXT("the list stops at 20 and counts the rest"), Error.Contains(TEXT("... 14 more]")));
	TestFalse(TEXT("values past the bound are not listed"), Error.Contains(TEXT("Extra_29")));

	Fixture->GetOutermost()->SetDirtyFlag(false);
	return true;
}

#endif
