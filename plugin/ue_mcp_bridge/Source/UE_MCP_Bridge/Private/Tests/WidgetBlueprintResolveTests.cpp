#if WITH_DEV_AUTOMATION_TESTS

#include "Handlers/WidgetHandlers.h"
#include "Misc/AutomationTest.h"
#include "Engine/Blueprint.h"
#include "WidgetBlueprint.h"

/**
 * #972: every widget action resolves its WidgetBlueprint through one shared
 * resolver that normalises the path, revalidates whatever it finds, and says
 * which of the three failures happened. These cases pin the two halves that do
 * not need an asset on disk: path normalisation, and the wording that lets a
 * caller tell "there is no such asset" from "the handle went stale".
 */

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWidgetBlueprintResolvePathTest,
	"UE.MCP.Widget.Resolve.ObjectPathNormalisation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWidgetBlueprintResolvePathTest::RunTest(const FString& Parameters)
{
	// The engine's own asset loader logs an Error every time it is asked for a
	// path that names nothing, and the automation framework fails a test on any
	// unexpected Error. Asserting what a deliberate miss reports therefore
	// failed the test doing the asserting. Declared the way SequencerHandlerTests
	// declares it; count 0 means it must occur, so a test that stops
	// provoking it fails.
	AddExpectedError(TEXT("LoadAsset failed"), EAutomationExpectedErrorFlags::Contains, 0);

	using namespace MCPWidget;

	const FString Expected = TEXT("/Game/UI/MCPResolveProbe.MCPResolveProbe");

	// A package path with no object part gets one inferred, which is what the
	// object hash needs before it can answer at all.
	const FString FromPackagePath = ResolveWidgetBlueprint(TEXT("/Game/UI/MCPResolveProbe")).ObjectPath;
	TestEqual(TEXT("package path gains the object part"), *FromPackagePath, *Expected);

	// Already an object path: unchanged.
	const FString FromObjectPath =
		ResolveWidgetBlueprint(TEXT("/Game/UI/MCPResolveProbe.MCPResolveProbe")).ObjectPath;
	TestEqual(TEXT("object path passes through"), *FromObjectPath, *Expected);

	// Export text form, which agents and the engine both emit.
	const FString FromExportText =
		ResolveWidgetBlueprint(TEXT("WidgetBlueprint'/Game/UI/MCPResolveProbe.MCPResolveProbe'")).ObjectPath;
	TestEqual(TEXT("export text form is unwrapped"), *FromExportText, *Expected);

	// The generated class path names the class, not the asset. The resolver
	// answers with the blueprint behind it, so the _C comes off the path.
	const FString FromGeneratedClass =
		ResolveWidgetBlueprint(TEXT("/Game/UI/MCPResolveProbe.MCPResolveProbe_C")).ObjectPath;
	TestEqual(TEXT("generated class suffix is dropped"), *FromGeneratedClass, *Expected);

	// A subobject path resolves to its owning asset.
	const FString FromSubobject =
		ResolveWidgetBlueprint(TEXT("/Game/UI/MCPResolveProbe.MCPResolveProbe:Inner")).ObjectPath;
	TestEqual(TEXT("subobject part is dropped"), *FromSubobject, *Expected);

	// Nothing above exists, so every one of them is NotFound rather than
	// Unresolvable. That is the distinction the error wording depends on.
	const FWidgetBlueprintResolve Missing = ResolveWidgetBlueprint(TEXT("/Game/UI/MCPResolveProbe"));
	TestFalse(TEXT("probe asset does not exist"), Missing.bAssetExists);
	TestTrue(
		TEXT("a missing asset reports NotFound"),
		Missing.Failure == EWidgetBlueprintResolveFailure::NotFound);
	TestNull(TEXT("no blueprint is handed back"), Missing.Blueprint);

	// An empty path cannot be normalised and must not be treated as a stale
	// handle on something real.
	const FWidgetBlueprintResolve Empty = ResolveWidgetBlueprint(FString());
	TestTrue(
		TEXT("an empty path reports NotFound"),
		Empty.Failure == EWidgetBlueprintResolveFailure::NotFound);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWidgetBlueprintResolveErrorWordingTest,
	"UE.MCP.Widget.Resolve.ErrorsDistinguishMissingFromStale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

static FString WidgetResolveTestErrorText(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid() || Value->Type != EJson::Object) return FString();
	return Value->AsObject()->GetStringField(TEXT("error"));
}

