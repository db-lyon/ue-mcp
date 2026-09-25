#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/GasHandlers.h"
#include "HandlerRegistry.h"
#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemTestAttributeSet.h"
#include "AttributeSet.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"

/**
 * #956: gas(get_live_attribute_value) / gas(set_live_attribute_value) must
 * reach the attribute set instance REGISTERED on the actor's ASC, never a
 * second one built alongside it.
 *
 * The case that makes this hard is the one these tests reproduce: an actor in
 * a world that has not begun play never runs InitializeComponent, so its ASC
 * has no registered sets at all even though the actor owns one. The only
 * correct answer there is to register the actor's OWN instance. Constructing a
 * fresh set instead compiles, runs, reports success, and writes to an object
 * the gameplay code never reads, which is why the pointer-identity assertion
 * below is the point of the whole file.
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGasAdoptOwnerAttributeSetsTest,
	"UE.MCP.Gas.LiveAttributes.AdoptsTheActorsOwnSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGasAdoptOwnerAttributeSetsTest::RunTest(const FString& Parameters)
{
	// A private world, so nothing here touches whatever map the user has open.
	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, /*bInformEngineOfWorld=*/false);
	if (!World)
	{
		AddError(TEXT("could not create a test world"));
		return false;
	}
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
	};

	AActor* Actor = World->SpawnActor<AActor>();
	if (!Actor)
	{
		AddError(TEXT("could not spawn the test actor"));
		return false;
	}

	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Actor);
	ASC->RegisterComponent();

	// The actor's own attribute set, outered to the actor exactly the way a
	// CreateDefaultSubobject-created one is. This is the object the gameplay
	// code would consult once the ASC knows about it.
	// Constructed through the class pointer rather than the templated form:
	// UAbilitySystemTestAttributeSet is MinimalAPI, so its class registration is
	// exported and its constructor is not.
	UAttributeSet* OwnSet = NewObject<UAttributeSet>(
		Actor, UAbilitySystemTestAttributeSet::StaticClass());

	// The whole premise: the world never began play, so nothing registered it.
	TestNull(
		TEXT("nothing is registered before adoption, even though the actor owns a set"),
		ASC->GetAttributeSet(UAbilitySystemTestAttributeSet::StaticClass()));

	TArray<FString> Adopted;
	const int32 Count = MCPGas::AdoptOwnerAttributeSets(ASC, Actor, Adopted);
	TestEqual(TEXT("one set was newly registered"), Count, 1);
	TestEqual(TEXT("the registered set is named in the report"), Adopted.Num(), 1);

	const UAttributeSet* Registered =
		ASC->GetAttributeSet(UAbilitySystemTestAttributeSet::StaticClass());
	TestNotNull(TEXT("the ASC now has a registered set"), Registered);

	// THE assertion. A second, disconnected instance would satisfy every other
	// check in this file and fail only this one.
	TestTrue(
		TEXT("the registered instance IS the actor's own object, not a copy"),
		Registered == OwnSet);

	// Idempotent: a second pass registers nothing and does not duplicate.
	TArray<FString> Again;
	TestEqual(TEXT("re-adopting registers nothing new"),
		MCPGas::AdoptOwnerAttributeSets(ASC, Actor, Again), 0);
	TestEqual(TEXT("the set appears exactly once"),
		ASC->GetSpawnedAttributes().Num(), 1);

	// A value written through the registered instance is the value the ASC's
	// own accessor reads back, which is what "the gameplay code consults it"
	// means in practice.
	FStructProperty* ManaProp = MCPGas::FindAttributeDataProperty(
		UAbilitySystemTestAttributeSet::StaticClass(), TEXT("Mana"));
	TestNotNull(TEXT("Mana is found as an FGameplayAttributeData property"), ManaProp);
	if (ManaProp)
	{
		const FGameplayAttribute Mana(ManaProp);
		float NewValue = 42.0f;
		Mana.SetNumericValueChecked(NewValue, const_cast<UAttributeSet*>(Registered));
		// Read back through the ASC, which resolves the attribute's set on its
		// own. It agreeing with the actor's object is the operational form of
		// "the gameplay code consults this instance", and it is what a second,
		// disconnected set would break.
		TestEqual(TEXT("the ASC reads back what was written to the actor's own instance"),
			ASC->GetNumericAttribute(Mana), Mana.GetNumericValue(OwnSet));
		TestEqual(TEXT("the written value is what landed"),
			Mana.GetNumericValue(OwnSet), 42.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGasAttributeDataPropertyLookupTest,
	"UE.MCP.Gas.LiveAttributes.AttributePropertyLookup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGasAttributeDataPropertyLookupTest::RunTest(const FString& Parameters)
{
	UClass* SetClass = UAbilitySystemTestAttributeSet::StaticClass();

	TestNotNull(TEXT("bare property name resolves"),
		MCPGas::FindAttributeDataProperty(SetClass, TEXT("Mana")));
	TestNotNull(TEXT("Set.Property spelling resolves"),
		MCPGas::FindAttributeDataProperty(SetClass, TEXT("AbilitySystemTestAttributeSet.Mana")));
	TestNotNull(TEXT("Set:Property spelling resolves"),
		MCPGas::FindAttributeDataProperty(SetClass, TEXT("AbilitySystemTestAttributeSet:Mana")));

	// Plain float UPROPERTYs on an attribute set are not FGameplayAttributeData
	// and are not what these actions address, so they must miss rather than
	// resolve to something the caller cannot read a base value from.
	TestNull(TEXT("a plain float property is not an attribute data property"),
		MCPGas::FindAttributeDataProperty(SetClass, TEXT("Health")));
	TestNull(TEXT("an unknown name misses"),
		MCPGas::FindAttributeDataProperty(SetClass, TEXT("NoSuchAttribute")));
	TestNull(TEXT("a null class misses rather than crashing"),
		MCPGas::FindAttributeDataProperty(nullptr, TEXT("Mana")));

	const FString Names = MCPGas::ListAttributeDataPropertyNames(SetClass);
	TestTrue(TEXT("the miss message lists the real attribute names"), Names.Contains(TEXT("Mana")));
	TestEqual(TEXT("a null class lists nothing"),
		*MCPGas::ListAttributeDataPropertyNames(nullptr), TEXT("(none)"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGasLiveAttributeRegistrationTest,
	"UE.MCP.Gas.LiveAttributes.RegistrationAndPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGasLiveAttributeRegistrationTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FGasHandlers::RegisterHandlers(Registry);

	TestTrue(TEXT("get_live_attribute_value is registered"),
		Registry.HasHandler(TEXT("get_live_attribute_value")));
	TestTrue(TEXT("set_live_attribute_value is registered"),
		Registry.HasHandler(TEXT("set_live_attribute_value")));

	// Preflight runs before any world or actor lookup, so this is safe with no
	// editor world and no PIE session.
	const TSharedPtr<FJsonValue> Response =
		Registry.ExecuteHandler(TEXT("get_live_attribute_value"), MakeShared<FJsonObject>());
	TestTrue(TEXT("preflight returns an object"), Response.IsValid() && Response->Type == EJson::Object);
	if (Response.IsValid() && Response->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject> Object = Response->AsObject();
		TestFalse(TEXT("preflight is unsuccessful"), Object->GetBoolField(TEXT("success")));
		TestTrue(TEXT("preflight names the first required field"),
			Object->GetStringField(TEXT("error")).Contains(TEXT("attributeSet")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FGasLooseTagRegistrationTest,
	"UE.MCP.Gas.LooseTags.RegistrationAndPreflight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGasLooseTagRegistrationTest::RunTest(const FString& Parameters)
{
	FMCPHandlerRegistry Registry;
	FGasHandlers::RegisterHandlers(Registry);

	for (const TCHAR* Method : { TEXT("add_loose_gameplay_tag"), TEXT("remove_loose_gameplay_tag") })
	{
		TestTrue(FString::Printf(TEXT("%s is registered"), Method), Registry.HasHandler(Method));

		// #1104: tag and count are checked before any world lookup.
		const TSharedPtr<FJsonValue> Missing = Registry.ExecuteHandler(Method, MakeShared<FJsonObject>());
		if (TestTrue(TEXT("preflight returns an object"), Missing.IsValid() && Missing->Type == EJson::Object))
		{
			TestFalse(TEXT("a missing tag is refused"), Missing->AsObject()->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the refusal names the tag parameter"),
				Missing->AsObject()->GetStringField(TEXT("error")).Contains(TEXT("tag")));
		}

		TSharedPtr<FJsonObject> ZeroCount = MakeShared<FJsonObject>();
		ZeroCount->SetStringField(TEXT("tag"), TEXT("Any.Tag"));
		ZeroCount->SetNumberField(TEXT("count"), 0);
		const TSharedPtr<FJsonValue> Zero = Registry.ExecuteHandler(Method, ZeroCount);
		if (TestTrue(TEXT("count preflight returns an object"), Zero.IsValid() && Zero->Type == EJson::Object))
		{
			TestFalse(TEXT("count 0 is refused"), Zero->AsObject()->GetBoolField(TEXT("success")));
			TestTrue(TEXT("the refusal names count"),
				Zero->AsObject()->GetStringField(TEXT("error")).Contains(TEXT("count")));
		}
	}

	return true;
}

#endif