bool FWidgetBlueprintResolveErrorWordingTest::RunTest(const FString& Parameters)
{
	using namespace MCPWidget;

	const FString Path = TEXT("/Game/UI/MCPResolveProbe");

	FWidgetBlueprintResolve NotFound;
	NotFound.Failure = EWidgetBlueprintResolveFailure::NotFound;
	const FString NotFoundText = WidgetResolveTestErrorText(WidgetBlueprintResolveError(Path, NotFound));
	TestTrue(TEXT("missing asset says no asset exists"), NotFoundText.Contains(TEXT("No asset exists")));
	TestTrue(TEXT("missing asset names the path"), NotFoundText.Contains(Path));

	FWidgetBlueprintResolve Stale;
	Stale.Failure = EWidgetBlueprintResolveFailure::Unresolvable;
	Stale.bAssetExists = true;
	const FString StaleText = WidgetResolveTestErrorText(WidgetBlueprintResolveError(Path, Stale));
	TestTrue(TEXT("stale handle says the asset exists"), StaleText.Contains(TEXT("exists but could not be resolved")));
	TestTrue(TEXT("stale handle names the recovery"), StaleText.Contains(TEXT("reload_bridge")));
	// The two must not read the same, because the caller's next move differs.
	TestNotEqual(TEXT("the two failures word differently"), *NotFoundText, *StaleText);

	FWidgetBlueprintResolve WrongType;
	WrongType.Failure = EWidgetBlueprintResolveFailure::WrongType;
	WrongType.FoundClass = TEXT("Material");
	const FString WrongTypeText = WidgetResolveTestErrorText(WidgetBlueprintResolveError(Path, WrongType));
	TestTrue(TEXT("wrong type names what was found"), WrongTypeText.Contains(TEXT("Material")));
	TestTrue(TEXT("wrong type says it is not a WidgetBlueprint"),
		WrongTypeText.Contains(TEXT("not a WidgetBlueprint")));

	// A broken asset is neither missing nor stale, and used to share the
	// "Failed to load WidgetBlueprint" wording with both.
	const FString TreeText = WidgetResolveTestErrorText(MissingWidgetTreeError(Path));
	TestTrue(TEXT("missing WidgetTree says the asset resolved"), TreeText.Contains(TEXT("no WidgetTree")));
	TestFalse(TEXT("missing WidgetTree is not reported as missing"), TreeText.Contains(TEXT("No asset exists")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWidgetBlueprintResolveWrongTypeTest,
	"UE.MCP.Widget.Resolve.RealAssetOfTheWrongType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWidgetBlueprintResolveWrongTypeTest::RunTest(const FString& Parameters)
{
	using namespace MCPWidget;

	// An asset that ships with the engine, is not a WidgetBlueprint, and is
	// already in memory in any editor session: the resolver must classify it
	// rather than claim the path does not exist.
	const FWidgetBlueprintResolve Resolved =
		ResolveWidgetBlueprint(TEXT("/Engine/EngineMaterials/DefaultMaterial"));
	TestNull(TEXT("a material is not handed back as a WidgetBlueprint"), Resolved.Blueprint);
	TestTrue(TEXT("the engine asset is found to exist"), Resolved.bAssetExists);
	TestTrue(
		TEXT("a real asset of the wrong type reports WrongType"),
		Resolved.Failure == EWidgetBlueprintResolveFailure::WrongType);
	TestFalse(TEXT("the class that was found is reported"), Resolved.FoundClass.IsEmpty());

	return true;
}

#endif
